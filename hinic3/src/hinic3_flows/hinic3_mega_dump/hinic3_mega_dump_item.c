/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2025 Huawei Technologies Co., Ltd
 */

#include "hinic3_log.h"
#include "hinic3_ui_string.h"
#include "hinic3_util.h"
#include "hinic3_nlattr.h"
#include "hinic3_mega_dump_item.h"

#define HINIC3_MEGA_MASK_32 0xffffffff
#define HINIC3_MEGA_MASK_16 0xffff

typedef int (*hinic3_mega_format_item)(enum hinic3_dump_data_type type, const hinic3_nlattr_itr *itr,
    struct hinic3_dump_item_data items[]);

struct hinic3_mega_format_func_item_map {
    enum hinic3_flow_key_type mega_type;
    hinic3_mega_format_item func;
};

enum hinic3_mega_dump_rte_item_idx {
    HINIC3_MEGA_DUMP_RTE_ITEM_IDX_ETH,
    HINIC3_MEGA_DUMP_RTE_ITEM_IDX_PORT_ID,
    HINIC3_MEGA_DUMP_RTE_ITEM_IDX_VXLAN,
    HINIC3_MEGA_DUMP_RTE_ITEM_IDX_MAX,
};

static const enum rte_flow_item_type rte_flow_item_types[] = {
    RTE_FLOW_ITEM_TYPE_ETH,
    RTE_FLOW_ITEM_TYPE_PORT_ID,
    RTE_FLOW_ITEM_TYPE_VXLAN,
};

static int
hinic3_mega_format_item_in_port(enum hinic3_dump_data_type type, const hinic3_nlattr_itr *itr,
    struct hinic3_dump_item_data items[])
{
    struct rte_flow_item_port_id *item = NULL;
    uint16_t up_data = 0;
    uint16_t port_id = 0;
    int ret = hinic3_build_item_data(
        &items[HINIC3_MEGA_DUMP_RTE_ITEM_IDX_PORT_ID], type, sizeof(struct rte_flow_item_port_id));
    if (ret != 0)
        return ENOMEM;

    item = (struct rte_flow_item_port_id *)hinic3_get_item_data(&items[HINIC3_MEGA_DUMP_RTE_ITEM_IDX_PORT_ID], type);
    up_data = ntohs(hinic3_nlattr_get_itr_u16(*itr));
    port_id = up_data;
    if (type == HINIC3_DUMP_DATA_TYPE_KEY) {
        item->id = up_data;
    } else {
        item->id = (port_id == 0) ? 0 : HINIC3_MEGA_MASK_32;
    }

    return 0;
}

static int
hinic3_mega_format_item_vxlan_vni(enum hinic3_dump_data_type type, const hinic3_nlattr_itr *itr,
    struct hinic3_dump_item_data items[])
{
    struct rte_flow_item_vxlan *item = NULL;
    int ret = hinic3_build_item_data(
        &items[HINIC3_MEGA_DUMP_RTE_ITEM_IDX_VXLAN], type, sizeof(struct rte_flow_item_vxlan));
    if (ret != 0)
        return ENOMEM;

    item = (struct rte_flow_item_vxlan *)hinic3_get_item_data(&items[HINIC3_MEGA_DUMP_RTE_ITEM_IDX_VXLAN], type);
    item->hdr.vx_vni = hinic3_nlattr_get_itr_u32(*itr);

    return 0;
}

static int
hinic3_mega_format_item_eth_smac(enum hinic3_dump_data_type type, const hinic3_nlattr_itr *itr,
    struct hinic3_dump_item_data items[])
{
    struct rte_flow_item_eth *item = NULL;
    int ret = hinic3_build_item_data(&items[HINIC3_MEGA_DUMP_RTE_ITEM_IDX_ETH], type, sizeof(struct rte_flow_item_eth));
    if (ret != 0)
        return ENOMEM;

    item = (struct rte_flow_item_eth *)hinic3_get_item_data(&items[HINIC3_MEGA_DUMP_RTE_ITEM_IDX_ETH], type);
    memcpy(&item->src, hinic3_nlattr_get_itr_unspec(*itr, sizeof(item->src)), sizeof(item->src));

    return 0;
}

static int
hinic3_mega_format_item_eth_dmac(enum hinic3_dump_data_type type, const hinic3_nlattr_itr *itr,
    struct hinic3_dump_item_data items[])
{
    struct rte_flow_item_eth *item = NULL;
    int ret = hinic3_build_item_data(&items[HINIC3_MEGA_DUMP_RTE_ITEM_IDX_ETH], type, sizeof(struct rte_flow_item_eth));
    if (ret != 0) {
        return ENOMEM;
    }

    item = (struct rte_flow_item_eth *)hinic3_get_item_data(&items[HINIC3_MEGA_DUMP_RTE_ITEM_IDX_ETH], type);
    memcpy(&item->dst, hinic3_nlattr_get_itr_unspec(*itr, sizeof(item->dst)), sizeof(item->dst));

    return 0;
}

static const struct hinic3_mega_format_func_item_map hinic3_mega_format_func_item_maps[] = {
    {HINIC3_FLOW_KEY_IN_PORT, hinic3_mega_format_item_in_port},
    {HINIC3_FLOW_KEY_VNI, hinic3_mega_format_item_vxlan_vni},
    {HINIC3_FLOW_KEY_SRC_MAC, hinic3_mega_format_item_eth_smac},
    {HINIC3_FLOW_KEY_DST_MAC, hinic3_mega_format_item_eth_dmac},
};

static int
hinic3_mega_find_item_func(enum hinic3_flow_key_type type, struct hinic3_mega_format_func_item_map *item_map)
{
    size_t len = sizeof(hinic3_mega_format_func_item_maps) / sizeof(hinic3_mega_format_func_item_maps[0]);
    for (size_t i = 0; i < len; i++) {
        if (hinic3_mega_format_func_item_maps[i].mega_type == type) {
            *item_map = hinic3_mega_format_func_item_maps[i];
            return 0;
        }
    }

    return -1;
}

static int
hinic3_mega_dump_items(enum hinic3_dump_data_type type, struct hinic3_nlattr_obj *obj, size_t obj_len,
    struct hinic3_dump_item_data data[])
{
    int ret;
    uint16_t mega_type;
    struct hinic3_nlattr nlattr;
    hinic3_nlattr_init(&nlattr, obj, obj_len);
    hinic3_nlattr_reset_itr(&nlattr, obj_len);
    hinic3_nlattr_itr itr = NULL;
    struct hinic3_nlattr *iter_content = &nlattr;
    struct hinic3_mega_format_func_item_map item_map = {0};

    HINIC3_NLATTR_FOR_EACH(itr, iter_content)
    {
        mega_type = hinic3_nlattr_get_itr_type(itr);
        ret = hinic3_mega_find_item_func(mega_type, &item_map);
        if (ret != 0) {
            HINIC3_LOG(ERR, FLOW, "mega flow dump: unrecognized item type: %" PRIu16 ".", mega_type);
            return -EPERM;
        }
        ret = item_map.func(type, &itr, data);
        if (ret != 0) {
            return ret;
        }
    }
    return 0;
}

static int
hinic3_mega_fill_items(struct hinic3_dump_flow_info *info, struct hinic3_dump_item_data items[], uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        if (items[i].is_valid == false)
            continue;

        if (info->useful_item_index >= HINIC3_FLOW_DUMP_MAX_PATTERN - 1)
            return -EPERM;

        struct rte_flow_item *item = &(info->items[info->useful_item_index]);
        item->type = rte_flow_item_types[i];
        item->spec = items[i].key;
        item->mask = items[i].mask;
        item->last = NULL;
        info->useful_item_index++;
    }

    return 0;
}

int
hinic3_mega_item_build(struct hinic3_dpif_flow_for_get *get, struct hinic3_dump_flow_info *info)
{
    if ((info == NULL) || (get == NULL)) {
        HINIC3_LOG(ERR, FLOW, "mega flow dump: Dump item input null.");
        return -EINVAL;
    }

    int ret = INT_MAX;
    int err = 0;
    struct hinic3_dump_item_data items[HINIC3_MEGA_DUMP_RTE_ITEM_IDX_MAX] = {0};

    ret = hinic3_mega_dump_items(HINIC3_DUMP_DATA_TYPE_KEY, (struct hinic3_nlattr_obj *)get->key, get->key_len, items);
    if (ret != 0 && ret != ERANGE) {
        hinic3_clean_items_data(items, HINIC3_MEGA_DUMP_RTE_ITEM_IDX_MAX);
        HINIC3_LOG(ERR, FLOW, "mega flow dump: Dump item spec failed.");
        return ret;
    } else if (ret == ERANGE) {
        err = ret;
    }

    ret = hinic3_mega_dump_items(HINIC3_DUMP_DATA_TYPE_MASK, (struct hinic3_nlattr_obj *)get->mask, get->mask_len, items);
    if (ret != 0) {
        hinic3_clean_items_data(items, HINIC3_MEGA_DUMP_RTE_ITEM_IDX_MAX);
        HINIC3_LOG(ERR, FLOW, "mega flow dump: Dump item mask failed.");
        return ret;
    }

    ret = hinic3_mega_fill_items(info, items, HINIC3_MEGA_DUMP_RTE_ITEM_IDX_MAX);
    if (ret != 0) {
        hinic3_clean_items_data(items, HINIC3_MEGA_DUMP_RTE_ITEM_IDX_MAX);
        HINIC3_LOG(ERR, FLOW, "mega flow dump: Dump item failed.");
        return ret;
    }

    if (err == ERANGE) {
        HINIC3_LOG(ERR, FLOW, "mega flow dump: port/session id error.");
        return err;
    }

    return 0;
}

void
hinic3_mega_item_clean(struct hinic3_dump_flow_info *info)
{
    /* Wcast-qual无法避免，局部忽略此告警 */
    for (unsigned int i = 0; i < info->useful_item_index; i++) {
        if (info->items[i].spec != NULL) {
            #pragma GCC diagnostic push
            #pragma GCC diagnostic ignored "-Wcast-qual"
            hinic3_free(HINIC3_CONST_CAST(void *, info->items[i].spec));
            #pragma GCC diagnostic pop
            info->items[i].spec = NULL;
        }
        if (info->items[i].mask != NULL) {
            #pragma GCC diagnostic push
            #pragma GCC diagnostic ignored "-Wcast-qual"
            hinic3_free(HINIC3_CONST_CAST(void *, info->items[i].mask));
            #pragma GCC diagnostic pop
            info->items[i].mask = NULL;
        }
    }
}