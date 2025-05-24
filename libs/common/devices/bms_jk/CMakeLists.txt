add_compile_definitions(HAVE_BMS_JK=1)

target_sources(${lib_name} INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}/bms_jk.c
  ${CMAKE_CURRENT_LIST_DIR}/bms_jk_mqtt.c
)

target_include_directories(${lib_name} INTERFACE ${CMAKE_CURRENT_LIST_DIR})

# enable all warnings
target_compile_options(${lib_name} INTERFACE -Wall -Wextra)
