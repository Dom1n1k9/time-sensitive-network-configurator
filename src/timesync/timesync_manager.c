#include "timesync/timesync_manager.h"

#include "db/db_timesync.h"

#include "timesync/timesync.h"

#include "common/str_util.h"

#include "common/log.h"
#include "db/db_timesync_report.h"
#include "mvc/model.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

struct htsn_timesync_manager {
    htsn_db *db;
    htsn_event_bus *bus;
    htsn_model model;
    htsn_timesync_status status;
};

htsn_timesync_manager *htsn_timesync_manager_create(htsn_db *db, htsn_event_bus *bus) {
    if (!db || !bus) return NULL;
    htsn_timesync_manager *m = calloc(1, sizeof(htsn_timesync_manager));
    if (!m) return NULL;
    m->db = db;
    m->bus = bus;
    htsn_model_init(&m->model, HTSN_TIMESYNC_MANAGER_MODEL, bus);
    htsn_timesync_manager_load(m);
    return m;
}

void htsn_timesync_manager_destroy(htsn_timesync_manager *m) {
    free(m);
}

htsn_error htsn_timesync_manager_load(htsn_timesync_manager *m) {
    if (!m) return HTSN_ERR_INVALID_ARG;
    if (htsn_db_timesync_load(m->db, &m->status) != HTSN_OK) {
        memset(&m->status, 0, sizeof(m->status));
        m->status.mode = HTSN_TIMESYNC_DISABLED;
        htsn_strlcpy(m->status.protocol, "gptp", sizeof(m->status.protocol));
    }
    return HTSN_OK;
}

htsn_error htsn_timesync_manager_set_mode(htsn_timesync_manager *m, htsn_timesync_mode mode) {
    if (!m) return HTSN_ERR_INVALID_ARG;
    htsn_timesync_validate_mode(mode);
    m->status.mode = mode;
    htsn_db_timesync_save(m->db, &m->status);
    htsn_model_notify(&m->model, "changed");
    return HTSN_OK;
}

htsn_error htsn_timesync_manager_set_grandmaster(htsn_timesync_manager *m, const char *gm_id) {
    if (!m || !gm_id) return HTSN_ERR_INVALID_ARG;
    htsn_strlcpy(m->status.grandmaster, gm_id, sizeof(m->status.grandmaster));
    htsn_db_timesync_save(m->db, &m->status);
    htsn_model_notify(&m->model, "changed");
    return HTSN_OK;
}

const htsn_timesync_status *htsn_timesync_manager_status(htsn_timesync_manager *m) {
    return m ? &m->status : NULL;
}

htsn_error htsn_timesync_manager_record_report(htsn_timesync_manager *m, const char *device_id,
                                           int64_t offset_ns, int64_t jitter_ns,
                                           int packet_count, int packet_loss) {
    if (!m || !device_id) return HTSN_ERR_INVALID_ARG;
    htsn_timesync_report r;
    memset(&r, 0, sizeof(r));
    htsn_strlcpy(r.device_id, device_id, sizeof(r.device_id));
    r.offset_ns = offset_ns;
    r.jitter_ns = jitter_ns;
    r.packet_count = packet_count;
    r.packet_loss = packet_loss;
    htsn_strlcpy(r.status, packet_loss > 0 ? "degraded" : "in_sync", sizeof(r.status));
    htsn_db_timesync_report_insert(m->db, &r);
    htsn_db_timesync_report_prune(m->db, 1000);
    /* update the active per-device summary shown to the user */
    htsn_strlcpy(m->status.report_device, device_id, sizeof(m->status.report_device));
    m->status.report_offset_ns = offset_ns;
    m->status.report_jitter_ns = jitter_ns;
    m->status.report_packet_count = packet_count;
    m->status.report_packet_loss = packet_loss;
    htsn_model_notify_data(&m->model, "report", &m->status);
    return HTSN_OK;
}
