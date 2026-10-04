#include "mvc/controller.h"

#include "common/str_util.h"

#include <stdlib.h>
#include <string.h>

#define MAX_VIEWS 32

struct htsn_controller {
    struct {
        htsn_view *view;
        char name[HTSN_MAX_STR];
    } views[MAX_VIEWS];
    int num_views;
    htsn_view *active;
};

htsn_controller *htsn_controller_create(void) {
    return calloc(1, sizeof(htsn_controller));
}

void htsn_controller_destroy(htsn_controller *c) {
    free(c);
}

htsn_error htsn_controller_register_view(htsn_controller *c, htsn_view *v, const char *name) {
    if (!c || !v || !name || c->num_views >= MAX_VIEWS) return HTSN_ERR_INVALID_ARG;
    htsn_strlcpy(c->views[c->num_views].name, name, HTSN_MAX_STR);
    c->views[c->num_views].view = v;
    c->num_views++;
    return HTSN_OK;
}

htsn_view *htsn_controller_activate(htsn_controller *c, const char *name) {
    if (!c || !name) return NULL;
    for (int i = 0; i < c->num_views; i++) {
        if (strcmp(c->views[i].name, name) == 0) {
            if (c->active && c->active->deactivate) c->active->deactivate(c->active);
            c->active = c->views[i].view;
            if (c->active && c->active->activate) c->active->activate(c->active);
            return c->active;
        }
    }
    return NULL;
}

void htsn_controller_route_event(htsn_controller *c, const char *topic, void *data) {
    if (!c || !topic) return;
    if (c->active && c->active->on_event) c->active->on_event(c->active, topic, data);
}

void htsn_controller_run(htsn_controller *c, void *ctx) {
    (void)c;
    (void)ctx;
}
