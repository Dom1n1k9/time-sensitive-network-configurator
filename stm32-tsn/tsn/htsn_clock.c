/* htsn-tsn PTP time base — DWT cycle counter as the nanosecond reference.
 *
 * The DWT CYCCNT (32-bit) free-runs at SystemCoreClock. We convert it to
 * nanoseconds for reads and convert an ns offset back to cycles for the
 * coarse steer. CYCCNT is enabled by board_dwt_init() before PTP starts.
 */
#include "htsn_clock.h"

#include "stm32f7xx.h"

uint64_t htsn_ptp_time_ns(void)
{
	return (uint64_t)(((uint64_t)DWT->CYCCNT * 1000000000ULL) /
	                  (uint64_t)SystemCoreClock);
}

void htsn_clock_apply_offset(int64_t offset_ns)
{
	/* cycles = offset_ns * SystemCoreClock / 1e9, computed in two parts so the
	 * multiply cannot overflow int64_t even for a whole-epoch-sized offset. */
	int64_t whole_s = offset_ns / 1000000000LL;
	int64_t rem_ns  = offset_ns % 1000000000LL;
	int64_t cycles  = whole_s * (int64_t)SystemCoreClock
	                 + (rem_ns * (int64_t)SystemCoreClock) / 1000000000LL;
	DWT->CYCCNT += (uint32_t)cycles;
}
