#include "trace/trace.h"

#include "common/str_util.h"

#include "db/db_trace_log.h"
#include "mvc/model.h"
#include "common/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TRACE_MODEL "trace"

struct htsn_trace {
    htsn_event_bus *bus;
    htsn_model model;
    htsn_trace_entry entries[HTSN_TRACE_MAX];
    int count;
    int head;
    htsn_db *db;
    size_t keep;
};

static void stamp(htsn_trace_entry *e) {
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    if (!tm) { e->timestamp[0] = '\0'; return; }
    strftime(e->timestamp, sizeof(e->timestamp), "%H:%M:%S", tm);
}

htsn_trace *htsn_trace_create(htsn_event_bus *bus) {
    htsn_trace *t = htsn_trace_create_persistent(bus, NULL, 0);
    return t;
}

htsn_trace *htsn_trace_create_persistent(htsn_event_bus *bus, htsn_db *db, size_t keep) {
    htsn_trace *t = calloc(1, sizeof(htsn_trace));
    if (!t) return NULL;
    t->bus = bus;
    t->db = db;
    t->keep = keep;
    htsn_model_init(&t->model, TRACE_MODEL, bus);
    return t;
}

void htsn_trace_destroy(htsn_trace *t) {
    free(t);
}

static void push(htsn_trace *t, htsn_trace_type type, const char *source, const char *line) {
    htsn_trace_entry *e = &t->entries[t->head];
    memset(e, 0, sizeof(*e));
    memset(e->timestamp, 0, sizeof(e->timestamp));
    stamp(e);
    e->type = type;
    htsn_strlcpy(e->source, source ? source : "-", sizeof(e->source));
    htsn_strlcpy(e->line, line ? line : "", sizeof(e->line));
    if (t->db) {
        htsn_db_trace_log_insert(t->db, e);
        if (t->keep > 0) htsn_db_trace_prune(t->db, (int)t->keep);
    }
    t->head = (t->head + 1) % HTSN_TRACE_MAX;
    if (t->count < HTSN_TRACE_MAX) t->count++;
    htsn_model_notify_data(&t->model, "entry", e);
}

htsn_error htsn_trace_add_comm(htsn_trace *t, const char *source, const char *msg) {
    if (!t || !msg) return HTSN_ERR_INVALID_ARG;
    push(t, HTSN_TRACE_COMM, source, msg);
    return HTSN_OK;
}

htsn_error htsn_trace_add_frame(htsn_trace *t, const char *source, const unsigned char *bytes, size_t len) {
    if (!t || (!bytes && len > 0)) return HTSN_ERR_INVALID_ARG;
    char line[HTSN_TRACE_LINE];
    size_t off = 0;
    for (size_t i = 0; i < len && off + 4 < sizeof(line); i++) {
        off += (size_t)snprintf(line + off, sizeof(line) - off, "%02X ", bytes[i]);
    }
    if (off == 0) { line[0] = '\0'; }
    else { line[off] = '\0'; }
    push(t, HTSN_TRACE_FRAME, source, line);
    return HTSN_OK;
}

htsn_error htsn_trace_add_config(htsn_trace *t, const char *source, const char *what) {
    if (!t || !what) return HTSN_ERR_INVALID_ARG;
    push(t, HTSN_TRACE_CONFIG, source, what);
    return HTSN_OK;
}

htsn_error htsn_trace_add_multicast(htsn_trace *t, const char *source, const char *group, const char *msg) {
    if (!t || !msg) return HTSN_ERR_INVALID_ARG;
    char line[HTSN_TRACE_LINE];
    snprintf(line, sizeof(line), "FX mcast -> %s: %s", group ? group : "?", msg);
    push(t, HTSN_TRACE_MULTICAST, source, line);
    return HTSN_OK;
}

htsn_trace_entry *htsn_trace_entry_at(htsn_trace *t, int index) {
    if (!t || index < 0 || index >= t->count) return NULL;
    int idx = (t->head - 1 - index + HTSN_TRACE_MAX) % HTSN_TRACE_MAX;
    return &t->entries[idx];
}

int htsn_trace_count(htsn_trace *t) {
    return t ? t->count : 0;
}

void htsn_trace_clear(htsn_trace *t) {
    if (!t) return;
    t->count = 0;
    t->head = 0;
}
