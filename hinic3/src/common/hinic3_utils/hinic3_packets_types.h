/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_PACKETS_TYPES_H
#define HINIC3_PACKETS_TYPES_H

#include <inttypes.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include "hinic3_eth_packets.h"
#include "rte_mbuf.h"
#include "hinic3_types.h"

#ifndef ETH_ALEN
#define ETH_ALEN    6
#endif

#ifndef IPV6_ALEN
#define IPV6_ALEN    16
#endif

#define PKT_MAX_BURST 32

#define HINIC3_ETH_HEADER_LEN 14
#define HINIC3_ETH_TYPE_LEN      2
#define HINIC3_VLAN_HEADER_LEN      4
#define HINIC3_ETH_ADDR_LEN           6
#define HINIC3_IPV6_ADDR_LEN           16
#define HINIC3_IPV4_ADDR_LEN         4
#define HINIC3_ARP_ETH_HEADER_LEN 28
#define HINIC3_IP_HEADER_LEN          20
#define HINIC3_IPV6_FLOW_TABLE_LEN      3
#define HINIC3_IPV6_HEADER_LEN        40
#define HINIC3_TCP_HEADER_LEN    20
#define HINIC3_UDP_HEADER_LEN    8
#define HINIC3_ICMP6_HEADER_LEN   4
#define HINIC3_ICMP6_MESSAGE_LEN  4
#define HINIC3_ICMP_HEADER_LEN    8
#define HINIC3_IGMP_HEADER_LEN    8
#define HINIC3_SCTP_HEADER_LEN 12
#define HINIC3_ETH_PAYLOAD_MIN 46
#define HINIC3_ETH_TOTAL_MIN (HINIC3_ETH_HEADER_LEN + HINIC3_ETH_PAYLOAD_MIN)
#define HINIC3_ARP_PACKET_SIZE  (2 + HINIC3_ETH_HEADER_LEN + HINIC3_VLAN_HEADER_LEN + \
                          HINIC3_ARP_ETH_HEADER_LEN)
#define HINIC3_VLAN_ETH_HEADER_LEN (HINIC3_ETH_HEADER_LEN + HINIC3_VLAN_HEADER_LEN)
#define HINIC3_ETH_TYPE_MIN           0x600

#define VLAN_CFI 0x1000
#define VLAN_CFI_SHIFT 12

#define VLAN_VID_MASK 0x0fff
#define VLAN_PCP_MASK  0xe000
#define VLAN_PCP_SHIFT 13

#define ND_OPT_SOURCE_LINKADDR 1
#define ND_OPT_TARGET_LINKADDR 2

#define ND_ROUTER_SOLICIT      133
#define ND_ROUTER_ADVERT      134
#define ND_NEIGHBOR_SOLICIT  135
#define ND_NEIGHBOR_ADVERT  136
#define ND_REDIRECT                 137

#define HINIC3_PACKET_CONTEXT_SIZE 64

#define VXLAN_FLAGS 0x08000000  /* struct vxlanhdr.vx_flags required value. */
#define ELB_VXLAN_FLAGS 0x48000000  /* elbtrans vxlan flags. */
#define EGF_VXLAN_FLAGS 0x88000000  /* egf vxlan flags. */
#define ELB_L7_VXLAN_FLAGS 0x0a000000  /* elbv3 L7 vxlan flags. */
#define DEFAULT_VXLAN_PORT 4789

/* The IPv6 flow label is in the lower 20 bits of the first 32-bit word. */
#define IPV6_LABEL_MASK 0x000fffff
#define DEFAULT_IP_IHL_VER 0x45

#define HINIC3_INET_ADDRSTRLEN 16
#define HINIC3_INET6_ADDRSTRLEN 46

enum hinic3_ip_type {
    HINIC3_IP_ADDR_V4 = 0,
    HINIC3_IP_ADDR_V6
};

struct eth_address {
    union {
        uint8_t ea[HINIC3_ETH_ADDR_LEN];
        hinic3_be16 be16[HINIC3_ETH_ADDR_LEN / 2]; /* hinic3_be16 has 2 bytes */
    };
};

typedef hinic3_be32 in4_addr_t;

typedef struct {
    uint8_t addr[HINIC3_IPV6_ADDR_LEN];
} in6_addr_t;

struct hinic3_ip_addr_header {
    bool is_ipv6;
    union {
        in4_addr_t ipv4;
        in6_addr_t ipv6;
    } u;
};

struct hinic3_eth_header {
    struct eth_address dst;
    struct eth_address src;
    hinic3_be16 eth_type;
};

struct hinic3_vlan_header {
    hinic3_be16 vlan_next_type;
    hinic3_be16 vlan_tci;
};

struct hinic3_vlan_eth_header {
    struct eth_address dst;
    struct eth_address src;
    hinic3_be16 eth_type;
    hinic3_be16 vlan_tci;
    hinic3_be16 vlan_next_type;
};

struct hinic3_arp_eth_header {
    /* arp header members. */
    hinic3_be16 ar_hrd;           /* Hardware type. */
    hinic3_be16 ar_pro;           /* Protocol type. */
    uint8_t ar_hln;            /* Hardware address length. */
    uint8_t ar_pln;            /* Protocol address length. */
    hinic3_be16 ar_op;            /* Opcode. */

    /* arp body members. */
    struct eth_address ar_sha;     /* src mac address. */
    hinic3_16aligned_be32 ar_spa;  /* src ip address. */
    struct eth_address ar_tha;     /* dst mac address. */
    hinic3_16aligned_be32 ar_tpa;  /* dst ip address. */
};

struct hinic3_ip_header {
    uint8_t ip_ihl_ver;
    uint8_t ip_tos;
    hinic3_be16 ip_tot_len;
    hinic3_be16 ip_id;
    hinic3_be16 ip_frag_off;
    uint8_t ip_ttl;
    uint8_t ip_proto;
    hinic3_be16 ip_csum;
    in4_addr_t ip_src;
    in4_addr_t ip_dst;
};

/* NextHeader field of IPv6 packet header */
#define HINIC3_NEXTHDR_HOP        0    /* Hop-by-hop option header. */
#define HINIC3_NEXTHDR_TCP        6    /* TCP segment. */
#define HINIC3_NEXTHDR_UDP        17    /* UDP message. */
#define HINIC3_NEXTHDR_IPV6        41    /* IPv6 in IPv6 */
#define HINIC3_NEXTHDR_ROUTING        43    /* Routing header. */
#define HINIC3_NEXTHDR_FRAGMENT    44    /* Fragmentation/reassembly header. */
#define HINIC3_NEXTHDR_GRE        47    /* GRE header. */
#define HINIC3_NEXTHDR_ESP        50    /* Encapsulating security payload. */
#define HINIC3_NEXTHDR_AUTH        51    /* Authentication header. */
#define HINIC3_NEXTHDR_ICMP        58    /* ICMP for IPv6. */
#define HINIC3_NEXTHDR_NONE        59    /* No next header */
#define HINIC3_NEXTHDR_DEST        60    /* Destination options header. */
#define HINIC3_NEXTHDR_SCTP        132    /* SCTP message. */
#define HINIC3_NEXTHDR_MOBILITY    135    /* Mobility header. */
#define HINIC3_NEXTHDR_MAX        255
#define HINIC3_IP6F_OFF_MASK     0xfff8

#define HINIC3_IP6_HEADER_LEN 40
struct hinic3_ip6_header {
    uint8_t version : 4,
    priority : 4;
    uint8_t flow_lbl[HINIC3_IPV6_FLOW_TABLE_LEN];
    hinic3_be16 payload_len;
    uint8_t nexthdr;
    uint8_t hop_limit;
    in6_addr_t saddr;
    in6_addr_t daddr;
};

#define HINIC3_IP6_HEADER_LEN 40
struct hinic3_ipv6_opt_hdr {
    uint8_t         nexthdr;
    uint8_t         hdrlen;
    /* TLV encoded option data follows. */
} __attribute__((packed));

struct hinic3_ip6_frag {
    uint8_t ip6f_nxt;
    uint8_t ip6f_reserved;
    hinic3_be16 ip6f_offlg;
    hinic3_be32 ip6f_ident;
};

struct hinic3_ip6_rthdr {
    uint8_t ip6r_nxt;
    uint8_t ip6r_len;
    uint8_t ip6r_type;
    uint8_t ip6r_segleft;
};

struct hinic3_16aligned_ip6_frag {
    uint8_t ip6f_nxt;
    uint8_t ip6f_reserved;
    hinic3_be16 ip6f_offlg;
    hinic3_16aligned_be32 ip6f_ident;
};

struct hinic3_icmp6_header {
    uint8_t icmp6_type;
    uint8_t icmp6_code;
    hinic3_be16 icmp6_cksum;
};

struct hinic3_udp_header {
    hinic3_be16 udp_src;
    hinic3_be16 udp_dst;
    hinic3_be16 udp_len;
    hinic3_be16 udp_csum;
};

struct hinic3_tcp_header {
    hinic3_be16 tcp_src;
    hinic3_be16 tcp_dst;
    hinic3_16aligned_be32 tcp_seq;
    hinic3_16aligned_be32 tcp_ack;
    hinic3_be16 tcp_ctl;
    hinic3_be16 tcp_winsz;
    hinic3_be16 tcp_csum;
    hinic3_be16 tcp_urg;
};

struct hinic3_icmp_header {
    uint8_t icmp_type;
    uint8_t icmp_code;
    hinic3_be16 icmp_csum;
    union {
        struct {
            hinic3_be16 id;
            hinic3_be16 seq;
        } echo;
        struct {
            hinic3_be16 empty;
            hinic3_be16 mtu;
        } frag;
        hinic3_16aligned_be32 gateway;
    } icmp_fields;
};

struct hinic3_sctp_header {
    hinic3_be16 sctp_src;
    hinic3_be16 sctp_dst;
    hinic3_be32 sctp_vtag;
    hinic3_be32 sctp_csum;
};

/* MPLS related definitions */
#define HINIC3_MPLS_TTL_MASK       0x000000ff
#define HINIC3_MPLS_TTL_SHIFT      0

#define HINIC3_MPLS_BOS_MASK       0x00000100
#define HINIC3_MPLS_BOS_SHIFT      8

#define HINIC3_MPLS_TC_MASK        0x00000e00
#define HINIC3_MPLS_TC_SHIFT       9

#define HINIC3_MPLS_LABEL_MASK     0xfffff000
#define HINIC3_MPLS_LABEL_SHIFT    12

#define HINIC3_MPLS_HLEN           4
#define HINIC3_PACKET_PADDING_INFO_LEN 64

struct hinic3_mpls_hdr {
    hinic3_16aligned_be32 mpls_lse;
};

/* VXLAN protocol header */
struct hinic3_vxlanhdr {
    union {
        hinic3_16aligned_be32 vx_flags;
        struct {
            uint8_t flags;
            uint8_t reserved[2];
            uint8_t next_protocol;
        } vx_gpe;
    };
    hinic3_16aligned_be32 vx_vni;
};

struct hinic3_flow_tnl {
    hinic3_be32 ip_dst;
    hinic3_be32 ip_src;
    in6_addr_t ipv6_dst;
    in6_addr_t ipv6_src;
    hinic3_be64 tun_id;
};

struct hinic3_packet_metadata {
    uint32_t pkt_hash;
    uint32_t pkt_mark;
    struct hinic3_flow_tnl tunnel;

    uint16_t ct_state;
    uint16_t ct_zone;       /* Needed by offload engine */
    uint32_t in_port;
    void *pkt_ctx;  /* for user to store private process context. */
    uint32_t entity_id;      /* Entity id for QoS. */
};

struct padding_info {
    /**
     * If RTE_MBUF_F_TX_TCP_CKSUM or RTE_MBUF_F_TX_UDP_CKSUM is set in mbuf->ol_flags,
     * and csum_start is not 0, We need to offload csum calculation.
     */
    uint16_t csum_start;      /* Position to start checksumming from */
    uint16_t csum_offset;    /* Offset after that to place checksum */
    /**
     * In eGF:
     * Save the packet's original vlan when packet enter the ecmp/route/arp component.
     * The Qos module uses this original vlan and packet's SIP to limit the flow rate.
     */
    uint16_t org_vlan;
    /**
     * In order to save the inner l2 offset of vxlan/gre packet
     */
    uint32_t ipv6_hash;
    uint8_t frag_cnt;
};

struct hinic3_packet {
    struct rte_mbuf mbuf;
    /* basic packet info */
    hinic3_be16 dl_type;
    uint16_t l3_ofs;               /* l3 header offset, or UINT16_MAX. */
    uint16_t l4_ofs;               /* l4 header offset, or UINT16_MAX. */
    uint16_t inner_l2_ofs;          /* inner l2 offset, or UINT16_MAX. */
    uint16_t inner_l3_ofs;         /* inner l3 header offset, or UINT16_MAX. */
    uint16_t inner_l4_ofs;         /* inner l4 header offset, or UINT16_MAX. */
    uint8_t l2_pad_size;           /* l2 padding size. */
    uint16_t l2_5_ofs;             /* for MPLS, or UINT16_MAX. */
    uint32_t mac_offset;           /* Record MAC offset of original packet when GSO */

    /* packet memtadata info */
    union {
        struct hinic3_packet_metadata md;
        uint64_t data[HINIC3_PACKET_CONTEXT_SIZE / 8];    /* 8 means eight bytes for uint64_t */
    };

    struct hinic3_packet *next;
    union {
        struct padding_info padding;
        uint8_t reserved[HINIC3_PACKET_PADDING_INFO_LEN];
    };
};

struct hinic3_packet_batch {
    int count;
    struct hinic3_packet *packets[PKT_MAX_BURST];
    bool drop_packets;
};

#define HINIC3_ND_OPT_LEN 8
struct hinic3_nd_opt {
    uint8_t  nd_opt_type;      /* Values defined in icmp6.h */
    uint8_t  nd_opt_len;       /* in units of 8 octets (the size of this struct) */
    struct eth_address nd_opt_mac;   /* Ethernet address in the case of SLL or TLL options */
};

/* Fragment bits, used for IPv4 and IPv6, always zero for non-IP flows. */
#define FLOW_NW_FRAG_ANY   (1 << 0) /* Set for any IP frag. */
#define FLOW_NW_FRAG_LATER (1 << 1) /* Set for IP frag with nonzero offset. */
#define FLOW_NW_FRAG_MASK  (FLOW_NW_FRAG_ANY | FLOW_NW_FRAG_LATER)
#define IPV6_MAX_EXT_HDRS  8
#define IPV6_EXT_HDR_MIN_LEN 8

/* as l3_len in dpdk is 9bit(511), thus here we define IPV6_EXT_HDR_MAX_TOTAL_LEN
 as 471(511-sizeof(struct ipv6_hdr)) */
#define IPV6_EXT_HDR_MAX_TOTAL_LEN 471

static inline uint32_t tcp_offset(hinic3_be16 tcp_ctl)
{
    const unsigned int bit_move_num = 12;
    return ntohs(tcp_ctl) >> bit_move_num;
}

#endif
