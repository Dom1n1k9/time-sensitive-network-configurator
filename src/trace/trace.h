#ifndef HTSN_TRACE_H
#define HTSN_TRACE_H

#include "common/common.h"
#include "db/db.h"
#include "mvc/event_bus.h"

#define HTSN_TRACE_MAX 1024
#define HTSN_TRACE_LINE 256

typedef enum {
    HTSN_TRACE_COMM = 0,
    HTSN_TRACE_FRAME,
    HTSN_TRACE_CONFIG,
    HTSN_TRACE_MULTICAST
} htsn_trace_type;

typedef struct {
    char timestamp[32];
    htsn_trace_type type;
    char source[HTSN_MAX_STR];
    char line[HTSN_TRACE_LINE];
} htsn_trace_entry;

typedef struct htsn_trace htsn_trace;

htsn_trace *htsn_trace_create(htsn_event_bus *bus);
htsn_trace *htsn_trace_create_persistent(htsn_event_bus *bus, htsn_db *db, size_t keep);
void htsn_trace_destroy(htsn_trace *t);

htsn_error htsn_trace_add_comm(htsn_trace *t, const char *source, const char *msg);
htsn_error htsn_trace_add_frame(htsn_trace *t, const char *source, const unsigned char *bytes, size_t len);
htsn_error htsn_trace_add_config(htsn_trace *t, const char *source, const char *what);
htsn_error htsn_trace_add_multicast(htsn_trace *t, const char *source, const char *group, const char *msg);

htsn_trace_entry *htsn_trace_entry_at(htsn_trace *t, int index);
int htsn_trace_count(htsn_trace *t);
void htsn_trace_clear(htsn_trace *t);

#endif
