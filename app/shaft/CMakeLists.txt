# Set minimum required version of CMake
cmake_minimum_required(VERSION 3.12)

# Include build functions from Pico SDK
set(PROJECT_LIB_DIR ${CMAKE_CURRENT_LIST_DIR}/../../libs)
set(ENV{PICO_SDK_PATH} "${PROJECT_LIB_DIR}/pico-sdk")
set(PICO_BOARD pico_w)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)


# Select the modules used in this application. ON / OFF
option(ADD_SSR "Solid State Relays" ON)         # libs/common/devices/ssr/README.md
option(ADD_SOIL "Soil sensor" ON)               # libs/common/devices/soil/README.md
option(ADD_SHT20 "SHT20 sensor" ON)             # libs/common/devices/sht20/README.md
option(ADD_ONE_WIRE "OneWire" ON)
option(ADD_LCD "LCD HD44780" ON)		# libs/common/devices/lcd/README.md


# Set name of project (as PROJECT_NAME) and C/C++ standards
project(herak-shaft C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(DEBUG_BUILD false)
set(HEAP_SIZE 8192)

add_compile_definitions(CYW43_HOST_NAME=\"${PROJECT_NAME}\")

set(PROJECT_TOP_DIR ${CMAKE_CURRENT_LIST_DIR}/../..)
set(PROJECT_INLUDE_DIR ${CMAKE_CURRENT_LIST_DIR}/../../include)


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

# Creates a pico-sdk subdirectory in our project for the libraries
pico_sdk_init()

#build required libraries
include(${PROJECT_LIB_DIR}/common/CMakeLists.txt)

# Tell CMake where to find the executable source file
add_executable(${PROJECT_NAME}
  ${CMAKE_CURRENT_LIST_DIR}/sonar.c
  ${CMAKE_CURRENT_LIST_DIR}/mqtt_shaft.c
  ${CMAKE_CURRENT_LIST_DIR}/main.c
)
if(${DEBUG_BUILD})
	set(CMAKE_BUILD_TYPE "Debug")
	target_compile_options(${PROJECT_NAME} PRIVATE -DPARAM_ASSERTIONS_ENABLE_ALL=1 -DPICO_CYW43_ARCH_DEBUG_ENABLED)
endif()
target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra)
target_compile_options(${PROJECT_NAME} PRIVATE -DMEM_HEAP_SIZE=${HEAP_SIZE})

target_include_directories(${PROJECT_NAME} PRIVATE
			   ${CMAKE_CURRENT_LIST_DIR}
			   ${PROJECT_INLUDE_DIR}
			   ${PROJECT_LIB_DIR}/pio-usb/src
			   ${PROJECT_LIB_DIR}/common/api
)

# Create map/bin/hex/uf2 files
pico_add_extra_outputs(${PROJECT_NAME})

target_link_libraries(${PROJECT_NAME} pico_stdlib hardware_i2c
		      hardware_rtc hardware_adc pico_btstack_base pico_btstack_ble pico_btstack_classic
		      pico_btstack_cyw43 pico_cyw43_arch_lwip_threadsafe_background pico_lwip_sntp
		      pico_lwip_mqtt herak_common pico_pio_usb)

target_link_options(${PROJECT_NAME} PRIVATE -Xlinker --print-memory-usage)
