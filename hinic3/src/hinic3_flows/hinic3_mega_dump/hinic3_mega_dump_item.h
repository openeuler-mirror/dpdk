/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2025 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_MEGA_DUMP_ITEM_H
#define HINIC3_MEGA_DUMP_ITEM_H

#include "hinic3_driver_public.h"
#include "hinic3_flow_dump_public.h"

int hinic3_mega_item_build(struct hinic3_dpif_flow_for_get *get, struct hinic3_dump_flow_info *info);
void hinic3_mega_item_clean(struct hinic3_dump_flow_info *info);
#endif