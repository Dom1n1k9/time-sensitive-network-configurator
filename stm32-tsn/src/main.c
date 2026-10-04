/* htsn-tsn endpoint application (STM32F767ZI) — Zephyr.
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

#include "htsn_config.h"
#include "htsn_port.h"
#include "htsn_net.h"
#include "ptp.h"
#include "board.h"
#include "pwm.h"
#include "htsn_sonar.h"
#include "htsn_display.h"
#include "htsn_actuator.h"

#include <string.h>
#include <stdlib.h>

/* Command handler: the RPi CNC sends short verbs over the TSN link. */
static void on_command(const char *cmd, const char *arg, void *ud)
{
	(void)ud;
	if (!cmd) return;
	if (strcmp(cmd, "servo") == 0)       htsn_sonar_set_angle(atoi(arg));
	else if (strcmp(cmd, "sonar") == 0)  htsn_sonar_trigger();
	else if (strcmp(cmd, "relay") == 0)  htsn_actuator_relay(atoi(arg));
	else if (strcmp(cmd, "beep") == 0)   htsn_actuator_beep(atoi(arg));
	else if (strcmp(cmd, "hud") == 0)    htsn_display_on_telemetry(arg);
	else if (strcmp(cmd, "reboot") == 0) htsn_reset();
	else LOGW("unknown command '%s'\n", cmd);
}

static void on_button(int n, void *ud)
{
	(void)ud;
	LOGI("button %d pressed\n", n);
	/* buttons publish their state via htsn_display_tick(); a press can also
	 * trigger a sonar sweep, matching the esp32-02 behaviour. */
	htsn_sonar_trigger();
}

/* ---- display + heartbeat threads (started from main after init) ---- */
K_THREAD_STACK_DEFINE(display_stack, 4096);
K_THREAD_STACK_DEFINE(heartbeat_stack, 1024);
static struct k_thread display_thread, heartbeat_thread;

static void display_task_fn(void *a, void *b, void *c)
{
	(void)a; (void)b; (void)c;
	for (;;) {
		htsn_display_tick();
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
	htsn_pwm_init();               /* servo + buzzer (CMSIS shim) */

	htsn_display_set_btn_cb(on_button, NULL);
	htsn_net_set_cmd_cb(on_command, NULL);
	if (htsn_net_init() != 0)
		LOGE("net init failed — is the Ethernet link to the RPi CNC up?\n");
	htsn_ptp_init();

	htsn_sonar_init(HTSN_NODE_ID);
	htsn_display_init(HTSN_NODE_ID);   /* blocks ~0.5 s probing the OLED */
	htsn_actuator_init(HTSN_NODE_ID);

	k_thread_create(&display_thread, display_stack, K_THREAD_STACK_SIZEOF(display_stack),
	                display_task_fn, NULL, NULL, NULL, 4, 0, K_NO_WAIT);
	k_thread_create(&heartbeat_thread, heartbeat_stack, K_THREAD_STACK_SIZEOF(heartbeat_stack),
	                heartbeat_task_fn, NULL, NULL, NULL, 3, 0, K_NO_WAIT);

	LOGI("htsn-tsn endpoint %s up (CNC = %s)\n", HTSN_NODE_ID, CNC_IP);
	return 0;   /* main thread done; the other threads keep running */
}
