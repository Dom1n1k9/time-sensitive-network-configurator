#include "tas/tas.h"

#include "tas/gcl.h"

#include <string.h>

htsn_error htsn_tas_validate(const htsn_tas_schedule_model *s) {
    if (!s) return HTSN_ERR_INVALID_ARG;
    if (s->cycle_time_ns <= 0) return HTSN_ERR_INVALID_ARG;
    if (!htsn_gcl_is_valid(&s->gcl)) return HTSN_ERR_INVALID_ARG;
    if (strlen(s->name) == 0) return HTSN_ERR_INVALID_ARG;
    return HTSN_OK;
}

htsn_error htsn_tas_generate_helper(htsn_tas_schedule_model *s) {
    if (!s || s->cycle_time_ns <= 0) return HTSN_ERR_INVALID_ARG;
    memset(&s->gcl, 0, sizeof(s->gcl));
    s->gcl.cycle_time_ns = s->cycle_time_ns;
    /* helper: single open window covering entire cycle */
    htsn_gcl_add_entry(&s->gcl, HTSN_GATE_OPEN, s->cycle_time_ns);
    return HTSN_OK;
}
