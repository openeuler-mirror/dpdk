/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_AGE_DELETE_FLOW_PUBLIC_H
#define HINIC3_AGE_DELETE_FLOW_PUBLIC_H

#include <arpa/inet.h>
#include <stdint.h>
#include "rte_ethdev.h"
#include "rte_flow.h"
#include "hinic3_flow_agent_public.h"
#include "hinic3_driver_public.h"

void hinic3_thread_rx_hw_age_info(uint32_t thread_id, const struct hinic3_dp_extend_info *dp_info);

#endif
