/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */
#ifndef HINIC3_AGENT_CMD_FORMAT_FLEXDA_H
#define HINIC3_AGENT_CMD_FORMAT_FLEXDA_H
#include "hinic3_util.h"
#include "hinic3_message.h"
#include "hinic3_nlattr.h"
#include "hinic3_flow_agent_public.h"
#include "hinic3_ds.h"
#include "hinic3_driver_public.h"
#include "hinic3_provider.h"
#include "hinic3_util.h"
#ifdef __cplusplus
extern "C" {
#endif

void hinic3_flexda_flow_key_format_output(const struct hinic3_nlattr *key, struct ds *ds);
void hinic3_flow_process_hydra_info(hinic3_hydra_type hydra_type, const hinic3_nlattr_itr nla, struct ds *ds);

#ifdef __cplusplus
}
#endif
#endif
