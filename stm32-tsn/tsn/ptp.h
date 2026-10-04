/* htsn-tsn PTP v2 (IEEE 1588) slave time base. */
#ifndef HTSN_PTP_H
#define HTSN_PTP_H

#include <stdint.h>
#include <stdbool.h>

/* Bring up the PTP slave task (locks to the RPi/CNC ptpd grandmaster). */
int      htsn_ptp_init(void);
bool     htsn_ptp_is_locked(void);
int64_t  htsn_ptp_offset_ns(void);    /* last applied correction (ns) */
uint64_t htsn_ptp_time_ns(void);      /* network-referenced ns time base */

#endif /* HTSN_PTP_H */
