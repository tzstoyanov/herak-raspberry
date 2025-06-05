add_compile_definitions(HAVE_SYS_FS=1)
add_compile_definitions(FS_SIZE=65536) #  64K File System size

target_sources(${lib_name} INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}/fs.c
)

target_include_directories(${lib_name} INTERFACE
							 ${CMAKE_CURRENT_LIST_DIR}
							 ${PROJECT_LIB_DIR}/littlefs-lib)
add_subdirectory(${PROJECT_LIB_DIR}/littlefs-lib  ${CMAKE_BINARY_DIR}/littlefs-lib)
target_link_libraries(${lib_name} INTERFACE littlefs-lib)

# enable all warnings
target_compile_options(${lib_name} INTERFACE -Wall -Wextra)