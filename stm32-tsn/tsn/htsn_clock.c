/* htsn-tsn PTP time base — DWT cycle counter as the nanosecond reference.
 *
 * The DWT CYCCNT (32-bit) free-runs at SystemCoreClock. We extend it to 64
 * bits (tracking wraps) and add a 64-bit base offset so the clock can be
 * aligned once to the grandmaster's absolute PTP (TAI/epoch) time and then
 * fine-steered. htsn_clock_apply_offset() adjusts the base (not the raw DWT),
 * so an epoch-sized first correction is applied cleanly instead of overflowing
 * the 32-bit CYCCNT.
 */
#include "htsn_clock.h"

#include "stm32f7xx.h"

static uint32_t g_last_cyc;
static uint64_t g_wrap_ns;
static int64_t  g_base_ns;

uint64_t htsn_ptp_time_ns(void)
{
	uint32_t c = DWT->CYCCNT;
	uint64_t t;

	if (c < g_last_cyc) {
		/* CYCCNT wrapped (32-bit) — extend to 64 bits. */
		g_wrap_ns += (((uint64_t)1 << 32) * 1000000000ULL) /
		             (uint64_t)SystemCoreClock;
	}
	g_last_cyc = c;

	t = g_wrap_ns + ((uint64_t)c * 1000000000ULL) / (uint64_t)SystemCoreClock;
	return (uint64_t)((int64_t)t + g_base_ns);
}

void htsn_clock_apply_offset(int64_t offset_ns)
{
	/* Step the absolute base. Handles both epoch-sized alignment (the GM's
	 * absolute PTP time vs our boot-relative counter) and fine corrections. */
	g_base_ns += offset_ns;
}

int64_t htsn_clock_base_ns(void)
{
	return g_base_ns;
}
