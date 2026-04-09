/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2025 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_MEGA_ITEM_H
#define HINIC3_MEGA_ITEM_H

#include "hinic3_mega_key.h"
#include "hinic3_nlattr.h"

int hinic3_mega_offload_item_construct(const struct hinic3_mega_flow_full_key *mega_full_key,
    struct hinic3_nlattr *dst_key, struct hinic3_nlattr *dst_mask, struct rte_flow_error *error);

#endif