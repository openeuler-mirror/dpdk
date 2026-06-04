/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2022 Huawei Technologies Co., Ltd
 */

#include "hinic3_offload_flow.h"
#include "rte_lcore.h"
#include "rte_flow.h"
#include "hinic3_message.h"
#include "hinic3_flow_agent.h"
#include "hinic3_flow_session.h"
#include "hinic3_iface_global.h"
#include "hinic3_iface_flow.h"
#include "hinic3_log.h"
#include "hinic3_offload_flow_public.h"
#include "hinic3_timeval.h"
#include "hinic3_iface_port.h"
#include "hinic3_agent_cmd_time.h"
#include "hinic3_iface_flow_api_record.h"
#include "hinic3_ui_string.h"
#include "hinic3_trace_flow.h"
#include "hinic3_parse_agent_config.h"
#include "hinic3_ufid_del_flow.h"
#include "hinic3_offload_action.h"
#include "hinic3_offload_action_public.h"
#include "hinic3_flow_qos.h"
#include "hinic3_ufid_del_flow.h"
#include "hinic3_ufid_map_rte_flow.h"

#define TCI_OFFSET 0xF000
#define RTE_TCP_FIN_SYN_RST_FLAG 0x07
#define VXLAN_VNI_LAST_INDEX 2
#define HINIC3_DFX_FLOW_ALLOC_MEM_BASE_SIZE 2048
#define HINIC3_NO_FOUND_FLOW (-4)

static void hinic3_flow_agent_construct_5tuple(struct hinic3_nlattr *hinic3_key,
    const struct hinic3_conntrack_key *key)
{
    const struct hinic3_key_5tuple *tuple = (const struct hinic3_key_5tuple *)key->key;
    const struct hinic3_key_raw_ip *raw_ip = (const struct hinic3_key_raw_ip *)key->key;
    hinic3_nlattr_put_u16(hinic3_key, HINIC3_FLOW_KEY_DL_TYPE, tuple->ip.ether_type);
    if (tuple->ip.ether_type == htons(ETH_TYPE_IP)) {
        hinic3_nlattr_put_u32(hinic3_key, HINIC3_FLOW_KEY_SRC_IP, tuple->ip.src.ipv4);
        hinic3_nlattr_put_u32(hinic3_key, HINIC3_FLOW_KEY_DST_IP, tuple->ip.dst.ipv4);
    } else if (tuple->ip.ether_type == htons(ETH_TYPE_IPV6)) {
        hinic3_nlattr_put_unspec(hinic3_key, HINIC3_FLOW_KEY_SRC_IPV6, &tuple->ip.src.ipv6, sizeof(tuple->ip.src.ipv6));
        hinic3_nlattr_put_unspec(hinic3_key, HINIC3_FLOW_KEY_DST_IPV6, &tuple->ip.dst.ipv6, sizeof(tuple->ip.dst.ipv6));
    }

    if (key->meta.tcp_udp_flag == HINIC3_CT_DEFAULT_FLAG) {
        hinic3_nlattr_put_u8(hinic3_key, HINIC3_FLOW_KEY_PROTOCOL, raw_ip->protocol);
    } else {
        hinic3_nlattr_put_u16(hinic3_key, HINIC3_FLOW_KEY_SRC_PORT, tuple->port_src);
        hinic3_nlattr_put_u16(hinic3_key, HINIC3_FLOW_KEY_DST_PORT, tuple->port_dst);
        hinic3_nlattr_put_u8(hinic3_key, HINIC3_FLOW_KEY_PROTOCOL, hinic3_ct_get_proto_by_flag(key->meta.tcp_udp_flag));
    }
}

static void hinic3_flow_agent_construct_5tuple_flexda(struct hinic3_nlattr *hinic3_key,
    const struct hinic3_conntrack_key *key)
{
    const struct hinic3_key_5tuple *tuple = (const struct hinic3_key_5tuple *)key->key;

    if (key->hdr_flags.has_eth) {
        hinic3_nlattr_put_u16(hinic3_key, HINIC3_FLOW_KEY_DEF_ETH_TYPE, tuple->ip.ether_type);
    }

    if (key->hdr_flags.has_ipv4) {
        hinic3_nlattr_put_u32(hinic3_key, HINIC3_FLOW_KEY_DEF_IPV4_SIP, tuple->ip.src.ipv4);
        hinic3_nlattr_put_u32(hinic3_key, HINIC3_FLOW_KEY_DEF_IPV4_DIP, tuple->ip.dst.ipv4);
        hinic3_nlattr_put_u8(hinic3_key, HINIC3_FLOW_KEY_DEF_IPV4_PROTOCOL, key->meta.protocol);
    } else if (key->hdr_flags.has_ipv6) {
        hinic3_nlattr_put_unspec(hinic3_key, HINIC3_FLOW_KEY_DEF_IPV6_SIP, &tuple->ip.src.ipv6,
            sizeof(tuple->ip.src.ipv6));
        hinic3_nlattr_put_unspec(hinic3_key, HINIC3_FLOW_KEY_DEF_IPV6_DIP, &tuple->ip.dst.ipv6,
            sizeof(tuple->ip.dst.ipv6));
        hinic3_nlattr_put_u8(hinic3_key, HINIC3_FLOW_KEY_DEF_IPV6_PROTOCOL, key->meta.protocol);
    }

    if (key->hdr_flags.has_tcp) {
        hinic3_nlattr_put_u16(hinic3_key, HINIC3_FLOW_KEY_DEF_TCP_SPORT, tuple->port_src);
        hinic3_nlattr_put_u16(hinic3_key, HINIC3_FLOW_KEY_DEF_TCP_DPORT, tuple->port_dst);
    } else if (key->hdr_flags.has_udp) {
        hinic3_nlattr_put_u16(hinic3_key, HINIC3_FLOW_KEY_DEF_UDP_SPORT, tuple->port_src);
        hinic3_nlattr_put_u16(hinic3_key, HINIC3_FLOW_KEY_DEF_UDP_DPORT, tuple->port_dst);
    } else if (key->hdr_flags.has_icmp || key->hdr_flags.has_icmp6) {
        uint8_t icmp_type = ((tuple->port_src & HINIC3_ICMP_TYPE_MASK) >> HINIC3_BIT_MID_MOVE_INDEX);
        uint8_t icmp_code = (tuple->port_src & HINIC3_ICMP_CODE_MASK);
        uint16_t icmp_id = tuple->port_dst;
        if (key->hdr_flags.has_icmp) {
            hinic3_nlattr_put_u8(hinic3_key, HINIC3_FLOW_KEY_DEF_ICMP_TYPE, icmp_type);
            hinic3_nlattr_put_u8(hinic3_key, HINIC3_FLOW_KEY_DEF_ICMP_CODE, icmp_code);
            hinic3_nlattr_put_u16(hinic3_key, HINIC3_FLOW_KEY_DEF_ICMP_IDENT, icmp_id);
        } else {
            hinic3_nlattr_put_u8(hinic3_key, HINIC3_FLOW_KEY_DEF_ICMP6_TYPE, icmp_type);
            hinic3_nlattr_put_u8(hinic3_key, HINIC3_FLOW_KEY_DEF_ICMP6_CODE, icmp_code);
        }
    }
}

void hinic3_flow_agent_construct_hydra_key(struct hinic3_nlattr *hinic3_key,
    const struct hinic3_conntrack_key *key)
{
    struct hydra_flow_item *hydra_key_node = key->hydra_key_head;
    while (hydra_key_node != NULL) {
        hinic3_nlattr_put_unspec(hinic3_key, hydra_key_node->item_type, hydra_key_node->item_data,
        hydra_key_node->item_data_size);
        hydra_key_node = hydra_key_node->next;
    }
}

static void hinic3_offload_flow_construct_key(const struct hinic3_conntrack_key *key, struct hinic3_nlattr *hinic3_key)
{
    const struct hinic3_key_mac *mac_tuple = (const struct hinic3_key_mac *)key->key;
    const struct hinic3_key_vxlan_vid_5tuple *port_key = (const struct hinic3_key_vxlan_vid_5tuple *)key->key;

    hinic3_nlattr_put_u16(hinic3_key, HINIC3_FLOW_KEY_IN_PORT, port_key->input_port);
    hinic3_nlattr_put_unspec(hinic3_key, HINIC3_FLOW_KEY_DST_MAC, mac_tuple->dmac, sizeof(mac_tuple->dmac));
    hinic3_nlattr_put_unspec(hinic3_key, HINIC3_FLOW_KEY_SRC_MAC, mac_tuple->smac, sizeof(mac_tuple->smac));

    const struct hinic3_key_vxlan *vxlan = &(((const struct hinic3_key_vxlan_vid_5tuple *)key->key)->vxlan);
    hinic3_nlattr_put_u32(hinic3_key, HINIC3_FLOW_KEY_VNI, vxlan->vni);

    uint16_t outer_vid = ((const struct hinic3_key_vid_5tuple *)key->key)->outer_vid;
    if (!hinic3_support_vlan_tci_get())
        hinic3_nlattr_put_u16(hinic3_key, HINIC3_FLOW_KEY_OUTER_VID, outer_vid);
    else
        hinic3_nlattr_put_u16(hinic3_key, HINIC3_FLOW_KEY_OUTER_TCI, outer_vid);

    hinic3_flow_agent_construct_5tuple(hinic3_key, key);
    hinic3_trace_flow_info_update(HINIC3_FLOW_CONSTRUCT_KEY_DONE_TRACE, key);
    return;
}

static void hinic3_offload_flow_construct_key_flexda(const struct hinic3_conntrack_key *key, struct hinic3_nlattr *hinic3_key)
{
    const struct hinic3_key_mac *mac_tuple = (const struct hinic3_key_mac *)key->key;
    const struct hinic3_key_vxlan_vid_5tuple *port_key = (const struct hinic3_key_vxlan_vid_5tuple *)key->key;

    if (key->hdr_flags.has_port) {
        hinic3_nlattr_put_u16(hinic3_key, HINIC3_FLOW_KEY_DPDK_PORT_ID, port_key->input_port);
    }
    if (key->hdr_flags.has_eth) {
        hinic3_nlattr_put_unspec(hinic3_key, HINIC3_FLOW_KEY_DEF_ETH_DMAC, mac_tuple->dmac, sizeof(mac_tuple->dmac));
        hinic3_nlattr_put_unspec(hinic3_key, HINIC3_FLOW_KEY_DEF_ETH_SMAC, mac_tuple->smac, sizeof(mac_tuple->smac));
    }
    if (key->hdr_flags.has_vxlan) {
        /* 移动it云规范默认下发RTE_FLOW_ITEM_TYPE_VXLAN及RTE_FLOW_ITEM_TYPE_VLAN */
        const struct hinic3_key_vxlan *vxlan = &(((const struct hinic3_key_vxlan_vid_5tuple *)key->key)->vxlan);
        hinic3_nlattr_put_unspec(hinic3_key, HINIC3_FLOW_KEY_DPDK_VXLAN, (const void *)&(vxlan->vni),
            HINIC3_VXLAN_VNI_DATA_SIZE);
    }
    if (key->hdr_flags.has_vlan) {
        const uint16_t outer_vid = ((const struct hinic3_key_vid_5tuple *)key->key)->outer_vid;
        hinic3_nlattr_put_u16(hinic3_key, HINIC3_FLOW_KEY_DEF_VLAN_TCI, outer_vid);
    }
    hinic3_flow_agent_construct_5tuple_flexda(hinic3_key, key);
    hinic3_flow_agent_construct_hydra_key(hinic3_key, key);
    hinic3_trace_flow_info_update(HINIC3_FLOW_CONSTRUCT_KEY_DONE_TRACE, key);
    return;
}

void hinic3_offload_flow_construct_key_entrance(const struct hinic3_conntrack_key *key, struct hinic3_nlattr *hinic3_key)
{
    if (hinic3_card_mod_get() == PROG_MODE)
        hinic3_offload_flow_construct_key_flexda(key, hinic3_key);
    else
        hinic3_offload_flow_construct_key(key, hinic3_key);
    return;
}

static void hinic3_parse_vlan_key(const struct rte_flow_item *item, struct hinic3_key_ip *ip,
                                 struct hinic3_conntrack_full_key *key)
{
    const struct rte_flow_item_vlan *vlan = item->spec;
    uint16_t *outer_vid = &(((struct hinic3_key_vid_5tuple *)key->key.key)->outer_vid);

    *outer_vid = vlan->tci;
    if (vlan->inner_type != 0) {
        if (hinic3_card_mod_get() == PROG_MODE)
            ip->inner_type = vlan->inner_type;
        else
            ip->ether_type = vlan->inner_type;
    }
    key->key.hdr_flags.has_vlan = 1;
}

static void hinic3_parse_tcp_key(const struct rte_flow_item *item, hinic3_be16 *port_src, hinic3_be16 *port_dst,
                                struct hinic3_conntrack_full_key *key)
{
    const struct rte_flow_item_tcp *tcp_spec = item->spec;
    const struct rte_flow_item_tcp *tcp_mask = item->mask;

    *port_src = tcp_spec->hdr.src_port;
    *port_dst = tcp_spec->hdr.dst_port;
    key->key.meta.tcp_udp_flag = HINIC3_CT_TCP_FLAG;
    key->key.hdr_flags.has_tcp = 1;
    if (tcp_mask == NULL) {
        return;
    }
    /** 
    支持tcp_flag 对于tcp_flags vSwtich在rte_flow中mask/key填写0x00/0x07，dpak将该key转换为ct action下发
    */
    if (tcp_spec->hdr.tcp_flags == 0 && tcp_mask->hdr.tcp_flags == RTE_TCP_FIN_SYN_RST_FLAG) {
            key->key.meta.need_ct_action = 1;
    }
}

static void hinic3_parese_ipv4_key(const struct rte_flow_item *item, struct hinic3_conntrack_full_key *key)
{
    const struct rte_flow_item_ipv4 *ipv4 = item->spec;
    struct hinic3_key_5tuple *tuple = NULL;
    struct hinic3_key_raw_ip *raw_ip = NULL;

    uint8_t protocol = ipv4->hdr.next_proto_id;
    /**
        1. raw_ip和5tuple是联合体，所以不能同时下四层信息(tuple->port_src)和协议信息(raw_ip->protocol)，因为这两个是同一块内存。
        2. 在原来【黑卡场景】下，TCP、UDP、ICMP、ICMPV6这四种报文需要下四层信息，所以在这个接口中只组三层信息，protocol及四层信息在其他接口中处理。
            比如：
            2.1 如果是UDP等四种报文，会在hinic3_parse_udp_key接口中处理src_port、dst_port和protocol，由于第一点，protocol不能放在raw_ip里和四层信息一起下,
                所以使用key->key.meta.tcp_udp_flag保存protocol信息
            2.2 如果是其他报文只需要下三层信息，则将protocol放在raw_ip->protocol中保存
        3. 在【灰卡场景】下，对这部分逻辑进行重构，将key->key.meta.tcp_udp_flag改成8bit，将所有报文的protocol信息都直接放在key->key.meta.tcp_udp_flag中保存
    */
    if (hinic3_card_mod_get() == PROG_MODE) {
        tuple = (struct hinic3_key_5tuple *)key->key.key;
        memcpy(&tuple->ip.src, &ipv4->hdr.src_addr, sizeof(ipv4->hdr.src_addr));
        memcpy(&tuple->ip.dst, &ipv4->hdr.dst_addr, sizeof(ipv4->hdr.dst_addr));
        key->key.meta.protocol = protocol;
        return;
    }

    if (protocol == IPPROTO_TCP || protocol == IPPROTO_UDP || protocol == IPPROTO_ICMP || protocol == IPPROTO_ICMPV6) {
        tuple = (struct hinic3_key_5tuple *)key->key.key;
        memcpy(&tuple->ip.src, &ipv4->hdr.src_addr, sizeof(ipv4->hdr.src_addr));
        memcpy(&tuple->ip.dst, &ipv4->hdr.dst_addr, sizeof(ipv4->hdr.dst_addr));
    } else {
        raw_ip = (struct hinic3_key_raw_ip *)key->key.key;
        memcpy(&raw_ip->ip.src, &ipv4->hdr.src_addr, sizeof(ipv4->hdr.src_addr));
        memcpy(&raw_ip->ip.dst, &ipv4->hdr.dst_addr, sizeof(ipv4->hdr.dst_addr));
        raw_ip->protocol = ipv4->hdr.next_proto_id;
    }
    key->key.meta.protocol = protocol;
    return;
}

static void howff_parse_ipv6_key(const struct rte_flow_item *item, struct hinic3_conntrack_full_key *key)
{
    const struct rte_flow_item_ipv6 *ipv6 = item->spec;
    struct hinic3_key_5tuple *tuple = NULL;
    struct hinic3_key_raw_ip *raw_ip = NULL;

    uint8_t protocol = ipv6->hdr.proto;
    if (hinic3_card_mod_get() == PROG_MODE) {
        tuple = (struct hinic3_key_5tuple *)key->key.key;
        memcpy(&tuple->ip.src, ipv6->hdr.src_addr, sizeof(ipv6->hdr.src_addr));
        memcpy(&tuple->ip.dst, ipv6->hdr.dst_addr, sizeof(ipv6->hdr.dst_addr));
        key->key.meta.protocol = protocol;
        return;
    }

    if (protocol == IPPROTO_TCP || protocol == IPPROTO_UDP || protocol == IPPROTO_ICMP || protocol == IPPROTO_ICMPV6) {
        tuple = (struct hinic3_key_5tuple *)key->key.key;
        memcpy(&tuple->ip.src, ipv6->hdr.src_addr, sizeof(ipv6->hdr.src_addr));
        memcpy(&tuple->ip.dst, ipv6->hdr.dst_addr, sizeof(ipv6->hdr.dst_addr));
    } else {
        raw_ip = (struct hinic3_key_raw_ip *)key->key.key;
        memcpy(&raw_ip->ip.src, ipv6->hdr.src_addr, sizeof(ipv6->hdr.src_addr));
        memcpy(&raw_ip->ip.dst, ipv6->hdr.dst_addr, sizeof(ipv6->hdr.dst_addr));
        raw_ip->protocol = ipv6->hdr.proto;
    }
    key->key.meta.protocol = protocol;
    return;
}

static void hinic3_parse_ip_key(const struct rte_flow_item *item, struct hinic3_conntrack_full_key *key)
{
    switch (item->type) {
        case RTE_FLOW_ITEM_TYPE_IPV4:
            hinic3_parese_ipv4_key(item, key);
            key->key.hdr_flags.has_ipv4 = 1; 
            break;
        case RTE_FLOW_ITEM_TYPE_IPV6:
            howff_parse_ipv6_key(item, key);
            key->key.hdr_flags.has_ipv6 = 1;
            break;
        default:
            break;
    }
}

static void hinic3_clear_ip_key(struct hinic3_key_ip *ip)
{
    memset(&ip->src, 0, sizeof(ip->src));
    memset(&ip->dst, 0, sizeof(ip->dst));
}

static void hinic3_parse_vxlan_key(const struct rte_flow_item *item, struct hinic3_conntrack_full_key *key)
{
    const struct rte_flow_item_vxlan *vxlan = NULL;

    vxlan = item->spec;
    uint32_t *vxlan_vni = &(((struct hinic3_key_vxlan_vid_5tuple *)key->key.key)->vxlan.vni);
    *vxlan_vni = vxlan->vni[0] + (vxlan->vni[1] << HINIC3_BYTE_BITS) +
                    (vxlan->vni[VXLAN_VNI_LAST_INDEX] << HINIC3_SHORT_BITS);
    if (hinic3_card_mod_get() != PROG_MODE)
        *vxlan_vni <<= HINIC3_BYTE_BITS;
    key->key.hdr_flags.has_vxlan = 1;
}

static int hinic3_parse_in_port_id(const struct rte_flow_item *item, struct hinic3_key_vxlan_vid_5tuple *port_key, struct hinic3_conntrack_full_key *key)
{
    uint16_t port_id;
    const struct rte_flow_item_port_id *port_id_item = NULL;

    port_id_item = item->spec;
    port_id = (uint16_t)hinic3_process_port_id(port_id_item->id);
    port_key->input_port = htons(port_id);
    key->key.hdr_flags.has_port = 1;
    return 0;
}

static void hinic3_parse_udp_key(const struct rte_flow_item *item, hinic3_be16 *port_src, hinic3_be16 *port_dst,
                                struct hinic3_conntrack_full_key *key)
{
    const struct rte_flow_item_udp *udp = item->spec;

    *port_src = udp->hdr.src_port;
    *port_dst = udp->hdr.dst_port;
    if (hinic3_card_mod_get() != PROG_MODE)
        key->key.meta.tcp_udp_flag = HINIC3_CT_UDP_FLAG;
    key->key.hdr_flags.has_udp = 1;
}

static void hinic3_clear_udp_key(hinic3_be16 *port_src, hinic3_be16 *port_dst, struct hinic3_conntrack_full_key *key)
{
    *port_src = 0;
    *port_dst = 0;
    key->key.meta.tcp_udp_flag = HINIC3_CT_DEFAULT_FLAG;
}

static int hinic3_parse_icmp_key(const struct rte_flow_item *item, hinic3_be16 *port_src, hinic3_be16 *port_dst,
    struct hinic3_conntrack_full_key *key)
{
    const struct rte_flow_item_icmp *icmp = item->spec;
    uint8_t icmp_type = icmp->hdr.icmp_type;
    uint8_t icmp_code = icmp->hdr.icmp_code;
    uint16_t icmp_ident = icmp->hdr.icmp_ident;

    *port_src = ((icmp_type) << HINIC3_BIT_MID_MOVE_INDEX) | icmp_code;
    *port_dst = icmp_ident;

    switch (item->type) {
        case RTE_FLOW_ITEM_TYPE_ICMP:
            key->key.hdr_flags.has_icmp = 1;
            key->key.meta.tcp_udp_flag = HINIC3_CT_ICMP_FLAG;
            break;
        case RTE_FLOW_ITEM_TYPE_ICMP6:
            key->key.hdr_flags.has_icmp6 = 1;
            key->key.meta.tcp_udp_flag = HINIC3_CT_ICMPV6_FLAG;
            break;
        default:
            break;
    }
    return 0;
}

static void hinic3_parse_eth_key(const struct rte_flow_item *item, struct hinic3_key_mac *mac, struct hinic3_key_ip *ip, struct hinic3_conntrack_full_key *key)
{
    const struct rte_flow_item_eth *eth = NULL;

    eth = item->spec;
    memcpy(mac->dmac, &eth->dst, ETH_ALEN);
    memcpy(mac->smac, &eth->src, ETH_ALEN);
    ip->ether_type = eth->type;
    key->key.hdr_flags.has_eth = 1;
}

static void hinic3_clear_eth_key(struct hinic3_key_mac *mac, struct hinic3_key_ip *ip)
{
    memset(mac->dmac, 0, ETH_ALEN);
    memset(mac->smac, 0, ETH_ALEN);
    ip->ether_type = 0;
}

static int hinic3_insert_hydra_key(const struct hydra_flow_item *hydra_key_new, struct hinic3_conntrack_key *key)
{
    struct hydra_flow_item *hydra_key_copy = NULL;
    /* 如果已经达到最大值，则不做处理 */
    if (key->hydra_key_count >= HINIC3_HYDRA_KEY_MAX_NUM) {
        HINIC3_LOG(ERR, FLOW, "hinic3load flow error, hydra key count is max");
        return -1;
    }

    /* 将hydra_item中的数据拷贝到key中 */
    hydra_key_copy = (struct hydra_flow_item *)hinic3_malloc(sizeof(struct hydra_flow_item), HINIC3_FLOWS);
    if (hydra_key_copy == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3load flow error, malloc memory for hydra key failed");
        return -1;
    }

    hydra_key_copy->item_type = hydra_key_new->item_type;
    hydra_key_copy->item_data_size = hydra_key_new->item_data_size;
    hydra_key_copy->next = NULL;

    /* 防止item_data大，导致内存申请失败 */
    if (hydra_key_copy->item_data_size > HINIC3_HYDRA_ALLOC_SIZE_MAX) {
        hinic3_free(hydra_key_copy);
        HINIC3_LOG(ERR, FLOW, "hinic3load flow error, hydra key data size is too big");
        return -1;
    }

    /* 为item_data申请内存 */
    hydra_key_copy->item_data = (void *)hinic3_malloc(hydra_key_copy->item_data_size, HINIC3_FLOWS);
    if (hydra_key_copy->item_data == NULL) {
        hinic3_free(hydra_key_copy);
        HINIC3_LOG(ERR, FLOW, "hinic3load flow error, malloc memory for hydra key data failed");
        return -1;
    }

    memcpy(hydra_key_copy->item_data, hydra_key_new->item_data, hydra_key_copy->item_data_size);

    /* 将hydra_item加入到链表中 */
    if (key->hydra_key_head == NULL) {
        key->hydra_key_head = hydra_key_copy;
    } else {
        key->hydra_key_tail->next = hydra_key_copy;
    }

    /* 调整链尾指针和计数 */
    key->hydra_key_tail = hydra_key_copy;
    key->hydra_key_count++;

    return 0;
}

static void hinic3_parse_vlan_mask(const struct rte_flow_item *item, struct hinic3_key_ip *ip,
                                 struct hinic3_conntrack_full_key *key)
{
    const struct rte_flow_item_vlan *vlan = item->mask;
    uint16_t *outer_vid = &(((struct hinic3_key_vid_5tuple *)key->key.key)->outer_vid);
    *outer_vid = vlan->tci;
    if (vlan->inner_type != 0) {
        ip->inner_type = vlan->inner_type;
    }
    key->key.hdr_flags.has_vlan = 1;
}

static void hinic3_parse_tcp_mask(const struct rte_flow_item *item, hinic3_be16 *port_src, hinic3_be16 *port_dst,
                                struct hinic3_conntrack_full_key *key)
{
    const struct rte_flow_item_tcp *tcp_spec = item->mask;
            
    *port_src = tcp_spec->hdr.src_port;
    *port_dst = tcp_spec->hdr.dst_port;
    key->key.meta.protocol = IPPROTO_TCP;
    key->key.hdr_flags.has_tcp = 1;
}

static void hinic3_parse_ipv4_mask(const struct rte_flow_item *item, struct hinic3_conntrack_full_key *key)
{
    const struct rte_flow_item_ipv4 *ipv4 = item->mask;
    struct hinic3_key_5tuple *tuple = NULL;
    struct hinic3_key_raw_ip *raw_ip = NULL;

    uint8_t protocol = ipv4->hdr.next_proto_id;
    if (hinic3_card_mod_get() == PROG_MODE) {
        tuple = (struct hinic3_key_5tuple *)key->key.key;
        (void)memcpy(&tuple->ip.src, &ipv4->hdr.src_addr, sizeof(ipv4->hdr.src_addr));
        (void)memcpy(&tuple->ip.dst, &ipv4->hdr.dst_addr, sizeof(ipv4->hdr.dst_addr));
        key->key.meta.protocol = protocol;
        return;
    }

    if (protocol == IPPROTO_TCP || protocol == IPPROTO_UDP || protocol == IPPROTO_ICMP || protocol == IPPROTO_ICMPV6) {
        tuple = (struct hinic3_key_5tuple *)key->key.key;
        (void)memcpy(&tuple->ip.src, &ipv4->hdr.src_addr, sizeof(ipv4->hdr.src_addr));
        (void)memcpy(&tuple->ip.dst, &ipv4->hdr.dst_addr, sizeof(ipv4->hdr.dst_addr));
        key->key.meta.protocol = protocol;
    } else {
        raw_ip = (struct hinic3_key_raw_ip *)key->key.key;
        (void)memcpy(&raw_ip->ip.src, &ipv4->hdr.src_addr, sizeof(ipv4->hdr.src_addr));
        (void)memcpy(&raw_ip->ip.dst, &ipv4->hdr.dst_addr, sizeof(ipv4->hdr.dst_addr));
        raw_ip->protocol = ipv4->hdr.next_proto_id;
        key->key.hdr_flags.has_raw_protocol = 1;
    }
    return;
}

static void howff_parse_ipv6_mask(const struct rte_flow_item *item, struct hinic3_conntrack_full_key *key)
{
    const struct rte_flow_item_ipv6 *ipv6 = item->mask;
    struct hinic3_key_5tuple *tuple = NULL;
    struct hinic3_key_raw_ip *raw_ip = NULL;

    uint8_t protocol = ipv6->hdr.proto;
    if (hinic3_card_mod_get() == PROG_MODE) {
        tuple = (struct hinic3_key_5tuple *)key->key.key;
        (void)memcpy(&tuple->ip.src, ipv6->hdr.src_addr, sizeof(ipv6->hdr.src_addr));
        (void)memcpy(&tuple->ip.dst, ipv6->hdr.dst_addr, sizeof(ipv6->hdr.dst_addr));
        key->key.meta.protocol = protocol;
        return;
    }

    if (protocol == IPPROTO_TCP || protocol == IPPROTO_UDP || protocol == IPPROTO_ICMP || protocol == IPPROTO_ICMPV6) {
        tuple = (struct hinic3_key_5tuple *)key->key.key;
        (void)memcpy(&tuple->ip.src, ipv6->hdr.src_addr, sizeof(ipv6->hdr.src_addr));
        (void)memcpy(&tuple->ip.dst, ipv6->hdr.dst_addr, sizeof(ipv6->hdr.dst_addr));
        key->key.meta.protocol = protocol;
    } else {
        raw_ip = (struct hinic3_key_raw_ip *)key->key.key;
        (void)memcpy(&raw_ip->ip.src, ipv6->hdr.src_addr, sizeof(ipv6->hdr.src_addr));
        (void)memcpy(&raw_ip->ip.dst, ipv6->hdr.dst_addr, sizeof(ipv6->hdr.dst_addr));
        raw_ip->protocol = ipv6->hdr.proto;
        key->key.hdr_flags.has_raw_protocol = 1;
    }
    return;
}

static void hinic3_parse_ip_mask(const struct rte_flow_item *item, struct hinic3_conntrack_full_key *key)
{
    switch (item->type) {
        case RTE_FLOW_ITEM_TYPE_IPV4:
            hinic3_parse_ipv4_mask(item, key);
            key->key.hdr_flags.has_ipv4 = 1;
            break;
        case RTE_FLOW_ITEM_TYPE_IPV6:
            howff_parse_ipv6_mask(item, key);
            key->key.hdr_flags.has_ipv6 = 1;
            break;
        default:
            break;
    }
}

static void hinic3_parse_vxlan_mask(const struct rte_flow_item *item, struct hinic3_conntrack_full_key *key)
{
    const struct rte_flow_item_vxlan *vxlan = NULL;

    vxlan = item->mask;
    uint32_t *vxlan_vni = &(((struct hinic3_key_vxlan_vid_5tuple *)key->key.key)->vxlan.vni);
    *vxlan_vni = vxlan->vni[0] + (vxlan->vni[1] << HINIC3_BYTE_BITS) +
    (vxlan->vni[VXLAN_VNI_LAST_INDEX] << HINIC3_SHORT_BITS);
    key->key.hdr_flags.has_vxlan = 1;
}

static int hinic3_parse_in_port_id_mask(const struct rte_flow_item *item, struct hinic3_key_vxlan_vid_5tuple *port_key, struct hinic3_conntrack_full_key *key)
{
    const struct rte_flow_item_port_id *port_id_item = NULL;
    port_id_item = item->mask;

    if (port_id_item->id != 0 && port_id_item->id != UINT16_MAX && port_id_item->id != UINT32_MAX) {
        return -1;
    }

    port_key->input_port = port_id_item->id != 0 ? UINT16_MAX : 0;
    key->key.hdr_flags.has_port = 1;
    return 0;
}

static void hinic3_parse_udp_mask(const struct rte_flow_item *item, hinic3_be16 *port_src, hinic3_be16 *port_dst,
                                struct hinic3_conntrack_full_key *key)
{
    const struct rte_flow_item_udp *udp = item->mask;

    *port_src = udp->hdr.src_port;
    *port_dst = udp->hdr.dst_port;
    key->key.meta.protocol = IPPROTO_UDP;

    key->key.hdr_flags.has_udp = 1;
}

static int hinic3_parse_icmp_mask(const struct rte_flow_item *item, hinic3_be16 *port_src, hinic3_be16 *port_dst,
    struct hinic3_conntrack_full_key *key)
{
    const struct rte_flow_item_icmp *icmp = item->mask;
    uint8_t icmp_type = icmp->hdr.icmp_type;
    uint8_t icmp_code = icmp->hdr.icmp_code;
    uint16_t icmp_ident = icmp->hdr.icmp_ident;

    *port_src = ((icmp_type) << HINIC3_BIT_MID_MOVE_INDEX) | icmp_code;
    *port_dst = icmp_ident;

    switch (item->type) {
        case RTE_FLOW_ITEM_TYPE_ICMP:
            key->key.meta.protocol = IPPROTO_ICMP;
            key->key.hdr_flags.has_icmp = 1;
            break;
        case RTE_FLOW_ITEM_TYPE_ICMP6:
            key->key.meta.protocol = IPPROTO_ICMPV6;
            key->key.hdr_flags.has_icmp6 = 1;
            break;
        default:
            break;
    }
    return 0;
}

static void hinic3_parse_eth_mask(const struct rte_flow_item *item, struct hinic3_key_mac *mac, struct hinic3_key_ip *ip)
{
    const struct rte_flow_item_eth *eth = NULL;

    eth = item->mask;
    (void)memcpy(mac->dmac, &eth->dst, ETH_ALEN);
    (void)memcpy(mac->smac, &eth->src, ETH_ALEN);
    ip->ether_type = eth->type;
}

static int hinic3_parse_hydra_mask(const struct rte_flow_item *item, struct hinic3_conntrack_full_key *mask)
{
    const struct hydra_flow_item *hydra_key_new = NULL;
    const struct hydra_flow_item *hydra_mask_new = NULL;
    int ret = 0;
    /* mask为空指针则报错返回 */
    if (item->mask == NULL) {
        HINIC3_LOG(ERR, FLOW, "hwoffload fuzzy flow error, hydra mask is null");
        return -1;
    }
    hydra_mask_new = (const struct hydra_flow_item *)item->mask;
    hydra_key_new = (const struct hydra_flow_item *)item->spec;  

    /* 如果item_type非法，则不做处理 */
    if (!hinic3_flexda_flow_key_is_in_table(hydra_mask_new->item_type)) {
        HINIC3_LOG(ERR, FLOW, "hinic3load flow error, hydra key type is invalid");
        return -1;
    }

    ret = hinic3_insert_hydra_key(hydra_mask_new, &(mask->key));
    return ret;
}

static int hinic3_offload_parse_mask_sub(const struct rte_flow_item *item, struct hinic3_conntrack_full_key *mask, uint8_t *has_vxlan_item)
{
    struct hinic3_key_mac *mac = (struct hinic3_key_mac *)mask->key.key;
    struct hinic3_key_vxlan_vid_5tuple *port_key = (struct hinic3_key_vxlan_vid_5tuple *)mask->key.key;
    struct hinic3_key_ip *ip = (struct hinic3_key_ip *)((uint8_t *)mac + sizeof(struct hinic3_key_mac));
    hinic3_be16 *port_src = (hinic3_be16 *)((uint8_t *)ip + sizeof(struct hinic3_key_ip));
    hinic3_be16 *port_dst = (hinic3_be16 *)((uint8_t *)port_src + sizeof(hinic3_be16));

    switch (item->type) {
        case RTE_FLOW_ITEM_TYPE_VOID:
            return hinic3_parse_hydra_mask(item, mask);
            break;
        case RTE_FLOW_ITEM_TYPE_ETH:
            (void)hinic3_parse_eth_mask(item, mac, ip);
            mask->key.hdr_flags.has_eth = 1;
            break;
        case RTE_FLOW_ITEM_TYPE_VLAN:
            (void)hinic3_parse_vlan_mask(item, ip, mask);
            break;
        case RTE_FLOW_ITEM_TYPE_IPV4:
        case RTE_FLOW_ITEM_TYPE_IPV6:
            (void)hinic3_parse_ip_mask(item, mask);
            break;
        case RTE_FLOW_ITEM_TYPE_VXLAN:
            (void)hinic3_parse_vxlan_mask(item, mask);
            *has_vxlan_item = 1;
        break;
        case RTE_FLOW_ITEM_TYPE_UDP:
            (void)hinic3_parse_udp_mask(item, port_src, port_dst, mask);
            break;
        case RTE_FLOW_ITEM_TYPE_TCP:
            (void)hinic3_parse_tcp_mask(item, port_src, port_dst, mask);
            break;
        case RTE_FLOW_ITEM_TYPE_PORT_ID:
            return hinic3_parse_in_port_id_mask(item, port_key, mask);
            break;
        case RTE_FLOW_ITEM_TYPE_ICMP:
        case RTE_FLOW_ITEM_TYPE_ICMP6:
            return hinic3_parse_icmp_mask(item, port_src, port_dst, mask);
            break;
        default:
            break;
    }

    return 0;
}

int hinic3_parse_hydra_key(const struct rte_flow_item *item, struct hinic3_conntrack_full_key *key)
{
    if (item->spec == NULL)
        return 0;

    int ret = 0;
    /* Wdiscarded-qualifiers无法避免，局部忽略此告警 */
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wdiscarded-qualifiers"
    struct hydra_flow_item *hydra_key_new = (const struct hydra_flow_item *)item->spec;
    #pragma GCC diagnostic pop

    /* 如果item_type非法，则不做处理 */
    if (!hinic3_flexda_flow_key_is_in_table(hydra_key_new->item_type)) {
        HINIC3_LOG(ERR, FLOW, "hinic3load flow error, hydra key type is invalid");
        return -1;
    }

    /* 将新的hydra_item加入到链尾 */
    ret = hinic3_insert_hydra_key(hydra_key_new, &(key->key));
    return ret;
}

static int hinic3_offload_parse_key_sub(const struct rte_flow_item *item, struct hinic3_conntrack_full_key *key,
    uint8_t *has_vxlan_item)
{
    struct hinic3_key_mac *mac = (struct hinic3_key_mac *)key->key.key;
    struct hinic3_key_vxlan_vid_5tuple *port_key = (struct hinic3_key_vxlan_vid_5tuple *)key->key.key;
    struct hinic3_key_ip *ip = (struct hinic3_key_ip *)((uint8_t *)mac + sizeof(struct hinic3_key_mac));
    hinic3_be16 *port_src = (hinic3_be16 *)((uint8_t *)ip + sizeof(struct hinic3_key_ip));
    hinic3_be16 *port_dst = (hinic3_be16 *)((uint8_t *)port_src + sizeof(hinic3_be16));

    switch (item->type) {
        case RTE_FLOW_ITEM_TYPE_VOID:
            (void)hinic3_parse_hydra_key(item, key);
            break;       
        case RTE_FLOW_ITEM_TYPE_ETH:
            (void)hinic3_parse_eth_key(item, mac, ip, key);
            break;
        case RTE_FLOW_ITEM_TYPE_VLAN:
            (void)hinic3_parse_vlan_key(item, ip, key);
            break;
        case RTE_FLOW_ITEM_TYPE_IPV4:
        case RTE_FLOW_ITEM_TYPE_IPV6:
            (void)hinic3_parse_ip_key(item, key);
            break;
        case RTE_FLOW_ITEM_TYPE_VXLAN:
            /* 报文外层key忽略不下发。
             * 当item遍历到item vxlan时，说明此前解析的key为外层key，将已解析的内容清零。 */
            (void)hinic3_clear_eth_key(mac, ip);
            (void)hinic3_clear_ip_key(ip);
            (void)hinic3_clear_udp_key(port_src, port_dst, key);
            (void)hinic3_parse_vxlan_key(item, key);
            *has_vxlan_item = 1;
            break;
        case RTE_FLOW_ITEM_TYPE_UDP:
            (void)hinic3_parse_udp_key(item, port_src, port_dst, key);
            break;
        case RTE_FLOW_ITEM_TYPE_TCP:
            (void)hinic3_parse_tcp_key(item, port_src, port_dst, key);
            break;
        case RTE_FLOW_ITEM_TYPE_PORT_ID:
            if (hinic3_parse_in_port_id(item, port_key, key) != 0) {
                return -1;
            }
            break;
        case RTE_FLOW_ITEM_TYPE_ICMP:
        case RTE_FLOW_ITEM_TYPE_ICMP6:
            (void)hinic3_parse_icmp_key(item, port_src, port_dst, key);
            break;
        default:
            HINIC3_LOG(DEBUG, FLOW, "Invalid flow item type %d.", item->type);
            break;
    }

    return 0;
}

static struct hydra_flow_item *hinic3_merge_hydra_key(struct hydra_flow_item *hydra_key_head1,
    struct hydra_flow_item *hydra_key_head2)
{
    struct hydra_flow_item *merge_head = NULL;
    struct hydra_flow_item *temp_node = NULL;
    struct hydra_flow_item *temp_node1 = NULL;
    struct hydra_flow_item *temp_node2 = NULL;
    struct hydra_flow_item *dummpy_head = (struct hydra_flow_item *)hinic3_malloc(sizeof(struct hydra_flow_item), HINIC3_FLOWS);
    if (dummpy_head == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3load flow error, malloc memory for dummpy head failed");
        return NULL;
    }

    temp_node = dummpy_head;
    temp_node1 = hydra_key_head1;
    temp_node2 = hydra_key_head2;

    while (temp_node1 != NULL && temp_node2 != NULL) {
        if (temp_node1->item_type <= temp_node2->item_type) {
            temp_node->next = temp_node1;
            temp_node1 = temp_node1->next;
        } else {
            temp_node->next = temp_node2;
            temp_node2 = temp_node2->next;
        }
        temp_node = temp_node->next;
    }

    if (temp_node1 != NULL) {
        temp_node->next = temp_node1;
    } else if (temp_node2 != NULL) {
        temp_node->next = temp_node2;
    }

    merge_head = dummpy_head->next;
    hinic3_free(dummpy_head);
    return merge_head;
}

static struct hydra_flow_item *hinic3_sort_hydra_key(struct hydra_flow_item *hydra_key_head,
    struct hydra_flow_item *hydra_key_tail)
{
    struct hydra_flow_item *slow = NULL;
    struct hydra_flow_item *fast = NULL;
    struct hydra_flow_item *mid = NULL;
    if (hydra_key_head == NULL) {
        return hydra_key_head;
    }

    if (hydra_key_head->next == hydra_key_tail) {
        hydra_key_head->next = NULL;
        return hydra_key_head;
    }

    slow = hydra_key_head;
    fast = hydra_key_head;

    while (fast != hydra_key_tail) {
        slow = slow->next;
        fast = fast->next;
        while (fast != hydra_key_tail) {
            fast = fast->next;
        }
    }

    mid = slow;
    return hinic3_merge_hydra_key(hinic3_sort_hydra_key(hydra_key_head, mid),
    hinic3_sort_hydra_key(mid, hydra_key_tail));
}

void hinic3_init_hydra_key(struct hinic3_conntrack_full_key *key)
{
    struct hinic3_conntrack_key *ct_key = &(key->key);
    ct_key->hydra_key_head = NULL;
    ct_key->hydra_key_tail = NULL;
    ct_key->hydra_key_count = 0;
}

void hinic3_process_hydra_key(struct hinic3_conntrack_full_key *key)
{
    if (key->key.hydra_key_head == NULL) {
        return;
    }

    struct hydra_flow_item *hydra_key_head = key->key.hydra_key_head;
    key->key.hydra_key_head = hinic3_sort_hydra_key(hydra_key_head, NULL);
    struct hydra_flow_item *hydra_key_tail = key->key.hydra_key_head;
    while (hydra_key_tail->next != NULL) {
        hydra_key_tail = hydra_key_tail->next;
    }
    key->key.hydra_key_tail = hydra_key_tail;
}

int hinic3_offload_parse_key(const struct rte_flow_item pattern[], struct hinic3_conntrack_full_key *key,
    uint8_t *has_vxlan_item)
{
    const struct rte_flow_item *item = NULL;
    int ret = 0;
    if (hinic3_card_mod_get() == PROG_MODE) {
        hinic3_init_hydra_key(key);
    }
    item = next_no_end_pattern(pattern, NULL);
    while (item) {
        if (item->spec == NULL) {
            HINIC3_LOG(INFO, FLOW, "rte_flow_item->spec is NULL.");
            item = next_no_end_pattern(pattern, item);
            continue;
        }

        ret = hinic3_offload_parse_key_sub(item, key, has_vxlan_item);
        if (ret != 0) {
            HINIC3_LOG(ERR, FLOW, "hinic3_offload_parse_key_sub failed.");
            return -1;
        }
        item = next_no_end_pattern(pattern, item);
    }
    if (hinic3_card_mod_get() == PROG_MODE) {
        hinic3_process_hydra_key(key);
    }
    hinic3_trace_flow_info_update(HINIC3_FLOW_PKT_KEY_RESOLVE_DONE_TRACE, &key->key);
    return 0;
}

int hinic3_offload_parse_mask(const struct rte_flow_item pattern[], struct hinic3_conntrack_full_key *mask, uint8_t *has_vxlan_item)
{
    const struct rte_flow_item *item = NULL;
    int ret = 0;
    hinic3_init_hydra_key(mask);
    
    item = next_no_end_pattern(pattern, NULL);
    while (item) {
        if (item->mask == NULL) {
            HINIC3_LOG(INFO, FLOW, "rte_flow_item->mask is NULL.\n");
            item = next_no_end_pattern(pattern, item);
            continue;
        }
        if (item->spec == NULL) {
            HINIC3_LOG(INFO, FLOW, "rte_flow_item->spec is NULL.\n");
            item = next_no_end_pattern(pattern, item);
            continue;
        }

        ret = hinic3_offload_parse_mask_sub(item, mask, has_vxlan_item);
        if (ret != 0) {
            HINIC3_LOG(ERR, FLOW, "hinic3_offload_parse_key_sub failed.");
            return -1;
        }
        item = next_no_end_pattern(pattern, item);
    }

    hinic3_process_hydra_key(mask);
    return 0;
}

static void hinic3_offload_parse_args(struct hinic3_nlattr *hinic3_args, const struct hinic3_conntrack_full_key *key,
                                          clock_t time, uint32_t flow_hash)
{
    hinic3_nlattr_put_unspec(hinic3_args, HINIC3_FLOW_ARG_NO_CT_UFID, key, sizeof(struct hinic3_conntrack_full_key));
    hinic3_nlattr_put_u64(hinic3_args, HINIC3_FLOW_ARG_TIME, time);
    hinic3_nlattr_put_u32(hinic3_args, HINIC3_FLOW_ARG_FLOW_HASH, flow_hash);
}

int hinic3_process_flow_key(const struct rte_flow_item pattern[], struct hinic3_flow_offload_param *param,
    struct rte_flow *mega_flow, struct hinic3_dpif_flow *flow, uint8_t *has_vxlan_item)
{
    int ret;
    struct hinic3_conntrack_full_key *key = &mega_flow->key;
    key->key.table_id = mega_flow->table_id;
    key->key.meta.key_len = sizeof(struct hinic3_conntrack_full_key) - sizeof(struct hinic3_conntrack_key);
    key->key.meta.tcp_udp_flag = HINIC3_CT_DEFAULT_FLAG;

    ret = hinic3_offload_parse_key(pattern, key, has_vxlan_item);
    if (ret != 0) {
        return -1;
    }
    flow->key = param->hinic3_key.data;
    (void)hinic3_offload_flow_construct_key_entrance(&key->key, &param->hinic3_key);
    flow->key_len = param->hinic3_key.used_len;

    hinic3_trace_flow_info_update(HINIC3_FLOW_OFFLOAD_KEY_PROCESS_DONE_TRACE, &key->key);
    return ret;
}

static void hinic3_calculate_hydra_key_and_mask(struct hinic3_conntrack_full_key *key, struct hinic3_conntrack_full_key *mask,
    struct hinic3_conntrack_full_key *raw_key)
{
    //计算非自定义key和mask的结果，放到key中, 同时保留raw key值
    raw_key->key.meta_num = key->key.meta_num;
    raw_key->key.table_id = key->key.table_id;
    raw_key->key.hdr_flags_num = key->key.hdr_flags_num;
    key->key.meta.protocol = mask->key.meta.protocol & key->key.meta.protocol;
    for (int32_t i = 0; i < key->key.meta.key_len; i++) {
        ((uint8_t *)raw_key->key.key)[i] = ((uint8_t *)key->key.key)[i];
        ((uint8_t *)key->key.key)[i] = ((uint8_t *)mask->key.key)[i] & ((uint8_t *)key->key.key)[i];
    }
    //计算自定义key和mask的结果，放到key中, 同时保留raw key值
    hinic3_init_hydra_key(raw_key);
    struct hydra_flow_item *hydra_key_current = key->key.hydra_key_head;
    struct hydra_flow_item *hydra_mask_current = mask->key.hydra_key_head;
    if (hydra_key_current == NULL || hydra_mask_current == NULL) {
        return;
    }
    while (hydra_key_current->next != NULL) {
        if (hinic3_insert_hydra_key(hydra_key_current, &(raw_key->key)) != 0) {
            hinic3_add_error_stats(HINIC3_FLOW_ERROR_FLEXDA_FUZZY_FLOW_COPY_RAW_KEY_ITEM_FAIL, 1);
        }
        for (size_t i = 0; i < hydra_key_current->item_data_size; i++) {
            ((uint8_t *)hydra_key_current->item_data)[i] = ((uint8_t *)hydra_mask_current->item_data)[i] & ((uint8_t *)hydra_key_current->item_data)[i]; 
        }
        hydra_key_current = hydra_key_current->next;
        hydra_mask_current = hydra_mask_current->next;
    }
    hinic3_process_hydra_key(raw_key);
}

int hinic3_process_flow_mask(const struct rte_flow_item pattern[], struct hinic3_flow_offload_param *param,
    struct fuzzy_flow *mega_flow, struct hinic3_dpif_flow *flow, uint8_t *has_vxlan_item)
{
    int ret;
    struct hinic3_conntrack_full_key *key = &mega_flow->flow.key;
    struct hinic3_conntrack_full_key *mask = &mega_flow->mask;
    struct hinic3_conntrack_full_key *raw_key = &mega_flow->raw_key;
    mask->key.meta.key_len = sizeof(struct hinic3_conntrack_full_key) - sizeof(struct hinic3_conntrack_key);
    raw_key->key.meta.key_len = sizeof(struct hinic3_conntrack_full_key) - sizeof(struct hinic3_conntrack_key);

    ret = hinic3_offload_parse_mask(pattern, mask, has_vxlan_item);
    if (ret != 0)
        return -1;

    hinic3_calculate_hydra_key_and_mask(key, mask, raw_key);
    flow->mask = param->hinic3_mask.data;
    (void)hinic3_offload_flow_construct_key_entrance(&mask->key, &param->hinic3_mask);
    flow->mask_len = param->hinic3_mask.used_len;
    flow->mask_present = 1;

    return ret;
}

static int hinic3_offload_parse_flow_action(const struct rte_flow_action actions[],
    struct hinic3_flow_offload_param *param, struct rte_flow *mega_flow, struct hinic3_dpif_flow *flow,
    uint8_t has_vxlan_item)
{
    int ret;
    uint8_t mirror_dir_flag = (has_vxlan_item == 0) ? HINIC3_SESSION_TX : HINIC3_SESSION_RX;

    ret = hinic3_offload_parse_action(actions, mega_flow, param, mirror_dir_flag);
    if (ret != 0) {
        hinic3_trace_flow_info_update(HINIC3_FLOW_PROCESS_OFFLOAD_ACTION_ERROR_TRACE, &mega_flow->key.key);
        return ret;
    }

    if (param->cur_actions.has_output == false && param->cur_actions.has_dp_hash == false) {
        return -EINVAL;
    }

    flow->actions = param->cur_actions.act_nla.data;
    flow->action_len = param->cur_actions.act_nla.used_len;

    hinic3_trace_flow_info_update(HINIC3_FLOW_PROCESS_OFFLOAD_ACTION_DONE_TRACE, &mega_flow->key.key);
    return ret;
}

static void hinic3_offload_parse_flow_args(struct hinic3_flow_offload_param *param, struct rte_flow *flow,
                                          clock_t time, uint32_t flow_hash)
{
    hinic3_offload_parse_args(&param->hinic3_args, &flow->key, time, flow_hash);
    hinic3_trace_flow_info_update(HINIC3_AGENT_IS_FLOW_READY_PUT_DONE_TRACE, &flow->key.key);
}

static int hinic3_alloc_get_f(struct hinic3_dpif_flow_for_get *f)
{
    f->key = hinic3_calloc(1, HINIC3_DFX_FLOW_ALLOC_MEM_BASE_SIZE, HINIC3_COMMAND);
    if (f->key == NULL) {
        HINIC3_LOG(WARNING, AGENT, "calloc memory for hw flow key error.");
        return -1;
    }

    f->actions = hinic3_calloc(1, HINIC3_DFX_FLOW_ALLOC_MEM_BASE_SIZE, HINIC3_COMMAND);
    if (f->actions == NULL) {
        HINIC3_LOG(WARNING, AGENT, "calloc memory for hw flow actions error.");
        hinic3_free(f->key);
        f->key = NULL;
        return -1;
    }

    f->mask = hinic3_calloc(1, HINIC3_DFX_FLOW_ALLOC_MEM_BASE_SIZE, HINIC3_COMMAND);
    if (f->mask == NULL) {
        HINIC3_LOG(WARNING, AGENT, "calloc memory for hw flow mask error.");
        hinic3_free(f->key);
        f->key = NULL;
        hinic3_free(f->actions);
        f->actions = NULL;
        return -1;
    }

    return 0;
}

static void hinic3_free_get_f(struct hinic3_dpif_flow_for_get *f)
{
    if (f->key != NULL) {
        hinic3_free(f->key);
        f->key = NULL;
    }
    f->key_len = 0;

    if (f->actions != NULL) {
        hinic3_free(f->actions);
        f->actions = NULL;
    }
    f->action_len = 0;

    if (f->mask != NULL) {
        hinic3_free(f->mask);
        f->mask = NULL;
    }
    f->mask_len = 0;
}

static int hinic3_insert_rte_flow(struct hash_table_node *rte_bucket, struct rte_flow *mega_flow,
                                 struct rte_flow_error *error)
{
    int ret = 0;
    struct rte_flow *flow = NULL;
    struct hinic3_dpif_flow_for_get hiovs_get = { 0 };

    flow = hinic3_get_offloaded_rte_flow(rte_bucket, &mega_flow->key, mega_flow->flow_hash);
    if (flow != NULL) {
        if (flow->flags.is_offload == 1) {
            ret = hinic3_alloc_get_f(&hiovs_get);
            if (ret != 0) {
                HINIC3_LOG(ERR, FLOW, "hinic3load insert rte flow alloc failed.");
                return -1;
            }
            ret = hinic3_flow_get_by_ufid(flow->hw_ufid, &hiovs_get, flow->table_id);
            hinic3_free_get_f(&hiovs_get);
            if (ret == 0) {
                hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_OFFLOAD_CHECK_OFFLOADING_REPEATED, 1);
                return rte_flow_error_set(error, EEXIST, RTE_FLOW_ERROR_TYPE_UNSPECIFIED,
                    NULL, HINIC3_EMC_ERROR_MSG_REPEATED);
            } else if (ret == HINIC3_NO_FOUND_FLOW) {
                // 如果dpak流表卸载完成，但是硬件流表不存在，则再下一遍流表给硬件
                hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_OFFLOAD_CHECK_OFFLOADING_EXIST_GAP, 1);
                return 0;
            }
        } else {
            hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_OFFLOAD_CHECK_OFFLOADING_ONGOING, 1);
            return rte_flow_error_set(error, EEXIST, RTE_FLOW_ERROR_TYPE_UNSPECIFIED,
                NULL, HINIC3_EMC_ERROR_MSG_REPEATED);
        }
    }

    ret = hinic3_insert_rte_flow_in_hmap(rte_bucket, mega_flow);
    if (ret != 0) {
        hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_OFFLOAD_INSERT_RTE_FLOW_IN_HMAP, 1);
        return rte_flow_error_set(error, EPERM, RTE_FLOW_ERROR_TYPE_UNSPECIFIED, NULL, HINIC3_EMC_ERROR_MSG_ITEM);
    }

    hinic3_trace_flow_info_update(HINIC3_FLOW_OFFLOAD_KEY_INSERT_TABLE_DONE_TRACE, &mega_flow->key.key);
    return 0;
}

static void hinic3_ovs_offload_args_put(const struct rte_flow_item *item, struct hinic3_nlattr *nla)
{
    for (int i = 0; i < HINIC3_FLOW_ITEMS_NUM; ++i) {
        if (item[i].type == RTE_FLOW_ITEM_TYPE_END) {
            break;
        }

        if (item[i].type == HINIC3_FLOW_ITEM_TYPE_SW_UFID) {
            hinic3_nlattr_put_unspec(nla, HINIC3_FLOW_ARG_SW_UFID, item[i].spec, sizeof(hinic3_u128));
        }
        if (item[i].type == HINIC3_FLOW_ITEM_TYPE_POLICY_ID) {
            hinic3_nlattr_put_unspec(nla, HINIC3_FLOW_ARG_POLICY_ID, item[i].spec, sizeof(hinic3_u128));
        }
    }
}

static void hinic3_init_param_buff(struct hinic3_flow_offload_param *param)
{
    hinic3_nlattr_init(&param->hinic3_key, param->offload_buff.key_buf, HINIC3_MSG_MAX_BUF);
    hinic3_nlattr_init(&param->hinic3_mask, param->offload_buff.mask_buf, HINIC3_MSG_MAX_BUF);
    hinic3_nlattr_init(&param->cur_actions.act_nla, param->offload_buff.actions_buf, HINIC3_MSG_MAX_BUF);
    hinic3_nlattr_init(&param->hinic3_args, param->offload_buff.args_buf, HINIC3_MSG_MAX_BUF);
}

static void hinic3_proces_put_args(struct hinic3_flow_offload_param *param, const struct rte_flow_item pattern[] HINIC3_UNUSED,
    struct rte_flow *flow, clock_t start_t)
{
    (void)hinic3_offload_parse_flow_args(param, flow, start_t, flow->flow_hash);
    (void)hinic3_ovs_offload_args_put(pattern, &param->hinic3_args);
}

struct hash_table_node *hinic3_get_offload_flow_bucket(uint32_t flow_hash, uint32_t table_id)
{
    if (table_id == 0) {
        return hinic3_get_flow_bucket(flow_hash);
    }
    return hinic3_flexda_get_table_flow_bucket(table_id, flow_hash);
}
 
static int hinic3_process_offload_flow(const struct rte_flow_item pattern[],
    const struct rte_flow_action actions[], struct rte_flow *mega_flow, struct hinic3_flow_agent_db *hw_offload,
    struct rte_flow_error *error)
{
    uint32_t flow_hash;
    uint8_t has_vxlan_item = 0;
    clock_t start_t = { 0 };
    struct hinic3_flow_offload_param param = { 0 };
    struct hinic3_dpif_flow flow = { 0 };
    param.hw_offload = hw_offload;
    struct hash_table_node *rte_bucket = NULL;
    hinic3_init_param_buff(&param);

    if (HINIC3_UNLIKELY(hinic3_is_offload_measure_alive() == true)) {
        start_t = clock();
    }

    /* deal with key */
    int ret = hinic3_process_flow_key(pattern, &param, mega_flow, &flow, &has_vxlan_item);
    if (HINIC3_UNLIKELY(ret != 0)) {
        hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_OFFLOAD_PROCESS_FLOW_KEY, 1);
        return rte_flow_error_set(error, EINVAL, RTE_FLOW_ERROR_TYPE_ITEM_SPEC, NULL, HINIC3_EMC_ERROR_MSG_FLOW);
    }

    /* deal with mask */
    if (IS_FLEXDA_FUZZY_TABLE(mega_flow->table_id)) {
        ret = hinic3_process_flow_mask(pattern, &param, (struct fuzzy_flow *)mega_flow, &flow, &has_vxlan_item);
        if (HINIC3_UNLIKELY(ret != 0)) {
            hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_OFFLOAD_PROCESS_FLOW_MASK, 1);
            return rte_flow_error_set(error, EINVAL, RTE_FLOW_ERROR_TYPE_ITEM_SPEC, NULL, HINIC3_EMC_ERROR_MSG_FLOW);
        }
    }
    /* calculate hash value*/
    flow_hash = hinic3_hash_generate_entrance(&mega_flow->key.key);

    rte_bucket = hinic3_get_flow_bucket(flow_hash);
    hinic3_trace_flow_info_update(HINIC3_FLOW_OFFLOAD_KEY_HASH_DONE_TRACE, &mega_flow->key.key);

    hinic3_spinlock_lock(&rte_bucket->spinlock);
    mega_flow->flow_hash = flow_hash;
    /* Check whether the offloading is repeated. If yes, a failure message is returned. */
    ret = hinic3_insert_rte_flow(rte_bucket, mega_flow, error);
    if (ret != 0) {
        hinic3_trace_flow_info_update(HINIC3_FLOW_OFFLOAD_KEY_INSERT_TABLE_ERROR_TRACE, &mega_flow->key.key);
        hinic3_spinlock_unlock(&rte_bucket->spinlock);
        return ret;
    }

    /* deal with action */
    ret = hinic3_offload_parse_flow_action(actions, &param, mega_flow, &flow, has_vxlan_item);
    if (HINIC3_UNLIKELY(ret != 0)) {
        hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_OFFLOAD_PARSE_FLOW_ACTION, 1);
        ret = rte_flow_error_set(error, EINVAL, RTE_FLOW_ERROR_TYPE_ACTION_CONF, NULL, HINIC3_EMC_ERROR_MSG_ACTION);
        goto fail;
    }

    /* deal with args */
    (void)hinic3_proces_put_args(&param, pattern, mega_flow, start_t);

    ret = hinic3_flow_put(&flow, param.hinic3_args.data, param.hinic3_args.used_len, mega_flow->table_id);
    if (ret != 0) {
        hinic3_trace_flow_info_update(HINIC3_FLOW_AGENT_ERROR_HARDWARE_FAIL_TRACE, &mega_flow->key.key);
        hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_OFFLOAD_CALL_HARDWARE_FUNC, 1);
        (void)rte_flow_error_set(error, -ret, RTE_FLOW_ERROR_TYPE_STATE, NULL, HINIC3_EMC_ERROR_MSG_OFFLOAD_HOVS);
        goto fail;
    }
    hinic3_spinlock_unlock(&rte_bucket->spinlock);

    hinic3_trace_flow_info_update(HINIC3_AGENT_FLOW_PUT_DONE_TRACE, &mega_flow->key.key);
    return 0;
fail:
    hinic3_del_rte_flow_if_offload_fail(rte_bucket, mega_flow);
    hinic3_spinlock_unlock(&rte_bucket->spinlock);
    return ret;
}

struct rte_flow *hinic3_offload_flow(const struct rte_flow_item pattern[], const struct rte_flow_action actions[],
    struct hinic3_flow_agent_db *hw_offload, struct rte_flow_error *error, uint8_t table_id)
{
    int ret;

    struct rte_flow *alloc_flow = hinic3_rte_or_fuzzy_flow_alloc(table_id);
    if (HINIC3_UNLIKELY(alloc_flow == NULL)) {
        hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_EMC_FLOW_ALLOC, 1);
        rte_flow_error_set(error, ENOMEM, RTE_FLOW_ERROR_TYPE_UNSPECIFIED, NULL, "alloc mem failed");
        return NULL;
    }

    alloc_flow->table_id = table_id;
    ret = hinic3_process_offload_flow(pattern, actions, alloc_flow, hw_offload, error);
    if (ret != 0) {
        hinic3_rte_or_fuzzy_flow_dealloc(alloc_flow);
        return NULL;
    }

    return alloc_flow;
}

static int hinic3_check_flow_modify_valid(struct rte_flow *remain_flow, struct rte_flow_error *error)
{
    if (remain_flow == NULL)
    {
        hinic3_add_error_stats(HINIC3_FLOW_ERROR_NO_EXIST_FLOW, 1);
        rte_flow_error_set(error, EINVAL, RTE_FLOW_ERROR_TYPE_ITEM, NULL, "No exist flow");
        return -1;
    }

    if (remain_flow->flags.is_offload == 0)
    {
        hinic3_add_error_stats(HINIC3_FLOW_ERROR_FLOW_NOT_READY, 1);
        rte_flow_error_set(error, EBUSY, RTE_FLOW_ERROR_TYPE_STATE, NULL, "flow not ready");
        return -1;
    }

    if (remain_flow->flags.is_mem_used == 0)
    {
        HINIC3_LOG(ERR, FLOW, "hinic3load flow error, flag(is_mem_used) error.");
        rte_flow_error_set(error, EBUSY, RTE_FLOW_ERROR_TYPE_UNSPECIFIED, NULL, "flow has gap");
        return -1;
    }

    return 0;
}

static int hinic3_modify_session(struct rte_flow *remain_flow, struct rte_flow *parse_flow,
                                     struct rte_flow_error *error)
{
    int ret;

    if (remain_flow->flags.is_sample != 0)
    {
        ret = hinic3_del_rte_flow_in_session(remain_flow);
        if (ret != 0)
        {
            hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_MODIFY_DEL_FLOW_IN_SESSION, 1);
            return rte_flow_error_set(error, EPERM, RTE_FLOW_ERROR_TYPE_STATE,
                                      NULL, HINIC3_EMC_ERROR_MSG_MODIFY_SESSION);
        }
    }

    remain_flow->session_id = parse_flow->session_id;
    remain_flow->flags.is_sample = parse_flow->flags.is_sample;
    return 0;
}

static int hinic3_modify_flow_qos(struct rte_flow *remain_flow, struct rte_flow *parse_flow,
    struct rte_flow_error *error __rte_unused)
{
    int ret;

    if (remain_flow->flags.has_flow_qos == 1)
    {
        ret = hinic3_del_flow_qos_by_meter_id(remain_flow->meter_id);
        if (ret != 0)
            return ret;
    }

    remain_flow->meter_id = parse_flow->meter_id;
    remain_flow->flags.has_flow_qos = parse_flow->flags.has_flow_qos;
    return 0;
}

static int hinic3_modify_flow_sub(struct rte_flow *remain_flow, struct rte_flow *parse_flow,
                                      struct rte_flow_error *error)
{
    int ret;

    ret = hinic3_modify_session(remain_flow, parse_flow, error);
    if (ret != 0)
    {
        return -1;
    }
    ret = hinic3_modify_flow_qos(remain_flow, parse_flow, error);
    if (ret != 0)
    {
        return -1;
    }
    return 0;
}

static void hinic3_offload_parse_modify_flow_args(struct hinic3_flow_offload_param *param, struct rte_flow *flow,
                                                 uint32_t flow_hash)
{
    hinic3_nlattr_init(&param->hinic3_args, param->offload_buff.args_buf, HINIC3_MSG_MAX_BUF);
    hinic3_nlattr_put_unspec(&param->hinic3_args, HINIC3_FLOW_ARG_NO_CT_UFID,
                            &flow->key, sizeof(struct hinic3_conntrack_full_key));
    hinic3_nlattr_put_flag(&param->hinic3_args, HINIC3_FLOW_ARG_MODIFY);
    hinic3_nlattr_put_u32(&param->hinic3_args, HINIC3_FLOW_ARG_FLOW_HASH, flow_hash);
}

struct rte_flow *hinic3_modify_flow(const struct rte_flow_item pattern[], const struct rte_flow_action actions[],
                                        struct hinic3_flow_agent_db *hw_offload, struct rte_flow_error *error)
{
    int ret;
    uint32_t flow_hash;
    uint8_t has_vxlan_item = 0;
    struct hinic3_flow_offload_param param = {0};
    struct rte_flow parse_flow = {0};
    struct hinic3_dpif_flow dpif_flow = {0};
    param.hw_offload = hw_offload;
    struct hash_table_node *rte_bucket = NULL;
    struct rte_flow *remain_flow = NULL;
    hinic3_init_param_buff(&param);

    ret = hinic3_process_flow_key(pattern, &param, &parse_flow, &dpif_flow, &has_vxlan_item);
    if (HINIC3_UNLIKELY(ret != 0))
    {
        hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_OFFLOAD_PROCESS_FLOW_KEY, 1);
        rte_flow_error_set(error, EINVAL, RTE_FLOW_ERROR_TYPE_ITEM_SPEC, NULL, HINIC3_EMC_ERROR_MSG_FLOW);
        return NULL;
    }

    flow_hash = hinic3_hash_generate_entrance(&parse_flow.key.key);
    rte_bucket = hinic3_get_flow_bucket(flow_hash);
    hinic3_spinlock_lock(&rte_bucket->spinlock);

    remain_flow = hinic3_get_offloaded_rte_flow(rte_bucket, &parse_flow.key, flow_hash);

    ret = hinic3_check_flow_modify_valid(remain_flow, error);
    if (HINIC3_UNLIKELY(ret != 0))
    {
        goto err;
    }

    ret = hinic3_offload_parse_flow_action(actions, &param, &parse_flow, &dpif_flow, has_vxlan_item);
    if (HINIC3_UNLIKELY(ret != 0))
    {
        hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_OFFLOAD_PARSE_FLOW_ACTION, 1);
        rte_flow_error_set(error, EINVAL, RTE_FLOW_ERROR_TYPE_ACTION_CONF, NULL, HINIC3_EMC_ERROR_MSG_ACTION);
        goto err;
    }

    ret = hinic3_modify_flow_sub(remain_flow, &parse_flow, error);
    if (ret != 0)
    {
        goto err;
    }

    (void)hinic3_offload_parse_modify_flow_args(&param, &parse_flow, flow_hash);
    dpif_flow.hw_ufid = remain_flow->hw_ufid;
    ret = hinic3_flow_modify(&dpif_flow, param.hinic3_args.data, param.hinic3_args.used_len);
    if (ret != 0)
    {
        hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_OFFLOAD_CALL_HARDWARE_FUNC, 1);
        (void)rte_flow_error_set(error, -ret, RTE_FLOW_ERROR_TYPE_STATE, NULL, HINIC3_EMC_ERROR_MSG_MODIFY_HOVS);
        goto err;
    }

    hinic3_spinlock_unlock(&rte_bucket->spinlock);
    return remain_flow;
err:
    hinic3_spinlock_unlock(&rte_bucket->spinlock);
    return NULL;
}
