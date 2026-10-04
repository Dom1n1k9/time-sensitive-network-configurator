#include "app/app.h"

#include "device/device_manager.h"
#include "common/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    htsn_app_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    snprintf(cfg.db_path, sizeof(cfg.db_path), "htsn.db");
    snprintf(cfg.mqtt_host, sizeof(cfg.mqtt_host), "localhost");
    cfg.mqtt_port = 1883;
    cfg.headless = true;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--db") == 0 && i + 1 < argc) {
            snprintf(cfg.db_path, sizeof(cfg.db_path), "%s", argv[++i]);
        } else if (strcmp(argv[i], "--mqtt-host") == 0 && i + 1 < argc) {
            snprintf(cfg.mqtt_host, sizeof(cfg.mqtt_host), "%s", argv[++i]);
        } else if (strcmp(argv[i], "--mqtt-port") == 0 && i + 1 < argc) {
            cfg.mqtt_port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--mqtt-user") == 0 && i + 1 < argc) {
            snprintf(cfg.mqtt_user, sizeof(cfg.mqtt_user), "%s", argv[++i]);
        } else if (strcmp(argv[i], "--mqtt-pass") == 0 && i + 1 < argc) {
            snprintf(cfg.mqtt_pass, sizeof(cfg.mqtt_pass), "%s", argv[++i]);
        } else if (strcmp(argv[i], "--plugin-dir") == 0 && i + 1 < argc) {
            snprintf(cfg.plugin_dir, sizeof(cfg.plugin_dir), "%s", argv[++i]);
        } else if (strcmp(argv[i], "--headless") == 0) {
            cfg.headless = true;
        }
    }

    /* Env fallback keeps the password out of the command line / unit file. */
    if (cfg.mqtt_user[0] == 0) {
        const char *e = getenv("HTSN_USER");
        if (e) snprintf(cfg.mqtt_user, sizeof(cfg.mqtt_user), "%s", e);
    }
    if (cfg.mqtt_pass[0] == 0) {
        const char *e = getenv("HTSN_PASS");
        if (e) snprintf(cfg.mqtt_pass, sizeof(cfg.mqtt_pass), "%s", e);
    }

    htsn_app app;
    if (htsn_app_init(&app, &cfg) != HTSN_OK) {
        fprintf(stderr, "application init failed\n");
        return EXIT_FAILURE;
    }
    htsn_log(HTSN_LOG_INFO, "htsn-configurator %d devices restored",
             (int)htsn_device_manager_count(app.devices));

    int rc = (htsn_app_run(&app) == HTSN_OK) ? EXIT_SUCCESS : EXIT_FAILURE;
    htsn_app_shutdown(&app);
    return rc;
}
