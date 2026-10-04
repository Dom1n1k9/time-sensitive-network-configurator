#ifndef HTSN_GCL_H
#define HTSN_GCL_H

#include "common/common.h"

#include <stdint.h>

#define HTSN_GATE_OPEN 0x01
#define HTSN_GATE_MAX_QUEUES 8
#define HTSN_GCL_MAX_ENTRIES 128

typedef struct {
    unsigned char gate_state;
    int64_t duration_ns;
} htsn_gcl_entry;

typedef struct {
    htsn_gcl_entry entries[HTSN_GCL_MAX_ENTRIES];
    size_t entry_count;
    int64_t cycle_time_ns;
} htsn_gcl;

htsn_error htsn_gcl_init(htsn_gcl *gcl, int64_t cycle_time_ns);
htsn_error htsn_gcl_add_entry(htsn_gcl *gcl, unsigned char gate_state, int64_t duration_ns);
void htsn_gcl_reset(htsn_gcl *gcl);
int64_t htsn_gcl_total_duration_ns(const htsn_gcl *gcl);
bool htsn_gcl_is_valid(const htsn_gcl *gcl);
void htsn_gcl_render_ascii(const htsn_gcl *gcl, char *out, size_t out_size);

#endif
