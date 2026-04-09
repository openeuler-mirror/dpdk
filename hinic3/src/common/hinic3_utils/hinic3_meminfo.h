/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2024 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_MEMINFO_H
#define HINIC3_MEMINFO_H

#include <stdint.h>
#include <stddef.h>
#include "hinic3_mutex.h"
#include "hinic3_flow_agent_enum.h"

struct hinic3_module_id {
    enum hinic3_module module_id;
} __attribute__((__aligned__(8)));

struct hinic3_module_meminfo {
    struct hinic3_rwlock rwlock;
    size_t allocated_size;
};

struct hinic3_mem_statistics {
    struct hinic3_module_meminfo module_meminfo[HINIC3_MODULE_MAX];
};

void hinic3_meminfo_init(void);
void hinic3_meminfo_uninit(void);
void *hinic3_malloc(size_t size, enum hinic3_module module_id);
void *hinic3_calloc(size_t num, size_t size, enum hinic3_module module_id);
void hinic3_free(void *ptr);

struct hinic3_mem_statistics *hinic3_get_meminfo(void);

#endif
