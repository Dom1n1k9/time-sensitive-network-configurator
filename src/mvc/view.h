#ifndef HTSN_VIEW_H
#define HTSN_VIEW_H

#include "common/common.h"

typedef struct htsn_view htsn_view;

struct htsn_view {
    void (*activate)(htsn_view *self);
    void (*deactivate)(htsn_view *self);
    void (*on_event)(htsn_view *self, const char *topic, void *data);
    void (*render)(htsn_view *self);
    void *userdata;
};

static inline void htsn_view_render(htsn_view *v) {
    if (v && v->render) v->render(v);
}

#endif
