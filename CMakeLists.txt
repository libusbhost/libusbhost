cmake_minimum_required (VERSION 2.6)

# initialize compiler
include (cmake/toolchain.cmake)

# initialize flashing
include (cmake/openocd_flash.cmake)

# initilize doc
include (cmake/doc.cmake)

project (libusbhost C)

# Declare cached variables

set (USE_STM32F4_FS TRUE CACHE BOOL "Use USB full speed (FS) host periphery")
set (USE_STM32F4_HS FALSE CACHE BOOL "Use USB high speed (HS) host periphery")
set (USE_USART_DEBUG TRUE CACHE BOOL "Use debug uart output")

# Set compiler and linker flags

set (FP_FLAGS
	"-mfloat-abi=hard -mfpu=fpv4-sp-d16 -mfp16-format=alternative"
)

set (ARCH_FLAGS
	"-mthumb -mcpu=cortex-m4 ${FP_FLAGS}"
)
set (COMMON_FLAGS
	"-O2 -g -Wextra -Wshadow -Wredundant-decls -fno-common -ffunction-sections -fdata-sections"
)

set (CMAKE_C_FLAGS
	"${COMMON_FLAGS} ${ARCH_FLAGS} -Wstrict-prototypes -Wmissing-prototypes -Wimplicit-function-declaration"
)

set (CMAKE_CXX_FLAGS
	"${COMMON_FLAGS} ${ARCH_FLAGS} -Weffc++"
)

# C preprocessor flags
set (CPP_FLAGS
	" -MD -Wall -Wundef"
)

add_definitions (${CPP_FLAGS})

# set platform
add_definitions (-DSTM32F4)

set (CMAKE_EXE_LINKER_FLAGS
	"--static -nostartfiles -T${CMAKE_SOURCE_DIR}/libusbhost_stm32f4.ld -Wl,-Map=FIXME_ONE.map -Wl,--gc-sections -Wl,--start-group -lc -lgcc -lnosys -Wl,--end-group"
)

include_directories (${CMAKE_SOURCE_DIR}/include)

function (init_libopencm3)
	include_directories (${CMAKE_SOURCE_DIR}/libopencm3/include)
	link_directories (${CMAKE_SOURCE_DIR}/libopencm3/lib)
	set (LIBOPENCM3_LIB opencm3_stm32f4 PARENT_SCOPE)
	execute_process (
		COMMAND sh "${CMAKE_SOURCE_DIR}/initRepo.sh"
		WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
		OUTPUT_QUIET
	)
endfunction (init_libopencm3)

message (STATUS "Initializing repository")
init_libopencm3 ()
message (STATUS "Repository initialized")

# Process cached varibles
message (STATUS "Setuping build")
if (USE_STM32F4_FS)
	message (STATUS "... Using USB full speed (FS) host periphery")
	add_definitions (-DUSE_STM32F4_USBH_DRIVER_FS)
endif (USE_STM32F4_FS)

if (USE_STM32F4_HS)
	message (STATUS "... Using USB high speed (HS) host periphery")
	add_definitions (-DUSE_STM32F4_USBH_DRIVER_HS)
endif (USE_STM32F4_HS)

if (USE_USART_DEBUG)
	message (STATUS "... Using debug uart output")
	add_definitions (-DUSART_DEBUG)
endif (USE_USART_DEBUG)
message (STATUS "Setup done")

add_custom_target (README.md
	SOURCES README.md
)

add_subdirectory (src)
