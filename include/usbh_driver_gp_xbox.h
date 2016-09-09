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

#ifndef USBH_DRIVER_GP_XBOX_
#define USBH_DRIVER_GP_XBOX_

#include "usbh_core.h"

#include <stdint.h>

BEGIN_DECLS

#define GP_XBOX_DPAD_TOP (1 << 0)
#define GP_XBOX_DPAD_LEFT (1 << 1)
#define GP_XBOX_DPAD_BOTTOM (1 << 2)
#define GP_XBOX_DPAD_RIGHT (1 << 3)
#define GP_XBOX_BUTTON_X (1 << 4)
#define GP_XBOX_BUTTON_Y (1 << 5)
#define GP_XBOX_BUTTON_A (1 << 6)
#define GP_XBOX_BUTTON_B (1 << 7)
#define GP_XBOX_BUTTON_SELECT (1 << 8)
#define GP_XBOX_BUTTON_START (1 << 9)
#define GP_XBOX_BUTTON_LT (1 << 10)
#define GP_XBOX_BUTTON_RT (1 << 11)
#define GP_XBOX_BUTTON_XBOX (1 << 12)
#define GP_XBOX_BUTTON_AXIS_LEFT (1 << 13)
#define GP_XBOX_BUTTON_AXIS_RIGHT (1 << 14)

struct _gp_xbox_packet {
	uint32_t buttons;
	int16_t axis_left_x;
	int16_t axis_left_y;
	int16_t axis_right_x;
	int16_t axis_right_y;
	uint8_t axis_rear_left;
	uint8_t axis_rear_right;
};
typedef struct _gp_xbox_packet gp_xbox_packet_t;


struct _gp_xbox_config {
	void (*update)(uint8_t device_id, gp_xbox_packet_t data);
	void (*notify_connected)(uint8_t device_id);
	void (*notify_disconnected)(uint8_t device_id);
};
typedef struct _gp_xbox_config gp_xbox_config_t;


/**
 * @brief gp_xbox_driver_init initialization routine - this will initialize internal structures of this device driver
 * @see gp_xbox_config_t
 */
void gp_xbox_driver_init(const gp_xbox_config_t *config);

typedef struct _usbh_dev_driver usbh_dev_driver_t;
extern const usbh_dev_driver_t usbh_gp_xbox_driver;

END_DECLS

#endif
