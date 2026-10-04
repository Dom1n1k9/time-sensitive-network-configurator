idf_component_register(
    SRCS "htsn_ota.c"
    INCLUDE_DIRS "."
    REQUIRES esp_https_ota esp_http_client esp_partition app_update log
)
