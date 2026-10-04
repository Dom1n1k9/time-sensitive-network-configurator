#include "vlan/vlan.h"

#include <string.h>
#include <stdio.h>

htsn_error htsn_vlan_validate_group(const htsn_vlan_group_model *g) {
    if (!g) return HTSN_ERR_INVALID_ARG;
    if (g->vlan_id < HTSN_VLAN_ID_MIN || g->vlan_id > HTSN_VLAN_ID_MAX)
        return HTSN_ERR_INVALID_ARG;
    if (strlen(g->name) == 0) return HTSN_ERR_INVALID_ARG;
    return HTSN_OK;
}

void htsn_vlan_group_id(htsn_vlan_group_model *g) {
    snprintf(g->id, sizeof(g->id), "vlan-%d", g->vlan_id);
}
