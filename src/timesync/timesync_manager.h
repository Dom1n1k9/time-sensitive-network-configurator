#ifndef HTSN_TIMESYNC_MANAGER_H
#define HTSN_TIMESYNC_MANAGER_H

#include "common/common.h"
#include "db/db.h"
#include "db/db_timesync.h"
#include "mvc/event_bus.h"
#include "timesync/timesync.h"

#define HTSN_TIMESYNC_MANAGER_MODEL "timesync"

typedef struct htsn_timesync_manager htsn_timesync_manager;

htsn_timesync_manager *htsn_timesync_manager_create(htsn_db *db, htsn_event_bus *bus);
void htsn_timesync_manager_destroy(htsn_timesync_manager *m);

htsn_error htsn_timesync_manager_set_mode(htsn_timesync_manager *m, htsn_timesync_mode mode);
htsn_error htsn_timesync_manager_set_grandmaster(htsn_timesync_manager *m, const char *gm_id);
const htsn_timesync_status *htsn_timesync_manager_status(htsn_timesync_manager *m);
htsn_error htsn_timesync_manager_load(htsn_timesync_manager *m);
htsn_error htsn_timesync_manager_record_report(htsn_timesync_manager *m, const char *device_id,
                                             int64_t offset_ns, int64_t jitter_ns,
                                             int packet_count, int packet_loss);

#endif
