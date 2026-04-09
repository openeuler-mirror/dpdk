/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2022 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_AGE_DELETE_FLOW_H
#define HINIC3_AGE_DELETE_FLOW_H

#include "hinic3_age_delete_flow_public.h"

struct rte_aged_flow_list_node {
    struct hinic3_list node;
    struct rte_flow *flow;
};

struct rte_aged_flow_list {
    struct hinic3_list list_head;
    struct hinic3_mutex mutex;
};

int hinic3_flow_emc_destroy(struct rte_eth_dev *dev, struct rte_flow *flow, struct rte_flow_error *error);
int hinic3_flow_agent_age_callback(uint64_t hw_ufid, const struct hinic3_dpif_flow_for_get *flow,
    const struct hinic3_nlattr *args);
int hinic3_flow_flush_all(struct rte_flow_error *error);
#endif
