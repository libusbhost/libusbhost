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
#include "usbh_hubbed.h"			/// provides usbh_init() and usbh_poll()
#include "usbh_lld_stm32f4.h"		/// provides low level usb host driver for stm32f4 platform
#include "usbh_driver_hid_mouse.h"	/// provides usb device driver Human Interface Device - type mouse
#include "usbh_driver_hub.h"		/// provides usb full speed hub driver (Low speed devices on hub are not supported)
#include "usbh_driver_gp_xbox.h"	/// provides usb device driver for Gamepad: Microsoft XBOX compatible Controller

 // STM32f407 compatible
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/stm32/timer.h>
#include <libopencm3/stm32/otg_hs.h>
#include <libopencm3/stm32/otg_fs.h>

#include <stdint.h>
#include <string.h>
#include <stdlib.h>

static inline void delay_ms_busy_loop(uint32_t ms)
{
	volatile uint32_t i;
	for (i = 0; i < 14903*ms; i++);
}


/* Set STM32 to 168 MHz. */
static void clock_setup(void)
{
	rcc_clock_setup_hse_3v3(&hse_8mhz_3v3[CLOCK_3V3_168MHZ]);

	// GPIO
	rcc_peripheral_enable_clock(&RCC_AHB1ENR, RCC_AHB1ENR_IOPAEN);	// OTG_FS + button
	rcc_peripheral_enable_clock(&RCC_AHB1ENR, RCC_AHB1ENR_IOPBEN);	// OTG_HS
	rcc_peripheral_enable_clock(&RCC_AHB1ENR, RCC_AHB1ENR_IOPCEN);	// USART + OTG_FS charge pump
	rcc_peripheral_enable_clock(&RCC_AHB1ENR, RCC_AHB1ENR_IOPDEN);	// LEDS

	// periphery
	rcc_peripheral_enable_clock(&RCC_APB2ENR, RCC_APB2ENR_USART6EN);// USART
	rcc_peripheral_enable_clock(&RCC_AHB2ENR, RCC_AHB2ENR_OTGFSEN);
	rcc_peripheral_enable_clock(&RCC_AHB1ENR, RCC_AHB1ENR_OTGHSEN);
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
	&usbh_hid_mouse_driver,
	&usbh_gp_xbox_driver,
	0
};

static void gp_xbox_update(uint8_t device_id, gp_xbox_packet_t packet)
{
	(void)device_id;
	(void)packet;
	LOG_PRINTF("update %d: %d %d \r\n", device_id, packet.axis_left_x, packet.buttons & GP_XBOX_BUTTON_A);
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

static void mouse_in_message_handler(uint8_t device_id, const uint8_t *data)
{
	(void)device_id;
	(void)data;
	// print only first 4 bytes, since every mouse should have at least these four set.
	// Report descriptors are not read by driver for now, so we do not know what each byte means
	LOG_PRINTF("MOUSE EVENT %02X %02X %02X %02X \r\n", data[0], data[1], data[2], data[3]);
}

static const hid_mouse_config_t mouse_config = {
	.mouse_in_message_handler = &mouse_in_message_handler
};

int main(void)
{
	clock_setup();
	gpio_setup();

#ifdef USART_DEBUG
	usart_init(USART6, 921600);
#endif
	LOG_PRINTF("\r\n\r\n\r\n\r\n\r\n###################\r\nInit\r\n");

	/**
	 * device driver initialization
	 *
	 * Pass configuration struct where the callbacks are defined
	 */
	hid_mouse_driver_init(&mouse_config);
	hub_driver_init();
	gp_xbox_driver_init(&gp_xbox_config);

	gpio_set(GPIOD,  GPIO13);

	/**
	 * Pass array of supported low level drivers
	 * In case of stm32f407, there are up to two supported OTG hosts on one chip.
	 * Each one can be enabled or disabled in config.mk - optimization for speed
	 *
	 * Pass array of supported device drivers
	 */
	usbh_init(usbh_lld_stm32f4_drivers, device_drivers);
	gpio_clear(GPIOD,  GPIO13);

	LOG_PRINTF("USB init complete\r\n");

	uint32_t i = 0;

	while (1) {
		LOG_FLUSH();

		// Toggle some led
		gpio_set(GPIOD,  GPIO14);
		usbh_poll(i);
		gpio_clear(GPIOD,  GPIO14);

		delay_ms_busy_loop(1);
		i += 1000;
	}

	return 0;
}
