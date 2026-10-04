#include "db/db_domains.h"

#include "common/str_util.h"

#include <string.h>

htsn_error htsn_db_domain_save(htsn_db *db, const htsn_domain *d) {
    if (!db || !db->handle || !d) return HTSN_ERR_INVALID_ARG;
    sqlite3_stmt *st = NULL;
    const char *sql =
        "INSERT INTO domains (id,name,description) VALUES (?,?,?)"
        " ON CONFLICT(id) DO UPDATE SET name=excluded.name, description=excluded.description;";
    if (sqlite3_prepare_v2(db->handle, sql, -1, &st, NULL) != SQLITE_OK) return HTSN_ERR_DB;
    sqlite3_bind_text(st, 1, d->id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, d->name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, d->description, -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? HTSN_OK : HTSN_ERR_DB;
}

htsn_error htsn_db_domain_delete(htsn_db *db, const char *id) {
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db->handle, "DELETE FROM domains WHERE id=?;", -1, &st, NULL) != SQLITE_OK)
        return HTSN_ERR_DB;
    sqlite3_bind_text(st, 1, id, -1, SQLITE_TRANSIENT);
    sqlite3_step(st);
    sqlite3_finalize(st);
    return HTSN_OK;
}

void htsn_db_domain_for_each(htsn_db *db, htsn_db_domain_cb cb, void *userdata) {
    if (!db || !db->handle || !cb) return;
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db->handle, "SELECT id,name,description FROM domains;", -1, &st, NULL) != SQLITE_OK)
        return;
    while (sqlite3_step(st) == SQLITE_ROW) {
        htsn_domain d;
        memset(&d, 0, sizeof(d));
        htsn_strlcpy(d.id, (const char *)sqlite3_column_text(st, 0), sizeof(d.id));
        htsn_strlcpy(d.name, (const char *)sqlite3_column_text(st, 1), sizeof(d.name));
        htsn_strlcpy(d.description, (const char *)sqlite3_column_text(st, 2), sizeof(d.description));
        cb(&d, userdata);
    }
    sqlite3_finalize(st);
}
