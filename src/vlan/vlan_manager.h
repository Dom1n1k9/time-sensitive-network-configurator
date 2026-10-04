#ifndef HTSN_VLAN_MANAGER_H
#define HTSN_VLAN_MANAGER_H

#include "common/common.h"
#include "db/db.h"
#include "db/db_vlan.h"
#include "mvc/event_bus.h"
#include "vlan/vlan.h"

#define HTSN_VLAN_MANAGER_MODEL "vlan"

typedef struct htsn_vlan_manager htsn_vlan_manager;

htsn_vlan_manager *htsn_vlan_manager_create(htsn_db *db, htsn_event_bus *bus);
void htsn_vlan_manager_destroy(htsn_vlan_manager *m);

htsn_error htsn_vlan_manager_create_group(htsn_vlan_manager *m, const htsn_vlan_group_model *g);
htsn_error htsn_vlan_manager_delete_group(htsn_vlan_manager *m, const char *id);
htsn_error htsn_vlan_manager_add_member(htsn_vlan_manager *m, const char *group_id, const char *device_id);
htsn_error htsn_vlan_manager_remove_member(htsn_vlan_manager *m, const char *group_id, const char *device_id);

htsn_error htsn_vlan_manager_import(htsn_vlan_manager *m, const char *file);
htsn_error htsn_vlan_manager_export(htsn_vlan_manager *m, const char *file);

#endif
