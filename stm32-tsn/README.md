# wtsn-tsn — STM32F767ZI TSN actuator endpoint (Zephyr)

A self-contained **real-TSN actuator node** that runs on the **STM32 Nucleo-
F767ZI** (on-board 10/100 Ethernet, LAN8742A PHY). It owns all the actuators that
were on the `esp32-02` actor board (panning sonar + servo, relay, SSD1306 OLED,
4 buttons, buzzer) and talks to the **RPi, which is the CNC**, over a wired
TSN Ethernet link.

The firmware is a **Zephyr** application (RTOS + device model + net stack). The
previous FreeRTOS + STM32-CubeMX + lwIP build has been removed; the same app
sources now use Zephyr kernels/threads, the device model (GPIO/I2C/ETH), Zephyr
POSIX sockets, and CMSIS for the DWT clock + a small PWM shim.

The Sienda TSN *library* is commercial and not bundled here. Instead this project
implements the TSN functionality the F767 MAC can do directly (see the feature
matrix below), wrapped in a clean `tsn/` abstraction so the Sienda library can be
swapped in later if you license it.

## Roles

| Node | Role |
|------|------|
| **RPi** | **CNC** — TSN controller, **gPTP grandmaster** (linuxptp), OPC UA server, MQTT broker, Web GUI. Runs a **PREEMPT_RT** kernel for tight PTP jitter. Sends actuator commands, receives telemetry. |
| **STM32 F767ZI** | **Endpoint** — gPTP **slave** (syncs to the RPi clock), owns all actuators, commands in / telemetry out over the TSN link. |
| **ESP32 nodes** | **Wireless** — MQTT over Wi-Fi (the "wireless scheme"); unchanged. |

Result: a **hybrid / heterogeneous TSN network** — wired (Ethernet + TSN + OPC UA
to the STM32) and wireless (MQTT to the ESP32s), both surfaced in one GUI.

## TSN feature matrix on the F767ZI

| Feature | Standard | Status on this board |
|---------|----------|----------------------|
| Time sync (gPTP / IEEE 1588) | 802.1AS / 1588 | **Implemented** — PTP **v2 slave over UDP** (1588 event/general ports) locking to the RPi `ptpd` grandmaster; local DWT software clock, ~us accuracy. *Upgrade path:* 802.1AS **link-layer** gPTP (0x88F7) + the F7 MAC **hardware** timestamp for sub-us. |
| Priority / VLAN tagging | 802.1Q (PCP/VID) | **Best-effort** — control + telemetry are the only traffic on a point-to-point link, so priority is effectively guaranteed by contention-free medium. 802.1Q **PCP** tagging through the Zephyr net-if is a documented TODO. |
| Time-aware shaping | 802.1Qbv | **Best-effort** — the F7 10/100 MAC has no HW Qbv offload. On a point-to-point link there is no shared-medium contention, so HW Qbv adds little; cadence is driven by the PTP clock instead. |
| Frame preemption | 802.1CB | **Not supported** — the F7 10/100 MAC has no HW frame-preemption engine. |
| Stream reservation | 802.1Qci / SRP | **Out of scope** for a point-to-point link (nothing to contend with); the RPi/CNC configures the link directly. |
| AVTP audio streaming | 802.1BA | **Not applicable** — this is an actuator node, not a Milan audio endpoint. |

"Real TSN" here = **PTP-v2 time-synced, deterministic Ethernet control** — exactly
what an actuator endpoint needs on a wired CNC link. The MAC hardware timestamp +
link-layer gPTP is the documented upgrade once it's validated on-target (the
`ptp.c` HW glue is isolated for that swap).

## Pin map (single source of truth: `boards/nucleo_f767zi.overlay`)

The Nucleo-F767ZI on-board Ethernet (LAN8742A) occupies PA1/PA2/PA7/PC1/PC4/PC5 —
those are avoided. The wiring lives in the `wtsn-actuators` node of the DT overlay
(the C reads pins via `GPIO_DT_SPEC_GET`); the servo/buzzer PWM pins are programmed
in `Board/pwm.c`. **Edit the overlay (and `Board/pwm.c` for the two PWM pins) to
match your actual wiring.** `inc/pins.h` is a human-readable index only.

| Signal | Pin | Periph / note |
|--------|-----|---------------|
| Servo PWM (SG90, 50 Hz) | PA8 | TIM1_CH1 (Board/pwm.c, AF1) |
| Sonar TRIG (out) | PB3 | GPIO (overlay) |
| Sonar ECHO (in) | PB4 | GPIO (overlay) |
| Buzzer PWM | PB1 | TIM3_CH4 (Board/pwm.c, AF2) |
| Relay (out) | PB0 | GPIO (overlay) |
| OLED I2C SCL | PB10 | I2C2_SCL (100 kHz) |
| OLED I2C SDA | PB11 | I2C2_SDA |
| Button K1 (in, active-low) | PC13 | GPIO + pull-up (overlay) |
| Button K2 (in, active-low) | PC14 | GPIO + pull-up (overlay) |
| Button K3 (in, active-low) | PC15 | GPIO + pull-up (overlay) |
| Button K4 (in, active-low) | PD0 | GPIO + pull-up (overlay) |
| Heartbeat LED (on-board LD1) | PC8 | GPIO (overlay) |

## Directory layout

```
stm32-tsn/
  CMakeLists.txt        Zephyr app CMake (find_package(Zephyr))
  prj.conf              Kconfig: console, net+sockets, ETH, PWM, I2C, GPIO
  boards/
    nucleo_f767zi.overlay  console + I2C2 + ETH + actuator pin map (DT)
  inc/
    pins.h              pin-map index (human-readable; the overlay is the source of truth)
    wtsn_config.h       node id, endpoints, timings
    wtsn_port.h         DWT clock / reset / logging macros
  Board/
    board.c/.h          DWT high-res clock, reset, heartbeat LED
    pwm.c/.h            CMSIS PWM shim: servo (TIM1) + buzzer (TIM3) + AF mux
  tsn/
    ptp.c/.h            PTP v2 (IEEE 1588) slave over Zephyr sockets -> time base
    wtsn_net.h          transport interface (publish_* + command callback)
    eth_proto.c         wtsn_net impl: Zephyr UDP sockets, frame codec, cmd thread
    wtsn_frame.h        shared wire format (CRC16, kinds)
  actuators/
    wtsn_sonar.c/.h     HC-SR04 + servo (DT GPIO + PWM shim)
    wtsn_display.c/.h   SSD1306 OLED (Zephyr I2C) + buttons (DT GPIO)
    wtsn_actuator.c/.h  relay (DT GPIO) + buzzer (PWM shim)
  src/
    main.c              Zephyr app: init board, start threads, command dispatch
  README.md             this file
```

## Building & flashing (Zephyr)

You need the **Zephyr SDK** (`west` + `zephyr-sdk`) on the machine that has the
board (this dev box has none). One-time setup, then build + flash:

```sh
# one-time (on a machine without the SDK)
git clone https://github.com/zephyrproject-rtos/sdk-ng.git && cd sdk-ng
./setup.sh -r                 # install west + zephyr-sdk into ./westenv
source ./westenv/zephyr-env.sh

cd /path/to/wtsn-configurator/stm32-tsn
west build -b nucleo_f767zi .
west flash -r                 # on-board ST-Link (or: west flash --runner openocd)
```

Console + logs are on the **Nucleo ST-Link VCP** (USART6, 115200). Static IP is
`192.168.1.20` (see `prj.conf`); the RPi/CNC is `192.168.1.10` — edit both to your
wired network before flashing.

### First build — version-fragile bits (fix here if `west build` complains)

These are the only parts that depend on your exact Zephyr version. Everything else
is standard Zephyr API.

- **Ethernet driver symbol** — `prj.conf` sets `CONFIG_STM32_ETH=y`. If your Zephyr
  names the STM32 ETH driver differently (e.g. the driver is auto-selected from the
  `&ethernet` node and the symbol was renamed), drop/adjust that line and keep the
  `&ethernet { status = "okay"; }` in the overlay.
- **LAN8742A PHY** — the on-board PHY is normally declared in the board DTS and
  auto-detected. If Zephyr has no LAN8742A PHY driver for your version and the
  link doesn't come up, enable the matching `CONFIG_ETH_PHY_*` (or add the PHY
  node). This is the single most likely on-target surprise.
- **PWM clocks** — `Board/pwm.c` assumes `SERVO_TIM_CLK=216 MHz` (TIM1/APB2) and
  `BUZZER_TIM_CLK=108 MHz` (TIM3/APB1). If your clock tree differs, set those two
  `#define`s; the servo just runs a few % off in speed (sonar accuracy is unaffected
  — the echo timing uses the DWT clock, not the PWM).
- **Static IP** — `CONFIG_NET_CONFIG_IPV4_ADDR` / `CONFIG_NET_CONFIG_PEER_IPV4_ADDR`
  in `prj.conf`; `CNC_IP` in `inc/wtsn_config.h` (the UDP destination + OPC UA host).
- **CMSIS peripheral registers** — `Board/pwm.c` + `Board/board.c` use the CMSIS
  core (`DWT`, `CoreDebug`, `NVIC_SystemReset`) and peripheral (`RCC`, `TIM1`, `TIM3`,
  `GPIOA/B`) register maps via `<zephyr/cmsis.h>`. Zephyr normally exposes these for
  STM32 SoCs through the SoC header; if the build reports `RCC`/`TIM1`/`GPIOA`
  undeclared, the CMSIS *device* header isn't on the include path — pull it in
  (`#include <stm32f7xx.h>` after the SoC defines) in `Board/pwm.c`.

## RPi (CNC) side

The RPi peer lives in the main repo (`rpi-tsn/`). It is the CNC and runs:

| Process | Role |
|---|---|
| `ptpd` (linuxptp) | PTP v2 **grandmaster** on the wired NIC (the STM32 slave in `tsn/ptp.c` locks to it). Runs on a **PREEMPT_RT** kernel — see `deploy/enable_preempt_rt.sh`. |
| `cnc_opcua` | open62541 **server** on `opc.tcp://:4840` — hosts the telemetry + `cmd_*` nodes (`rpi-tsn/wtsn_opcua_ids.h`) |
| `tsn_opcua_link` | a single persistent OPC UA **client** that reads telemetry every ~1 s and writes a JSON file (`/tmp/wtsn_tsn_opcua.json`) the GUI reads |
| Web GUI | real-mode only; reads the JSON file (no Python OPC UA dep), sends commands via `wtsn_opcua_cli` |
| MQTT broker | used **only** for the wireless ESP32 side — the STM32 never touches MQTT |

The wired data plane is **OPC UA only**; the wireless side is **MQTT only**.

## PREEMPT_RT on the RPi

For the RPi to be a low-jitter PTP grandmaster it should run a **PREEMPT_RT**
kernel. Provision it (idempotent; safe to re-run) from the repo root:

```sh
./deploy/enable_preempt_rt.sh            # installs rpi-rt-kernel, sets it as default,
                                         # applies PTP tuning; prints verify + rollback
```

See `deploy/enable_preempt_rt.sh` for exactly what it does and how to roll back.

## STM32 → OPC UA client (on-target rework, not yet done)

`tsn/eth_proto.c` (Zephyr UDP + `wtsn_frame.h`) is the current **interim** transport.
The final design replaces it with an **open62541 embedded client** on the F767
(`opc.tcp://` to the RPi). Because there is no board on the dev box, this has not
been built or verified on-target. Steps:

1. Port open62541 for the Zephyr SDK's `arm-none-eabi` (no TLS;
   `UA_ENABLE_SUBSCRIBERS=ON`) — it is sizable, so budget heap (the F7 has 512 KB RAM
   + 128 KB CCM).
2. In a new `tsn/opcua_client.c` (replacing `eth_proto.c` for the data plane, keep
   `ptp.c` as the time base):
   - connect to `opc.tcp://<CNC_IP>:4840` (`CNC_IP` in `wtsn_config.h`);
   - **write** the telemetry nodes (id list in `rpi-tsn/wtsn_opcua_ids.h`):
     `servo_angle`, `relay_on`, `buzzer_hz/ms`, `sonar_sweep[]`, `btn1..4`,
     `ptp_offset_ns`, `ptp_state`, `ptp_locked`, `last_seen` — from the existing
     `wtsn_net_publish_*` calls;
   - **monitor** (or poll) the `cmd_*` nodes and dispatch to `wtsn_actuator_*`:
     `cmd_servo_angle` → servo, `cmd_relay_on` → relay, `cmd_beep_ms` → buzzer,
     `cmd_sonar_trigger` → sonar sweep, `cmd_reboot` → `NVIC_SystemReset()`.
3. `wtsn_net.h` keeps the same `publish_*` / command-callback interface, so
   `main.c` and the actuator code are unchanged — only the transport swaps.
4. Known server quirk handled on the RPi side (see `tsn_opcua_link.c`): a client
   session opened during the server's first few seconds can read
   `BadNodeIdUnknown` until it "warms up"; the poller drops and reconnects until a
   good read. The endpoint client should apply the same retry-on-connect policy.
