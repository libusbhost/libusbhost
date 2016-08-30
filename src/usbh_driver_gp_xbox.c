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
#include "usbh_driver_gp_xbox.h"
#include "driver/usbh_device_driver.h"

#include <stdint.h>
#include <libopencm3/usb/usbstd.h>

enum STATES {
	STATE_INACTIVE,
	STATE_READING_COMPLETE,
	STATE_READING_REQUEST,
	STATE_SET_CONFIGURATION_REQUEST,
	STATE_SET_CONFIGURATION_EMPTY_READ,
	STATE_SET_CONFIGURATION_COMPLETE
};

#define GP_XBOX_CORRECT_TRANSFERRED_LENGTH 20

struct _gp_xbox_device {
	usbh_device_t *usbh_device;
	uint8_t buffer[USBH_GP_XBOX_BUFFER];
	uint16_t endpoint_in_maxpacketsize;
	uint8_t endpoint_in_address;
	enum STATES state_next;
	uint8_t endpoint_in_toggle;
	uint8_t device_id;
	uint8_t configuration_value;
};
typedef struct _gp_xbox_device gp_xbox_device_t;

static gp_xbox_device_t gp_xbox_device[USBH_GP_XBOX_MAX_DEVICES];
static const gp_xbox_config_t *gp_xbox_config;

static bool initialized = false;
static void read_gp_xbox_in(gp_xbox_device_t *gp_xbox);

void gp_xbox_driver_init(const gp_xbox_config_t *config)
{
	if (!config) {
		return;
	}
	initialized = true;
	uint32_t i;
	gp_xbox_config = config;
	for (i = 0; i < USBH_GP_XBOX_MAX_DEVICES; i++) {
		gp_xbox_device[i].state_next = STATE_INACTIVE;
	}
}

/**
 *
 *
 */
static void *init(void *usbh_dev)
{
	if (!initialized) {
		LOG_PRINTF("\n%s/%d : driver not initialized\n", __FILE__, __LINE__);
		return 0;
	}

	uint32_t i;
	gp_xbox_device_t *drvdata = 0;

	// find free data space for gp_xbox device
	for (i = 0; i < USBH_GP_XBOX_MAX_DEVICES; i++) {
		if (gp_xbox_device[i].state_next == STATE_INACTIVE) {
			drvdata = &gp_xbox_device[i];
			drvdata->device_id = i;
			drvdata->endpoint_in_address = 0;
			drvdata->endpoint_in_toggle = 0;
			drvdata->usbh_device = (usbh_device_t *)usbh_dev;
			break;
		}
	}

	return drvdata;
}

/**
 * Returns true if all needed data are parsed
 */
static bool analyze_descriptor(void *drvdata, void *descriptor)
{
	gp_xbox_device_t *gp_xbox = (gp_xbox_device_t *)drvdata;
	uint8_t desc_type = ((uint8_t *)descriptor)[1];
	switch (desc_type) {
	case USB_DT_CONFIGURATION:
		{
			struct usb_config_descriptor *cfg = (struct usb_config_descriptor*)descriptor;
			gp_xbox->configuration_value = cfg->bConfigurationValue;
		}
		break;
	case USB_DT_DEVICE:
		break;
	case USB_DT_INTERFACE:
		break;
	case USB_DT_ENDPOINT:
		{
			struct usb_endpoint_descriptor *ep = (struct usb_endpoint_descriptor*)descriptor;
			if ((ep->bmAttributes&0x03) == USB_ENDPOINT_ATTR_INTERRUPT) {
				uint8_t epaddr = ep->bEndpointAddress;
				if (epaddr & (1<<7)) {
					gp_xbox->endpoint_in_address = epaddr&0x7f;
					if (ep->wMaxPacketSize < USBH_GP_XBOX_BUFFER) {
						gp_xbox->endpoint_in_maxpacketsize = ep->wMaxPacketSize;
					} else {
						gp_xbox->endpoint_in_maxpacketsize = USBH_GP_XBOX_BUFFER;
					}
				}

				if (gp_xbox->endpoint_in_address) {
					gp_xbox->state_next = STATE_SET_CONFIGURATION_REQUEST;
					return true;
				}
			}
		}
		break;
	// TODO Class Specific descriptors
	default:
		break;
	}
	return false;
}

static void parse_data(usbh_device_t *dev)
{
	gp_xbox_device_t *gp_xbox = (gp_xbox_device_t *)dev->drvdata;

	uint8_t *packet = gp_xbox->buffer;

	gp_xbox_packet_t gp_xbox_packet;
	gp_xbox_packet.buttons = 0;

	// DPAD
	const uint8_t data1 = packet[2];
	const uint8_t data2 = packet[3];
	if (data1 & (1 << 0)) {
		gp_xbox_packet.buttons |= GP_XBOX_DPAD_TOP;
	}

	if (data1 & (1 << 1)) {
		gp_xbox_packet.buttons |= GP_XBOX_DPAD_BOTTOM;
	}

	if (data1 & (1 << 2)) {
		gp_xbox_packet.buttons |= GP_XBOX_DPAD_LEFT;
	}

	if (data1 & (1 << 3)) {
		gp_xbox_packet.buttons |= GP_XBOX_DPAD_RIGHT;
	}

	// Start + select

	if (data1 & (1 << 4)) {
		gp_xbox_packet.buttons |= GP_XBOX_BUTTON_START;
	}

	if (data1 & (1 << 5)) {
		gp_xbox_packet.buttons |= GP_XBOX_BUTTON_SELECT;
	}

	// axis buttons

	if (data1 & (1 << 6)) {
		gp_xbox_packet.buttons |= GP_XBOX_BUTTON_AXIS_LEFT;
	}

	if (data1 & (1 << 7)) {
		gp_xbox_packet.buttons |= GP_XBOX_BUTTON_AXIS_RIGHT;
	}

	// buttons ABXY

	if (data2 & (1 << 4)) {
		gp_xbox_packet.buttons |= GP_XBOX_BUTTON_A;
	}

	if (data2 & (1 << 5)) {
		gp_xbox_packet.buttons |= GP_XBOX_BUTTON_B;
	}

	if (data2 & (1 << 6)) {
		gp_xbox_packet.buttons |= GP_XBOX_BUTTON_X;
	}

	if (data2 & (1 << 7)) {
		gp_xbox_packet.buttons |= GP_XBOX_BUTTON_Y;
	}

	// buttons rear

	if (data2 & (1 << 0)) {
		gp_xbox_packet.buttons |= GP_XBOX_BUTTON_LT;
	}

	if (data2 & (1 << 1)) {
		gp_xbox_packet.buttons |= GP_XBOX_BUTTON_RT;
	}

	if (data2 & (1 << 2)) {
		gp_xbox_packet.buttons |= GP_XBOX_BUTTON_XBOX;
	}

	// rear levers

	gp_xbox_packet.axis_rear_left = packet[4];
	gp_xbox_packet.axis_rear_right = packet[5];
	gp_xbox_packet.axis_left_x = packet[7]*256 + packet[6];
	gp_xbox_packet.axis_left_y = packet[9]*256 + packet[8];
	gp_xbox_packet.axis_right_x = packet[11]*256 + packet[10];
	gp_xbox_packet.axis_right_y = packet[13]*256 + packet[12];

	// call update callback
	if (gp_xbox_config->update) {
		gp_xbox_config->update(gp_xbox->device_id, gp_xbox_packet);
	}
}

static void event(usbh_device_t *dev, usbh_packet_callback_data_t cb_data)
{
	gp_xbox_device_t *gp_xbox = (gp_xbox_device_t *)dev->drvdata;
	switch (gp_xbox->state_next) {
	case STATE_READING_COMPLETE:
		{
			switch (cb_data.status) {
			case USBH_PACKET_CALLBACK_STATUS_OK:
				parse_data(dev);
				gp_xbox->state_next = STATE_READING_REQUEST;
				break;

			case USBH_PACKET_CALLBACK_STATUS_ERRSIZ:
				if (cb_data.transferred_length == GP_XBOX_CORRECT_TRANSFERRED_LENGTH) {
					parse_data(dev);
				}
				gp_xbox->state_next = STATE_READING_REQUEST;
				break;

			case USBH_PACKET_CALLBACK_STATUS_EFATAL:
			case USBH_PACKET_CALLBACK_STATUS_EAGAIN:
				ERROR(cb_data.status);
				gp_xbox->state_next = STATE_INACTIVE;
				break;
			}
		}
		break;

	case STATE_SET_CONFIGURATION_EMPTY_READ:
		{
			LOG_PRINTF("|empty packet read|");
			switch (cb_data.status) {
			case USBH_PACKET_CALLBACK_STATUS_OK:
				gp_xbox->state_next = STATE_SET_CONFIGURATION_COMPLETE;
				device_xfer_control_read(0, 0, event, dev);
				break;
			case USBH_PACKET_CALLBACK_STATUS_EFATAL:
			case USBH_PACKET_CALLBACK_STATUS_EAGAIN:
			case USBH_PACKET_CALLBACK_STATUS_ERRSIZ:
				ERROR(cb_data.status);
				gp_xbox->state_next = STATE_INACTIVE;
				break;
			}
		}
		break;
	case STATE_SET_CONFIGURATION_COMPLETE: // Configured
		{
			switch (cb_data.status) {
			case USBH_PACKET_CALLBACK_STATUS_OK:
				gp_xbox->state_next = STATE_READING_REQUEST;
				gp_xbox->endpoint_in_toggle = 0;
				LOG_PRINTF("\ngp_xbox CONFIGURED\n");
				if (gp_xbox_config->notify_connected) {
					gp_xbox_config->notify_connected(gp_xbox->device_id);
				}
				break;

			case USBH_PACKET_CALLBACK_STATUS_EFATAL:
			case USBH_PACKET_CALLBACK_STATUS_EAGAIN:
			case USBH_PACKET_CALLBACK_STATUS_ERRSIZ:
				ERROR(cb_data.status);
				gp_xbox->state_next = STATE_INACTIVE;
				break;
			}
		}
		break;

	case STATE_INACTIVE:
		{
			LOG_PRINTF("XBOX inactive");
		}
		break;
	default:
		{
			LOG_PRINTF("Unknown state\n");
		}
		break;
	}
}


static void read_gp_xbox_in(gp_xbox_device_t *gp_xbox)
{
	usbh_packet_t packet;

	packet.address = gp_xbox->usbh_device->address;
	packet.data = &gp_xbox->buffer[0];
	packet.datalen = gp_xbox->endpoint_in_maxpacketsize;
	packet.endpoint_address = gp_xbox->endpoint_in_address;
	packet.endpoint_size_max = gp_xbox->endpoint_in_maxpacketsize;
	packet.endpoint_type = USBH_ENDPOINT_TYPE_INTERRUPT;
	packet.speed = gp_xbox->usbh_device->speed;
	packet.callback = event;
	packet.callback_arg = gp_xbox->usbh_device;
	packet.toggle = &gp_xbox->endpoint_in_toggle;

	gp_xbox->state_next = STATE_READING_COMPLETE;
	usbh_read(gp_xbox->usbh_device, &packet);

	// LOG_PRINTF("@gp_xbox EP1 |  \n");
}

/**
 * \param time_curr_us - monotically rising time (see usbh_hubbed.h)
 *		unit is microseconds
 */
static void poll(void *drvdata, uint32_t time_curr_us)
{
	(void)time_curr_us;

	gp_xbox_device_t *gp_xbox = (gp_xbox_device_t *)drvdata;
	usbh_device_t *dev = gp_xbox->usbh_device;

	switch (gp_xbox->state_next) {
	case STATE_READING_REQUEST:
		{
			read_gp_xbox_in(gp_xbox);
		}
		break;

	case STATE_SET_CONFIGURATION_REQUEST:
		{
			struct usb_setup_data setup_data;

			setup_data.bmRequestType = USB_REQ_TYPE_STANDARD | USB_REQ_TYPE_DEVICE;
			setup_data.bRequest = USB_REQ_SET_CONFIGURATION;
			setup_data.wValue = gp_xbox->configuration_value;
			setup_data.wIndex = 0;
			setup_data.wLength = 0;

			gp_xbox->state_next = STATE_SET_CONFIGURATION_EMPTY_READ;

			device_xfer_control_write_setup(&setup_data, sizeof(setup_data), event, dev);
		}
		break;

	default:
		{
			// do nothing - probably transfer is in progress
		}
		break;
	}
}

static void remove(void *drvdata)
{
	LOG_PRINTF("Removing xbox\n");

	gp_xbox_device_t *gp_xbox = (gp_xbox_device_t *)drvdata;
	if (gp_xbox_config->notify_disconnected) {
		gp_xbox_config->notify_disconnected(gp_xbox->device_id);
	}
	gp_xbox->state_next = STATE_INACTIVE;
	gp_xbox->endpoint_in_address = 0;
}

static const usbh_dev_driver_info_t driver_info = {
	.deviceClass = 0xff,
	.deviceSubClass = 0xff,
	.deviceProtocol = 0xff,
	.idVendor = 0x045e,
	.idProduct = 0x028e,
	.ifaceClass = 0xff,
	.ifaceSubClass = 93,
	.ifaceProtocol = 0x01
};

const usbh_dev_driver_t usbh_gp_xbox_driver = {
	.init = init,
	.analyze_descriptor = analyze_descriptor,
	.poll = poll,
	.remove = remove,
	.info = &driver_info
};
