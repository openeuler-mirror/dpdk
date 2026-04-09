/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2025 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_MEGA_KEY_H
#define HINIC3_MEGA_KEY_H

#include "rte_flow.h"

enum hinic3_mega_item_type {
    HINIC3_MEGA_ITEM_TYPE_ETH,
    HINIC3_MEGA_ITEM_TYPE_PORT_ID,
    HINIC3_MEGA_ITEM_TYPE_VXLAN,
    HINIC3_MEGA_ITEM_TYPE_MAX,
};

struct hinic3_mega_key {
    struct rte_flow_item_eth eth;
    struct rte_flow_item_vlan vlan;
    struct rte_flow_item_port_id in_port;
    struct rte_flow_item_vxlan vxlan;
};

struct hinic3_mega_flow_full_key {
    uint8_t item_num;
    enum rte_flow_item_type item_types[HINIC3_MEGA_ITEM_TYPE_MAX];
    struct hinic3_mega_key mega_key;
    struct hinic3_mega_key mask;
};

int hinic3_mega_full_key_construct(const struct rte_flow_item *pattern,
    struct hinic3_mega_flow_full_key *mega_full_key, struct rte_flow_error *error);
#endif