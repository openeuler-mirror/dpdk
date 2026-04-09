/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_PACKETS_H
#define HINIC3_PACKETS_H

#include <netinet/in.h>
#include <stdbool.h>
#include "hinic3_types.h"

#define IP_ARGS(ip)                             \
    ntohl(ip) >> 24,                            \
    (ntohl(ip) >> 16) & 0xff,                   \
    (ntohl(ip) >> 8) & 0xff,                    \
    ntohl(ip) & 0xff

#define ETH_TYPE_IP            0x0800
#define ETH_TYPE_ARP           0x0806
#define ETH_TYPE_VLAN_8021Q    0x8100
#define ETH_TYPE_VLAN          ETH_TYPE_VLAN_8021Q
#define ETH_TYPE_VLAN_8021AD   0x88a8
#define ETH_TYPE_IPV6          0x86dd
#define ETH_TYPE_RARP          0x8035

#define ETH_ADDR_LEN           6

#define ETH_ADDR_FMT                                                    \
    "%02"PRIx8":%02"PRIx8":%02"PRIx8":%02"PRIx8":%02"PRIx8":%02"PRIx8

#define ETH_ADDR_ARGS(EA) \
    ((EA).ea)[0], ((EA).ea)[1], ((EA).ea)[2], ((EA).ea)[3], ((EA).ea)[4], ((EA).ea)[5]

#define IP_FMT "%"PRIu32".%"PRIu32".%"PRIu32".%"PRIu32

#define ETH_HEADER_LEN 14

struct eth_addr {
    union {
        uint8_t ea[6];
        uint16_t be16[3];
    };
};

struct eth_header {
    struct eth_addr eth_dst;
    struct eth_addr eth_src;
    uint16_t eth_type;
};

#define VLAN_HEADER_LEN 4
struct vlan_header {
    uint16_t vlan_tci;          /* Lowest 12 bits are VLAN ID. */
    uint16_t vlan_next_type;
};

#define IP_DONT_FRAGMENT  0x4000 /* Don't fragment. */
#define IP_MORE_FRAGMENTS 0x2000 /* More fragments. */
#define IP_FRAG_OFF_MASK  0x1fff /* Fragment offset. */
#define IP_IS_FRAGMENT(ip_frag_off) \
        ((ip_frag_off) & htons(IP_MORE_FRAGMENTS | IP_FRAG_OFF_MASK))

#define IP_IHL(ip_ihl_ver) ((ip_ihl_ver) & 15)

struct ip_header {
    uint8_t ip_ihl_ver;
    uint8_t ip_tos;
    uint16_t ip_tot_len;
    uint16_t ip_id;
    uint16_t ip_frag_off;
    uint8_t ip_ttl;
    uint8_t ip_proto;
    uint16_t ip_csum;
    hinic3_16aligned_be32 ip_src;
    hinic3_16aligned_be32 ip_dst;
};

#define UDP_HEADER_LEN 8
struct udp_header {
    uint16_t udp_src;
    uint16_t udp_dst;
    uint16_t udp_len;
    uint16_t udp_csum;
};

#define TCP_HEADER_LEN 20
struct tcp_header {
    uint16_t tcp_src;
    uint16_t tcp_dst;
    hinic3_16aligned_be32 tcp_seq;
    hinic3_16aligned_be32 tcp_ack;
    uint16_t tcp_ctl;
    uint16_t tcp_winsz;
    uint16_t tcp_csum;
    uint16_t tcp_urg;
};

static inline bool eth_type_vlan(uint16_t eth_type)
{
    return eth_type == htons(ETH_TYPE_VLAN_8021Q) ||
        eth_type == htons(ETH_TYPE_VLAN_8021AD);
}

static inline bool eth_addr_is_zero(const struct eth_addr a)
{
    return !(a.be16[0] | a.be16[0x1] | a.be16[0x2]);
}

static inline bool eth_addr_is_multicast(const struct eth_addr a)
{
    return a.ea[0] & 1;
}

#endif
