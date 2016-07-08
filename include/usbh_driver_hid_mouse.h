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

#ifndef USBH_DRIVER_HID_MOUSE_
#define USBH_DRIVER_HID_MOUSE_

#include "usbh_core.h"

#include <stdint.h>

BEGIN_DECLS

struct _hid_mouse_config {
	/**
	 * @brief this is called when some data is read when polling the device
	 * @param device_id
	 * @param data pointer to the data (only 4 bytes are valid!)
	 */
	void (*mouse_in_message_handler)(uint8_t device_id, const uint8_t *data);
};
typedef struct _hid_mouse_config hid_mouse_config_t;

/**
 * @brief hid_mouse_driver_init initialization routine - this will initialize internal structures of this device driver
 * @param config
 */
void hid_mouse_driver_init(const hid_mouse_config_t *config);

extern const usbh_dev_driver_t usbh_hid_mouse_driver;

END_DECLS

#endif
