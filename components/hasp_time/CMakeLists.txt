idf_component_register(
    SRCS "src/hasp_time.cpp"
    INCLUDE_DIRS "include"
    REQUIRES hasp_service
             esp_event
             lwip
             nvs_flash
    PRIV_REQUIRES bblanchon__arduinojson
)
