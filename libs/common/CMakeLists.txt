set(lib_name herak_common)
add_library(${lib_name} INTERFACE)

set(dir ${CMAKE_CURRENT_LIST_DIR}/src)

if(${ADD_USB})
        set(COMMON_USB ${dir}/usb.c)
endif()

target_sources(${lib_name} INTERFACE
   ${dir}/base64.c
   ${dir}/system.c
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
   ${COMMON_USB}
)

target_include_directories(${lib_name} INTERFACE ${dir})

# enable all warnings
target_compile_options(${lib_name} INTERFACE -Wall -Wextra)
