target_include_directories(${lib_name} INTERFACE ${CMAKE_CURRENT_LIST_DIR})

if(ADD_SSR)
   include(${CMAKE_CURRENT_LIST_DIR}/ssr/CMakeLists.txt)
endif()

if(ADD_SOIL)
   include(${CMAKE_CURRENT_LIST_DIR}/soil/CMakeLists.txt)
endif()

if(ADD_SHT20)
   include(${CMAKE_CURRENT_LIST_DIR}/sht20/CMakeLists.txt)
endif()

if(ADD_OPENTHERM)
include(${CMAKE_CURRENT_LIST_DIR}/opentherm/CMakeLists.txt)
endif()

target_sources(${lib_name} INTERFACE
   ${CMAKE_CURRENT_LIST_DIR}/devices_init.c
)

# enable all warnings
target_compile_options(${lib_name} INTERFACE -Wall -Wextra)
