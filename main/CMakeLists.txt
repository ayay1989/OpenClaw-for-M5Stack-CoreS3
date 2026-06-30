idf_component_register(
    SRCS
        "main.c"
        "audiodriver.c"
        "lcddriver.c"
        "i2c_bus.c"
        "leddriver.c"
        "body_service.c"
        "py32driver.c"
        "scservo_bus.c"
        "servodriver.c"
        "si12tdriver.c"
        "presence.c"
        "diagnostics.c"
        "visuals.c"
        "body_events.c"
        "selftest.c"
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
