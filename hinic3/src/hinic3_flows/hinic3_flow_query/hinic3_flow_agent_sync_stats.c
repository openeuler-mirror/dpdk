/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2022 Huawei Technologies Co., Ltd
 */
#include "hinic3_flow_agent_sync_stats.h"
#include <stdlib.h>
#include "hinic3_util.h"
#include "rte_ethdev.h"
#include "rte_cycles.h"
#include "rte_flow.h"
#include "hinic3_iface_flow.h"
#include "hinic3_ufid_map_rte_flow.h"
#include "hinic3_log.h"
#include "hinic3_flow_agent.h"
#include "hinic3_timeval.h"
#include "hinic3_meminfo.h"
#include "hinic3_parse_agent_config.h"
#include "hinic3_error_stats.h"

#define SEC_TO_MSEC_BASE 1000
#define LIVE_TIME_SHIFT 13u
#define MAX_LIVE_TIME ((1 << LIVE_TIME_SHIFT) - 1)

static bool hinic3_check_offload_extend_info(struct hinic3_dp_extend_info *dp_info, long long now,
    uint32_t thread_id)
{
    struct hinic3_flow_agent_db *hw_offload = dp_info->hw_offload;

    if (hw_offload == NULL) {
        HINIC3_LOG(WARNING, FLOW, "hinic3_check_offload_extend_info hw_offload is null.");
        return false;
    }

    bool is_push_mode = hinic3_stats_sync_push_mode(hw_offload);
    if (!is_push_mode) {
        HINIC3_LOG(WARNING, FLOW, "offload thread is not in sync pull mode.");
        return false;
    }


    if (hw_offload->next_hw_stats_sync[thread_id] > now) {
        return false;
    }

    bool is_hmap_empty = hinic3_is_rte_hmap_empty();
    if (is_hmap_empty) {
        return false;
    }

    return true;
}

static void hinic3_flow_agent_sync_hw_flow_stats(const struct hinic3_dp_extend_info *dp_info, uint64_t hw_ufid,
    struct rte_flow *flow, struct hinic3_flow_stats *stats)
{
    if (dp_info == NULL || dp_info->hw_offload == NULL) {
        HINIC3_LOG(WARNING, FLOW, "sync single flow pointer is null.");
        return;
    }

    struct hinic3_flow_stats stats_buf;

    if (flow == NULL || flow->flags.is_offload == 0) {
        return;
    }

    if (stats == NULL) {
        stats = &stats_buf;
        /* Get stats from hardware */
        if (hinic3_statistics_flow_get_by_ufid(&hw_ufid, 1, stats, flow->table_id) != 0) {
            hinic3_add_error_stats(HINIC3_FLOWS_ERROR_GET_HW_STATS_BY_UFID, 1);
            return;
        }
    }

    flow->hw_packets = stats->packet_count;
    flow->hw_bytes = stats->byte_count;

    if (stats->live_time / SEC_TO_MSEC_BASE > MAX_LIVE_TIME) {
        flow->flags.live_time = MAX_LIVE_TIME;
    } else {
        flow->flags.live_time = (uint16_t)(stats->live_time / SEC_TO_MSEC_BASE);
    }

    return;
}

static int hinic3_sync_hardware_flow_statistics(const uint64_t *ufid, const size_t cnt,
    struct hinic3_stats_dump_context *sync_ctx, uint8_t table_id)
{
    if (hinic3_statistics_flow_get_by_ufid(ufid, cnt, sync_ctx->stats, table_id) != 0) {
        hinic3_add_error_stats(HINIC3_FLOWS_ERROR_GET_HW_STATS_BY_UFID, 1);
        return -1;
    }

    for (size_t idx = 0; idx < cnt; idx++) {
        hinic3_flow_agent_sync_hw_flow_stats(sync_ctx->dp_info, sync_ctx->ufids[idx], sync_ctx->priv_data[idx],
            &sync_ctx->stats[idx]);
    }
    return 0;
}

static int hinic3_flow_agent_hw_iterate_cb(struct rte_flow *flow_data,
    struct traverse_data *cur_traverse_data)
{
    struct hinic3_stats_dump_context *sync_ctx = NULL;
    struct rte_flow *flow = NULL;
    bool is_end;

    if (cur_traverse_data == NULL) {
        return -1;
    }

    sync_ctx = (struct hinic3_stats_dump_context *)cur_traverse_data->para;
    is_end = cur_traverse_data->is_end;

    if (is_end == true) {
        hinic3_sync_hardware_flow_statistics(sync_ctx->ufids, sync_ctx->num_entries, sync_ctx, sync_ctx->table_id);
        sync_ctx->num_entries = 0;
        return 0;
    }

    if (flow_data == NULL) {
        return -1;
    }
    flow = flow_data;
    if (flow->flags.is_offload == 0) {
        return 0;
    }

    sync_ctx->priv_data[sync_ctx->num_entries] = flow;
    sync_ctx->ufids[sync_ctx->num_entries] = flow->hw_ufid;
    if ((++sync_ctx->num_entries) < HINIC3_MAX_ENTRY_PER_STATS_DUMP) {
        return 0;
    }

    /* Get stats from hardware */
    hinic3_sync_hardware_flow_statistics(sync_ctx->ufids, sync_ctx->num_entries, sync_ctx, sync_ctx->table_id);

    sync_ctx->num_entries = 0;
    sync_ctx->total_hw_num += HINIC3_MAX_ENTRY_PER_STATS_DUMP;
    return 0;
}

int hinic3_sync_flow_statistics(uint32_t thread_id, struct hinic3_dp_extend_info *dp_info)
{
    struct hinic3_flow_agent_db *hw_offload = NULL;
    struct hinic3_stats_dump_context *sync_ctx = NULL;
    struct hinic3_offload_thread_data *thread_data = NULL;

    long long now = hinic3_time_msec();
    if (dp_info == NULL) {
        HINIC3_LOG(ERR, FLOW, "offload thread :%" PRIu32 " sync operate parameter point is null.", thread_id);
        return -1;
    }

    if (!hinic3_check_offload_extend_info(dp_info, now, thread_id)) {
        return -1;
    }

    hw_offload = dp_info->hw_offload;
    sync_ctx = (struct hinic3_stats_dump_context *)hinic3_calloc(1, sizeof(struct hinic3_stats_dump_context), HINIC3_FLOWS);
    if (sync_ctx == NULL) {
        HINIC3_LOG(ERR, FLOW, "alloc sync_ctx failed.");
        return -1;
    }
    sync_ctx->dp_info = dp_info;
    sync_ctx->num_entries = 0;
    sync_ctx->total_hw_num = 0;
    sync_ctx->start_time = now;
    thread_data = &dp_info->offload_threads[thread_id];

    /* get time stamps */
    if (thread_data->cur_buk_idx == thread_data->min_buk_idx) {
        if (hinic3_statistics_flow_get_by_ufid(NULL, thread_id, NULL, 0) != 0) {
            hinic3_free(sync_ctx);
            hinic3_add_error_stats(HINIC3_FLOWS_ERROR_GET_TIME_STAMP, 1);
            return -1;
        }
    }

    hinic3_dump_rte_flow_by_hw_ufid(thread_id, &thread_data->cur_buk_idx, thread_data->max_buk_idx,
        (uint8_t *)sync_ctx, hinic3_flow_agent_hw_iterate_cb);

    if (thread_data->cur_buk_idx >= thread_data->max_buk_idx) {
        now = hinic3_time_msec();
        hw_offload->next_hw_stats_sync[thread_id] = now + HINIC3_STATS_SYNC_INTERVAL;
        thread_data->cur_buk_idx = thread_data->min_buk_idx;
    }
    hinic3_free(sync_ctx);
    return 0;
}
