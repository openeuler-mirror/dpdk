/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2022 Huawei Technologies Co., Ltd
 */

#include "hinic3_mtr.h"
#include "hinic3_meminfo.h"
#include "hinic3_qos.h"

struct rte_mtr_ops *g_hinic3_mtr_ops = NULL;

int
hinic3_mtr_ops_get(struct rte_eth_dev *dev HINIC3_UNUSED, void *ops)
{
    if (g_hinic3_mtr_ops == NULL) {
        g_hinic3_mtr_ops = hinic3_mtr_multi_ops_construct();
    }

    if (g_hinic3_mtr_ops == NULL) {
        return -1;
    }

    *(const struct rte_mtr_ops **)ops = g_hinic3_mtr_ops;
    return 0;
}
