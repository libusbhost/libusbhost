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

#ifndef USBH_USART_HELPERS_H
#define USBH_USART_HELPERS_H

#include "usbh_core.h"
#include <stdint.h>
#include <stdarg.h>

BEGIN_DECLS

struct usart_commands{
	const char * cmd;
	void (*callback)(const char * arg);
};


#ifdef USART_DEBUG
void usart_init(uint32_t usart, uint32_t baudrate);
void usart_printf(const char *str, ...);
void usart_fifo_send(void);

void usart_call_cmd(struct usart_commands * commands);
void usart_interrupt(void);
#define LOG_PRINTF(format, ...) usart_printf(format, ##__VA_ARGS__);
#define LOG_FLUSH() usart_fifo_send()
#else
#define LOG_PRINTF(dummy, ...) ((void)dummy)
#define LOG_FLUSH()
#endif

END_DECLS

#endif
