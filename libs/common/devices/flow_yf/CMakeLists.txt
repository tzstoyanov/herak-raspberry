add_compile_definitions(HAVE_FLOW_YF=1)

option(ADD_MQTT_CLIENT "MQTT Client" ON)

target_sources(${lib_name} INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}/flow_yf.c
)

target_include_directories(${lib_name} INTERFACE ${CMAKE_CURRENT_LIST_DIR})

# enable all warnings
target_compile_options(${lib_name} INTERFACE -Wall -Wextra)
