add_compile_definitions(HAVE_OPENTHERM=1)

option(ADD_MQTT_CLIENT "MQTT Client" ON)

pico_generate_pio_header(${lib_name} ${CMAKE_CURRENT_LIST_DIR}/opentherm_dev.pio)

target_sources(${lib_name} INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}/opentherm_pio.c
  ${CMAKE_CURRENT_LIST_DIR}/opentherm_dev.c
  ${CMAKE_CURRENT_LIST_DIR}/opentherm_cmd.c
  ${CMAKE_CURRENT_LIST_DIR}/opentherm_mqtt.c
  ${CMAKE_CURRENT_LIST_DIR}/opentherm.c
)

target_include_directories(${lib_name} INTERFACE ${CMAKE_CURRENT_LIST_DIR})

# enable all warnings
target_compile_options(${lib_name} INTERFACE -Wall -Wextra)
