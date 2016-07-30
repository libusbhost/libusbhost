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

#include "usbh_core.h"
#include "driver/usbh_device_driver.h"
#include "usbh_driver_hid_mouse.h"
#include "usart_helpers.h"

#include <libopencm3/usb/usbstd.h>

enum STATES {
	STATE_INACTIVE,
	STATE_READING_COMPLETE,
	STATE_READING_REQUEST,
	STATE_SET_CONFIGURATION_REQUEST,
	STATE_SET_CONFIGURATION_EMPTY_READ,
	STATE_SET_CONFIGURATION_COMPLETE
};

struct _hid_mouse_device {
	usbh_device_t *usbh_device;
	uint8_t buffer[USBH_HID_MOUSE_BUFFER];
	uint16_t endpoint_in_maxpacketsize;
	uint8_t endpoint_in_address;
	enum STATES state_next;
	uint8_t endpoint_in_toggle;
	uint8_t device_id;
	uint8_t configuration_value;
};
typedef struct _hid_mouse_device hid_mouse_device_t;

static hid_mouse_device_t mouse_device[USBH_HID_MOUSE_MAX_DEVICES];
static const hid_mouse_config_t *mouse_config;




#include <stdint.h>

static bool initialized = false;

void hid_mouse_driver_init(const hid_mouse_config_t *config)
{
	uint32_t i;

	initialized = true;

	mouse_config = config;
	for (i = 0; i < USBH_HID_MOUSE_MAX_DEVICES; i++) {
		mouse_device[i].state_next = STATE_INACTIVE;
	}
}

/**
 *
 *
 */
static void *init(void *usbh_dev)
{
	if (!initialized) {
		LOG_PRINTF("\n%s/%d : driver not initialized\r\n", __FILE__, __LINE__);
		return 0;
	}

	uint32_t i;
	hid_mouse_device_t *drvdata = 0;

	// find free data space for mouse device
	for (i = 0; i < USBH_HID_MOUSE_MAX_DEVICES; i++) {
		if (mouse_device[i].state_next == STATE_INACTIVE) {
			drvdata = &mouse_device[i];
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
	hid_mouse_device_t *mouse = (hid_mouse_device_t *)drvdata;
	uint8_t desc_type = ((uint8_t *)descriptor)[1];
	switch (desc_type) {
	case USB_DT_CONFIGURATION:
		{
			struct usb_config_descriptor *cfg = (struct usb_config_descriptor*)descriptor;
			mouse->configuration_value = cfg->bConfigurationValue;
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
					mouse->endpoint_in_address = epaddr&0x7f;
					if (ep->wMaxPacketSize < USBH_HID_MOUSE_BUFFER) {
						mouse->endpoint_in_maxpacketsize = ep->wMaxPacketSize;
					} else {
						mouse->endpoint_in_maxpacketsize = USBH_HID_MOUSE_BUFFER;
					}
				}

				if (mouse->endpoint_in_address) {
					mouse->state_next = STATE_SET_CONFIGURATION_REQUEST;
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

static void event(usbh_device_t *dev, usbh_packet_callback_data_t cb_data)
{
	hid_mouse_device_t *mouse = (hid_mouse_device_t *)dev->drvdata;
	switch (mouse->state_next) {
	case STATE_READING_COMPLETE:
		{
			switch (cb_data.status) {
			case USBH_PACKET_CALLBACK_STATUS_OK:
			case USBH_PACKET_CALLBACK_STATUS_ERRSIZ:
				mouse->state_next = STATE_READING_REQUEST;
				break;

			case USBH_PACKET_CALLBACK_STATUS_EFATAL:
			case USBH_PACKET_CALLBACK_STATUS_EAGAIN:
				ERROR(cb_data.status);
				mouse->state_next = STATE_INACTIVE;
				break;
			}
		}
		break;

	case STATE_SET_CONFIGURATION_EMPTY_READ:
		{
			LOG_PRINTF("|empty packet read|");
			switch (cb_data.status) {
			case USBH_PACKET_CALLBACK_STATUS_OK:
				mouse->state_next = STATE_SET_CONFIGURATION_COMPLETE;
				device_xfer_control_read(0, 0, event, dev);
				break;

			case USBH_PACKET_CALLBACK_STATUS_ERRSIZ:
			case USBH_PACKET_CALLBACK_STATUS_EFATAL:
			case USBH_PACKET_CALLBACK_STATUS_EAGAIN:
				ERROR(cb_data.status);
				mouse->state_next = STATE_INACTIVE;
				break;
			}
		}
		break;
	case STATE_SET_CONFIGURATION_COMPLETE: // Configured
		{
			switch (cb_data.status) {
			case USBH_PACKET_CALLBACK_STATUS_OK:
				mouse->state_next = STATE_READING_REQUEST;
				mouse->endpoint_in_toggle = 0;
				LOG_PRINTF("\nMOUSE CONFIGURED\n");
				break;

			case USBH_PACKET_CALLBACK_STATUS_ERRSIZ:
			case USBH_PACKET_CALLBACK_STATUS_EFATAL:
			case USBH_PACKET_CALLBACK_STATUS_EAGAIN:
				ERROR(cb_data.status);
				mouse->state_next = STATE_INACTIVE;
				break;
			}
		}
		break;
	default:
		break;
	}
}


static void read_mouse_in(void *drvdata)
{
	hid_mouse_device_t *mouse = (hid_mouse_device_t *)drvdata;
	usbh_packet_t packet;

	packet.address = mouse->usbh_device->address;
	packet.data = &mouse->buffer[0];
	packet.datalen = mouse->endpoint_in_maxpacketsize;
	packet.endpoint_address = mouse->endpoint_in_address;
	packet.endpoint_size_max = mouse->endpoint_in_maxpacketsize;
	packet.endpoint_type = USBH_ENDPOINT_TYPE_INTERRUPT;
	packet.speed = mouse->usbh_device->speed;
	packet.callback = event;
	packet.callback_arg = mouse->usbh_device;
	packet.toggle = &mouse->endpoint_in_toggle;

	mouse->state_next = STATE_READING_COMPLETE;
	usbh_read(mouse->usbh_device, &packet);

	// LOG_PRINTF("@MOUSE EP1 |  \n");

}

/**
 * @param time_curr_us - monotically rising time
 *		unit is microseconds
 * @see usbh_poll()
 */
static void poll(void *drvdata, uint32_t time_curr_us)
{
	(void)time_curr_us;

	hid_mouse_device_t *mouse = (hid_mouse_device_t *)drvdata;
	usbh_device_t *dev = mouse->usbh_device;
	switch (mouse->state_next) {
	case STATE_READING_REQUEST:
		{
			read_mouse_in(drvdata);
		}
		break;

	case STATE_SET_CONFIGURATION_REQUEST:
		{
			struct usb_setup_data setup_data;

			setup_data.bmRequestType = 0b00000000;
			setup_data.bRequest = USB_REQ_SET_CONFIGURATION;
			setup_data.wValue = mouse->configuration_value;
			setup_data.wIndex = 0;
			setup_data.wLength = 0;

			mouse->state_next = STATE_SET_CONFIGURATION_EMPTY_READ;

			device_xfer_control_write_setup(&setup_data, sizeof(setup_data), event, dev);
		}
		break;

	default:
		// do nothing - probably transfer is in progress
		break;
	}
}

static void remove(void *drvdata)
{
	hid_mouse_device_t *mouse = (hid_mouse_device_t *)drvdata;
	mouse->state_next = STATE_INACTIVE;
	mouse->endpoint_in_address = 0;
}

static const usbh_dev_driver_info_t driver_info = {
	.deviceClass = -1,
	.deviceSubClass = -1,
	.deviceProtocol = -1,
	.idVendor = -1,
	.idProduct = -1,
	.ifaceClass = 0x03,
	.ifaceSubClass = -1,
	.ifaceProtocol = 0x02
};

const usbh_dev_driver_t usbh_hid_mouse_driver = {
	.init = init,
	.analyze_descriptor = analyze_descriptor,
	.poll = poll,
	.remove = remove,
	.info = &driver_info
};
