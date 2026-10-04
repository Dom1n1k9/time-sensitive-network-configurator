#include "domain/domain_manager.h"

#include "db/db_domains.h"

#include "common/log.h"
#include "common/str_util.h"
#include "db/db_devices.h"
#include "mvc/model.h"

#include <stdlib.h>
#include <string.h>

struct htsn_domain_manager {
    htsn_db *db;
    htsn_event_bus *bus;
    htsn_model model;
};

htsn_domain_manager *htsn_domain_manager_create(htsn_db *db, htsn_event_bus *bus) {
    if (!db || !bus) return NULL;
    htsn_domain_manager *m = calloc(1, sizeof(htsn_domain_manager));
    if (!m) return NULL;
    m->db = db;
    m->bus = bus;
    htsn_model_init(&m->model, HTSN_DOMAIN_MANAGER_MODEL, bus);
    htsn_domain def;
    memset(&def, 0, sizeof(def));
    htsn_strlcpy(def.id, "default", sizeof(def.id));
    htsn_strlcpy(def.name, "Default", sizeof(def.name));
    htsn_strlcpy(def.description, "Global default domain", sizeof(def.description));
    htsn_db_domain_save(db, &def);
    return m;
}

void htsn_domain_manager_destroy(htsn_domain_manager *m) {
    free(m);
}

htsn_error htsn_domain_manager_save(htsn_domain_manager *m, const htsn_domain *d) {
    if (!m || !d) return HTSN_ERR_INVALID_ARG;
    htsn_db_domain_save(m->db, d);
    htsn_model_notify(&m->model, "changed");
    return HTSN_OK;
}

htsn_error htsn_domain_manager_delete(htsn_domain_manager *m, const char *id) {
    if (!m || !id) return HTSN_ERR_INVALID_ARG;
    if (strcmp(id, "default") == 0) return HTSN_ERR_INVALID_ARG;
    htsn_db_domain_delete(m->db, id);
    /* re-home devices that referenced this domain back to default */
    htsn_db *db = m->db;
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db->handle, "UPDATE devices SET domain='default' WHERE domain=?;",
                           -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, id, -1, SQLITE_TRANSIENT);
        sqlite3_step(st);
        sqlite3_finalize(st);
    }
    htsn_model_notify(&m->model, "changed");
    return HTSN_OK;
}

void htsn_domain_manager_for_each(htsn_domain_manager *m, htsn_db_domain_cb cb, void *ud) {
    if (m) htsn_db_domain_for_each(m->db, cb, ud);
}

htsn_error htsn_domain_manager_assign_device(htsn_domain_manager *m, const char *device_id,
                                            const char *domain_id) {
    if (!m || !device_id || !domain_id) return HTSN_ERR_INVALID_ARG;
    htsn_db *db = m->db;
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db->handle, "UPDATE devices SET domain=? WHERE id=?;",
                           -1, &st, NULL) != SQLITE_OK) return HTSN_ERR_DB;
    sqlite3_bind_text(st, 1, domain_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, device_id, -1, SQLITE_TRANSIENT);
    sqlite3_step(st);
    sqlite3_finalize(st);
    htsn_model_notify(&m->model, "changed");
    return HTSN_OK;
}
