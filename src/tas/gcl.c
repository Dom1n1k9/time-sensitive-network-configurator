#include "tas/gcl.h"

#include <string.h>
#include <stdio.h>

htsn_error htsn_gcl_init(htsn_gcl *gcl, int64_t cycle_time_ns) {
    if (!gcl || cycle_time_ns <= 0) return HTSN_ERR_INVALID_ARG;
    memset(gcl, 0, sizeof(*gcl));
    gcl->cycle_time_ns = cycle_time_ns;
    return HTSN_OK;
}

htsn_error htsn_gcl_add_entry(htsn_gcl *gcl, unsigned char gate_state, int64_t duration_ns) {
    if (!gcl || duration_ns < 0 || gcl->entry_count >= HTSN_GCL_MAX_ENTRIES)
        return HTSN_ERR_INVALID_ARG;
    gcl->entries[gcl->entry_count].gate_state = gate_state;
    gcl->entries[gcl->entry_count].duration_ns = duration_ns;
    gcl->entry_count++;
    return HTSN_OK;
}

void htsn_gcl_reset(htsn_gcl *gcl) {
    if (gcl) gcl->entry_count = 0;
}

int64_t htsn_gcl_total_duration_ns(const htsn_gcl *gcl) {
    if (!gcl) return 0;
    int64_t total = 0;
    for (size_t i = 0; i < gcl->entry_count; i++) total += gcl->entries[i].duration_ns;
    return total;
}

bool htsn_gcl_is_valid(const htsn_gcl *gcl) {
    if (!gcl || gcl->entry_count == 0) return false;
    int64_t total = htsn_gcl_total_duration_ns(gcl);
    /* GCL must cover a full cycle (802.1Qbv) */
    return total == gcl->cycle_time_ns;
}

static char row_state_char(unsigned char s) {
    unsigned char o = s & HTSN_GATE_OPEN;
    return o ? '#' : '.';
}

void htsn_gcl_render_ascii(const htsn_gcl *gcl, char *out, size_t out_size) {
    if (!gcl || !out || out_size == 0) return;
    char *p = out;
    size_t remaining = out_size;
    for (size_t i = 0; i < gcl->entry_count && remaining > 1; i++) {
        int len = snprintf(p, remaining, "[%c %lldns]",
                           row_state_char(gcl->entries[i].gate_state),
                           (long long)gcl->entries[i].duration_ns);
        if (len < 0) break;
        p += len;
        remaining -= (size_t)len;
    }
    if (remaining > 0) *p = '\0';
}
