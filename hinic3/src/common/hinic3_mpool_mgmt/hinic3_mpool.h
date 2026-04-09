/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_MPOOL_H
#define HINIC3_MPOOL_H

#include <stdint.h>
#include "hinic3_mutex.h"
#include "hinic3_ds.h"
#include "hinic3_meminfo.h"

#define HINIC3_POOL_NAME_MAX_LEN                     32
#define MP_CK_HEADER_LEN    sizeof(struct dpak_mp_chunk)
#define HINIC3_KB_TO_BYTE                            1024
#define HINIC3_PERCENTAGE                            100

struct dpak_mp_chunk {
    struct dpak_mp_chunk *next;
};

struct dpak_mp_block {
    struct dpak_mp_chunk *free_list;
    struct dpak_mp_block *next;

    /* The memory pool block's start virtual address */
    char *virt_start;
    uint32_t free_count;
    uint32_t alloc_count;
};

struct dpak_mempool {
    char pool_name[HINIC3_POOL_NAME_MAX_LEN];
    uint32_t ele_size;
    uint32_t ele_need_size;
    uint32_t per_block_ele_count;
    uint32_t ele_total_count;
    uint32_t ele_used_count;
    uint32_t block_num;
    struct dpak_mp_block *blk_list;
    pthread_mutex_t lock;
    bool is_valid;
    bool is_extend;
};

int dpak_mempool_flush(struct dpak_mempool *mp);
void dpak_mempool_free(struct dpak_mempool *mp, void *ptr);
void dpak_mempool_destroy(struct dpak_mempool *mp);
struct dpak_mempool *hinic3_create_single_mpool(const char *pool_name, uint32_t ele_size, uint32_t ele_num,
    enum hinic3_module module_id, bool is_extend);
void *dpak_mempool_alloc(struct dpak_mempool *mp, enum hinic3_module module_id);
void hinic3_show_one_mpool(const struct dpak_mempool *mpool, struct ds *ds, bool is_end);
#endif
