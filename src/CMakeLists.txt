if (USE_USART_DEBUG)
	set (USART_HELPERS
		usart_helpers.c
		tinyprintf.c
	)
else (USE_USART_DEBUG)
	set (USART_HELPERS "")
endif (USE_USART_DEBUG)

add_library (usbhost
	usbh_core.c
	${USART_HELPERS}
	usbh_driver_ac_midi.c
	usbh_driver_ac_midi_private.h
	usbh_driver_gp_xbox.c
	usbh_driver_hid_mouse.c
	usbh_driver_hub.c
	usbh_driver_hub_private.h
	usbh_lld_stm32f4.c
)

target_link_libraries (usbhost
	${LIBOPENCM3_LIB}
)

add_executable (demo_f4
	demo.c
)

target_link_libraries (demo_f4
	usbhost
)

add_custom_command (TARGET demo_f4
	POST_BUILD
	COMMAND ${CMAKE_OBJCOPY} -Oihex $<TARGET_FILE:demo_f4> ${CMAKE_BINARY_DIR}/demo_f4.hex
	COMMENT "Generating output files: ${CMAKE_BINARY_DIR}/demo_f4.hex"
)

add_custom_command (TARGET demo_f4
	POST_BUILD
	COMMAND ${CMAKE_SIZE} $<TARGET_FILE:demo_f4>
	COMMENT "Calculating size of the binary"
)

add_custom_command (TARGET usbhost
	POST_BUILD
	COMMENT "Calculating size of the library"
	COMMAND ${CMAKE_SIZE} $<TARGET_FILE:usbhost>
)

