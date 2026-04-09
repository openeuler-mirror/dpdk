 /* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_IFACE_GLOBAL_API_RECORD_H
#define HINIC3_IFACE_GLOBAL_API_RECORD_H

#include "hinic3_iface_global.h"

void hinic_global_api_record_error(int ret_code, hinic3_port_api api_index, uint64_t exec_time);
void hinic_global_fill_api_record(hinic3_global_api api_index, uint64_t exec_time);
hiovs_api_record *hinic3_get_global_api_record(void);

#endif
