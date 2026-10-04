#include "db/db_devices.h"

#include "device/device.h"

#include "common/str_util.h"

#include <string.h>

static void bind_device(sqlite3_stmt *st, const htsn_device *dev) {
    sqlite3_bind_text(st, 1, dev->id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, dev->name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, dev->ip, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, dev->mac, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 5, htsn_device_kind_str(dev->kind), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 6, dev->firmware, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 7, (int)dev->status);
    sqlite3_bind_int64(st, 8, (long long)dev->last_seen);
    sqlite3_bind_text(st, 9, dev->domain[0] ? dev->domain : "default", -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 10, (long long)dev->heartbeat_at);
}

htsn_error htsn_db_device_upsert(htsn_db *db, const htsn_device *dev) {
    if (!db || !dev || !db->handle) return HTSN_ERR_INVALID_ARG;
    const char *sql =
        "INSERT INTO devices (id,name,ip,mac,kind,firmware,status,last_seen,domain,heartbeat_at)"
        " VALUES (?,?,?,?,?,?,?,?,?,?)"
        " ON CONFLICT(id) DO UPDATE SET"
        " name=excluded.name, ip=excluded.ip, mac=excluded.mac,"
        " kind=excluded.kind, firmware=excluded.firmware, status=excluded.status,"
        " last_seen=excluded.last_seen, domain=excluded.domain,"
        " heartbeat_at=excluded.heartbeat_at;";
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db->handle, sql, -1, &st, NULL) != SQLITE_OK)
        return HTSN_ERR_DB;
    bind_device(st, dev);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return HTSN_ERR_DB;

    sqlite3_stmt *del = NULL;
    if (sqlite3_prepare_v2(db->handle,
        "DELETE FROM device_tsn_features WHERE device_id=?;", -1, &del, NULL) == SQLITE_OK) {
        sqlite3_bind_text(del, 1, dev->id, -1, SQLITE_TRANSIENT);
        sqlite3_step(del);
        sqlite3_finalize(del);
    }
    for (size_t i = 0; i < dev->tsn_features_count; i++) {
        sqlite3_stmt *f = NULL;
        if (sqlite3_prepare_v2(db->handle,
            "INSERT INTO device_tsn_features (device_id,feature) VALUES (?,?);",
            -1, &f, NULL) != SQLITE_OK) continue;
        sqlite3_bind_text(f, 1, dev->id, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(f, 2, dev->tsn_features[i], -1, SQLITE_TRANSIENT);
        sqlite3_step(f);
        sqlite3_finalize(f);
    }
    return HTSN_OK;
}

static htsn_device row_to_device(const htsn_db *db, sqlite3_stmt *st) {
    htsn_device d;
    memset(&d, 0, sizeof(d));
    htsn_strlcpy(d.id, (const char *)sqlite3_column_text(st, 0), sizeof(d.id));
    htsn_strlcpy(d.name, (const char *)sqlite3_column_text(st, 1), sizeof(d.name));
    htsn_strlcpy(d.ip, (const char *)sqlite3_column_text(st, 2), sizeof(d.ip));
    htsn_strlcpy(d.mac, (const char *)sqlite3_column_text(st, 3), sizeof(d.mac));
    d.kind = htsn_device_kind_parse((const char *)sqlite3_column_text(st, 4));
    htsn_strlcpy(d.firmware, (const char *)sqlite3_column_text(st, 5), sizeof(d.firmware));
    d.status = (htsn_device_status)sqlite3_column_int(st, 6);
    d.last_seen = (time_t)sqlite3_column_int64(st, 7);
    const char *dom = (const char *)sqlite3_column_text(st, 8);
    if (dom && dom[0]) htsn_strlcpy(d.domain, dom, sizeof(d.domain));
    d.heartbeat_at = (time_t)sqlite3_column_int64(st, 9);

    sqlite3_stmt *f = NULL;
    if (sqlite3_prepare_v2(db->handle,
        "SELECT feature FROM device_tsn_features WHERE device_id=?;", -1, &f, NULL) == SQLITE_OK) {
        sqlite3_bind_text(f, 1, d.id, -1, SQLITE_TRANSIENT);
        while (sqlite3_step(f) == SQLITE_ROW) {
            htsn_device_add_tsn_feature(&d, (const char *)sqlite3_column_text(f, 0));
        }
        sqlite3_finalize(f);
    }
    return d;
}

void htsn_db_device_for_each(htsn_db *db, htsn_db_device_cb cb, void *userdata) {
    if (!db || !db->handle || !cb) return;
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db->handle,
        "SELECT id,name,ip,mac,kind,firmware,status,last_seen,domain,heartbeat_at FROM devices;",
        -1, &st, NULL) != SQLITE_OK) return;
    while (sqlite3_step(st) == SQLITE_ROW) {
        htsn_device d = row_to_device(db, st);
        cb(&d, userdata);
    }
    sqlite3_finalize(st);
}

void htsn_db_device_for_each_in_domain(htsn_db *db, const char *domain,
                                       htsn_db_device_cb cb, void *userdata) {
    if (!db || !domain || !cb) return;
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db->handle,
        "SELECT id,name,ip,mac,kind,firmware,status,last_seen,domain,heartbeat_at "
        "FROM devices WHERE domain=?;", -1, &st, NULL) != SQLITE_OK) return;
    sqlite3_bind_text(st, 1, domain, -1, SQLITE_TRANSIENT);
    while (sqlite3_step(st) == SQLITE_ROW) {
        htsn_device d = row_to_device(db, st);
        cb(&d, userdata);
    }
    sqlite3_finalize(st);
}

htsn_error htsn_db_device_get(htsn_db *db, const char *id, htsn_device *out) {
    if (!db || !db->handle || !id || !out) return HTSN_ERR_INVALID_ARG;
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db->handle,
        "SELECT id,name,ip,mac,kind,firmware,status,last_seen,domain,heartbeat_at FROM devices WHERE id=?;",
        -1, &st, NULL) != SQLITE_OK) return HTSN_ERR_DB;
    sqlite3_bind_text(st, 1, id, -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(st);
    bool found = (rc == SQLITE_ROW);
    if (found) *out = row_to_device(db, st);
    sqlite3_finalize(st);
    return found ? HTSN_OK : HTSN_ERR_NOT_FOUND;
}

htsn_error htsn_db_device_delete(htsn_db *db, const char *id) {
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db->handle,
        "DELETE FROM devices WHERE id=?;", -1, &st, NULL) != SQLITE_OK)
        return HTSN_ERR_DB;
    sqlite3_bind_text(st, 1, id, -1, SQLITE_TRANSIENT);
    sqlite3_step(st);
    sqlite3_finalize(st);
    return HTSN_OK;
}

htsn_error htsn_db_device_set_status(htsn_db *db, const char *id, htsn_device_status status) {
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db->handle,
        "UPDATE devices SET status=? WHERE id=?;", -1, &st, NULL) != SQLITE_OK)
        return HTSN_ERR_DB;
    sqlite3_bind_int(st, 1, (int)status);
    sqlite3_bind_text(st, 2, id, -1, SQLITE_TRANSIENT);
    sqlite3_step(st);
    sqlite3_finalize(st);
    return HTSN_OK;
}

htsn_error htsn_db_device_touch(htsn_db *db, const char *id, time_t seen) {
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db->handle,
        "UPDATE devices SET last_seen=? WHERE id=?;", -1, &st, NULL) != SQLITE_OK)
        return HTSN_ERR_DB;
    sqlite3_bind_int64(st, 1, (long long)seen);
    sqlite3_bind_text(st, 2, id, -1, SQLITE_TRANSIENT);
    sqlite3_step(st);
    sqlite3_finalize(st);
    return HTSN_OK;
}
