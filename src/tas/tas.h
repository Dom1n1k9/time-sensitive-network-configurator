#ifndef HTSN_TAS_H
#define HTSN_TAS_H

#include "common/common.h"
#include "tas/gcl.h"

typedef struct {
    char id[HTSN_MAX_STR];
    char name[HTSN_MAX_STR];
    int64_t cycle_time_ns;
    char deploy_target[HTSN_MAX_STR];
    htsn_gcl gcl;
} htsn_tas_schedule_model;

htsn_error htsn_tas_validate(const htsn_tas_schedule_model *s);
htsn_error htsn_tas_generate_helper(htsn_tas_schedule_model *s);

#endif
