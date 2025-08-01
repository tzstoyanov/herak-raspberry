set(lib_name herak_common)
add_library(${lib_name} INTERFACE)

set(dir ${CMAKE_CURRENT_LIST_DIR}/src)

option(ADD_SYS_FS "File System" ON)
option(ADD_SYS_CFG_STORE "Config Store" ON)
option(ADD_SYS_LOG "System Log" ON)
option(ADD_SYS_WIFI "WIFI" ON)
option(ADD_NTP "NTP Client" ON)
option(ADD_WEBSERVER "Web Server" ON)
option(ADD_WEBHOOK "WebHook service" ON)
option(ADD_SYS_COMMANDS "System Commands" ON)
option(ADD_TEMPERATURE "Temperature sensors" ON) 
option(ADD_SYS_TFTP_SERVER "TFTP Server" ON)
option(ADD_COMMANDS "Commands engine" ON)
option(ADD_SYS_SCRIPTS "Scripts" ON)

add_compile_definitions(CYW43_HOST_NAME=\"${PROJECT_NAME}\")

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
						hardware_rtc
					 	pico_cyw43_arch_lwip_threadsafe_background
					 )

# enable all warnings
target_compile_options(${lib_name} INTERFACE -Wall -Wextra)

if(${DEBUG_BUILD})
	set(CMAKE_BUILD_TYPE "Debug")
	target_compile_options(${lib_name} INTERFACE -DPARAM_ASSERTIONS_ENABLE_ALL=1 -DPICO_CYW43_ARCH_DEBUG_ENABLED)
endif()
