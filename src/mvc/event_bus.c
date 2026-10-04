#include "mvc/event_bus.h"

#include "common/str_util.h"

#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#define MAX_SUBSCRIBERS 64

typedef struct {
    char topic[HTSN_MAX_STR];
    htsn_event_handler cb;
    void *userdata;
} subscription;

struct htsn_event_bus {
    subscription subs[MAX_SUBSCRIBERS];
    int num_subs;
    pthread_mutex_t lock;
};

htsn_event_bus *htsn_event_bus_create(void) {
    htsn_event_bus *bus = calloc(1, sizeof(htsn_event_bus));
    if (!bus) return NULL;
    pthread_mutex_init(&bus->lock, NULL);
    return bus;
}

void htsn_event_bus_destroy(htsn_event_bus *bus) {
    if (!bus) return;
    pthread_mutex_destroy(&bus->lock);
    free(bus);
}

int htsn_event_bus_subscribe(htsn_event_bus *bus, const char *topic,
                             htsn_event_handler cb, void *userdata) {
    if (!bus || !topic || !cb) return HTSN_ERR_INVALID_ARG;
    pthread_mutex_lock(&bus->lock);
    if (bus->num_subs >= MAX_SUBSCRIBERS) {
        pthread_mutex_unlock(&bus->lock);
        return HTSN_ERR_BUSY;
    }
    int i = bus->num_subs++;
    htsn_strlcpy(bus->subs[i].topic, topic, sizeof(bus->subs[i].topic));
    bus->subs[i].cb = cb;
    bus->subs[i].userdata = userdata;
    pthread_mutex_unlock(&bus->lock);
    return HTSN_OK;
}

void htsn_event_bus_publish(htsn_event_bus *bus, const char *topic, void *data) {
    if (!bus || !topic) return;
    /* copy subscribers list to avoid holding lock while calling callbacks */
    subscription local[MAX_SUBSCRIBERS];
    int n = 0;
    pthread_mutex_lock(&bus->lock);
    for (int i = 0; i < bus->num_subs; i++) {
        if (strncmp(topic, bus->subs[i].topic, strlen(bus->subs[i].topic)) == 0 ||
            strcmp(bus->subs[i].topic, "*") == 0) {
            local[n++] = bus->subs[i];
        }
    }
    pthread_mutex_unlock(&bus->lock);

    for (int i = 0; i < n; i++) {
        local[i].cb(topic, data, local[i].userdata);
    }
}
