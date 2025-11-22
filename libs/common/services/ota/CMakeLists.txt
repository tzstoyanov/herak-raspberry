add_compile_definitions(HAVE_OTA=1)

option(ADD_SYS_TFTP_CLIENT "TFTP Client" ON)

if ( NOT DEFINED BOOT_AES_KEY )
option(PFB_WITH_IMAGE_ENCRYPTION "Enables image encryption using AES ECB algorithm" OFF)
else()
option(PFB_WITH_IMAGE_ENCRYPTION "Enables image encryption using AES ECB algorithm" ON)
set(PFB_AES_KEY ${BOOT_AES_KEY})
endif()

target_sources(${lib_name} INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}/ota.c
)

target_include_directories(${lib_name} INTERFACE
							 ${CMAKE_CURRENT_LIST_DIR}
							 ${PROJECT_LIB_DIR}/pico_fota_bootloader)
add_subdirectory(${PROJECT_LIB_DIR}/pico_fota_bootloader  ${CMAKE_BINARY_DIR}/pico_fota_bootloader)


target_link_libraries(${lib_name} INTERFACE pico_fota_bootloader_lib pico_lwip_tftp pico_mbedtls)

# enable all warnings
target_compile_options(${lib_name} INTERFACE -Wall -Wextra)