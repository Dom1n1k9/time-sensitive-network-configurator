#ifndef HTSN_CONTROLLER_H
#define HTSN_CONTROLLER_H

#include "common/common.h"
#include "mvc/view.h"

typedef struct htsn_controller htsn_controller;

typedef void (*htsn_controller_views_ref)(htsn_view *views[], size_t *n);

htsn_controller *htsn_controller_create(void);
void htsn_controller_destroy(htsn_controller *c);
htsn_error htsn_controller_register_view(htsn_controller *c, htsn_view *v, const char *name);
htsn_view *htsn_controller_activate(htsn_controller *c, const char *name);
void htsn_controller_route_event(htsn_controller *c, const char *topic, void *data);
void htsn_controller_run(htsn_controller *c, void *ctx);

#endif
