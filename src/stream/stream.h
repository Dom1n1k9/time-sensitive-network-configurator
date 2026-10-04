#ifndef HTSN_STREAM_H
#define HTSN_STREAM_H

#include "common/common.h"

/* TSN stream status following the 802.1Qcc reservation lifecycle:
 * CONFIGURED - requested by CNC, waiting for reservation
 * READY      - reservation established, all endpoints configured
 * FAILED     - reservation could not be satisfied (no path / bad params)
 * STANDBY    - reserved but not active (e.g. redundancy partner)
 */
typedef enum {
    HTSN_STREAM_CONFIGURED = 0,
    HTSN_STREAM_READY = 1,
    HTSN_STREAM_FAILED = 2,
    HTSN_STREAM_STANDBY = 3
} htsn_stream_status;

typedef enum {
    HTSN_STREAM_ROLE_TALKER = 0,
    HTSN_STREAM_ROLE_LISTENER = 1,
    HTSN_STREAM_ROLE_COUNT
} htsn_stream_role;

/* A wildcard "all devices" member (802.1Qcc all-listeners) */
#define HTSN_STREAM_ALL_LISTENERS "*"

typedef struct {
    char stream_id[HTSN_MAX_STR];       /* IEEE 802.1Qtalker stream ID */
    char name[HTSN_MAX_STR];
    char talker[HTSN_MAX_STR];          /* talker device id */
    int vlan_id;                        /* 0 = none */
    int64_t max_latency_ns;           /* PCP / max latency */
    int64_t max_interval_ns;          /* stream interval / TASA */
    int priority;                     /* 0-7 */
    int data_frame_prio;            /* data frame priority */
    htsn_stream_status status;
    char comment[HTSN_MAX_STR];
    /* members: talker at index 0, followed by listeners */
    char listeners[HTSN_MAX_DEVICES][HTSN_MAX_STR];
    size_t listener_count;
    char listener_all;            /* true if ALL listener wildcard set */
} htsn_stream;

htsn_error htsn_stream_validate(const htsn_stream *s);
htsn_stream_status htsn_stream_status_parse(const char *s);
const char *htsn_stream_status_str(htsn_stream_status st);
const char *htsn_stream_role_str(htsn_stream_role r);

#endif
