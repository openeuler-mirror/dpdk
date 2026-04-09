/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Huawei Technologies Co., Ltd
 */
#include "hinic3_flow_dump_item.h"
#include "hinic3_flow_dump_item_key.h"
#include "hinic3_flow_dump.h"
#include "hinic3_log.h"
#include "hinic3_packet_key_public.h"
#include "hinic3_util.h"
#include "hinic3_meminfo.h"

static void hinic3_free_rte_flow_item(struct rte_flow_item *item)
{
    if (item == NULL) {
        return;
    }
    if (item -> spec != NULL) {
        hinic3_free((void *)(uintptr_t)item -> spec);
        item -> spec = NULL;
    }
    if (item -> last != NULL) {
        hinic3_free((void *)(uintptr_t)item -> last);
        item -> last = NULL;
    }
    if (item -> mask != NULL) {
        hinic3_free((void *)(uintptr_t)item -> mask);
        item -> mask = NULL;
    }
}

void hinic3_free_one_flow_items(struct rte_flow_item* items, int item_num)
{
    for (int i = 0; i < item_num; ++i) {
        hinic3_free_rte_flow_item(&items[i]);
    }
}

int hinic3_dump_flow_item_insert(struct hinic3_dump_flow_info *flow, struct rte_flow_item *item)
{
    unsigned int index = flow->useful_item_index;

    if (index >= HINIC3_FLOW_DUMP_MAX_PATTERN) {
        return -1;
    }

    memcpy(&flow->items[index], item, sizeof(struct rte_flow_item));

    flow->useful_item_index++;
    hinic3_free(item);
    item = NULL;
    return 0;
}

static struct rte_flow_item_vxlan* hinic3_build_vxlan_item(uint32_t vni_value)
{
    struct rte_flow_item_vxlan* vxlan = hinic3_calloc(1, sizeof(struct rte_flow_item_vxlan), HINIC3_FLOWS);
    if (vxlan == NULL) {
        HINIC3_LOG(DEBUG, FLOW, "Malloc memmory for rte_flow_item_vxlan failed!");
        return NULL;
    }

    vni_value = vni_value >> HINIC3_BIT_MID_MOVE_INDEX;

    uint8_t vni[HINIC3_VNI_ARR_SIZE];
    vni[HINIC3_VNI_ARR_LOW] = vni_value & HINIC3_LOW_EIGHT_BIT_MASK;

    uint32_t vni_mid = (vni_value & HINIC3_MID_EIGHT_BIT_MASK);
    vni[HINIC3_VNI_ARR_MID] = vni_mid >> HINIC3_BIT_MID_MOVE_INDEX;

    uint32_t vni_high = vni_value & HINIC3_HIGH_EIGHT_BIT_MASK;
    vni[HINIC3_VNI_ARR_HIGH] = vni_high >> HINIC3_BIT_HIGH_MOVE_INDEX;

    memcpy(vxlan->vni, vni, sizeof(uint8_t) * HINIC3_VNI_ARR_SIZE);
    return vxlan;
}

static struct rte_flow_item_vlan *hinic3_build_vlan_item(uint32_t inner_vid, uint16_t ether_type,
    uint16_t ether_inner_type)
{
    struct rte_flow_item_vlan* vlan = hinic3_calloc(1, sizeof(struct rte_flow_item_vlan), HINIC3_FLOWS);
    if (vlan == NULL) {
        HINIC3_LOG(DEBUG, FLOW, "Malloc memmory for rte_flow_item_vlan failed!");
        return NULL;
    }

    if (ether_type != ETH_TYPE_VLAN) {
        vlan->inner_type = 0;
    } else {
        vlan->inner_type = htons(ether_inner_type);
    }
    vlan -> tci = inner_vid;
    return vlan;
}

static struct rte_flow_item_eth* hinic3_build_eth_item(uint16_t ether_type, const struct hinic3_key_mac* mac)
{
    struct rte_flow_item_eth* eth = hinic3_calloc(1, sizeof(struct rte_flow_item_eth), HINIC3_FLOWS);
    if (eth == NULL) {
        HINIC3_LOG(DEBUG, FLOW, "Malloc memmory for rte_flow_item_eth failed!");
        return NULL;
    }
    memcpy(&(eth ->dst), mac->dmac, sizeof(mac->dmac));
    memcpy(&(eth ->src), mac->smac, sizeof(mac->smac));
    eth->type = htons(ether_type);
    return eth;
}

static struct rte_flow_item_ipv4* hinic3_build_ipv4_item(const struct hinic3_key_ip *ip, uint16_t tcp_udp_flag)
{
    if (ip == NULL) {
        return NULL;
    }
    struct rte_flow_item_ipv4* ipv4 = hinic3_calloc(1, sizeof(struct rte_flow_item_ipv4), HINIC3_FLOWS);
    if (ipv4 == NULL) {
        HINIC3_LOG(DEBUG, FLOW, "Malloc memmory for rte_flow_item_eth failed!");
        return NULL;
    }
    memcpy(&ipv4 ->hdr.src_addr, &ip->src, sizeof(ipv4->hdr.src_addr));
    memcpy(&ipv4 ->hdr.dst_addr, &ip->dst, sizeof(ipv4->hdr.dst_addr));

    if (tcp_udp_flag == HINIC3_DUMP_UDP_FLAG) {
        ipv4->hdr.next_proto_id = HINIC3_UDP_PROTO;
    } else if (tcp_udp_flag == HINIC3_DUMP_TCP_FLAG) {
        ipv4->hdr.next_proto_id = HINIC3_TCP_PROTO;
    }

    return ipv4;
}

static struct rte_flow_item_ipv6* hinic3_build_ipv6_item(const struct hinic3_key_ip *ip, uint16_t tcp_udp_flag)
{
    if (ip == NULL) {
        return NULL;
    }
    struct rte_flow_item_ipv6* ipv6 = hinic3_calloc(1, sizeof(struct rte_flow_item_ipv6), HINIC3_FLOWS);
    if (ipv6 == NULL) {
        HINIC3_LOG(DEBUG, FLOW, "Malloc memmory for rte_flow_item_eth failed!");
        return NULL;
    }
    memcpy(&ipv6 ->hdr.src_addr, &ip->src, sizeof(ipv6->hdr.src_addr));
    memcpy(&ipv6 ->hdr.dst_addr, &ip->dst, sizeof(ipv6->hdr.dst_addr));

    if (tcp_udp_flag == HINIC3_DUMP_UDP_FLAG) {
        ipv6->hdr.proto = HINIC3_UDP_PROTO;
    } else if (tcp_udp_flag == HINIC3_DUMP_TCP_FLAG) {
        ipv6->hdr.proto = HINIC3_TCP_PROTO;
    }
    return ipv6;
}

static struct rte_flow_item_tcp* hinic3_build_tcp_item(uint16_t src_port, uint16_t dst_port)
{
    struct rte_flow_item_tcp* tcp = hinic3_calloc(1, sizeof(struct rte_flow_item_tcp), HINIC3_FLOWS);
    if (tcp == NULL) {
        HINIC3_LOG(DEBUG, FLOW, "Malloc memmory for rte_flow_item_tcp failed!");
        return NULL;
    }
    tcp->hdr.dst_port = dst_port;
    tcp->hdr.src_port = src_port;
    return tcp;
}

static struct rte_flow_item_udp* hinic3_build_udp_item(uint16_t src_port, uint16_t dst_port)
{
    struct rte_flow_item_udp* udp = hinic3_calloc(1, sizeof(struct rte_flow_item_udp), HINIC3_FLOWS);
    if (udp == NULL) {
        HINIC3_LOG(DEBUG, FLOW, "Malloc memmory for rte_flow_item_udp failed!");
        return NULL;
    }
    udp->hdr.dst_port = dst_port;
    udp->hdr.src_port = src_port;
    return udp;
}

static uint32_t* hinic3_build_inpou_port_item(uint32_t input_port)
{
    uint32_t* port = hinic3_calloc(1, sizeof(uint32_t), HINIC3_FLOWS);
    if (port == NULL) {
        HINIC3_LOG(DEBUG, FLOW, "Malloc memmory for inpou_port_item failed!");
        return NULL;
    }
    *port = input_port;
    return port;
}

static void* hinic3_build_flow_item_spec(enum rte_flow_item_type type, const struct hinic3_flow_dump_keys* keys)
{
    void *spec = NULL;

    switch (type) {
        case RTE_FLOW_ITEM_TYPE_PORT_ID:
            spec = hinic3_build_inpou_port_item(keys->input_port);
            break;
        case RTE_FLOW_ITEM_TYPE_ETH:
            spec = (void*)hinic3_build_eth_item(keys->ether_type, &(keys->mac));
            break;
        case RTE_FLOW_ITEM_TYPE_IPV4:
            spec = (void*)hinic3_build_ipv4_item(&(keys->ip), keys->tcp_udp_flag);
            break;
        case RTE_FLOW_ITEM_TYPE_IPV6:
            spec = (void*)hinic3_build_ipv6_item(&(keys->ip), keys->tcp_udp_flag);
            break;
        case RTE_FLOW_ITEM_TYPE_VLAN:
            spec = (void*)hinic3_build_vlan_item(keys ->inner_vid, keys->ether_type, keys->ether_inner_type);
            break;
        case RTE_FLOW_ITEM_TYPE_TCP:
            spec = (void*)hinic3_build_tcp_item(keys->port_src, keys->port_dst);
            break;
        case RTE_FLOW_ITEM_TYPE_UDP:
            spec = (void*)hinic3_build_udp_item(keys->port_src, keys->port_dst);
            break;
        case RTE_FLOW_ITEM_TYPE_VXLAN:
            spec = (void*)hinic3_build_vxlan_item(keys ->vni);
            break;
        default:
            break;
    }
    return spec;
}

struct rte_flow_item* hinic3_build_rte_flow_item(enum rte_flow_item_type type, const void *spec,
    const void *last, const void *mask)
{
    struct rte_flow_item* item = hinic3_calloc(1, sizeof(struct rte_flow_item), HINIC3_FLOWS);
    if (item == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3 flow dump: calloc for rte flow item failed");
        return NULL;
    }

    item->type = type;
    item->spec = spec;
    item->mask = mask;
    item->last = last;
    return item;
}

static struct rte_flow_item* hinic3_dump_flow_item_factory(enum rte_flow_item_type type,
                                                          const struct hinic3_flow_dump_keys* keys)
{
    if (keys == NULL) {
        return NULL;
    }

    void *spec = NULL;
    void *mask = NULL;
    struct rte_flow_item* item = NULL;

    spec = hinic3_build_flow_item_spec(type, keys);
    if (spec == NULL) {
        HINIC3_LOG(ERR, FLOW, "HINIC3 FLOW DUMP: malloc for item spec failed");
        return NULL;
    }

    item = hinic3_build_rte_flow_item(type, (void*)spec, NULL, mask);
    if (item == NULL) {
        hinic3_free(spec);
    }
    return item;
}

static struct rte_flow_item *hinic3_flow_dump_tcpudp_item_build(const struct hinic3_flow_dump_keys *keys)
{
    struct rte_flow_item *flow_item = NULL;
    if (keys->tcp_udp_flag == HINIC3_DUMP_TCP_FLAG) {
        flow_item = hinic3_dump_flow_item_factory(RTE_FLOW_ITEM_TYPE_TCP, keys);
    } else if (keys->tcp_udp_flag == HINIC3_DUMP_UDP_FLAG) {
        flow_item = hinic3_dump_flow_item_factory(RTE_FLOW_ITEM_TYPE_UDP, keys);
    }
    return flow_item;
}

static struct rte_flow_item *hinic3_flow_dump_ip_item_build(const struct hinic3_flow_dump_keys *keys)
{
    struct rte_flow_item *flow_item = NULL;
    if (keys->ip_version  == 0) {
        flow_item = hinic3_dump_flow_item_factory(RTE_FLOW_ITEM_TYPE_IPV4, keys);
    } else if (keys ->ip_version == 1) {
        flow_item = hinic3_dump_flow_item_factory(RTE_FLOW_ITEM_TYPE_IPV6, keys);
    }
    return flow_item;
}

static int hinic3_flow_item_build_sub(struct hinic3_dump_flow_info *flow, struct hinic3_flow_dump_keys* keys)
{
    if (keys == NULL) {
        return -1;
    }
    struct rte_flow_item *flow_item = NULL;
    int ret = 0;

    flow_item = hinic3_dump_flow_item_factory(RTE_FLOW_ITEM_TYPE_PORT_ID, keys);
    ret = hinic3_dump_flow_item_insert(flow, flow_item);
    if (ret != 0) {
        goto err_func;
    }

    flow_item = hinic3_dump_flow_item_factory(RTE_FLOW_ITEM_TYPE_ETH, keys);
    ret = hinic3_dump_flow_item_insert(flow, flow_item);
    if (ret != 0) {
        goto err_func;
    }

    flow_item = hinic3_flow_dump_ip_item_build(keys);
    ret = hinic3_dump_flow_item_insert(flow, flow_item);
    if (ret != 0) {
        goto err_func;
    }

    flow_item = hinic3_dump_flow_item_factory(RTE_FLOW_ITEM_TYPE_VLAN, keys);
    ret = hinic3_dump_flow_item_insert(flow, flow_item);
    if (ret != 0) {
        goto err_func;
    }

    if (keys->tcp_udp_flag != HINIC3_DUMP_TCP_UDP_INVALID) {
        flow_item = hinic3_flow_dump_tcpudp_item_build(keys);
        ret = hinic3_dump_flow_item_insert(flow, flow_item);
        if (ret != 0) {
            goto err_func;
        }
    }

    flow_item = hinic3_dump_flow_item_factory(RTE_FLOW_ITEM_TYPE_VXLAN, keys);
    ret = hinic3_dump_flow_item_insert(flow, flow_item);
    if (ret != 0) {
        goto err_func;
    }

    return 0;
err_func:
    hinic3_free_one_flow_items(flow->items, flow->useful_item_index);
    hinic3_free_rte_flow_item(flow_item);
    if (flow_item != NULL) {
        hinic3_free(flow_item);
    }
    flow_item = NULL;
    HINIC3_LOG(ERR, FLOW, "HINIC3 FLOW DUMP: build hinic3_dump_flow_info item failed");
    return -1;
}

int hinic3_hinic3_flow_info_item_build(struct hinic3_dump_flow_info *flow, struct hinic3_nlattr_obj* dump_key,
                                     size_t key_length)
{
    if (flow == NULL) {
        return -1;
    }

    struct hinic3_nlattr flow_key;
    hinic3_nlattr_init(&flow_key, dump_key, key_length);
    hinic3_nlattr_reset_itr(&flow_key, key_length);

    struct hinic3_flow_dump_keys row_keys = {0};
    row_keys.vlan_flag = false;
    row_keys.vxlan_flag = false;

    int ret = hinic3_get_full_key(&row_keys, &flow_key);
    if (ret != 0) {
        HINIC3_LOG(ERR, FLOW, "HINIC3 FLOW DUMP: flow item build failed");
        return -1;
    }

    ret = hinic3_flow_item_build_sub(flow, &row_keys);
    return ret;
}
