/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_OFFLOAD_ACTION_PUBLIC_H
#define HINIC3_OFFLOAD_ACTION_PUBLIC_H

#include "hinic3_message.h"
#include "hinic3_flow_agent.h"
#include "hinic3_offload_flow_public.h"
#include "hinic3_log.h"

#define HINIC3_AGED_TIME_MIN 30
#define HINIC3_AGED_TIME_MAX 2047

int hinic3_offload_parse_vxlan_act(struct hinic3_offload_action *offload_action,
                                  const struct rte_flow_action_vxlan_encap *vxlan_info);
int hinic3_offload_parse_action_sub(const struct rte_flow_action *act,
                                   struct hinic3_offload_action *offload_action, struct rte_flow* mega_flow);
int hinic3_offload_copy_current_action(const struct hinic3_nlattr *cur_action, struct hinic3_nlattr *final_action);
bool hinic3_offload_check_actions(struct hinic3_nlattr *hinic3_actions);
bool hinic3_offload_flow_actions_check(struct rte_flow_action *action, size_t nums);
int hinic3_offload_fill_vxlan_header(struct hinic3_flow_act_vxlan_gpe_header *offload_vxlan_hdr,
    const struct rte_flow_action_vxlan_encap *vxlan_info);
bool hinic3_offload_flow_actions_check(struct rte_flow_action *action, size_t nums);

#endif
