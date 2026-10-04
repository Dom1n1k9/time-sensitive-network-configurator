#ifndef HTSN_PTP_H
#define HTSN_PTP_H

#include <stdbool.h>
#include "htsn_mqtt.h"

/* Timing report published on tsn/ptp by the agent. */
typedef struct {
    int64_t   offset_ns;      /* signed offset of local clock vs master */
    int64_t   jitter_ns;      /* pk-pk variation of recent offsets */
    int       state;           /* 0 sync, 1 holdover, 2 unsync */
    int       mode;           /* 0 disabled,1 local,2 external,3 auto */
    char      grandmaster[32];
    char      grandmaster_id[64]; /* EUI-64 of grandmaster */
    char      clock_identity[64]; /* EUI-64 of this node */
} htsn_ptp_report;

/* Bind: remember device id + MQTT handle used later for publishing. */
int  htsn_ptp_setup(const char *device_id, htsn_mqtt *mq);

/* (re)configure the PTP clock and start the periodic reporter. */
void htsn_ptp_apply(int mode, const char *grandmaster);

/* get latest computed report (mainly for testing). */
htsn_ptp_report *htsn_ptp_get_report(void);

/* start background task (call from app_main after successful MQTT connect). */
int htsn_ptp_start(void);

#endif
