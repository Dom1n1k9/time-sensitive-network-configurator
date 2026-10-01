/* App-level board helpers for the Zephyr port.
 *
 * Owns the high-resolution DWT clock, full reset, and the heartbeat LED.
 * Zephyr (via the device model + the DT overlay) handles the heavy lifting the
 * CubeMX/HAL base used to do: clocks, UART console, I2C, Ethernet. printf
 * (LOGI/LOGW/LOGE) routes to the zephyr,console (USART6) automatically, so the
 * old raw USART6 _write() retarget is gone.
 */
#include "board.h"
#include "wtsn_port.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/cmsis.h>              /* DWT, CoreDebug, SystemCoreClock, NVIC_SystemReset */
#include <zephyr/drivers/gpio.h>

#define WTSN_ACT DT_NODELABEL(wtsn_actuators)
static const struct gpio_dt_spec led_hb = GPIO_DT_SPEC_GET(WTSN_ACT, led_hb_gpios);

void board_dwt_init(void)
{
	CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;  /* enable the trace unit */
	DWT->CYCCNT = 0;
	DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;            /* start the cycle counter */
}

uint64_t wtsn_now_us(void)
{
	return (uint64_t)((DWT->CYCCNT * 1000000ULL) / SystemCoreClock);
}

void wtsn_delay_us(uint32_t us)
{
	uint32_t cycles = (uint32_t)((uint64_t)us * SystemCoreClock / 1000000u);
	uint32_t start = DWT->CYCCNT;
	while ((uint32_t)(DWT->CYCCNT - start) < cycles) { /* busy-wait */ }
}

void wtsn_reset(void)
{
	NVIC_SystemReset();
}

void board_heartbeat_init(void)
{
	if (device_is_ready(led_hb.port)) {
		gpio_pin_configure_dt(&led_hb, GPIO_OUTPUT_INACTIVE);
	}
	gpio_pin_set_dt(&led_hb, 0);
}

void board_heartbeat_toggle(void)
{
	static int on = 0;
	on = !on;
	gpio_pin_set_dt(&led_hb, on);
}
