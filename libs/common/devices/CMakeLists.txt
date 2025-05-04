

target_sources(${lib_name} INTERFACE
   ${CMAKE_CURRENT_LIST_DIR}/devices_init.c
)

target_include_directories(${lib_name} INTERFACE ${CMAKE_CURRENT_LIST_DIR})

include(${CMAKE_CURRENT_LIST_DIR}/ssr/CMakeLists.txt)
include(${CMAKE_CURRENT_LIST_DIR}/opentherm/CMakeLists.txt)

# enable all warnings
target_compile_options(${lib_name} INTERFACE -Wall -Wextra)
