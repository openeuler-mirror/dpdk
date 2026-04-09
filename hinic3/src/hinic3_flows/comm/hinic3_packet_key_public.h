/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_PACKET_KEY_PUBLIC_H
#define HINIC3_PACKET_KEY_PUBLIC_H

#include "hinic3_util.h"
#include <linux/if_ether.h>
#include "hinic3_types.h"
#include "hinic3_packets_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* in a byte:
 * bit 6-7: version
 * bit 3-5: forward mode
 * bit 0-2: escape mode
 */
#define HINIC3_ESCAPE_MODE_OFFSET      0U
#define HINIC3_FORWARD_MODE_OFFSET     3U
#define HINIC3_MODE_VERSION_OFFSET     6U

#define HINIC3_ESCAPE_MODE_MASK        0x07
#define HINIC3_FORWARD_MODE_MASK       0x38
#define HINIC3_MODE_VERSION_MASK       0xC0

#define ETH_PAD_SIZE                  19

#define HINIC3_CT_UPDATE_MAX_ZONE      2

#define KEY_VLAN_OFFSET               1
#define KEY_VXLAN_OFFSET              2
#define KEY_INNER_VID_OFFSET          1
#define KEY_OUTER_VID_OFFSET          2

enum HINIC3_IP_TYPE {
    HINIC3_IP_TYPE_V4 = 1,
    HINIC3_IP_TYPE_V6
};

enum {
    HINIC3_CT_SEQ_RECORD_NONE,
    HINIC3_CT_SEQ_RECORD_ORIGIN,
    HINIC3_CT_SEQ_RECORD_REVERSE,
    HINIC3_CT_SEQ_RECORD_MAX,
};

enum {
    HINIC3_FLUSH_ALL,
    HINIC3_FLUSH_SINGLE_CT_ZONE,
    HINIC3_FLUSH_ALL_CT_ZONE
};

enum {
    CONN_HINIC3_STATUS_NONE,
    CONN_HINIC3_STATUS_ADDING,
    CONN_HINIC3_STATUS_ADDED,
    CONN_HINIC3_STATUS_DELETE
};

enum {
    /* Key = in_port + 7 tuple(smac, dmac, sip, dip, sport, dport, protocol) */
    HINIC3_KEY_5TUPLE,
    /* Key = in_port + outer vid + 7 tuple(smac, dmac, sip, dip, sport, dport, protocol) */
    HINIC3_KEY_VID_5TUPLE,
    /* Key = in_port + vxlan(sip, dip, vni) + 7 tuple(smac, dmac, sip, dip, sport, dport, protocol) */
    HINIC3_KEY_VXLAN_5TUPLE,
    /* Key = in_port + vxlan(sip, dip, vni) + inner vid + 7 tuple(smac, dmac, sip, dip, sport, dport, protocol) */
    HINIC3_KEY_VXLAN_VID_5TUPLE,
    /* Key = in_port + outer vid+ vxlan(sip, dip, vni) + 7 tuple(smac, dmac, sip, dip, sport, dport, protocol) */
    HINIC3_KEY_VID_VXLAN_5TUPLE,
    /* Key = in_port+outer vid+vxlan(sip, dip, vni)+inner vid+7 tuple(smac, dmac, sip, dip, sport, dport, protocol) */
    HINIC3_KEY_VID_VXLAN_VID_5TUPLE,
    /* Key = ct_zone + 7 tuple(smac, dmac, sip, dip, sport, dport, protocol) */
    HINIC3_KEY_CT_ZONE_5TUPLE,
    /* Key = in_port + 5 tuple(smac, dmac, sip, dip, protocol) */
    HINIC3_KEY_RAW_IP,
    /* Key = in_port + outer vid + 5 tuple(smac, dmac, sip, dip, protocol) */
    HINIC3_KEY_VID_RAW_IP,
    /* Key = in_port + vxlan(sip, dip, vni) + 5 tuple(smac, dmac, sip, dip, protocol) */
    HINIC3_KEY_VXLAN_RAW_IP,
    /* Key = in_port + vxlan(sip, dip, vni) + inner vid + 5 tuple(smac, dmac, sip, dip, protocol) */
    HINIC3_KEY_VXLAN_VID_RAW_IP,
    /* Key = in_port + outer vid  + vxlan(sip, dip, vni) + 5 tuple(smac, dmac, sip, dip, protocol) */
    HINIC3_KEY_VID_VXLAN_RAW_IP,
    /* Key = in_port + outer vid  + vxlan(sip, dip, vni) + inner vid + 5 tuple(smac, dmac, sip, dip, protocol) */
    HINIC3_KEY_VID_VXLAN_VID_RAW_IP,
    /* Key = in_port + eth(src mac, dst mac, ethernet type) */
    HINIC3_KEY_ETH,
    /* Key = in_port + outer vid + eth(src mac, dst mac, ethernet type) */
    HINIC3_KEY_VID_ETH,
    /* Key = in_port + vxlan(sip, dip, vni) + eth(src mac, dst mac, ethernet type) */
    HINIC3_KEY_VXLAN_ETH,
    /* Key = in_port + vxlan(sip, dip, vni) + inner vid + eth(src mac, dst mac, ethernet type) */
    HINIC3_KEY_VXLAN_VID_ETH,
    /* Key = in_port + outer vid + vxlan(sip, dip, vni) + eth(src mac, dst mac, ethernet type) */
    HINIC3_KEY_VID_VXLAN_ETH,
    /* Key = in_port + outer vid + vxlan(sip, dip, vni) + inner vid + eth(src mac, dst mac, ethernet type) */
    HINIC3_KEY_VID_VXLAN_VID_ETH,
    /* Key = hash + recisd_id */
    HINIC3_KEY_DP_HASH,
    HINIC3_KEY_END
};

enum {
    HINIC3_CT_DEFAULT_FLAG,
    HINIC3_CT_UDP_FLAG,
    HINIC3_CT_TCP_FLAG,
    HINIC3_CT_ICMP_FLAG,
    HINIC3_CT_ICMPV6_FLAG,
    HINIC3_CT_MAX_FLAG,
};

struct hinic3_ip_addr {
    union {
        hinic3_be32 ipv4;
        struct in6_addr ipv6;
    };
};

struct hinic3_key_ip {
    struct hinic3_ip_addr src;
    struct hinic3_ip_addr dst;
    hinic3_be16 ether_type;
    hinic3_be16 inner_type;
};

struct hinic3_key_vxlan {
    struct hinic3_key_ip ip;
    hinic3_be32 vni;
};

struct hinic3_key_mac {
    uint8_t dmac[ETH_ALEN];
    uint8_t smac[ETH_ALEN];
};

/* the struct below should be the same size */
struct hinic3_key_5tuple {
    struct hinic3_key_mac eth;
    struct hinic3_key_ip ip;
    hinic3_be16 port_src;
    hinic3_be16 port_dst;
};

struct hinic3_key_icmp {
    struct hinic3_key_mac eth;
    struct hinic3_key_ip ip;
    uint8_t icmp_code;
    uint8_t icmp_type;
    hinic3_be16 id;
};

struct hinic3_key_raw_ip {
    struct hinic3_key_mac eth;
    struct hinic3_key_ip ip;
    uint32_t protocol;
};

struct hinic3_key_eth {
    struct hinic3_key_mac eth;
    hinic3_be16 ether_type;
    uint16_t padding[ETH_PAD_SIZE];
}; /* the struct above should be the same size */

/* the struct below should be the same size before the vxlan field */
struct hinic3_key_vid_5tuple {
    struct hinic3_key_5tuple tuple;
    hinic3_be16 outer_vid;
    hinic3_be16 inner_vid;
    hinic3_be16 cvlan_id;  /* Inner cvlan in QinQ */
    hinic3_be16 vlan_id;
};

struct hinic3_key_vid_raw_ip {
    struct hinic3_key_raw_ip ip;
    hinic3_be16 outer_vid;
    hinic3_be16 inner_vid;
    uint16_t padding1;
    uint16_t padding2;
};

struct hinic3_key_vid_eth {
    struct hinic3_key_eth eth;
    hinic3_be16 outer_vid;
    hinic3_be16 inner_vid;
    uint16_t padding1;
    uint16_t padding2;
};

struct hinic3_key_vxlan_vid_5tuple {
    struct hinic3_key_5tuple tuple;
    hinic3_be16 outer_vid;
    hinic3_be16 inner_vid;
    hinic3_be16 cvlan_id;  /* Inner cvlan in QinQ */
    uint16_t input_port;
    struct hinic3_key_vxlan vxlan;
};

struct hinic3_key_vxlan_vid_raw_ip {
    struct hinic3_key_raw_ip ip;
    hinic3_be16 outer_vid;
    hinic3_be16 inner_vid;
    uint16_t padding1;
    uint16_t padding2;
    struct hinic3_key_vxlan vxlan;
};

struct hinic3_key_vxlan_vid_eth {
    struct hinic3_key_eth eth;
    hinic3_be16 outer_vid;
    hinic3_be16 inner_vid;
    uint16_t padding1;
    uint16_t padding2;
    struct hinic3_key_vxlan vxlan;
}; /* the struct above should be the same size before the vxlan field */

struct hinic3_key_geneve_opt {
    uint16_t opt_class;
    uint8_t type;
    uint8_t rsvd_len;
    uint32_t opt_data;
};

struct hinic3_key_geneve {
    struct hinic3_key_ip ip;
    hinic3_be32 vni;
    uint8_t flag;
    struct hinic3_key_geneve_opt option;
};

struct hinic3_key_geneve_vid_5tuple {
    struct hinic3_key_mac mac;
    hinic3_be16 ether_type;
    struct hinic3_key_ip ip;
    hinic3_be16 port_dst;
    uint16_t padding1;
    uint16_t padding2;
    uint16_t padding3;
    uint16_t input_port;
    struct hinic3_key_geneve geneve;
};

struct hinic3_conntrack_key_metadata {
    /* ct_zone is valid when type is HINIC3_KEY_CT_ZONE_5TUPLE, zero means notrack */
    union {
        uint16_t input_port;
        uint16_t ct_zone;
    };
    uint32_t is_vxlan_encap : 1;
    uint32_t is_vlan_push : 1;
    uint32_t key_len : 7;
    uint32_t protocol : 8;
    uint32_t tcp_udp_flag : 3;
    uint32_t type : 5;
    uint32_t need_ct_action : 1;
    uint32_t reserved : 22;
};

enum hinic3_conntrack_key_flags_type {
    HINIC3_KEY_FLAG_TYPE_ETH,
    HINIC3_KEY_FLAG_TYPE_VXLAN,
    HINIC3_KEY_FLAG_TYPE_VLAN,
    HINIC3_KEY_FLAG_TYPE_OUTER_IPV4,
    HINIC3_KEY_FLAG_TYPE_OUTER_IPV6,
    HINIC3_KEY_FLAG_TYPE_IPV4,
    HINIC3_KEY_FLAG_TYPE_IPV6,
    HINIC3_KEY_FLAG_TYPE_GRE,
    HINIC3_KEY_FLAG_TYPE_GRE_KEY,
    HINIC3_KEY_FLAG_TYPE_TCP,
    HINIC3_KEY_FLAG_TYPE_UDP,
    HINIC3_KEY_FLAG_TYPE_PORT,
    HINIC3_KEY_FLAG_TYPE_RAW_PROTOCOL,
    HINIC3_KEY_FLAG_TYPE_ICMP,
    HINIC3_KEY_FLAG_TYPE_ICMP6,
    HINIC3_KEY_FLAG_TYPE_END,
};

struct hinic3_conntrack_key_flags {
    uint32_t has_eth : 1;
    uint32_t has_vxlan : 1;
    uint32_t has_vlan : 1;
    uint32_t has_outer_ipv4 : 1;
    uint32_t has_outer_ipv6 : 1;
    uint32_t has_ipv4 : 1;
    uint32_t has_ipv6 : 1;
    uint32_t has_gre : 1;
    uint32_t has_gre_key : 1;
    uint32_t has_tcp : 1;
    uint32_t has_udp : 1;
    uint32_t has_port : 1;
    uint32_t has_raw_protocol : 1;
    uint32_t has_icmp : 1;
    uint32_t has_icmp6 : 1;
    uint32_t reserved : 17;
};

struct hinic3_conntrack_key {
    int32_t hydra_key_count;
    struct hydra_flow_item *hydra_key_head;
    struct hydra_flow_item *hydra_key_tail;
    union {
        uint64_t meta_num;
        struct hinic3_conntrack_key_metadata meta;
    };
    uint32_t table_id;
    union {
        uint32_t hdr_flags_num;
        struct hinic3_conntrack_key_flags hdr_flags;
    };
    uint8_t key[0];
};

struct hinic3_conntrack_simple_key {
    struct hinic3_conntrack_key key;
    struct hinic3_key_5tuple tuple;
};

struct hinic3_key_dp_hash {
    uint32_t hash;  // DP-HASH value
    uint32_t recirc_id;     // Recirc_id
};

struct hinic3_conntrack_full_key {
    struct hinic3_conntrack_key key;
    union {
        struct hinic3_key_5tuple tuple;
        struct hinic3_key_vid_5tuple vid_tuple;
        struct hinic3_key_vxlan_vid_5tuple vxlan_vid_tuple;
        struct hinic3_key_raw_ip ip;
        struct hinic3_key_vid_raw_ip vid_ip;
        struct hinic3_key_vxlan_vid_raw_ip vxlan_vid_ip;
        struct hinic3_key_eth eth;
        struct hinic3_key_vid_eth vid_eth;
        struct hinic3_key_vxlan_vid_eth vxlan_vid_eth;
        struct hinic3_key_dp_hash dp_hash;
    };
};

struct hinic3_l2_vlan {
    hinic3_be16 vlan_id;
    hinic3_be16 pcp;
    hinic3_be16 encap;
};

struct hinic3_l4_port {
    hinic3_be16 sport;
    hinic3_be16 dport;
};

struct hinic3_l4_icmp {
    uint8_t icmp_code;
    uint8_t icmp_type;
    hinic3_be16 icmp_id;
};

/* 灰卡分支结构体*/
struct hinic3_pkt_l2 {
    uint8_t smac[ETH_ALEN];
    uint8_t dmac[ETH_ALEN];
    struct hinic3_l2_vlan vlan;
    hinic3_be16 cvlan_id;
    hinic3_be16 eth_type;
};

struct hinic3_pkt_l3 {
    uint32_t ip_type;
    struct hinic3_ip_addr sip;
    struct hinic3_ip_addr dip;
    uint8_t ip_proto;
};

struct hinic3_pkt_l4 {
    union {
        struct hinic3_l4_port port_info;
        struct hinic3_l4_icmp icmp_info;
    };
};

struct hinic3_pkt_sub_hdr {
    uint8_t has_l3;
    uint8_t has_l4;
    uint8_t l4_is_icmp;
    uint8_t l2_has_vlan;
    struct hinic3_pkt_l2 l2;
    struct hinic3_pkt_l3 l3;
    struct hinic3_pkt_l4 l4;
};

struct hinic3_geneve_option {
    uint16_t opt_class;
    uint8_t type;
    uint8_t rsvd_len;
    uint32_t opt_data;
};

struct hinic3_pkt_header {
    struct hinic3_pkt_sub_hdr out_header;
    struct hinic3_pkt_sub_hdr inner_header;
    bool may_vxlan;
    bool may_geneve;
    bool is_vxlan;
    bool is_vxlan_encap;
    uint32_t vni;
    uint32_t geneve_vni;
    uint8_t geneve_flag;
    struct hinic3_geneve_option options;
};

static inline uint8_t hinic3_ct_get_proto_by_flag(uint32_t tcp_udp_flag)
{
    uint8_t proto[] = {HINIC3_CT_DEFAULT_FLAG, IPPROTO_UDP, IPPROTO_TCP, IPPROTO_ICMP, IPPROTO_ICMPV6};

    if (tcp_udp_flag < HINIC3_CT_MAX_FLAG) {
        return proto[tcp_udp_flag];
    } else {
        return HINIC3_CT_DEFAULT_FLAG;
    }
}

static inline void
hinic3_full_key_flag_set_at(uint32_t* flag, enum hinic3_conntrack_key_flags_type type, bool value) {
    if (type >= HINIC3_KEY_FLAG_TYPE_END)
        return;
    if (value) {
        *flag |= (1U << type);
    } else {
        *flag &= ~(1U << type);
    }
}

static inline bool
hinic3_full_key_flag_get_at(uint32_t flag, enum hinic3_conntrack_key_flags_type type) {
    if (type >= HINIC3_KEY_FLAG_TYPE_END)
        return false;
    return (flag & (1U << type)) != 0;
}

#ifdef __cplusplus
}
#endif

#endif
