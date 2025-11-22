set(lib_name herak_common)
add_library(${lib_name} INTERFACE)

set(dir ${CMAKE_CURRENT_LIST_DIR}/src)

option(ADD_SYS_LOG "System Log" ON)
option(ADD_SYS_WIFI "WIFI" ON)
option(ADD_NTP "NTP Client" ON)
option(ADD_WEBSERVER "Web Server" ON)
option(ADD_WEBHOOK "WebHook service" ON)
option(ADD_SYS_COMMANDS "System Commands" ON)
option(ADD_TEMPERATURE "Temperature sensors" ON) 
option(ADD_COMMANDS "Commands engine" ON)
option(ADD_SYS_STATE "System status log" ON)
option(ADD_SYS_TFTP_CLIENT "TFTP Client" ON)

if ( NOT DEFINED ADD_OTA )
option(ADD_OTA "Bootloader and OTA updates" ON)
endif()

# Depend on FS
option(ADD_SYS_FS "File System" ON)
option(ADD_SYS_CFG_STORE "Config Store" ON)
option(ADD_SYS_SCRIPTS "Scripts" ON)

add_compile_definitions(CYW43_HOST_NAME=\"${PROJECT_NAME}\")
add_compile_definitions(PICO_PLATFORM_STR="${PICO_PLATFORM}")

set(PARAMS_FILE params)
set_source_files_properties(${CMAKE_BINARY_DIR}/${PARAMS_FILE}.c PROPERTIES GENERATED TRUE)

add_custom_target( clear-params
  COMMAND rm -f "${CMAKE_BINARY_DIR}/${PARAMS_FILE}.c" "${PROJECT_INLUDE_DIR}/${PARAMS_FILE}.h"
  COMMENT "Removing old ${PARAMS_FILE} artifacts..."
)

add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/${PARAMS_FILE}.c" "${PROJECT_INLUDE_DIR}/${PARAMS_FILE}.h"
    COMMAND "${PROJECT_TOP_DIR}/scripts/params_crypt.sh" "${CMAKE_BINARY_DIR}/${PARAMS_FILE}" "${PROJECT_TOP_DIR}/scripts/params_all.txt"
    COMMAND mv ${CMAKE_BINARY_DIR}/${PARAMS_FILE}.h ${PROJECT_INLUDE_DIR}/${PARAMS_FILE}.h
    COMMENT "Generating ${PARAMS_FILE} code ..."
    DEPENDS clear-params)

target_sources(${lib_name} INTERFACE
   ${dir}/base64.c
   ${dir}/system.c
   ${dir}/sys_utils.c
   ${dir}/sys_irq.c
   ${dir}/system_modules.c
   ${dir}/time.c
   ${dir}/manchester_code.c
   ${CMAKE_BINARY_DIR}/${PARAMS_FILE}.c
)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

if(ADD_OTA)
if (NOT BOOT_AES_KEY)
set(APP_FILE ${PROJECT_NAME}_fota_image.bin)
else()
set(APP_FILE ${PROJECT_NAME}_fota_image_encrypted.bin)
endif()
else()
set(APP_FILE ${PROJECT_NAME}.uf2)
endif()

# Read version from file
file(READ "${PROJECT_TOP_DIR}/VERSION" PROJECT_VERSION_CONTENT)
string(STRIP "${PROJECT_VERSION_CONTENT}" PROJECT_VERSION)
string(TIMESTAMP BUILD_DATE "%d.%m.%Y")
string(TIMESTAMP BUILD_TIME "%H:%M:%S")
string(STRIP "${PROJECT_NAME}" IMAGE_NAME)
string(STRIP "${APP_FILE}" IMAGE_FILE)
string(STRIP "${PICO_PLATFORM}" DEV_ARCH)

# Get current commit hash (short)
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${PROJECT_TOP_DIR}
    OUTPUT_VARIABLE GIT_COMMIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Generate version.h from template
configure_file(
    ${PROJECT_TOP_DIR}/version.h.in
    ${PROJECT_TOP_DIR}/include/version.h
    @ONLY
)

target_include_directories(${lib_name} INTERFACE
							${dir}
							${CMAKE_CURRENT_LIST_DIR}
							${PROJECT_INLUDE_DIR}
							${CMAKE_CURRENT_LIST_DIR}/devices
							${CMAKE_CURRENT_LIST_DIR}/services
							${CMAKE_CURRENT_LIST_DIR}/api
)

include(${CMAKE_CURRENT_LIST_DIR}/devices/CMakeLists.txt)
include(${CMAKE_CURRENT_LIST_DIR}/services/CMakeLists.txt)

target_link_libraries(${lib_name} INTERFACE
						pico_stdlib
						pico_aon_timer
					 	pico_cyw43_arch_lwip_threadsafe_background
					 )

# enable all warnings
target_compile_options(${lib_name} INTERFACE -Wall -Wextra)

if(${DEBUG_BUILD})
	set(CMAKE_BUILD_TYPE "Debug")
	target_compile_options(${lib_name} INTERFACE -DPARAM_ASSERTIONS_ENABLE_ALL=1 -DPICO_CYW43_ARCH_DEBUG_ENABLED)
endif()
