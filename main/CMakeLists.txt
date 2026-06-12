idf_component_register(
    SRCS
        "main.c"
        "lcddriver.c"
        "i2c_bus.c"
        "leddriver.c"
        "body_service.c"
        "py32driver.c"
        "scservo_bus.c"
        "servodriver.c"
        "presence.c"
        "protocol.c"
        "button.c"
        "emotions_data.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_wifi
        esp_netif
        esp_event
        nvs_flash
        driver
        json
        esp_timer
)

target_link_libraries(${COMPONENT_LIB} PRIVATE m)
