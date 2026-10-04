#ifndef HTSN_DB_DOMAINS_H
#define HTSN_DB_DOMAINS_H

#include "db/db.h"

typedef struct {
    char id[HTSN_MAX_STR];
    char name[HTSN_MAX_STR];
    char description[HTSN_MAX_STR];
} htsn_domain;

htsn_error htsn_db_domain_save(htsn_db *db, const htsn_domain *d);
htsn_error htsn_db_domain_delete(htsn_db *db, const char *id);
typedef void (*htsn_db_domain_cb)(const htsn_domain *d, void *userdata);
void htsn_db_domain_for_each(htsn_db *db, htsn_db_domain_cb cb, void *userdata);

#endif
