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

#include "usbh_config.h"
#include "usbh_lld_stm32f4.h"
#include "driver/usbh_device_driver.h"
#include "usart_helpers.h"

#include <libopencm3/stm32/gpio.h>
#include <libopencm3/usb/usbstd.h>

static struct {
	bool enumeration_run;
	const usbh_low_level_driver_t * const *lld_drivers;
	const usbh_dev_driver_t * const *dev_drivers;
	int8_t address_temporary;
} usbh_data = {0};

static void set_enumeration(void)
{
	usbh_data.enumeration_run = true;
}

static void reset_enumeration(void)
{
	usbh_data.enumeration_run = false;
}

static bool enumeration(void)
{
	return usbh_data.enumeration_run;
}

/**
 *
 */
static bool find_driver(usbh_device_t *dev, const usbh_dev_driver_info_t * device_info)
{

#define CHECK_PARTIAL_COMPATIBILITY(what) \
	if (usbh_data.dev_drivers[i]->info->what != -1\
	&& device_info->what != usbh_data.dev_drivers[i]->info->what) {\
		i++;\
		continue;\
	}

	int i = 0;

	while (usbh_data.dev_drivers[i]) {

		CHECK_PARTIAL_COMPATIBILITY(ifaceClass);
		CHECK_PARTIAL_COMPATIBILITY(ifaceSubClass);
		CHECK_PARTIAL_COMPATIBILITY(ifaceProtocol);
		CHECK_PARTIAL_COMPATIBILITY(deviceClass);
		CHECK_PARTIAL_COMPATIBILITY(deviceSubClass);
		CHECK_PARTIAL_COMPATIBILITY(deviceProtocol);
		CHECK_PARTIAL_COMPATIBILITY(idVendor);
		CHECK_PARTIAL_COMPATIBILITY(idProduct);

		dev->drv = usbh_data.dev_drivers[i];
		dev->drvdata = dev->drv->init(dev);
		if (dev->drvdata) {
			LOG_PRINTF("Unable to initialize device driver at index %d", i);
			continue;
		}
		return true;
	}
	return false;
#undef CHECK_PARTIAL_COMPATIBILITY
}


static void device_register(void *descriptors, uint16_t descriptors_len, usbh_device_t *dev)
{
	uint32_t i = 0;
	dev->drv = 0;
	uint8_t *buf = (uint8_t *)descriptors;

	dev->drv = 0;
	dev->drvdata = 0;

	uint8_t desc_len = buf[i];
	uint8_t desc_type = buf[i + 1];

	usbh_dev_driver_info_t device_info;
	if (desc_type == USB_DT_DEVICE) {
		struct usb_device_descriptor *device_desc = (void*)&buf[i];
		LOG_PRINTF("DEVICE DESCRIPTOR\n");
		device_info.deviceClass = device_desc->bDeviceClass;
		device_info.deviceSubClass = device_desc->bDeviceSubClass;
		device_info.deviceProtocol = device_desc->bDeviceProtocol;
		device_info.idVendor = device_desc->idVendor;
		device_info.idProduct = device_desc->idProduct;
	} else {
		LOG_PRINTF("INVALID descriptors pointer - fatal error");
		return;
	}


	while (i < descriptors_len) {
		desc_len = buf[i];
		desc_type = buf[i + 1];
		switch (desc_type) {
		case USB_DT_INTERFACE:
		{
			LOG_PRINTF("INTERFACE_DESCRIPTOR\n");
			struct usb_interface_descriptor *iface = (void*)&buf[i];
			device_info.ifaceClass = iface->bInterfaceClass;
			device_info.ifaceSubClass = iface->bInterfaceSubClass;
			device_info.ifaceProtocol = iface->bInterfaceProtocol;
			if (find_driver(dev, &device_info)) {
				int k = 0;
				while (k < descriptors_len) {
					desc_len = buf[k];
					void *drvdata = dev->drvdata;
					LOG_PRINTF("[%d]", buf[k+1]);
					if (dev->drv->analyze_descriptor(drvdata, &buf[k])) {
						LOG_PRINTF("Device Initialized\n");
						return;
					}

					if (desc_len == 0) {
						LOG_PRINTF("Problem occured while parsing complete configuration descriptor");
						return;
					}
					k += desc_len;
				}
				LOG_PRINTF("Device driver isn't compatible with this device\n");
			} else {
				LOG_PRINTF("No compatible driver has been found for interface #%d\n", iface->bInterfaceNumber);
			}
		}
			break;
		default:
			break;
		}

		if (desc_len == 0) {
			LOG_PRINTF("PROBLEM WITH PARSE %d\n",i);
			return;
		}
		i += desc_len;
	}
	LOG_PRINTF("Device NOT Initialized\n");
}

void usbh_init(const void *low_level_drivers[], const usbh_dev_driver_t * const device_drivers[])
{
	if (!low_level_drivers) {
		return;
	}

	usbh_data.lld_drivers = (const usbh_low_level_driver_t **)low_level_drivers;
	usbh_data.dev_drivers = device_drivers;

	uint32_t k = 0;
	while (usbh_data.lld_drivers[k]) {
		LOG_PRINTF("Initialization low-level driver with index=%d\n", k);

		usbh_device_t * usbh_device =
			((usbh_generic_data_t *)(usbh_data.lld_drivers[k])->driver_data)->usbh_device;
		uint32_t i;
		for (i = 0; i < USBH_MAX_DEVICES; i++) {
			//~ LOG_PRINTF("%p ", &usbh_device[i]);
			usbh_device[i].address = -1;
			usbh_device[i].drv = 0;
			usbh_device[i].drvdata = 0;
		}
		usbh_data.lld_drivers[k]->init(usbh_data.lld_drivers[k]->driver_data);

		k++;
	}

}

/*
 * NEW ENUMERATE
 *
 */
void device_xfer_control_write_setup(const void *data, uint16_t datalen, usbh_packet_callback_t callback, usbh_device_t *dev)
{
	usbh_packet_t packet;

	packet.data.out = data;
	packet.datalen = datalen;
	packet.address = dev->address;
	packet.endpoint_address = 0;
	packet.endpoint_size_max = dev->packet_size_max0;
	packet.endpoint_type = USBH_ENDPOINT_TYPE_CONTROL;
	packet.control_type = USBH_CONTROL_TYPE_SETUP;
	packet.speed = dev->speed;
	packet.callback = callback;
	packet.callback_arg = dev;
	packet.toggle = &dev->toggle0;

	usbh_write(dev, &packet);
	LOG_PRINTF("WR-setup@device...%d \n", dev->address);
}

void device_xfer_control_write_data(const void *data, uint16_t datalen, usbh_packet_callback_t callback, usbh_device_t *dev)
{
	usbh_packet_t packet;

	packet.data.out = data;
	packet.datalen = datalen;
	packet.address = dev->address;
	packet.endpoint_address = 0;
	packet.endpoint_size_max = dev->packet_size_max0;
	packet.endpoint_type = USBH_ENDPOINT_TYPE_CONTROL;
	packet.control_type = USBH_CONTROL_TYPE_DATA;
	packet.speed = dev->speed;
	packet.callback = callback;
	packet.callback_arg = dev;
	packet.toggle = &dev->toggle0;

	usbh_write(dev, &packet);
	LOG_PRINTF("WR-data@device...%d \n", dev->address);
}

void device_xfer_control_read(void *data, uint16_t datalen, usbh_packet_callback_t callback, usbh_device_t *dev)
{
	usbh_packet_t packet;

	packet.data.in = data;
	packet.datalen = datalen;
	packet.address = dev->address;
	packet.endpoint_address = 0;
	packet.endpoint_size_max = dev->packet_size_max0;
	packet.endpoint_type = USBH_ENDPOINT_TYPE_CONTROL;
	packet.speed = dev->speed;
	packet.callback = callback;
	packet.callback_arg = dev;
	packet.toggle = &dev->toggle0;

	usbh_read(dev, &packet);
	LOG_PRINTF("RD@device...%d |  \n", dev->address);
}



bool usbh_enum_available(void)
{
	return !enumeration();
}

/**
 * Returns 0 on error
 * device otherwise
 */
usbh_device_t *usbh_get_free_device(const usbh_device_t *dev)
{
	const usbh_low_level_driver_t *lld = dev->lld;
	usbh_generic_data_t *lld_data = lld->driver_data;
	usbh_device_t *usbh_device = lld_data->usbh_device;

	uint8_t i;
	LOG_PRINTF("DEV ADDRESS%d\n", dev->address);
	for (i = 0; i < USBH_MAX_DEVICES; i++) {
		if (usbh_device[i].address < 0) {
			LOG_PRINTF("\t\t\t\t\tFOUND: %d", i);
			usbh_device[i].address = i+1;
			return &usbh_device[i];
		} else {
			LOG_PRINTF("address: %d\n\n\n", usbh_device[i].address);
		}
	}

	return 0;
}

static void device_enumeration_finish(usbh_device_t *dev)
{
	reset_enumeration();
	dev->state = USBH_ENUM_STATE_FIRST;
}

static void device_enumeration_terminate(usbh_device_t *dev)
{
	dev->address = -1;
	device_enumeration_finish(dev);
}

#define CONTINUE_WITH(en) \
	dev->state = en;\
	device_enumerate(dev, cb_data);

static void device_enumerate(usbh_device_t *dev, usbh_packet_callback_data_t cb_data)
{
	const usbh_low_level_driver_t *lld = dev->lld;
	usbh_generic_data_t *lld_data = lld->driver_data;
	uint8_t *usbh_buffer = lld_data->usbh_buffer;
	uint8_t state_start = dev->state; // Detection of hang
//	LOG_PRINTF("\nSTATE: %d\n", state);
	switch (dev->state) {
	case USBH_ENUM_STATE_SET_ADDRESS_EMPTY_READ:
		{
			switch (cb_data.status) {
			case USBH_PACKET_CALLBACK_STATUS_OK:
				dev->state = USBH_ENUM_STATE_SET_ADDRESS_EMPTY_READ_COMPLETE;
				LOG_PRINTF("Assigning address: %d\n", usbh_data.address_temporary);
				device_xfer_control_read(0, 0, device_enumerate, dev);
				break;

			case USBH_PACKET_CALLBACK_STATUS_EFATAL:
			case USBH_PACKET_CALLBACK_STATUS_EAGAIN:
			case USBH_PACKET_CALLBACK_STATUS_ERRSIZ:
				device_enumeration_terminate(dev);
				ERROR(cb_data.status);
				break;
			}
		}
		break;

	case USBH_ENUM_STATE_SET_ADDRESS_EMPTY_READ_COMPLETE:
		switch (cb_data.status) {
		case USBH_PACKET_CALLBACK_STATUS_OK:
			if (dev->address == 0) {
				dev->address = usbh_data.address_temporary;
				LOG_PRINTF("Assigned address: %d\n", dev->address);
			}
			CONTINUE_WITH(USBH_ENUM_STATE_DEVICE_DT_READ_SETUP);
			break;

		case USBH_PACKET_CALLBACK_STATUS_EFATAL:
		case USBH_PACKET_CALLBACK_STATUS_EAGAIN:
		case USBH_PACKET_CALLBACK_STATUS_ERRSIZ:
			device_enumeration_terminate(dev);
			ERROR(cb_data.status);
			break;
		}
		break;

	case USBH_ENUM_STATE_DEVICE_DT_READ_SETUP:
		{
			struct usb_setup_data setup_data;

			setup_data.bmRequestType = USB_REQ_TYPE_IN | USB_REQ_TYPE_STANDARD | USB_REQ_TYPE_DEVICE;
			setup_data.bRequest = USB_REQ_GET_DESCRIPTOR;
			setup_data.wValue = USB_DT_DEVICE << 8;
			setup_data.wIndex = 0;
			setup_data.wLength = USB_DT_DEVICE_SIZE;

			dev->state = USBH_ENUM_STATE_DEVICE_DT_READ;
			device_xfer_control_write_setup(&setup_data, sizeof(setup_data),
				device_enumerate, dev);
		}
		break;


	case USBH_ENUM_STATE_DEVICE_DT_READ:
		{
			switch (cb_data.status) {
			case USBH_PACKET_CALLBACK_STATUS_OK:
				dev->state = USBH_ENUM_STATE_DEVICE_DT_READ_COMPLETE;
				device_xfer_control_read(&usbh_buffer[0], USB_DT_DEVICE_SIZE,
					device_enumerate, dev);
				break;

			case USBH_PACKET_CALLBACK_STATUS_EAGAIN:
				CONTINUE_WITH(USBH_ENUM_STATE_DEVICE_DT_READ_SETUP);
				break;

			case USBH_PACKET_CALLBACK_STATUS_EFATAL:
			case USBH_PACKET_CALLBACK_STATUS_ERRSIZ:
				device_enumeration_terminate(dev);
				ERROR(cb_data.status);
				break;
			}
		}
		break;

	case USBH_ENUM_STATE_DEVICE_DT_READ_COMPLETE:
		{
			switch (cb_data.status) {
			case USBH_PACKET_CALLBACK_STATUS_OK:
				{
					struct usb_device_descriptor *ddt =
							(struct usb_device_descriptor *)&usbh_buffer[0];
					dev->packet_size_max0 = ddt->bMaxPacketSize0;
					LOG_PRINTF("Found device with vid=0x%04x pid=0x%04x\n", ddt->idVendor, ddt->idProduct);
					LOG_PRINTF("class=0x%02x subclass=0x%02x protocol=0x%02x\n", ddt->bDeviceClass, ddt->bDeviceSubClass, ddt->bDeviceProtocol);
					CONTINUE_WITH(USBH_ENUM_STATE_CONFIGURATION_DT_HEADER_READ_SETUP)
				}
				break;

			case USBH_PACKET_CALLBACK_STATUS_ERRSIZ:
				if (cb_data.transferred_length >= 8) {
					struct usb_device_descriptor *ddt =
						(struct usb_device_descriptor *)&usbh_buffer[0];
					dev->packet_size_max0 = ddt->bMaxPacketSize0;
					CONTINUE_WITH(USBH_ENUM_STATE_DEVICE_DT_READ_SETUP);
				} else {
					device_enumeration_terminate(dev);
					ERROR(cb_data.status);
				}
				break;

			case USBH_PACKET_CALLBACK_STATUS_EFATAL:
			case USBH_PACKET_CALLBACK_STATUS_EAGAIN:
				device_enumeration_terminate(dev);
				ERROR(cb_data.status);
				break;
			}
		}
		break;

	case USBH_ENUM_STATE_CONFIGURATION_DT_HEADER_READ_SETUP:
		{
			struct usb_setup_data setup_data;

			setup_data.bmRequestType = USB_REQ_TYPE_IN | USB_REQ_TYPE_STANDARD | USB_REQ_TYPE_DEVICE;
			setup_data.bRequest = USB_REQ_GET_DESCRIPTOR;
			setup_data.wValue = USB_DT_CONFIGURATION << 8;
			setup_data.wIndex = 0;
			setup_data.wLength = dev->packet_size_max0;

			dev->state = USBH_ENUM_STATE_CONFIGURATION_DT_HEADER_READ;
			device_xfer_control_write_setup(&setup_data, sizeof(setup_data),
				device_enumerate, dev);
		}
		break;

	case USBH_ENUM_STATE_CONFIGURATION_DT_HEADER_READ:
		{
			switch (cb_data.status) {
			case USBH_PACKET_CALLBACK_STATUS_OK:
				dev->state = USBH_ENUM_STATE_CONFIGURATION_DT_HEADER_READ_COMPLETE;
				device_xfer_control_read(&usbh_buffer[USB_DT_DEVICE_SIZE],
					dev->packet_size_max0, device_enumerate, dev);
				break;

			case USBH_PACKET_CALLBACK_STATUS_EFATAL:
			case USBH_PACKET_CALLBACK_STATUS_EAGAIN:
			case USBH_PACKET_CALLBACK_STATUS_ERRSIZ:
				device_enumeration_terminate(dev);
				ERROR(cb_data.status);
				break;
			}
		}
		break;

	case USBH_ENUM_STATE_CONFIGURATION_DT_HEADER_READ_COMPLETE:
		{
			switch (cb_data.status) {
			case USBH_PACKET_CALLBACK_STATUS_OK:
				CONTINUE_WITH(USBH_ENUM_STATE_CONFIGURATION_DT_READ_SETUP);
				break;

			case USBH_PACKET_CALLBACK_STATUS_ERRSIZ:
				if (cb_data.transferred_length >= USB_DT_CONFIGURATION_SIZE) {
					struct usb_config_descriptor *cdt =
						(struct usb_config_descriptor *)&usbh_buffer[USB_DT_DEVICE_SIZE];
					if (cb_data.transferred_length == cdt->wTotalLength) {
						LOG_PRINTF("Configuration descriptor read complete. length: %d\n", cdt->wTotalLength);
						CONTINUE_WITH(USBH_ENUM_STATE_FIND_DRIVER);
					}
				}
				break;

			case USBH_PACKET_CALLBACK_STATUS_EAGAIN:
			case USBH_PACKET_CALLBACK_STATUS_EFATAL:
				device_enumeration_terminate(dev);
				ERROR(cb_data.status);
				break;
			}
		}
		break;

	case USBH_ENUM_STATE_CONFIGURATION_DT_READ_SETUP:
		{
			struct usb_config_descriptor *cdt =
				(struct usb_config_descriptor *)&usbh_buffer[USB_DT_DEVICE_SIZE];
			struct usb_setup_data setup_data;
			LOG_PRINTF("Getting complete configuration descriptor of length: %d bytes\n", cdt->wTotalLength);
			setup_data.bmRequestType = USB_REQ_TYPE_IN | USB_REQ_TYPE_STANDARD | USB_REQ_TYPE_DEVICE;
			setup_data.bRequest = USB_REQ_GET_DESCRIPTOR;
			setup_data.wValue = USB_DT_CONFIGURATION << 8;
			setup_data.wIndex = 0;
			setup_data.wLength = cdt->wTotalLength;

			dev->state = USBH_ENUM_STATE_CONFIGURATION_DT_READ;
			device_xfer_control_write_setup(&setup_data, sizeof(setup_data),
				device_enumerate, dev);
		}
		break;

	case USBH_ENUM_STATE_CONFIGURATION_DT_READ:
		{
			switch (cb_data.status) {
			case USBH_PACKET_CALLBACK_STATUS_OK:
				{
					struct usb_config_descriptor *cdt =
						(struct usb_config_descriptor *)&usbh_buffer[USB_DT_DEVICE_SIZE];
					dev->state = USBH_ENUM_STATE_CONFIGURATION_DT_READ_COMPLETE;
					device_xfer_control_read(&usbh_buffer[USB_DT_DEVICE_SIZE],
						cdt->wTotalLength, device_enumerate, dev);
				}
				break;

			case USBH_PACKET_CALLBACK_STATUS_EFATAL:
			case USBH_PACKET_CALLBACK_STATUS_EAGAIN:
			case USBH_PACKET_CALLBACK_STATUS_ERRSIZ:
				device_enumeration_terminate(dev);
				ERROR(cb_data.status);
				break;
			}
		}
		break;

	case USBH_ENUM_STATE_CONFIGURATION_DT_READ_COMPLETE:
		{
			switch (cb_data.status) {
			case USBH_PACKET_CALLBACK_STATUS_OK:
				{
					struct usb_config_descriptor *cdt =
						(struct usb_config_descriptor *)&usbh_buffer[USB_DT_DEVICE_SIZE];
					LOG_PRINTF("Configuration descriptor read complete. length: %d\n", cdt->wTotalLength);
					CONTINUE_WITH(USBH_ENUM_STATE_FIND_DRIVER);

				}
				break;

			case USBH_PACKET_CALLBACK_STATUS_EFATAL:
			case USBH_PACKET_CALLBACK_STATUS_EAGAIN:
			case USBH_PACKET_CALLBACK_STATUS_ERRSIZ:
				device_enumeration_terminate(dev);
				ERROR(cb_data.status);
				break;
			}

		}
		break;

	case USBH_ENUM_STATE_FIND_DRIVER:
		{
			struct usb_config_descriptor *cdt =
				(struct usb_config_descriptor *)&usbh_buffer[USB_DT_DEVICE_SIZE];
			device_register(usbh_buffer, cdt->wTotalLength + USB_DT_DEVICE_SIZE, dev);

			device_enumeration_finish(dev);
		}
		break;

	default:
		LOG_PRINTF("Error: Unknown state "__FILE__"/%d\n", __LINE__);
		break;
	}

	if (state_start == dev->state) {
		LOG_PRINTF("\n !HANG %d\n", state_start);
	}
}

void device_enumeration_start(usbh_device_t *dev)
{
	set_enumeration();
	dev->state = 1;

	// save address
	uint8_t address = dev->address;
	dev->address = 0;

	if (dev->speed == USBH_SPEED_LOW) {
		dev->packet_size_max0 = 8;
	} else {
		dev->packet_size_max0 = 64;
	}

	usbh_data.address_temporary = address;

	LOG_PRINTF("\n\n\n ENUMERATION OF DEVICE@%d STARTED \n\n", address);

	struct usb_setup_data setup_data;

	setup_data.bmRequestType = USB_REQ_TYPE_STANDARD | USB_REQ_TYPE_DEVICE;
	setup_data.bRequest = USB_REQ_SET_ADDRESS;
	setup_data.wValue = address;
	setup_data.wIndex = 0;
	setup_data.wLength = 0;

	device_xfer_control_write_setup(&setup_data, sizeof(setup_data),
		device_enumerate, dev);
}

/**
 * Should be called with at least 1kHz frequency
 *
 */
void usbh_poll(uint32_t time_curr_us)
{
	uint32_t k = 0;
	while (usbh_data.lld_drivers[k]) {
		usbh_device_t * usbh_device =
			((usbh_generic_data_t *)(usbh_data.lld_drivers[k]->driver_data))->usbh_device;
		usbh_generic_data_t *lld_data = usbh_data.lld_drivers[k]->driver_data;

		enum USBH_POLL_STATUS poll_status =
			usbh_data.lld_drivers[k]->poll(lld_data, time_curr_us);

		switch (poll_status) {
		case USBH_POLL_STATUS_DEVICE_CONNECTED:
			// New device found
			LOG_PRINTF("\nDEVICE FOUND\n");
			usbh_device[0].lld = usbh_data.lld_drivers[k];
			usbh_device[0].speed = usbh_data.lld_drivers[k]->root_speed(lld_data);
			usbh_device[0].address = 1;

			device_enumeration_start(&usbh_device[0]);
			break;

		case USBH_POLL_STATUS_DEVICE_DISCONNECTED:
			{
				// Device disconnected
				if (usbh_device[0].drv && usbh_device[0].drvdata) {
					usbh_device[0].drv->remove(usbh_device[0].drvdata);
				}
				usbh_device[0].drv = 0;
				usbh_device[0].drvdata = 0;

				uint32_t i;
				for (i = 1; i < USBH_MAX_DEVICES; i++) {
					usbh_device[i].address = -1;
					usbh_device[i].drv = 0;
					usbh_device[i].drvdata = 0;
				}
			}
			break;

		default:
			break;
		}

		if (lld_data->usbh_device[0].drv && usbh_device[0].drvdata) {
			usbh_device[0].drv->poll(usbh_device[0].drvdata, time_curr_us);
		}

		k++;
	}
}

void usbh_read(usbh_device_t *dev, usbh_packet_t *packet)
{
	const usbh_low_level_driver_t *lld = dev->lld;
	lld->read(lld->driver_data, packet);
}

void usbh_write(usbh_device_t *dev, const usbh_packet_t *packet)
{
	const usbh_low_level_driver_t *lld = dev->lld;
	lld->write(lld->driver_data, packet);
}

