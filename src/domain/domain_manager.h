#ifndef HTSN_DOMAIN_MANAGER_H
#define HTSN_DOMAIN_MANAGER_H

#include "common/common.h"
#include "db/db.h"
#include "db/db_domains.h"
#include "mvc/event_bus.h"

#define HTSN_DOMAIN_MANAGER_MODEL "domain"

typedef struct htsn_domain_manager htsn_domain_manager;

htsn_domain_manager *htsn_domain_manager_create(htsn_db *db, htsn_event_bus *bus);
void htsn_domain_manager_destroy(htsn_domain_manager *m);
htsn_error htsn_domain_manager_save(htsn_domain_manager *m, const htsn_domain *d);
htsn_error htsn_domain_manager_delete(htsn_domain_manager *m, const char *id);
void htsn_domain_manager_for_each(htsn_domain_manager *m, htsn_db_domain_cb cb, void *ud);
htsn_error htsn_domain_manager_assign_device(htsn_domain_manager *m, const char *device_id,
                                            const char *domain_id);

#endif
