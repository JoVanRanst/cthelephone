idf_component_register( SRCS "cthelephone_main.c"
                        REQUIRES driver
                        INCLUDE_DIRS "."
                        PRIV_REQUIRES esp_driver_dac)
