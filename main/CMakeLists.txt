idf_component_register(
    SRCS
        "mouse_cursor_icon.c"
        "main.cpp"
        "board_lvgl.c"
    INCLUDE_DIRS "."
    REQUIRES
        gen_bmgr_codes
        hasp_fs
        hasp_ftp
        hasp_http
        hasp_log
        hasp_mqtt
        hasp_service
        hasp_wifi
        nvs_flash
        bblanchon__arduinojson
        espressif__esp_board_manager
        espressif__esp_lvgl_adapter
        lvgl__lvgl
)

# littlefs_create_partition_image(storage ${CMAKE_CURRENT_LIST_DIR}/../data/storage FLASH_IN_PROJECT)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Werror
    -Wno-error=missing-field-initializers
    -Wno-error=stringop-truncation
)

idf_build_set_property(
    COMPILE_OPTIONS
    "-Wno-error=format-truncation;-Wno-error=stringop-truncation"
    APPEND
)

# Strict compiler warnings for this component
idf_component_get_property(
    simple_ftp_lib
    cezap__simple_ftp_server
    COMPONENT_LIB
)

target_compile_options(${simple_ftp_lib} PRIVATE
    -Wno-error=format-truncation
    -Wno-error=stringop-truncation
)

message(STATUS "simple_ftp_server library = ${simple_ftp_lib}")