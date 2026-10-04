#include "qos/qos.h"

#include <string.h>

#include "common/log.h"

htsn_error htsn_qos_validate(const htsn_qos_config_model *cfg) {
    if (!cfg) return HTSN_ERR_INVALID_ARG;
    if (cfg->priority < 0 || cfg->priority > 7)
        return HTSN_ERR_INVALID_ARG;
    if (cfg->bandwidth_kbps < 0 || cfg->bandwidth_kbps > 10000000)
        return HTSN_ERR_INVALID_ARG;
    if (cfg->latency_ms < 0 || cfg->latency_ms > 60000)
        return HTSN_ERR_INVALID_ARG;
    if (strlen(cfg->device_id) == 0)
        return HTSN_ERR_INVALID_ARG;
    return HTSN_OK;
}

const char *htsn_qos_tc_str(htsn_qos_traffic_class tc) {
    switch (tc) {
    case HTSN_QOS_TC_BEST_EFFORT: return "Best Effort";
    case HTSN_QOS_TC_AUDIO_VIDEO: return "Audio/Video";
    case HTSN_QOS_TC_CONTROLLED: return "Controlled";
    case HTSN_QOS_TC_CRITICAL: return "Critical";
    default: return "Unknown";
    }
}

const char *htsn_qos_latency_str(htsn_qos_latency_class lc) {
    switch (lc) {
    case HTSN_QOS_LATENCY_PRIORITY: return "Priority";
    case HTSN_QOS_LATENCY_SOFT_REAL_TIME: return "Soft Real-Time";
    case HTSN_QOS_LATENCY_HARD_REAL_TIME: return "Hard Real-Time";
    default: return "Unknown";
    }
}

const char *htsn_preemption_str(htsn_frame_preemption p) {
    switch (p) {
    case HTSN_PREEMPT_OFF: return "off";
    case HTSN_PREEMPT_EXPRESS_QUEUE: return "express-queue";
    case HTSN_PREEMPT_ON: return "on";
    default: return "off";
    }
}
