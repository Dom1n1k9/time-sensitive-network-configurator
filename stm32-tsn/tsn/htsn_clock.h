/* htsn-tsn PTP time base.
 *
 * A network-referenced nanosecond clock the gPTP slave reads for T3/T4 and
 * stamps into the Delay_Req originTimestamp. It free-runs off the DWT cycle
 * counter and is coarsely steered toward the grandmaster by
 * htsn_clock_apply_offset(). (A production slave would also adjust the rate;
 * this demo only needs a free-running, steerable base.)
 */
#ifndef HTSN_CLOCK_H
#define HTSN_CLOCK_H

#include <stdint.h>

/* Current PTP time base, nanoseconds (monotonic, wraps with the DWT counter). */
uint64_t htsn_ptp_time_ns(void);

/* Coarsely step the time base by `offset_ns` (positive = faster/advance). */
void     htsn_clock_apply_offset(int64_t offset_ns);

#endif /* HTSN_CLOCK_H */
