add_compile_definitions(HAVE_SYS_FS=1)

#  128K File System size
# If OTA is used and FS size is changed, a new boot loader must be compiled
# and flashed with the new FS size.
add_compile_definitions(FS_SIZE=131072)
set(PFB_RESERVED_FILESYSTEM_SIZE_KB 128)

target_sources(${lib_name} INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}/fs.c
  ${CMAKE_CURRENT_LIST_DIR}/fs_tftp.c
)

target_include_directories(${lib_name} INTERFACE
							 ${CMAKE_CURRENT_LIST_DIR}
							 ${PROJECT_LIB_DIR}/littlefs-lib)
add_subdirectory(${PROJECT_LIB_DIR}/littlefs-lib  ${CMAKE_BINARY_DIR}/littlefs-lib)
target_link_libraries(${lib_name} INTERFACE littlefs-lib)

# enable all warnings
target_compile_options(${lib_name} INTERFACE -Wall -Wextra)