#include "mvc/model.h"

#include "mvc/event_bus.h"

#include "common/str_util.h"

#include <string.h>

void htsn_model_init(htsn_model *m, const char *name, htsn_event_bus *bus) {
    if (!m) return;
    htsn_strlcpy(m->name, name, sizeof(m->name));
    m->bus = bus;
}

void htsn_model_notify(htsn_model *m, const char *event) {
    htsn_model_notify_data(m, event, NULL);
}

void htsn_model_notify_data(htsn_model *m, const char *event, void *data) {
    if (!m || !m->bus || !event) return;
    char topic[HTSN_MAX_STR];
    htsn_strlcpy(topic, m->name, sizeof(topic));
    htsn_strlcpy(topic + strlen(topic), ".", sizeof(topic) - strlen(topic));
    htsn_strlcpy(topic + strlen(topic), event, sizeof(topic) - strlen(topic));
    htsn_event_bus_publish(m->bus, topic, data);
}
