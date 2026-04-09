/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2022 Huawei Technologies Co., Ltd
 */
#include "hinic3_log.h"
#include "hinic3_ufid_map_rte_flow.h"
#include "hinic3_flow_agent.h"
#include "hinic3_iface_flow_api_record.h"
#include "hinic3_query_flow.h"

#define HINIC3_LIVE_TIME_SHIFT 16u

static int
hinic3_flow_query_sub(struct rte_flow *flow, const struct rte_flow_action actions[] HINIC3_UNUSED,
    void *data, struct rte_flow_error *error)
{
    struct rte_flow_query_count *stat = NULL;
    stat = (struct rte_flow_query_count *)data;
    stat->hits_set = 1;
    stat->bytes_set = 1;
    stat->hits = 0;
    stat->bytes = 0;
    stat->reserved = 0;
    struct hash_table_node *rte_bucket = hinic3_get_flow_bucket(flow->flow_hash);
    hinic3_spinlock_lock(&rte_bucket->spinlock);

    if (flow->flags.is_mem_used == 0) {
        hinic3_spinlock_unlock(&rte_bucket->spinlock);
        stat->hits = 0;
        stat->bytes = 0;
        return 0;
    }
    if (flow->flags.is_offload == 0) {
        hinic3_spinlock_unlock(&rte_bucket->spinlock);
        return rte_flow_error_set(error, EBUSY, RTE_FLOW_ERROR_TYPE_UNSPECIFIED, NULL,
                                  "Flow query: Flow not completely offload.");
    }
    if (stat->reset == 1) {
        if (flow->sw_packets < flow->hw_packets) {
            stat->hits = flow->hw_packets - flow->sw_packets;
        }

        if (flow->sw_bytes < flow->hw_bytes) {
            stat->bytes = flow->hw_bytes - flow->sw_bytes;
        }

        flow->sw_packets = flow->hw_packets;
        flow->sw_bytes = flow->hw_bytes;
    } else {
        stat->hits = flow->hw_packets;
        stat->bytes = flow->hw_bytes;
    }

    stat->reserved = flow->flags.live_time;
    stat->reserved = stat->reserved << HINIC3_LIVE_TIME_SHIFT;
    hinic3_spinlock_unlock(&rte_bucket->spinlock);
    return 0;
}

int
hinic3_flow_query_emc(struct rte_eth_dev *dev HINIC3_UNUSED, struct rte_flow *flow,
    const struct rte_flow_action actions[], void *data, struct rte_flow_error *error)
{
    int ret;
    hinic3_rlock_flush_all();
    ret = hinic3_flow_query_sub(flow, actions, data, error);
    hinic3_runlock_flush_all();
    return ret;
}
