/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Huawei Technologies Co., Ltd
 */
#include "hinic3_vxlan_dump.h"
#include "hinic3_flow_dump_item.h"
#include "hinic3_log.h"
#include "hinic3_eth_packets.h"
#include "hinic3_meminfo.h"

#define HINIC3_VXLAN_HEAD_MAX_ITEMS 6
#define HINIC3_VXLAN_IPV4_FLAG 0
#define HINIC3_VXLAN_IPV6_FLAG 1

static int hinic3_vxlan_header_items_insert(struct rte_flow_item *vxlan_header_items, int header_max_length,
    const struct rte_flow_item *src_item)
{
    for (int i = 0; i < header_max_length - 1; ++i) {
        if (vxlan_header_items[i].type == RTE_FLOW_ITEM_TYPE_END) {
            memcpy(&vxlan_header_items[i], src_item, sizeof(struct rte_flow_item));
            return 0;
        }
    }
    HINIC3_LOG(ERR, FLOW, "hinic3 vxlan dump: insert item into vxlan header failed");
    return -1;
}

static int hinic3_get_eth_vxlan_items(struct rte_flow_item *vxlan_header_items,
    const struct hinic3_flow_act_vxlan_gpe_header *vxlan_header)
{
    struct rte_flow_item eth = { 0 };
    eth.type = RTE_FLOW_ITEM_TYPE_ETH;

    struct rte_flow_item_eth *mac = hinic3_calloc(1, sizeof(struct rte_flow_item_eth), HINIC3_FLOWS);
    if (mac == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3 vxlan dump: malloc eth memory failed");
        return -1;
    }

    memcpy(&(mac->src), vxlan_header->smac, sizeof(uint8_t) * ETH_ALEN);
    memcpy(&(mac->dst), vxlan_header->dmac, sizeof(uint8_t) * ETH_ALEN);

    if (vxlan_header->vlan_id != 0) {
        mac->type = htons(ETH_TYPE_VLAN);
    } else {
        mac->type = vxlan_header->ip_version == 0 ? htons(ETH_TYPE_IP) : htons(ETH_TYPE_IPV6);
    }

    eth.spec = (void *)mac;
    int ret = hinic3_vxlan_header_items_insert(vxlan_header_items, HINIC3_VXLAN_HEAD_MAX_ITEMS, &eth);
    if (ret != 0) {
        hinic3_free(mac);
        return -1;
    }
    return 0;
}

static int hinic3_get_vlan_vxlan_items(struct rte_flow_item *vxlan_header_items,
    const struct hinic3_flow_act_vxlan_gpe_header *vxlan_header)
{
    struct rte_flow_item vlan;
    struct rte_flow_item_vlan *vlan_spec = hinic3_calloc(1, sizeof(struct rte_flow_item_vlan), HINIC3_FLOWS);
    if (vlan_spec == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3 vxlan dump: malloc vlan item memory failed");
        return -1;
    }

    vlan_spec->tci = vxlan_header->vlan_id;
    if (vxlan_header->ip_version == 0) {
        vlan_spec->inner_type = htons(ETH_TYPE_IP);
    } else if (vxlan_header->ip_version == 1) {
        vlan_spec->inner_type = htons(ETH_TYPE_IPV6);
    }

    vlan.spec = (void *)vlan_spec;
    vlan.type = RTE_FLOW_ITEM_TYPE_VLAN;
    int ret = hinic3_vxlan_header_items_insert(vxlan_header_items, HINIC3_VXLAN_HEAD_MAX_ITEMS, &vlan);
    if (ret != 0) {
        hinic3_free(vlan_spec);
        return -1;
    }
    return 0;
}

static int hinic3_get_ip_vxlan_items(struct rte_flow_item *vxlan_header_items,
    const struct hinic3_flow_act_vxlan_gpe_header *vxlan_header)
{
    struct rte_flow_item_ipv4 *ipv4 = NULL;
    struct rte_flow_item_ipv6 *ipv6 = NULL;
    struct rte_flow_item ip;

    if (vxlan_header->ip_version == 0) {
        ipv4 = hinic3_calloc(1, sizeof(struct rte_flow_item_ipv4), HINIC3_FLOWS);
        if (ipv4 == NULL) {
            HINIC3_LOG(ERR, FLOW, "hinic3 vxlan dump: malloc ipv4 memory failed");
            return -1;
        }
        ipv4->hdr.dst_addr = vxlan_header->dip[0];
        ipv4->hdr.src_addr = vxlan_header->sip[0];
        ipv4->hdr.next_proto_id = IPPROTO_UDP;

        ip.type = RTE_FLOW_ITEM_TYPE_IPV4;
        ip.spec = (void *)ipv4;
    } else if (vxlan_header->ip_version == 1) {
        ipv6 = hinic3_calloc(1, sizeof(struct rte_flow_item_ipv6), HINIC3_FLOWS);
        if (ipv6 == NULL) {
            HINIC3_LOG(ERR, FLOW, "hinic3 vxlan dump: malloc ipv6 memory failed");
            return -1;
        }
        memcpy(&ipv6->hdr.src_addr, vxlan_header->sip, sizeof(vxlan_header->sip));
        memcpy(&ipv6->hdr.dst_addr, vxlan_header->dip, sizeof(vxlan_header->dip));
        ipv6->hdr.proto = IPPROTO_UDP;
        ip.type = RTE_FLOW_ITEM_TYPE_IPV6;
        ip.spec = (void *)ipv6;
    }

    int ret = hinic3_vxlan_header_items_insert(vxlan_header_items, HINIC3_VXLAN_HEAD_MAX_ITEMS, &ip);
    if (ret != 0) {
        if (vxlan_header->ip_version == 0) {
            hinic3_free(ipv4);
        } else if (vxlan_header->ip_version == 1) {
            hinic3_free(ipv6);
        }
        return -1;
    }
    return 0;
}

static int hinic3_get_vlxan_header_udp_item(struct rte_flow_item *vxlan_header_items,
    const struct hinic3_flow_act_vxlan_gpe_header *vxlan_header)
{
    struct rte_flow_item udp;
    struct rte_flow_item_udp *udp_spec = hinic3_calloc(1, sizeof(struct rte_flow_item_udp), HINIC3_FLOWS);
    if (udp_spec == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3 vxlan dump: malloc udp memory failed");
        return -1;
    }

    udp_spec->hdr.dst_port = vxlan_header->dport;
    udp_spec->hdr.src_port = vxlan_header->sport;

    udp.type = RTE_FLOW_ITEM_TYPE_UDP;
    udp.spec = (void *)udp_spec;

    int ret = hinic3_vxlan_header_items_insert(vxlan_header_items, HINIC3_VXLAN_HEAD_MAX_ITEMS, &udp);
    if (ret != 0) {
        hinic3_free(udp_spec);
        return -1;
    }
    return 0;
}

static int hinic3_get_vlxan_header_vxlan_item(struct rte_flow_item *vxlan_header_items,
    const struct hinic3_flow_act_vxlan_gpe_header *vxlan_header)
{
    struct rte_flow_item vxlan;
    vxlan.type = RTE_FLOW_ITEM_TYPE_VXLAN;
    int ret;
    struct rte_flow_item_vxlan *vxlan_spec = hinic3_calloc(1, sizeof(struct rte_flow_item_vxlan), HINIC3_FLOWS);
    if (vxlan_spec == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3 vxlan dump: malloc vxlan memory failed");
        return -1;
    }

    memcpy(vxlan_spec, &vxlan_header->vxlan, sizeof(vxlan_header->vxlan));

    vxlan.spec = (void *)vxlan_spec;

    ret = hinic3_vxlan_header_items_insert(vxlan_header_items, HINIC3_VXLAN_HEAD_MAX_ITEMS, &vxlan);
    if (ret != 0) {
        hinic3_free(vxlan_spec);
        return -1;
    }
    return 0;
}

rte_vxlan_encap hinic3_get_vxlan_gpe_header_items(struct hinic3_flow_act_vxlan_gpe_header *vxlan_header)
{
    rte_vxlan_encap vxlan_headr = hinic3_calloc(1, sizeof(struct rte_flow_action_vxlan_encap), HINIC3_FLOWS);
    if (vxlan_headr == NULL) {
        HINIC3_LOG(ERR, FLOW, "FLOW DUMP: malloc for vxlan_headr failed");
        return NULL;
    }

    struct rte_flow_item *vxlan_header_items =
        hinic3_calloc(HINIC3_VXLAN_HEAD_MAX_ITEMS, sizeof(struct rte_flow_item), HINIC3_FLOWS);
    if (vxlan_header_items == NULL) {
        hinic3_free(vxlan_headr);
        HINIC3_LOG(ERR, FLOW, "FLOW DUMP: malloc for vxlan_header_items failed");
        return NULL;
    }

    int ret = 0;
    ret = hinic3_get_eth_vxlan_items(vxlan_header_items, vxlan_header);
    if (ret != 0) {
        goto err;
    }

    if (vxlan_header->vlan_id != 0) {
        ret = hinic3_get_vlan_vxlan_items(vxlan_header_items, vxlan_header);
        if (ret != 0) {
            goto err;
        }
    }

    ret = hinic3_get_ip_vxlan_items(vxlan_header_items, vxlan_header);
    if (ret != 0) {
        goto err;
    }

    ret = hinic3_get_vlxan_header_udp_item(vxlan_header_items, vxlan_header);
    if (ret != 0) {
        goto err;
    }

    ret = hinic3_get_vlxan_header_vxlan_item(vxlan_header_items, vxlan_header);
    if (ret != 0) {
        goto err;
    }
    vxlan_headr->definition = vxlan_header_items;
    return vxlan_headr;
err:
    HINIC3_LOG(ERR, FLOW, "FLOW DUMP: get vxlan headers failed");
    hinic3_free_one_flow_items(vxlan_header_items, HINIC3_VXLAN_HEAD_MAX_ITEMS);
    hinic3_free(vxlan_header_items);
    hinic3_free(vxlan_headr);
    return NULL;
}

void hinic3_free_vxlan_header(struct rte_flow_action_vxlan_encap *vxlan_header)
{
    if (vxlan_header == NULL) {
        return;
    }

    struct rte_flow_item *vxlan_header_items = vxlan_header->definition;
    if (vxlan_header_items == NULL) {
        hinic3_free(vxlan_header);
        return;
    }

    for (int i = 0; i < HINIC3_VXLAN_HEAD_MAX_ITEMS; ++i) {
        if (vxlan_header_items[i].type == RTE_FLOW_ITEM_TYPE_END) {
            break;
        }
        if (vxlan_header_items[i].spec != NULL) {
            hinic3_free((void *)(uintptr_t)vxlan_header_items[i].spec);
            vxlan_header_items[i].spec = NULL;
        }
    }

    hinic3_free(vxlan_header_items);
    hinic3_free(vxlan_header);
    vxlan_header = NULL;

    return;
}
