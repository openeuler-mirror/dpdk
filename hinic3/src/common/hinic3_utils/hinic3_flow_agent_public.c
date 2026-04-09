/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2026 Huawei Technologies Co., Ltd
 */

#include "hinic3_flow_agent_public.h"

static struct hinic3_pmd_status g_hinic3_pmd_cache[MAX_PMD_CORE] = {0};

void hinic3_pmd_status_init(void)
{
    for (int i = 0; i < MAX_PMD_CORE; i++) {
        g_hinic3_pmd_cache[i].rx_seq = i;
    }
}

struct hinic3_pmd_status *hinic3_pmd_status_get(uint32_t lcore_idx)
{
    return &g_hinic3_pmd_cache[lcore_idx];
}