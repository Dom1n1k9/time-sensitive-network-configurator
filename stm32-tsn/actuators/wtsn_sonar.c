/* HC-SR04 panning sonar + SG90 servo, ported from esp32-02 (wtsn_sonar.c).
 * Transport is the TSN UDP link (wtsn_net). Zephyr: TRIG/ECHO are DT GPIOs,
 * the servo is the CMSIS PWM shim, the sweep runs in its own thread.
 */
#include "wtsn_sonar.h"

#include "wtsn_config.h"
#include "wtsn_port.h"
#include "wtsn_net.h"
#include "pwm.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

#include <string.h>

#define WTSN_ACT DT_NODELABEL(wtsn_actuators)
static const struct gpio_dt_spec trig = GPIO_DT_SPEC_GET(WTSN_ACT, sonar_trig_gpios);
static const struct gpio_dt_spec echo = GPIO_DT_SPEC_GET(WTSN_ACT, sonar_echo_gpios);

static int16_t g_sweep[SONAR_SWEEP_ANGLES];
static int     g_sweep_len = 0;
static int32_t g_sweep_id = 0;
static bool    g_sweep_ok = false;

static volatile bool g_trigger = false;
static volatile bool g_sweeping = false;
static volatile int  g_cur_angle = -1;

/* ---- servo (SG90, 50 Hz PWM) ---- */
static void servo_set_us(uint16_t us) { wtsn_pwm_servo_us(us); }

static void servo_set_angle(int deg)
{
	if (deg < 0) deg = 0;
	if (deg > 180) deg = 180;
	uint32_t us = SERVO_MIN_US + (deg * (SERVO_MAX_US - SERVO_MIN_US)) / 180u;
	servo_set_us((uint16_t)us);
}

/* ---- HC-SR04 read (blocking echo) ---- */
static int sonar_read_cm(void)
{
	gpio_pin_set_dt(&trig, 0);
	wtsn_delay_us(5);
	gpio_pin_set_dt(&trig, 1);
	wtsn_delay_us(10);
	gpio_pin_set_dt(&trig, 0);

	uint64_t t0 = wtsn_now_us();
	while (gpio_pin_get_dt(&echo) == 0) {
		if (wtsn_now_us() - t0 > (uint64_t)SONAR_ECHO_TIMEOUT_US) return -1;
	}
	uint64_t rise = wtsn_now_us();
	while (gpio_pin_get_dt(&echo) == 1) {
		if (wtsn_now_us() - t0 > (uint64_t)SONAR_ECHO_TIMEOUT_US) { rise = 0; break; }
	}
	if (rise == 0) return -1;
	uint64_t fall = wtsn_now_us();
	float cm = (float)(fall - rise) / 58.0f;
	if (cm < 2.0f || cm > 400.0f) return -1;
	return (int)cm;
}

/* ---- sweep thread ---- */
K_THREAD_STACK_DEFINE(sonar_stack, 4096);
static struct k_thread sonar_thread;

static void sonar_task_fn(void *a, void *b, void *c)
{
	(void)a; (void)b; (void)c;
	k_msleep(500);   /* let the servo settle after init */

	LOGI("servo self-test: sweeping 0..180..0\n");
	for (int d = 0; d <= 180; d += 15) { servo_set_angle(d); k_msleep(120); }
	for (int d = 180; d >= 0; d -= 15) { servo_set_angle(d); k_msleep(120); }
	servo_set_angle(90);
	k_msleep(300);
	LOGI("servo self-test done (parked at 90)\n");

	for (;;) {
		k_msleep(20);
		if (!g_trigger) continue;
		g_trigger = false;
		if (g_sweeping) continue;
		g_sweeping = true;
		LOGI("sonar sweep start (motion)\n");

		for (int a = 0; a < SONAR_SWEEP_ANGLES; a++) {
			g_cur_angle = a;
			servo_set_angle(a);
			k_msleep(SONAR_STEP_MS);   /* servo move + echo settle */
			int cm = sonar_read_cm();
			g_sweep[a] = (int16_t)(cm < 0 ? -1 : cm);
		}
		servo_set_angle(90);           /* return to park position */
		g_sweep_len = SONAR_SWEEP_ANGLES;
		g_sweep_id++;
		g_sweep_ok = true;
		g_cur_angle = -1;
		g_sweeping = false;
		wtsn_net_publish_sweep(g_sweep_id, g_sweep, g_sweep_len);
		LOGI("sonar sweep done (%d angles)\n", g_sweep_len);
	}
}

/* ---- public API ---- */
void wtsn_sonar_init(const char *device_id)
{
	(void)device_id;   /* identity is carried by the transport (wtsn_net) */
	for (int i = 0; i < SONAR_SWEEP_ANGLES; i++) g_sweep[i] = -1;
	if (device_is_ready(trig.port)) {
		gpio_pin_configure_dt(&trig, GPIO_OUTPUT_INACTIVE);
	}
	if (device_is_ready(echo.port)) {
		gpio_pin_configure_dt(&echo, GPIO_INPUT);
	}
	servo_set_angle(0);
	k_thread_create(&sonar_thread, sonar_stack, K_THREAD_STACK_SIZEOF(sonar_stack),
	                sonar_task_fn, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
	LOGI("sonar ready: TRIG=PB3 ECHO=PB4 SERVO=PA8 (SG90, 50 Hz)\n");
}

void wtsn_sonar_trigger(void) { g_trigger = true; }

const int16_t *wtsn_sonar_map(int *n)
{
	if (n) *n = g_sweep_len;
	return g_sweep_ok ? g_sweep : NULL;
}

void wtsn_sonar_set_angle(int deg)
{
	g_cur_angle = deg;
	servo_set_angle(deg);
	LOGI("servo manual -> %d deg\n", deg);
}

int wtsn_sonar_active_angle(void) { return g_cur_angle; }
