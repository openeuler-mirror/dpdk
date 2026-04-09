/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Huawei Technologies Co., Ltd
 */
#include "hinic3_agent_cmd_time.h"
#include <unistd.h>
#include "hinic3_util.h"
#include "hinic3_log.h"
#include "hinic3_ufid_map_rte_flow.h"
#include "hinic3_flow_agent.h"
#include "hinic3_iface_flow.h"
#include "hinic3_string_util.h"
#include "hinic3_parse_agent_config.h"
#include "hinic3_command.h"
#include "hinic3_ui_string.h"
#include "hinic3_ds.h"

struct hinic3_stat_ref_time_measure_task g_offload_stat_ref_time_task = {
    .basic.alive = false,
    .basic.mutex = HINIC3_MUTEX_INITIALIZER,
    .time_list = { { 0, 0 }, { 0, 0 } }
};

struct hinic3_time_measure_task g_offload_time_task = {
    .basic.alive = false,
    .basic.mutex = HINIC3_MUTEX_INITIALIZER,
    .time_list = { { 0, 0 }, { 0, 0 } }
};

bool
hinic3_is_offload_measure_alive(void)
{
    return g_offload_time_task.basic.alive;
}

bool
hinic3_is_stat_scan_measure_alive(void)
{
    return g_offload_stat_ref_time_task.basic.alive;
}
