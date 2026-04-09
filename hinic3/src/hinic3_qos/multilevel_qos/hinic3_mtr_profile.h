/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2022 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_MTR_PROFILE_H
#define HINIC3_MTR_PROFILE_H

#include "hinic3_list.h"
#include "hinic3_mutex.h"
#include "ethdev_driver.h"
#include "hinic3_vf_port_qos_public.h"

struct hinic3_mtr_profile_node {
    uint32_t used_num;
    struct hinic3_list node;
    uint32_t profile_id;
    struct qos_single_value profile;
};

struct hinic3_mtr_profile_list {
    struct hinic3_list node;
    struct hinic3_mutex mutex;
    uint32_t length;
};

void hinic3_mtr_profile_list_lock(void);
void hinic3_mtr_profile_list_unlock(void);
struct hinic3_mtr_profile_list *hinic3_profile_list_get(void);
struct hinic3_mtr_profile_node *hinic3_mtr_profile_find(uint32_t profile_id);
void hinic3_meter_profile_list_init(void);
int hinic3_multi_meter_profile_del(struct rte_eth_dev *dev, uint32_t profile_id, struct rte_mtr_error *error);
int hinic3_multi_meter_profile_add(struct rte_eth_dev *dev, uint32_t meter_profile_id,
    struct rte_mtr_meter_profile *profile, struct rte_mtr_error *error);

#endif
