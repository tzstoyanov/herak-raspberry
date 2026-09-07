add_compile_definitions(HAVE_SONIC_A20=1)

option(ADD_MQTT_CLIENT "MQTT Client" ON)

pico_generate_pio_header(${lib_name} ${CMAKE_CURRENT_LIST_DIR}/dev_uart.pio)

target_sources(${lib_name} INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}/sonic_a20.c
  ${CMAKE_CURRENT_LIST_DIR}/dev_uart.c
)

target_include_directories(${lib_name} INTERFACE ${CMAKE_CURRENT_LIST_DIR})

# enable all warnings
target_compile_options(${lib_name} INTERFACE -Wall -Wextra)
