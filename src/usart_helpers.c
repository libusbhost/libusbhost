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


#include "usart_helpers.h"
#define TINYPRINTF_OVERRIDE_LIBC 0
#define TINYPRINTF_DEFINE_TFP_SPRINTF 0
#include "tinyprintf.h"

#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <libopencm3/stm32/usart.h>

#define USART_FIFO_OUT_SIZE (4096)
uint8_t usart_fifo_out_data[USART_FIFO_OUT_SIZE];
uint32_t usart_fifo_out_len = 0;
uint32_t usart_fifo_out_index = 0;

#define USART_FIFO_IN_SIZE (1024)
uint8_t usart_fifo_in_data[USART_FIFO_IN_SIZE];
uint32_t usart_fifo_in_len = 0;
uint32_t usart_fifo_in_index = 0;

static uint32_t usart = 0;

static uint8_t usart_fifo_pop(void)
{
	uint8_t ret;
	usart_fifo_out_len--;
	ret = usart_fifo_out_data[usart_fifo_out_index];
	usart_fifo_out_index++;
	if (usart_fifo_out_index == USART_FIFO_OUT_SIZE ) {
		usart_fifo_out_index = 0;
	}
	return ret;
}

static void usart_fifo_push(uint8_t aData)
{
	uint32_t i;
	if( (usart_fifo_out_len + 1) == USART_FIFO_OUT_SIZE)//overflow
	{
		usart_fifo_out_len = 0;
		LOG_PRINTF("OVERFLOW!");
		return;
	}

	i = usart_fifo_out_index + usart_fifo_out_len;
	if (i >= USART_FIFO_OUT_SIZE) {
		i -= USART_FIFO_OUT_SIZE;
	}
	usart_fifo_out_data[i] = aData;
	usart_fifo_out_len++;
}


static uint8_t usart_fifo_in_pop(void)
{
	uint8_t ret;
	usart_fifo_in_len--;
	ret = usart_fifo_in_data[usart_fifo_in_index];
	usart_fifo_in_index++;
	if (usart_fifo_in_index == USART_FIFO_IN_SIZE ) {
		usart_fifo_in_index = 0;
	}
	return ret;
}

static void usart_fifo_in_push(uint8_t aData)
{
	uint32_t i;
	if( (usart_fifo_in_len + 1) == USART_FIFO_IN_SIZE)//overflow
	{
		usart_fifo_in_len = 0;
		return;
	}

	i = usart_fifo_in_index + usart_fifo_in_len;
	if (i >= USART_FIFO_IN_SIZE) {
		i -= USART_FIFO_IN_SIZE;
	}
	usart_fifo_in_data[i] = aData;
	usart_fifo_in_len++;
}

static void putf(void *arg, char c)
{
	//unused argument
	(void)arg;

	usart_fifo_push(c);
}

void usart_printf(const char *str, ...)
{
	va_list va;
	va_start(va, str);
	tfp_format(NULL, putf, str, va);
	va_end(va);
}

void usart_init(uint32_t arg_usart, uint32_t baudrate)
{
	usart_set_baudrate(arg_usart, baudrate);
	usart_set_databits(arg_usart, 8);
	usart_set_flow_control(arg_usart, USART_FLOWCONTROL_NONE);
	usart_set_mode(arg_usart, USART_MODE_TX | USART_MODE_RX);
	usart_set_parity(arg_usart, USART_PARITY_NONE);
	usart_set_stopbits(arg_usart, USART_STOPBITS_1);

	usart_enable_rx_interrupt(arg_usart);
	usart_enable(arg_usart);
	usart = arg_usart;
}

void usart_interrupt(void)
{
	if (usart_get_interrupt_source(usart, USART_SR_RXNE)) {
		uint8_t data = usart_recv(usart);
		usart_fifo_in_push(data);
		if ( data != 3 && data != '\r' && data != '\n') {
			usart_fifo_push(data);
		} else {
			LOG_PRINTF("\n>>");
		}
	}
}

void usart_fifo_send(void)
{
	while(usart_fifo_out_len) {
		uint8_t data = usart_fifo_pop();
		usart_wait_send_ready(usart);
		usart_send(usart, data);
	}
}
static char command[128];
static uint8_t command_len = 0;
static uint8_t command_argindex = 0;

static uint8_t usart_read_command(void)
{
	uint32_t fifo_len = usart_fifo_in_len;
	while (fifo_len) {
		uint8_t data = usart_fifo_in_pop();

		if ((data >= 'A') && (data <= 'Z')) {
			data += 'a'-'A';
		}

		if (((data >= 'a') && (data <= 'z')) || ((data >='0') && (data<='9'))) {
			command[command_len++] = data;
		} else if (data == ' ') {
			if (command_len) {
				if (command_argindex == 0) {
					command[command_len++] = 0;
					command_argindex = command_len;
				} else {
					command[command_len++] = ' ';
				}
			}
		} else if (data == '\r' || data == '\n') {
			if (command_len) {
				command[command_len++] = 0;
				if (!command_argindex) {
					command_argindex = command_len;
				}
				return 1;
			}
		} else if (data == 127) {
			if (command_len) {
				if (command_argindex) {
					if (command_len == command_argindex) {
						command_argindex = 0;
					}
				}
				command[command_len] = '\0';
				command_len--;
			}
		} else if (data == 3) {
			command_len = 0;
			command_argindex = 0;
		} else {
			LOG_PRINTF("%d ",data);
		}

		fifo_len--;
	}
	return 0;
}
void usart_call_cmd(struct usart_commands * commands)
{
	uint32_t i = 0;
	if(!usart_read_command()) {
		return;
	}
	if (!command_len) {
		LOG_PRINTF("#2");
		return;
	}

	i=0;
	while(commands[i].cmd != NULL) {
		if (!strcmp((char*)command, (char*)commands[i].cmd)) {
			if (commands[i].callback) {
				if(command_argindex == command_len) {
					commands[i].callback(NULL);
				} else {
					commands[i].callback(&command[command_argindex]);
				}
			}
			LOG_PRINTF("\n>>");
			command_len = 0;
			command_argindex = 0;
			return;
		} else {

		}
		i++;
	}
	command_len = 0;
	command_argindex = 0;
	LOG_PRINTF("INVALID COMMAND\n>>");
}
