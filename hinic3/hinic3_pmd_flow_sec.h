/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2025-2026 Huawei Technologies Co., Ltd
 */

#ifndef _HINIC3_PMD_FLOW_SEC_H
#define _HINIC3_PMD_FLOW_SEC_H

#include "hinic3_pmd_fdir.h"

#define HINIC3_FDIR_EXT_320 0
#define HINIC3_FDIR_EXT_640 1

int
hinic3_flow_parse_sec_fdir_pattern(__rte_unused struct rte_eth_dev *dev,
                   const struct rte_flow_item      *pattern,
                   struct rte_flow_error          *error,
                   struct hinic3_filter_t         *filter);

int hinic3_flow_add_del_sec_fdir_filter(struct rte_eth_dev *dev,
					struct hinic3_sec_fdir_filter *sec_fdir_filter,
					struct hinic3_fdir_filter *fdir_ctrl,
					bool add);

#endif /* _HINIC3_PMD_FLOW_SEC_H */
