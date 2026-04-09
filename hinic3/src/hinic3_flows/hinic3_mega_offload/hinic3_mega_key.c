/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2025 Huawei Technologies Co., Ltd
 */

#include "hinic3_vxlan.h"
#include "hinic3_offload_flow_public.h"
#include "hinic3_mega_item.h"
#include "hinic3_mega_key.h"

typedef int (*hinic3_mega_key_get)(const struct rte_flow_item *, struct hinic3_mega_flow_full_key *);

struct hinic3_mega_key_info {
    enum rte_flow_item_type type;
    hinic3_mega_key_get callback;
};

static int
hinic3_mega_eth_key(const struct rte_flow_item *pattern, struct hinic3_mega_flow_full_key *mega_full_key)
{
    if ((pattern->spec == NULL) || (pattern->mask == NULL))
        return -EINVAL;

    mega_full_key->mega_key.eth.dst = ((const struct rte_flow_item_eth *)pattern->spec)->dst;
    mega_full_key->mask.eth.dst = ((const struct rte_flow_item_eth *)pattern->mask)->dst;
    mega_full_key->item_types[mega_full_key->item_num] = pattern->type;
    mega_full_key->item_num++;

    return 0;
}

static int
hinic3_mega_input_port_key(const struct rte_flow_item *pattern, struct hinic3_mega_flow_full_key *mega_full_key)
{
    if ((pattern->spec == NULL) || (pattern->mask == NULL))
        return -EINVAL;

    mega_full_key->mega_key.in_port = *(const struct rte_flow_item_port_id *)pattern->spec;
    mega_full_key->mask.in_port = *(const struct rte_flow_item_port_id *)pattern->mask;
    mega_full_key->item_types[mega_full_key->item_num] = pattern->type;
    mega_full_key->item_num++;

    return 0;
}

static int
hinic3_mega_vxlan_key(const struct rte_flow_item *pattern, struct hinic3_mega_flow_full_key *mega_full_key)
{
    if ((pattern->spec == NULL) || (pattern->mask == NULL))
        return -EINVAL;

    mega_full_key->mega_key.vxlan.hdr.vx_vni =
        hinic3_swap_vx_vni(((const struct rte_flow_item_vxlan *)pattern->spec)->hdr.vx_vni);
    mega_full_key->mask.vxlan.hdr.vx_vni =
        hinic3_swap_vx_vni(((const struct rte_flow_item_vxlan *)pattern->mask)->hdr.vx_vni);
    mega_full_key->item_types[mega_full_key->item_num] = pattern->type;
    mega_full_key->item_num++;

    return 0;
}

static const struct hinic3_mega_key_info g_mega_key_construct_table[] = {
    {RTE_FLOW_ITEM_TYPE_ETH, hinic3_mega_eth_key},
    {RTE_FLOW_ITEM_TYPE_PORT_ID, hinic3_mega_input_port_key},
    {RTE_FLOW_ITEM_TYPE_VXLAN, hinic3_mega_vxlan_key},
};

static int
hinic3_get_mega_info(enum rte_flow_item_type type, struct hinic3_mega_key_info *mega_info)
{
    int len = sizeof(g_mega_key_construct_table) / sizeof(g_mega_key_construct_table[0]);
    for (int i = 0; i < len; ++i) {
        if (g_mega_key_construct_table[i].type == type) {
            mega_info->type = type;
            mega_info->callback = g_mega_key_construct_table[i].callback;
            return 0;
        }
    }

    return -1;
}

int
hinic3_mega_full_key_construct(const struct rte_flow_item *pattern,
    struct hinic3_mega_flow_full_key *mega_full_key, struct rte_flow_error *error)
{
    if (pattern == NULL || mega_full_key == NULL || error == NULL)
        return -EINVAL;

    int ret = 0;
    struct hinic3_mega_key_info key_info = {0};
    const struct rte_flow_item *item = NULL;
    item = next_no_end_pattern(pattern, NULL);
    while (item) {
        ret = hinic3_get_mega_info(item->type, &key_info);
        if (ret != 0) {
            return rte_flow_error_set(error, EINVAL, RTE_FLOW_ERROR_TYPE_ITEM_NUM, NULL,
                "hinic3_mega_flow_offload: unrecognized item type.");
        }

        if (key_info.callback != NULL) {
            ret = key_info.callback(item, mega_full_key);
            if (ret != 0) {
                return rte_flow_error_set(error, EINVAL, RTE_FLOW_ERROR_TYPE_ITEM_SPEC, NULL,
                    "hinic3_mega_flow_offload: invalid item spec.");
            }
        }

        item = next_no_end_pattern(pattern, item);
    }

    return 0;
}