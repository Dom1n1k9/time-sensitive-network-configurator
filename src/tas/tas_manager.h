#ifndef HTSN_TAS_MANAGER_H
#define HTSN_TAS_MANAGER_H

#include "common/common.h"
#include "db/db.h"
#include "db/db_tas.h"
#include "mvc/event_bus.h"
#include "tas/tas.h"

#define HTSN_TAS_MANAGER_MODEL "tas"

typedef struct htsn_tas_manager htsn_tas_manager;

htsn_tas_manager *htsn_tas_manager_create(htsn_db *db, htsn_event_bus *bus);
void htsn_tas_manager_destroy(htsn_tas_manager *m);

htsn_error htsn_tas_manager_save(htsn_tas_manager *m, const htsn_tas_schedule_model *s);
htsn_error htsn_tas_manager_load(htsn_tas_manager *m, const char *id, htsn_tas_schedule_model *out);
htsn_error htsn_tas_manager_delete(htsn_tas_manager *m, const char *id);
htsn_error htsn_tas_manager_deploy(htsn_tas_manager *m, const char *id);

#endif
