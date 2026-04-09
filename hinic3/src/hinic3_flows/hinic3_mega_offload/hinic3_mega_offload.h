/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2025 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_MEGA_OFFLOAD_H
#define HINIC3_MEGA_OFFLOAD_H

#include "rte_flow.h"
#include "ethdev_driver.h"
#include "hinic3_flow_agent_public.h"
#include "hinic3_mega_key.h"

#define HINIC3_MEGA_FLOW_MAX_NUM 1500

struct hinic3_mega_flow {
    uint32_t index;
    uint64_t ufid;
    struct hinic3_mega_flow_full_key full_key;
};

struct mega_flow {
    struct rte_flow flow;
    struct hinic3_mega_flow mega_flow;
};

struct hinic3_mega_flow_node {
    uint8_t is_used;
    struct mega_flow *raw_mega_flow;
    struct hinic3_mega_flow flow;
};

struct hinic3_mega_table {
    uint32_t flow_num;
    struct hinic3_mega_flow_node flow_table[HINIC3_MEGA_FLOW_MAX_NUM];
    struct hinic3_mutex mutex_lock;
};

int hinic3_mega_flow_init(void);
struct rte_flow *hinic3_mega_flow_offload(const struct rte_flow_item pattern[], struct rte_flow_error *error);
int hinic3_mega_flow_delete(struct rte_eth_dev *dev, struct rte_flow *flow, struct rte_flow_error *error);
int hinic3_mega_flow_flush_by_port(uint16_t port_id, struct rte_flow_error *error);
int hinic3_mega_flow_query(struct rte_eth_dev *dev, struct rte_flow *flow, void *data, struct rte_flow_error *error);
const struct hinic3_mega_table* hinic3_get_mega_table(void);
#endif