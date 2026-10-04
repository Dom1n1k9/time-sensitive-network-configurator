#include "device/device.h"

#include "common/str_util.h"

#include <string.h>
#include <strings.h>

const char *htsn_device_status_str(htsn_device_status s) {
    switch (s) {
    case HTSN_DEVICE_ONLINE: return "online";
    case HTSN_DEVICE_OFFLINE: return "offline";
    case HTSN_DEVICE_ERROR: return "error";
    default: return "unknown";
    }
}

const char *htsn_device_kind_str(htsn_device_kind k) {
    switch (k) {
    case HTSN_DEVICE_KIND_ESP32: return "ESP32";
    case HTSN_DEVICE_KIND_RASPBERRYPI: return "RaspberryPi";
    case HTSN_DEVICE_KIND_STM32: return "STM32";
    default: return "Unknown";
    }
}

htsn_device_kind htsn_device_kind_parse(const char *s) {
    if (!s) return HTSN_DEVICE_KIND_UNKNOWN;
    if (strcasecmp(s, "ESP32") == 0) return HTSN_DEVICE_KIND_ESP32;
    if (strcasecmp(s, "RaspberryPi") == 0) return HTSN_DEVICE_KIND_RASPBERRYPI;
    if (strcasecmp(s, "STM32") == 0) return HTSN_DEVICE_KIND_STM32;
    return HTSN_DEVICE_KIND_UNKNOWN;
}

void htsn_device_add_tsn_feature(htsn_device *d, const char *feature) {
    if (!d || !feature || d->tsn_features_count >= HTSN_TSN_FEATURES_MAX) return;
    htsn_strlcpy(d->tsn_features[d->tsn_features_count], feature, 64);
    d->tsn_features_count++;
}
