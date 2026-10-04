#include "tas/tas_manager.h"

#include "db/db_tas.h"

#include "tas/tas.h"

#include "common/str_util.h"

#include "common/log.h"
#include "mvc/model.h"

#include <stdlib.h>
#include <string.h>

struct htsn_tas_manager {
    htsn_db *db;
    htsn_event_bus *bus;
    htsn_model model;
};

htsn_tas_manager *htsn_tas_manager_create(htsn_db *db, htsn_event_bus *bus) {
    if (!db || !bus) return NULL;
    htsn_tas_manager *m = calloc(1, sizeof(htsn_tas_manager));
    if (!m) return NULL;
    m->db = db;
    m->bus = bus;
    htsn_model_init(&m->model, HTSN_TAS_MANAGER_MODEL, bus);
    return m;
}

void htsn_tas_manager_destroy(htsn_tas_manager *m) {
    free(m);
}

static void to_model(const htsn_tas_schedule *src, htsn_tas_schedule_model *dst) {
    memset(dst, 0, sizeof(*dst));
    htsn_strlcpy(dst->id, src->id, sizeof(dst->id));
    htsn_strlcpy(dst->name, src->name, sizeof(dst->name));
    dst->cycle_time_ns = src->cycle_time_ns;
    htsn_strlcpy(dst->deploy_target, src->deploy_target, sizeof(dst->deploy_target));
    memcpy(&dst->gcl.entries, src->entries, sizeof(htsn_gcl_entry) * src->entry_count);
    dst->gcl.entry_count = src->entry_count;
    dst->gcl.cycle_time_ns = src->cycle_time_ns;
}

static void to_db_schedule_copy(const htsn_tas_schedule_model *src, htsn_tas_schedule *dst) {
    memset(dst, 0, sizeof(*dst));
    htsn_strlcpy(dst->id, src->id, sizeof(dst->id));
    htsn_strlcpy(dst->name, src->name, sizeof(dst->name));
    dst->cycle_time_ns = src->cycle_time_ns;
    htsn_strlcpy(dst->deploy_target, src->deploy_target, sizeof(dst->deploy_target));
    dst->entry_count = src->gcl.entry_count;
    dst->entries = (htsn_gcl_entry *)src->gcl.entries;
}

htsn_error htsn_tas_manager_save(htsn_tas_manager *m, const htsn_tas_schedule_model *s) {
    if (!m || !s) return HTSN_ERR_INVALID_ARG;
    if (htsn_tas_validate(s) != HTSN_OK) return HTSN_ERR_INVALID_ARG;
    htsn_tas_schedule db_s;
    to_db_schedule_copy(s, &db_s);
    htsn_db_tas_save(m->db, &db_s);
    htsn_model_notify(&m->model, "schedule_changed");
    return HTSN_OK;
}

htsn_error htsn_tas_manager_load(htsn_tas_manager *m, const char *id, htsn_tas_schedule_model *out) {
    if (!m || !id || !out) return HTSN_ERR_INVALID_ARG;
    htsn_tas_schedule db_s;
    memset(&db_s, 0, sizeof(db_s));
    htsn_error e = htsn_db_tas_load(m->db, id, &db_s);
    if (e != HTSN_OK) return e;
    to_model(&db_s, out);
    free(db_s.entries);
    return HTSN_OK;
}

htsn_error htsn_tas_manager_delete(htsn_tas_manager *m, const char *id) {
    if (!m || !id) return HTSN_ERR_INVALID_ARG;
    htsn_db_tas_delete(m->db, id);
    htsn_model_notify(&m->model, "schedule_changed");
    return HTSN_OK;
}

htsn_error htsn_tas_manager_deploy(htsn_tas_manager *m, const char *id) {
    if (!m || !id) return HTSN_ERR_INVALID_ARG;
    htsn_tas_schedule_model s;
    memset(&s, 0, sizeof(s));
    htsn_error e = htsn_tas_manager_load(m, id, &s);
    if (e != HTSN_OK) return e;
    htsn_model_notify_data(&m->model, "deployed", &s);
    return HTSN_OK;
}
