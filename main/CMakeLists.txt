idf_component_register( SRCS "state_machine.c" "audio_player.c" "cthelephone_main.c"
                        REQUIRES driver
                        INCLUDE_DIRS "."
                        PRIV_REQUIRES esp_driver_dac)
