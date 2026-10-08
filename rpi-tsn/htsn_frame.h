/* htsn-tsn deterministic wire format — RPi (CNC) side.
 *
 * MUST stay byte-identical to stm32-tsn/tsn/htsn_frame.h: one small UDP
 * datagram = one bounded frame, CRC16-CCITT protected. The bridge
 * (tsn_opcua_bridge.c) packs/unpacks these to talk to the STM32 endpoint.
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
 */
#ifndef HTSN_FRAME_RPI_H
#define HTSN_FRAME_RPI_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define HTSN_FRAME_MAGIC   0xA5
#define HTSN_FRAME_HDR     5
#define HTSN_FRAME_CRC     2
#define HTSN_FRAME_MAXPLEN 384
#define HTSN_FRAME_MAX     (HTSN_FRAME_HDR + HTSN_FRAME_MAXPLEN + HTSN_FRAME_CRC)

enum {
    HTSN_TYPE_CMD = 0,
    HTSN_TYPE_TELEM = 1,
};

/* Command kinds (CNC -> endpoint). */
enum {
    HTSN_KIND_CMD_SERVO     = 1,  /* payload: int16 LE angle              */
    HTSN_KIND_CMD_SONAR     = 2,  /* payload: none                        */
    HTSN_KIND_CMD_RELAY     = 3,  /* payload: uint8 on                    */
    HTSN_KIND_CMD_BEEP      = 4,  /* payload: int16 LE ms                 */
    HTSN_KIND_CMD_HUD       = 5,  /* payload: JSON                        */
    HTSN_KIND_CMD_REBOOT    = 6,  /* payload: none                        */
    HTSN_KIND_CMD_TSN_CFG   = 7,  /* payload: htsn_tsn_cfg_t + gcl[]      */
};

/* Telemetry kinds (endpoint -> CNC). */
enum {
    HTSN_KIND_TELEM_HELLO      = 1, /* payload: JSON {node,mac}          */
    HTSN_KIND_TELEM_SWEEP      = 2, /* payload: int32 id + int16[angles] */
    HTSN_KIND_TELEM_BUTTONS    = 3, /* payload: uint8 k1|k2<<1|k3<<2|k4  */
    HTSN_KIND_TELEM_ACTUATOR   = 4, /* payload: JSON {on,hz,ms,angle}    */
    HTSN_KIND_TELEM_PTP        = 5, /* payload: JSON {offset_ns,state}   */
    HTSN_KIND_TELEM_TSN_APPLIED= 6, /* payload: htsn_tsn_cfg_t + gcl[] + int32 features */
    HTSN_KIND_TELEM_PREEMPT    = 7, /* 802.3br: preemptible (low-pri) demo payload       */
    HTSN_KIND_TELEM_PREEMPT_URG= 8, /* 802.3br: preempting (high-pri) demo payload       */
};

/* ---- TSN config / applied-state binary layout (shared with the endpoint) ----
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

static uint16_t htsn_crc16(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

static int htsn_frame_pack(uint8_t *out, uint8_t type, uint8_t kind,
                           const uint8_t *payload, uint16_t len) {
    if (len > HTSN_FRAME_MAXPLEN) return -1;
    size_t n = HTSN_FRAME_HDR + len;
    out[0] = HTSN_FRAME_MAGIC;
    out[1] = type;
    out[2] = kind;
    out[3] = (uint8_t)(len & 0xFF);
    out[4] = (uint8_t)((len >> 8) & 0xFF);
    if (len && payload) memcpy(&out[HTSN_FRAME_HDR], payload, len);
    uint16_t crc = htsn_crc16(out, n);
    out[n]     = (uint8_t)(crc & 0xFF);
    out[n + 1] = (uint8_t)(crc >> 8);
    return (int)(n + HTSN_FRAME_CRC);
}

static int htsn_frame_unpack(const uint8_t *in, size_t in_len, uint8_t *type, uint8_t *kind,
                             uint8_t *payload, size_t *payload_len) {
    if (in_len < HTSN_FRAME_HDR + HTSN_FRAME_CRC) return -1;
    if (in[0] != HTSN_FRAME_MAGIC) return -1;
    uint16_t len = (uint16_t)in[3] | ((uint16_t)in[4] << 8);
    if (len > HTSN_FRAME_MAXPLEN) return -1;
    if (in_len < (size_t)HTSN_FRAME_HDR + len + HTSN_FRAME_CRC) return -1;
    uint16_t crc = htsn_crc16(in, HTSN_FRAME_HDR + len);
    uint16_t got = (uint16_t)in[HTSN_FRAME_HDR + len] |
                   ((uint16_t)in[HTSN_FRAME_HDR + len + 1] << 8);
    if (crc != got) return -1;
    *type = in[1];
    *kind = in[2];
    *payload_len = len;
    if (len && payload) memcpy(payload, &in[HTSN_FRAME_HDR], len);
    return 0;
}

#endif /* HTSN_FRAME_RPI_H */
