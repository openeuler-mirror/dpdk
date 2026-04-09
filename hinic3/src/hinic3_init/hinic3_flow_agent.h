/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_FLOW_AGENT_H
#define HINIC3_FLOW_AGENT_H

#include <stdint.h>
#include "hinic3_flow_agent_public.h"
#include "hinic3_error_stats.h"

#define HINIC3_THREAD_NAME_MAX 16
#define HINIC3_FLOW_LIVE_TIME               10000 /* ms */
#define HINIC3_MSEC_TO_SEC(XMS) ((XMS) / 1000)

#define CFG_INT_LEN 10
#define AGENT_SECTION "hwoff_agent"

struct hinic3_agent_hw_flow_del_context {
    uint32_t target_flow_pps;
    struct hinic3_flow_agent_db *hinic3_db;
};

struct hinic3_flow_time {
    double time; /* 记录流表卸载时长 单位为s */
    uint32_t update; /* 更新标记，1表示更新，0表示此时间数据已被采集过，不是最新数据 */
};

struct hinic3_ct_tcp_state {
    uint32_t seqlo;      /* Max sequence number sent, network order */
    uint32_t seqhi;        /* Max the other end ACKd + win, network order */
    uint16_t max_win;  /* Largest window (pre scaling), network order */
    uint8_t wscale;       /* Window scaling factor */
    uint8_t state;         /* see enum hinic3_ct_tcp_state_type */
    uint32_t r_seqhi;   /* Max allowed seqence of reverse, network order */
};

struct hinic3_dp_extend_info *hinic3_get_offload_extend_info(void);
void hinic3_inc_offload_flow_nums(void);
int hinic3_flow_agent_construct(void);
void hinic3_flow_agent_destruct(void);
void hinic3_dec_offload_flow_nums(void);
int32_t hinic3_get_offload_flow_nums(void);
void hinic3_reset_offload_flow_nums(void);
int hinic3_flow_callback_register(void);
struct hinic3_flow_time *hinic3_get_flow_offload_time(void);
int hinic3_get_thread_core_isolation(struct hinic3_dp_extend_info *dp_info);
int hinic3_offload_extend_init(void);
int hinic3_create_offload_threads(struct hinic3_dp_extend_info *dp_info);
void hinic3_offload_extend_deinit(void);
#endif
