/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2022 Huawei Technologies Co., Ltd
 */
#ifndef HINIC3_VXLAN_H
#define HINIC3_VXLAN_H

#include <ethdev_driver.h>
#include "rte_flow_driver.h"
#include "rte_flow.h"
#include "hinic3_log.h"
#include "hinic3_iface_global.h"

int hinic3_flow_tunnel_decap_set(struct rte_eth_dev *eth_dev, struct rte_flow_tunnel *tunnel,
    struct rte_flow_action **pmd_actions, uint32_t *num_of_actions, struct rte_flow_error *error);
uint32_t hinic3_get_hiovs_vni(const uint8_t *vni);
uint32_t hinic3_swap_vx_vni(const uint32_t vx_vni);
uint32_t hinic3_swap_endian32(uint32_t value);
#endif
