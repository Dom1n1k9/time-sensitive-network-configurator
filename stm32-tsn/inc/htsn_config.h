/* htsn-tsn node configuration: identity, network endpoints, timings. */
#ifndef HTSN_CONFIG_H
#define HTSN_CONFIG_H

/* Node identity (shown in the GUI and in every telemetry frame). */
#ifndef HTSN_NODE_ID
#define HTSN_NODE_ID "stm32-tsn-01"
#endif

/* Default MAC (the two low bits can be set by PD2/PG2-to-GND bridges for
 * multiple endpoints on one network, mirroring the Sienda reference design). */
#define HTSN_MAC "70:B3:D5:A8:10:2C"

/* Deterministic UDP protocol endpoints (the RPi/CNC is the peer).
 * The STM32 listens on CMD_PORT for commands; it sends telemetry to the CNC's
 * TELEM_ADDR:TELEM_PORT. All control/telemetry frames are priority-tagged. */
#define CNC_IP           "192.168.1.10"   /* RPi CNC TSN NIC (edit to your net) */
#define CNC_TELEM_PORT   4001             /* where the STM32 sends telemetry */
#define STM32_CMD_PORT   4000             /* where the STM32 receives commands */

/* 802.1Q priority for TSN control traffic (6 = highest, VO/AC_VO). */
#define TSN_PCP          6u
#define TSN_VID          0u

/* Servo timing (SG90). */
#define SERVO_FREQ_HZ    50u
#define SERVO_MIN_US     1000u            /* 0 deg */
#define SERVO_MAX_US     2000u            /* 180 deg */

/* Sonar sweep. */
#define SONAR_ECHO_TIMEOUT_US 40000L
#define SONAR_SWEEP_ANGLES    180         /* one step per degree */
#define SONAR_STEP_MS         35

/* Display refresh. */
#define DISPLAY_REFRESH_MS    10000       /* 10 s info-panel cadence */
#define DISPLAY_PUB_MS        2000        /* button-state publish cadence */

#endif /* HTSN_CONFIG_H */
