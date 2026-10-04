#ifndef HTSN_QOS_H
#define HTSN_QOS_H

#include "common/common.h"

typedef enum {
    HTSN_QOS_TC_BEST_EFFORT = 0,
    HTSN_QOS_TC_AUDIO_VIDEO,
    HTSN_QOS_TC_CONTROLLED,
    HTSN_QOS_TC_CRITICAL
} htsn_qos_traffic_class;

typedef enum {
    HTSN_QOS_LATENCY_PRIORITY = 0,
    HTSN_QOS_LATENCY_SOFT_REAL_TIME,
    HTSN_QOS_LATENCY_HARD_REAL_TIME
} htsn_qos_latency_class;

/* IEEE 802.1Qbu Frame Preemption: express frames preempt preemptable classes */
typedef enum {
    HTSN_PREEMPT_OFF = 0,
    HTSN_PREEMPT_EXPRESS_QUEUE,
    HTSN_PREEMPT_ON
} htsn_frame_preemption;

typedef struct {
    char device_id[HTSN_MAX_STR];
    int priority;
    htsn_qos_traffic_class traffic_class;
    int bandwidth_kbps;
    int latency_ms;
    htsn_qos_latency_class latency_class;
    htsn_frame_preemption preemption;
} htsn_qos_config_model;

htsn_error htsn_qos_validate(const htsn_qos_config_model *cfg);
const char *htsn_qos_tc_str(htsn_qos_traffic_class tc);
const char *htsn_qos_latency_str(htsn_qos_latency_class lc);
const char *htsn_preemption_str(htsn_frame_preemption p);

#endif
