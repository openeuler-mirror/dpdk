/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Huawei Technologies Co., Ltd
 */

#include "rte_lcore.h"
#include "rte_flow.h"
#include "hinic3_message.h"
#include "hinic3_flow_agent.h"
#include "hinic3_iface_global.h"
#include "hinic3_iface_flow.h"
#include "hinic3_log.h"
#include "hinic3_flow_agent_public.h"
#include "hinic3_offload_flow_public.h"

void hinic3_offload_thread_rx_put_ack(uint32_t thread_id, struct hinic3_dp_extend_info *dp_info)
{
    struct rte_mbuf *buffer[HINIC3_THREAD_RX_BURST_MAX] = { 0 };

    if (dp_info == NULL) {
        return;
    }
    (void)hinic3_global_rte_eth_rx_burst(thread_id, HINIC3_PTHREAD_ACK, buffer, HINIC3_THREAD_RX_BURST_MAX);
}

uint32_t hinic3_convert_u8_to_u32(const uint8_t *src_array, uint8_t len)
{
    if (src_array == NULL) {
        return 0;
    }
    uint32_t target_value = 0;
    if (len <= 0 || len > sizeof(uint32_t)) {
        return 0;
    }
    for (int i = len - 1; i >= 0; i--) {
        target_value += src_array[i];
        if (i == 0) {
            break;
        }
        target_value <<= HINIC3_UINT8_SHIFT;
    }
    return target_value;
}

void hinic3_free_pmd_pkt_info(const struct hinic3_inner_metadata *udata64)
{
    struct hinic3_pmd_status *pmd_status = NULL;
    struct hinic3_pkt_info *the_pkt_info = NULL;
    struct hinic3_flow_agent_db *hw_offload = NULL;
    struct hinic3_dp_extend_info *offload_extend_info = hinic3_get_offload_extend_info();

    if (offload_extend_info == NULL)
        return;

    hw_offload = offload_extend_info->hw_offload;
    if ((udata64->lcore_idx >= hw_offload->pmd_status_num) || (udata64->offset >= MAX_PKT_BURST)) {
        HINIC3_LOG(ERR, FLOW, "Release pmd pkt info error, pkt udata64"
                " lcore id %hu or offset %hhu is out of range!", udata64->lcore_idx, udata64->offset);
        return;
    }

    pmd_status = hinic3_pmd_status_get(udata64->lcore_idx);
    for (int i = 0; i < MAX_PKT_BURST; i++) {
        the_pkt_info = pmd_status->pkt_info_bufs + i;
        the_pkt_info->is_used = 0;
        the_pkt_info->flows_num = 0;
    }
}