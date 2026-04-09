/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
 * Description: hinic3_flow结构体工具模块
 * Create: 2023-10-07
 */
#ifndef HINIC3_FLOW_FORMAT_H
#define HINIC3_FLOW_FORMAT_H
#include "hinic3_ds.h"
#include "rte_flow.h"
#include "hinic3_flexda_flow_public.h"

#define HINIC3_FLOW_ITEMS_NUM 15
#define HINIC3_FLOW_ACTIONS_NUM 15
struct hinic3_flow {
    struct rte_flow_item items[HINIC3_FLOW_ITEMS_NUM];
    struct rte_flow_action actions[HINIC3_FLOW_ACTIONS_NUM];
};

enum hinic3_key_type {
    HINIC3_ITEM_SPEC,
    HINIC3_ITEM_MASK,
    HINIC3_ITEM_LAST
};

int hinic3_format_flow(const struct hinic3_flow *flow, bool need_mask, struct ds *ds);

int hinic3_flow_construct_key(struct hinic3_flow *flow, enum rte_flow_item_type type, void *spec);
int hinic3_rte_action_insert(struct rte_flow_action dst_actions[], int dst_size,
    const struct rte_flow_action src_actions[], int src_size);
int hinic3_rte_item_insert(struct rte_flow_item dst_items[], int dst_size,
    const struct rte_flow_item src_items[], int src_size);
void hinic3_free_flow(struct hinic3_flow *flow);
int hinic3_format_keys(const struct hinic3_flow *flow, enum hinic3_key_type type, struct ds *ds);
int hinic3_flow_construct_hydra_key(struct hydra_flow_item *geneve,
    enum hydra_item_type type, void *data, int size);
#endif
