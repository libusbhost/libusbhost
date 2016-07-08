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

#ifndef USBH_CORE_
#define USBH_CORE_

#include "usbh_config.h"

#include <stdint.h>
#include <stdbool.h>

/* This must be placed around external function declaration for C++
 * support. */
#ifdef __cplusplus
# define BEGIN_DECLS extern "C" {
# define END_DECLS }
#else
# define BEGIN_DECLS
# define END_DECLS
#endif

BEGIN_DECLS

/// set to -1 for unused items ("don't care" functionality) @see find_driver()
struct _usbh_dev_driver_info {
	int32_t deviceClass;
	int32_t deviceSubClass;
	int32_t deviceProtocol;
	int32_t idVendor;
	int32_t idProduct;
	int32_t ifaceClass;
	int32_t ifaceSubClass;
	int32_t ifaceProtocol;
};
typedef struct _usbh_dev_driver_info usbh_dev_driver_info_t;

struct _usbh_dev_driver {
	/**
	 * @brief init is initialization routine of the device driver
	 *
	 * This function is called during the initialization of the device driver
	 */
	void *(*init)(void *usbh_dev);

	/**
	 * @brief analyze descriptor
	 * @param[in/out] drvdata is the device driver's private data
	 * @param[in] descriptor is the pointer to the descriptor that should
	 *		be parsed in order to prepare driver to be loaded
	 *
	 * @retval true when the enumeration is complete and the driver is ready to be used
	 * @retval false when the device driver is not ready to be used
	 *
	 * This should be used for getting correct endpoint numbers, getting maximum sizes of endpoints.
	 * Should return true, when no more data is needed.
	 *
	 */
	bool (*analyze_descriptor)(void *drvdata, void *descriptor);

	/**
	 * @brief poll method is called periodically by the library core
	 * @param[in/out] drvdata is the device driver's private data
	 * @param[in] time_curr_us current timestamp in microseconds
	 * @see usbh_poll()
	 */
	void (*poll)(void *drvdata, uint32_t time_curr_us);

	/**
	 * @brief unloads the device driver
	 * @param[in/out] drvdata is the device driver's private data
	 *
	 * This should free any data associated with this device
	 */
	void (*remove)(void *drvdata);

	/**
	 * @brief info - compatibility information about the driver. It is used by the core during device enumeration
	 * @see find_driver()
	 */
	const usbh_dev_driver_info_t * const info;
};
typedef struct _usbh_dev_driver usbh_dev_driver_t;

/**
 * @brief usbh_init
 * @param low_level_drivers list of the low level drivers to be used by this library
 * @param device_drivers list of the device drivers that could be used with attached devices
 */
void usbh_init(const void *low_level_drivers[], const usbh_dev_driver_t * const device_drivers[]);

/**
 * @brief usbh_poll
 * @param time_curr_us - use monotically rising time
 *
 *	time_curr_us:
 *		* can overflow, in time of this writing, after 1s
 *		* unit is microseconds
 */
void usbh_poll(uint32_t time_curr_us);

END_DECLS

#endif // USBH_CORE_
