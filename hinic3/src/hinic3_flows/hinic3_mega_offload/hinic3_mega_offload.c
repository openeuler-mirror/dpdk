/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2025 Huawei Technologies Co., Ltd
 */

#include "hinic3_mutex.h"
#include "hinic3_ufid_map_rte_flow.h"
#include "hinic3_error_stats.h"
#include "hinic3_flow_session.h"
#include "hinic3_iface_flow.h"
#include "hinic3_mega_dump.h"
#include "hinic3_mega_item.h"
#include "hinic3_mega_offload.h"

#define HINIC3_MEGA_FULL (-2)
#define HINIC3_MEGA_KEY_NLA_BUFFER_LENGTH  512

static struct hinic3_mega_table g_hinic3_mega_flow_table;

const struct hinic3_mega_table*
hinic3_get_mega_table(void)
{
    return &g_hinic3_mega_flow_table;
}

int
hinic3_mega_flow_init(void)
{
    if(hinic3_check_fuzzy_flow_l3_forward_switch() == true) {
        int ret = hinic3_mega_flow_set_l3_forward(true);
        if (ret != 0){
            HINIC3_LOG(ERR, AGENT, "Failed to init mega flow l3 forward.");
            return ret;
        }
    }

    memset(&g_hinic3_mega_flow_table, 0, sizeof(struct hinic3_mega_table));
    hinic3_pthread_mutex_init(&g_hinic3_mega_flow_table.mutex_lock);
    hinic3_mega_flow_dump_init();
    HINIC3_LOG(INFO, AGENT, "l3 forward mega flow is enabled.");

    return 0;
}

static int
hinic3_insert_mega_flow_into_table(struct hinic3_mega_flow *flow)
{
    struct hinic3_mega_table *mega_table = &g_hinic3_mega_flow_table;
    for (int i = 0; i < HINIC3_MEGA_FLOW_MAX_NUM; ++i) {
        if (mega_table->flow_table[i].is_used == 0) {
            flow->index = i;

            memcpy(&mega_table->flow_table[i].flow, flow, sizeof(struct hinic3_mega_flow));
            mega_table->flow_table[i].is_used = 1;
            mega_table->flow_table[i].raw_mega_flow = HINIC3_CONTAINER_OF(flow, struct mega_flow, mega_flow);
            mega_table->flow_num++;
            return 0;
        }
    }

    return HINIC3_MEGA_FULL;
}

static bool
hinic3_mega_flow_table_del(const struct hinic3_mega_flow *flow)
{
    struct hinic3_mega_table *mega_table = &g_hinic3_mega_flow_table;
    if (memcmp(&mega_table->flow_table[flow->index].flow, flow, sizeof(struct hinic3_mega_flow)) != 0) {
        return false;
    }

    memset(&mega_table->flow_table[flow->index], 0, sizeof(struct hinic3_mega_flow_node));
    mega_table->flow_table[flow->index].raw_mega_flow = NULL;
    mega_table->flow_num--;

    return true;
}

static bool
hinic3_mega_flow_repeated_offload(const struct hinic3_mega_flow *flow)
{
    struct hinic3_mega_table *mega_table = &g_hinic3_mega_flow_table;
    for (int i = 0; i < HINIC3_MEGA_FLOW_MAX_NUM; ++i) {
        if (mega_table->flow_table[i].is_used == 0)
            continue;

        if (memcmp(&(mega_table->flow_table[i].flow.full_key), &flow->full_key,
                sizeof(struct hinic3_mega_flow_full_key)) == 0) {
            return true;
        }
    }
    return false;
}

static int
hinic3_mega_flow_offload_execute(struct hinic3_dpif_flow *dpif_flow,
    struct hinic3_mega_flow *mega_flow, struct rte_flow_error *error)
{
    int ret = 0;

    hinic3_pthread_mutex_lock(&g_hinic3_mega_flow_table.mutex_lock);
    ret = hinic3_insert_mega_flow_into_table(mega_flow);
    if (ret != 0) {
        hinic3_pthread_mutex_unlock(&g_hinic3_mega_flow_table.mutex_lock);
        return rte_flow_error_set(error, EPERM, RTE_FLOW_ERROR_TYPE_UNSPECIFIED, NULL,
            "mega flow offload: mega flow table is full.");
    }

    ret = hinic3_mega_flow_put(dpif_flow, mega_flow->index, &(mega_flow->ufid));
    if (ret != 0) {
        hinic3_mega_flow_table_del(mega_flow);
        hinic3_pthread_mutex_unlock(&g_hinic3_mega_flow_table.mutex_lock);
        return rte_flow_error_set(error, -ret, RTE_FLOW_ERROR_TYPE_STATE, NULL,
            "mega flow offload: Driver interface error.");
    }

    g_hinic3_mega_flow_table.flow_table[mega_flow->index].flow.ufid = mega_flow->ufid;
    hinic3_pthread_mutex_unlock(&g_hinic3_mega_flow_table.mutex_lock);

    return 0;
}

struct rte_flow *
hinic3_mega_flow_offload(const struct rte_flow_item pattern[], struct rte_flow_error *error)
{
    if (pattern == NULL || error == NULL) {
        rte_flow_error_set(error, EINVAL, RTE_FLOW_ERROR_TYPE_UNSPECIFIED, NULL,
            "hinic3_mega_flow_offload: invalid parameter.");
        return NULL;
    }

    int ret = INT_MAX;
    struct hinic3_dpif_flow dpif_flow = {0};
    struct hinic3_nlattr key = {0};
    struct hinic3_nlattr mask = {0};
    struct hinic3_nlattr nla_action = { 0 };
    uint8_t key_buffer[HINIC3_MEGA_KEY_NLA_BUFFER_LENGTH] = {0};
    uint8_t mask_buffer[HINIC3_MEGA_KEY_NLA_BUFFER_LENGTH] = {0};
    uint8_t action_buffer[HINIC3_MEGA_KEY_NLA_BUFFER_LENGTH] = {0};
    hinic3_nlattr_init(&key, key_buffer, HINIC3_MEGA_KEY_NLA_BUFFER_LENGTH);
    hinic3_nlattr_init(&mask, mask_buffer, HINIC3_MEGA_KEY_NLA_BUFFER_LENGTH);
    hinic3_nlattr_init(&nla_action, action_buffer, HINIC3_MEGA_KEY_NLA_BUFFER_LENGTH);

    struct mega_flow *flow = hinic3_mega_flow_alloc();
    if (flow == NULL) {
        rte_flow_error_set(error, ENOMEM, RTE_FLOW_ERROR_TYPE_UNSPECIFIED, NULL,
            "hinic3_mega_flow_offload: alloc mpool for mega flow failed.");
        return NULL;
    }

    ret = hinic3_mega_full_key_construct(pattern, &flow->mega_flow.full_key, error);
    if (ret != 0)
        goto dealloc;

    if (hinic3_mega_flow_repeated_offload(&flow->mega_flow)) {
        rte_flow_error_set(error, EEXIST, RTE_FLOW_ERROR_TYPE_UNSPECIFIED, NULL,
            "mega flow offload: repeated offload.");
        goto dealloc;
    }

    ret = hinic3_mega_offload_item_construct(&flow->mega_flow.full_key, &key, &mask, error);
    if (ret != 0)
        goto dealloc;

    dpif_flow.key = key.data;
    dpif_flow.key_len = key.used_len;
    dpif_flow.mask = mask.data;
    dpif_flow.mask_len = mask.used_len;
    dpif_flow.actions = nla_action.data;
    dpif_flow.action_len = nla_action.used_len;
    ret = hinic3_mega_flow_offload_execute(&dpif_flow, &flow->mega_flow, error);
    if (ret != 0)
        goto dealloc;

    flow->flow.flags.is_mega = 1;
    return (struct rte_flow *)flow;
dealloc:
    hinic3_mega_flow_dealloc(flow);
    return NULL;
}

int
hinic3_mega_flow_delete(struct rte_eth_dev *dev __rte_unused, struct rte_flow *flow, struct rte_flow_error *error)
{
    struct mega_flow *mega = (struct mega_flow *)flow;
    struct hinic3_mega_flow *mega_flow = &mega->mega_flow;
    hinic3_pthread_mutex_lock(&g_hinic3_mega_flow_table.mutex_lock);
    int ret = hinic3_mega_flow_del_by_ufid(mega_flow->ufid);
    if (ret != 0) {
        hinic3_pthread_mutex_unlock(&g_hinic3_mega_flow_table.mutex_lock);
        return rte_flow_error_set(error, -ret, RTE_FLOW_ERROR_TYPE_STATE, NULL,
            "mega flow destroy: Driver interface error.");
    }

    if (flow->flags.is_sample == HINIC3_FLOW_MEGA_MIRROR) {
        ret = hinic3_del_rte_flow_in_session(flow);
        if (ret != 0)
            hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_MEGA_DEL_FLOW_IN_SESSION, 1);
    }

    bool is_deleted = hinic3_mega_flow_table_del(mega_flow);
    if (is_deleted == false) {
        hinic3_pthread_mutex_unlock(&g_hinic3_mega_flow_table.mutex_lock);
        return rte_flow_error_set(error, EPERM, RTE_FLOW_ERROR_TYPE_UNSPECIFIED, NULL,
            "mega flow destroy: delete in table failed.");
    }
    hinic3_mega_flow_dealloc(mega);
    hinic3_pthread_mutex_unlock(&g_hinic3_mega_flow_table.mutex_lock);
    return 0;
}

int
hinic3_mega_flow_flush_by_port(uint16_t port_id, struct rte_flow_error *error)
{
    int ret = INT_MAX;
    struct mega_flow *flow = NULL;
    struct hinic3_mega_flow_node *mega_flow_node = NULL;

    for (int i = 0; i < HINIC3_MEGA_FLOW_MAX_NUM; ++i) {
        hinic3_pthread_mutex_lock(&g_hinic3_mega_flow_table.mutex_lock);
        mega_flow_node = &g_hinic3_mega_flow_table.flow_table[i];
        if (mega_flow_node == NULL || mega_flow_node->is_used == false) {
            hinic3_pthread_mutex_unlock(&g_hinic3_mega_flow_table.mutex_lock);
            continue;
        }
        flow = mega_flow_node->raw_mega_flow;
        hinic3_pthread_mutex_unlock(&g_hinic3_mega_flow_table.mutex_lock);

        if (flow->flow.port_id == port_id) {
            ret = hinic3_mega_flow_delete(NULL, (struct rte_flow *)flow, error);
            if (ret != 0)
                return ret;
        }
    }

    return 0;
}

int
hinic3_mega_flow_query(struct rte_eth_dev *dev __rte_unused, struct rte_flow *flow, void *data, struct rte_flow_error *error)
{
    if (flow == NULL)
        return -EINVAL;
    
    struct mega_flow *mega = (struct mega_flow *)flow;
    struct hinic3_mega_flow *mega_flow = &mega->mega_flow;
    struct hinic3_flow_stats flow_stats = {0};
    struct rte_flow_query_count *stat = (struct rte_flow_query_count *)data;
    stat->hits = 0;
    stat->bytes = 0;
    stat->hits_set = 1;
    stat->bytes_set = 1;
    hinic3_pthread_mutex_lock(&g_hinic3_mega_flow_table.mutex_lock);
    uint64_t ufid = mega_flow->ufid;
    int ret = hinic3_statistics_mega_flow_get_by_ufid(ufid, &flow_stats);
    if (ret != 0) {
        hinic3_pthread_mutex_unlock(&g_hinic3_mega_flow_table.mutex_lock);
        return rte_flow_error_set(error, -ret, RTE_FLOW_ERROR_TYPE_STATE, NULL,
            "mega flow query: Driver interface error.");
    }
    stat->hits = flow_stats.packet_count;
    stat->bytes = flow_stats.byte_count;
    hinic3_pthread_mutex_unlock(&g_hinic3_mega_flow_table.mutex_lock);

    return 0;
}