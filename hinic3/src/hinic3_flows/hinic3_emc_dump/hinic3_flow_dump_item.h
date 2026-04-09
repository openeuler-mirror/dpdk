/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Huawei Technologies Co., Ltd
 */
#ifndef HINIC3_FLOW_DUMP_ITEM_H
#define HINIC3_FLOW_DUMP_ITEM_H
#include <stddef.h>
#include "hinic3_nlattr.h"
#include "rte_flow.h"
#include "hinic3_flow_dump_public.h"
#include "hinic3_flow_agent_public.h"

void hinic3_free_one_flow_items(struct rte_flow_item* items, int item_num);
int hinic3_hinic3_flow_info_item_build(struct hinic3_dump_flow_info *flow, struct hinic3_nlattr_obj* dump_key,
                                     size_t key_length);
struct rte_flow_item* hinic3_build_rte_flow_item(enum rte_flow_item_type type, const void *spec,
    const void *last, const void *mask);
int hinic3_dump_flow_item_insert(struct hinic3_dump_flow_info *flow, struct rte_flow_item *item);
#endif
