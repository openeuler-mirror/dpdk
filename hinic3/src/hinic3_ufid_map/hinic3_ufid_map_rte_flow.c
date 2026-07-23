/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2022 Huawei Technologies Co., Ltd
 */

#include "hinic3_command.h"
#include "hinic3_driver_public.h"
#include "hinic3_iface_flow.h"
#include "hinic3_ui_string.h"
#include "hinic3_log.h"
#include "hinic3_flow_agent.h"
#include "hinic3_flow_session.h"
#include "hinic3_util.h"
#include "hinic3_agent_cmd_time.h"
#include "hinic3_iface_flow_api_record.h"
#include "hinic3_meminfo.h"
#include "hinic3_ds.h"
#include "hinic3_command.h"
#include "hinic3_parse_agent_config.h"
#include "hinic3_ufid_del_flow.h"
#include "hinic3_ufid_map_rte_flow.h"
#include "hinic3_ufid_hmap.h"
#include "hinic3_ufid_del_flow.h"
#include "hinic3_age_delete_flow.h"
#include "hinic3_port_util.h"
#include "hinic3_bond_controller.h"
#include "hinic3_vf_controller.h"
#include "hinic3_string_util.h"

static struct hinic3_ufid_map_table hinic3_mpool_mgmt;
struct stat_refresh_time g_stat_ref_time[HINIC3_COMMAND_QUEUES_NUM_MAX] = {0};
static struct hinic3_spin_rwlock g_emc_flush_all_lock;
#define HINIC3_FLOW_NOT_FOUND (-2)

static inline void hinic3_flush_all_lock_init(void)
{
    rte_rwlock_init(&g_emc_flush_all_lock.lock);
}

void hinic3_rlock_flush_all(void)
{
    rte_rwlock_read_lock(&g_emc_flush_all_lock.lock);
}

void hinic3_runlock_flush_all(void)
{
    rte_rwlock_read_unlock(&g_emc_flush_all_lock.lock);
}

void hinic3_wlock_flush_all(void)
{
    rte_rwlock_write_lock(&g_emc_flush_all_lock.lock);
}

void hinic3_wunlock_flush_all(void)
{
    rte_rwlock_write_unlock(&g_emc_flush_all_lock.lock);
}

int hinic3_try_rlock_flush_all(void)
{
    return rte_rwlock_read_trylock(&g_emc_flush_all_lock.lock);
}

const struct hinic3_ufid_map_table *hinic3_get_rte_flow_map_table(void)
{
    return &hinic3_mpool_mgmt;
}

struct hinic3_ufid_map_table *hinic3_get_ufid_map_table(void)
{
    return &hinic3_mpool_mgmt;
}

bool hinic3_is_rte_hmap_empty(void)
{
    if (rte_atomic32_read(&hinic3_mpool_mgmt.count) != 0) {
        return false;
    }
    return true;
}

static uint32_t hinic3_rte_flow_hash_to_bucket(uint32_t hash)
{
#define OFFLOAD_BUCKETS_UINT_BIT 32
    return (hash >> (OFFLOAD_BUCKETS_UINT_BIT - OFFLOAD_FLOW_BUCKETS_SHIFT)) % OFFLOAD_FLOW_BUCKETS;
}

static uint32_t hinic3_hash_generate_flexda(const struct hinic3_conntrack_key *key)
{
    struct hydra_flow_item *hydra_key_node = key->hydra_key_head;
    uint32_t hash = hinic3_mpool_mgmt.hash_base;
    hash = hinic3_hash_add(hash, key->meta_num);
    hash = hinic3_hash_add(hash, key->table_id);
    hash = hinic3_hash_add(hash, key->hdr_flags_num);

    /* for DPDK key */
    for (size_t idx = 0; idx < key->meta.key_len / sizeof(uint32_t); idx++)
    {
        hash = hinic3_hash_add(hash, ((const uint32_t *)key->key)[idx]);
    }

    /* for DSL define key */
    uint32_t hash_idx = 0;
    for (int32_t idx = 0; idx < key->hydra_key_count; idx++)
    {
        hash = hinic3_hash_add(hash, (uint32_t)hydra_key_node->item_type);
        hash = hinic3_hash_add(hash, (uint32_t)hydra_key_node->item_data_size);
        if (hydra_key_node->item_data != NULL)
        {
            for (hash_idx = 0; hash_idx < hydra_key_node->item_data_size; hash_idx++)
            {
                // progressive hashing in bytes
                hash = hinic3_hash_add(hash, ((uint8_t *)hydra_key_node->item_data)[hash_idx]);
            }
        }
        hydra_key_node = hydra_key_node->next;
    }
    return hash;
}

static uint32_t hinic3_hash_generate(const struct hinic3_conntrack_key *key)
{
    uint32_t hash = hinic3_mpool_mgmt.hash_base;
    hash = hinic3_hash_add(hash, key->meta_num);

    for (size_t idx = 0; idx < key->meta.key_len / sizeof(uint32_t); idx++)
    {
        hash = hinic3_hash_add(hash, ((const uint32_t *)key->key)[idx]);
    }
    return hash;
}

uint32_t hinic3_hash_generate_entrance(const struct hinic3_conntrack_key *key)
{
    if (hinic3_card_mod_get() == PROG_MODE)
        return hinic3_hash_generate_flexda(key);
    else
        return hinic3_hash_generate(key);
}
static struct hash_table_node *hinic3_rte_flow_bucket_get(struct hinic3_ufid_map_table *tbl, uint32_t flow_hash)
{
    uint32_t bucket = hinic3_rte_flow_hash_to_bucket(flow_hash);
    return &tbl->hash_table_array[bucket];
}

struct hash_table_node *hinic3_get_flow_bucket(uint32_t flow_hash)
{
    return hinic3_rte_flow_bucket_get(&hinic3_mpool_mgmt, flow_hash);
}

static uint32_t hinic3_flexda_rte_table_flow_hash_to_bucket(uint32_t table_id, uint32_t hash)
{
#define OFFLOAD_BUCKEYS_UINT_BIT 32
    int table_index = hinic3_flexda_flow_get_table_index(table_id);
    return ((hash >> (OFFLOAD_BUCKETS_UINT_BIT - OFFLOAD_FLOW_BUCKETS_SHIFT)) %
            hinic3_mpool_mgmt.table_segment_array[table_index].length) +
           hinic3_mpool_mgmt.table_segment_array[table_index].base;
}

static struct hash_table_node *hinic3_flexda_rte_table_flow_bucket_get(struct hinic3_ufid_map_table *tbl,
                                                                       uint32_t table_id, uint32_t flow_hash)
{
    uint32_t bucket = hinic3_flexda_rte_table_flow_hash_to_bucket(table_id, flow_hash);
    return &tbl->hash_table_array[bucket];
}

struct hash_table_node *hinic3_flexda_get_table_flow_bucket(uint32_t table_id, uint32_t flow_hash)
{
    return hinic3_flexda_rte_table_flow_bucket_get(&hinic3_mpool_mgmt, table_id, flow_hash);
}

/* 判断是否重复卸载，如果重复卸载直接返回NULL */
struct rte_flow *hinic3_get_offloaded_rte_flow(struct hash_table_node *rte_bucket,
                                               const struct hinic3_conntrack_full_key *key, uint32_t flow_hash)
{
    return hinic3_ufid_hmap_get(&rte_bucket->key_hmap, key, flow_hash);
}

int hinic3_del_rte_flow_if_offload_fail(struct hash_table_node *rte_bucket, struct rte_flow *mega_flow)
{
    int ret;
    int ret_session;
    struct hinic3_conntrack_full_key *key = &mega_flow->key;

    if (mega_flow->flags.is_sample == 1)
    {
        ret_session = hinic3_del_rte_flow_in_session(mega_flow);
        if (ret_session != 0)
        {
            HINIC3_LOG(ERR, FLOW, "Hinic3 delete session flow failed!");
        }
    }

    
    ret = hinic3_ufid_hmap_del_by_key(&rte_bucket->key_hmap, key, mega_flow->flow_hash, EMC_UFID_MAP);
    if (ret != 0) {
        HINIC3_LOG(ERR, FLOW, "Failed to delete ufid hmap by key if offload failed, hash is %u!",
                   mega_flow->flow_hash);
        return -1;
    }
    rte_atomic32_dec(&hinic3_mpool_mgmt.count);
    return 0;
}

int hinic3_del_rte_flow_in_hmap(struct hash_table_node *rte_bucket, struct rte_flow *mega_flow)
{
    int ret;
    struct hinic3_conntrack_full_key *key = &mega_flow->key;

    ret = hinic3_ufid_hmap_del_by_key(&rte_bucket->key_hmap, key, mega_flow->flow_hash, EMC_UFID_MAP);
    if (ret == 0)
    {
        rte_atomic32_dec(&hinic3_mpool_mgmt.count);
        return 0;
    }
    return -1;
}

int hinic3_insert_rte_flow_in_hmap(struct hash_table_node *rte_bucket, struct rte_flow *mega_flow)
{
    struct hinic3_ufid_hmap_node *node = NULL;
    struct hmap *hash_table = &rte_bucket->key_hmap;

    node = hinic3_ufid_hmap_add(hash_table, mega_flow, mega_flow->flow_hash, HINIC3_UFID_MAP, EMC_UFID_MAP);
    if (node == NULL)
    {
        HINIC3_LOG(ERR, FLOW, "Failed to add ufid hmap node, hash is %u!", mega_flow->flow_hash);
        return -1;
    }
    rte_atomic32_inc(&hinic3_mpool_mgmt.count);
    return 0;
}

static struct hinic3_flow_del_rte_flow_context *hinic3_del_rte_flow_batch_context_alloc(uint16_t port_id)
{
    struct hinic3_flow_del_rte_flow_context *batch = NULL;
    struct hinic3_dpif_flow_for_get *flow = NULL;

    batch = (struct hinic3_flow_del_rte_flow_context *)hinic3_calloc(1, sizeof(struct hinic3_flow_del_rte_flow_context),
                                                                     HINIC3_UFID_MAP);
    if (batch == NULL)
    {
        HINIC3_LOG(ERR, FLOW, "Hinic3 flow del batch calloc failed!");
        return NULL;
    }

    batch->num_entries = 0;
    batch->port_id = port_id;
    for (int idx = 0; idx < HINIC3_MAX_ENTRY_PER_BATCH_DEL; idx++)
    {
        flow = &batch->buf[idx].get_flow;
        flow->key = (struct hinic3_nlattr_obj *)batch->buf[idx].key_buf;
        flow->key_len = sizeof(batch->buf[idx].actions_buf);
        flow->mask = NULL;
        flow->mask_len = 0;
        flow->mask_present = false;
        flow->actions = (struct hinic3_nlattr_obj *)batch->buf[idx].actions_buf;
        flow->action_len = sizeof(batch->buf[idx].actions_buf);
        batch->flows[idx] = flow;
    }

    return batch;
}

static void hinic3_del_hw_flows_batch_rte_flow(struct hinic3_flow_del_rte_flow_context *batch, uint64_t hw_ufid)
{
    if (batch->num_entries < HINIC3_MAX_ENTRY_PER_BATCH_DEL)
    {
        batch->ufids[batch->num_entries] = hw_ufid;
        batch->buf[batch->num_entries].get_flow.ol_ufid = hw_ufid;
        batch->num_entries++;
    }

    if (batch->num_entries == HINIC3_MAX_ENTRY_PER_BATCH_DEL)
    {
        hinic3_flow_del_batch(batch->table_id, batch->ufids, batch->flows, batch->num_entries);
        batch->num_entries = 0;
        batch->is_deleted = 1;
    }
    return;
}

int hinic3_set_ufid_in_rte_flow(uint32_t table_id, struct hinic3_flow_callback_info *callback_info, uint64_t ufid)
{
    struct rte_flow *flow = NULL;
    struct hash_table_node *rte_bucket = NULL;
    struct hmap *hash_table = NULL;
    struct hinic3_conntrack_full_key *key = &callback_info->no_ct_key;
    uint32_t flow_hash = callback_info->flow_hash;

    if (table_id == 0) {
        rte_bucket = hinic3_get_flow_bucket(flow_hash);
    } else {
        rte_bucket = hinic3_flexda_get_table_flow_bucket(table_id, flow_hash);
    }
    
    hash_table = &rte_bucket->key_hmap;
    hinic3_spinlock_lock(&rte_bucket->spinlock);

    flow = hinic3_ufid_hmap_get(hash_table, key, flow_hash);
    
    if (flow == NULL) {
        hinic3_spinlock_unlock(&rte_bucket->spinlock);
        return -1;
    }
    flow->hw_ufid = ufid;
    flow->flags.is_offload = 1;

    hinic3_spinlock_unlock(&rte_bucket->spinlock);
    return 0;
}

void hinic3_ufid_map_flush(void)
{
    for (uint32_t i = 0; i < OFFLOAD_FLOW_BUCKETS; i++)
    {
        struct hash_table_node *rte_bucket = &hinic3_mpool_mgmt.hash_table_array[i];
        hinic3_spinlock_lock(&rte_bucket->spinlock);
        hinic3_hmap_clear(&rte_bucket->key_hmap);
        hinic3_spinlock_unlock(&rte_bucket->spinlock);
    }
    rte_atomic32_set(&hinic3_mpool_mgmt.count, 0);
}

void hinic3_dump_rte_flow_by_hw_ufid(uint32_t thread_id, uint32_t *cur_buk_index, uint32_t max_buk_index, uint8_t *para,
                                     int (*callback)(struct rte_flow *, struct traverse_data *))
{
    uint32_t i;
    clock_t start_t = 0;
    clock_t finish_t = 0;
    struct traverse_data cur_node_data;
    struct hinic3_ufid_hmap_node *ufid_hmap_node = NULL;
    cur_node_data.para = para;

    if (HINIC3_UNLIKELY(hinic3_is_stat_scan_measure_alive() == true))
    {
        start_t = clock();
    }

    for (i = *cur_buk_index; i < (*cur_buk_index + SYNC_BUCKET_NUM) && i < max_buk_index; i++)
    {
        (void)hinic3_rlock_flush_all();
        struct hash_table_node *rte_bucket = &hinic3_mpool_mgmt.hash_table_array[i];
        hinic3_spinlock_lock(&rte_bucket->spinlock);
        if (hinic3_hmap_is_empty(&rte_bucket->key_hmap))
        {
            hinic3_spinlock_unlock(&rte_bucket->spinlock);
            (void)hinic3_runlock_flush_all();
            continue;
        }
        cur_node_data.is_end = false;
        HINIC3_HMAP_FOR_EACH(ufid_hmap_node, node, &rte_bucket->key_hmap)
        {
            callback(ufid_hmap_node->key, &cur_node_data);
        }
        cur_node_data.is_end = true;
        callback(NULL, &cur_node_data);
        hinic3_spinlock_unlock(&rte_bucket->spinlock);
        (void)hinic3_runlock_flush_all();
    }

    if (i == max_buk_index)
    {
        *cur_buk_index = max_buk_index;
    }
    else
    {
        *cur_buk_index += SYNC_BUCKET_NUM;
    }

    if (HINIC3_UNLIKELY(hinic3_is_stat_scan_measure_alive() == true))
    {
        finish_t = clock();
        g_stat_ref_time[thread_id].time = ((double)(finish_t - start_t)) / CLOCKS_PER_SEC;
        g_stat_ref_time[thread_id].update = 1;
    }
}

static void hinic3_release_ufid_map(int index)
{
    for (int i = 0; i < index; ++i)
    {
        struct hash_table_node *rte_bucket = &hinic3_mpool_mgmt.hash_table_array[i];
        hinic3_spinlock_destroy(&rte_bucket->spinlock);
    }
}

void hinic3_dump_hmap_flow_num(struct unixctl_conn *conn, int argc HINIC3_UNUSED,
                               const char *argv[] HINIC3_UNUSED, void *aux)
{
    uint32_t flow_num = 0;
    struct ds ds = DS_EMPTY_INITIALIZER;

    for (int i = 0; i < OFFLOAD_FLOW_BUCKETS; ++i)
    {
        struct hash_table_node *rte_bucket = &hinic3_mpool_mgmt.hash_table_array[i];
        hinic3_spinlock_lock(&rte_bucket->spinlock);
        flow_num += hinic3_hmap_count(&rte_bucket->key_hmap);
        hinic3_spinlock_unlock(&rte_bucket->spinlock);
    }

    hinic3_ds_put_format(&ds, "Info: The hmap flow num is %u.\n", flow_num);
    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
    *(int *)aux = 0;
}

size_t hinic3_get_rte_flow_map_table_count(void)
{
    size_t flow_num = 0;
    for (int i = 0; i < OFFLOAD_FLOW_BUCKETS; ++i)
    {
        struct hash_table_node *rte_bucket = &hinic3_mpool_mgmt.hash_table_array[i];
        hinic3_spinlock_lock(&rte_bucket->spinlock);
        flow_num += hinic3_hmap_count(&rte_bucket->key_hmap);
        hinic3_spinlock_unlock(&rte_bucket->spinlock);
    }
    return flow_num;
}

static int hinic3_flexda_ufid_map_segment(void)
{
    int table_num = hinic3_flexda_flow_get_table_num();
    uint32_t table_flow_num = 0;
    uint64_t flow_num = 0;
    uint64_t flow_num_all = hinic3_flexda_flow_get_total_flow_num();
    uint32_t remain_buckets = OFFLOAD_FLOW_BUCKETS;
    uint32_t cal_total_buckets = OFFLOAD_FLOW_BUCKETS - table_num;

    /* 按每个表的流数量占总的流数量的比例划分hash buckets */
    for (int i = 0; i < table_num; i++) {
        /* 判断是否还剩余hash buckets */
        if (remain_buckets <= 0) {
            HINIC3_LOG(ERR, AGENT, "hinic3_flexda_ufid_map_segment failed, no enough buckets for table segment.\n");
            return -1;
        }

        /* 设置table id为i+1的表的hash buckets的起始引索 */
        if (i == 0) {
            hinic3_mpool_mgmt.table_segment_array[i].base = 0;
        } else {
            hinic3_mpool_mgmt.table_segment_array[i].base = hinic3_mpool_mgmt.table_segment_array[i - 1].base + \
            hinic3_mpool_mgmt.table_segment_array[i - 1].length;
        }

        /* 设置table id为i+1的表的hash buckets的数量 */
        if (hinic3_flexda_flow_get_table_flow_num(i + 1, &table_flow_num) != 0)
        {
            HINIC3_LOG(ERR, AGENT, "hinic3_flexda_flow_get_table_flow_num failed!");
            return -1;
        }
        flow_num = (uint64_t)table_flow_num;
        hinic3_mpool_mgmt.table_segment_array[i].length = (uint32_t)((flow_num * cal_total_buckets) / flow_num_all);
        if (hinic3_mpool_mgmt.table_segment_array[i].length == 0) {
            hinic3_mpool_mgmt.table_segment_array[i].length = 1;
        }

        /* 设置剩余的hash bucket数量 */
        if (remain_buckets < hinic3_mpool_mgmt.table_segment_array[i].length) {
            hinic3_mpool_mgmt.table_segment_array[i].length = remain_buckets;
            remain_buckets = 0;
        } else {
            remain_buckets -= hinic3_mpool_mgmt.table_segment_array[i].length;
        }
    }

    /* 将剩余的hash buckets分配给最后一个表 */
    if (remain_buckets > 0) {
        hinic3_mpool_mgmt.table_segment_array[table_num - 1].length += remain_buckets;
        remain_buckets = 0;
    }

    return 0;
}

int hinic3_ufid_map_rte_flow_init(void)
{
    int ret;
    hinic3_mpool_mgmt.hash_base = 0;
    rte_atomic32_init(&hinic3_mpool_mgmt.count);
    rte_atomic32_set(&hinic3_mpool_mgmt.count, 0);
    if (hinic3_card_mod_get() == PROG_MODE) {
        ret = hinic3_flexda_ufid_map_segment();
        if (ret != 0) {
            HINIC3_LOG(ERR, FLOW, "hinic3 ufid_map_segment fail!\n");
            return -1;
        }
    }

    hinic3_flush_all_lock_init();
    for (int i = 0; i < OFFLOAD_FLOW_BUCKETS; ++i)
    {
        struct hash_table_node *rte_bucket = &hinic3_mpool_mgmt.hash_table_array[i];
        struct hmap *hash_table = &rte_bucket->key_hmap;

        hinic3_ufid_hmap_init(hash_table);
        ret = hinic3_spinlock_init(&rte_bucket->spinlock, PTHREAD_PROCESS_PRIVATE);
        if (ret != 0)
        {
            (void)hinic3_release_ufid_map(i);
            return -1;
        }
    }
    HINIC3_LOG(INFO, FLOW, "Hinic3 hash_map init success.");

    return 0;
}

struct stat_refresh_time *hinic3_get_stat_ref_time(void)
{
    return g_stat_ref_time;
}

static void hinic3_batch_del_rte_flow_set_error_code(const struct hinic3_dpif_flow_for_get *flow, int *destroy_result)
{
    switch (flow->ol_ufid)
    {
    case HIOVS_UFID_DEL_NO_RESPON:
        *destroy_result = -EDOM;
        break;
    case HIOVS_UFID_INVALID:
        *destroy_result = -ENOENT;
        break;
    case HIOVS_UFID_NO_EXIST:
        *destroy_result = -ENOENT;
        break;
    case HIOVS_UFID_DEL_FAilED:
        *destroy_result = -EFAULT;
        break;
    case HIOVS_FLOW_NO_EXIST:
        *destroy_result = -ENOENT;
        break;
    default:
        *destroy_result = -EDOM;
    }
}

static void hinic3_batch_del_rte_flow_in_hmap(struct hinic3_flow_del_rte_flow_context *batch,
                                              int index, int destroy_result[], int *delete_nums, struct rte_flow **flow)
{
    struct hash_table_node *rte_bucket;
    uint16_t err_num = 0;
    int batch_flow_index = batch->is_deleted == 1 ? (HINIC3_MAX_ENTRY_PER_BATCH_DEL - 1) : (batch->num_entries - 1);
    for (int destroy_result_index = index; destroy_result_index >= 0 && batch_flow_index >= 0; destroy_result_index--)
    {
        if (destroy_result[destroy_result_index] == -1)
        {
            continue;
        }
        if (batch->ufids[batch_flow_index] == batch->flows[batch_flow_index]->ol_ufid)
        {
            rte_bucket = hinic3_get_flow_bucket(flow[batch_flow_index]->flow_hash);
            hinic3_spinlock_lock(&rte_bucket->spinlock);
            (void)hinic3_del_rte_flow_in_hmap(rte_bucket, flow[batch_flow_index]);
            hinic3_rte_or_fuzzy_flow_dealloc(flow[batch_flow_index]);
            hinic3_spinlock_unlock(&rte_bucket->spinlock);
            (*delete_nums)++;
            batch_flow_index--;
        }
        else
        {
            err_num++;
            (void)hinic3_batch_del_rte_flow_set_error_code(batch->flows[batch_flow_index],
                                                           &destroy_result[destroy_result_index]);
        }
    }
    if (err_num != 0)
    {
        HINIC3_LOG(ERR, FLOW, "Batch delete flow not cleaned up, err num is %hu!", err_num);
    }
}

int hinic3_batch_del_flow(struct rte_flow **flow, int flow_num, int destroy_result[])
{
    int delete_nums = 0;
    uint16_t port_id = 0;
    struct hinic3_flow_del_rte_flow_context *batch = hinic3_del_rte_flow_batch_context_alloc(port_id); // 为复用接口，port_id=0为无效参数
    if (batch == NULL)
    {
        HINIC3_LOG(ERR, FLOW, "Alloc del rte flow bacth context failed, port id is %hu!", port_id);
        return -ENOMEM;
    }
    memset(destroy_result, 0, sizeof(int) * flow_num);
    for (int i = 0; i < flow_num; i++)
    {
        if ((flow[i] != NULL) && (flow[i]->flags.is_offload == 0))
        {
            destroy_result[i] = -1;
            continue;
        }

        if (flow[i] == NULL)
        {
            continue;
        }
        hinic3_del_hw_flows_batch_rte_flow(batch, flow[i]->hw_ufid);
        if (batch->is_deleted == 1)
        {
            hinic3_batch_del_rte_flow_in_hmap(batch, i % HINIC3_MAX_ENTRY_PER_BATCH_DEL,
                                              destroy_result, &delete_nums, flow);
            batch->is_deleted = 0;
        }
    }
    if (batch->num_entries != 0)
    {
        hinic3_flow_del_batch(batch->table_id, batch->ufids, batch->flows, batch->num_entries);
        hinic3_batch_del_rte_flow_in_hmap(batch, flow_num - 1, destroy_result, &delete_nums, flow);
    }
    hinic3_free(batch);
    return delete_nums;
}

static int hinic3_release_hmap_node_by_port(struct rte_flow *flow, struct hinic3_flow_del_rte_flow_context *batch,
                                            struct hash_table_node *bucket)
{
    int ret;

    if (flow->port_id != batch->port_id)
    {
        HINIC3_LOG(ERR, FLOW, "Port id error, flow port id is %hu, batch port id is %hu!", flow->port_id, batch->port_id);
        return 0;
    }

    if (flow->flags.is_sample == 1)
    {
        ret = hinic3_del_rte_flow_in_session(flow);
        if (ret != 0)
        {
            HINIC3_LOG(ERR, FLOW, "Failed to delete rte flow in session!");
            return -1;
        }
    }

    if (flow->flags.has_flow_qos == 1)
    {
        ret = hinic3_del_flow_qos_by_meter_id(flow->meter_id);
        if (ret != 0)
        {
            HINIC3_LOG(ERR, FLOW, "Failed to delete flow qos by meter id, meter id is %u!", flow->meter_id);
            return -1;
        }
    }

    ret = hinic3_del_rte_flow_in_hmap(bucket, flow);
    if (ret != 0)
    {
        HINIC3_LOG(ERR, FLOW, "Flow del failed in hmap!");
        return -1;
    }
    hinic3_del_hw_flows_batch_rte_flow(batch, flow->hw_ufid);
    hinic3_rte_or_fuzzy_flow_dealloc(flow);

    return 0;
}

int hinic3_del_flow_in_hmap_by_port(uint16_t port_id)
{
    int ret;
    struct hinic3_ufid_hmap_node *ufid_hmap_node = NULL;
    struct hinic3_flow_del_rte_flow_context *batch = hinic3_del_rte_flow_batch_context_alloc(port_id);
    if (batch == NULL)
    {
        HINIC3_LOG(ERR, FLOW, "Failed to alloc del batch context!");
        return -ENOMEM;
    }

    for (int idx = 0; idx < OFFLOAD_FLOW_BUCKETS; idx++)
    {
        struct hash_table_node *bucket = &hinic3_mpool_mgmt.hash_table_array[idx];
        hinic3_spinlock_lock(&bucket->spinlock);
        HINIC3_HMAP_FOR_EACH(ufid_hmap_node, node, &bucket->key_hmap)
        {
            ret = hinic3_release_hmap_node_by_port(ufid_hmap_node->key, batch, bucket);
            if (ret != 0)
            {
                hinic3_free(batch);
                hinic3_spinlock_unlock(&bucket->spinlock);
                HINIC3_LOG(ERR, FLOW, "Hinic3load delete by port_id failed!");
                return -1;
            }
        }
        hinic3_spinlock_unlock(&bucket->spinlock);
    }

    if (batch->num_entries != 0)
    {
        hinic3_flow_del_batch(batch->table_id, batch->ufids, batch->flows, batch->num_entries);
    }
    hinic3_free(batch);
    return 0;
}

int hinic3_flow_destroy_in_hmap_by_ufid(uint64_t hw_ufid)
{
    int ret = -1;

    for (uint32_t i = 0; i < OFFLOAD_FLOW_BUCKETS; i++)
    {
        if (&hinic3_mpool_mgmt.hash_table_array[i] != NULL)
        {
            ret = hinic3_ufid_hmap_del_by_ufid(&hinic3_mpool_mgmt.hash_table_array[i].key_hmap, hw_ufid, EMC_UFID_MAP);
        }
        if (ret == 0)
        {
            return ret;
        }
    }
    return ret;
}

int hinic3_deal_aged_flow_by_event(struct hinic3_conntrack_full_key *full_key)
{
    struct hinic3_bond_dev *bond_dev = NULL;
    struct hinic3_vf_dev *vf_dev = NULL;
    struct rte_eth_dev *dev = NULL;
    uint32_t flow_hash;
    struct hash_table_node *rte_bucket = NULL;
    struct rte_flow *aged_flow = NULL;
    uint16_t port_id;

    struct rte_aged_flow_list_node *ptr = NULL;
    ptr = (struct rte_aged_flow_list_node *)hinic3_malloc(sizeof(struct rte_aged_flow_list_node), 1);
    if (ptr == NULL)
    {
        HINIC3_LOG(ERR, FLOW, "Failed to record rte_aged_flow_list_node!");
        return -ENOMEM;
    }

    (void)hinic3_wlock_flush_all();
    flow_hash = hinic3_hash_generate_entrance(&full_key->key);
    rte_bucket = hinic3_get_flow_bucket(flow_hash);

    hinic3_spinlock_lock(&rte_bucket->spinlock);
    aged_flow = hinic3_get_offloaded_rte_flow(rte_bucket, full_key, flow_hash);
    if (aged_flow == NULL || aged_flow->flags.is_mem_used == 0)
    {
        hinic3_spinlock_unlock(&rte_bucket->spinlock);
        (void)hinic3_wunlock_flush_all();
        HINIC3_LOG(ERR, FLOW, "Failed to find aged flow by full key!");
        hinic3_free(ptr);
        return -1;
    }

    port_id = aged_flow->port_id;
    aged_flow->flags.is_aged = 1;
    bond_dev = (struct hinic3_bond_dev *)hinic3_get_private_data(port_id);
    if(bond_dev == NULL) {
        hinic3_spinlock_unlock(&rte_bucket->spinlock);
        (void)hinic3_wunlock_flush_all();
        HINIC3_LOG(ERR, FLOW, "Invalid port id %u!", port_id);
        hinic3_free(ptr);
        return -1;
    }
    hinic3_list_init(&ptr->node);
    ptr->flow = aged_flow;
    if (hinic3_is_bond_by_prefix(bond_dev->vport_id) == false)
    {
        vf_dev = (struct hinic3_vf_dev *)hinic3_get_private_data(port_id);
        hinic3_pthread_mutex_lock(&vf_dev->aged_flow_list.mutex);
        hinic3_list_insert(&vf_dev->aged_flow_list.list_head, &ptr->node);
        hinic3_pthread_mutex_unlock(&vf_dev->aged_flow_list.mutex);
    }
    else
    {
        hinic3_pthread_mutex_lock(&bond_dev->aged_flow_list.mutex);
        hinic3_list_insert(&bond_dev->aged_flow_list.list_head, &ptr->node);
        hinic3_pthread_mutex_unlock(&bond_dev->aged_flow_list.mutex);
    }

    dev = &rte_eth_devices[port_id];

    hinic3_spinlock_unlock(&rte_bucket->spinlock);
    (void)hinic3_wunlock_flush_all();
    rte_eth_dev_callback_process(dev, RTE_ETH_EVENT_FLOW_AGED, NULL);

    return 0;
}

static inline void hinic3_free_hydra_key(struct hinic3_conntrack_key *key)
{
    struct hydra_flow_item *hydra_key_node_cur = key->hydra_key_head;
    struct hydra_flow_item *hydra_key_node_next = NULL;
    if (key->hydra_key_count == 0)
    {
        return;
    }

    for (int32_t idx = 0; idx < key->hydra_key_count; idx++)
    {
        hydra_key_node_next = hydra_key_node_cur->next;
        hydra_key_node_cur->next = NULL;
        if (hydra_key_node_cur->item_data != NULL)
        {
            free(hydra_key_node_cur->item_data);
            hydra_key_node_cur->item_data = NULL;
        }
        free(hydra_key_node_cur);
        hydra_key_node_cur = hydra_key_node_next;
    }

    key->hydra_key_count = 0;
    key->hydra_key_head = NULL;
    key->hydra_key_tail = NULL;
}

static void hinic3_flexda_show_hmap_flow_num_reply(struct unixctl_conn *conn, struct ds *ds, void *aux, bool is_success)
{
    hinic3_command_reply(conn, hinic3_ds_cstr(ds));
    hinic3_ds_destroy(ds);
    if (is_success)
    {
        *(int *)aux = 0;
    }
    else
    {
        *(int *)aux = -1;
    }
}

static void hinic3_flexda_show_hmap_flow_num_help_str(struct ds *output_msg)
{
    hinic3_ds_put_format(output_msg, "%2s%s%s\n\n", HINIC3_UI_INDENT_SPACE,
                        HINIC3_UI_CMD_USAGE_STRING, HINIC3_UI_SHOW_HMAP_FLOW_NUM_STRING);
    hinic3_ds_put_format(output_msg, "%2s%s\n", HINIC3_UI_INDENT_SPACE, HINIC3_UI_FLOW_OPTION_LIST_STRING);
    hinic3_ds_put_format(output_msg, "%4s%-30s%-s\n", HINIC3_UI_INDENT_SPACE,
                        HINIC3_UI_SHOW_HMAP_FLOW_NUM_ID_FORMAT_STRING, HINIC3_UI_SHOW_HMAP_FLOW_NUM_ID_TIPS_STRING);
    hinic3_ds_put_format(output_msg, "%4s%-30s%-s\n", HINIC3_UI_INDENT_SPACE,
                        HINIC3_UI_SHOW_HMAP_FLOW_NUM_HELP_FORMAT_STRING, HINIC3_UI_SHOW_HMAP_FLOW_NUM_HELP_TIPS_STRING);
}

uint32_t hinic3_flexda_dump_hmap_get_flow_num(uint32_t table_id)
{
    uint32_t flow_num = 0;
    struct hinic3_ufid_hmap_node *ufid_hmap_node = NULL;
    for (int i = 0; i < OFFLOAD_FLOW_BUCKETS; ++i)
    {
        struct hash_table_node *rte_bucket = &hinic3_mpool_mgmt.hash_table_array[i];
        hinic3_spinlock_lock(&rte_bucket->spinlock);
        HINIC3_HMAP_FOR_EACH(ufid_hmap_node, node, &rte_bucket->key_hmap)
        {
            if (ufid_hmap_node->key->key.key.table_id == table_id)
            {
                flow_num++;
            }
        }
        hinic3_spinlock_unlock(&rte_bucket->spinlock);
    }
    return flow_num;
}

static void hinic3_flexda_dump_hmap_flow_num_sub(struct unixctl_conn *conn, int argc , const char *argv[], void *aux)
{
    uint32_t flow_num = 0;
    struct ds ds = DS_EMPTY_INITIALIZER;
    enum
    {
        ARGC = 2,
        ARGC_TABLE_Id = 3
    };
    if (strncmp("-h", argv[ARGC - 1], sizeof("-h")) == 0 ||
        strncmp("--help", argv[ARGC - 1], sizeof("--help") == 0))
    {
        hinic3_flexda_show_hmap_flow_num_help_str(&ds);
        hinic3_flexda_show_hmap_flow_num_reply(conn, &ds, aux, true);
        return;
    } else if (strncmp("-t", argv[ARGC - 1], sizeof("-t")) == 0 && argc == ARGC_TABLE_Id) {
        uint32_t table_id;
        const int ret = hinic3_parse_uint32_from_string(argv[2], &table_id);
        if (ret != 0 || table_id > 12 || table_id < 1)
        {
            hinic3_ds_put_format(&ds, "Error: %s, invalid table id!\n", HINIC3_UI_ERROR_WRONG_PARAMETER);
            hinic3_flexda_show_hmap_flow_num_reply(conn, &ds, aux, false);
            return;
        }
        flow_num = hinic3_flexda_dump_hmap_get_flow_num(table_id);
        hinic3_ds_put_format(&ds, "Info: The hmap flow num of table id '%u' is '%u'.\n", table_id, flow_num);
        hinic3_flexda_show_hmap_flow_num_reply(conn, &ds, aux, true);
        return;
    }
    hinic3_ds_put_format(&ds, "Error: %s, input -h to get help info!\n", HINIC3_UI_ERROR_WRONG_PARAMETER);
    hinic3_flexda_show_hmap_flow_num_reply(conn, &ds, aux, false);
}

void hinic3_flexda_dump_hmap_flow_num(struct unixctl_conn *conn, int argc, const char *argv[], void *aux)
{
    enum
    {
        ARG_ALL = 1,
        ARGC = 2
    };

    if (argc == ARG_ALL)
    {
        hinic3_dump_hmap_flow_num(conn, argc, argv, aux);
    } else 
    {
        hinic3_flexda_dump_hmap_flow_num_sub(conn, argc, argv, aux);
    }
}

void hinic3_free_hydra_key_from_rte_flow(struct rte_flow *flow)
{
    struct hinic3_conntrack_key *key = &flow->key.key;
    hinic3_free_hydra_key(key);
    return;
}
 
void hinic3_free_hydra_key_from_fuzzy_flow(struct fuzzy_flow *flow)
{
    struct hinic3_conntrack_key *key = &flow->flow.key.key;
    struct hinic3_conntrack_key *mask = &flow->mask.key;
    struct hinic3_conntrack_key *masked_key = &flow->raw_key.key;
    hinic3_free_hydra_key(key);
    hinic3_free_hydra_key(mask);
    hinic3_free_hydra_key(masked_key);
    return;
}