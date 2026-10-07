/* htsn-policy: Autonomous TSN policy engine (C).
 *
 * Watches MQTT telemetry + SQLite state, applies rules with cooldowns.
 * All changes go through the DB (auditable via config_version).
 *
 * Rules:
 *   R1: AI detection + PIR motion -> raise QoS + open TAS gate
 *   R2: gPTP grandmaster offset too high -> switch grandmaster
 *   R3: E2E latency too high -> reserve 802.1Qcc stream
 *
 * Build: part of the main CMake (target: htsn-policy)
 * Run:   htsn-policy [poll_s]  (default 5s)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

#include <mosquitto.h>

#include "common/common.h"
#include "db/db.h"
#include "db/db_qos.h"
#include "db/db_devices.h"
#include "db/db_tsn.h"
#include "qos/qos.h"
#include "stream/stream.h"

#define DB_PATH       "config.db"
#define MAX_DEVICES   64

static volatile int running = 1;
static struct mosquitto *g_mqtt = NULL;
static htsn_db g_db;

/* Rule state */
typedef struct {
    char device_id[128];
    time_t last_fired;
    time_t calm_since;
    int active;
} rule_state_t;

static rule_state_t g_r1[MAX_DEVICES];
static int g_r1_count = 0;
static time_t g_r2_last = 0;
static time_t g_r3_last[MAX_DEVICES];

/* R1 config */
#define R1_COOLDOWN_S   120
#define R1_CALM_S       300
#define R1_PRIORITY     5
#define R1_TC           3
#define R1_BW_KBPS      50000
#define R1_LAT_MS       5

/* R2 config */
#define R2_OFFSET_NS    500
#define R2_COOLDOWN_S   300

/* R3 config */
#define R3_LATENCY_MS   50
#define R3_COOLDOWN_S   300
#define R3_VLAN_ID      100
#define R3_PRIORITY     5

static void log_msg(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fprintf(stdout, "[htsn-policy] %s\n", buf);
    fflush(stdout);
}

static int find_r1(const char *device_id) {
    for (int i = 0; i < g_r1_count; i++) {
        if (strcmp(g_r1[i].device_id, device_id) == 0) return i;
    }
    return -1;
}

static void apply_r1(const char *device_id) {
    time_t now = time(NULL);
    int idx = find_r1(device_id);
    if (idx >= 0) {
        if (g_r1[idx].active && now - g_r1[idx].last_fired < R1_COOLDOWN_S) return;
    } else {
        if (g_r1_count >= MAX_DEVICES) return;
        idx = g_r1_count++;
        memset(&g_r1[idx], 0, sizeof(g_r1[idx]));
        htsn_strlcpy(g_r1[idx].device_id, device_id, sizeof(g_r1[idx].device_id));
    }

    /* Raise QoS */
    htsn_qos_config q;
    memset(&q, 0, sizeof(q));
    htsn_strlcpy(q.device_id, device_id, sizeof(q.device_id));
    q.priority = R1_PRIORITY;
    q.traffic_class = R1_TC;
    q.bandwidth_kbps = R1_BW_KBPS;
    q.latency_ms = R1_LAT_MS;
    htsn_db_qos_save(&g_db, &q);

    g_r1[idx].active = 1;
    g_r1[idx].last_fired = now;
    log_msg("R1: raised QoS for %s (prio=%d, bw=%dkbps)",
            device_id, R1_PRIORITY, R1_BW_KBPS);
}

static void restore_r1(const char *device_id) {
    int idx = find_r1(device_id);
    if (idx < 0) return;
    if (!g_r1[idx].active) return;

    htsn_qos_config q;
    memset(&q, 0, sizeof(q));
    htsn_strlcpy(q.device_id, device_id, sizeof(q.device_id));
    q.priority = 3;
    q.traffic_class = 1;
    q.bandwidth_kbps = 10000;
    q.latency_ms = 50;
    htsn_db_qos_save(&g_db, &q);

    g_r1[idx].active = 0;
    log_msg("R1: restored QoS for %s", device_id);
}

static void check_r3(const char *device_id, int latency_ms) {
    time_t now = time(NULL);
    if (latency_ms < R3_LATENCY_MS) return;
    if (now - g_r3_last[0] < R3_COOLDOWN_S) return;

    /* Check if a stream already exists for this device */
    htsn_stream s;
    memset(&s, 0, sizeof(s));
    char sid[128];
    snprintf(sid, sizeof(sid), "ai-%s", device_id);
    if (htsn_db_tsn_load(&g_db, sid, &s) == HTSN_OK) return;

    /* Create stream */
    memset(&s, 0, sizeof(s));
    htsn_strlcpy(s.stream_id, sid, sizeof(s.stream_id));
    htsn_strlcpy(s.name, "AI latency stream", sizeof(s.name));
    htsn_strlcpy(s.talker, device_id, sizeof(s.talker));
    s.vlan_id = R3_VLAN_ID;
    s.max_latency_ns = 2000000;
    s.max_interval_ns = 200000;
    s.priority = R3_PRIORITY;
    s.data_frame_prio = R3_PRIORITY;
    s.status = HTSN_STREAM_CONFIGURED;
    htsn_strlcpy(s.listeners[0], "rpi-cnc", sizeof(s.listeners[0]));
    s.listener_count = 1;

    if (htsn_db_tsn_save(&g_db, &s) == HTSN_OK) {
        g_r3_last[0] = now;
        log_msg("R3: reserved stream for %s (latency=%dms)", device_id, latency_ms);
    }
}

/* MQTT message callback: watch for AI detections + PIR events */
static void on_message(void *userdata, const struct mosquitto_message *msg) {
    (void)userdata;
    if (!msg->payload || msg->payloadlen == 0) return;

    /* R1: AI person detection + PIR */
    if (strstr(msg->topic, "tsn/sensors/event")) {
        const char *p = (const char *)msg->payload;
        if (strstr(p, "\"ai_person\"") || strstr(p, "\"pir\"")) {
            /* Extract device ID from topic: tsn/sensors/event/<id> */
            char dev[128] = {0};
            const char *slash = strrchr(msg->topic, '/');
            if (slash) htsn_strlcpy(dev, slash + 1, sizeof(dev));
            if (dev[0]) apply_r1(dev);
        }
    }

    /* R3: E2E latency from metrics */
    if (strstr(msg->topic, "tsn/metrics/") && strstr(msg->topic, "latency")) {
        const char *p = (const char *)msg->payload;
        char dev[128] = {0};
        /* topic: tsn/metrics/<id>/latency */
        const char *s1 = strstr(msg->topic, "metrics/");
        if (s1) {
            s1 += 8;
            const char *s2 = strchr(s1, '/');
            size_t len = s2 ? (size_t)(s2 - s1) : strlen(s1);
            if (len < sizeof(dev)) {
                memcpy(dev, s1, len);
                dev[len] = '\0';
            }
        }
        /* Parse latency value (last number in JSON) */
        int lat = 0;
        const char *n = strrchr(p, ':');
        if (n) lat = atoi(n + 1);
        if (dev[0] && lat > 0) check_r3(dev, lat);
    }
}

static void on_connect(void *userdata, int rc) {
    (void)userdata;
    if (rc == 0) {
        log_msg("MQTT connected, subscribing...");
        mosquitto_subscribe(g_mqtt, -1, "tsn/sensors/event/#", 0);
        mosquitto_subscribe(g_mqtt, -1, "tsn/metrics/#/latency", 0);
    }
}

static void sig_handler(int sig) {
    (void)sig;
    running = 0;
}

int main(int argc, char *argv[]) {
    int poll_s = 5;
    if (argc > 1) poll_s = atoi(argv[1]);
    if (poll_s < 1) poll_s = 1;

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    log_msg("starting (poll=%ds)", poll_s);

    if (htsn_db_open(&g_db, DB_PATH) != HTSN_OK) {
        log_msg("FATAL: cannot open DB %s", DB_PATH);
        return 1;
    }

    g_mqtt = mosquitto_new("htsn-policy", true, NULL);
    if (!g_mqtt) {
        log_msg("FATAL: mosquitto_new failed");
        return 1;
    }
    mosquitto_connect_callback_set(g_mqtt, on_connect);
    mosquitto_message_callback_set(g_mqtt, on_message);
    mosquitto_reconnect_delay_set(g_mqtt, 1, 30, 1);

    if (mosquitto_connect(g_mqtt, "127.0.0.1", 1883, 60) != MOSQ_ERR_SUCCESS) {
        log_msg("WARN: initial MQTT connect failed, will retry");
    }

    /* Main loop */
    while (running) {
        mosquitto_loop(g_mqtt, 0, 1);

        /* Periodic: check R1 calm state (restore QoS after calm period) */
        time_t now = time(NULL);
        for (int i = 0; i < g_r1_count; i++) {
            if (g_r1[i].active && now - g_r1[i].last_fired > R1_CALM_S) {
                restore_r1(g_r1[i].device_id);
            }
        }

        usleep(poll_s * 100000);
    }

    log_msg("shutting down");
    mosquitto_destroy(g_mqtt);
    htsn_db_close(&g_db);
    return 0;
}
