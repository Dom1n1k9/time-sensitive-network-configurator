/* wtsn-tsn deterministic wire format.
 *
 * One small UDP datagram = one bounded frame. No variable-length JSON on the
 * wire itself; payloads are fixed/typed so the RPi CNC can re-emit them to
 * MQTT/OPC UA. Both the STM32 endpoint and the RPi peer must agree on this.
 *
 *   offset  size  field
 *   ------  ----  ---------------------------------------------
 *        0     1   magic   (0xA5)
 *        1     1   type    0 = command (CNC -> endpoint)
 *                          1 = telemetry (endpoint -> CNC)
 *        2     1   kind    see WTSN_KIND_* below
 *        3     2   len     payload length in bytes, little-endian (0..65535)
 *        5    len  payload
 *      5+len   2   crc16   CRC16-CCITT over bytes [0 .. 5+len-1], little-endian
 *
 * Max datagram = 5 + 384 + 2 = 391 bytes (still a single unfragmented Ethernet
 * frame with headroom for the 802.1Q tag); the 2-byte length covers the full
 * 180-angle (364-byte) sonar sweep.
 */
#ifndef WTSN_FRAME_H
#define WTSN_FRAME_H

#include <stdint.h>
#include <stddef.h>

#define WTSN_FRAME_MAGIC   0xA5
#define WTSN_FRAME_HDR     5     /* magic + type + kind + len(2) */
#define WTSN_FRAME_CRC     2
/* 384 keeps the full 180-angle (1 deg) sonar sweep in a single, unfragmented
 * Ethernet frame (~390-byte datagram, well under the 1518-byte MTU). */
#define WTSN_FRAME_MAXPLEN 384
#define WTSN_FRAME_MAX     (WTSN_FRAME_HDR + WTSN_FRAME_MAXPLEN + WTSN_FRAME_CRC)

enum {
    WTSN_TYPE_CMD = 0,
    WTSN_TYPE_TELEM = 1,
};

/* Command kinds (CNC -> endpoint). */
enum {
    WTSN_KIND_CMD_SERVO   = 1,   /* payload: int16 LE angle (0..180)         */
    WTSN_KIND_CMD_SONAR   = 2,   /* payload: none — trigger a sweep          */
    WTSN_KIND_CMD_RELAY   = 3,   /* payload: uint8 on (0/1)                  */
    WTSN_KIND_CMD_BEEP    = 4,   /* payload: int16 LE ms                     */
    WTSN_KIND_CMD_HUD     = 5,   /* payload: JSON {"temp":..,"humidity":..}  */
    WTSN_KIND_CMD_REBOOT  = 6,   /* payload: none                            */
};

/* Telemetry kinds (endpoint -> CNC). */
enum {
    WTSN_KIND_TELEM_HELLO   = 1,  /* payload: JSON {node,mac}                */
    WTSN_KIND_TELEM_SWEEP   = 2,  /* payload: int32 id + int16[angles]       */
    WTSN_KIND_TELEM_BUTTONS = 3,  /* payload: uint8 k1|k2<<1|k3<<2|k4<<3     */
    WTSN_KIND_TELEM_ACTUATOR= 4,  /* payload: JSON {on,hz,ms}                */
    WTSN_KIND_TELEM_PTP     = 5,  /* payload: JSON {offset_ns,state}         */
};

uint16_t wtsn_crc16(const uint8_t *data, size_t len);

/* Serialize a frame into `out`; returns total frame length (<= WTSN_FRAME_MAX). */
int wtsn_frame_pack(uint8_t *out, uint8_t type, uint8_t kind,
                    const uint8_t *payload, uint16_t len);

/* Validate + unpack a received frame. Returns 0 on success, -1 on bad magic/CRC,
 * and writes the payload into `payload` (capacity >= WTSN_FRAME_MAXPLEN). */
int wtsn_frame_unpack(const uint8_t *in, size_t in_len, uint8_t *type, uint8_t *kind,
                      uint8_t *payload, size_t *payload_len);

#endif /* WTSN_FRAME_H */
