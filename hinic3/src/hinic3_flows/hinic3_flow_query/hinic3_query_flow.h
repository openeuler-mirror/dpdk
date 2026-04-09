/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2022 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_QUERY_FLOW_H
#define HINIC3_QUERY_FLOW_H
#include "rte_ethdev.h"
#include "rte_flow.h"
#include "hinic3_flow_agent_public.h"

int hinic3_flow_query_emc(struct rte_eth_dev *dev, struct rte_flow *flow, const struct rte_flow_action actions[],
    void *data, struct rte_flow_error *error);
#endif
