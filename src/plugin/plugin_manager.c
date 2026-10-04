#include "plugin/plugin_manager.h"

#include "common/log.h"

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#define DL_HANDLE HMODULE
#define DL_OPEN(name) LoadLibraryA(name)
#define DL_SYM(handle, sym) ((void *)GetProcAddress(handle, sym))
#define DL_CLOSE(handle) FreeLibrary(handle)
#else
#include <dlfcn.h>
#define DL_HANDLE void *
#define DL_OPEN(name) dlopen(name, RTLD_NOW | RTLD_LOCAL)
#define DL_SYM(handle, sym) dlsym(handle, sym)
#define DL_CLOSE(handle) dlclose(handle)
#endif

#define MAX_PLUGINS 16

struct htsn_plugin_manager {
    htsn_plugin *plugins[MAX_PLUGINS];
    size_t count;
};

htsn_plugin_manager *htsn_plugin_manager_create(void) {
    return calloc(1, sizeof(htsn_plugin_manager));
}

void htsn_plugin_manager_destroy(htsn_plugin_manager *m) {
    if (!m) return;
    for (size_t i = 0; i < m->count; i++) {
        htsn_plugin *p = m->plugins[i];
        if (!p) continue;
        if (p->shutdown && p->shutdown(p) == HTSN_OK) {
        }
        if (p->handle) DL_CLOSE((DL_HANDLE)p->handle);
        free(p);
    }
    free(m);
}

htsn_error htsn_plugin_manager_load(htsn_plugin_manager *m, const char *path) {
    if (!m || !path || m->count >= MAX_PLUGINS) return HTSN_ERR_INVALID_ARG;

    DL_HANDLE h = DL_OPEN(path);
    if (!h) {
        htsn_log(HTSN_LOG_WARN, "plugin load failed: %s", path);
        return HTSN_ERR_IO;
    }

    htsn_plugin_create_fn create = (htsn_plugin_create_fn)DL_SYM(h, "htsn_plugin_create");
    if (!create) {
        DL_CLOSE(h);
        return HTSN_ERR_IO;
    }

    htsn_plugin *p = create();
    if (!p) {
        DL_CLOSE(h);
        return HTSN_ERR_NO_MEMORY;
    }
    p->handle = (void *)h;
    m->plugins[m->count++] = p;
    htsn_log(HTSN_LOG_INFO, "loaded plugin: %s", p->name);
    return HTSN_OK;
}

size_t htsn_plugin_manager_count(htsn_plugin_manager *m) {
    return m ? m->count : 0;
}

htsn_plugin *htsn_plugin_manager_get(htsn_plugin_manager *m, size_t index) {
    if (!m || index >= m->count) return NULL;
    return m->plugins[index];
}
