/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2025 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_VIRTUAL_QUEUE_H
#define HINIC3_VIRTUAL_QUEUE_H

#include "rte_ring.h"
#include "hinic3_parse_agent_config.h"

#define HINIC3_RING_NAME_MAX 16

struct hinic3_virtual_queue {
    /* 静态成员，赋初始值后只读取不变更 */
    struct hinic3_list node;                      /* 链表节点 */
    uint8_t virtual_queue_group_index;            /* 描述该软件队列属于第几组 */
    uint16_t physical_queue_index;                /* 软件队列的队列id，值与其映射的硬件队列id相等 */
    struct hinic3_physical_queue *physical_queue; /* 该软件队列映射的硬件队列指针 */
    char ring_name[HINIC3_RING_NAME_MAX];         /* 环形队列name,可通过lookup接口获取队列指针 */
    /* 动态成员，队列分配返还时变更 */
    struct rte_ring *ring;                        /* 环形队列，存mbuf指针 */
    struct hinic3_queue *queue_info;              /* 软件队列映射的抽象部分数据 */
};

struct hinic3_physical_queue {
    /* 静态成员，赋初始值后只读取不变更 */
    rte_spinlock_t physical_queue_lock;                 /* 硬件队列自旋锁 */
    uint16_t hinic3_queue_id;                           /* 硬件队列id，0——队列最大值 */
    uint16_t hiovs_queue_id;                            /* 硬件队列组件侧id，与硬件队列id一对一映射 */
    struct hinic3_virtual_queue *virtual_queues[HINIC3_VIRTUAL_QUEUE_MULTIPLEX_MAX]; /* 硬件队列映射的软件队列的列表 */
    /* 动态成员，队列分配返还时变更 */
    bool is_virtual_queue_valids[HINIC3_VIRTUAL_QUEUE_MULTIPLEX_MAX];                /* 标记映射的软件队列是否激活到某端口 */
    uint8_t virtual_queue_valid_cnt;                    /* 硬件队列在被几个激活的软件队列共享 */
    struct rte_mempool *mp;                             /* 硬件队列要取包的内存池 */
};

struct hinic3_virtual_queue_manager {                   /* 软件模拟队列资源池 */
    struct hinic3_list *virtual_queue_groups;           /* 软件队列组链表头节点的数组 */
    uint8_t virtual_queue_group_num;                    /* 软件队列组链表头节点的数组大小 */
    struct hinic3_physical_queue *physical_queues;      /* 硬件队列动态数组指针 */
    uint16_t physical_queue_num;                        /* 硬件队列动态数组大小 */
    struct hinic3_virtual_queue *virtual_queues;        /* 软件队列数组指针，大小为physical_queue_num * virtual_queue_group_num */
    struct hinic3_mutex mutex;
};

struct hinic3_virtual_queue_info {
    uint8_t virtual_queue_group_num;
    uint16_t physical_queue_num;
    uint16_t total_virtual_queue_num;
    uint16_t left_group_queue_num[HINIC3_VIRTUAL_QUEUE_MULTIPLEX_MAX];
    uint16_t left_virtual_queue_num;
};

/* 软件模拟初始化 */
int hinic3_virtual_queue_init(void);
/* 软件模拟逆初始化 */
void hinic3_virtual_queue_uninit(void);
/* 分配软件模拟队列 */
int hinic3_take_virtual_queue_to_vf(uint8_t upcall_queue_num, uint16_t upcall_queue_ids[],
    struct hinic3_virtual_queue *virtual_queues[], uint8_t virtual_queue_size);
/* 释放软件模拟队列 */
void hinic3_take_virtual_queue_back_manager(struct hinic3_virtual_queue *virtual_queue);
/* 软件模拟队列有效性检测 */
int hinic3_virtual_queue_valid(const struct hinic3_virtual_queue *virtual_queue);
/* 获取软件模拟队列使用情况 */
int hinic3_virtual_queue_get_upcall_info(struct hinic3_virtual_queue_info *info);
/* 用软件模拟队列id查询软件队列 */
struct hinic3_virtual_queue *hinic3_get_virtual_queue_by_ring(const char *ring_name);
/* 使用硬件队列id查询硬件队列 */
struct hinic3_physical_queue *hinic3_get_physical_queue_by_index(uint16_t index);
/* 获取所有软件队列 */
struct hinic3_virtual_queue_manager *hinic3_get_virtual_queue_manager(void);
#endif /* HINIC3_VIRTUAL_QUEUE_H */
