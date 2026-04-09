/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2025 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_QUEUE_H
#define HINIC3_QUEUE_H

#include "hinic3_port_util.h"
#include "hinic3_standard_queue.h"
#include "hinic3_virtual_queue.h"

enum queue_type {
    HINIC3_UNKNOWN_QUEUE_TYPE,
    HINIC3_TX_QUEUE,
    HINIC3_STANDARD_RX_QUEUE,
    HINIC3_SHARED_RX_QUEUE,
    HINIC3_VIRTUAL_RX_QUEUE,
};

struct hinic3_queue {
    uint16_t queue_id;                          /* 端口上的队列编号 */
    uint16_t vport_id;                          /* 队列绑定的端口vport id */
    uint16_t dpdk_port_id;                      /* vport id对应的dpdk port id */
    uint16_t dpdk_index_id;                     /* 收发包统计时用于获取到vf_dev */
    enum queue_type type;                       /* 队列类型 */
    rte_atomic64_t pkt_stats;                   /* 队列级收包统计 */
    union {                                     /* 该联合体表示队列的具象变体 */
        struct hinic3_standard_queue *stdqueue; /* 硬件标准upcall队列standard_queue有效 */
        struct hinic3_virtual_queue *virtqueue; /* 软件模拟upcall队列virtual_queue有效 */
    } variant;                                  /* reinject队列无需访问该联合体 */
};

struct hinic3_upcall_queue {
    bool is_queue_valid[MAX_RX_QUEUE_PER_VPORT];
    uint16_t upcall_queue_id[MAX_RX_QUEUE_PER_VPORT];
    struct hinic3_queue rx_queues[MAX_RX_QUEUE_PER_VPORT];
};

struct hinic3_reinject_queue {
    bool is_queue_valid[MAX_TX_QUEUE_PER_VPORT];
    struct hinic3_queue tx_queues[MAX_TX_QUEUE_PER_VPORT];
};

/* 硬件标准/软件模拟 upcall特性开关置位 */
void hinic3_set_virtual_queue_mode_enabled(void);
/* 获取 硬件标准/软件模拟 upcall特性开关 */
bool hinic3_get_virtual_queue_mode_enabled(void);
/* 队列模块初始化 */
int hinic3_queue_init(void);
/* 队列模块逆初始化 */
void hinic3_queue_uninit(void);
/* 队列有效性检测 */
int hinic3_queue_valid(struct hinic3_queue *queue);
/* 从队列资源池分配队列到端口 */
int hinic3_alloc_queues_to_port(struct hinic3_upcall_queue *upcall_queue, uint8_t upcall_queue_num);
/* 从端口释放队列回队列资源池 */
void hinic3_free_queues_from_port(struct hinic3_upcall_queue *upcall_queue, uint8_t upcall_queue_num);
/* 初始化队列结构体 */
void hinic3_reset_queue_info(struct hinic3_queue *queue);
/* 获取hiovs queue id */
uint16_t hinic3_queue_get_hiovs_queue_id(struct hinic3_queue *queue);
#endif /* HINIC3_QUEUE_H */
