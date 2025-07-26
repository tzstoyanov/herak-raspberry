add_compile_definitions(HAVE_TEMPERATURE=1)

target_sources(${lib_name} INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}/temperature.c
)

target_include_directories(${lib_name} INTERFACE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(${lib_name} INTERFACE hardware_adc)

# enable all warnings
target_compile_options(${lib_name} INTERFACE -Wall -Wextra)
