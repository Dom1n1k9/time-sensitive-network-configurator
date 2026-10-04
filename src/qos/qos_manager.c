#include "qos/qos_manager.h"

#include "db/db_qos.h"

#include "qos/qos.h"

#include "common/str_util.h"

#include "common/log.h"
#include "mvc/model.h"

#include <stdlib.h>
#include <string.h>

struct htsn_qos_manager {
    htsn_db *db;
    htsn_event_bus *bus;
    htsn_model model;
};

htsn_qos_manager *htsn_qos_manager_create(htsn_db *db, htsn_event_bus *bus) {
    if (!db || !bus) return NULL;
    htsn_qos_manager *m = calloc(1, sizeof(htsn_qos_manager));
    if (!m) return NULL;
    m->db = db;
    m->bus = bus;
    htsn_model_init(&m->model, HTSN_QOS_MANAGER_MODEL, bus);
    return m;
}

void htsn_qos_manager_destroy(htsn_qos_manager *m) {
    free(m);
}

htsn_error htsn_qos_manager_configure(htsn_qos_manager *m, const char *device_id,
                                      const htsn_qos_config_model *cfg) {
    if (!m || !device_id || !cfg) return HTSN_ERR_INVALID_ARG;
    htsn_error e = htsn_qos_validate(cfg);
    if (e != HTSN_OK) return e;

    htsn_qos_config q;
    memset(&q, 0, sizeof(q));
    htsn_strlcpy(q.device_id, device_id, sizeof(q.device_id));
    q.priority = cfg->priority;
    q.traffic_class = (int)cfg->traffic_class;
    q.bandwidth_kbps = cfg->bandwidth_kbps;
    q.latency_ms = cfg->latency_ms;
    q.preemption = (int)cfg->preemption;
    htsn_db_qos_save(m->db, &q);

    htsn_model_notify(&m->model, "changed");
    return HTSN_OK;
}

htsn_error htsn_qos_manager_load_for_device(htsn_qos_manager *m, const char *device_id, htsn_qos_config *out) {
    if (!m || !device_id || !out) return HTSN_ERR_INVALID_ARG;
    return htsn_db_qos_load(m->db, device_id, out);
}

htsn_error htsn_qos_manager_remove(htsn_qos_manager *m, const char *device_id) {
    if (!m || !device_id) return HTSN_ERR_INVALID_ARG;
    htsn_db_qos_delete(m->db, device_id);
    htsn_model_notify(&m->model, "changed");
    return HTSN_OK;
}
