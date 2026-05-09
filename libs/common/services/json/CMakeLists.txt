add_compile_definitions(HAVE_SYS_JSON=1)

target_include_directories(${lib_name} INTERFACE
							 ${CMAKE_CURRENT_LIST_DIR}
							 ${PROJECT_LIB_DIR}/lwjson/lwjson)
add_subdirectory(${PROJECT_LIB_DIR}/lwjson/lwjson  ${CMAKE_BINARY_DIR}/lwjson/lwjson)
target_link_libraries(${lib_name} INTERFACE lwjson)

# enable all warnings
target_compile_options(${lib_name} INTERFACE -Wall -Wextra)