option(ADD_SYS_FS "File System" ON)
option(ADD_SYS_TFTP_SERVER "TFTP Server" ON)
add_compile_definitions(HAVE_SYS_SCRIPTS=1)
add_compile_definitions(CRON_USE_LOCAL_TIME=1)

target_sources(${lib_name} INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}/scripts.c
  ${CMAKE_CURRENT_LIST_DIR}/ccronexpr.c
)

target_include_directories(${lib_name} INTERFACE ${CMAKE_CURRENT_LIST_DIR})

# enable all warnings
target_compile_options(${lib_name} INTERFACE -Wall -Wextra)
