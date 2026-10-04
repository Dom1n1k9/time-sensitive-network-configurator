#ifndef HTSN_DB_TAS_H
#define HTSN_DB_TAS_H

#include "db/db.h"
#include "tas/gcl.h"

typedef struct {
    char id[HTSN_MAX_STR];
    char name[HTSN_MAX_STR];
    int64_t cycle_time_ns;
    char deploy_target[HTSN_MAX_STR];
    unsigned char gate_state;
    htsn_gcl_entry *entries;
    size_t entry_count;
} htsn_tas_schedule;

htsn_error htsn_db_tas_save(htsn_db *db, const htsn_tas_schedule *s);
htsn_error htsn_db_tas_load(htsn_db *db, const char *id, htsn_tas_schedule *out);
void htsn_db_tas_for_each(htsn_db *db, int (*cb)(const htsn_tas_schedule *s, void *ud), void *ud);
htsn_error htsn_db_tas_delete(htsn_db *db, const char *id);

#endif
