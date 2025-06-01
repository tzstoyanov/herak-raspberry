add_compile_definitions(HAVE_SYS_BT=1)

target_sources(${lib_name} INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}/bt.c
)

target_include_directories(${lib_name} INTERFACE ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(${lib_name} INTERFACE 
						pico_btstack_base
						pico_btstack_ble
						pico_btstack_classic
						pico_btstack_cyw43
					 )

# enable all warnings
target_compile_options(${lib_name} INTERFACE -Wall -Wextra)
