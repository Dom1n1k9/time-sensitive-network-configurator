#ifndef HTSN_EVENT_BUS_H
#define HTSN_EVENT_BUS_H

#include "common/common.h"

#include <stdbool.h>

typedef void (*htsn_event_handler)(const char *topic, void *data, void *userdata);

typedef struct htsn_event_bus htsn_event_bus;

htsn_event_bus *htsn_event_bus_create(void);
void htsn_event_bus_destroy(htsn_event_bus *bus);
void htsn_event_bus_publish(htsn_event_bus *bus, const char *topic, void *data);
int htsn_event_bus_subscribe(htsn_event_bus *bus, const char *topic, htsn_event_handler cb, void *userdata);

#endif
