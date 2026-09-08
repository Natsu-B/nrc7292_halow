idf_component_register(
    SRCS "src/nrc7292.c" "src/nrc_l2_protocol.c" "hspi/nrc-hspi.c"
    INCLUDE_DIRS "include" "hspi"
    REQUIRES esp_netif esp_event driver esp_timer
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-function -Wno-unused-parameter)
