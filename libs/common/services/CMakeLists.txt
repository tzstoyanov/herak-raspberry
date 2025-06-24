target_include_directories(${lib_name} INTERFACE ${CMAKE_CURRENT_LIST_DIR})

if(ADD_SYS_FS)
   include(${CMAKE_CURRENT_LIST_DIR}/fs/CMakeLists.txt)
endif()

if(ADD_SYS_CFG_STORE)
   include(${CMAKE_CURRENT_LIST_DIR}/cfg_store/CMakeLists.txt)
endif()

if(ADD_SYS_LOG)
   include(${CMAKE_CURRENT_LIST_DIR}/log/CMakeLists.txt)
endif()

if(ADD_SYS_WIFI)
   include(${CMAKE_CURRENT_LIST_DIR}/wifi/CMakeLists.txt)
endif()

if(ADD_BT_STACK)
   include(${CMAKE_CURRENT_LIST_DIR}/bt/CMakeLists.txt)
endif()

if(ADD_MQTT_CLIENT)
   include(${CMAKE_CURRENT_LIST_DIR}/mqtt/CMakeLists.txt)
endif()

if(ADD_USB_HOST)
   include(${CMAKE_CURRENT_LIST_DIR}/usb/CMakeLists.txt)
endif()

if(ADD_NTP)
   include(${CMAKE_CURRENT_LIST_DIR}/ntp/CMakeLists.txt)
endif()

if(ADD_WEBSERVER)
   include(${CMAKE_CURRENT_LIST_DIR}/webserver/CMakeLists.txt)
endif()

if(ADD_WEBHOOK)
   include(${CMAKE_CURRENT_LIST_DIR}/webhook/CMakeLists.txt)
endif()

if(ADD_SYS_COMMANDS)
   include(${CMAKE_CURRENT_LIST_DIR}/syscmd/CMakeLists.txt)
endif()

if(ADD_SYS_TFTP_SERVER)
   include(${CMAKE_CURRENT_LIST_DIR}/tftp_srv/CMakeLists.txt)
endif()

if(ADD_COMMANDS)
   include(${CMAKE_CURRENT_LIST_DIR}/commands/CMakeLists.txt)
endif()

target_sources(${lib_name} INTERFACE
   ${CMAKE_CURRENT_LIST_DIR}/systems_init.c
)

# enable all warnings
target_compile_options(${lib_name} INTERFACE -Wall -Wextra)
