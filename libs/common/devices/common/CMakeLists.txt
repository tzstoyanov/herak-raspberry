target_sources(${lib_name} INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}/adc_sensor.c
)

target_include_directories(${lib_name} INTERFACE ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(${lib_name} INTERFACE)

# enable all warnings
target_compile_options(${lib_name} INTERFACE -Wall -Wextra)
