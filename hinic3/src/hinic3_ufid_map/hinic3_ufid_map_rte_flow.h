/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_UFID_MAP_RTE_FLOW_H
#define HINIC3_UFID_MAP_RTE_FLOW_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdint.h>
#include "hinic3_mutex.h"
#include "hinic3_ufid_hmap.h"
#include "hinic3_flow_agent_public.h"
#include "hinic3_offload_flow_public.h"
#include "hinic3_mega_offload.h"
#include "hinic3_hash.h"
#include "hinic3_command.h"
#include "hinic3_mpool_rte_flow.h"
#include "hiovs_api.h"
#include "hinic3_flexda_flow_public.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STAT_REFRESH_MAX_THREAD_NUM 10
#define SYNC_BUCKET_NUM 16

enum {
    HIOVS_UFID_DEL_NO_RESPON,
    HIOVS_UFID_INVALID,
    HIOVS_UFID_NO_EXIST,
    HIOVS_UFID_DEL_FAilED,
    HIOVS_FLOW_NO_EXIST,
};

struct hinic3_flow_del_rte_flow_context {
    size_t num_entries;
    uint8_t is_deleted; /* 标记batch中内容是否在硬件中已经删除，在dpak中还没有删除 */
    uint32_t table_id;
    uint16_t port_id;
    uint64_t ufids[HINIC3_MAX_ENTRY_PER_BATCH_DEL];
    struct hinic3_dpif_flow_for_get *flows[HINIC3_MAX_ENTRY_PER_BATCH_DEL];
    struct hinic3_flow_del_buf buf[HINIC3_MAX_ENTRY_PER_BATCH_DEL];
};

struct hash_table_node {
    struct hinic3_spinlock spinlock;
    struct hmap key_hmap;
};

struct hinic3_flexda_ufid_map_segment_data {
    uint32_t base;
    uint32_t length;
};

struct hinic3_ufid_map_table {
    uint32_t hash_base;
    struct hash_table_node hash_table_array[OFFLOAD_FLOW_BUCKETS];
    struct hinic3_flexda_ufid_map_segment_data table_segment_array[HINIC3_HYDRA_TYPE_TABLE_MAX];
    rte_atomic32_t count;
};

struct traverse_data {
    uint8_t *para;
    bool is_end;
};

struct stat_refresh_time {
    double time; /* 记录统计信息全量刷新时长 单位为s */
    uint32_t update; /* 更新标记，1表示更新，0表示此时间数据已被采样过，不是最新数据 */
};

static inline struct rte_flow *hinic3_rte_flow_alloc(void)
{
    struct rte_flow *flow = NULL;

    flow = hinic3_alloc_from_rte_flow_mpool();
    if (HINIC3_UNLIKELY(flow == NULL)) {
        return NULL;
    }
    memset(flow, 0, sizeof(struct rte_flow));
    flow->flags.is_mem_used = 1;
    return flow;
}

void hinic3_free_hydra_key_from_rte_flow(struct rte_flow *flow);
void hinic3_free_hydra_key_from_fuzzy_flow(struct fuzzy_flow *flow);

static inline void hinic3_rte_flow_dealloc(struct rte_flow *flow)
{
    if (flow == NULL) {
        return;
    }

    if (flow->flags.is_mem_used == 0) {
        HINIC3_LOG(ERR, FLOW, "flow is unused in mem dealloc");
        return;
    }
    flow->flags.is_mem_used = 0;
    if (hinic3_card_mod_get() == PROG_MODE) {
        hinic3_free_hydra_key_from_rte_flow(flow);
    }
    hinic3_free_from_rte_flow_mpool(flow);
    flow = NULL;
}

static inline struct mega_flow *hinic3_mega_flow_alloc(void)
{
    struct mega_flow *mega = NULL;

    mega = hinic3_alloc_from_fuzzy_flow_mpool();
    if (mega == NULL)
        return NULL;

    memset(mega, 0, sizeof(struct mega_flow));
    mega->flow.flags.is_mem_used = 1;
    return mega;
}

static inline void hinic3_mega_flow_dealloc(struct mega_flow *mega)
{
    if (mega == NULL)
        return;

    if (mega->flow.flags.is_mem_used == 0) {
        HINIC3_LOG(ERR, FLOW, "mega is unused in mem dealloc");
        return;
    }

    mega->flow.flags.is_mem_used = 0;
    hinic3_free_from_fuzzy_flow_mpool(mega);
    mega = NULL;
}

static inline struct fuzzy_flow* hinic3_fuzzy_flow_alloc(void)
{
    struct fuzzy_flow *flow = NULL;
         
    flow = hinic3_alloc_from_fuzzy_flow_mpool();
    if (HINIC3_UNLIKELY(flow == NULL)) {
        return NULL;
    }
    (void)memset(flow, 0, sizeof(struct fuzzy_flow));
    flow->flow.flags.is_mem_used = 1;
    return flow;
}
 
static inline
void hinic3_fuzzy_flow_dealloc(struct fuzzy_flow *flow)
{
    if (HINIC3_UNLIKELY(flow == NULL)) {
        return;
    }
         
    if (flow->flow.flags.is_mem_used == 0) {
        HINIC3_LOG(ERR, FLOW, "flow is unused in mem dealloc");
        return;
    }
    flow->flow.flags.is_mem_used = 0;
    hinic3_free_hydra_key_from_fuzzy_flow(flow);
    hinic3_free_from_fuzzy_flow_mpool(flow);
}
  
static inline
struct rte_flow* hinic3_rte_or_fuzzy_flow_alloc(uint8_t table_id)
{
    return IS_FLEXDA_FUZZY_TABLE(table_id) ? (struct rte_flow *)hinic3_fuzzy_flow_alloc() : hinic3_rte_flow_alloc();
}
   
static inline
void hinic3_rte_or_fuzzy_flow_dealloc(struct rte_flow *flow)
{
    if (IS_FLEXDA_FUZZY_TABLE(flow->table_id)) {
        (void)hinic3_fuzzy_flow_dealloc((struct fuzzy_flow*)flow);
    } else {
        (void)hinic3_rte_flow_dealloc(flow);
    }
}
void hinic3_rlock_flush_all(void);
void hinic3_runlock_flush_all(void);
void hinic3_wlock_flush_all(void);
void hinic3_wunlock_flush_all(void);
int hinic3_try_rlock_flush_all(void);
int hinic3_ufid_map_rte_flow_init(void);
void hinic3_ufid_map_flush(void);
void hinic3_dump_rte_flow_by_hw_ufid(uint32_t thread_id, uint32_t *cur_buk_index, uint32_t max_buk_index,
    uint8_t *para, int (*callback)(struct rte_flow *, struct traverse_data *));
bool hinic3_is_rte_hmap_empty(void);
void hinic3_dump_hmap_flow_num(struct unixctl_conn *conn, int argc, const char *argv[],
    void *aux HINIC3_UNUSED);
void hinic3_flexda_dump_hmap_flow_num(struct unixctl_conn *conn, int argc, const char *argv[],
    void *aux HINIC3_UNUSED);
int hinic3_del_flow_in_hmap_by_port(uint16_t port_id);
int hinic3_set_ufid_in_rte_flow(uint32_t table_id, struct hinic3_flow_callback_info *callback_info, uint64_t ufid);
int hinic3_del_rte_flow_if_offload_fail(struct hash_table_node *rte_bucket, struct rte_flow *mega_flow);
int hinic3_del_rte_flow_in_hmap(struct hash_table_node *rte_bucket, struct rte_flow *mega_flow);
int hinic3_deal_aged_flow_by_event(struct hinic3_conntrack_full_key* full_key);
struct rte_flow *hinic3_get_offloaded_rte_flow(struct hash_table_node *rte_bucket,
    const struct hinic3_conntrack_full_key *key, uint32_t flow_hash);
int hinic3_insert_rte_flow_in_hmap(struct hash_table_node *rte_bucket, struct rte_flow *mega_flow);
int hinic3_batch_del_flow(struct rte_flow **flow, int flow_num, int destroy_result[]);
struct stat_refresh_time *hinic3_get_stat_ref_time(void);
struct hash_table_node *hinic3_get_flow_bucket(uint32_t flow_hash);
struct hash_table_node *hinic3_flexda_get_table_flow_bucket(uint32_t table_id, uint32_t flow_hash);
uint32_t hinic3_hash_generate_entrance(const struct hinic3_conntrack_key *key);
int hinic3_flow_destroy_in_hmap_by_ufid(uint64_t hw_ufid);
const struct hinic3_ufid_map_table *hinic3_get_rte_flow_map_table(void);
struct hinic3_ufid_map_table *hinic3_get_ufid_map_table(void);
size_t hinic3_get_rte_flow_map_table_count(void);
uint32_t hinic3_flexda_dump_hmap_get_flow_num(uint32_t table_id);
#ifdef __cplusplus
}
#endif

#endif
