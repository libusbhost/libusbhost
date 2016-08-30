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

#include "usbh_driver_hub_private.h"
#include "driver/usbh_device_driver.h"
#include "usart_helpers.h"
#include "usbh_config.h"

#include <stdint.h>


static hub_device_t hub_device[USBH_MAX_HUBS];

static bool initialized = false;

void hub_driver_init(void)
{
	uint32_t i;

	initialized = true;

	for (i = 0; i < USBH_MAX_HUBS; i++) {
		hub_device[i].device[0] = 0;
		hub_device[i].ports_num = 0;
		hub_device[i].current_port = -1;
	}
}

static void *init(void *usbh_dev)
{
	if (!initialized) {
		LOG_PRINTF("\n%s/%d : driver not initialized\n", __FILE__, __LINE__);
		return 0;
	}

	uint32_t i;
	hub_device_t *drvdata = 0;
	// find free data space for hub device
	for (i = 0; i < USBH_MAX_HUBS; i++) {
		if (hub_device[i].device[0] == 0) {
			break;
		}
	}
	LOG_PRINTF("%{%d}",i);
    LOG_FLUSH();
	if (i == USBH_MAX_HUBS) {
		LOG_PRINTF("ERRRRRRR");
		return 0;
	}

	drvdata = &hub_device[i];
	drvdata->state = 0;
	drvdata->ports_num = 0;
	drvdata->device[0] = (usbh_device_t *)usbh_dev;
	drvdata->busy = 0;
	drvdata->endpoint_in_address = 0;
	drvdata->endpoint_in_maxpacketsize = 0;

	return drvdata;
}

/**
 * @returns true if all needed data are parsed
 */
static bool analyze_descriptor(void *drvdata, void *descriptor)
{
	hub_device_t *hub = (hub_device_t *)drvdata;
	uint8_t desc_type = ((uint8_t *)descriptor)[1];
	switch (desc_type) {
	case USB_DT_CONFIGURATION:
		{
			struct usb_config_descriptor *cfg = (struct usb_config_descriptor*)descriptor;
			hub->buffer[0] = cfg->bConfigurationValue;
		}
		break;

	case USB_DT_ENDPOINT:
		{
			struct usb_endpoint_descriptor *ep = (struct usb_endpoint_descriptor *)descriptor;
			if ((ep->bmAttributes&0x03) == USB_ENDPOINT_ATTR_INTERRUPT) {
				uint8_t epaddr = ep->bEndpointAddress;
				if (epaddr & (1<<7)) {
					hub->endpoint_in_address = epaddr&0x7f;
					hub->endpoint_in_maxpacketsize = ep->wMaxPacketSize;
				}
			}
			LOG_PRINTF("ENDPOINT DESCRIPTOR FOUND\n");
		}
		break;

	case USB_DT_HUB:
		{
			struct usb_hub_descriptor *desc = (struct usb_hub_descriptor *)descriptor;
			//~ hub->ports_num = desc->head.bNbrPorts;
			if ( desc->head.bNbrPorts <= USBH_HUB_MAX_DEVICES) {
				hub->ports_num = desc->head.bNbrPorts;
			} else {
				LOG_PRINTF("INCREASE NUMBER OF ENABLED PORTS\n");
				hub->ports_num = USBH_HUB_MAX_DEVICES;
			}
			LOG_PRINTF("HUB DESCRIPTOR FOUND \n");
		}
		break;

	default:
		LOG_PRINTF("TYPE: %02X \n",desc_type);
		break;
	}

	if (hub->endpoint_in_address) {
		hub->state = 1;
		LOG_PRINTF("end enum");
		return true;
	}
	return false;
}

// Enumerate
static void event(usbh_device_t *dev, usbh_packet_callback_data_t cb_data)
{
	hub_device_t *hub = (hub_device_t *)dev->drvdata;

	LOG_PRINTF("\nHUB->STATE = %d\n", hub->state);
	switch (hub->state) {
	case 26:
		switch (cb_data.status) {
		case USBH_PACKET_CALLBACK_STATUS_OK:
			{
				uint8_t i;
				uint8_t *buf = hub->buffer;
				uint32_t psc = 0;	// Limit: up to 4 bytes...
				for (i = 0; i < cb_data.transferred_length; i++) {
					psc += buf[i] << (i*8);
				}
				int8_t port = 0;

				LOG_PRINTF("psc:%d\n",psc);
				// Driver error... port not found
				if (!psc) {
					// Continue reading status change endpoint
					hub->state = 25;
					break;
				}

				for (i = 0; i <= hub->ports_num; i++) {
					if (psc & (1<<i)) {
						port = i;
						psc = 0;
						break;
					}
				}

				if (hub->current_port >= 1) {
					if (hub->current_port != port) {
						LOG_PRINTF("X");
						hub->state = 25;
						break;
					}
				}
				struct usb_setup_data setup_data;
				// If regular port event, else hub event
				if (port) {
					setup_data.bmRequestType = USB_REQ_TYPE_IN | USB_REQ_TYPE_CLASS | USB_REQ_TYPE_INTERFACE | USB_REQ_TYPE_ENDPOINT;
				} else {
					setup_data.bmRequestType = USB_REQ_TYPE_IN | USB_REQ_TYPE_CLASS | USB_REQ_TYPE_DEVICE;
				}

				setup_data.bRequest = USB_REQ_GET_STATUS;
				setup_data.wValue = 0;
				setup_data.wIndex = port;
				setup_data.wLength = 4;
				hub->state = 31;

				hub->current_port = port;
				LOG_PRINTF("\n\nPORT FOUND: %d\n", port);
				device_xfer_control_write_setup(&setup_data, sizeof(setup_data), event, dev);
			}
			break;

		case USBH_PACKET_CALLBACK_STATUS_EFATAL:
		case USBH_PACKET_CALLBACK_STATUS_ERRSIZ:
			ERROR(cb_data.status);
			hub->state = 0;
			break;

		case USBH_PACKET_CALLBACK_STATUS_EAGAIN:

			// In case of EAGAIN error, retry read on status endpoint
			hub->state = 25;
			LOG_PRINTF("HUB: Retrying...\n");
			break;
		}
		break;

	case EMPTY_PACKET_READ_STATE:
		{
			LOG_PRINTF("|empty packet read|");
			switch (cb_data.status) {
			case USBH_PACKET_CALLBACK_STATUS_OK:
				device_xfer_control_read(0, 0, event, dev);
				hub->state = hub->state_after_empty_read;
				hub->state_after_empty_read = 0;
				break;

			case USBH_PACKET_CALLBACK_STATUS_EFATAL:
			case USBH_PACKET_CALLBACK_STATUS_EAGAIN:
			case USBH_PACKET_CALLBACK_STATUS_ERRSIZ:
				hub->state = hub->state_after_empty_read;
				event(dev, cb_data);
				break;
			}
		}
		break;

	case 3: // Get HUB Descriptor write
		{
			switch (cb_data.status) {
			case USBH_PACKET_CALLBACK_STATUS_OK:
				if (hub->ports_num) {
					hub->index = 0;
					hub->state = 6;
					LOG_PRINTF("No need to get HUB DESC\n");
					event(dev, cb_data);
				} else {
					hub->endpoint_in_toggle = 0;

					struct usb_setup_data setup_data;
					hub->desc_len = hub->device[0]->packet_size_max0;

					setup_data.bmRequestType = USB_REQ_TYPE_IN | USB_REQ_TYPE_CLASS | USB_REQ_TYPE_DEVICE;
					setup_data.bRequest = USB_REQ_GET_DESCRIPTOR;
					setup_data.wValue = 0x29<<8;
					setup_data.wIndex = 0;
					setup_data.wLength = hub->desc_len;

					hub->state++;
					device_xfer_control_write_setup(&setup_data, sizeof(setup_data), event, dev);
					LOG_PRINTF("DO Need to get HUB DESC\n");
				}
				break;

			case USBH_PACKET_CALLBACK_STATUS_EFATAL:
			case USBH_PACKET_CALLBACK_STATUS_EAGAIN:
			case USBH_PACKET_CALLBACK_STATUS_ERRSIZ:
				ERROR(cb_data.status);
				break;
			}
		}
		break;

	case 4: // Get HUB Descriptor read
		{
			switch (cb_data.status) {
			case USBH_PACKET_CALLBACK_STATUS_OK:
				hub->state++;
				device_xfer_control_read(hub->buffer, hub->desc_len, event, dev); // "error dynamic size" - bad comment, investigate
				break;

			case USBH_PACKET_CALLBACK_STATUS_EFATAL:
			case USBH_PACKET_CALLBACK_STATUS_EAGAIN:
			case USBH_PACKET_CALLBACK_STATUS_ERRSIZ:
				ERROR(cb_data.status);
				break;
			}
		}
		break;

	case 5:// Hub descriptor found
		{
			switch (cb_data.status) {
			case USBH_PACKET_CALLBACK_STATUS_OK:
				{
					struct usb_hub_descriptor *hub_descriptor =
						(struct usb_hub_descriptor *)hub->buffer;

					// Check size
					if (hub_descriptor->head.bDescLength > hub->desc_len) {
						struct usb_setup_data setup_data;
						hub->desc_len = hub_descriptor->head.bDescLength;

						setup_data.bmRequestType = USB_REQ_TYPE_IN | USB_REQ_TYPE_CLASS | USB_REQ_TYPE_DEVICE;
						setup_data.bRequest = USB_REQ_GET_DESCRIPTOR;
						setup_data.wValue = 0x29<<8;
						setup_data.wIndex = 0;
						setup_data.wLength = hub->desc_len;

						hub->state = 4;
						device_xfer_control_write_setup(&setup_data, sizeof(setup_data), event, dev);
						break;
					} else if (hub_descriptor->head.bDescLength == hub->desc_len) {
						hub->ports_num = hub_descriptor->head.bNbrPorts;

						hub->state++;
						hub->index = 0;
						cb_data.status = USBH_PACKET_CALLBACK_STATUS_OK;
						event(dev, cb_data);
					} else {
						//try again
					}
				}
				break;

			case USBH_PACKET_CALLBACK_STATUS_ERRSIZ:
				{
					LOG_PRINTF("->\t\t\t\t\t ERRSIZ: deschub\n");
					struct usb_hub_descriptor*hub_descriptor =
						(struct usb_hub_descriptor *)hub->buffer;

					if (cb_data.transferred_length >= sizeof(struct usb_hub_descriptor_head)) {
						if (cb_data.transferred_length == hub_descriptor->head.bDescLength) {
							// Process HUB descriptor
							if ( hub_descriptor->head.bNbrPorts <= USBH_HUB_MAX_DEVICES) {
								hub->ports_num = hub_descriptor->head.bNbrPorts;
							} else {
								LOG_PRINTF("INCREASE NUMBER OF ENABLED PORTS\n");
								hub->ports_num = USBH_HUB_MAX_DEVICES;
							}
							hub->state++;
							hub->index = 0;

							cb_data.status = USBH_PACKET_CALLBACK_STATUS_OK;
							event(dev, cb_data);
						}
					}
				}
				break;

			case USBH_PACKET_CALLBACK_STATUS_EFATAL:
			case USBH_PACKET_CALLBACK_STATUS_EAGAIN:
				ERROR(cb_data.status);
				break;
			}
		}
		break;

	case 6:// enable ports
		{
			switch (cb_data.status) {
			case USBH_PACKET_CALLBACK_STATUS_OK:
				if (hub->index < hub->ports_num) {
					hub->index++;
					struct usb_setup_data setup_data;

					LOG_PRINTF("[!%d!]",hub->index);
					setup_data.bmRequestType = USB_REQ_TYPE_CLASS | USB_REQ_TYPE_INTERFACE | USB_REQ_TYPE_ENDPOINT;
					setup_data.bRequest = HUB_REQ_SET_FEATURE;
					setup_data.wValue = HUB_FEATURE_PORT_POWER;
					setup_data.wIndex = hub->index;
					setup_data.wLength = 0;

					hub->state_after_empty_read = hub->state;
					hub->state = EMPTY_PACKET_READ_STATE;

					device_xfer_control_write_setup(&setup_data, sizeof(setup_data), event, dev);
				} else {
					hub->state++;
					// TODO:
					// Delay Based on hub descriptor field bPwr2PwrGood
					// delay_ms_busy_loop(200);

					LOG_PRINTF("\nHUB CONFIGURED & PORTS POWERED\n");

					cb_data.status = USBH_PACKET_CALLBACK_STATUS_OK;
					event(dev, cb_data);
				}
				break;

			case USBH_PACKET_CALLBACK_STATUS_EFATAL:
			case USBH_PACKET_CALLBACK_STATUS_EAGAIN:
			case USBH_PACKET_CALLBACK_STATUS_ERRSIZ:
				ERROR(cb_data.status);
				break;
			}
		}
		break;

	case 7:
		{
			switch (cb_data.status) {
			case USBH_PACKET_CALLBACK_STATUS_OK:
				{
					struct usb_setup_data setup_data;

					setup_data.bmRequestType = USB_REQ_TYPE_IN | USB_REQ_TYPE_CLASS | USB_REQ_TYPE_DEVICE;
					setup_data.bRequest = USB_REQ_GET_STATUS;
					setup_data.wValue = 0;
					setup_data.wIndex = 0;
					setup_data.wLength = 4;

					hub->state++;
					device_xfer_control_write_setup(&setup_data, sizeof(setup_data), event, dev);
				}
				break;

			case USBH_PACKET_CALLBACK_STATUS_EFATAL:
			case USBH_PACKET_CALLBACK_STATUS_EAGAIN:
			case USBH_PACKET_CALLBACK_STATUS_ERRSIZ:
				ERROR(cb_data.status);
				break;
			}

		}
		break;
	case 8:
		{
			switch (cb_data.status) {
			case USBH_PACKET_CALLBACK_STATUS_OK:
				device_xfer_control_read(hub->buffer, 4, event, dev);
				hub->index = 0;
				hub->state++;
				break;

			case USBH_PACKET_CALLBACK_STATUS_EFATAL:
			case USBH_PACKET_CALLBACK_STATUS_EAGAIN:
			case USBH_PACKET_CALLBACK_STATUS_ERRSIZ:
				ERROR(cb_data.status);
				break;
			}
		}
		break;

	case 9:
		{
			switch (cb_data.status) {
			case USBH_PACKET_CALLBACK_STATUS_OK:
				{
					struct usb_setup_data setup_data;

					setup_data.bmRequestType = USB_REQ_TYPE_IN | USB_REQ_TYPE_CLASS | USB_REQ_TYPE_INTERFACE | USB_REQ_TYPE_ENDPOINT;
					setup_data.bRequest = USB_REQ_GET_STATUS;
					setup_data.wValue = 0;
					setup_data.wIndex = ++hub->index;
					setup_data.wLength = 4;

					hub->state++;

					device_xfer_control_write_setup(&setup_data, sizeof(setup_data), event, dev);
				}
				break;

			case USBH_PACKET_CALLBACK_STATUS_EFATAL:
			case USBH_PACKET_CALLBACK_STATUS_EAGAIN:
			case USBH_PACKET_CALLBACK_STATUS_ERRSIZ:
				ERROR(cb_data.status);
				break;
			}
		}
		break;

	case 10:
		{
			switch (cb_data.status) {
			case USBH_PACKET_CALLBACK_STATUS_OK:
				device_xfer_control_read(hub->buffer, 4, event, dev);
				hub->state++;
				break;

			case USBH_PACKET_CALLBACK_STATUS_EFATAL:
			case USBH_PACKET_CALLBACK_STATUS_EAGAIN:
			case USBH_PACKET_CALLBACK_STATUS_ERRSIZ:
				ERROR(cb_data.status);
				break;
			}
		}
		break;

	case 11:
		{
			switch (cb_data.status) {
			case USBH_PACKET_CALLBACK_STATUS_OK:
				if (hub->index < hub->ports_num) {
					hub->state = 9;
					// process data contained in hub->buffer
					// TODO:
					cb_data.status = USBH_PACKET_CALLBACK_STATUS_OK;
					event(dev, cb_data);
				} else {
					hub->busy = 0;
					hub->state = 25;
				}
				break;

			case USBH_PACKET_CALLBACK_STATUS_EFATAL:
			case USBH_PACKET_CALLBACK_STATUS_EAGAIN:
			case USBH_PACKET_CALLBACK_STATUS_ERRSIZ:
				ERROR(cb_data.status);
				break;
			}
		}
		break;

	case 31: // Read port status
		{
			switch (cb_data.status) {
			case USBH_PACKET_CALLBACK_STATUS_OK:
				{
					int8_t port = hub->current_port;
					hub->state++;

					// TODO: rework to endianess aware,
					// (maybe whole library is affected by this)
					// Detail:
					// 	Isn't universal. Here is endianess ok,
					// 	but on another architecture may be incorrect
					device_xfer_control_read(&hub->hub_and_port_status[port], 4, event, dev);
				}
				break;

			case USBH_PACKET_CALLBACK_STATUS_EFATAL:
			case USBH_PACKET_CALLBACK_STATUS_EAGAIN:
			case USBH_PACKET_CALLBACK_STATUS_ERRSIZ:
				ERROR(cb_data.status);
				// continue
				hub->state = 25;
				break;
			}

		}
		break;
	case 32:
		{
			switch (cb_data.status) {
			case USBH_PACKET_CALLBACK_STATUS_OK:
				{
					int8_t port = hub->current_port;
					LOG_PRINTF("|%d",port);


					// Get Port status, else Get Hub status
					if (port) {
						uint16_t stc = hub->hub_and_port_status[port].stc;

						// Connection status changed
						if (stc & (1<<HUB_FEATURE_PORT_CONNECTION)) {

							// Check, whether device is in connected state
							if (!hub->device[port]) {
								if (!usbh_enum_available() || hub->busy) {
									LOG_PRINTF("\n\t\t\tCannot enumerate %d %d\n", !usbh_enum_available(), hub->busy);
									hub->state = 25;
									break;
								}
							}

							// clear feature C_PORT_CONNECTION
							struct usb_setup_data setup_data;

							setup_data.bmRequestType = USB_REQ_TYPE_CLASS | USB_REQ_TYPE_INTERFACE | USB_REQ_TYPE_ENDPOINT;
							setup_data.bRequest = HUB_REQ_CLEAR_FEATURE;
							setup_data.wValue = HUB_FEATURE_C_PORT_CONNECTION;
							setup_data.wIndex = port;
							setup_data.wLength = 0;

							hub->state_after_empty_read = 33;
							hub->state = EMPTY_PACKET_READ_STATE;

							device_xfer_control_write_setup(&setup_data, sizeof(setup_data), event, dev);

						} else if(stc & (1<<HUB_FEATURE_PORT_RESET)) {
							// clear feature C_PORT_RESET
							// Reset processing is complete, enumerate device
							struct usb_setup_data setup_data;

							setup_data.bmRequestType = USB_REQ_TYPE_CLASS | USB_REQ_TYPE_INTERFACE | USB_REQ_TYPE_ENDPOINT;
							setup_data.bRequest = HUB_REQ_CLEAR_FEATURE;
							setup_data.wValue = HUB_FEATURE_C_PORT_RESET;
							setup_data.wIndex = port;
							setup_data.wLength = 0;

							hub->state_after_empty_read = 35;
							hub->state = EMPTY_PACKET_READ_STATE;

							LOG_PRINTF("RESET");
							device_xfer_control_write_setup(&setup_data, sizeof(setup_data), event, dev);
						} else {
							LOG_PRINTF("another STC %d\n", stc);
						}
					} else {
						hub->state = 25;
						LOG_PRINTF("HUB status change\n");
					}
				}
				break;

			case USBH_PACKET_CALLBACK_STATUS_EFATAL:
			case USBH_PACKET_CALLBACK_STATUS_EAGAIN:
			case USBH_PACKET_CALLBACK_STATUS_ERRSIZ:
				ERROR(cb_data.status);
				// continue
				hub->state = 25;
				break;
			}
		}
		break;
	case 33:
		{
			switch (cb_data.status) {
			case USBH_PACKET_CALLBACK_STATUS_OK:
				{
					int8_t port = hub->current_port;
					uint16_t stc = hub->hub_and_port_status[port].stc;
					if (!hub->device[port]) {
						if ((stc) & (1<<HUB_FEATURE_PORT_CONNECTION)) {
							struct usb_setup_data setup_data;

							setup_data.bmRequestType = USB_REQ_TYPE_CLASS | USB_REQ_TYPE_INTERFACE | USB_REQ_TYPE_ENDPOINT;
							setup_data.bRequest = HUB_REQ_SET_FEATURE;
							setup_data.wValue = HUB_FEATURE_PORT_RESET;
							setup_data.wIndex = port;
							setup_data.wLength = 0;

							hub->state_after_empty_read = 11;
							hub->state = EMPTY_PACKET_READ_STATE;

							LOG_PRINTF("CONN");

							hub->busy = 1;
							device_xfer_control_write_setup(&setup_data, sizeof(setup_data), event, dev);
						}
					} else {
						LOG_PRINTF("\t\t\t\tDISCONNECT EVENT\n");
						if (hub->device[port]->drv && hub->device[port]->drvdata) {
							hub->device[port]->drv->remove(hub->device[port]->drvdata);
						}
						hub->device[port]->address = -1;

						hub->device[port]->drv = 0;
						hub->device[port]->drvdata = 0;
						hub->device[port] = 0;
						hub->current_port = CURRENT_PORT_NONE;
						hub->state = 25;
						hub->busy = 0;
					}
				}
				break;

			case USBH_PACKET_CALLBACK_STATUS_EFATAL:
			case USBH_PACKET_CALLBACK_STATUS_EAGAIN:
			case USBH_PACKET_CALLBACK_STATUS_ERRSIZ:
				ERROR(cb_data.status);
				// continue
				hub->state = 25;
				break;
			}
		}
		break;
	case 35:	// RESET COMPLETE, start enumeration
		{
			switch (cb_data.status) {
			case USBH_PACKET_CALLBACK_STATUS_OK:
				{
					LOG_PRINTF("\nPOLL\n");
					int8_t port = hub->current_port;
					uint16_t sts = hub->hub_and_port_status[port].sts;


					if (sts & (1<<HUB_FEATURE_PORT_ENABLE)) {
						hub->device[port] = usbh_get_free_device(dev);

						if (!hub->device[port]) {
							LOG_PRINTF("\nFATAL ERROR\n");
							return;// DEAD END
						}
						if ((sts & (1<<(HUB_FEATURE_PORT_LOWSPEED))) &&
							!(sts & (1<<(HUB_FEATURE_PORT_HIGHSPEED)))) {
							LOG_PRINTF("Low speed device");

							// Disable Low speed device immediately
							struct usb_setup_data setup_data;

							setup_data.bmRequestType = USB_REQ_TYPE_CLASS | USB_REQ_TYPE_INTERFACE | USB_REQ_TYPE_ENDPOINT;
							setup_data.bRequest = HUB_REQ_CLEAR_FEATURE;
							setup_data.wValue = HUB_FEATURE_PORT_ENABLE;
							setup_data.wIndex = port;
							setup_data.wLength = 0;

							// After write process another devices, poll for events
							hub->state_after_empty_read = 11;//Expecting all ports are powered (constant/non-changeable after init)
							hub->state = EMPTY_PACKET_READ_STATE;

							hub->current_port = CURRENT_PORT_NONE;
							device_xfer_control_write_setup(&setup_data, sizeof(setup_data), event, dev);
						} else if (!(sts & (1<<(HUB_FEATURE_PORT_LOWSPEED))) &&
							!(sts & (1<<(HUB_FEATURE_PORT_HIGHSPEED)))) {
							hub->device[port]->speed = USBH_SPEED_FULL;
							LOG_PRINTF("Full speed device");
							hub->timestamp_us = hub->time_curr_us;
							hub->state = 100; // schedule wait for 500ms
						}


					} else {
						LOG_PRINTF("%s:%d Do not know what to do, when device is disabled after reset\n", __FILE__, __LINE__);
						hub->state = 25;
						return;
					}
				}
				break;

			case USBH_PACKET_CALLBACK_STATUS_EFATAL:
			case USBH_PACKET_CALLBACK_STATUS_EAGAIN:
			case USBH_PACKET_CALLBACK_STATUS_ERRSIZ:
				ERROR(cb_data.status);
				// continue
				hub->state = 25;
				break;
			}
		}
		break;
	default:
		LOG_PRINTF("UNHANDLED EVENT %d\n",hub->state);
		break;
	}
}

static void read_ep1(void *drvdata)
{
	hub_device_t *hub = (hub_device_t *)drvdata;
	usbh_packet_t packet;

	packet.address = hub->device[0]->address;
	packet.data = hub->buffer;
	packet.datalen = hub->endpoint_in_maxpacketsize;
	packet.endpoint_address = hub->endpoint_in_address;
	packet.endpoint_size_max = hub->endpoint_in_maxpacketsize;
	packet.endpoint_type = USBH_ENDPOINT_TYPE_INTERRUPT;
	packet.speed = hub->device[0]->speed;
	packet.callback = event;
	packet.callback_arg = hub->device[0];
	packet.toggle = &hub->endpoint_in_toggle;

	hub->state = 26;
	usbh_read(hub->device[0], &packet);
	LOG_PRINTF("@hub %d/EP1 |  \n", hub->device[0]->address);

}

/**
 * @param time_curr_us - monotically rising time
 *		unit is microseconds
 * @see usbh_poll()
 */
static void poll(void *drvdata, uint32_t time_curr_us)
{
	hub_device_t *hub = (hub_device_t *)drvdata;
	usbh_device_t *dev = hub->device[0];

	hub->time_curr_us = time_curr_us;

	switch (hub->state) {
	case 25:
		{
			if (usbh_enum_available()) {
				read_ep1(hub);
			} else {
				LOG_PRINTF("enum not available\n");
			}
		}
		break;

	case 1:
		{
			LOG_PRINTF("CFGVAL: %d\n", hub->buffer[0]);
			struct usb_setup_data setup_data;

			setup_data.bmRequestType = USB_REQ_TYPE_STANDARD | USB_REQ_TYPE_DEVICE;
			setup_data.bRequest = USB_REQ_SET_CONFIGURATION;
			setup_data.wValue = hub->buffer[0];
			setup_data.wIndex = 0;
			setup_data.wLength = 0;

			hub->state = EMPTY_PACKET_READ_STATE;
			hub->state_after_empty_read = 3;
			device_xfer_control_write_setup(&setup_data, sizeof(setup_data), event, dev);

		}
		break;
	case 100:
		if (hub->time_curr_us - hub->timestamp_us > 500000) {
			int8_t port = hub->current_port;
			LOG_PRINTF("PORT: %d", port);
			LOG_PRINTF("\nNEW device at address: %d\n", hub->device[port]->address);
			hub->device[port]->lld = hub->device[0]->lld;

			device_enumeration_start(hub->device[port]);
			hub->current_port = CURRENT_PORT_NONE;

			// Maybe error, when assigning address is taking too long
			//
			// Detail:
			// USB hub cannot enable another port while the device
			// the current one is also in address state (has address==0)
			// Only one device on bus can have address==0
			hub->busy = 0;

			hub->state = 25;
		}
		break;
	default:
		break;
	}

	if (usbh_enum_available()) {
		uint32_t i;
		for (i = 1; i < USBH_HUB_MAX_DEVICES + 1; i++) {
			if (hub->device[i]) {
				if (hub->device[i]->drv && hub->device[i]->drvdata) {
					hub->device[i]->drv->poll(hub->device[i]->drvdata, time_curr_us);
				}
			}
		}
	}
}
static void remove(void *drvdata)
{
	hub_device_t *hub = (hub_device_t *)drvdata;
	uint8_t i;

	// Call fast... to avoid polling
	hub->state = 0;
	hub->endpoint_in_address = 0;
	hub->busy = 0;
	for (i = 1; i < USBH_HUB_MAX_DEVICES + 1; i++) {
		if (hub->device[i]) {
			if (hub->device[i]->drv && hub->device[i]->drvdata) {
				if (hub->device[i]->drv->remove != remove) {
					LOG_PRINTF("\t\t\t\tHUB REMOVE %d\n",hub->device[i]->address);
					hub->device[i]->drv->remove(hub->device[i]->drvdata);
				}
			}
			hub->device[i] = 0;
		}
		hub->device[0]->drv = 0;
		hub->device[0]->drvdata = 0;
		hub->device[0] = 0;

	}
}

static const usbh_dev_driver_info_t driver_info = {
	.deviceClass = 0x09,
	.deviceSubClass = -1,
	.deviceProtocol = -1,
	.idVendor = -1,
	.idProduct = -1,
	.ifaceClass = 0x09,
	.ifaceSubClass = -1,
	.ifaceProtocol = -1
};

const usbh_dev_driver_t usbh_hub_driver = {
	.init = init,
	.analyze_descriptor = analyze_descriptor,
	.poll = poll,
	.remove = remove,
	.info = &driver_info
};
