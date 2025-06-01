add_compile_definitions(HAVE_ONE_WIRE=1)

option(ADD_MQTT_CLIENT "MQTT Client" ON)

target_sources(${lib_name} INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}/one_wire_sensors.cpp
)

target_include_directories(${lib_name} INTERFACE 
                           ${CMAKE_CURRENT_LIST_DIR}
                           ${PROJECT_LIB_DIR}/one-wire/api)

add_subdirectory(${PROJECT_LIB_DIR}/one-wire  ${CMAKE_BINARY_DIR}/one-wire)
target_link_libraries(${lib_name} INTERFACE pico_one_wire)

# enable all warnings
target_compile_options(${lib_name} INTERFACE -Wall -Wextra)
