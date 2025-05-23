set(lib_name herak_common)
add_library(${lib_name} INTERFACE)

set(dir ${CMAKE_CURRENT_LIST_DIR}/src)

if(${ADD_USB})
        set(COMMON_USB ${dir}/usb.c)
endif()

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
   ${dir}/system_modules.c
   ${dir}/system_weak.c
   ${dir}/lcd.cpp
   ${dir}/wifi.c
   ${dir}/mqtt_client.c
   ${dir}/bt.c
   ${dir}/log.c
   ${dir}/time.c
   ${dir}/webhook.c
   ${dir}/webserv.c
   ${dir}/webdebug.c
   ${dir}/temperature.cpp
   ${dir}/manchester_code.c
   ${CMAKE_BINARY_DIR}/${PARAMS_FILE}.c
   ${COMMON_USB}
)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

target_include_directories(${lib_name} INTERFACE
                           ${dir}      
			   ${CMAKE_CURRENT_LIST_DIR}
			   ${PROJECT_INLUDE_DIR}
			   ${PROJECT_LIB_DIR}/pio-usb/src
			   ${PROJECT_LIB_DIR}/hd44780/api
			   ${PROJECT_LIB_DIR}/common/api
)

include(${CMAKE_CURRENT_LIST_DIR}/devices/CMakeLists.txt)

add_subdirectory(${PROJECT_LIB_DIR}/pio-usb  ${CMAKE_BINARY_DIR}/pio-usb)
add_subdirectory(${PROJECT_LIB_DIR}/hd44780  ${CMAKE_BINARY_DIR}/hd44780)
target_link_libraries(${lib_name} INTERFACE pico_hd44780) 
target_link_libraries(${lib_name} INTERFACE pico_pio_usb)

target_link_libraries(${lib_name} INTERFACE pico_stdlib hardware_i2c
		      hardware_rtc hardware_adc pico_btstack_base pico_btstack_ble pico_btstack_classic
		      pico_btstack_cyw43 pico_cyw43_arch_lwip_threadsafe_background pico_lwip_sntp
		      pico_lwip_mqtt)

# enable all warnings
target_compile_options(${lib_name} INTERFACE -Wall -Wextra)

if(${DEBUG_BUILD})
	set(CMAKE_BUILD_TYPE "Debug")
	target_compile_options(${lib_name} INTERFACE -DPARAM_ASSERTIONS_ENABLE_ALL=1 -DPICO_CYW43_ARCH_DEBUG_ENABLED)
endif()
