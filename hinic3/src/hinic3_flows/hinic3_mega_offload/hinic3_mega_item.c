/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2025 Huawei Technologies Co., Ltd
 */

#include "hinic3_vxlan.h"
#include "hinic3_parse_agent_config.h"
#include "hinic3_offload_action.h"
#include "hinic3_mega_item.h"

#define HINIC3_MEGA_PORT_MASK 0xFFFFFFFF
#define HINIC3_MEGA_PORT_IFINDEX_MASK 0xFFFF

typedef int (*mega_item_callback)(const struct hinic3_mega_key *mega_key, struct hinic3_nlattr *dst_key);
struct hinic3_mega_item_info {
    enum rte_flow_item_type type;
    mega_item_callback call_back;
};

static int
hinic3_mega_put_eth(const struct hinic3_mega_key *mega_key, struct hinic3_nlattr *dst_key)
{
    int ret = hinic3_nlattr_put_unspec(dst_key, HINIC3_FLOW_KEY_DST_MAC, &mega_key->eth.dst, sizeof(mega_key->eth.dst));
    if (ret != 0) {
        HINIC3_LOG(ERR, FLOW, "mega offload: hwoff mega put eth type failed");
        return -1;
    }

    return 0;
}

static int
hinic3_mega_port_item(const struct hinic3_mega_key *mega_key, struct hinic3_nlattr *dst_key)
{
    int ret = INT_MAX;
    uint16_t port = 0;
    uint32_t port_id = mega_key->in_port.id;
    /* if port_id is 0xffffffff, will put mask to hiovs */
    if (port_id == HINIC3_MEGA_PORT_MASK) 
        port = HINIC3_MEGA_PORT_IFINDEX_MASK;
    else 
        port = (uint16_t)hinic3_process_port_id(port_id);

    ret = hinic3_nlattr_put_u16(dst_key, HINIC3_FLOW_KEY_IN_PORT, htons(port));
    if (ret != 0) {
        HINIC3_LOG(ERR, FLOW, "mega offload: hwoff mega put mega input_port item failed");
        return -1;
    }

    return 0;
}

static int
hinic3_mega_vni_item(const struct hinic3_mega_key *mega_key, struct hinic3_nlattr *dst_key)
{
    int ret = hinic3_nlattr_put_u32(dst_key, HINIC3_FLOW_KEY_VNI, mega_key->vxlan.hdr.vx_vni);
    if (ret != 0) {
        HINIC3_LOG(ERR, FLOW, "mega offload: hwoff mega put vxlan vni item failed");
        return -1;
    }

    return ret;
}

static const struct hinic3_mega_item_info g_items[] = {
    {RTE_FLOW_ITEM_TYPE_ETH, hinic3_mega_put_eth},
    {RTE_FLOW_ITEM_TYPE_PORT_ID, hinic3_mega_port_item},
    {RTE_FLOW_ITEM_TYPE_VXLAN, hinic3_mega_vni_item},
};

static int
hinic3_mega_item_info_find(enum rte_flow_item_type type, struct hinic3_mega_item_info *item_info)
{
    int len = sizeof(g_items) / sizeof(g_items[0]);
    for (int i = 0; i < len; ++i) {
        if (g_items[i].type == type) {
            *item_info = g_items[i];
            return 0;
        }
    }

    return -1;
}

static int
hinic3_mega_item_construct(const struct hinic3_mega_flow_full_key *mega_key,
    const struct hinic3_mega_key *key, struct hinic3_nlattr *dst)
{
    int ret = 0;
    for (uint8_t i = 0; i < mega_key->item_num; ++i) {
        struct hinic3_mega_item_info item_info = {0};
        ret = hinic3_mega_item_info_find(mega_key->item_types[i], &item_info);
        if (ret != 0) {
            HINIC3_LOG(ERR, FLOW, "mega offload: can't find the type info, type id %d", mega_key->item_types[i]);
            return -1;
        }

        if (item_info.call_back != NULL) {
            ret = item_info.call_back(key, dst);
            if (ret != 0)
                return -1;
        }
    }

    return ret;
}

int
hinic3_mega_offload_item_construct(const struct hinic3_mega_flow_full_key *mega_full_key,
    struct hinic3_nlattr *dst_key, struct hinic3_nlattr *dst_mask, struct rte_flow_error *error)
{
    if (mega_full_key == NULL || dst_key == NULL || dst_mask == NULL || error == NULL) {
        HINIC3_LOG(ERR, FLOW, "mega offload: item_construct input NULL ptr");
        return -1;
    }
    int ret = 0;

    ret = hinic3_mega_item_construct(mega_full_key, &mega_full_key->mega_key, dst_key);
    if (ret != 0) {
        return rte_flow_error_set(
            error, EPERM, RTE_FLOW_ERROR_TYPE_ITEM_SPEC, NULL, "mega flow offload: construct item failed.");
    }

    ret = hinic3_mega_item_construct(mega_full_key, &mega_full_key->mask, dst_mask);
    if (ret != 0) {
        return rte_flow_error_set(
            error, EPERM, RTE_FLOW_ERROR_TYPE_ITEM_MASK, NULL, "mega flow offload: construct mask failed.");
    }

    return ret;
}