/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2022 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_MTR_POLICY_H
#define HINIC3_MTR_POLICY_H

#include "hinic3_list.h"
#include "hinic3_mutex.h"
#include "rte_mtr.h"
#include "ethdev_driver.h"

struct hinic3_mtr_policy_node {
    uint32_t used_num;
    uint32_t policy_id;
    uint32_t next_meter_id;
    bool has_next_meter;
    struct hinic3_list node;
};

struct hinic3_mtr_policy_list {
    struct hinic3_mutex mutex;
    struct hinic3_list node;
    uint32_t length;
};

struct hinic3_mtr_policy_list *hinic3_policy_list_get(void);
void hinic3_mtr_policy_list_lock(void);
void hinic3_mtr_policy_list_unlock(void);
struct hinic3_mtr_policy_node *hinic3_mtr_policy_find(uint32_t policy_id);
int hinic3_meter_policy_delete(struct rte_eth_dev *dev, uint32_t policy_id, struct rte_mtr_error *error);
int hinic3_meter_policy_add(struct rte_eth_dev *eth_dev, uint32_t policy_id,
    struct rte_mtr_meter_policy_params *policy, struct rte_mtr_error *error);
void hinic3_mtr_policy_list_init(void);

#endif
