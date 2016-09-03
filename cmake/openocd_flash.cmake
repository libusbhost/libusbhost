find_program (OOCD openocd)

set (OOCD_INTERFACE stlink-v2 CACHE STRING "interface config file used for openocd flashing")
set (OOCD_BOARD stm32f4discovery CACHE STRING "board config file used for openocd flashing")

add_custom_target (flash
	COMMAND sh -c '${OOCD} -f interface/${OOCD_INTERFACE}.cfg
		-f board/${OOCD_BOARD}.cfg
		-c "init" -c "reset init"
		-c "flash write_image erase $<TARGET_FILE:demo_f4>"
		-c "reset"
		-c "shutdown" '
	DEPENDS demo_f4
)
