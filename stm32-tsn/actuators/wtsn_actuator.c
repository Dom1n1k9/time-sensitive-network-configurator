/* Relay (digital) + buzzer (PWM) endpoint actuators, ported from esp32-02
 * (wtsn_actuator.c). Driven by the TSN transport (wtsn_net) instead of MQTT.
 *
 * Zephyr: the relay is a DT GPIO (polarity in the overlay); the buzzer PWM is
 * the CMSIS shim in Board/pwm.c.
 */
#include "wtsn_actuator.h"

#include "wtsn_config.h"
#include "wtsn_port.h"
#include "wtsn_net.h"
#include "pwm.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

#include <stdio.h>
#include <string.h>

#define WTSN_ACT DT_NODELABEL(wtsn_actuators)
static const struct gpio_dt_spec relay = GPIO_DT_SPEC_GET(WTSN_ACT, relay_gpios);

static bool   g_relay_on = false;
static int    g_buzz_hz = 0;
static uint8_t g_buzz_pct = 0;
static int    g_buzz_ms = 0;

static void relay_write(bool on)
{
	g_relay_on = on;
	/* Polarity (active high) is carried by the DT GPIO spec in the overlay. */
	gpio_pin_set_dt(&relay, on ? 1 : 0);
}

/* Set the buzzer PWM duty (0..100) via the CMSIS shim. */
static void buzzer_set_duty(uint8_t pct) { g_buzz_pct = pct; wtsn_pwm_buzzer_duty(pct); }
static void buzzer_off(void) { g_buzz_pct = 0; wtsn_pwm_buzzer_duty(0); }

/* Blocking beep: on for ms then off. (Called from a low-priority context.) */
static void buzzer_beep(int hz, int ms)
{
	g_buzz_hz = hz;
	g_buzz_ms = ms;
	if (hz <= 0 || ms <= 0) { buzzer_off(); return; }
	wtsn_pwm_buzzer_duty(50);
	for (int waited = 0; waited < ms; waited++) { k_msleep(1); }
	wtsn_pwm_buzzer_duty(0);
}

void wtsn_actuator_init(const char *device_id)
{
	(void)device_id;   /* identity carried by wtsn_net */
	if (device_is_ready(relay.port)) {
		gpio_pin_configure_dt(&relay, GPIO_OUTPUT_INACTIVE);
	}
	relay_write(false);
	buzzer_off();
	LOGI("relay ready: PB0 (DT GPIO)  buzzer ready: TIM3_CH4 (CMSIS PWM)\n");
}

void wtsn_actuator_relay(int on)
{
	int state = (on != 0) ? 1 : 0;
	if (state == (int)g_relay_on) return;
	relay_write(state != 0);
	char payload[64];
	snprintf(payload, sizeof(payload), "{\"device\":\"%s\",\"on\":%d}",
	         WTSN_NODE_ID, state);
	wtsn_net_publish_telemetry("actuator", payload);
	LOGI("relay %s\n", state ? "ON" : "OFF");
}

void wtsn_actuator_beep(int ms)
{
	const int hz = 1000;
	if (ms <= 0) { buzzer_off(); return; }
	buzzer_beep(hz, ms);
	char payload[64];
	snprintf(payload, sizeof(payload), "{\"device\":\"%s\",\"hz\":%d,\"ms\":%d}",
	         WTSN_NODE_ID, hz, ms);
	wtsn_net_publish_telemetry("actuator", payload);
	LOGI("beep %d Hz for %d ms\n", hz, ms);
}

int wtsn_actuator_relay_state(void) { return g_relay_on ? 1 : 0; }
