/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */

#include <stdint.h>
#include "hinic3_log.h"
#include "hinic3_util.h"
#include "hinic3_meminfo.h"
#include "hinic3_ds.h"
#include "hinic3_ui_string.h"
#include "hinic3_mpool.h"

static uint32_t dpak_mp_align_up(uint32_t size)
{
    /* Aligin to sizeof uint64_t */
    return (size + sizeof(uint64_t) - 1) & (~(sizeof(uint64_t) - 1));
}

static inline int dpak_mp_lock(struct dpak_mempool *mp)
{
    return pthread_mutex_lock(&mp->lock);
}

static inline int dpak_mp_unlock(struct dpak_mempool *mp)
{
    return pthread_mutex_unlock(&mp->lock);
}

/* 头删除 */
static void dpak_mp_list_delete(struct dpak_mp_chunk **head, struct dpak_mp_chunk *ck)
{
    if (HINIC3_UNLIKELY((*head == NULL) || (ck == NULL))) {
        return;
    }
    *head = ck->next;
}

/* 头插入 */
static inline void dpak_mp_list_insert(struct dpak_mp_chunk **head, struct dpak_mp_chunk *ck)
{
    if (HINIC3_UNLIKELY(ck == NULL)) {
        return;
    }
    ck->next = *head;
    *head = ck;
}

static int dpak_mp_insert_blocks(struct dpak_mempool *mp, uint32_t need_size, uint32_t ele_count,
    enum hinic3_module module_id)
{
    struct dpak_mp_block *blk_head = (struct dpak_mp_block *)hinic3_calloc(1, sizeof(struct dpak_mp_block), module_id);
    if (blk_head == NULL) {
        HINIC3_LOG(ERR, AGENT, "Hinic3 calloc blk_head failed!");
        return -1;
    }

    if ((ele_count == 0) || (need_size == 0)) {
        HINIC3_LOG(ERR, AGENT, "Block alloc size is invalid!");
        hinic3_free(blk_head);
        return -1;
    }

    blk_head->virt_start = (char *)hinic3_calloc(ele_count, need_size, module_id);
    if (blk_head->virt_start == NULL) {
        HINIC3_LOG(ERR, AGENT, "Hinic3 calloc virt_start failed!");
        hinic3_free(blk_head);
        return -1;
    }

    blk_head->next = mp->blk_list;
    mp->blk_list = blk_head;
    mp->ele_total_count += ele_count;
    mp->block_num += 1;
    blk_head->free_count = ele_count;
    blk_head->alloc_count = 0;

    for (uint32_t i = 0; i < ele_count; i++) {
        struct dpak_mp_chunk *tmp = (struct dpak_mp_chunk *)(blk_head->virt_start + need_size * i);
        dpak_mp_list_insert(&blk_head->free_list, tmp);
    }
    return 0;
}

static int dpak_mempool_create(struct dpak_mempool *mp, uint32_t ele_size, uint32_t ele_count,
    enum hinic3_module module_id)
{
    int ret;

    if (ele_size == 0) {
        HINIC3_LOG(ERR, AGENT, "Memory pool size can not be %u, mempool create failed!", ele_size);
        return -1;
    }

    uint32_t need_size = dpak_mp_align_up(ele_size + MP_CK_HEADER_LEN);
    ret = dpak_mp_insert_blocks(mp, need_size, ele_count, module_id);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "Init block failed, err is %d!", ret);
        return ret;
    }

    mp->ele_size = ele_size;
    mp->ele_need_size = need_size;
    mp->per_block_ele_count = ele_count;
    ret = pthread_mutex_init(&mp->lock, NULL);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "Init lock failed. errno is %d!", ret);
        return ret;
    }
    mp->is_valid = true;
    return 0;
}

static void *
dpak_mp_alloc_mem_from_block(struct dpak_mp_block *blk, uint64_t size HINIC3_UNUSED)
{
    struct dpak_mp_chunk *allocated = NULL;
    char *alloc = NULL;

    allocated = blk->free_list;
    if (HINIC3_UNLIKELY(allocated == NULL)) {
        return NULL;
    }

    dpak_mp_list_delete(&blk->free_list, allocated);
    blk->free_count--;
    blk->alloc_count++;
    alloc = (char *)allocated + MP_CK_HEADER_LEN;
    return alloc;
}

static void *dpak_mp_alloc_mem_from_blocks(struct dpak_mempool *mp, uint32_t need_size, enum hinic3_module module_id)
{
    struct dpak_mp_block *blk = mp->blk_list;
    void *alloc = NULL;

    while (blk != NULL) {
        if (blk->free_count == 0) {
            blk = blk->next;
            continue;
        }

        alloc = dpak_mp_alloc_mem_from_block(blk, need_size);
        if (HINIC3_LIKELY(alloc != NULL)) {
            mp->ele_used_count++;
            return alloc;
        }
        blk = blk->next;
    }

    HINIC3_LOG(WARNING, AGENT, "Mempool of ele_size %u no enough memory, start alloc new block!", need_size);
    if (mp->is_extend)
    {
        (void)dpak_mp_insert_blocks(mp, need_size, mp->per_block_ele_count, module_id);
    }
    return NULL;
}

static struct dpak_mp_block *dpak_mp_find_block(struct dpak_mempool *mp, void *p)
{
    struct dpak_mp_block *blk = mp->blk_list;
    uint32_t need_size = mp->ele_need_size;

    while (blk != NULL) {
        if ((blk->virt_start <= (char *)p) && ((blk->virt_start + mp->per_block_ele_count * need_size) > (char *)p)) {
            break;
        }
        blk = blk->next;
    }

    return blk;
}

static void dpak_mp_free_blk_heads(struct dpak_mp_block *blk)
{
    struct dpak_mp_block *blk_head = blk;
    struct dpak_mp_block *blk_cur_node = NULL;

    while (blk_head != NULL) {
        blk_cur_node = blk_head;
        blk_head = blk_head->next;
        hinic3_free(blk_cur_node->virt_start);
        hinic3_free(blk_cur_node);
        blk_cur_node = NULL;
    }
}

int dpak_mempool_flush(struct dpak_mempool *mp)
{
    int ret;

    if (mp == NULL) {
        HINIC3_LOG(ERR, AGENT, "Flush mempool is null!");
        return -1;
    }

    uint32_t need_size = mp->ele_need_size;
    uint32_t ele_count = mp->per_block_ele_count;
    struct dpak_mp_block *blk = mp->blk_list;
    struct dpak_mp_chunk *mp_chunk_start = NULL;

    ret = dpak_mp_lock(mp);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "Mp flush locked failed. errno is %d!", ret);
        return -1;
    }

    while (blk != NULL) {
        blk->free_list = NULL;
        memset(blk->virt_start, 0, need_size * ele_count);
        for (uint32_t i = 0; i < ele_count; i++) {
            mp_chunk_start = (struct dpak_mp_chunk *)(blk->virt_start + need_size * i);
            dpak_mp_list_insert(&blk->free_list, mp_chunk_start);
        }
        blk->free_count = ele_count;
        blk->alloc_count = 0;
        blk = blk->next;
    }
    mp->ele_used_count = 0;

    ret = dpak_mp_unlock(mp);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "Mp flush unlocked failed. errno is %d!", ret);
        return -1;
    }

    return 0;
}

void dpak_mempool_destroy(struct dpak_mempool *mp)
{
    int ret;
    if (mp == NULL) {
        HINIC3_LOG(WARNING, AGENT, "Destory mempool is null!");
        return;
    }

    ret = dpak_mp_lock(mp);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "Mp locked failed. errno is %d!", ret);
        return;
    }
    dpak_mp_free_blk_heads(mp->blk_list);
    ret = dpak_mp_unlock(mp);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "Mp unlocked failed. errno is %d!", ret);
        return;
    }
    ret = pthread_mutex_destroy(&mp->lock);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "Mp lock destroy failed. errno is %d!", ret);
        return;
    }
    hinic3_free(mp);
}

void *dpak_mempool_alloc(struct dpak_mempool *mp, enum hinic3_module module_id)
{
    int ret;
    void *alloc = NULL;
    if (HINIC3_UNLIKELY((mp == NULL) || (mp->blk_list == NULL) || (mp->block_num == 0))) {
        HINIC3_LOG(ERR, AGENT, "Alloc mempool is null!");
        return NULL;
    }

    ret = dpak_mp_lock(mp);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "Mp lock failed. errno is %d!", ret);
        return NULL;
    }
    alloc = dpak_mp_alloc_mem_from_blocks(mp, mp->ele_need_size, module_id);
    if (HINIC3_UNLIKELY(alloc == NULL)) {
        /* 此处为支持block可动态扩张，如果一个block里边没有可用内存，则再申请一个 */
        alloc = dpak_mp_alloc_mem_from_blocks(mp, mp->ele_need_size, module_id);
    }
    ret = dpak_mp_unlock(mp);
    if (ret != 0) {
        dpak_mempool_free(mp, alloc);
        HINIC3_LOG(ERR, AGENT, "Mp unlock failed. errno is %d!", ret);
        return NULL;
    }
    return alloc;
}

struct dpak_mempool *hinic3_create_single_mpool(const char *pool_name, uint32_t ele_size, uint32_t ele_num,
    enum hinic3_module module_id, bool is_extend)
{
    if (pool_name == NULL) {
        HINIC3_LOG(ERR, AGENT, "pool_name is NULL!");
        return NULL;
    }
    struct dpak_mempool *pool = (struct dpak_mempool *)hinic3_calloc(1, sizeof(struct dpak_mempool), module_id);
    if (pool == NULL) {
        HINIC3_LOG(ERR, AGENT, "Alloc mpool failed!");
        return NULL;
    }

    int ret = snprintf(pool->pool_name, sizeof(pool->pool_name), "%s", pool_name);
    if (ret <= 0) {
        HINIC3_LOG(ERR, AGENT, "snprintf %s for mpool name failed, err is %d!", pool_name, ret);
        hinic3_free(pool);
        return NULL;
    }

    pool->is_extend = is_extend;
    ret = dpak_mempool_create(pool, ele_size, ele_num, module_id);
    if (ret != 0) {
        dpak_mempool_destroy(pool);
        return NULL;
    }
    return pool;
}

void dpak_mempool_free(struct dpak_mempool *mp, void *ptr)
{
    int ret;
    if (mp == NULL) {
        HINIC3_LOG(ERR, AGENT, "Pointer mp is null!");
        return;
    }
    if ((mp == NULL) || (ptr == NULL)) {
        HINIC3_LOG(ERR, AGENT, "Pointer ptr is null!");
        return;
    }

    struct dpak_mp_block *blk = NULL;
    struct dpak_mp_chunk *ck = NULL;

    ret = dpak_mp_lock(mp);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "Mp lock failed. errno is %d!", ret);
        return;
    }
    blk = dpak_mp_find_block(mp, ptr);
    if (HINIC3_UNLIKELY(blk == NULL)) {
        HINIC3_LOG(WARNING, AGENT, "Find block failed for free element!");
        ret = dpak_mp_unlock(mp);
        if (ret != 0) {
            HINIC3_LOG(ERR, AGENT, "Mp unlock failed. errno is %d!", ret);
        }
        return;
    }

    ck = (struct dpak_mp_chunk *)((char *)ptr - MP_CK_HEADER_LEN);
    dpak_mp_list_insert(&blk->free_list, ck);
    blk->free_count++;
    blk->alloc_count--;
    mp->ele_used_count--;
    ret = dpak_mp_unlock(mp);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "Mp unlock failed, errno is %d!", ret);
        return;
    }
}

void hinic3_show_one_mpool(const struct dpak_mempool *mpool, struct ds *ds, bool is_end)
{
    if ((mpool == NULL) || (mpool->is_valid != true) || ds == NULL) {
        return;
    }

    uint64_t need_size = mpool->ele_need_size;
    hinic3_ds_put_format(ds, "%2smempool %s:\n", HINIC3_UI_INDENT_SPACE, mpool->pool_name);
    hinic3_ds_put_format(ds, "%4selt-size(Byte):            %u\n", HINIC3_UI_INDENT_SPACE, mpool->ele_size);
    hinic3_ds_put_format(ds, "%4stotal-obj-size(Byte):      %u\n", HINIC3_UI_INDENT_SPACE, need_size);
    hinic3_ds_put_format(ds, "%4sblock-count:               %u\n", HINIC3_UI_INDENT_SPACE, mpool->block_num);
    hinic3_ds_put_format(ds, "%4sper-block-ele-num:         %u\n", HINIC3_UI_INDENT_SPACE, mpool->per_block_ele_count);
    hinic3_ds_put_format(ds, "%4sused-ele-count:            %u \n", HINIC3_UI_INDENT_SPACE, mpool->ele_used_count);
    hinic3_ds_put_format(ds, "%4stotal-ele-count:           %u\n", HINIC3_UI_INDENT_SPACE, mpool->ele_total_count);
    hinic3_ds_put_format(ds, "%4sTotal Memory(KByte):       %u\n", HINIC3_UI_INDENT_SPACE,
        mpool->ele_total_count * need_size / HINIC3_KB_TO_BYTE);
    hinic3_ds_put_format(ds, "%4sUsed Memory(KByte):        %u\n", HINIC3_UI_INDENT_SPACE,
        mpool->ele_used_count * need_size / HINIC3_KB_TO_BYTE);
    float mem_usage = mpool->ele_total_count == 0 ?
        (float)0 :
        (((float)mpool->ele_used_count) / ((float)mpool->ele_total_count) * HINIC3_PERCENTAGE);
    hinic3_ds_put_format(ds, "%4sMemory usage(%%):           %.2f\n", HINIC3_UI_INDENT_SPACE, mem_usage);

    if (!is_end) {
    hinic3_ds_put_format(ds, "%s\n", HINIC3_UI_INDENT_SPACE);
    }
    return;
}
