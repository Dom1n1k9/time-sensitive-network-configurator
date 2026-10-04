idf_component_register(
    SRCS "htsn_tsn.c" "htsn_mqtt.c" "htsn_cfg.c" "htsn_json.c" "htsn_ptp.c" "htsn_sensor.c" "htsn_wifimotion.c" "htsn_sonar.c" "htsn_display.c"
    INCLUDE_DIRS "."
    REQUIRES mqtt nvs_flash esp_wifi esp_netif esp_event esp_timer driver esp_adc lwip
)
