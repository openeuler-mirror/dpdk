/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2024 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_HUGEPAGE_MEMINFO_H
#define HINIC3_HUGEPAGE_MEMINFO_H

#include "hinic3_list.h"
#include "malloc_elem.h"
#include "rte_malloc.h"
#include "rte_mempool.h"
#include "rte_mbuf.h"
#include "rte_ring.h"
#include "hinic3_parse_agent_config.h"
#include "hinic3_meminfo.h"

#define HINIC3_MODULE_NAME_MAX 30

struct hinic3_module_hugepage_meminfo {
    struct hinic3_rwlock rwlock;
    size_t allocated_size[HINIC3_MODULE_MAX];
};

struct hinic3_rte_memzone_ele {
    struct hinic3_list node;
    const struct rte_memzone *memzone;
    char name[RTE_MEMZONE_NAMESIZE];
    enum hinic3_module module_id;
    size_t allocated_size;
};

struct hinic3_rte_mempool_ele {
    struct hinic3_list node;
    struct rte_mempool *mempool;
    char name[RTE_MEMZONE_NAMESIZE];
    size_t allocated_size;
};

struct hinic3_rte_meminfo_list {
    struct hinic3_mutex mutex;
    struct hinic3_list list_head;
};

struct hinic3_hugepage_mem_statistics {
    struct hinic3_module_hugepage_meminfo heap_meminfo;
    struct hinic3_module_hugepage_meminfo memzone_meminfo;
    struct hinic3_rte_meminfo_list memzone_list;
    struct hinic3_rte_meminfo_list mempool_list;
};

const char *hinic3_get_module_name_from_module_id(enum hinic3_module module_id);
int hinic3_hugepage_meminfo_init(void);
void hinic3_hugepage_meminfo_uninit(void);
struct hinic3_hugepage_mem_statistics *hinic3_get_hugepage_meminfo(void);
// heap部分
void *hinic3_rte_malloc(enum hinic3_module module_id, size_t size, unsigned align);
void *hinic3_rte_zmalloc(enum hinic3_module module_id, size_t size, unsigned align);
void *hinic3_rte_malloc_socket(enum hinic3_module module_id, size_t size, unsigned int align, int socket_arg);
void *hinic3_rte_zmalloc_socket(enum hinic3_module module_id, size_t size, unsigned align, int socket);
void hinic3_rte_free(void *addr);

// memzone部分
const struct rte_memzone *hinic3_rte_memzone_reserve(
    const char *name, size_t len, int socket_id, unsigned flags, enum hinic3_module module_id);
const struct rte_memzone *hinic3_rte_memzone_reserve_aligned(
    const char *name, size_t len, int socket_id, unsigned flags, unsigned align, enum hinic3_module module_id);
int hinic3_rte_memzone_free(const struct rte_memzone *mz);

// mempool部分
struct rte_mempool *hinic3_rte_pktmbuf_pool_create(const char *name, unsigned n,
    unsigned cache_size, uint16_t priv_size, uint16_t data_room_size, int socket_id);
struct rte_mempool *hinic3_rte_mempool_create(const char *name, unsigned n, unsigned elt_size,
    unsigned cache_size, unsigned private_data_size, rte_mempool_ctor_t *mp_init, void *mp_init_arg,
    rte_mempool_obj_cb_t *obj_init, void *obj_init_arg, int socket_id, unsigned flags);
void hinic3_rte_mempool_free(struct rte_mempool *mp);

// ring部分
struct rte_ring *hinic3_ring_create(const char *name, unsigned int count,
    int socket_id, unsigned flags, enum hinic3_module module_id);
void hinic3_ring_free(struct rte_ring *r, enum hinic3_module module_id);
#endif
