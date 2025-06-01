target_include_directories(${lib_name} INTERFACE ${CMAKE_CURRENT_LIST_DIR})

if(ADD_BT_STACK)
   include(${CMAKE_CURRENT_LIST_DIR}/bt/CMakeLists.txt)
endif()

target_sources(${lib_name} INTERFACE
   ${CMAKE_CURRENT_LIST_DIR}/systems_init.c
)

# enable all warnings
target_compile_options(${lib_name} INTERFACE -Wall -Wextra)
