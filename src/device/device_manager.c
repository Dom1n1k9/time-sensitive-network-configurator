#include "device/device_manager.h"

#include "db/db_devices.h"

#include "plugin/plugin_manager.h"

#include "common/str_util.h"

#include "common/log.h"
#include "mvc/model.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DEVICE_MODEL_NAME "device"
#define MAX_DB_DEVICES 512

struct htsn_device_manager {
    htsn_db *db;
    htsn_event_bus *bus;
    htsn_plugin_manager *plugins;
    htsn_model model;
    htsn_device devices[MAX_DB_DEVICES];
    size_t count;
};

static void collect_devices(const htsn_device *dev, void *ud) {
    htsn_device_manager *m = (htsn_device_manager *)ud;
    if (m->count < MAX_DB_DEVICES) {
        m->devices[m->count++] = *dev;
    }
}

static void publish_device_event(htsn_device_manager *m, const char *event,
                                 const htsn_device *dev) {
    if (!m) return;
    char buf[512];
    snprintf(buf, sizeof(buf), "%s", event);
    htsn_model_notify_data(&m->model, buf, (void *)dev);
}

htsn_device_manager *htsn_device_manager_create(htsn_db *db, htsn_event_bus *bus,
                                                 htsn_plugin_manager *plugins) {
    if (!db || !bus) return NULL;
    htsn_device_manager *m = calloc(1, sizeof(htsn_device_manager));
    if (!m) return NULL;
    m->db = db;
    m->bus = bus;
    m->plugins = plugins;
    htsn_model_init(&m->model, DEVICE_MODEL_NAME, bus);
    htsn_device_manager_restore(m);
    return m;
}

void htsn_device_manager_destroy(htsn_device_manager *m) {
    if (!m) return;
    for (size_t i = 0; i < m->count; i++) {
        htsn_db_device_upsert(m->db, &m->devices[i]);
    }
    free(m);
}

const htsn_device *htsn_device_manager_get(htsn_device_manager *m, const char *id) {
    if (!m || !id) return NULL;
    for (size_t i = 0; i < m->count; i++) {
        if (strcmp(m->devices[i].id, id) == 0) return &m->devices[i];
    }
    return NULL;
}

size_t htsn_device_manager_count(htsn_device_manager *m) {
    return m ? m->count : 0;
}

void htsn_device_manager_for_each(htsn_device_manager *m,
                                  void (*cb)(const htsn_device *dev, void *ud), void *ud) {
    if (!m || !cb) return;
    for (size_t i = 0; i < m->count; i++) cb(&m->devices[i], ud);
}

htsn_error htsn_device_manager_upsert(htsn_device_manager *m, const htsn_device *dev) {
    if (!m || !dev) return HTSN_ERR_INVALID_ARG;
    htsn_device copy = *dev;
    copy.last_seen = dev->last_seen ? dev->last_seen : time(NULL);

    bool exists = false;
    for (size_t i = 0; i < m->count; i++) {
        if (strcmp(m->devices[i].id, copy.id) == 0) {
            m->devices[i] = copy;
            exists = true;
            break;
        }
    }
    if (!exists) {
        if (m->count >= MAX_DB_DEVICES) return HTSN_ERR_BUSY;
        m->devices[m->count++] = copy;
    }

    htsn_db_device_upsert(m->db, &copy);
    publish_device_event(m, exists ? "updated" : "discovered", &copy);
    return HTSN_OK;
}

htsn_error htsn_device_manager_remove(htsn_device_manager *m, const char *id) {
    if (!m || !id) return HTSN_ERR_INVALID_ARG;
    for (size_t i = 0; i < m->count; i++) {
        if (strcmp(m->devices[i].id, id) == 0) {
            htsn_device removed = m->devices[i];
            memmove(&m->devices[i], &m->devices[i+1],
                    (m->count - i - 1) * sizeof(htsn_device));
            m->count--;
            htsn_db_device_delete(m->db, id);
            publish_device_event(m, "removed", &removed);
            return HTSN_OK;
        }
    }
    return HTSN_ERR_NOT_FOUND;
}

void htsn_device_manager_restore(htsn_device_manager *m) {
    for (size_t i = 0; i < MAX_DB_DEVICES; i++) m->devices[i].id[0] = '\0';
    m->count = 0;
    htsn_db_device_for_each(m->db, collect_devices, m);
    htsn_log(HTSN_LOG_INFO, "restored %zu devices from database", m->count);
}

void htsn_device_manager_mark_offline_after(htsn_device_manager *m, time_t threshold) {
    if (!m) return;
    for (size_t i = 0; i < m->count; i++) {
        htsn_device *d = &m->devices[i];
        if (d->last_seen < threshold && d->status != HTSN_DEVICE_ERROR) {
            d->status = HTSN_DEVICE_OFFLINE;
            htsn_db_device_set_status(m->db, d->id, d->status);
            publish_device_event(m, "status", d);
        }
    }
}

static void collect_plugin_devices(htsn_plugin *p, void *ud) {
    htsn_device_manager *m = (htsn_device_manager *)ud;
    htsn_device out[HTSN_MAX_DEVICES];
    memset(out, 0, sizeof(out));
    int count = 0;
    if (p->discover && p->discover(p, out, HTSN_MAX_DEVICES, &count) == HTSN_OK) {
        for (int i = 0; i < count; i++) {
            htsn_device_manager_upsert(m, &out[i]);
        }
    }
}

htsn_error htsn_device_manager_discover_once(htsn_device_manager *m) {
    if (!m || !m->plugins) return HTSN_ERR_INVALID_ARG;
    for (size_t i = 0; i < htsn_plugin_manager_count(m->plugins); i++) {
        collect_plugin_devices(htsn_plugin_manager_get(m->plugins, i), m);
    }
    return HTSN_OK;
}

htsn_error htsn_device_manager_record_heartbeat(htsn_device_manager *m, const char *id) {
    if (!m || !id) return HTSN_ERR_INVALID_ARG;
    for (size_t i = 0; i < m->count; i++) {
        if (strcmp(m->devices[i].id, id) == 0) {
            htsn_device *d = &m->devices[i];
            time_t now = time(NULL);
            d->heartbeat_at = now;
            d->last_seen = now;
            bool was_online = (d->status == HTSN_DEVICE_ONLINE);
            d->status = HTSN_DEVICE_ONLINE;
            sqlite3_stmt *st = NULL;
            if (sqlite3_prepare_v2(m->db->handle,
                "UPDATE devices SET heartbeat_at=?, last_seen=?, status=? WHERE id=?;",
                -1, &st, NULL) == SQLITE_OK) {
                sqlite3_bind_int64(st, 1, (sqlite3_int64)now);
                sqlite3_bind_int64(st, 2, (sqlite3_int64)now);
                sqlite3_bind_int(st, 3, (int)HTSN_DEVICE_ONLINE);
                sqlite3_bind_text(st, 4, id, -1, SQLITE_TRANSIENT);
                sqlite3_step(st);
                sqlite3_finalize(st);
            }
            if (!was_online) publish_device_event(m, "status", &m->devices[i]);
            return HTSN_OK;
        }
    }
    return HTSN_ERR_NOT_FOUND;
}

htsn_error htsn_device_manager_set_domain(htsn_device_manager *m, const char *id,
                                         const char *domain) {
    if (!m || !id || !domain) return HTSN_ERR_INVALID_ARG;
    for (size_t i = 0; i < m->count; i++) {
        if (strcmp(m->devices[i].id, id) == 0) {
            htsn_strlcpy(m->devices[i].domain, domain, sizeof(m->devices[i].domain));
            htsn_db_device_upsert(m->db, &m->devices[i]);
            return HTSN_OK;
        }
    }
    return HTSN_ERR_NOT_FOUND;
}

htsn_error htsn_device_manager_set_health(htsn_device_manager *m, const char *id,
                                         htsn_device_status status) {
    if (!m || !id) return HTSN_ERR_INVALID_ARG;
    for (size_t i = 0; i < m->count; i++) {
        if (strcmp(m->devices[i].id, id) == 0) {
            m->devices[i].status = status;
            htsn_db_device_set_status(m->db, id, status);
            publish_device_event(m, "status", &m->devices[i]);
            return HTSN_OK;
        }
    }
    return HTSN_ERR_NOT_FOUND;
}
