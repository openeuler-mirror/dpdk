/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Huawei Technologies Co., Ltd
 */
#include "hinic3_flow_dump_item_key.h"
#include "hinic3_offload_flow_port.h"
#include "hinic3_message.h"
#include "hinic3_log.h"
#include "hinic3_parse_agent_config.h"
#include "hinic3_init_arg.h"

#define HINIC3_VFPORT_TRANS_HEAD_BIT 0X1000
#define HINIC3_PORT_VALUE_MASK 0x0FFF
static void hinic3_construct_port_key(enum hinic3_flow_key_type type, const hinic3_nlattr_itr nla,
                                     uint16_t *port_src, uint16_t *port_dst)
{
    switch (type) {
        case HINIC3_FLOW_KEY_SRC_PORT:
            *port_src = hinic3_nlattr_get_itr_u16(nla);
            break;
        case HINIC3_FLOW_KEY_DST_PORT:
            *port_dst = hinic3_nlattr_get_itr_u16(nla);
            break;
        default:
            break;
    }
}

static void hinic3_construct_protocol_key(const hinic3_nlattr_itr nla, uint16_t *tcp_udp_flag)
{
    uint16_t flag = hinic3_nlattr_get_itr_u8(nla);
    switch (flag) {
        case IPPROTO_UDP:
            *tcp_udp_flag = HINIC3_DUMP_UDP_FLAG;
            break;
        case IPPROTO_TCP:
            *tcp_udp_flag = HINIC3_DUMP_TCP_FLAG;
            break;
        default:
            *tcp_udp_flag = HINIC3_DUMP_TCP_UDP_INVALID;
            break;
    }
    return;
}

static void hinic3_construct_ip_key(enum hinic3_flow_key_type type, const hinic3_nlattr_itr nla,
                                   struct hinic3_key_ip *ip_key, uint8_t *ip_version)
{
    const void* ipv6 = NULL;
    switch (type) {
        case HINIC3_FLOW_KEY_SRC_IP:
            *ip_version = 0;
            ip_key ->src.ipv4 = hinic3_nlattr_get_itr_u32(nla);
            break;
        case HINIC3_FLOW_KEY_DST_IP:
            ip_key ->dst.ipv4 = hinic3_nlattr_get_itr_u32(nla);
            break;
        case HINIC3_FLOW_KEY_SRC_IPV6:
            *ip_version = 1;
            ipv6 = hinic3_nlattr_get_itr_unspec(nla, sizeof(ip_key ->src.ipv6));
            memcpy(&(ip_key ->src.ipv6), ipv6, sizeof(ip_key ->src.ipv6));
            break;
        case HINIC3_FLOW_KEY_DST_IPV6:
            ipv6 = hinic3_nlattr_get_itr_unspec(nla, sizeof(ip_key ->dst.ipv6));
            memcpy(&(ip_key ->dst.ipv6), ipv6, sizeof(ip_key ->dst.ipv6));
            break;
        default:
            break;
    }
}

static void hinic3_construct_mac_key(enum hinic3_flow_key_type type, const hinic3_nlattr_itr nla,
                                    struct hinic3_key_mac *mac)
{
    const uint8_t *nla_mac = NULL;
    switch (type) {
        case HINIC3_FLOW_KEY_SRC_MAC:
            nla_mac = (const uint8_t*)hinic3_nlattr_get_itr_unspec(nla, ETH_ALEN);
            memcpy(mac -> smac, nla_mac, sizeof(mac->smac));
            break;
        case HINIC3_FLOW_KEY_DST_MAC:
            nla_mac = (const uint8_t*)hinic3_nlattr_get_itr_unspec(nla, ETH_ALEN);
            memcpy(mac -> dmac, nla_mac, sizeof(mac->dmac));
            break;
        default:
            break;
    }
    return;
}

static void hinic3_construct_vxlan_key(const hinic3_nlattr_itr nla, uint32_t *vni)
{
    *vni = hinic3_nlattr_get_itr_u32(nla);
    return;
}

static void hinic3_construct_vlan_key(enum hinic3_flow_key_type type HINIC3_UNUSED, const hinic3_nlattr_itr nla,
                                     uint32_t *vlan_id)
{
    *vlan_id = hinic3_nlattr_get_itr_u16(nla);
    return;
}

static void hinic3_construct_ether_type_key(const hinic3_nlattr_itr nla, uint16_t *ether_type)
{
    *ether_type =  ntohs(hinic3_nlattr_get_itr_u16(nla));
    return;
}

static void hinic3_construct_input_port_key(const hinic3_nlattr_itr nla, uint32_t *port)
{
    uint16_t meta_port = hinic3_nlattr_get_itr_u16(nla);
    uint16_t ifindex = ntohs(meta_port);
    uint16_t dpdk_port;

    int ret = hinic3_get_port_id_by_ifindex(ifindex, &dpdk_port);
    if (ret != 0) {
        HINIC3_LOG(ERR, FLOW, "HINIC3 FLOW DUMP: construct_input_port_key, get error port");
        return;
    }

    *port = dpdk_port;
    return;
}

int hinic3_get_full_key(struct hinic3_flow_dump_keys* row_keys, const struct hinic3_nlattr *key)
{
    if (row_keys == NULL || key == NULL) {
        HINIC3_LOG(ERR, FLOW, "HINIC3 FLOW DUMP: Get full key error, input NULL pointer");
        return -1;
    }

    hinic3_nlattr_itr nla = NULL;
    HINIC3_NLATTR_FOR_EACH(nla, key) {
        enum hinic3_flow_key_type type = hinic3_nlattr_get_itr_type(nla);
        switch (type) {
            case HINIC3_FLOW_KEY_VNI:
                row_keys ->vxlan_flag = true;
                hinic3_construct_vxlan_key(nla, &(row_keys->vni));
                break;
            case HINIC3_FLOW_KEY_OUTER_TCI:
            case HINIC3_FLOW_KEY_OUTER_VID:
                row_keys ->vlan_flag = true;
                hinic3_construct_vlan_key(type, nla, &(row_keys->inner_vid));
                break;
            case HINIC3_FLOW_KEY_PROTOCOL:
                hinic3_construct_protocol_key(nla, &(row_keys->tcp_udp_flag));
                break;
            case HINIC3_FLOW_KEY_SRC_PORT:
            case HINIC3_FLOW_KEY_DST_PORT:
                hinic3_construct_port_key(type, nla, &(row_keys->port_src), &(row_keys->port_dst));
                break;
            case HINIC3_FLOW_KEY_SRC_MAC:
            case HINIC3_FLOW_KEY_DST_MAC:
                hinic3_construct_mac_key(type, nla, &(row_keys->mac));
                break;
            case HINIC3_FLOW_KEY_DL_TYPE:
                hinic3_construct_ether_type_key(nla, &(row_keys->ether_type));
                break;
            case HINIC3_FLOW_KEY_SRC_IP:
            case HINIC3_FLOW_KEY_DST_IP:
            case HINIC3_FLOW_KEY_SRC_IPV6:
            case HINIC3_FLOW_KEY_DST_IPV6:
                hinic3_construct_ip_key(type, nla, &row_keys->ip, &row_keys->ip_version);
                break;
            case HINIC3_FLOW_KEY_IN_PORT:
                hinic3_construct_input_port_key(nla, &(row_keys->input_port));
                break;
            default:
                break;
        }
    }

    if (row_keys->inner_vid != 0) {
        row_keys->ether_inner_type = row_keys->ether_type;
        row_keys->ether_type = ETH_TYPE_VLAN;
    }
    return 0;
}

static void hinic3_revert_ip_key(enum hinic3_flow_key_type type, const hinic3_nlattr_itr nla, struct hinic3_key_ip *ip_key)
{
    const void *ipv6 = NULL;
    switch (type)
    {
    case HINIC3_FLOW_KEY_SRC_IP:
        ip_key->src.ipv4 = hinic3_nlattr_get_itr_u32(nla);
        break;
    case HINIC3_FLOW_KEY_DST_IP:
        ip_key->dst.ipv4 = hinic3_nlattr_get_itr_u32(nla);
        break;
    case HINIC3_FLOW_KEY_SRC_IPV6:
        ipv6 = hinic3_nlattr_get_itr_unspec(nla, sizeof(ip_key->src.ipv6));
        (void)memcpy(&(ip_key->src.ipv6), ipv6, sizeof(ip_key->src.ipv6));
        break;
    case HINIC3_FLOW_KEY_DST_IPV6:
        ipv6 = hinic3_nlattr_get_itr_unspec(nla, sizeof(ip_key->dst.ipv6));
        (void)memcpy(&(ip_key->dst.ipv6), ipv6, sizeof(ip_key->dst.ipv6));
        break;
    default:
        break;
    }
}

static uint32_t hinic3_ct_get_flag_by_proto(uint8_t proto)
{
    static const uint32_t proto_to_flag[] = {
        [IPPROTO_UDP] = HINIC3_CT_UDP_FLAG,
        [IPPROTO_TCP] = HINIC3_CT_TCP_FLAG,
        [IPPROTO_ICMP] = HINIC3_CT_ICMP_FLAG,
        [IPPROTO_ICMPV6] = HINIC3_CT_ICMPV6_FLAG,
    };

    if (proto == HINIC3_CT_DEFAULT_FLAG)
    {
        return HINIC3_CT_DEFAULT_FLAG;
    }

    if (proto < sizeof(proto_to_flag) / sizeof(proto_to_flag[0]))
    {
        uint32_t flag = proto_to_flag[proto];
        if (flag != 0)
        {
            return flag;
        }
    }

    return HINIC3_CT_DEFAULT_FLAG;
}

int hinic3_revert_key(struct hinic3_conntrack_key *full_key, const struct hinic3_nlattr *key)
{
    if (full_key == NULL || key == NULL)
    {
        HINIC3_LOG(ERR, FLOW, "HINIC3 FLOW DUMP: Get full key error, input NULL pointer");
        return -1;
    }
    hinic3_be16 ether_type = ETH_TYPE;
    hinic3_nlattr_itr nla = NULL;
    struct hinic3_key_vxlan_vid_5tuple *tuple = (struct hinic3_key_vxlan_vid_5tuple *)full_key->key;
    HINIC3_NLATTR_FOR_EACH(nla, key)
    {
        enum hinic3_flow_key_type type = hinic3_nlattr_get_itr_type(nla);
        switch (type)
        {
        case HINIC3_FLOW_KEY_VNI:
            hinic3_construct_vxlan_key(nla, &(tuple->vxlan.vni));
            break;
        case HINIC3_FLOW_KEY_OUTER_TCI:
        case HINIC3_FLOW_KEY_OUTER_VID:
            tuple->inner_vid = hinic3_nlattr_get_itr_u16(nla);
            break;
        case HINIC3_FLOW_KEY_PROTOCOL:
            full_key->meta.tcp_udp_flag = hinic3_ct_get_flag_by_proto(hinic3_nlattr_get_itr_u8(nla));
            break;
        case HINIC3_FLOW_KEY_SRC_PORT:
        case HINIC3_FLOW_KEY_DST_PORT:
            hinic3_construct_port_key(type, nla, &(tuple->tuple.port_src), &(tuple->tuple.port_dst));
            break;
        case HINIC3_FLOW_KEY_SRC_MAC:
        case HINIC3_FLOW_KEY_DST_MAC:
            hinic3_construct_mac_key(type, nla, &(tuple->tuple.eth));
            break;
        case HINIC3_FLOW_KEY_DL_TYPE:
            hinic3_construct_ether_type_key(nla, &(tuple->tuple.ip.ether_type));
            tuple->tuple.ip.ether_type = ether_type;
            break;
        case HINIC3_FLOW_KEY_SRC_IP:
        case HINIC3_FLOW_KEY_DST_IP:
        case HINIC3_FLOW_KEY_SRC_IPV6:
        case HINIC3_FLOW_KEY_DST_IPV6:
            hinic3_revert_ip_key(type, nla, &(tuple->tuple.ip));
            break;
        case HINIC3_FLOW_KEY_IN_PORT:
            tuple->input_port = hinic3_nlattr_get_itr_u16(nla);
            break;
        default:
            break;
        }
    }
    return 0;
}

int hinic3_revert_action(struct hinic3_conntrack_key *full_key, const struct hinic3_nlattr *action)
{
    if (full_key == NULL || action == NULL)
    {
        HINIC3_LOG(ERR, FLOW, "HINIC3 FLOW DUMP: Get full key error, input NULL pointer");
        return -1;
    }

    hinic3_nlattr_itr nla = NULL;
    HINIC3_NLATTR_FOR_EACH(nla, action)
    {
        enum hinic3_flow_action_type type = hinic3_nlattr_get_itr_type(nla);
        switch (type)
        {
        case HINIC3_FLOW_ACT_VLAN_PUSH:
            full_key->meta.is_vlan_push = 1;
            break;
        case HINIC3_FLOW_ACT_CT:
            full_key->meta.need_ct_action = (hinic3_status_packet_upcall_get() == DEFAULT_PUT) ? 0 : 1;
            break;
        default:
            break;
        }
    }
    return 0;
}