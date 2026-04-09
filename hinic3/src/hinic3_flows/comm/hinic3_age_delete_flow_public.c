/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Huawei Technologies Co., Ltd
 */

#include "hinic3_age_delete_flow_public.h"

#include "hinic3_iface_flow.h"
#include "hinic3_provider.h"
#include "hinic3_iface_global.h"
#include "hinic3_flow_agent.h"
#include "hinic3_log.h"
#define HINIC3_AGE_RX_BURST_MAX        8

void hinic3_thread_rx_hw_age_info(uint32_t thread_id, const struct hinic3_dp_extend_info *dp_info)
{
    if (dp_info == NULL) {
        hinic3_add_error_stats(HINIC3_FLOW_ERROR_RX_HW_AGE_ERR, 1);
        return;
    }
    struct rte_mbuf *buffer[HINIC3_THREAD_RX_BURST_MAX] = {0};
    (void)hinic3_global_rte_eth_rx_burst((uint16_t)thread_id, (uint16_t)HINIC3_PTHREAD_AGING, buffer,
                                        HINIC3_AGE_RX_BURST_MAX);
}
