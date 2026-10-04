#include "stream/tsn_manager.h"

#include "mqtt/mqtt_client.h"

#include "stream/stream.h"

#include "common/log.h"
#include "common/str_util.h"
#include "db/db_devices.h"
#include "db/db_tsn.h"
#include "mvc/model.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TSN_MODEL "tsn"

struct htsn_tsn_manager {
    htsn_db *db;
    htsn_event_bus *bus;
    htsn_mqtt_client *mqtt;
    htsn_model model;
    htsn_stream streams[HTSN_MAX_DEVICES];
    size_t count;
};

static int collect_stream(const htsn_stream *s, void *ud) {
    htsn_tsn_manager *m = (htsn_tsn_manager *)ud;
    if (m->count < HTSN_MAX_DEVICES) m->streams[m->count++] = *s;
    return 0;
}

static void notify(htsn_tsn_manager *m, const char *ev, const htsn_stream *s) {
    char buf[HTSN_MAX_STR];
    snprintf(buf, sizeof(buf), "%s", ev);
    htsn_model_notify_data(&m->model, buf, (void *)s);
}

htsn_tsn_manager *htsn_tsn_manager_create(const htsn_tsn_manager_config *cfg) {
    if (!cfg || !cfg->db) return NULL;
    htsn_tsn_manager *m = calloc(1, sizeof(htsn_tsn_manager));
    if (!m) return NULL;
    m->db = cfg->db;
    m->bus = cfg->bus;
    m->mqtt = cfg->mqtt;
    htsn_model_init(&m->model, TSN_MODEL, m->bus);
    htsn_db_tsn_for_each(m->db, collect_stream, m);
    return m;
}

void htsn_tsn_manager_destroy(htsn_tsn_manager *m) {
    free(m);
}

void htsn_tsn_manager_set_mqtt(htsn_tsn_manager *m, htsn_mqtt_client *mqtt) {
    if (m) m->mqtt = mqtt;
}

size_t htsn_tsn_manager_count(htsn_tsn_manager *m) {
    return m ? m->count : 0;
}

htsn_error htsn_tsn_manager_add(htsn_tsn_manager *m, const htsn_stream *s) {
    if (!m || !s) return HTSN_ERR_INVALID_ARG;
    htsn_error e = htsn_stream_validate(s);
    if (e != HTSN_OK) return e;

    bool exists = false;
    for (size_t i = 0; i < m->count; i++) {
        if (strcmp(m->streams[i].stream_id, s->stream_id) == 0) {
            m->streams[i] = *s;
            exists = true;
            break;
        }
    }
    if (!exists) {
        if (m->count >= HTSN_MAX_DEVICES) return HTSN_ERR_BUSY;
        m->streams[m->count++] = *s;
    }
    htsn_db_tsn_save(m->db, s);
    notify(m, exists ? "updated" : "created", s);
    return HTSN_OK;
}

htsn_error htsn_tsn_manager_remove(htsn_tsn_manager *m, const char *stream_id) {
    if (!m || !stream_id) return HTSN_ERR_INVALID_ARG;
    for (size_t i = 0; i < m->count; i++) {
        if (strcmp(m->streams[i].stream_id, stream_id) == 0) {
            htsn_stream removed = m->streams[i];
            memmove(&m->streams[i], &m->streams[i+1], (m->count - i - 1) * sizeof(htsn_stream));
            m->count--;
            htsn_db_tsn_delete(m->db, stream_id);
            notify(m, "removed", &removed);
            return HTSN_OK;
        }
    }
    return HTSN_ERR_NOT_FOUND;
}

htsn_error htsn_tsn_manager_load(htsn_tsn_manager *m, const char *stream_id, htsn_stream *out) {
    if (!m || !stream_id || !out) return HTSN_ERR_INVALID_ARG;
    for (size_t i = 0; i < m->count; i++) {
        if (strcmp(m->streams[i].stream_id, stream_id) == 0) { *out = m->streams[i]; return HTSN_OK; }
    }
    return HTSN_ERR_NOT_FOUND;
}

void htsn_tsn_manager_for_each(htsn_tsn_manager *m, int (*cb)(const htsn_stream *s, void *ud), void *ud) {
    if (!m || !cb) return;
    for (size_t i = 0; i < m->count; i++) cb(&m->streams[i], ud);
}

/* ---- 802.1Qcc deploy: push stream to talker + listeners via MQTT ----
   topic: tsn/cmd/<device>/stream  payload: JSON snapshot of the stream.
   Uses the established MQTT client. */
static htsn_error publish_stream(htsn_tsn_manager *m, const char *device,
                               const htsn_stream *s, htsn_stream_role role) {
    if (!m || !m->mqtt || !device) return HTSN_ERR_NOT_READY;
    char payload[1024];
    size_t off = (size_t)snprintf(payload, sizeof(payload),
        "{\"stream\":\"%s\",\"name\":\"%s\",\"role\":\"%s\",\"talker\":\"%s\","
        "\"vlan_id\":%d,\"latency_ns\":%lld,\"interval_ns\":%lld,"
        "\"priority\":%d,\"dfprio\":%d,\"status\":\"%s\"}",
        s->stream_id, s->name, htsn_stream_role_str(role), s->talker,
        s->vlan_id, (long long)s->max_latency_ns, (long long)s->max_interval_ns,
        s->priority, s->data_frame_prio, htsn_stream_status_str(s->status));
    char topic[HTSN_MAX_STR];
    snprintf(topic, sizeof(topic), "tsn/cmd/%s/stream", device);
    if (htsn_mqtt_client_publish(m->mqtt, topic, payload) == HTSN_OK) {
        htsn_log(HTSN_LOG_INFO, "stream %s -> %s (%s): %s",
                 s->stream_id, device, htsn_stream_role_str(role), payload);
        return HTSN_OK;
    }
    return HTSN_ERR_NET;
}

struct listener_walk_ctx {
    htsn_tsn_manager *m;
    const htsn_stream *s;
    const char *domain;     /* NULL = all domains */
};

static void all_listener_cb(const htsn_device *dev, void *userdata) {
    struct listener_walk_ctx *ctx = (struct listener_walk_ctx *)userdata;
    if (!dev->id[0] || strcmp(dev->id, ctx->s->talker) == 0) return;
    if (ctx->domain && strcmp(dev->domain, ctx->domain) != 0) return;
    publish_stream(ctx->m, dev->id, ctx->s, HTSN_STREAM_ROLE_LISTENER);
}

static htsn_error deploy_scoped(htsn_tsn_manager *m, const char *stream_id,
                                const char *domain);

htsn_error htsn_tsn_manager_deploy(htsn_tsn_manager *m, const char *stream_id) {
    return deploy_scoped(m, stream_id, NULL);
}

/* Internal scoped deploy: `domain` (NULL/empty = all domains) also limits
 * the all-listeners fan-out so a domain-scoped deploy never configures
 * endpoints outside its own physical cell. */
static htsn_error deploy_scoped(htsn_tsn_manager *m, const char *stream_id,
                                const char *domain) {
    if (!m || !stream_id) return HTSN_ERR_INVALID_ARG;
    htsn_stream s;
    htsn_error e = htsn_tsn_manager_load(m, stream_id, &s);
    if (e != HTSN_OK) return e;
    if (!m->mqtt) return HTSN_ERR_NOT_READY;

    if (publish_stream(m, s.talker, &s, HTSN_STREAM_ROLE_TALKER) != HTSN_OK)
        return HTSN_ERR_NET;

    if (s.listener_all) {
        /* all-listeners: push the stream to every device in the DB except
           the talker, so each agent registers itself as a listener. When a
           domain scope is active, only devices of that domain are reached. */
        struct listener_walk_ctx ctx = { m, &s, domain };
        if (domain && domain[0])
            (void)htsn_db_device_for_each_in_domain(m->db, domain, all_listener_cb, &ctx);
        else
            (void)htsn_db_device_for_each(m->db, all_listener_cb, &ctx);
    } else {
        for (size_t i = 0; i < s.listener_count; i++) {
            if (!s.listeners[i][0]) continue;
            publish_stream(m, s.listeners[i], &s, HTSN_STREAM_ROLE_LISTENER);
        }
    }
    htsn_db_tsn_set_status(m->db, stream_id, HTSN_STREAM_READY);
    s.status = HTSN_STREAM_READY;
    notify(m, "deployed", &s);
    return HTSN_OK;
}

static int collect_order(const htsn_stream *s, void *ud) {
    (void)s;
    (void)ud;
    return 0;
}

/* True when the stream belongs to `domain` (via its talker device), or when
 * the scope argument is empty (global deploy). A device missing from the DB
 * is treated as "belongs" so a domain scope never silently loses a stream
 * that still needs its endpoints pushed. */
static int stream_in_domain(htsn_tsn_manager *m, const char *stream_id,
                            const char *domain) {
    if (!domain || !domain[0]) return 1;
    htsn_stream s;
    if (htsn_tsn_manager_load(m, stream_id, &s) != HTSN_OK) return 1;
    htsn_device talker;
    if (htsn_db_device_get(m->db, s.talker, &talker) != HTSN_OK) return 1;
    return !talker.domain[0] || strcmp(talker.domain, domain) == 0;
}

struct deploy_ctx {
    htsn_tsn_manager *m;
    const char *domain;     /* NULL/empty = all domains */
};

static int deploy_cb(const htsn_stream *s, void *ud) {
    struct deploy_ctx *dctx = (struct deploy_ctx *)ud;
    if (!stream_in_domain(dctx->m, s->stream_id, dctx->domain)) return 0;
    return deploy_scoped(dctx->m, s->stream_id, dctx->domain) == HTSN_OK ? 0 : 0;
}

htsn_error htsn_tsn_manager_deploy_all(htsn_tsn_manager *m, const char *domain) {
    if (!m) return HTSN_ERR_INVALID_ARG;
    (void)collect_order;
    struct deploy_ctx dctx = { m, domain };
    htsn_tsn_manager_for_each(m, deploy_cb, &dctx);
    return HTSN_OK;
}
