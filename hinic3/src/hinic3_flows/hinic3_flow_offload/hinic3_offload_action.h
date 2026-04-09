/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2022 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_OFFLOAD_ACTION_H
#define HINIC3_OFFLOAD_ACTION_H

#include <stdint.h>
#include "hinic3_offload_flow.h"

int hinic3_offload_parse_action(const struct rte_flow_action actions[], struct rte_flow *mega_flow,
    struct hinic3_flow_offload_param *param, uint8_t mirror_dir_flag);
uint32_t hinic3_process_port_id(uint32_t port_id);
#endif
