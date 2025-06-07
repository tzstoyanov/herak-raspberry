option(ADD_SYS_FS "File System" ON)
add_compile_definitions(HAVE_SYS_CFG_STORE=1)

target_sources(${lib_name} INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}/cfg_store.c
)

target_include_directories(${lib_name} INTERFACE ${CMAKE_CURRENT_LIST_DIR})

# enable all warnings
target_compile_options(${lib_name} INTERFACE -Wall -Wextra)