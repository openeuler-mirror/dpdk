/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_DUMP_FLOW_H
#define HINIC3_DUMP_FLOW_H

#include <string.h>
#include <stdbool.h>
#include "rte_log.h"
#include "rte_ethdev.h"
#include "rte_cycles.h"
#include "hinic3_init.h"
#include "hinic3_message.h"
#include "hinic3_log.h"
#include "hinic3_flow_agent_public.h"
#include "hinic3_iface_port.h"
#include "hinic3_offload_action_public.h"
#include "hinic3_iface_flow.h"

int hinic3_eth_flow_dev_dump(struct rte_eth_dev *dev, struct rte_flow *flow, FILE *file, struct rte_flow_error *error);

#endif
