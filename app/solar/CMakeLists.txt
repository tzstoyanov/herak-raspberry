# Set minimum required version of CMake
cmake_minimum_required(VERSION 3.12)

# Include build functions from Pico SDK
set(PROJECT_LIB_DIR ${CMAKE_CURRENT_LIST_DIR}/../../libs)
set(ENV{PICO_SDK_PATH} "${PROJECT_LIB_DIR}/pico-sdk")
set(PICO_BOARD pico_w)

# Select the modules used in this application. ON / OFF
option(ADD_BT_STACK "Bluetoth Stack" ON)
option(ADD_SSR "Solid State Relays" ON)         # libs/common/devices/ssr/README.md
option(ADD_SOIL "Soil sensor" ON)               # libs/common/devices/soil/README.md
option(ADD_SHT20 "SHT20 sensor" ON)             # libs/common/devices/sht20/README.md
option(ADD_ONE_WIRE "OneWire" ON)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

# Set name of project (as PROJECT_NAME) and C/C++ standards
project(herak-solar C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(ADD_USB true)
set(DEBUG_BUILD false)
set(HEAP_SIZE 2000)

set(PROJECT_TOP_DIR ${CMAKE_CURRENT_LIST_DIR}/../..)
set(PROJECT_INLUDE_DIR ${CMAKE_CURRENT_LIST_DIR}/../../include)

add_compile_definitions(CYW43_HOST_NAME=\"${PROJECT_NAME}\")

# Set params file
set(PARAMS_FILE ${CMAKE_CURRENT_LIST_DIR}/params.txt)
set(PARAMS_TEMPLETE ${PROJECT_TOP_DIR}/app/params_example.txt)
if(NOT EXISTS "${PARAMS_FILE}")
  message(WARNING "File ${PARAMS_FILE} does not exist.")
  message(FATAL_ERROR
    "Please, copy ${PARAMS_TEMPLETE} as ${PARAMS_FILE} and modify it with your settings.")
endif()
execute_process(
	COMMAND cp "${PARAMS_FILE}" ${CMAKE_BINARY_DIR}
	WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
)

if(${ADD_USB})
	set(TINY_USB_HOST tinyusb_host)
	set(TINY_USB_BOARD tinyusb_board)
	set(INLUDE_USB ${PROJECT_TOP_DIR}/libs/common/api/usb)
	set(PICO_PIO_USB pico_pio_usb)
	set(PICO_PIO_USB_PATH ${PROJECT_LIB_DIR}/pio-usb)
else()
	set(MPTT_TEST ${CMAKE_CURRENT_LIST_DIR}/mppt_voltronic_test.c)
endif()

# Creates a pico-sdk subdirectory in our project for the libraries
pico_sdk_init()

# Tell CMake where to find the executable source file
add_executable(${PROJECT_NAME}
  ${CMAKE_CURRENT_LIST_DIR}/bms_daly_proto.c
  ${CMAKE_CURRENT_LIST_DIR}/bms_daly_bt.c
  ${CMAKE_CURRENT_LIST_DIR}/mppt_voltronic_proto.c
  ${CMAKE_CURRENT_LIST_DIR}/mppt_voltronic_usb.c
  ${CMAKE_CURRENT_LIST_DIR}/mqtt_solar.c
  ${CMAKE_CURRENT_LIST_DIR}/wh_notify.c
  ${CMAKE_CURRENT_LIST_DIR}/main.c
  ${MPTT_TEST}
)

target_compile_definitions(${PROJECT_NAME} PRIVATE PIO_USB_USE_TINYUSB)


#build required libraries
include(${PROJECT_LIB_DIR}/common/CMakeLists.txt)

if(${DEBUG_BUILD})
	set(CMAKE_BUILD_TYPE "Debug")
	target_compile_options(${PROJECT_NAME} PRIVATE -DPARAM_ASSERTIONS_ENABLE_ALL=1 -DPICO_CYW43_ARCH_DEBUG_ENABLED)
endif()

target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra)
target_compile_options(${PROJECT_NAME} PRIVATE -DMEM_HEAP_SIZE=${HEAP_SIZE})

target_include_directories(${PROJECT_NAME} PRIVATE
			   ${CMAKE_CURRENT_LIST_DIR}
			   ${PROJECT_INCLUDE_DIR}
			   ${INLUDE_USB}
			   ${PROJECT_LIB_DIR}/pio-usb/src
			   ${PROJECT_LIB_DIR}/common/api
)

# Create map/bin/hex/uf2 files
pico_add_extra_outputs(${PROJECT_NAME})

target_link_libraries(${PROJECT_NAME} ${TINY_USB_HOST} ${TINY_USB_BOARD} ${PICO_PIO_USB}
		      pico_stdlib hardware_i2c hardware_rtc hardware_adc
		      hardware_dma hardware_pio
		      pico_cyw43_arch_lwip_threadsafe_background
		      pico_lwip_sntp pico_lwip_mqtt herak_common)

target_link_options(${PROJECT_NAME} PRIVATE -Xlinker --print-memory-usage)

# Enable usb output, disable uart output
if(${ADD_USB})
	pico_enable_stdio_usb(${PROJECT_NAME} 0)
	pico_enable_stdio_uart(${PROJECT_NAME} 0)
else()
	pico_enable_stdio_usb(${PROJECT_NAME} 1)
	pico_enable_stdio_uart(${PROJECT_NAME} 0)
endif()
