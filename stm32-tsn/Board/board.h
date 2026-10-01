/* App-level board helpers (Zephyr port). The heavy peripheral init — clocks,
 * UART console, I2C, Ethernet — comes from the Zephyr device model + the DT
 * overlay. This file owns the high-resolution DWT clock + reset + heartbeat. */
#ifndef WTSN_BOARD_H
#define WTSN_BOARD_H

#include <stdint.h>

/* Enable the DWT cycle counter (call once, after the core is up). */
void board_dwt_init(void);

/* Start the on-board heartbeat LED (toggle in a low-pri thread). */
void board_heartbeat_init(void);
void board_heartbeat_toggle(void);

#endif /* WTSN_BOARD_H */
