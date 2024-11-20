idf_component_register(SRCS 
                        "doorbell_buffer.c" 
                        "doorbell_camera.c" 
                        "doorbell_httpd.c" 
                        "doorbell_sound.c" 
                        "doorbell_main.c"
                        "doorbell_wifi.c"
                        EMBED_FILES "home.html" 
                        INCLUDE_DIRS "."
                        )
