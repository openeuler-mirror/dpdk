/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2024 Huawei Technologies Co., Ltd
 */

#include <errno.h>

#include "hinic3_log.h"
#include "hinic3_util.h"
#include "hinic3_hugepage_meminfo.h"

static struct hinic3_hugepage_mem_statistics g_hugepage_mem_statistics = {0};
static struct hinic3_module_name g_module_name[] = {
    {HINIC3_COMMON_RESOURCE, "hwoff-common-resource"},
    {HINIC3_CAPTURE, "hwoff-capture"},
    {HINIC3_COMMAND, "hwoff-command"},
    {HINIC3_CT_OFFLOAD, "hwoff-ct-offload"},
    {HINIC3_DRIVER_ADAPTER, "hwoff-driver-adapter"},
    {HINIC3_FLOWS, "hwoff-flows"},
    {HINIC3_OVS_FLOW, "hwoff-ovs-flows"},
    {HINIC3_INIT, "hwoff-init"},
    {HINIC3_POLICY, "hwoff-policy"},
    {HINIC3_PORTS, "hwoff-ports"},
    {HINIC3_QOS, "hwoff-qos"},
    {HINIC3_SECURITY_FILTER, "hwoff-security-filter"},
    {HINIC3_UFID_MAP, "hwoff-ufid-map"},
    {HINIC3_OVS_MEMPOOL, "hwoff-ovs-mempool"},
    {HINIC3_OVS_UFID_MEMPOOL, "hwoff-ovs-ufid-mempool"},
    {HINIC3_OVS_UFID_MAP, "hwoff-ovs-ufid-map"},
    {HINIC3_PACKET_PARSE, "hwoff-packet-parse"},
    {HINIC3_OVS_SMAC, "hwoff-ovs-smac"},
    {HINIC3_OVS_HINIC3_PRIVATE, "hwoff-ovs-private-data"},
    {HIOVS_MEM, "hiovs-mem"},
    {HINIC3_CMD, "hwoff-command"},
    {HINIC3_MODULE_MAX, "unknown module"},
};

struct hinic3_hugepage_mem_statistics *hinic3_get_hugepage_meminfo(void)
{
    if (hinic3_get_enable_hugepage_meminfo_statistic() == false) {
        return NULL;
    }
    return &g_hugepage_mem_statistics;
}

const char *hinic3_get_module_name_from_module_id(enum hinic3_module module_id)
{
    if (module_id >= 0 && module_id < HINIC3_MODULE_MAX) {
        return g_module_name[module_id].module_name;
    }
    return g_module_name[HINIC3_MODULE_MAX].module_name;
}

int hinic3_hugepage_meminfo_init(void)
{
    int ret = 0;
    // 初始化存储结构
    ret = hinic3_rwlock_init(&g_hugepage_mem_statistics.heap_meminfo.rwlock);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "heap meminfo rwlock init failed!");
        goto heap_rwlock_failed;
    }

    ret = hinic3_rwlock_init(&g_hugepage_mem_statistics.memzone_meminfo.rwlock);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "memzone meminfo rwlock init failed, err is %d!", ret);
        goto memzone_rwlock_failed;
    }

    for (int i = HINIC3_COMMON_RESOURCE; i < HINIC3_MODULE_MAX; i++) {
        g_hugepage_mem_statistics.heap_meminfo.allocated_size[i] = 0;
        g_hugepage_mem_statistics.memzone_meminfo.allocated_size[i] = 0;
    }

    ret = hinic3_pthread_mutex_init(&g_hugepage_mem_statistics.memzone_list.mutex);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "memzone list mutex init failed, err is %d!", ret);
        goto memzone_mutex_failed;
    }

    ret = hinic3_pthread_mutex_init(&g_hugepage_mem_statistics.mempool_list.mutex);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "mempool list mutex init failed, err is %d!", ret);
        goto mempool_mutex_failed;
    }

    hinic3_list_init(&g_hugepage_mem_statistics.memzone_list.list_head);
    hinic3_list_init(&g_hugepage_mem_statistics.mempool_list.list_head);
    return 0;

mempool_mutex_failed:
    hinic3_pthread_mutex_destroy(&g_hugepage_mem_statistics.memzone_list.mutex);
memzone_mutex_failed:
    hinic3_rwlock_destroy(&g_hugepage_mem_statistics.memzone_meminfo.rwlock);
memzone_rwlock_failed:
    hinic3_rwlock_destroy(&g_hugepage_mem_statistics.heap_meminfo.rwlock);
heap_rwlock_failed:
    return -1;
}

void hinic3_hugepage_meminfo_uninit(void)
{
    struct hinic3_rte_memzone_ele *iter = NULL;
    struct hinic3_rte_memzone_ele *next = NULL;
    struct hinic3_rte_mempool_ele *iter_pool = NULL;
    struct hinic3_rte_mempool_ele *next_pool = NULL;

    LIST_FOR_EACH_SAFE(iter_pool, next_pool, node, &g_hugepage_mem_statistics.mempool_list.list_head)
    {
        hinic3_pthread_mutex_lock(&g_hugepage_mem_statistics.mempool_list.mutex);
        hinic3_list_remove(&iter_pool->node);
        hinic3_pthread_mutex_unlock(&g_hugepage_mem_statistics.mempool_list.mutex);
        hinic3_rte_mempool_free(iter_pool->mempool);
        iter_pool->mempool = NULL;
        hinic3_free(iter_pool);
    }
    hinic3_pthread_mutex_destroy(&g_hugepage_mem_statistics.mempool_list.mutex);

    LIST_FOR_EACH_SAFE(iter, next, node, &g_hugepage_mem_statistics.memzone_list.list_head)
    {
        hinic3_pthread_mutex_lock(&g_hugepage_mem_statistics.memzone_list.mutex);
        hinic3_list_remove(&iter->node);
        hinic3_pthread_mutex_unlock(&g_hugepage_mem_statistics.memzone_list.mutex);
        hinic3_rte_memzone_free(iter->memzone);
        iter->memzone = NULL;
        hinic3_free(iter);
    }
    hinic3_pthread_mutex_destroy(&g_hugepage_mem_statistics.memzone_list.mutex);

    hinic3_rwlock_destroy(&g_hugepage_mem_statistics.memzone_meminfo.rwlock);
    hinic3_rwlock_destroy(&g_hugepage_mem_statistics.heap_meminfo.rwlock);
}

static int hinic3_record_hugepage_meminfo(void *addr, enum hinic3_module module_id)
{
    void *elem = (void *)((uintptr_t)addr - MALLOC_ELEM_HEADER_LEN);
    size_t elem_size = ((struct malloc_elem *)elem)->size;
    if (elem_size < MALLOC_ELEM_TRAILER_LEN + sizeof(struct hinic3_module_id)) {
        HINIC3_LOG(ERR, AGENT, "Failed to record hugepage meminfo, malloc elem size is %zu!", elem_size);
        return -EINVAL;
    }
    struct hinic3_module_id *module_info = (struct hinic3_module_id *)((uintptr_t)elem +
        elem_size - MALLOC_ELEM_TRAILER_LEN - sizeof(struct hinic3_module_id));

    module_info->module_id = module_id;
    hinic3_rwlock_wrlock(&g_hugepage_mem_statistics.heap_meminfo.rwlock);
    g_hugepage_mem_statistics.heap_meminfo.allocated_size[module_id] += elem_size;
    hinic3_rwlock_wrunlock(&g_hugepage_mem_statistics.heap_meminfo.rwlock);
    return 0;
}

void *hinic3_rte_malloc(enum hinic3_module module_id, size_t size, unsigned align)
{
    if (hinic3_get_enable_hugepage_meminfo_statistic() == false) {
        return rte_malloc(g_module_name[module_id].module_name, size, align);
    }

    int ret;
    void *ptr = rte_malloc(g_module_name[module_id].module_name, size + sizeof(struct hinic3_module_id), align);
    if (ptr == NULL) {
        HINIC3_LOG(ERR, AGENT,
            "Failed to allocate %" PRIu64 " bytes, module_name: %s!", size, g_module_name[module_id].module_name);
        return NULL;
    }

    ret = hinic3_record_hugepage_meminfo(ptr, module_id);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "Failed to alloc hugepage meminfo!");
        hinic3_rte_free(ptr);
        return NULL;
    }
    return ptr;
}

void *hinic3_rte_zmalloc(enum hinic3_module module_id, size_t size, unsigned align)
{
    if (hinic3_get_enable_hugepage_meminfo_statistic() == false) {
        return rte_zmalloc(g_module_name[module_id].module_name, size, align);
    }

    int ret;
    void *ptr = rte_zmalloc(g_module_name[module_id].module_name, size + sizeof(struct hinic3_module_id), align);
    if (ptr == NULL) {
        HINIC3_LOG(ERR, AGENT,
            "Failed to allocate %" PRIu64 " bytes, module_name: %s!", size, g_module_name[module_id].module_name);
        return NULL;
    }

    ret = hinic3_record_hugepage_meminfo(ptr, module_id);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "Failed to alloc hugepage meminfo, err is %d!", ret);
        hinic3_rte_free(ptr);
        return NULL;
    }
    return ptr;
}

void *hinic3_rte_malloc_socket(enum hinic3_module module_id, size_t size, unsigned int align, int socket_arg)
{
    if (hinic3_get_enable_hugepage_meminfo_statistic() == false) {
        return rte_malloc_socket(g_module_name[module_id].module_name, size, align, socket_arg);
    }

    int ret;
    void *ptr = rte_malloc_socket(g_module_name[module_id].module_name,
        size + sizeof(struct hinic3_module_id), align, socket_arg);
    if (ptr == NULL) {
        HINIC3_LOG(ERR, AGENT,
            "Failed to allocate %" PRIu64 " bytes, module_name: %s!", size, g_module_name[module_id].module_name);
        return NULL;
    }

    ret = hinic3_record_hugepage_meminfo(ptr, module_id);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "Failed to alloc hugepage meminfo, err is %d!", ret);
        hinic3_rte_free(ptr);
        return NULL;
    }
    return ptr;
}

void *hinic3_rte_zmalloc_socket(enum hinic3_module module_id, size_t size, unsigned align, int socket)
{
    if (hinic3_get_enable_hugepage_meminfo_statistic() == false) {
        return rte_zmalloc_socket(g_module_name[module_id].module_name, size, align, socket);
    }

    int ret;
    void *ptr = rte_zmalloc_socket(g_module_name[module_id].module_name,
        size + sizeof(struct hinic3_module_id), align, socket);
    if (ptr == NULL) {
        HINIC3_LOG(ERR, AGENT,
            "Failed to allocate %" PRIu64 " bytes, module_name: %s!", size, g_module_name[module_id].module_name);
        return NULL;
    }

    ret = hinic3_record_hugepage_meminfo(ptr, module_id);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "Failed to alloc hugepage meminfo, err is %d!", ret);
        hinic3_rte_free(ptr);
        return NULL;
    }
    return ptr;
}

void hinic3_rte_free(void *addr)
{
    if (addr == NULL)
        return;

    if (hinic3_get_enable_hugepage_meminfo_statistic() == false) {
        rte_free(addr);
        return;
    }

    void *elem = (void *)((uintptr_t)addr - MALLOC_ELEM_HEADER_LEN);
    size_t elem_size = ((struct malloc_elem *)elem)->size;
    if (elem_size < MALLOC_ELEM_TRAILER_LEN + sizeof(struct hinic3_module_id)) {
        HINIC3_LOG(ERR, AGENT, "Failed to free memory, input wrong addr, malloc elem size is %zu!", elem_size);
        return;
    }
    struct hinic3_module_id *module_info = (struct hinic3_module_id *)((uintptr_t)elem +
        elem_size - MALLOC_ELEM_TRAILER_LEN - sizeof(struct hinic3_module_id));
    if (module_info->module_id >= HINIC3_MODULE_MAX) {
        HINIC3_LOG(ERR, AGENT, "Failed to free memory, parse module_id %d error!", module_info->module_id);
        return;
    }

    hinic3_rwlock_wrlock(&g_hugepage_mem_statistics.heap_meminfo.rwlock);
    if (g_hugepage_mem_statistics.heap_meminfo.allocated_size[module_info->module_id] < elem_size) {
        HINIC3_LOG(ERR, AGENT, "Failed to free memory, elem size error!");
        hinic3_rwlock_wrunlock(&g_hugepage_mem_statistics.heap_meminfo.rwlock);
        return;
    }
    g_hugepage_mem_statistics.heap_meminfo.allocated_size[module_info->module_id] -= elem_size;
    hinic3_rwlock_wrunlock(&g_hugepage_mem_statistics.heap_meminfo.rwlock);
    rte_free(addr);
    return;
}

static int hinic3_record_memzone_info(const struct rte_memzone *mz, const char *name, size_t len, enum hinic3_module module_id)
{
    struct hinic3_rte_memzone_ele *ptr = NULL;
    ptr = (struct hinic3_rte_memzone_ele *)hinic3_malloc(sizeof(struct hinic3_rte_memzone_ele), HINIC3_COMMON_RESOURCE);
    if (ptr == NULL) {
        HINIC3_LOG(ERR, AGENT, "Failed to record memzone!");
        return -ENOMEM;
    }
    ptr->memzone = mz;
    ptr->module_id = module_id;
    ptr->allocated_size = len;
    strcpy(ptr->name, name);
    hinic3_list_init(&ptr->node);
    hinic3_pthread_mutex_lock(&g_hugepage_mem_statistics.memzone_list.mutex);
    hinic3_list_insert(&g_hugepage_mem_statistics.memzone_list.list_head, &ptr->node);
    hinic3_pthread_mutex_unlock(&g_hugepage_mem_statistics.memzone_list.mutex);
    hinic3_rwlock_wrlock(&g_hugepage_mem_statistics.memzone_meminfo.rwlock);
    g_hugepage_mem_statistics.memzone_meminfo.allocated_size[module_id] += len;
    hinic3_rwlock_wrunlock(&g_hugepage_mem_statistics.memzone_meminfo.rwlock);
    return 0;
}

const struct rte_memzone *hinic3_rte_memzone_reserve(const char *name,
    size_t len, int socket_id, unsigned flags, enum hinic3_module module_id)
{
    int ret;
    const struct rte_memzone *mz = rte_memzone_reserve(name, len, socket_id, flags);
    if (mz == NULL) {
        HINIC3_LOG(ERR, AGENT, "Failed to allocate %" PRIu64 " bytes, module_name: %s!", len, name);
        return NULL;
    }

    if (hinic3_get_enable_hugepage_meminfo_statistic() == false) {
        return mz;
    }

    ret = hinic3_record_memzone_info(mz, name, len, module_id);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "Failed to alloc memzone, record error is %d!", ret);
        rte_memzone_free(mz);
        return NULL;
    }
    return mz;
}

const struct rte_memzone *hinic3_rte_memzone_reserve_aligned(
    const char *name, size_t len, int socket_id, unsigned flags, unsigned align, enum hinic3_module module_id)
{
    int ret;
    const struct rte_memzone *mz = rte_memzone_reserve_aligned(name, len, socket_id, flags, align);
    if (mz == NULL) {
        HINIC3_LOG(ERR, AGENT, "Failed to allocate %" PRIu64 " bytes, module_name: %s!", len, name);
        return NULL;
    }

    if (hinic3_get_enable_hugepage_meminfo_statistic() == false) {
        return mz;
    }

    ret = hinic3_record_memzone_info(mz, name, len, module_id);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "Failed to alloc memzone, record error is %d!", ret);
        rte_memzone_free(mz);
        return NULL;
    }
    return mz;
}

int hinic3_rte_memzone_free(const struct rte_memzone *mz)
{
    if (mz == NULL) {
        return -EINVAL;
    }
    if (hinic3_get_enable_hugepage_meminfo_statistic() == false) {
        return rte_memzone_free(mz);
    }

    struct hinic3_rte_memzone_ele *iter = NULL;
    struct hinic3_rte_memzone_ele *next = NULL;
    LIST_FOR_EACH_SAFE(iter, next, node, &g_hugepage_mem_statistics.memzone_list.list_head)
    {
        if (iter->memzone == mz) {
            hinic3_rwlock_wrlock(&g_hugepage_mem_statistics.memzone_meminfo.rwlock);
            if (g_hugepage_mem_statistics.memzone_meminfo.allocated_size[iter->module_id] < iter->allocated_size) {
                HINIC3_LOG(ERR, AGENT, "Failed to free memzone, memzone size error!");
                hinic3_rwlock_wrunlock(&g_hugepage_mem_statistics.memzone_meminfo.rwlock);
                return -EINVAL;
            }
            g_hugepage_mem_statistics.memzone_meminfo.allocated_size[iter->module_id] -= iter->allocated_size;
            hinic3_rwlock_wrunlock(&g_hugepage_mem_statistics.memzone_meminfo.rwlock);

            hinic3_pthread_mutex_lock(&g_hugepage_mem_statistics.memzone_list.mutex);
            hinic3_list_remove(&iter->node);
            hinic3_pthread_mutex_unlock(&g_hugepage_mem_statistics.memzone_list.mutex);

            rte_memzone_free(mz);
            iter->memzone = NULL;
            hinic3_free(iter);
            return 0;
        }
    }
    return -EINVAL;
}

static int hinic3_record_mempool_info(struct rte_mempool *mp, const char *name, unsigned private_data_size)
{
    struct hinic3_rte_mempool_ele *ptr = NULL;
    uint32_t unit_size = mp->elt_size + mp->header_size + mp->trailer_size;

    ptr = (struct hinic3_rte_mempool_ele *)hinic3_malloc(sizeof(struct hinic3_rte_mempool_ele), HINIC3_COMMON_RESOURCE);
    if (ptr == NULL) {
        HINIC3_LOG(ERR, AGENT, "Failed to record mempool!");
        return -ENOMEM;
    }
    ptr->mempool = mp;
    ptr->allocated_size = unit_size * mp->size + mp->mz->len + private_data_size;
    strcpy(ptr->name, name);
    hinic3_list_init(&ptr->node);
    hinic3_pthread_mutex_lock(&g_hugepage_mem_statistics.mempool_list.mutex);
    hinic3_list_insert(&g_hugepage_mem_statistics.mempool_list.list_head, &ptr->node);
    hinic3_pthread_mutex_unlock(&g_hugepage_mem_statistics.mempool_list.mutex);
    return 0;
}

struct rte_mempool *hinic3_rte_pktmbuf_pool_create(
    const char *name, unsigned n, unsigned cache_size, uint16_t priv_size, uint16_t data_room_size, int socket_id)
{
    int ret;
    struct rte_mempool *mp = rte_pktmbuf_pool_create(name, n, cache_size, priv_size, data_room_size, socket_id);
    if (mp == NULL) {
        HINIC3_LOG(ERR, AGENT, "Failed to allocate %" PRIu64 " bytes, module_name: %s!", n, name);
        return NULL;
    }

    if (hinic3_get_enable_hugepage_meminfo_statistic() == false) {
        return mp;
    }

    ret = hinic3_record_mempool_info(mp, name, 0);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "Failed to alloc mempool, record error!");
        rte_mempool_free(mp);
        return NULL;
    }
    return mp;
}

struct rte_mempool *hinic3_rte_mempool_create(const char *name, unsigned n, unsigned elt_size, unsigned cache_size,
    unsigned private_data_size, rte_mempool_ctor_t *mp_init, void *mp_init_arg, rte_mempool_obj_cb_t *obj_init,
    void *obj_init_arg, int socket_id, unsigned flags)
{
    int ret;
    struct rte_mempool *mp = rte_mempool_create(name, n, elt_size, cache_size,
        private_data_size, mp_init, mp_init_arg, obj_init, obj_init_arg, socket_id, flags);
    if (mp == NULL) {
        HINIC3_LOG(ERR, AGENT, "Failed to allocate %" PRIu64 " bytes, module_name: %s!", n, name);
        return NULL;
    }

    if (hinic3_get_enable_hugepage_meminfo_statistic() == false) {
        return mp;
    }

    ret = hinic3_record_mempool_info(mp, name, private_data_size);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "Failed to alloc mempool, record error is %d!", ret);
        rte_mempool_free(mp);
        return NULL;
    }
    return mp;
}

void hinic3_rte_mempool_free(struct rte_mempool *mp)
{
    if (hinic3_get_enable_hugepage_meminfo_statistic() == false) {
        rte_mempool_free(mp);
        return;
    }

    struct hinic3_rte_mempool_ele *iter = NULL;
    struct hinic3_rte_mempool_ele *next = NULL;
    LIST_FOR_EACH_SAFE(iter, next, node, &g_hugepage_mem_statistics.mempool_list.list_head)
    {
        if (iter->mempool == mp) {
            hinic3_pthread_mutex_lock(&g_hugepage_mem_statistics.mempool_list.mutex);
            hinic3_list_remove(&iter->node);
            hinic3_pthread_mutex_unlock(&g_hugepage_mem_statistics.mempool_list.mutex);
            rte_mempool_free(mp);
            iter->mempool = NULL;
            hinic3_free(iter);
            return;
        }
    }
}

struct rte_ring *
hinic3_ring_create(const char *name, unsigned int count,
    int socket_id, unsigned flags, enum hinic3_module module_id)
{
    struct rte_ring *r = NULL;
    ssize_t ring_memsize = rte_ring_get_memsize(count);
    if (ring_memsize < 0 || module_id < 0 || module_id >= HINIC3_MODULE_MAX) {
        HINIC3_LOG(ERR, AGENT, "Failed to get ring memsize!");
        return NULL;
    }

    r = rte_ring_create(name, count, socket_id, flags);
    if (r == NULL || ring_memsize < 0) {
        HINIC3_LOG(ERR, AGENT, "Failed to allocate ring, module_name: %s!");
        return NULL;
    }

    if (hinic3_get_enable_hugepage_meminfo_statistic() == false)
        return r;

    hinic3_rwlock_wrlock(&g_hugepage_mem_statistics.memzone_meminfo.rwlock);
    g_hugepage_mem_statistics.memzone_meminfo.allocated_size[module_id] += (size_t)ring_memsize;
    hinic3_rwlock_wrunlock(&g_hugepage_mem_statistics.memzone_meminfo.rwlock);

    return r;
}

void
hinic3_ring_free(struct rte_ring *r, enum hinic3_module module_id)
{
    if (r == NULL || module_id < 0 || module_id >= HINIC3_MODULE_MAX)
        return;

    if (hinic3_get_enable_hugepage_meminfo_statistic() == false) {
        rte_ring_free(r);
        return;
    }

    ssize_t ring_memsize = rte_ring_get_memsize(rte_ring_get_size(r));
    if (ring_memsize < 0) {
        HINIC3_LOG(ERR, AGENT, "Failed to get ring memsize in ring free, the ring memsize is %zd!", ring_memsize);
        rte_ring_free(r);
        return;
    }

    hinic3_rwlock_wrlock(&g_hugepage_mem_statistics.memzone_meminfo.rwlock);
    if (g_hugepage_mem_statistics.memzone_meminfo.allocated_size[module_id] < (size_t)ring_memsize) {
        hinic3_rwlock_wrunlock(&g_hugepage_mem_statistics.memzone_meminfo.rwlock);
        HINIC3_LOG(ERR, AGENT, "Ring memsize error, memzone_meminfo: %zu ring_mensize: %zd!",
            g_hugepage_mem_statistics.memzone_meminfo.allocated_size[module_id], ring_memsize);
        rte_ring_free(r);
        return;
    }

    g_hugepage_mem_statistics.memzone_meminfo.allocated_size[module_id] -= (size_t)ring_memsize;
    hinic3_rwlock_wrunlock(&g_hugepage_mem_statistics.memzone_meminfo.rwlock);
    rte_ring_free(r);
    return;
}