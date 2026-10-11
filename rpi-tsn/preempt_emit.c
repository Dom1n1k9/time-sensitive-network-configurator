/* htsn_preempt_emit — software 802.3br frame-preemption demo on the RPi (CNC).
 *
 * The RPi's macb NIC has no HW frame-preemption engine (and no taprio qdisc),
 * so this process *simulates* 802.3br preemption in software: it emits the same
 * 3-frame sequence a real preempting MAC would put on the wire, VLAN-tagged, so
 * the preemption is physically visible on the wire / in Wireshark:
 *   1) a preemptible (low-PCP) frame aborted mid-flight, ending in the 802.3br
 *      Last Fragment Marker (0xFFFF),
 *   2) the preempting urgent frame (PCP 7) that interrupted it,
 *   3) the full preemptible frame retransmitted now that the line is clear.
 *
 * Mirrors stm32-tsn/tsn/eth_proto.c:send_preempt_seq() — the tagged portion
 * ([TCI][0x0800][IPv4][UDP][htsn frame]) is byte-identical to the endpoint's.
 *
 * Env:
 *   HTSN_IFACE       egress interface   (default eth0)
 *   HTSN_VLAN        802.1Q vlan id     (default 100)
 *   HTSN_PCP         preemptible PCP    (default 5)
 *   HTSN_DST         dst MAC, a:b:c:d:e:f (default 01:80:c2:00:00:01 multicast demo)
 *   HTSN_INTERVAL_MS emit period (ms)   (default 2000)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <linux/if_packet.h>
#include <linux/if_ether.h>
#include "htsn_frame.h"

static volatile sig_atomic_t g_run = 1;
static void on_signal(int s) { (void)s; g_run = 0; }
static const char *env_or(const char *k, const char *d) {
    const char *v = getenv(k);
    return (v && *v) ? v : d;
}

#define IP_RPI     "192.168.1.10"   /* RPi (this host)   */
#define IP_EP      "192.168.1.20"   /* STM32 endpoint    */
#define PORT_TELEM 4001
#define PORT_CMD   4000

static int mac_parse(const char *s, uint8_t *out) {
    unsigned a, b, c, d, e, f;
    if (sscanf(s, "%x:%x:%x:%x:%x:%x", &a, &b, &c, &d, &e, &f) != 6) return -1;
    out[0] = a; out[1] = b; out[2] = c; out[3] = d; out[4] = e; out[5] = f;
    return 0;
}

/* IPv4 header checksum (ones' complement of 16-bit sums). */
static uint16_t ip_sum(const uint8_t *h, size_t len) {
    uint32_t s = 0;
    for (size_t i = 0; i + 1 < len; i += 2)
        s += (uint16_t)((h[i] << 8) | h[i + 1]);
    if (len & 1) s += (uint8_t)h[len - 1] << 8;
    while (s >> 16) s = (s & 0xffff) + (s >> 16);
    return (uint16_t)(~s & 0xffff);
}

/* Build a full 802.1Q-tagged frame:
 *   [dst 6][src 6][0x8100 2][TCI 2][0x0800 2][IPv4 20][UDP 8][htsn frame flen]
 * Returns the total byte length. */
static int build_tagged(uint8_t *out, const uint8_t *dst, const uint8_t *src,
                        uint16_t vlan, uint8_t pcp, const uint8_t *frame, uint16_t flen)
{
    int o = 0;
    memcpy(out + o, dst, 6); o += 6;
    memcpy(out + o, src, 6); o += 6;
    out[o++] = 0x81; out[o++] = 0x00;
    uint16_t tci = ((uint16_t)(pcp & 0x07) << 13) | (vlan & 0x0fff);
    out[o++] = (uint8_t)(tci >> 8);
    out[o++] = (uint8_t)(tci & 0xff);
    out[o++] = 0x08; out[o++] = 0x00;

    uint16_t udp_len = 8 + flen, ip_len = 20 + udp_len;
    uint8_t *ip = out + o; o += 20;
    memset(ip, 0, 20);
    ip[0] = 0x45;
    ip[2] = (uint8_t)(ip_len >> 8); ip[3] = (uint8_t)(ip_len & 0xff);
    ip[8] = 64; ip[9] = 17;
    uint8_t s4[4], d4[4]; unsigned a, b, c, e;
    sscanf(IP_RPI, "%u.%u.%u.%u", &a, &b, &c, &e); s4[0] = a; s4[1] = b; s4[2] = c; s4[3] = e;
    sscanf(IP_EP,  "%u.%u.%u.%u", &a, &b, &c, &e); d4[0] = a; d4[1] = b; d4[2] = c; d4[3] = e;
    memcpy(ip + 12, s4, 4); memcpy(ip + 16, d4, 4);
    uint16_t cs = ip_sum(ip, 20); ip[10] = (uint8_t)(cs >> 8); ip[11] = (uint8_t)(cs & 0xff);

    uint8_t *udp = out + o; o += 8;
    memset(udp, 0, 8);
    udp[0] = (uint8_t)(PORT_TELEM >> 8); udp[1] = (uint8_t)(PORT_TELEM & 0xff);
    udp[2] = (uint8_t)(PORT_CMD   >> 8); udp[3] = (uint8_t)(PORT_CMD   & 0xff);
    udp[4] = (uint8_t)(udp_len >> 8);    udp[5] = (uint8_t)(udp_len & 0xff);

    memcpy(out + o, frame, flen); o += flen;
    return o;
}

static void send_preempt_seq(int sock, const uint8_t *dst, const uint8_t *src,
                             uint16_t vlan, uint8_t pcp, struct sockaddr_ll *ll)
{
    static uint32_t seq = 0;
    seq++;
    uint8_t fbuf[64], full[160], part[160], ubuf[160];

    /* Frame 1/3: the "preemptible" low-priority frame (readable marker). */
    char pl[24];
    int plen = snprintf(pl, sizeof(pl), "PREEMPTIBLE %04u", (unsigned)(seq & 0xffff));
    int flen = htsn_frame_pack(fbuf, HTSN_TYPE_TELEM, HTSN_KIND_TELEM_PREEMPT,
                               (const uint8_t *)pl, (uint16_t)plen);
    if (flen <= 0) return;
    int full_len = build_tagged(full, dst, src, vlan, pcp, fbuf, (uint16_t)flen);
    if (full_len <= 0) return;

    /* 1) Aborted partial frame: cut off part of the frame, then append the
     *    2-byte 0xFFFF Last Fragment Marker (the 802.3br preemption signal). */
    int cut = full_len - 8;
    if (cut < 32) cut = 32;
    if (cut + 2 > (int)sizeof(part)) cut = (int)sizeof(part) - 2;
    memcpy(part, full, cut);
    part[cut] = 0xFF; part[cut + 1] = 0xFF;
    sendto(sock, part, (size_t)(cut + 2), 0, (struct sockaddr *)ll, sizeof(*ll));

    /* 2) The preempting urgent frame at the highest priority (PCP 7). */
    int uflen = htsn_frame_pack(fbuf, HTSN_TYPE_TELEM, HTSN_KIND_TELEM_PREEMPT_URG,
                                (const uint8_t *)"PREEMPTING!", 11);
    if (uflen > 0) {
        int ulen = build_tagged(ubuf, dst, src, vlan, 7, fbuf, (uint16_t)uflen);
        if (ulen > 0)
            sendto(sock, ubuf, (size_t)ulen, 0, (struct sockaddr *)ll, sizeof(*ll));
    }

    /* 3) Retransmit the full preemptible frame now that the line is clear. */
    sendto(sock, full, (size_t)full_len, 0, (struct sockaddr *)ll, sizeof(*ll));
}

int main(void)
{
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    const char *iface = env_or("HTSN_IFACE", "eth0");
    uint16_t vlan = (uint16_t)atoi(env_or("HTSN_VLAN", "100"));
    uint8_t pcp = (uint8_t)atoi(env_or("HTSN_PCP", "5"));
    long interval_ms = atol(env_or("HTSN_INTERVAL_MS", "2000"));

    uint8_t dst[6] = { 0x01, 0x80, 0xc2, 0x00, 0x00, 0x01 };
    const char *dstr = env_or("HTSN_DST", "");
    if (dstr && dstr[0] && mac_parse(dstr, dst) != 0)
        fprintf(stderr, "preempt: bad HTSN_DST, using multicast demo MAC\n");

    int sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_8021Q));
    if (sock < 0) { perror("socket AF_PACKET"); return 1; }

    uint8_t src[6] = { 0 };
    struct ifreq ifr; memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
    if (ioctl(sock, SIOCGIFHWADDR, &ifr) < 0) { perror("SIOCGIFHWADDR"); return 1; }
    memcpy(src, ifr.ifr_hwaddr.sa_data, 6);

    struct sockaddr_ll ll; memset(&ll, 0, sizeof(ll));
    ll.sll_family = AF_PACKET;
    ll.sll_protocol = htons(ETH_P_8021Q);
    ll.sll_ifindex = (int)if_nametoindex(iface);
    ll.sll_halen = 6;
    memcpy(ll.sll_addr, dst, 6);

    fprintf(stderr, "preempt: %s vlan=%d pcp=%d dst=%02x:%02x:%02x:%02x:%02x:%02x every %ldms\n",
            iface, vlan, pcp, dst[0], dst[1], dst[2], dst[3], dst[4], dst[5], interval_ms);

    while (g_run) {
        send_preempt_seq(sock, dst, src, vlan, pcp, &ll);
        for (long t = 0; t < interval_ms && g_run; t++) usleep(1000);
    }
    close(sock);
    return 0;
}
