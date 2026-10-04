#ifndef HTSN_TIMESYNC_H
#define HTSN_TIMESYNC_H

#include "common/common.h"

#include <stdint.h>

typedef enum {
    HTSN_TIMESYNC_DISABLED = 0,
    HTSN_TIMESYNC_LOCAL_GRANDMASTER,
    HTSN_TIMESYNC_EXTERNAL_GRANDMASTER,
    HTSN_TIMESYNC_AUTO
} htsn_timesync_mode;

typedef struct {
    htsn_timesync_mode mode;
    char grandmaster[HTSN_MAX_STR];
    int64_t offset_ns;
    int64_t jitter_ns;
    int quality;
    bool gptp_active;
    char protocol[32];
    /* over-the-air sync report (per device) */
    int64_t report_offset_ns;
    int64_t report_jitter_ns;
    int report_packet_count;
    int report_packet_loss;
    char report_device[HTSN_MAX_STR];
} htsn_timesync_status;

const char *htsn_timesync_mode_str(htsn_timesync_mode m);
htsn_timesync_mode htsn_timesync_mode_parse(const char *s);
htsn_error htsn_timesync_validate_mode(htsn_timesync_mode m);

#endif
