/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2025 Huawei Technologies Co., Ltd
 */

#include "hinic3_port_util.h"
#include "hinic3_virtual_queue.h"

static struct hinic3_virtual_queue_manager g_virtual_queue_manager = { 0 };

static int
hinic3_create_physical_and_virtual_queue_all(void)
{
    struct hinic3_virtual_queue *virtual_queue = NULL;
    struct hinic3_physical_queue *physical_queue = NULL;
    uint8_t group_num = g_virtual_queue_manager.virtual_queue_group_num;
    uint16_t physical_queue_num = g_virtual_queue_manager.physical_queue_num;

    /* 初始化硬件队列内存结构的值 */
    for (uint16_t physical_queue_index = 0; physical_queue_index < physical_queue_num; ++physical_queue_index) {
        physical_queue = &g_virtual_queue_manager.physical_queues[physical_queue_index];
        physical_queue->virtual_queue_valid_cnt = 0;
        physical_queue->hinic3_queue_id = physical_queue_index;
        physical_queue->hiovs_queue_id = INVALID_UPCALL_QUEUE_ID; // 该值在端口configure后赋值为从组件侧获取的id
        physical_queue->mp = NULL;
        rte_spinlock_init(&physical_queue->physical_queue_lock);
    }
    /* 创初始化软件队列内存结构的值 */
    for (uint8_t group_index = 0; group_index < group_num; ++group_index) {
        hinic3_list_init(&g_virtual_queue_manager.virtual_queue_groups[group_index]);
        for (uint16_t physical_queue_index = 0; physical_queue_index < physical_queue_num; ++physical_queue_index) {
            /* 为软件队列节点赋初始值 */
            virtual_queue =
                &g_virtual_queue_manager.virtual_queues[physical_queue_index + group_index * physical_queue_num];
            virtual_queue->physical_queue_index = physical_queue_index;
            virtual_queue->virtual_queue_group_index = group_index;
            int ret = snprintf(virtual_queue->ring_name, HINIC3_RING_NAME_MAX,
                "group%u-rx%u", group_index, physical_queue_index);
            if (ret <= 0 || ret >= HINIC3_RING_NAME_MAX) {
                HINIC3_LOG(ERR, VPORT, "The virtual queue ring name is misspelled.");
                return -ERANGE;
            }
            hinic3_list_init(&virtual_queue->node);
            hinic3_list_insert(&g_virtual_queue_manager.virtual_queue_groups[group_index], &virtual_queue->node);
            /* 建立硬件队列与软件队列的映射关系 */
            physical_queue = &g_virtual_queue_manager.physical_queues[physical_queue_index];
            physical_queue->virtual_queues[group_index] = virtual_queue;
            physical_queue->is_virtual_queue_valids[group_index] = false;
            virtual_queue->physical_queue = physical_queue;
        }
    }

    return 0;
}

static int
hinic3_virtual_queue_malloc_manager_mem(struct hinic3_port_upcall_info *info)
{
    uint16_t virtual_queue_num = 0;

    g_virtual_queue_manager.physical_queue_num = info->total_upcall_qnum;
    g_virtual_queue_manager.physical_queues = (struct hinic3_physical_queue *)hinic3_calloc(
        g_virtual_queue_manager.physical_queue_num, sizeof(struct hinic3_physical_queue), HINIC3_PORTS);
    if (g_virtual_queue_manager.physical_queues == NULL) {
        HINIC3_LOG(ERR, VPORT, "failed to create physical queues!");
        goto fail;
    };

    g_virtual_queue_manager.virtual_queue_group_num = hinic3_virtual_queue_multiplex_get();
    g_virtual_queue_manager.virtual_queue_groups = (struct hinic3_list *)hinic3_calloc(
        g_virtual_queue_manager.virtual_queue_group_num, sizeof(struct hinic3_list), HINIC3_PORTS);
    if (g_virtual_queue_manager.virtual_queue_groups == NULL) {
        HINIC3_LOG(ERR, VPORT, "failed to create virtual queue groups!");
        goto fail;
    };

    virtual_queue_num = g_virtual_queue_manager.physical_queue_num * g_virtual_queue_manager.virtual_queue_group_num;
    g_virtual_queue_manager.virtual_queues =
        (struct hinic3_virtual_queue *)hinic3_calloc(virtual_queue_num, sizeof(struct hinic3_virtual_queue), HINIC3_PORTS);
    if (g_virtual_queue_manager.virtual_queues == NULL) {
        HINIC3_LOG(ERR, VPORT, "failed to create virtual queues!");
        goto fail;
    }

    return 0;
fail:
    hinic3_virtual_queue_uninit();
    return -ENOMEM;
}

int
hinic3_virtual_queue_init(void)
{
    int ret = 0;
    struct hinic3_port_upcall_info info = {0};

    ret = hinic3_port_mgmt_get_upcall_info(&info);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "failed to get mgmt upcall queue info!");
        return ret;
    }

    ret = hinic3_pthread_mutex_init(&g_virtual_queue_manager.mutex);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "failed to init virtual queue manager mutex!");
        return ret;
    }

    ret = hinic3_virtual_queue_malloc_manager_mem(&info);
    if (ret != 0)
        return ret;

    ret = hinic3_create_physical_and_virtual_queue_all();
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "failed to init virtual queue!");
        hinic3_virtual_queue_uninit();
        return ret;
    }

    return 0;
}

void
hinic3_virtual_queue_uninit(void)
{
    // 先释放端口，返还所有软件队列到链表
    if (g_virtual_queue_manager.virtual_queues != NULL)
        hinic3_free(g_virtual_queue_manager.virtual_queues);

    if (g_virtual_queue_manager.virtual_queue_groups != NULL)
        hinic3_free(g_virtual_queue_manager.virtual_queue_groups);

    if (g_virtual_queue_manager.physical_queues != NULL)
        hinic3_free(g_virtual_queue_manager.physical_queues);

    hinic3_pthread_mutex_destroy(&g_virtual_queue_manager.mutex);
}

static void
hinic3_move_node_to_other_list(struct hinic3_list *src_list, struct hinic3_list *dst_list)
{
    struct hinic3_list *node;
    node = src_list->next;
    hinic3_list_remove(src_list->next);
    hinic3_list_init(node);
    hinic3_list_insert(dst_list->next, node);
}

static struct hinic3_virtual_queue *
hinic3_check_virtual_queue_available(uint8_t group_index, bool *is_physical_queue_selected, struct hinic3_list *buffer)
{
    struct hinic3_virtual_queue *virtual_queue = NULL;

    while (hinic3_list_is_empty(&g_virtual_queue_manager.virtual_queue_groups[group_index]) == false) {
        virtual_queue = HINIC3_CONTAINER_OF(
            g_virtual_queue_manager.virtual_queue_groups[group_index].next, struct hinic3_virtual_queue, node);
        if (is_physical_queue_selected[virtual_queue->physical_queue_index] == true)
            hinic3_move_node_to_other_list(&g_virtual_queue_manager.virtual_queue_groups[group_index], buffer);
        else
            break; // 不允许跨group取的软件队列映射到同一个硬件队列上
    }

    return virtual_queue;
}

int
hinic3_take_virtual_queue_to_vf(uint8_t upcall_queue_num, uint16_t upcall_queue_ids[],
    struct hinic3_virtual_queue *virtual_queues[], uint8_t virtual_queue_size)
{
    int alloced_queue_num = 0;
    struct hinic3_virtual_queue *virtual_queue = NULL;
    bool *is_physical_queue_selected = NULL;
    uint16_t physical_queue_num = g_virtual_queue_manager.physical_queue_num;
    struct hinic3_list buffer;

    hinic3_list_init(&buffer);
    is_physical_queue_selected = (bool *)hinic3_calloc(physical_queue_num, sizeof(bool), HINIC3_PORTS);
    if (is_physical_queue_selected == NULL) {
        HINIC3_LOG(ERR, VPORT, "take virtual queues failed, no enough memory.");
        return -ENOMEM;
    }

    upcall_queue_num = upcall_queue_num > virtual_queue_size ? virtual_queue_size : upcall_queue_num;
    hinic3_pthread_mutex_lock(&g_virtual_queue_manager.mutex);
    for (uint8_t idx = 0; idx < upcall_queue_num; ++idx) {
        for (uint8_t group_index = 0; group_index < g_virtual_queue_manager.virtual_queue_group_num; ++group_index) {
            virtual_queue = hinic3_check_virtual_queue_available(group_index, is_physical_queue_selected, &buffer);
            if (virtual_queue == NULL)
                continue;

            hinic3_list_remove(&virtual_queue->node);
            virtual_queues[idx] = virtual_queue;
            upcall_queue_ids[idx] = virtual_queue->physical_queue->hinic3_queue_id;
            is_physical_queue_selected[virtual_queue->physical_queue_index] = true;
            ++alloced_queue_num;
            break;
        }
    }

    while (hinic3_list_is_empty(&buffer) == false) {
        virtual_queue = HINIC3_CONTAINER_OF(buffer.next, struct hinic3_virtual_queue, node);
        hinic3_move_node_to_other_list(
            &buffer, &g_virtual_queue_manager.virtual_queue_groups[virtual_queue->virtual_queue_group_index]);
    }
    hinic3_pthread_mutex_unlock(&g_virtual_queue_manager.mutex);

    hinic3_free(is_physical_queue_selected);
    if (alloced_queue_num != upcall_queue_num) {
        for (uint8_t idx = 0; idx < alloced_queue_num; ++idx) {
            hinic3_take_virtual_queue_back_manager(virtual_queues[idx]);
            upcall_queue_ids[idx] = INVALID_UPCALL_QUEUE_ID;
        }
        HINIC3_LOG(ERR, VPORT, "virtual rx queues is not enough!");
        return -ENOSPC;
    }
    return 0;
}

void
hinic3_take_virtual_queue_back_manager(struct hinic3_virtual_queue *virtual_queue)
{
    if (virtual_queue == NULL)
        return;

    virtual_queue->queue_info = NULL;
    hinic3_list_init(&virtual_queue->node);
    hinic3_list_insert(
        &g_virtual_queue_manager.virtual_queue_groups[virtual_queue->virtual_queue_group_index], &virtual_queue->node);
}

int
hinic3_virtual_queue_valid(const struct hinic3_virtual_queue *virtual_queue)
{
    if (HINIC3_UNLIKELY(virtual_queue == NULL))
        return -EINVAL;

    if (HINIC3_UNLIKELY(virtual_queue->queue_info == NULL))
        return -EINVAL;

    if (HINIC3_UNLIKELY(virtual_queue->physical_queue->hiovs_queue_id == INVALID_UPCALL_QUEUE_ID))
        return -EINVAL;

    return 0;
}

int
hinic3_virtual_queue_get_upcall_info(struct hinic3_virtual_queue_info *info)
{
    if (info == NULL) {
        HINIC3_LOG(ERR, VPORT, "failed to get virtual queues info!");
        return -EINVAL;
    }
    hinic3_pthread_mutex_lock(&g_virtual_queue_manager.mutex);
    info->physical_queue_num = g_virtual_queue_manager.physical_queue_num;
    info->virtual_queue_group_num = g_virtual_queue_manager.virtual_queue_group_num;
    info->total_virtual_queue_num = info->physical_queue_num * info->virtual_queue_group_num;
    info->left_virtual_queue_num = 0;
    for (uint8_t group_index = 0; group_index < info->virtual_queue_group_num; ++group_index) {
        info->left_group_queue_num[group_index] =
            hinic3_list_size(&g_virtual_queue_manager.virtual_queue_groups[group_index]);
        info->left_virtual_queue_num += info->left_group_queue_num[group_index];
    }
    hinic3_pthread_mutex_unlock(&g_virtual_queue_manager.mutex);
    return 0;
}

struct hinic3_virtual_queue *
hinic3_get_virtual_queue_by_ring(const char *ring_name)
{
    if (ring_name == NULL) {
        HINIC3_LOG(ERR, VPORT, "failed to get virtual queues by ring name!");
        return NULL;
    }

    uint16_t total_virtual_queue_num =
        g_virtual_queue_manager.physical_queue_num * g_virtual_queue_manager.virtual_queue_group_num;
    struct hinic3_virtual_queue *virtual_queue = NULL;

    hinic3_pthread_mutex_lock(&g_virtual_queue_manager.mutex);
    for (uint16_t idx = 0; idx < total_virtual_queue_num; ++idx) {
        virtual_queue = &g_virtual_queue_manager.virtual_queues[idx];
        if (strcmp(virtual_queue->ring_name, ring_name) == 0) {
            hinic3_pthread_mutex_unlock(&g_virtual_queue_manager.mutex);
            return virtual_queue;
        }
    }

    hinic3_pthread_mutex_unlock(&g_virtual_queue_manager.mutex);
    return NULL;
}

struct hinic3_physical_queue *
hinic3_get_physical_queue_by_index(uint16_t index)
{
    if (index >= g_virtual_queue_manager.physical_queue_num)
        return NULL;

    return &g_virtual_queue_manager.physical_queues[index];
}

struct hinic3_virtual_queue_manager *
hinic3_get_virtual_queue_manager(void)
{
    return &g_virtual_queue_manager;
}