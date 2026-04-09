/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Huawei Technologies Co., Ltd
 */
#ifndef HINIC3_FLOW_DUMP_ACTION_H
#define HINIC3_FLOW_DUMP_ACTION_H

#include <stddef.h>
#include "hinic3_nlattr.h"
#include "rte_flow.h"
#include "hinic3_flow_dump_public.h"

void hinic3_free_one_flow_actions(struct rte_flow_action* actions, int action_num);
int hinic3_hinic3_flow_info_action_build(struct hinic3_dump_flow_info *flow, struct hinic3_nlattr_obj* dump_key,
                                       size_t key_length);
int hinic3_flow_dump_sample_conf_get(const hinic3_nlattr_itr nla, void **dst_conf);
int hinic3_flow_dump_acl_sample_conf_get(const hinic3_nlattr_itr nla, void **dst_conf);
int hinic3_build_flow_action_sub(struct hinic3_dump_flow_info *flow, const struct hinic3_nlattr *key);
#endif
