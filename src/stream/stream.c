#include "stream/stream.h"

#include <string.h>

htsn_error htsn_stream_validate(const htsn_stream *s) {
    if (!s) return HTSN_ERR_INVALID_ARG;
    if (strlen(s->stream_id) == 0) return HTSN_ERR_INVALID_ARG;
    if (strlen(s->talker) == 0) return HTSN_ERR_INVALID_ARG;
    if (s->vlan_id < 0 || s->vlan_id > 4094) return HTSN_ERR_INVALID_ARG;
    if (s->priority < 0 || s->priority > 7) return HTSN_ERR_INVALID_ARG;
    if (s->data_frame_prio < 0 || s->data_frame_prio > 7)
        return HTSN_ERR_INVALID_ARG;
    if (s->max_latency_ns <= 0) return HTSN_ERR_INVALID_ARG;
    if (s->max_interval_ns <= 0) return HTSN_ERR_INVALID_ARG;
    /* every stream needs at least one listener */
    if (!s->listener_all && s->listener_count == 0) return HTSN_ERR_INVALID_ARG;
    return HTSN_OK;
}

htsn_stream_status htsn_stream_status_parse(const char *s) {
    if (!s) return HTSN_STREAM_CONFIGURED;
    if (strcmp(s, "ready") == 0) return HTSN_STREAM_READY;
    if (strcmp(s, "failed") == 0) return HTSN_STREAM_FAILED;
    if (strcmp(s, "standby") == 0) return HTSN_STREAM_STANDBY;
    return HTSN_STREAM_CONFIGURED;
}

const char *htsn_stream_status_str(htsn_stream_status st) {
    switch (st) {
    case HTSN_STREAM_READY: return "ready";
    case HTSN_STREAM_FAILED: return "failed";
    case HTSN_STREAM_STANDBY: return "standby";
    default: return "configured";
    }
}

const char *htsn_stream_role_str(htsn_stream_role r) {
    switch (r) {
    case HTSN_STREAM_ROLE_TALKER: return "talker";
    default: return "listener";
    }
}
