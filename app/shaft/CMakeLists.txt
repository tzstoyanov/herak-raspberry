# Set minimum required version of CMake
cmake_minimum_required(VERSION 3.12)

# Include build functions from Pico SDK
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

# Set name of project (as PROJECT_NAME) and C/C++ standards
project(herak-shaft C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(PICO_BOARD pico_w)

add_compile_definitions(CYW43_HOST_NAME=\"${PROJECT_NAME}\")

set(PROJECT_TOP_DIR ${CMAKE_CURRENT_LIST_DIR}/../..)
set(PROJECT_LIB_DIR ${CMAKE_CURRENT_LIST_DIR}/../../libs)
set(PROJECT_INLUDE_DIR ${CMAKE_CURRENT_LIST_DIR}/../../include)
set(PARAMS_FILE params)
set(PARAMS_TEMPLETE ${PROJECT_TOP_DIR}/app/params_example)

add_custom_target( check-params
  COMMAND test -e ${CMAKE_CURRENT_LIST_DIR}/${PARAMS_FILE}.txt ||
		  echo "File ${CMAKE_CURRENT_LIST_DIR}/${PARAMS_FILE}.txt does not exist"
  COMMAND test -e ${CMAKE_CURRENT_LIST_DIR}/${PARAMS_FILE}.txt ||
  		echo "Please, copy ${PARAMS_TEMPLETE}.txt as\
			${CMAKE_CURRENT_LIST_DIR}/${PARAMS_FILE}.txt and modify it with your settings."
  COMMAND test -e ${CMAKE_CURRENT_LIST_DIR}/${PARAMS_FILE}.txt
  COMMENT "Checking if ${CMAKE_CURRENT_LIST_DIR}/${PARAMS_FILE}.txt exists..."
)

add_custom_target( clear-params
  COMMAND rm -f "${CMAKE_CURRENT_LIST_DIR}/${PARAMS_FILE}.c" "${PROJECT_INLUDE_DIR}/${PARAMS_FILE}.h"
  COMMENT "Removing old ${PARAMS_FILE} artifacts..."
)

add_custom_command(OUTPUT "${CMAKE_CURRENT_LIST_DIR}/${PARAMS_FILE}.c" "${PROJECT_INLUDE_DIR}/${PARAMS_FILE}.h"
    COMMAND "${PROJECT_TOP_DIR}/params_crypt.sh" "${CMAKE_CURRENT_LIST_DIR}/${PARAMS_FILE}"
    COMMAND mv ${CMAKE_CURRENT_LIST_DIR}/${PARAMS_FILE}.h ${PROJECT_INLUDE_DIR}/${PARAMS_FILE}.h
    COMMENT "Generating ${PARAMS_FILE} code ..."
    DEPENDS check-params clear-params)

# Creates a pico-sdk subdirectory in our project for the libraries
pico_sdk_init()

# Tell CMake where to find the executable source file
add_executable(${PROJECT_NAME}
  ${CMAKE_CURRENT_LIST_DIR}/${PARAMS_FILE}.c
  ${CMAKE_CURRENT_LIST_DIR}/sonar.c
  ${CMAKE_CURRENT_LIST_DIR}/mqtt_shaft.c
  ${CMAKE_CURRENT_LIST_DIR}/main.c
)

target_include_directories(${PROJECT_NAME} PRIVATE
			   ${PROJECT_INLUDE_DIR}
			   ${CMAKE_CURRENT_LIST_DIR})

# Create map/bin/hex/uf2 files
pico_add_extra_outputs(${PROJECT_NAME})

add_library(herak_common INTERFACE)
target_sources(herak_common INTERFACE
  ${PROJECT_LIB_DIR}/common/base64.c
  ${PROJECT_LIB_DIR}/common/system.c
  ${PROJECT_LIB_DIR}/common/system_weak.c
  ${PROJECT_LIB_DIR}/common/lcd.cpp
  ${PROJECT_LIB_DIR}/common/wifi.c
  ${PROJECT_LIB_DIR}/common/mqtt_client.c
  ${PROJECT_LIB_DIR}/common/bt.c
  ${PROJECT_LIB_DIR}/common/log.c
  ${PROJECT_LIB_DIR}/common/time.c
  ${PROJECT_LIB_DIR}/common/temperature.cpp
)
target_include_directories(herak_common INTERFACE ${PROJECT_INLUDE_DIR})

add_library(one_wire INTERFACE)
target_sources(one_wire INTERFACE
  ${PROJECT_LIB_DIR}/one-wire/one_wire.cpp
)
target_include_directories(one_wire INTERFACE ${PROJECT_INLUDE_DIR})

add_library(pico_hd44780 INTERFACE)
target_sources(pico_hd44780 INTERFACE
  ${PROJECT_LIB_DIR}/hd44780/HD44780_LCD_PCF8574.cpp
  ${PROJECT_LIB_DIR}/hd44780/HD44780_LCD_PCF8574_Print.cpp
)
target_include_directories(pico_hd44780 INTERFACE ${PROJECT_INLUDE_DIR})

# Pull in pico libraries that we need
target_link_libraries(${PROJECT_NAME} pico_stdlib hardware_i2c pico_hd44780 one_wire
		      hardware_rtc hardware_adc pico_btstack_base pico_btstack_ble pico_btstack_classic
		      pico_btstack_cyw43 pico_cyw43_arch_lwip_threadsafe_background
		      pico_lwip_sntp pico_lwip_mqtt herak_common)

# Enable usb output, disable uart output
pico_enable_stdio_usb(${PROJECT_NAME} 1)
pico_enable_stdio_uart(${PROJECT_NAME} 0)
