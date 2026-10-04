#ifndef HTSN_DB_H
#define HTSN_DB_H

#include "common/common.h"

#include <sqlite3.h>

typedef struct htsn_db {
    sqlite3 *handle;
    char path[HTSN_MAX_STR];
} htsn_db;

htsn_error htsn_db_open(htsn_db *db, const char *path);
void htsn_db_close(htsn_db *db);
htsn_error htsn_db_migrate(htsn_db *db);

#endif
