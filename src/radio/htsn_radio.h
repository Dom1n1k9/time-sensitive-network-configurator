#ifndef HTSN_RADIO_H
#define HTSN_RADIO_H

#include "common/common.h"

/* Wi-Fi Multi-Media (WMM / 802.11e) access categories that map 802.1P
 * priorities to the radio queue that carries them. This is the wireless
 * analog of wired egress queueing.
 */
typedef enum {
    HTSN_WMM_AC_BK = 0,   /* background */
    HTSN_WMM_AC_BE,       /* best effort */
    HTSN_WMM_AC_VI,       /* video */
    HTSN_WMM_AC_VO,       /* voice */
    HTSN_WMM_AC_COUNT
} htsn_wmm_ac;

typedef struct {
    int priority;          /* 802.1P / PCP 0-7 */
    htsn_wmm_ac ac;        /* mapped WMM access category */
    bool admitted;         /* TSPEC admission-controlled stream */
    int64_t interval_ns;   /* requested service interval for stream */
    int64_t burst_ns;      /* nominal burst size */
} htsn_radio_flow;

htsn_wmm_ac htsn_radio_map_priority(int priority);
const char *htsn_wmm_ac_str(htsn_wmm_ac ac);
const char *htsn_wmm_ac_description(htsn_wmm_ac ac);

/* Check whether a wired TSN feature is realisable over 802.11 and
 * return a human-readable hint if not. */
const char *htsn_radio_feature_hint(const char *feature);
bool htsn_radio_feature_supported(const char *feature);

/* Build a radio flow description for a stream on a given radio. */
int htsn_radio_build_flow(int priority, int64_t interval_ns, int64_t burst_ns,
                          htsn_radio_flow *out, size_t cap);

#endif
