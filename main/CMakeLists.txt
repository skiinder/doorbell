idf_component_register(SRCS 
                        "doorbell_camera.c" 
                        "doorbell_sound.c" 
                        "doorbell_main.c"
                        "doorbell_wifi.c"
                        "doorbell_mqtt.c"
                        "doorbell_wsclient.c"
                        "doorbell_ota.c"
                        "doorbell_button.c"
                        "doorbell_led.c"
                        INCLUDE_DIRS "."
                        EMBED_TXTFILES "output.pcm"
                        )
