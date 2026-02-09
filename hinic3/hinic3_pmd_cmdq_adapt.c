/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2025 Huawei Technologies Co., Ltd
 */

#include "hinic3_pmd_cmdq_adapt.h"

static bool hinic3_is_htn_cmdq_support(struct hinic3_nic_dev *nic_dev)
{
	return hinic3_get_driver_feature(nic_dev) & NIC_F_HTN_CMDQ;
}

void hinic3_nic_cmdq_adapt_init(struct hinic3_nic_dev *nic_dev)
{
	if (!hinic3_is_htn_cmdq_support(nic_dev))
		nic_dev->cmdq_ops = hinic3_nic_cmdq_get_stn_ops();
	else
		nic_dev->cmdq_ops = hinic3_nic_cmdq_get_htn_ops();
}