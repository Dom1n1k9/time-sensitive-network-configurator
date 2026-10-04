idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES htsn_agent htsn_prov htsn_version htsn_ota nvs_flash esp_wifi esp_netif esp_event driver lwip mdns
)

# Optional fixed per-board device id (overrides the runtime auto-detection):
#   idf.py build -D HTSN_DEVICE_ID=esp32-01
# Leave unset to auto-pick (BME280 present -> esp32-01, else esp32-02 relay).
if(DEFINED HTSN_DEVICE_ID)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE HTSN_DEVICE_ID="${HTSN_DEVICE_ID}")
endif()
