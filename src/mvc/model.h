#ifndef HTSN_MODEL_H
#define HTSN_MODEL_H

#include "mvc/event_bus.h"

typedef struct {
    char name[HTSN_MAX_STR];
    htsn_event_bus *bus;
} htsn_model;

void htsn_model_init(htsn_model *m, const char *name, htsn_event_bus *bus);
void htsn_model_notify(htsn_model *m, const char *event);
void htsn_model_notify_data(htsn_model *m, const char *event, void *data);

#endif
