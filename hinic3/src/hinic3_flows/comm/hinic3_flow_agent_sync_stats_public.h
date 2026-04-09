/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Huawei Technologies Co., Ltd
 */
#ifndef HINIC3_FLOW_AGENT_SYNC_STATS_PUBLIC_H
#define HINIC3_FLOW_AGENT_SYNC_STATS_PUBLIC_H

#include "hinic3_flow_agent_public.h"

#define HINIC3_STATS_SYNC_INTERVAL 1500 /* 1.5s */
#define HINIC3_MAX_STATS_SYNC_INTERVAL 3000 /* 3.0s */
#define HINIC3_EVERY_STATS_SYNC_INTERVAL 100 /* 0.1s */
#define HINIC3_MAX_OFFLOAD_RATE 50
#define HINIC3_STATS_SYNC_PKT_MIN 8
#define HINIC3_MAX_ENTRY_PER_STATS_DUMP 1000
#define HINIC3_FLOW_FACTOR 4
#define HINIC3_MAX_FLOW_SIZE 500000
#define HINIC3_FLOW_STATUS_DEAD 1

static inline bool hinic3_stats_sync_pull_mode(const struct hinic3_flow_agent_db *hw_offload)
{
    return hw_offload->forward_engine.cap.flags & (1 << HINIC3_ENG_CAP_F_STATS_SYNC_PULL);
}

static inline bool hinic3_stats_sync_push_mode(const struct hinic3_flow_agent_db *hw_offload)
{
    return !hinic3_stats_sync_pull_mode(hw_offload);
}

#endif
