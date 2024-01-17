set(lib_name pico_hd44780)
add_library(${lib_name} INTERFACE)

set(dir ${CMAKE_CURRENT_LIST_DIR})

target_sources(${lib_name} INTERFACE
	${dir}/HD44780_LCD_PCF8574.cpp
	${dir}/HD44780_LCD_PCF8574_Print.cpp
)

target_include_directories(${lib_name} INTERFACE
			   ${dir}
			   ${dir}/api
)

# enable all warnings
target_compile_options(${lib_name} INTERFACE -Wall -Wextra)
