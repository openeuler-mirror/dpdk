/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2025 Huawei Technologies Co., Ltd
 */

#include "hinic3_log.h"
#include "hinic3_ui_string.h"
#include "hinic3_vxlan.h"
#include "hinic3_agent_cmd_format.h"
#include "hinic3_mega_dump_format.h"

typedef int (*hinic3_format_mega_item_pro)(const struct rte_flow_item *item, struct ds *ds);

struct hinic3_format_mega_item_pro_str {
    enum rte_flow_item_type type;
    hinic3_format_mega_item_pro pro;
};

static int
hinic3_format_mega_item_in_port(const struct rte_flow_item *item, struct ds *ds)
{
    const struct rte_flow_item_port_id *key = (const struct rte_flow_item_port_id *)item->spec;
    const struct rte_flow_item_port_id *mask = (const struct rte_flow_item_port_id *)item->mask;
    if ((key == NULL) || (mask == NULL))
        return -EINVAL;

    hinic3_ds_put_format(ds, "%s(%u/%u),", HINIC3_UI_KEY_VPORT_STR, key->id, mask->id);

    return 0;
}

static int
hinic3_format_mega_item_eth_type(const struct rte_flow_item *item, struct ds *ds)
{
    const struct rte_flow_item_eth *key = (const struct rte_flow_item_eth *)item->spec;
    const struct rte_flow_item_eth *mask = (const struct rte_flow_item_eth *)item->mask;
    if ((key == NULL) || (mask == NULL))
        return -EINVAL;

    hinic3_ds_put_format(
        ds, "%s(" HINIC3_MAC_FMT "), ", HINIC3_UI_KEY_SRC_MAC_STR, HINIC3_OUTPUT_MAC((const uint8_t *)&key->src));
    hinic3_ds_put_format(
        ds, "%s(" HINIC3_MAC_FMT "), ", HINIC3_UI_KEY_DST_MAC_STR, HINIC3_OUTPUT_MAC((const uint8_t *)&key->dst));

    return 0;
}

static int
hinic3_format_mega_item_vxlan_vni(const struct rte_flow_item *item, struct ds *ds)
{
    const struct rte_flow_item_vxlan *key = (const struct rte_flow_item_vxlan *)item->spec;
    const struct rte_flow_item_vxlan *mask = (const struct rte_flow_item_vxlan *)item->mask;
    if ((key == NULL) || (mask == NULL))
        return -EINVAL;

    hinic3_ds_put_format(ds, "%s(%u),", HINIC3_UI_KEY_VNI_STR, hinic3_swap_endian32(key->hdr.vx_vni));

    return 0;
}

static struct hinic3_format_mega_item_pro_str item_pro_arrs[] = {
    {RTE_FLOW_ITEM_TYPE_PORT_ID, hinic3_format_mega_item_in_port},
    {RTE_FLOW_ITEM_TYPE_ETH, hinic3_format_mega_item_eth_type},
    {RTE_FLOW_ITEM_TYPE_VXLAN, hinic3_format_mega_item_vxlan_vni},
};

static int
hinic3_format_mega_get_item_pro(enum rte_flow_item_type type, struct hinic3_format_mega_item_pro_str *item)
{
    int len = sizeof(item_pro_arrs) / sizeof(item_pro_arrs[0]);
    for (int i = 0; i < len; i++) {
        if (item_pro_arrs[i].type == type) {
            *item = item_pro_arrs[i];
            return 0;
        }
    }

    return -1;
}

int
hinic3_format_mega_flow_items(const struct hinic3_flow *flow, struct ds *ds)
{
    int ret = 0;
    struct rte_flow_item item = {0};
    struct hinic3_format_mega_item_pro_str item_pro = {0};

    hinic3_ds_put_format(ds, "%s", HINIC3_UI_FLOW_KEY_STRING);
    for (int i = 0; i < HINIC3_FLOW_ITEMS_NUM && flow->items[i].type != RTE_FLOW_ITEM_TYPE_END; i++) {
        item = flow->items[i];
        ret = hinic3_format_mega_get_item_pro(item.type, &item_pro);
        if (ret != 0) {
            HINIC3_LOG(ERR, FLOW, "mega flow format: unrecognized item type: %d.", item.type);
            return -EPERM;
        }

        if (item_pro.pro != NULL) {
            ret = item_pro.pro(&item, ds);
            if (ret != 0)
                return -EPERM;
        }
    }

    return 0;
}

void
hinic3_format_mega_flow(const struct hinic3_flow *flow, struct ds *ds)
{
    if ((flow == NULL) || (ds == NULL))
        return;

    (void)hinic3_format_mega_flow_items(flow, ds);
}