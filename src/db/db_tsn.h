#ifndef HTSN_DB_TSN_H
#define HTSN_DB_TSN_H

#include "db/db.h"
#include "stream/stream.h"

/* load / save a single TSN stream (and its listener members) */
htsn_error htsn_db_tsn_save(htsn_db *db, const htsn_stream *s);
htsn_error htsn_db_tsn_load(htsn_db *db, const char *stream_id, htsn_stream *out);
htsn_error htsn_db_tsn_delete(htsn_db *db, const char *stream_id);
htsn_error htsn_db_tsn_set_status(htsn_db *db, const char *stream_id, htsn_stream_status st);
void htsn_db_tsn_for_each(htsn_db *db, int (*cb)(const htsn_stream *s, void *ud), void *ud);

#endif
