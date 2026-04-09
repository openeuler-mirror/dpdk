/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2025 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_ETH_UTIL_H
#define HINIC3_ETH_UTIL_H

#include "hinic3_iface_global.h"
#include "hinic3_query_flow.h"
#include "hinic3_age_delete_flow.h"
#include "hinic3_dump_flow.h"
#include "hinic3_parse_agent_config.h"

int hinic3_eth_flow_validate(struct rte_eth_dev *dev, const struct rte_flow_attr *attr,
    const struct rte_flow_item *pattern, const struct rte_flow_action *actions, struct rte_flow_error *error);

struct rte_flow *hinic3_eth_flow_create(struct rte_eth_dev *dev, const struct rte_flow_attr *attr,
    const struct rte_flow_item *pattern, const struct rte_flow_action *actions, struct rte_flow_error *error);

int hinic3_eth_flow_destroy(struct rte_eth_dev *dev, struct rte_flow *flow, struct rte_flow_error *error);

int hinic3_eth_flow_flush(struct rte_eth_dev *dev, __rte_unused struct rte_flow_error *error);

int hinic3_eth_flow_query(struct rte_eth_dev *dev, struct rte_flow *flow, const struct rte_flow_action *actions,
    void *data, struct rte_flow_error *error);

int hinic3_eth_get_aged_flow(struct rte_eth_dev *dev, void **context, uint32_t nb_contexts, struct rte_flow_error *err);
#endif
