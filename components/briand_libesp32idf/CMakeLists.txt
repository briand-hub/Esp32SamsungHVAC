# CMakeList file for component.

idf_component_register(SRCS "BriandLibEsp32IDF.cpp"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_wifi spi_flash esp_psram nvs_flash)
