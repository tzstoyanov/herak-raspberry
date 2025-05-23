add_compile_definitions(HAVE_SSR=1)

target_sources(${lib_name} INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}/ssr.c
)

target_include_directories(${lib_name} INTERFACE ${CMAKE_CURRENT_LIST_DIR})

# enable all warnings
target_compile_options(${lib_name} INTERFACE -Wall -Wextra)
