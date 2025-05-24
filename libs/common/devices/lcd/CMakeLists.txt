add_compile_definitions(HAVE_LCD=1)

target_sources(${lib_name} INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}/lcd.cpp
)

target_include_directories(${lib_name} INTERFACE 
                           ${CMAKE_CURRENT_LIST_DIR}
                           ${PROJECT_LIB_DIR}/hd44780/api)

add_subdirectory(${PROJECT_LIB_DIR}/hd44780  ${CMAKE_BINARY_DIR}/hd44780)
target_link_libraries(${lib_name} INTERFACE hardware_i2c pico_hd44780) 

# enable all warnings
target_compile_options(${lib_name} INTERFACE -Wall -Wextra)
