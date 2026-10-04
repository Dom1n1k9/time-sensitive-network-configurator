#ifndef HTSN_DB_TIMESYNC_REPORT_H
#define HTSN_DB_TIMESYNC_REPORT_H

#include "db/db.h"
#include "timesync/timesync.h"

#include <time.h>

typedef struct {
    char device_id[HTSN_MAX_STR];
    time_t ts;
    int64_t offset_ns;
    int64_t jitter_ns;
    int packet_count;
    int packet_loss;
    char status[32];
} htsn_timesync_report;

htsn_error htsn_db_timesync_report_insert(htsn_db *db, const htsn_timesync_report *r);
typedef void (*htsn_db_report_cb)(const htsn_timesync_report *r, void *userdata);
void htsn_db_timesync_report_for_each(htsn_db *db, const char *device_id,
                                     int limit, htsn_db_report_cb cb, void *userdata);
htsn_error htsn_db_timesync_report_prune(htsn_db *db, int keep);

#endif
