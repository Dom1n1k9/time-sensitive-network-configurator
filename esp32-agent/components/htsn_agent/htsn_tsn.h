#ifndef HTSN_TSN_H
#define HTSN_TSN_H

#include <stdint.h>
#include <stdbool.h>

#define HTSN_GCL_MAX 32
#define HTSN_STREAM_MAX_LISTENERS 8
#define HTSN_STREAM_ID_LEN 48

/* Full configuration snapshot received on tsn/cmd/<id>/apply */
typedef struct {
    int priority;      /* 0-7 */
    int traffic_class;
    int bandwidth_kbps;
    int latency_ms;
    int preemption;    /* 0 = off, 1 = on */
    int vlan_id;      /* 0 = none */
    char group[32];
    int timesync_mode; /* 0 disabled,1 local GM,2 external GM,3 auto */
    char grandmaster[32];
    int64_t tas_cycle_ns;
    int gcl_entries;
    int gates[HTSN_GCL_MAX];        /* per entry gate_state */
    int64_t durations[HTSN_GCL_MAX];
} htsn_config_snapshot;

/* current applied state, kept for status reporting */
typedef struct {
    int priority;
    int traffic_class;
    int preemption;
    int vlan_id;
    int timesync_mode;
    int64_t tas_cycle_ns;
    int gcl_entries;
    int gates[HTSN_GCL_MAX];
    int64_t durations[HTSN_GCL_MAX];
} htsn_tsn_state;

htsn_tsn_state *htsn_tsn_get_state(void);
void htsn_tsn_restore(void);
void htsn_tsn_reset_state(void);

/* apply a full snapshot, return 0 on success (used by /apply handler) */
int htsn_tsn_apply_snapshot(const htsn_config_snapshot *cfg);

int htsn_tsn_apply_qos(int priority, int traffic_class, int bw_kbps, int lat_ms, int preemption);
int htsn_tsn_apply_vlan(int vlan_id, const char *group);
int htsn_tsn_apply_timesync(int mode, const char *gm);
int htsn_tsn_apply_tas(int64_t cycle_ns, const int *gates, const int64_t *durations, int entries);
int htsn_tsn_apply_preemption(int preemption, const char *emac_csv, const char *pmac_csv);

/* 802.1Qcc stream reservation received over FXMQTT (tsn/fx/...) */
typedef struct {
    char stream_id[HTSN_STREAM_ID_LEN];
    char name[64];
    char talker[64];
    char listeners[HTSN_STREAM_MAX_LISTENERS][64];
    int listener_count;
    int vlan_id;
    int64_t max_latency_ns;
    int64_t max_interval_ns;
    int priority;
    int data_frame_prio;
} htsn_stream;
extern htsn_stream *htsn_tsn_streams;
extern int htsn_tsn_stream_count;

int htsn_tsn_apply_stream(const htsn_stream *s);
htsn_stream *htsn_tsn_find_stream(const char *id);

#endif
