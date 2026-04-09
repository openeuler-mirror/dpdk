/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2022 Huawei Technologies Co., Ltd
 */
#ifndef HINIC3_FLOW_AGENT_SYNC_STATS_H
#define HINIC3_FLOW_AGENT_SYNC_STATS_H

#include "hinic3_ufid_map_rte_flow.h"
#include "hinic3_flow_agent_sync_stats_public.h"

int hinic3_sync_flow_statistics(uint32_t thread_id, struct hinic3_dp_extend_info *dp_info);
#endif
