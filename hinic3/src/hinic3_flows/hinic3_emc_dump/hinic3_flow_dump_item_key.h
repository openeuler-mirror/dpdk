/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Huawei Technologies Co., Ltd
 */
#ifndef HINIC3_FLOW_DUMP_ITEM_KEY
#define HINIC3_FLOW_DUMP_ITEM_KEY
#include "hinic3_packet_key_public.h"
#include "hinic3_nlattr.h"

#define ETH_TYPE 8
struct hinic3_flow_dump_keys {
    // ether
    struct hinic3_key_mac mac;
    hinic3_be16 ether_type;
    // vlan
    bool vlan_flag;
    hinic3_be32 cvlan_id;
    hinic3_be32 inner_vid;
    hinic3_be16 ether_inner_type;
    // vxlan
    bool vxlan_flag;
    hinic3_be32 vni;

    // ip_version : 0 ipv4, 1 ipv6
    uint8_t ip_version;
    struct hinic3_key_ip ip;
    // application
    hinic3_be16 port_src;
    hinic3_be16 port_dst;
    uint16_t tcp_udp_flag;
    // input_port
    uint32_t input_port;
};

enum {
    HINIC3_DUMP_TCP_UDP_INVALID,
    HINIC3_DUMP_TCP_FLAG,
    HINIC3_DUMP_UDP_FLAG
};

struct proto_flag_map {
    uint8_t proto;
    uint32_t flag;
};

int hinic3_get_full_key(struct hinic3_flow_dump_keys* row_keys, const struct hinic3_nlattr *key);

int hinic3_revert_key(struct hinic3_conntrack_key* full_key, const struct hinic3_nlattr *key);

int hinic3_revert_action(struct hinic3_conntrack_key* full_key, const struct hinic3_nlattr *action);
#endif
