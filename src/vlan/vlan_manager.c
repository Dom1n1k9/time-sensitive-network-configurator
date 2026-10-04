#include "vlan/vlan_manager.h"

#include "db/db_vlan.h"

#include "vlan/vlan.h"

#include "common/str_util.h"

#include "common/log.h"
#include "mvc/model.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct htsn_vlan_manager {
    htsn_db *db;
    htsn_event_bus *bus;
    htsn_model model;
};

htsn_vlan_manager *htsn_vlan_manager_create(htsn_db *db, htsn_event_bus *bus) {
    if (!db || !bus) return NULL;
    htsn_vlan_manager *m = calloc(1, sizeof(htsn_vlan_manager));
    if (!m) return NULL;
    m->db = db;
    m->bus = bus;
    htsn_model_init(&m->model, HTSN_VLAN_MANAGER_MODEL, bus);
    return m;
}

void htsn_vlan_manager_destroy(htsn_vlan_manager *m) {
    free(m);
}

htsn_error htsn_vlan_manager_create_group(htsn_vlan_manager *m, const htsn_vlan_group_model *g) {
    if (!m || !g) return HTSN_ERR_INVALID_ARG;
    htsn_error e = htsn_vlan_validate_group(g);
    if (e != HTSN_OK) return e;
    htsn_vlan_group tmp;
    memset(&tmp, 0, sizeof(tmp));
    htsn_strlcpy(tmp.id, g->id, sizeof(tmp.id));
    htsn_strlcpy(tmp.name, g->name, sizeof(tmp.name));
    tmp.vlan_id = g->vlan_id;
    htsn_db_vlan_group_save(m->db, &tmp);
    htsn_model_notify(&m->model, "group_changed");
    return HTSN_OK;
}

htsn_error htsn_vlan_manager_delete_group(htsn_vlan_manager *m, const char *id) {
    if (!m || !id) return HTSN_ERR_INVALID_ARG;
    htsn_db_vlan_group_delete(m->db, id);
    htsn_model_notify(&m->model, "group_changed");
    return HTSN_OK;
}

htsn_error htsn_vlan_manager_add_member(htsn_vlan_manager *m, const char *group_id, const char *device_id) {
    if (!m || !group_id || !device_id) return HTSN_ERR_INVALID_ARG;
    htsn_vlan_member mem;
    memset(&mem, 0, sizeof(mem));
    htsn_strlcpy(mem.group_id, group_id, sizeof(mem.group_id));
    htsn_strlcpy(mem.device_id, device_id, sizeof(mem.device_id));
    htsn_db_vlan_member_add(m->db, &mem);
    htsn_model_notify(&m->model, "member_changed");
    return HTSN_OK;
}

htsn_error htsn_vlan_manager_remove_member(htsn_vlan_manager *m, const char *group_id, const char *device_id) {
    if (!m || !group_id || !device_id) return HTSN_ERR_INVALID_ARG;
    htsn_db_vlan_member_remove(m->db, group_id, device_id);
    htsn_model_notify(&m->model, "member_changed");
    return HTSN_OK;
}

struct import_ctx {
    htsn_vlan_manager *m;
};

static int export_group_cb(const htsn_vlan_group *g, void *ud) {
    FILE *f = (FILE *)ud;
    fprintf(f, "VLAN,%s,%d\n", g->name, g->vlan_id);
    return 0;
}

htsn_error htsn_vlan_manager_import(htsn_vlan_manager *m, const char *file) {
    if (!m || !file) return HTSN_ERR_INVALID_ARG;
    FILE *f = fopen(file, "r");
    if (!f) return HTSN_ERR_IO;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char name[HTSN_MAX_STR] = "";
        int vlan_id = 0;
        if (sscanf(line, "VLAN,%255[^,],%d", name, &vlan_id) == 2) {
            htsn_vlan_group_model g;
            memset(&g, 0, sizeof(g));
            htsn_strlcpy(g.name, name, sizeof(g.name));
            g.vlan_id = vlan_id;
            htsn_vlan_group_id(&g);
            htsn_vlan_manager_create_group(m, &g);
        }
    }
    fclose(f);
    return HTSN_OK;
}

htsn_error htsn_vlan_manager_export(htsn_vlan_manager *m, const char *file) {
    if (!m || !file) return HTSN_ERR_INVALID_ARG;
    FILE *f = fopen(file, "w");
    if (!f) return HTSN_ERR_IO;
    htsn_db_vlan_group_for_each(m->db, export_group_cb, f);
    fclose(f);
    return HTSN_OK;
}
