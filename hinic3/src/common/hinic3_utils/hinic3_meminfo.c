/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2024 Huawei Technologies Co., Ltd
 */

#include <stdlib.h>
#include <malloc.h>
#include <inttypes.h>
#include "hinic3_log.h"
#include "hinic3_meminfo.h"

#define HINIC3_CALLOC_DEFAULT_NUM 1
#define HINIC3_ALLOC_SIZE_MAX (1 << 30)

static struct hinic3_mem_statistics g_mem_statistics = { 0 };

static void hinic3_mark_mem_header(void *ptr, enum hinic3_module module_id)
{
    (*(struct hinic3_module_id *)ptr).module_id = module_id;
}

static void hinic3_record_meminfo(size_t allocated_size, enum hinic3_module module_id)
{
    struct hinic3_module_meminfo *module_meminfo = &g_mem_statistics.module_meminfo[module_id];

    hinic3_rwlock_wrlock(&module_meminfo->rwlock);
    module_meminfo->allocated_size += allocated_size;
    hinic3_rwlock_wrunlock(&module_meminfo->rwlock);
    return;
}

void hinic3_meminfo_init(void)
{
    for (int i = 0; i < HINIC3_MODULE_MAX; i++) {
        (void)hinic3_rwlock_init(&g_mem_statistics.module_meminfo[i].rwlock);
    }
}

void hinic3_meminfo_uninit(void)
{
    for (int i = 0; i < HINIC3_MODULE_MAX; i++) {
        (void)hinic3_rwlock_destroy(&g_mem_statistics.module_meminfo[i].rwlock);
    }
}

void *hinic3_malloc(size_t size, enum hinic3_module module_id)
{
    void *ptr = NULL;

    if (size > HINIC3_ALLOC_SIZE_MAX) {
        HINIC3_LOG(ERR, AGENT, "Failed to allocate %zu bytes, exceeded 1 GB, module_id: %d!", size, module_id);
        return NULL;
    }

    ptr = malloc(sizeof(struct hinic3_module_id) + size);
    if (ptr == NULL) {
        HINIC3_LOG(ERR, AGENT, "Failed to allocate %zu bytes, module_id: %d!", size, module_id);
        return NULL;
    }
    hinic3_mark_mem_header(ptr, module_id);
    hinic3_record_meminfo(malloc_usable_size(ptr), module_id);

    return (void *)((struct hinic3_module_id *)ptr + 1);
}

void *hinic3_calloc(size_t num, size_t size, enum hinic3_module module_id)
{
    void *ptr = NULL;
    size_t total_size = num * size;

    if (total_size > HINIC3_ALLOC_SIZE_MAX) {
        HINIC3_LOG(ERR, AGENT, "Failed to allocate %" PRIu64 " bytes, exceeded 1 GB, module_id: %d!", size, module_id);
        return NULL;
    }

    ptr = calloc(HINIC3_CALLOC_DEFAULT_NUM, sizeof(struct hinic3_module_id) + total_size);
    if (ptr == NULL) {
        HINIC3_LOG(ERR, AGENT, "Failed to allocate %" PRIu64 " bytes, module_id: %d!", size, module_id);
        return NULL;
    }
    hinic3_mark_mem_header(ptr, module_id);
    hinic3_record_meminfo(malloc_usable_size(ptr), module_id);

    return (void *)((struct hinic3_module_id *)ptr + 1);
}

void hinic3_free(void *ptr)
{
    if (ptr == NULL) {
        return;
    }

    void *original_ptr = (void *)((struct hinic3_module_id *)ptr - 1);
    size_t allocated_size = malloc_usable_size(original_ptr);
    enum hinic3_module module_id = (*(struct hinic3_module_id *)original_ptr).module_id;

    hinic3_rwlock_wrlock(&g_mem_statistics.module_meminfo[module_id].rwlock);
    g_mem_statistics.module_meminfo[module_id].allocated_size -= allocated_size;
    hinic3_rwlock_wrunlock(&g_mem_statistics.module_meminfo[module_id].rwlock);

    free(original_ptr);
    return;
}

struct hinic3_mem_statistics *hinic3_get_meminfo(void)
{
    return &g_mem_statistics;
}
