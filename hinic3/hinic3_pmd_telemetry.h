/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2025-2026 Huawei Technologies Co., Ltd
 */

#ifndef _HINIC3_PMD_TELEMETRY_H_
#define _HINIC3_PMD_TELEMETRY_H_

#define HINIC_PF_MAX_SIZE 16
#define HINIC_VF_MAX_SIZE 4096
#define BUSINFO_LEN	  32

struct func_mbox_cnt_info {
	char bus_info[BUSINFO_LEN];
	u64 send_cnt;
	u64 ack_cnt;
};

struct mbox_cnt_info {
	struct func_mbox_cnt_info func_info[HINIC_PF_MAX_SIZE + HINIC_VF_MAX_SIZE];
	u32 func_num;
};

#endif /* _HINIC3_PMD_TELEMETRY_H_ */