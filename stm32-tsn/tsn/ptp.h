/* wtsn-tsn PTP v2 (IEEE 1588) slave time base. */
#ifndef WTSN_PTP_H
#define WTSN_PTP_H

#include <stdint.h>
#include <stdbool.h>

/* Bring up the PTP slave task (locks to the RPi/CNC ptpd grandmaster). */
int      wtsn_ptp_init(void);
bool     wtsn_ptp_is_locked(void);
int64_t  wtsn_ptp_offset_ns(void);    /* last applied correction (ns) */
uint64_t wtsn_ptp_time_ns(void);      /* network-referenced ns time base */

#endif /* WTSN_PTP_H */
