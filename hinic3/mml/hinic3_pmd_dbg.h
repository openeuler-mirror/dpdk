/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) Huawei Technologies Co., Ltd. 2022. All rights reserved.
 */

#ifndef _HINIC3_PMD_MML_DBG_H
#define _HINIC3_PMD_MML_DBG_H

/*******************nic_tool******************/
struct hinic3_tx_hw_page {
    u64 *phy_addr;
    u64 *map_addr;
};

struct hinic3_dbg_sq_info {
    u16 q_id;
    u16 pi;
    u16 ci; /* sw_ci */
    u16 fi; /* hw_ci */

    u32 q_depth;
    u16 weqbb_size;

    volatile u16 *ci_addr;
    u64 cla_addr;

    struct hinic3_tx_hw_page db_addr;
    u32 pg_idx;
};

struct hinic3_dbg_rq_info {
    u16 q_id;
    u16 hw_pi;
    u16 ci; /* sw_ci */
    u16 sw_pi;
    u16 wqebb_size;
    u16 q_depth;
    u16 buf_len;

    void *ci_wqe_page_addr;
    void *ci_cla_tbl_addr;
    u16 msix_idx;
    u32 msix_vector;
};
/*******************nic_tool******************/

void *hinic3_dbg_get_sq_wq_handle(void *hwdev, u16 q_id);

void *hinic3_dbg_get_rq_wq_handle(void *hwdev, u16 q_id);

void *hinic3_dbg_get_sq_ci_addr(void *hwdev, u16 q_id);

u16 hinic3_dbg_get_global_qpn(void *hwdev);

int hinic3_dbg_get_sq_info(void *hwdev, u16 q_id, struct hinic3_dbg_sq_info *sq_info, u16 *msg_size);

int hinic3_dbg_get_rq_info(void *hwdev, uint16_t q_id, struct hinic3_dbg_rq_info *rq_info, u16 *msg_size);

int hinic3_dbg_get_rx_cqe_info(void *hwdev, uint16_t q_id, uint16_t idx, void *buf_out, uint16_t *out_size);

int hinic3_dbg_get_sq_wqe_info(void *hwdev, u16 q_id, u16 idx, u16 wqebb_cnt, u8 *wqe, u16 *wqe_size);

int hinic3_dbg_get_rq_wqe_info(void *hwdev, u16 q_id, u16 idx, u16 wqebb_cnt, u8 *wqe, u16 *wqe_size);

#endif
