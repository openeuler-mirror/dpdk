/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2022 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_TRACE_FLOW_H
#define HINIC3_TRACE_FLOW_H

#include <stdint.h>
#include "hinic3_packet_key_public.h"
#include "hinic3_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HINIC3_MAX_TRACE_FLOW_NUM  20

enum hinic3_flow_trace_status {
    /* normal process info */
    HINIC3_FLOW_PKT_KEY_RESOLVE_DONE_TRACE,
    HINIC3_FLOW_AGENT_ERROR_INPUT_NOT_HIOVS_TRACE,
    HINIC3_FLOW_CONSTRUCT_KEY_DONE_TRACE,
    HINIC3_FLOW_AGENT_ERROR_TRANS_KEY_TRACE,
    HINIC3_FLOW_OFFLOAD_KEY_PROCESS_DONE_TRACE,
    HINIC3_FLOW_OFFLOAD_KEY_HASH_DONE_TRACE,
    HINIC3_FLOW_OFFLOAD_KEY_INSERT_TABLE_DONE_TRACE,
    HINIC3_FLOW_OFFLOAD_KEY_INSERT_TABLE_ERROR_TRACE,
    HINIC3_FLOW_COPY_RECIRCLE_ACT_DONE_TRACE,
    HINIC3_FLOW_PROCESS_OFFLOAD_ACTION_DONE_TRACE,
    HINIC3_FLOW_PROCESS_OFFLOAD_ACTION_ERROR_TRACE,
    HINIC3_BACKUP_CT_STATE_DONE_TRACE,
    HINIC3_FLOW_AGENT_ERROR_PROCESS_RECIRC_TRACE,
    HINIC3_PROCESS_OFFLOAD_KEY_ACT_DONE_TRACE,
    HINIC3_FLOW_AGENT_ERROR_PREPARE_OFFLOAD_ARGS_TRACE,
    HINIC3_PROCESS_OFFLOAD_ARGS_DONE_TRACE,
    HINIC3_FLOW_AGENT_ERROR_OFFLOAD_LIMITS_TRACE,
    HINIC3_FLOW_AGENT_ERROR_NO_CONN_TABLE_TRACE,
    HINIC3_FLOW_AGENT_STATS_OFFLOAD_DELAY_TRACE,
    HINIC3_FLOW_AGENT_ERROR_DUPLICATE_OFFLOAD_TRACE,
    HINIC3_AGENT_IS_FLOW_READY_PUT_DONE_TRACE,
    HINIC3_FLOW_AGENT_ERROR_CT_CHECK_FAIL_TRACE,
    HINIC3_FLOW_AGENT_ERROR_CT_NOT_READY_TRACE,
    HINIC3_AGENT_IS_CT_READY_TO_OFFLOAD_DONE_TRACE,
    HINIC3_FLOW_AGENT_ERROR_HARDWARE_FAIL_TRACE,
    HINIC3_AGENT_FLOW_PUT_DONE_TRACE,
    HINIC3_FLOW_TRACE_STATUS_MAX
};

struct hinic3_filter_tuple {
    struct hinic3_ip_addr src_ip;
    struct hinic3_ip_addr dst_ip;
    hinic3_be16 port_src;
    hinic3_be16 port_dst;
    uint8_t proto;
};

struct hinic3_trace_filter {
    struct hinic3_filter_tuple key;
    struct hinic3_filter_tuple mask;
    uint64_t status[HINIC3_FLOW_TRACE_STATUS_MAX];
};
struct hinic3_trace_flow {
    struct hinic3_trace_filter filter[HINIC3_MAX_TRACE_FLOW_NUM];
    struct hinic3_filter_tuple cur_key;
    int count;
};

void hinic3_trace_flow_init(struct hinic3_trace_flow *trace_flow_info);

void hinic3_trace_flow_info_update(int status, const struct hinic3_conntrack_key *key);

void unixctl_hinic3_trace_flow_init(void);

#ifdef __cplusplus
}
#endif

#endif
