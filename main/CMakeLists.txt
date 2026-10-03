idf_component_register(SRCS "hub.cpp" "credentials.cpp" "lighting.cpp" "network_home.cpp" INCLUDE_DIRS "."
    REQUIRES esp_matter esp_matter_controller esp_matter_console openthread nvs_flash esp_timer esp_driver_usb_serial_jtag
        esp_wifi esp_netif esp_event esp_hap_core esp_hap_apple_profiles mbedtls)
target_compile_options(${COMPONENT_LIB} PRIVATE "-DCHIP_HAVE_CONFIG_H")
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/paa_cert/manifest.json")
    message(FATAL_ERROR "Run scripts/prepare-paa.py with the pinned CHIP checkout before building")
endif()
spiffs_create_partition_image(paa_cert ${CMAKE_SOURCE_DIR}/paa_cert FLASH_IN_PROJECT)
