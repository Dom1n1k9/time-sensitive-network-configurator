#ifndef HTSN_DB_TIMESYNC_H
#define HTSN_DB_TIMESYNC_H

#include "db/db.h"
#include "timesync/timesync.h"

htsn_error htsn_db_timesync_save(htsn_db *db, const htsn_timesync_status *s);
htsn_error htsn_db_timesync_load(htsn_db *db, htsn_timesync_status *out);

#endif
