#include "discovery/discovery.h"

#include "common/str_util.h"

#include <stdlib.h>
#include <string.h>

htsn_discoverer *htsn_discovery_create(htsn_discovery_source src, const char *name,
                                       htsn_discovery_run_fn run,
                                       htsn_discovery_destroy_fn destroy, void *data) {
    htsn_discoverer *d = calloc(1, sizeof(htsn_discoverer));
    if (!d) return NULL;
    d->source = src;
    htsn_strlcpy(d->name, name ? name : "discoverer", sizeof(d->name));
    d->run = run;
    d->destroy = destroy;
    d->data = data;
    return d;
}

void htsn_discovery_destroy(htsn_discoverer *d) {
    if (!d) return;
    if (d->destroy) d->destroy(d);
    free(d);
}
