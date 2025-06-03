add_compile_definitions(HAVE_SYS_USB=1)

target_sources(${lib_name} INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}/usb.c
)

add_subdirectory(${PROJECT_LIB_DIR}/pio-usb  ${CMAKE_BINARY_DIR}/pio-usb)
target_include_directories(${lib_name} INTERFACE 
							${CMAKE_CURRENT_LIST_DIR}
							${CMAKE_CURRENT_LIST_DIR}/cfg
							${PROJECT_LIB_DIR}/pio-usb/src
						  )

set(PICO_PIO_USB_PATH ${PROJECT_LIB_DIR}/pio-usb)

target_link_libraries(${lib_name} INTERFACE 
						tinyusb_host
						tinyusb_board
						pico_pio_usb
					 )

# enable all warnings
target_compile_options(${lib_name} INTERFACE -Wall -Wextra)
