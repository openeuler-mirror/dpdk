/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Huawei Technologies Co., Ltd
 */
#ifndef HINIC3_VXLAN_DUMP
#define HINIC3_VXLAN_DUMP
#include "rte_flow.h"
#include "hinic3_message.h"
typedef struct rte_flow_action_vxlan_encap* rte_vxlan_encap;
rte_vxlan_encap hinic3_get_vxlan_gpe_header_items(struct hinic3_flow_act_vxlan_gpe_header *vxlan_header);
void hinic3_free_vxlan_header(struct rte_flow_action_vxlan_encap *vxlan_header);
#endif
