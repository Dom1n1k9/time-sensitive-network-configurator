#include "common/common.h"

#include "db/db_devices.h"

#include "device/device.h"

#include "tas/gcl.h"
#include "common/str_util.h"
#include "config_version/config_version_manager.h"
#include "db/db.h"
#include "db/db_tsn.h"
#include "device/device_manager.h"
#include "db/db_qos.h"
#include "mvc/event_bus.h"
#include "qos/qos.h"
#include "sensors/sensor.h"
#include "stream/stream.h"
#include "tas/tas.h"
#include "timesync/timesync.h"
#include "vlan/vlan.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0;
static int tests_failed = 0;

#define CHECK(cond) do { tests_run++; if (!(cond)) { tests_failed++; \
    fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void test_device(void) {
    htsn_device d;
    memset(&d, 0, sizeof(d));
    htsn_strlcpy(d.id, "esp-1", sizeof(d.id));
    htsn_strlcpy(d.name, "hall sensor node", sizeof(d.name));
    htsn_strlcpy(d.ip, "192.168.1.50", sizeof(d.ip));
    d.kind = HTSN_DEVICE_KIND_ESP32;
    d.status = HTSN_DEVICE_ONLINE;
    snprintf(d.firmware, sizeof(d.firmware), "1.4.2");
    htsn_device_add_tsn_feature(&d, "802.1Qbv");
    htsn_device_add_tsn_feature(&d, "802.1AS");

    CHECK(strcmp(htsn_device_status_str(d.status), "online") == 0);
    CHECK(d.tsn_features_count == 2);
    CHECK(strcmp(d.tsn_features[0], "802.1Qbv") == 0);
}

static void test_qos_validation(void) {
    htsn_qos_config_model q;
    memset(&q, 0, sizeof(q));
    htsn_strlcpy(q.device_id, "esp-1", sizeof(q.device_id));
    q.priority = 5;
    q.traffic_class = HTSN_QOS_TC_CRITICAL;
    q.bandwidth_kbps = 1000;
    q.latency_ms = 10;

    CHECK(htsn_qos_validate(&q) == HTSN_OK);

    htsn_qos_config_model bad = q;
    bad.priority = 8;
    CHECK(htsn_qos_validate(&bad) == HTSN_ERR_INVALID_ARG);
}

static void test_vlan(void) {
    htsn_vlan_group_model g;
    memset(&g, 0, sizeof(g));
    htsn_strlcpy(g.name, "zone-A", sizeof(g.name));
    g.vlan_id = 100;
    CHECK(htsn_vlan_validate_group(&g) == HTSN_OK);
    htsn_vlan_group_id(&g);
    CHECK(strcmp(g.id, "vlan-100") == 0);
}

static void test_gcl(void) {
    htsn_gcl gcl;
    CHECK(htsn_gcl_init(&gcl, 100000) == HTSN_OK);
    CHECK(htsn_gcl_add_entry(&gcl, HTSN_GATE_OPEN, 40000) == HTSN_OK);
    CHECK(htsn_gcl_add_entry(&gcl, 0, 60000) == HTSN_OK);
    CHECK(htsn_gcl_is_valid(&gcl) == true);

    htsn_gcl gcl2;
    CHECK(htsn_gcl_init(&gcl2, 100000) == HTSN_OK);
    CHECK(htsn_gcl_add_entry(&gcl2, HTSN_GATE_OPEN, 10000) == HTSN_OK);
    CHECK(htsn_gcl_is_valid(&gcl2) == false);

    char buf[256];
    htsn_gcl_render_ascii(&gcl, buf, sizeof(buf));
    CHECK(strlen(buf) > 0);
}

static void test_timesync(void) {
    CHECK(strcmp(htsn_timesync_mode_str(HTSN_TIMESYNC_LOCAL_GRANDMASTER),
                "local_grandmaster") == 0);
    CHECK(htsn_timesync_mode_parse("external_grandmaster") ==
          HTSN_TIMESYNC_EXTERNAL_GRANDMASTER);
}

static void test_sensor_type(void) {
    CHECK(htsn_sensor_type_parse("imu") == HTSN_SENSOR_IMU);
    CHECK(strcmp(htsn_sensor_type_str(HTSN_SENSOR_PRESSURE), "pressure") == 0);
}

static void test_db_roundtrip(void) {
    htsn_db db;
    CHECK(htsn_db_open(&db, "test_htsn.db") == HTSN_OK);

    htsn_device d;
    memset(&d, 0, sizeof(d));
    htsn_strlcpy(d.id, "stm-1", sizeof(d.id));
    d.kind = HTSN_DEVICE_KIND_STM32;
    d.status = HTSN_DEVICE_ONLINE;
    htsn_device_add_tsn_feature(&d, "802.1Qav");

    CHECK(htsn_db_device_upsert(&db, &d) == HTSN_OK);

    htsn_device loaded;
    memset(&loaded, 0, sizeof(loaded));
    CHECK(htsn_db_device_get(&db, "stm-1", &loaded) == HTSN_OK);
    CHECK(loaded.kind == HTSN_DEVICE_KIND_STM32);
    CHECK(loaded.tsn_features_count == 1);
    CHECK(strcmp(loaded.tsn_features[0], "802.1Qav") == 0);

    htsn_db_close(&db);
    remove("test_htsn.db");
}

static void test_stream_validate(void) {
    htsn_stream s;
    memset(&s, 0, sizeof(s));
    htsn_strlcpy(s.stream_id, "stream-1", sizeof(s.stream_id));
    htsn_strlcpy(s.name, "Control", sizeof(s.name));
    htsn_strlcpy(s.talker, "esp32-01", sizeof(s.talker));
    s.vlan_id = 100;
    s.max_latency_ns = 1000000;
    s.max_interval_ns = 100000;
    s.priority = 5;
    s.data_frame_prio = 5;
    htsn_strlcpy(s.listeners[0], "rpi-1", sizeof(s.listeners[0]));
    s.listener_count = 1;

    CHECK(htsn_stream_validate(&s) == HTSN_OK);

    htsn_stream bad = s;
    bad.priority = 8;
    CHECK(htsn_stream_validate(&bad) == HTSN_ERR_INVALID_ARG);

    htsn_stream nono = s;
    nono.listener_count = 0;
    nono.listener_all = 0;
    CHECK(htsn_stream_validate(&nono) == HTSN_ERR_INVALID_ARG);

    CHECK(strcmp(htsn_stream_status_str(HTSN_STREAM_READY), "ready") == 0);
    CHECK(htsn_stream_status_parse("failed") == HTSN_STREAM_FAILED);
    CHECK(strcmp(htsn_stream_role_str(HTSN_STREAM_ROLE_LISTENER), "listener") == 0);
}

static void test_stream_db_roundtrip(void) {
    htsn_db db;
    CHECK(htsn_db_open(&db, "test_stream.db") == HTSN_OK);

    htsn_stream s;
    memset(&s, 0, sizeof(s));
    htsn_strlcpy(s.stream_id, "s1", sizeof(s.stream_id));
    htsn_strlcpy(s.name, "Control", sizeof(s.name));
    htsn_strlcpy(s.talker, "esp32-01", sizeof(s.talker));
    s.vlan_id = 100;
    s.max_latency_ns = 1000000;
    s.max_interval_ns = 100000;
    s.priority = 5;
    s.data_frame_prio = 5;
    s.status = HTSN_STREAM_CONFIGURED;
    htsn_strlcpy(s.listeners[0], "rpi-1", sizeof(s.listeners[0]));
    s.listener_count = 1;

    CHECK(htsn_db_tsn_save(&db, &s) == HTSN_OK);

    htsn_stream loaded;
    memset(&loaded, 0, sizeof(loaded));
    CHECK(htsn_db_tsn_load(&db, "s1", &loaded) == HTSN_OK);
    CHECK(strcmp(loaded.talker, "esp32-01") == 0);
    CHECK(loaded.listener_count == 1);
    CHECK(strcmp(loaded.listeners[0], "rpi-1") == 0);
    CHECK(loaded.priority == 5);

    htsn_db_tsn_set_status(&db, "s1", HTSN_STREAM_READY);
    memset(&loaded, 0, sizeof(loaded));
    CHECK(htsn_db_tsn_load(&db, "s1", &loaded) == HTSN_OK);
    CHECK(loaded.status == HTSN_STREAM_READY);

    CHECK(htsn_db_tsn_delete(&db, "s1") == HTSN_OK);
    CHECK(htsn_db_tsn_load(&db, "s1", &loaded) == HTSN_ERR_NOT_FOUND);

    htsn_db_close(&db);
    remove("test_stream.db");
}

static void test_str_util(void) {
    char buf[8];
    memset(buf, 'x', sizeof(buf));

    CHECK(htsn_strlcpy(buf, "hello", sizeof(buf)) == 5);
    CHECK(strcmp(buf, "hello") == 0);

    CHECK(htsn_strlcpy(buf, "hello world", sizeof(buf)) == 11);
    CHECK(strcmp(buf, "hello w") == 0);

    memset(buf, 'x', sizeof(buf));
    CHECK(htsn_strlcpy(buf, "abc", 0) == 0);
    CHECK(buf[0] == 'x');

    char s[32];
    htsn_strlcpy(s, "  pad \t", sizeof(s));
    htsn_str_trim(s);
    CHECK(strcmp(s, "pad") == 0);

    CHECK(htsn_str_starts_with("wireless-tsn", "wireless") == true);
    CHECK(htsn_str_starts_with("wireless", "tsn") == false);

    char *d = htsn_str_dup("dup me");
    CHECK(d != NULL);
    if (d) {
        CHECK(strcmp(d, "dup me") == 0);
        free(d);
    }

    CHECK(htsn_str_valid_utf8("plain ascii") == 1);
    char bad[4];
    bad[0] = (char)0xFF;
    bad[1] = 'a';
    bad[2] = '\0';
    CHECK(htsn_str_valid_utf8(bad) == 0);
    CHECK(htsn_str_valid_utf8(NULL) == 0);
}

static int ev_count_a = 0;
static int ev_count_star = 0;
static char ev_topic[HTSN_MAX_STR];

static void ev_handler(const char *topic, void *data, void *userdata) {
    (void)data;
    *(int *)userdata += 1;
    htsn_strlcpy(ev_topic, topic, sizeof(ev_topic));
}

static void test_event_bus(void) {
    ev_count_a = 0;
    ev_count_star = 0;
    ev_topic[0] = '\0';
    htsn_event_bus *bus = htsn_event_bus_create();
    CHECK(bus != NULL);
    if (!bus) return;

    CHECK(htsn_event_bus_subscribe(bus, "tsn", ev_handler, &ev_count_a) == HTSN_OK);
    CHECK(htsn_event_bus_subscribe(bus, "*", ev_handler, &ev_count_star) == HTSN_OK);
    CHECK(htsn_event_bus_subscribe(bus, NULL, ev_handler, &ev_count_a) == HTSN_ERR_INVALID_ARG);
    CHECK(htsn_event_bus_subscribe(NULL, "tsn", ev_handler, &ev_count_a) == HTSN_ERR_INVALID_ARG);

    htsn_event_bus_publish(bus, "tsn/cmd/apply", NULL);
    CHECK(ev_count_a == 1);
    CHECK(ev_count_star == 1);
    CHECK(strcmp(ev_topic, "tsn/cmd/apply") == 0);

    htsn_event_bus_publish(bus, "other/topic", NULL);
    CHECK(ev_count_a == 1);
    CHECK(ev_count_star == 2);

    htsn_event_bus_destroy(bus);
}

static void test_config_version(void) {
    htsn_db db;
    CHECK(htsn_db_open(&db, "test_cfgver.db") == HTSN_OK);
    htsn_event_bus *bus = htsn_event_bus_create();
    htsn_config_version_manager *m = htsn_cfg_ver_manager_create(&db, bus);
    CHECK(m != NULL);
    if (!m) {
        htsn_event_bus_destroy(bus);
        htsn_db_close(&db);
        remove("test_cfgver.db");
        return;
    }

    CHECK(htsn_cfg_ver_snapshot(m, "v1", NULL) == HTSN_OK);
    CHECK(htsn_cfg_ver_count(m) == 1);

    htsn_qos_config q;
    memset(&q, 0, sizeof(q));
    htsn_strlcpy(q.device_id, "d1", sizeof(q.device_id));
    q.priority = 5;
    q.traffic_class = 5;
    q.bandwidth_kbps = 1000;
    q.latency_ms = 5;
    CHECK(htsn_db_qos_save(&db, &q) == HTSN_OK);
    CHECK(htsn_cfg_ver_snapshot(m, "v2", NULL) == HTSN_OK);
    CHECK(htsn_cfg_ver_count(m) == 2);

    char out[4096];
    CHECK(htsn_cfg_ver_diff(m, 1, 2, out, sizeof(out)) == HTSN_OK);
    CHECK(strstr(out, "qos:d1:p=5") != NULL);

    CHECK(htsn_cfg_ver_diff(m, 1, 1, out, sizeof(out)) == HTSN_OK);
    CHECK(strcmp(out, "no differences") == 0);

    q.priority = 7;
    q.traffic_class = 7;
    CHECK(htsn_db_qos_save(&db, &q) == HTSN_OK);
    CHECK(htsn_cfg_ver_rollback(m, 2) == HTSN_OK);
    htsn_qos_config loaded;
    memset(&loaded, 0, sizeof(loaded));
    CHECK(htsn_db_qos_load(&db, "d1", &loaded) == HTSN_OK);
    CHECK(loaded.priority == 5);

    CHECK(htsn_cfg_ver_rollback(m, 999) == HTSN_ERR_NOT_FOUND);

    htsn_cfg_ver_manager_destroy(m);
    htsn_event_bus_destroy(bus);
    htsn_db_close(&db);
    remove("test_cfgver.db");
}

int main(void) {
    test_device();
    test_qos_validation();
    test_vlan();
    test_gcl();
    test_timesync();
    test_sensor_type();
    test_db_roundtrip();
    test_stream_validate();
    test_stream_db_roundtrip();
    test_str_util();
    test_event_bus();
    test_config_version();

    printf("%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
