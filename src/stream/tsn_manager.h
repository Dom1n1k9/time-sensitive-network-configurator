#ifndef HTSN_TSN_MANAGER_H
#define HTSN_TSN_MANAGER_H

#include "db/db.h"
#include "mqtt/mqtt_client.h"
#include "mvc/event_bus.h"
#include "stream/stream.h"

typedef struct htsn_tsn_manager htsn_tsn_manager;

/* fully configured 802.1Qcc CNC manager over the shared managers */
typedef struct {
    htsn_db *db;
    htsn_event_bus *bus;
    htsn_mqtt_client *mqtt;
} htsn_tsn_manager_config;

htsn_tsn_manager *htsn_tsn_manager_create(const htsn_tsn_manager_config *cfg);
void htsn_tsn_manager_destroy(htsn_tsn_manager *m);

/* set/replace the MQTT broker channel used for deploys (may be NULL) */
void htsn_tsn_manager_set_mqtt(htsn_tsn_manager *m, htsn_mqtt_client *mqtt);

htsn_error htsn_tsn_manager_add(htsn_tsn_manager *m, const htsn_stream *s);
htsn_error htsn_tsn_manager_remove(htsn_tsn_manager *m, const char *stream_id);
htsn_error htsn_tsn_manager_load(htsn_tsn_manager *m, const char *stream_id, htsn_stream *out);
void htsn_tsn_manager_for_each(htsn_tsn_manager *m, int (*cb)(const htsn_stream *s, void *ud), void *ud);
size_t htsn_tsn_manager_count(htsn_tsn_manager *m);

/* 802.1Qcc: compute the path and push talker + listeners to their agents via MQTT */
htsn_error htsn_tsn_manager_deploy(htsn_tsn_manager *m, const char *stream_id);
/* Deploy every configured stream. When `domain` is non-NULL and non-empty,
 * only streams whose endpoints all belong to that TSN domain are deployed
 * (physical 802.11 cell); NULL/empty = all domains (legacy global deploy). */
htsn_error htsn_tsn_manager_deploy_all(htsn_tsn_manager *m, const char *domain);

#endif
