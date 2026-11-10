/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026-2027 Huawei Technologies Co., Ltd
 */

#ifndef _HNS5_FLOW_H_
#define _HNS5_FLOW_H_

#include <rte_flow.h>
#include <rte_ethdev_driver.h>
#include <rte_flow_driver.h>
#include "hns5_fdir.h"

struct hns5_flow_counter {
    LIST_ENTRY(hns5_flow_counter) next; /* Pointer to the next counter. */
    uint32_t indirect : 1;              /* Indirect counter flag */
    uint32_t ref_cnt : 31;              /* Reference counter. */
    uint16_t id;                        /* Counter ID. */
    uint64_t hits;                      /* Number of packets matched by the rule. */
};

struct rte_flow {
    enum rte_filter_type filter_type;
    void *rule;
    uint32_t counter_id;
};

enum {
    HNS5_INDIRECT_ACTION_TYPE_COUNT = 1,
};

struct rte_flow_action_handle {
    int indirect_type;
    uint32_t counter_id;
};

union hns5_filter_conf {
    struct hns5_fdir_rule fdir_conf;
};

struct hns5_filter_info {
    enum rte_filter_type type;
    union hns5_filter_conf conf;
};

int hns5_dev_filter_ctrl(struct rte_eth_dev *dev,
                         enum rte_filter_type filter_type,
                         enum rte_filter_op filter_op,
                         void *arg);
int hns5_flow_init(struct rte_eth_dev *dev);
void hns5_flow_uninit(struct rte_eth_dev *dev);

#endif /* _HNS5_FLOW_H_ */
