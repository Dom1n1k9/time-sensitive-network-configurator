/* htsn-tsn telemetry/command transport.
 *
 * Replaces the ESP32 agent's MQTT publish with a deterministic UDP protocol
 * over the wired TSN Ethernet link to the RPi (CNC). Actuators call the
 * publish_* functions; incoming commands are delivered to a registered
 * callback. The actual socket + priority tagging + gPTP live in eth_proto.c. */
#ifndef HTSN_NET_H
#define HTSN_NET_H

#include <stdint.h>

/* Bring up the transport (socket on STM32_CMD_PORT, destination = CNC). */
int  htsn_net_init(void);

/* Publish an actuator frame to the CNC (priority-tagged). */
void htsn_net_publish_sweep(int32_t id, const int16_t *sweep, int n);
void htsn_net_publish_buttons(int k1, int k2, int k3, int k4);

/* Generic state frame: `kind` is a short tag ("actuator", "sonar", "buttons"),
 * `payload` a JSON object. The CNC re-publishes it to MQTT/OPC UA for the GUI. */
void htsn_net_publish_telemetry(const char *kind, const char *payload);

/* Command delivery. The app registers one handler; `cmd` is a short verb
 * (e.g. "servo", "sonar", "relay", "reboot") and `arg` its payload string. */
typedef void (*htsn_cmd_cb)(const char *cmd, const char *arg, void *ud);
void htsn_net_set_cmd_cb(htsn_cmd_cb cb, void *ud);

#endif /* HTSN_NET_H */
