/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2025 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_MEGA_DUMP_FORMAT_H
#define HINIC3_MEGA_DUMP_FORMAT_H

#include "hinic3_flow_format.h"

void hinic3_format_mega_flow(const struct hinic3_flow *flow, struct ds *ds);
int hinic3_format_mega_flow_items(const struct hinic3_flow *flow, struct ds *ds);
#endif