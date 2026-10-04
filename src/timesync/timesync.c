#include "timesync/timesync.h"

#include <string.h>

const char *htsn_timesync_mode_str(htsn_timesync_mode m) {
    switch (m) {
    case HTSN_TIMESYNC_DISABLED: return "disabled";
    case HTSN_TIMESYNC_LOCAL_GRANDMASTER: return "local_grandmaster";
    case HTSN_TIMESYNC_EXTERNAL_GRANDMASTER: return "external_grandmaster";
    case HTSN_TIMESYNC_AUTO: return "auto";
    default: return "unknown";
    }
}

htsn_timesync_mode htsn_timesync_mode_parse(const char *s) {
    if (!s) return HTSN_TIMESYNC_DISABLED;
    if (strcmp(s, "local_grandmaster") == 0) return HTSN_TIMESYNC_LOCAL_GRANDMASTER;
    if (strcmp(s, "external_grandmaster") == 0) return HTSN_TIMESYNC_EXTERNAL_GRANDMASTER;
    if (strcmp(s, "auto") == 0) return HTSN_TIMESYNC_AUTO;
    return HTSN_TIMESYNC_DISABLED;
}

htsn_error htsn_timesync_validate_mode(htsn_timesync_mode m) {
    return HTSN_OK;
}
