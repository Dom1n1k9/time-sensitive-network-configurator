/* wtsn-tsn endpoint application (STM32F767ZI) — Zephyr.
 *
 * The RPi (CNC) is the peer: it is the PTP v2 grandmaster (ptpd), TSN
 * controller, OPC UA server, MQTT broker and the GUI. The PC is NOT a runtime
 * node.
 *
 * Zephyr (device model + DT overlay) brings up clocks, the USART6 console,
 * I2C2 and the Ethernet MAC before main() runs. This file is the application
 * layer on top of that: it starts the actuator PWM, the transport, PTP, and
 * the display/heartbeat threads.
 */
#include <zephyr/kernel.h>
#include <zephyr/cmsis.h>

#include "wtsn_config.h"
#include "wtsn_port.h"
#include "wtsn_net.h"
#include "ptp.h"
#include "board.h"
#include "pwm.h"
#include "wtsn_sonar.h"
#include "wtsn_display.h"
#include "wtsn_actuator.h"

#include <string.h>
#include <stdlib.h>

/* Command handler: the RPi CNC sends short verbs over the TSN link. */
static void on_command(const char *cmd, const char *arg, void *ud)
{
	(void)ud;
	if (!cmd) return;
	if (strcmp(cmd, "servo") == 0)       wtsn_sonar_set_angle(atoi(arg));
	else if (strcmp(cmd, "sonar") == 0)  wtsn_sonar_trigger();
	else if (strcmp(cmd, "relay") == 0)  wtsn_actuator_relay(atoi(arg));
	else if (strcmp(cmd, "beep") == 0)   wtsn_actuator_beep(atoi(arg));
	else if (strcmp(cmd, "hud") == 0)    wtsn_display_on_telemetry(arg);
	else if (strcmp(cmd, "reboot") == 0) wtsn_reset();
	else LOGW("unknown command '%s'\n", cmd);
}

static void on_button(int n, void *ud)
{
	(void)ud;
	LOGI("button %d pressed\n", n);
	/* buttons publish their state via wtsn_display_tick(); a press can also
	 * trigger a sonar sweep, matching the esp32-02 behaviour. */
	wtsn_sonar_trigger();
}

/* ---- display + heartbeat threads (started from main after init) ---- */
K_THREAD_STACK_DEFINE(display_stack, 4096);
K_THREAD_STACK_DEFINE(heartbeat_stack, 1024);
static struct k_thread display_thread, heartbeat_thread;

static void display_task_fn(void *a, void *b, void *c)
{
	(void)a; (void)b; (void)c;
	for (;;) {
		wtsn_display_tick();
		k_msleep(100);
	}
}

static void heartbeat_task_fn(void *a, void *b, void *c)
{
	(void)a; (void)b; (void)c;
	for (;;) {
		board_heartbeat_toggle();
		k_msleep(1000);
	}
}

int main(void)
{
	board_dwt_init();
	board_heartbeat_init();
	wtsn_pwm_init();               /* servo + buzzer (CMSIS shim) */

	wtsn_display_set_btn_cb(on_button, NULL);
	wtsn_net_set_cmd_cb(on_command, NULL);
	if (wtsn_net_init() != 0)
		LOGE("net init failed — is the Ethernet link to the RPi CNC up?\n");
	wtsn_ptp_init();

	wtsn_sonar_init(WTSN_NODE_ID);
	wtsn_display_init(WTSN_NODE_ID);   /* blocks ~0.5 s probing the OLED */
	wtsn_actuator_init(WTSN_NODE_ID);

	k_thread_create(&display_thread, display_stack, K_THREAD_STACK_SIZEOF(display_stack),
	                display_task_fn, NULL, NULL, NULL, 4, 0, K_NO_WAIT);
	k_thread_create(&heartbeat_thread, heartbeat_stack, K_THREAD_STACK_SIZEOF(heartbeat_stack),
	                heartbeat_task_fn, NULL, NULL, NULL, 3, 0, K_NO_WAIT);

	LOGI("wtsn-tsn endpoint %s up (CNC = %s)\n", WTSN_NODE_ID, CNC_IP);
	return 0;   /* main thread done; the other threads keep running */
}
