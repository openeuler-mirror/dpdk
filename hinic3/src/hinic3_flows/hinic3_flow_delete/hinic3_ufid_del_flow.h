/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2024 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_UFID_DEL_FLOW_H
#define HINIC3_UFID_DEL_FLOW_H

void unixctl_hinic3_delete_cmd_init(void);
int hinic3_del_flow_qos_by_meter_id(uint32_t meter_id);
#endif