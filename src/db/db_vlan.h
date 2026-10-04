#ifndef HTSN_DB_VLAN_H
#define HTSN_DB_VLAN_H

#include "db/db.h"
#include "device/device.h"

typedef struct {
    char id[HTSN_MAX_STR];
    char name[HTSN_MAX_STR];
    int vlan_id;
} htsn_vlan_group;

typedef struct {
    char group_id[HTSN_MAX_STR];
    char device_id[HTSN_MAX_STR];
} htsn_vlan_member;

htsn_error htsn_db_vlan_group_save(htsn_db *db, const htsn_vlan_group *g);
htsn_error htsn_db_vlan_group_delete(htsn_db *db, const char *id);
typedef int (*htsn_db_vlan_group_cb)(const htsn_vlan_group *g, void *userdata);
void htsn_db_vlan_group_for_each(htsn_db *db, htsn_db_vlan_group_cb cb, void *userdata);

htsn_error htsn_db_vlan_member_add(htsn_db *db, const htsn_vlan_member *m);
htsn_error htsn_db_vlan_member_remove(htsn_db *db, const char *group_id, const char *device_id);
typedef int (*htsn_db_vlan_member_cb)(const htsn_vlan_member *m, void *userdata);
void htsn_db_vlan_member_for_each_group(htsn_db *db, const char *group_id, htsn_db_vlan_member_cb cb, void *userdata);
void htsn_db_vlan_member_for_each_all(htsn_db *db, htsn_db_vlan_member_cb cb, void *userdata);

#endif
