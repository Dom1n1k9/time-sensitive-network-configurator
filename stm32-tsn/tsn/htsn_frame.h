/* htsn-tsn deterministic wire format.
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
 *        2     1   kind    see HTSN_KIND_* below
 *        3     2   len     payload length in bytes, little-endian (0..65535)
 *        5    len  payload
 *      5+len   2   crc16   CRC16-CCITT over bytes [0 .. 5+len-1], little-endian
 *
 * Max datagram = 5 + 384 + 2 = 391 bytes (still a single unfragmented Ethernet
 * frame with headroom for the 802.1Q tag); the 2-byte length covers the full
 * 180-angle (364-byte) sonar sweep.
 */
#ifndef HTSN_FRAME_H
#define HTSN_FRAME_H

#include <stdint.h>
#include <stddef.h>

#define HTSN_FRAME_MAGIC   0xA5
#define HTSN_FRAME_HDR     5     /* magic + type + kind + len(2) */
#define HTSN_FRAME_CRC     2
/* 384 keeps the full 180-angle (1 deg) sonar sweep in a single, unfragmented
 * Ethernet frame (~390-byte datagram, well under the 1518-byte MTU). */
#define HTSN_FRAME_MAXPLEN 384
#define HTSN_FRAME_MAX     (HTSN_FRAME_HDR + HTSN_FRAME_MAXPLEN + HTSN_FRAME_CRC)

enum {
    HTSN_TYPE_CMD = 0,
    HTSN_TYPE_TELEM = 1,
};

/* Command kinds (CNC -> endpoint). */
enum {
    HTSN_KIND_CMD_SERVO   = 1,   /* payload: int16 LE angle (0..180)         */
    HTSN_KIND_CMD_SONAR   = 2,   /* payload: none — trigger a sweep          */
    HTSN_KIND_CMD_RELAY   = 3,   /* payload: uint8 on (0/1)                  */
    HTSN_KIND_CMD_BEEP    = 4,   /* payload: int16 LE ms                     */
    HTSN_KIND_CMD_HUD     = 5,   /* payload: JSON {"temp":..,"humidity":..}  */
    HTSN_KIND_CMD_REBOOT  = 6,   /* payload: none                            */
    HTSN_KIND_CMD_TSN_CFG = 7,   /* payload: htsn_tsn_cfg_t + gcl[]          */
};

/* Telemetry kinds (endpoint -> CNC). */
enum {
    HTSN_KIND_TELEM_HELLO   = 1,  /* payload: JSON {node,mac}                */
    HTSN_KIND_TELEM_SWEEP   = 2,  /* payload: int32 id + int16[angles]       */
    HTSN_KIND_TELEM_BUTTONS = 3,  /* payload: uint8 k1|k2<<1|k3<<2|k4<<3     */
    HTSN_KIND_TELEM_ACTUATOR= 4,  /* payload: JSON {on,hz,ms}                */
    HTSN_KIND_TELEM_PTP     = 5,  /* payload: JSON {offset_ns,state}         */
    HTSN_KIND_TELEM_TSN_APPLIED=6,/* payload: htsn_tsn_cfg_t + gcl[] + int32 features */
};

/* ---- TSN config / applied-state binary layout (shared with the RPi peer) ----
 * Fixed header, then gcl_count entries of {gate_state(1) + duration_ns(8 LE)}.
 * The applied-state frame appends a 4-byte little-endian features bitmap after
 * the GCL array. All multi-byte fields are little-endian. */
#pragma pack(push, 1)
typedef struct {
    uint8_t  priority;        /* 802.1p PCP (0-7)          */
    uint8_t  traffic_class;   /* traffic class (0-7)       */
    uint16_t vlan_id;         /* 802.1Q vlan (0-4094)      */
    uint8_t  preemption;      /* 802.1Qbb (0-2)            */
    uint8_t  timesync_mode;   /* 0 none / 1 GM / 2 slave   */
    uint8_t  stream_role;     /* 0 idle / 1 talker / 2 listener */
    uint16_t stream_vlan_id;  /* stream vlan               */
    uint8_t  stream_priority; /* stream data-frame PCP     */
    uint8_t  reserved;
    int64_t  tas_cycle_ns;    /* TAS cycle time            */
    uint16_t gcl_count;       /* number of gate entries    */
} htsn_tsn_cfg_t;
#pragma pack(pop)

#define HTSN_TSN_GCL_ENTRY  9
#define HTSN_TSN_MAX_GCL    16

/* Feature bitmap (endpoint reports what its HW actually enforces). */
#define HTSN_TSN_F_VLAN       (1u << 0)
#define HTSN_TSN_F_PCP        (1u << 1)
#define HTSN_TSN_F_PREEMPT    (1u << 2)
#define HTSN_TSN_F_STREAM     (1u << 3)
#define HTSN_TSN_F_TAS        (1u << 4)
#define HTSN_TSN_F_PTP        (1u << 5)

uint16_t htsn_crc16(const uint8_t *data, size_t len);

/* Serialize a frame into `out`; returns total frame length (<= HTSN_FRAME_MAX). */
int htsn_frame_pack(uint8_t *out, uint8_t type, uint8_t kind,
                    const uint8_t *payload, uint16_t len);

/* Validate + unpack a received frame. Returns 0 on success, -1 on bad magic/CRC,
 * and writes the payload into `payload` (capacity >= HTSN_FRAME_MAXPLEN). */
int htsn_frame_unpack(const uint8_t *in, size_t in_len, uint8_t *type, uint8_t *kind,
                      uint8_t *payload, size_t *payload_len);

#endif /* HTSN_FRAME_H */
