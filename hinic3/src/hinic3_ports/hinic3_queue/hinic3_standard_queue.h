/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2025 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_STANDARD_QUEUE_H
#define HINIC3_STANDARD_QUEUE_H

struct hinic3_standard_queue {
    uint16_t hiovs_queue_id; /* 硬件队列组件侧id，与硬件队列id一对一映射 */
    struct rte_mempool *mp;
    uint8_t share_upcall;
    struct hinic3_queue *queue_info;
};

/* 硬件标准队列有效性检测 */
int hinic3_standard_queue_valid(const struct hinic3_standard_queue *stdqueue);
#endif /* HINIC3_STANDARD_QUEUE_H */
