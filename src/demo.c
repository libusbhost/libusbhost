/*
 * This file is part of the libusbhost library
 * hosted at http://github.com/libusbhost/libusbhost
 *
 * Copyright (C) 2015 Amir Hammad <amir.hammad@hotmail.com>
 *
 *
 * libusbhost is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "usart_helpers.h"			/// provides LOG_PRINTF macros used for debugging
#include "usbh_core.h"				/// provides usbh_init() and usbh_poll()
#include "usbh_lld_stm32f4.h"		/// provides low level usb host driver for stm32f4 platform
#include "usbh_driver_hid.h"		/// provides generic usb device driver for Human Interface Device (HID)
#include "usbh_driver_hub.h"		/// provides usb full speed hub driver (Low speed devices on hub are not supported)
#include "usbh_driver_gp_xbox.h"	/// provides usb device driver for Gamepad: Microsoft XBOX compatible Controller
#include "usbh_driver_ac_midi.h"	/// provides usb device driver for midi class devices

 // STM32f407 compatible
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/flash.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/stm32/timer.h>

#include <libopencm3/usb/dwc/otg_hs.h>
#include <libopencm3/usb/dwc/otg_fs.h>

#include <stdint.h>
#include <string.h>
#include <stdlib.h>


static inline void delay_ms_busy_loop(uint32_t ms)
{
	volatile uint32_t i;
	for (i = 0; i < 14903*ms; i++);
}

const struct rcc_clock_scale stm32f411xE_discovery = {
    /* 96MHz */
    .pllm = 4,   // RCC_PLLCFGR main pll division factor for VCO_Inputf = PLL input / PLLM (should end up to around 2MHz to limit pll jitter)
    .plln = 288, // RCC_PLLCFGR main pll multiplication factor for VCOf = VCO_Inputf * PLLN
    .pllp = 6,   // RCC_PLLCFGR main pll division factor for main system clock = VCOf / PLLP
    .pllq = 12,  // RCC_PLLCFGR main pll division factor for USB OTG FS = VCOf / PLLQ (must result in 48 MHz to work correctly)
    .pllr = 0,   // unused
    .flash_config = FLASH_ACR_DCEN | FLASH_ACR_ICEN | FLASH_ACR_LATENCY_3WS,
    .hpre = RCC_CFGR_HPRE_DIV_NONE,  // RCC_CFGR AHB Prescaler
    .ppre1 = RCC_CFGR_PPRE_DIV_2,    // RCC_CFGR APB1 Prescaler (low speed)
    .ppre2 = RCC_CFGR_PPRE_DIV_NONE, // RCC_CFGR APB2 Prescaler (high speed)
    .voltage_scale = PWR_SCALE1,     // PWR_CR -> Regulator voltage scaling output selection <= 100 MHz
    .ahb_frequency  = 96000000,
    .apb1_frequency = 48000000,
    .apb2_frequency = 96000000,


    ///* 120MHz */
    //.pllm = 8,
    //.plln = 240,
    //.pllp = 2,
    //.pllq = 5,
    //.pllr = 0,
    //.flash_config = FLASH_ACR_DCEN | FLASH_ACR_ICEN | FLASH_ACR_LATENCY_3WS,
    //.hpre = RCC_CFGR_HPRE_DIV_NONE,
    //.ppre1 = RCC_CFGR_PPRE_DIV_4,
    //.ppre2 = RCC_CFGR_PPRE_DIV_2,
    //.voltage_scale = PWR_SCALE1,
    //.ahb_frequency  = 120000000,
    //.apb1_frequency = 30000000,
    //.apb2_frequency = 60000000,
};

/* Set STM32 to 168 MHz. */
static void clock_setup(void)
{
	//rcc_clock_setup_hse_3v3(&rcc_hse_8mhz_3v3[RCC_CLOCK_3V3_84MHZ]);
	rcc_clock_setup_hse_3v3(&stm32f411xE_discovery);

	// GPIO
	rcc_periph_clock_enable(RCC_GPIOA); // OTG_FS + button
	rcc_periph_clock_enable(RCC_GPIOB); // OTG_HS
	rcc_periph_clock_enable(RCC_GPIOC); // USART + OTG_FS charge pump
	rcc_periph_clock_enable(RCC_GPIOD); // LEDS

	// periphery
	rcc_periph_clock_enable(RCC_USART6); // USART
	rcc_periph_clock_enable(RCC_OTGFS); // OTG_FS
	rcc_periph_clock_enable(RCC_OTGHS); // OTG_HS
	rcc_periph_clock_enable(RCC_TIM3); // TIM3
}


/*
 * setup 10kHz timer
 */
static void timer_setup(void)
{
	rcc_periph_reset_pulse(RST_TIM3);
	timer_set_prescaler(TIM3, 4800 - 1);	// 84Mhz/10kHz - 1
	timer_set_period(TIM3, 65535);			// Overflow in ~6.5 seconds
	timer_enable_counter(TIM3);
}

static uint32_t timer_get_time_us(void)
{
	uint32_t cnt = timer_get_counter(TIM3);

	// convert to 1MHz less precise timer value -> units: microseconds
	uint32_t time_us = cnt * 100;

	return time_us;
}

static void gpio_setup(void)
{
	/* Set GPIO12-15 (in GPIO port D) to 'output push-pull'. */
	gpio_mode_setup(GPIOD, GPIO_MODE_OUTPUT,
			GPIO_PUPD_NONE, GPIO12 | GPIO13 | GPIO14 | GPIO15);

	/* Set	 */
	gpio_mode_setup(GPIOC, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO0);
	gpio_clear(GPIOC, GPIO0);

	// OTG_FS
	gpio_mode_setup(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO11 | GPIO12);
	gpio_set_af(GPIOA, GPIO_AF10, GPIO11 | GPIO12);

	// OTG_HS
	gpio_mode_setup(GPIOB, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO15 | GPIO14);
	gpio_set_af(GPIOB, GPIO_AF12, GPIO14 | GPIO15);

	// USART TX
	gpio_mode_setup(GPIOC, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO6 | GPIO7);
	gpio_set_af(GPIOC, GPIO_AF8, GPIO6 | GPIO7);

	// button
	gpio_mode_setup(GPIOA, GPIO_MODE_INPUT, GPIO_PUPD_NONE, GPIO0);
}

static const usbh_dev_driver_t *device_drivers[] = {
	&usbh_hub_driver,
	&usbh_hid_driver,
	&usbh_gp_xbox_driver,
	&usbh_midi_driver,
	NULL
};

static const usbh_low_level_driver_t * const lld_drivers[] = {
#ifdef USE_STM32F4_USBH_DRIVER_FS
	&usbh_lld_stm32f4_driver_fs, // Make sure USE_STM32F4_USBH_DRIVER_FS is defined in usbh_config.h
#endif

#ifdef USE_STM32F4_USBH_DRIVER_HS
	&usbh_lld_stm32f4_driver_hs, // Make sure USE_STM32F4_USBH_DRIVER_HS is defined in usbh_config.h
#endif
	NULL
	};

static void gp_xbox_update(uint8_t device_id, gp_xbox_packet_t packet)
{
	(void)device_id;
	(void)packet;
	LOG_PRINTF("update %d: %d %d \n", device_id, packet.axis_left_x, packet.buttons & GP_XBOX_BUTTON_A);
}


static void gp_xbox_connected(uint8_t device_id)
{
	(void)device_id;
	LOG_PRINTF("connected %d", device_id);
}

static void gp_xbox_disconnected(uint8_t device_id)
{
	(void)device_id;
	LOG_PRINTF("disconnected %d", device_id);
}

static const gp_xbox_config_t gp_xbox_config = {
	.update = &gp_xbox_update,
	.notify_connected = &gp_xbox_connected,
	.notify_disconnected = &gp_xbox_disconnected
};

static void hid_in_message_handler(uint8_t device_id, const uint8_t *data, uint32_t length)
{
	(void)device_id;
	(void)data;
	if (length < 4) {
		LOG_PRINTF("data too short, type=%d\n", hid_get_type(device_id));
		return;
	}

	// print only first 4 bytes, since every mouse should have at least these four set.
	// Report descriptors are not read by driver for now, so we do not know what each byte means
	LOG_PRINTF("HID EVENT %02X %02X %02X %02X \n", data[0], data[1], data[2], data[3]);
	if (hid_get_type(device_id) == HID_TYPE_KEYBOARD) {
		static int x = 0;
		if (x != data[2]) {
			x = data[2];
			hid_set_report(device_id, x);
		}
	}
}

static const hid_config_t hid_config = {
	.hid_in_message_handler = &hid_in_message_handler
};

static void midi_in_message_handler(int device_id, uint8_t *data)
{
	(void)device_id;
	switch (data[1]>>4) {
	case 8:
		LOG_PRINTF("\r\nNote Off");
		break;

	case 9:
		LOG_PRINTF("\r\nNote On");
		break;

	default:
		break;
	}
}

const midi_config_t midi_config = {
	.read_callback = &midi_in_message_handler
};

int main(void)
{
	clock_setup();
	gpio_setup();

	// provides time_curr_us to usbh_poll function
	timer_setup();

#ifdef USART_DEBUG
	usart_init(USART6, 921600);
#endif
	LOG_PRINTF("\n\n\n\n\n###################\nInit\n");

	/**
	 * device driver initialization
	 *
	 * Pass configuration struct where the callbacks are defined
	 */
	hid_driver_init(&hid_config);
	hub_driver_init();
	gp_xbox_driver_init(&gp_xbox_config);
	midi_driver_init(&midi_config);

	gpio_set(GPIOD,  GPIO13);
	/**
	 * Pass array of supported low level drivers
	 * In case of stm32f407, there are up to two supported OTG hosts on one chip.
	 * Each one can be enabled or disabled in usbh_config.h - optimization for speed
	 *
	 * Pass array of supported device drivers
	 */
	usbh_init(lld_drivers, device_drivers);
	gpio_clear(GPIOD,  GPIO13);

	LOG_PRINTF("USB init complete\n");

	LOG_FLUSH();

	while (1) {
		// set busy led
		gpio_set(GPIOD,  GPIO14);

		uint32_t time_curr_us = timer_get_time_us();

		usbh_poll(time_curr_us);

		// clear busy led
		gpio_clear(GPIOD,  GPIO14);

		LOG_FLUSH();

		// approx 1ms interval between usbh_poll()
		delay_ms_busy_loop(1);
	}

	return 0;
}
