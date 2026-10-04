#ifndef HTSN_VLAN_H
#define HTSN_VLAN_H

#include "common/common.h"
#include "device/device.h"

#define HTSN_VLAN_ID_MIN 1
#define HTSN_VLAN_ID_MAX 4094

typedef struct {
    char id[HTSN_MAX_STR];
    char name[HTSN_MAX_STR];
    int vlan_id;
} htsn_vlan_group_model;

typedef struct {
    char group_id[HTSN_MAX_STR];
    char device_id[HTSN_MAX_STR];
} htsn_vlan_membership;

htsn_error htsn_vlan_validate_group(const htsn_vlan_group_model *g);
void htsn_vlan_group_id(htsn_vlan_group_model *g);

#endif
