#include "db/db_trace_log.h"

#include <string.h>
#include <time.h>

htsn_error htsn_db_trace_log_insert(htsn_db *db, const htsn_trace_entry *e) {
    if (!db || !db->handle || !e) return HTSN_ERR_INVALID_ARG;
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db->handle,
        "INSERT INTO trace_log (ts,type,source,line) VALUES (?,?,?,?);",
        -1, &st, NULL) != SQLITE_OK) return HTSN_ERR_DB;
    sqlite3_bind_int64(st, 1, (sqlite3_int64)time(NULL));
    sqlite3_bind_int(st, 2, (int)e->type);
    sqlite3_bind_text(st, 3, e->source, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, e->line, -1, SQLITE_TRANSIENT);
    sqlite3_step(st);
    sqlite3_finalize(st);
    return HTSN_OK;
}

void htsn_db_trace_paged(htsn_db *db, int offset, int limit, htsn_db_trace_cb cb, void *userdata) {
    if (!db || !db->handle || !cb) return;
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db->handle,
        "SELECT ts,type,source,line FROM trace_log ORDER BY id DESC LIMIT ? OFFSET ?;",
        -1, &st, NULL) != SQLITE_OK) return;
    sqlite3_bind_int(st, 1, limit);
    sqlite3_bind_int(st, 2, offset);
    while (sqlite3_step(st) == SQLITE_ROW) {
        htsn_trace_entry e;
        memset(&e, 0, sizeof(e));
        time_t ts = (time_t)sqlite3_column_int64(st, 0);
        struct tm *tm = localtime(&ts);
        if (tm) strftime(e.timestamp, sizeof(e.timestamp), "%H:%M:%S", tm);
        e.type = (htsn_trace_type)sqlite3_column_int(st, 1);
        strncpy(e.source, (char *)sqlite3_column_text(st, 2) ? (char *)sqlite3_column_text(st, 2) : "-", sizeof(e.source));
        strncpy(e.line, (char *)sqlite3_column_text(st, 3) ? (char *)sqlite3_column_text(st, 3) : "", sizeof(e.line));
        cb(&e, userdata);
    }
    sqlite3_finalize(st);
}

int htsn_db_trace_count(htsn_db *db) {
    if (!db || !db->handle) return 0;
    sqlite3_stmt *st = NULL;
    int n = 0;
    if (sqlite3_prepare_v2(db->handle, "SELECT COUNT(*) FROM trace_log;", -1, &st, NULL) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW) n = sqlite3_column_int(st, 0);
        sqlite3_finalize(st);
    }
    return n;
}

htsn_error htsn_db_trace_prune(htsn_db *db, int keep) {
    if (!db || !db->handle) return HTSN_ERR_INVALID_ARG;
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db->handle,
        "DELETE FROM trace_log WHERE id NOT IN (SELECT id FROM trace_log ORDER BY id DESC LIMIT ?);",
        -1, &st, NULL) != SQLITE_OK) return HTSN_ERR_DB;
    sqlite3_bind_int(st, 1, keep);
    sqlite3_step(st);
    sqlite3_finalize(st);
    return HTSN_OK;
}
