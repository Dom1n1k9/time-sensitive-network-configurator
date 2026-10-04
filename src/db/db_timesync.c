#include "db/db_timesync.h"

#include "timesync/timesync.h"

#include "common/str_util.h"

#include <string.h>

htsn_error htsn_db_timesync_save(htsn_db *db, const htsn_timesync_status *s) {
    if (!db || !db->handle || !s) return HTSN_ERR_INVALID_ARG;
    sqlite3_stmt *st = NULL;
    const char *sql =
        "INSERT INTO timesync_status (id,mode,grandmaster,offset_ns,quality,jitter_ns)"
        " VALUES ('global',?,?,?,?,?)"
        " ON CONFLICT(id) DO UPDATE SET mode=excluded.mode,"
        " grandmaster=excluded.grandmaster, offset_ns=excluded.offset_ns,"
        " quality=excluded.quality, jitter_ns=excluded.jitter_ns;";
    if (sqlite3_prepare_v2(db->handle, sql, -1, &st, NULL) != SQLITE_OK) return HTSN_ERR_DB;
    sqlite3_bind_text(st, 1, htsn_timesync_mode_str(s->mode), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, s->grandmaster, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 3, (sqlite3_int64)s->offset_ns);
    sqlite3_bind_int(st, 4, s->quality);
    sqlite3_bind_int64(st, 5, (sqlite3_int64)s->jitter_ns);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? HTSN_OK : HTSN_ERR_DB;
}

htsn_error htsn_db_timesync_load(htsn_db *db, htsn_timesync_status *out) {
    if (!db || !db->handle || !out) return HTSN_ERR_INVALID_ARG;
    sqlite3_stmt *st = NULL;
    const char *sql = "SELECT mode,grandmaster,offset_ns,quality,jitter_ns FROM timesync_status WHERE id='global';";
    if (sqlite3_prepare_v2(db->handle, sql, -1, &st, NULL) != SQLITE_OK) return HTSN_ERR_DB;
    int rc = sqlite3_step(st);
    if (rc != SQLITE_ROW) { sqlite3_finalize(st); return HTSN_ERR_NOT_FOUND; }
    memset(out, 0, sizeof(*out));
    out->mode = htsn_timesync_mode_parse((const char *)sqlite3_column_text(st, 0));
    htsn_strlcpy(out->grandmaster, (const char *)sqlite3_column_text(st, 1), sizeof(out->grandmaster));
    out->offset_ns = (int64_t)sqlite3_column_int64(st, 2);
    out->quality = sqlite3_column_int(st, 3);
    out->jitter_ns = (int64_t)sqlite3_column_int64(st, 4);
    sqlite3_finalize(st);
    return HTSN_OK;
}
