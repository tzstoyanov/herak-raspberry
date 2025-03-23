# Set minimum required version of CMake
cmake_minimum_required(VERSION 3.12)

# Include build functions from Pico SDK
set(PROJECT_LIB_DIR ${CMAKE_CURRENT_LIST_DIR}/../../libs)
set(ENV{PICO_SDK_PATH} "${PROJECT_LIB_DIR}/pico-sdk")
set(PICO_BOARD pico_w)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

# Set name of project (as PROJECT_NAME) and C/C++ standards
project(herak-shaft C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(DEBUG_BUILD false)

add_compile_definitions(CYW43_HOST_NAME=\"${PROJECT_NAME}\")

set(PROJECT_TOP_DIR ${CMAKE_CURRENT_LIST_DIR}/../..)
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
    COMMAND "${PROJECT_TOP_DIR}/scripts/params_crypt.sh" "${CMAKE_CURRENT_LIST_DIR}/${PARAMS_FILE}" "${PROJECT_TOP_DIR}/scripts/params_all.txt"
    COMMAND mv ${CMAKE_CURRENT_LIST_DIR}/${PARAMS_FILE}.h ${PROJECT_INLUDE_DIR}/${PARAMS_FILE}.h
    COMMENT "Generating ${PARAMS_FILE} code ..."
    DEPENDS check-params clear-params)

# Creates a pico-sdk subdirectory in our project for the libraries
pico_sdk_init()

#build required libraries
include(${PROJECT_LIB_DIR}/pio-usb/CMakeLists.txt)
include(${PROJECT_LIB_DIR}/one-wire/CMakeLists.txt)
include(${PROJECT_LIB_DIR}/hd44780/CMakeLists.txt)
include(${PROJECT_LIB_DIR}/common/CMakeLists.txt)

# Tell CMake where to find the executable source file
add_executable(${PROJECT_NAME}
  ${CMAKE_CURRENT_LIST_DIR}/${PARAMS_FILE}.c
  ${CMAKE_CURRENT_LIST_DIR}/sonar.c
  ${CMAKE_CURRENT_LIST_DIR}/mqtt_shaft.c
  ${CMAKE_CURRENT_LIST_DIR}/main.c
)
if(${DEBUG_BUILD})
	set(CMAKE_BUILD_TYPE "Debug")
	target_compile_options(${PROJECT_NAME} PRIVATE -DPARAM_ASSERTIONS_ENABLE_ALL=1 -DPICO_CYW43_ARCH_DEBUG_ENABLED)
endif()
target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra)

target_include_directories(${PROJECT_NAME} PRIVATE
			   ${CMAKE_CURRENT_LIST_DIR}
			   ${PROJECT_INLUDE_DIR}
			   ${PROJECT_LIB_DIR}/pio-usb/src
			   ${PROJECT_LIB_DIR}/one-wire/api
			   ${PROJECT_LIB_DIR}/hd44780/api
			   ${PROJECT_LIB_DIR}/common/api
)

# Create map/bin/hex/uf2 files
pico_add_extra_outputs(${PROJECT_NAME})

target_link_libraries(${PROJECT_NAME} pico_stdlib hardware_i2c
		      hardware_rtc hardware_adc pico_btstack_base pico_btstack_ble pico_btstack_classic
		      pico_btstack_cyw43 pico_cyw43_arch_lwip_threadsafe_background pico_lwip_sntp
		      pico_lwip_mqtt pico_hd44780 herak_common pico_pio_usb pico_one_wire)

target_link_options(${PROJECT_NAME} PRIVATE -Xlinker --print-memory-usage)

# Enable usb output, disable uart output
pico_enable_stdio_usb(${PROJECT_NAME} 1)
pico_enable_stdio_uart(${PROJECT_NAME} 0)
