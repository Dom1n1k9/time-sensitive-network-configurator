#ifndef HTSN_DB_TRACE_LOG_H
#define HTSN_DB_TRACE_LOG_H

#include "db/db.h"
#include "trace/trace.h"

htsn_error htsn_db_trace_log_insert(htsn_db *db, const htsn_trace_entry *e);
typedef void (*htsn_db_trace_cb)(const htsn_trace_entry *e, void *userdata);
void htsn_db_trace_paged(htsn_db *db, int offset, int limit, htsn_db_trace_cb cb, void *userdata);
int htsn_db_trace_count(htsn_db *db);
htsn_error htsn_db_trace_prune(htsn_db *db, int keep);

#endif
