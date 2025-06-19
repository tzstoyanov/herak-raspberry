option(ADD_SYS_FS "File System" ON)
add_compile_definitions(HAVE_SYS_TFTP_SERVER=1)

target_sources(${lib_name} INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}/tftp_srv.c
)

target_include_directories(${lib_name} INTERFACE ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(${lib_name} INTERFACE pico_lwip_tftp)

# enable all warnings
target_compile_options(${lib_name} INTERFACE -Wall -Wextra)