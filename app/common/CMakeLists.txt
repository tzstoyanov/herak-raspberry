set(PROJECT_NAME herak-common)
set(HEAP_SIZE 32768)
set(DEBUG_BUILD false)

# Select the modules used in this application. ON / OFF
option(ADD_ONE_WIRE "OneWire" ON)		# # libs/common/devices/one_wire/README.md 
option(ADD_SSR "Solid State Relays" ON)		# libs/common/devices/ssr/README.md
option(ADD_SOIL "Soil sensor" ON)		# libs/common/devices/soil/README.md
option(ADD_SHT20 "SHT20 sensor" ON)		# libs/common/devices/sht20/README.md
option(ADD_OPENTHERM "OneTherm device" ON)	# libs/common/devices/opentherm/README.md
option(ADD_LCD "LCD HD44780" ON)		# libs/common/devices/lcd/README.md
option(ADD_BMS_JK "JK BMS" ON)			# libs/common/devices/bms_jk/README.md
option(ADD_SONAR "AJ-SR04M sonar sensor" ON)			# libs/common/devices/sonar/README.md
option(-B6 "Water Flow YF sensor" ON)			# libs/common/devices/flow_yf/README.md

# CMake settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
cmake_minimum_required(VERSION 3.12)

set(PROJECT_TOP_DIR ${CMAKE_CURRENT_LIST_DIR}/../..)
set(PROJECT_INLUDE_DIR ${PROJECT_TOP_DIR}/include)

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

# Init Pico SDK
set(PROJECT_LIB_DIR ${PROJECT_TOP_DIR}/libs)
set(ENV{PICO_SDK_PATH} "${PROJECT_LIB_DIR}/pico-sdk")
set(PICO_BOARD pico_w)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(${PROJECT_NAME} C CXX ASM)
pico_sdk_init()

# build the common library
include(${PROJECT_LIB_DIR}/common/CMakeLists.txt)

# The main source file
add_executable(${PROJECT_NAME}
  ${CMAKE_CURRENT_LIST_DIR}/main.c
)

target_include_directories(${PROJECT_NAME} PRIVATE ${PROJECT_INLUDE_DIR})
target_compile_options(${PROJECT_NAME} PRIVATE -DMEM_HEAP_SIZE=${HEAP_SIZE})
target_link_libraries(${PROJECT_NAME} PRIVATE pico_stdlib herak_common)

target_link_options(${PROJECT_NAME} PRIVATE -Xlinker --print-memory-usage)

pico_add_extra_outputs(${PROJECT_NAME})

# Enable usb output, disable uart output
pico_enable_stdio_usb(${PROJECT_NAME} 1)
pico_enable_stdio_uart(${PROJECT_NAME} 0)
