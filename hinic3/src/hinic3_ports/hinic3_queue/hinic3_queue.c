/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2025 Huawei Technologies Co., Ltd
 */

#include "hinic3_iface_global.h"
#include "hinic3_command.h"
#include "hinic3_queue.h"

static bool g_virtual_queue_mode_enabled = false;

bool
hinic3_get_virtual_queue_mode_enabled(void)
{
    return g_virtual_queue_mode_enabled;
}

void
hinic3_set_virtual_queue_mode_enabled(void)
{
    if (hinic3_virtual_queue_multiplex_get() <= 1) {  // 读配置文件判断软件模拟功能是否开启
        g_virtual_queue_mode_enabled = false;
    } else {
        g_virtual_queue_mode_enabled = true;
    }
}

int
hinic3_queue_init(void)
{
    int ret = -EPERM;

    hinic3_eth_dev_tx_lock_init();
    if (hinic3_get_virtual_queue_mode_enabled() == false) {
        ret = 0;
        HINIC3_LOG(INFO, VPORT, "queue module is on standard mode.");
    } else {
        ret = hinic3_virtual_queue_init();
        HINIC3_LOG(INFO, VPORT, "queue module is on virtual mode.");
    }

    return ret;
}


void
hinic3_queue_uninit(void)
{
    if (g_virtual_queue_mode_enabled == false)
        return;
    else
        hinic3_virtual_queue_uninit();
}

int
hinic3_queue_valid(struct hinic3_queue *queue)
{
    if (HINIC3_UNLIKELY(queue == NULL))
        return -EINVAL;

    if (HINIC3_UNLIKELY(queue->vport_id == HINIC3_PORT_ID_INVALID))
        return -EINVAL;

    if (HINIC3_UNLIKELY(queue->dpdk_port_id == INVALID_DPDK_PORT_ID))
        return -EINVAL;

    if (queue->type == HINIC3_TX_QUEUE)
        return 0;

    if (g_virtual_queue_mode_enabled == false)
        return hinic3_standard_queue_valid(queue->variant.stdqueue);
    else
        return hinic3_virtual_queue_valid(queue->variant.virtqueue);
}

int
hinic3_alloc_queues_to_port(struct hinic3_upcall_queue *upcall_queue, uint8_t upcall_queue_num)
{
    if (upcall_queue_num > MAX_RX_QUEUE_PER_VPORT || upcall_queue_num == 0) {
        HINIC3_LOG(ERR, VPORT, "alloc queues failed, invalid argument!");
        return -EINVAL;
    }

    if (g_virtual_queue_mode_enabled == false)
        return 0;

    int ret = -EPERM;
    struct hinic3_queue *queue = NULL;
    struct hinic3_virtual_queue *virtual_queues[MAX_RX_QUEUE_PER_VPORT] = {0};

    ret = hinic3_take_virtual_queue_to_vf(upcall_queue_num,
        upcall_queue->upcall_queue_id, virtual_queues, MAX_RX_QUEUE_PER_VPORT);
    if (ret != 0)
        return ret;

    for (int i = 0; i < upcall_queue_num; ++i) {
        queue = &upcall_queue->rx_queues[i];
        queue->variant.virtqueue = virtual_queues[i];
    }

    return 0;
}

void
hinic3_free_queues_from_port(struct hinic3_upcall_queue *upcall_queue, uint8_t upcall_queue_num)
{
    if (upcall_queue_num > MAX_RX_QUEUE_PER_VPORT || upcall_queue_num == 0)
        return;

    struct hinic3_queue *queue = NULL;
    if (g_virtual_queue_mode_enabled == false)
        return;
    else {
        for (int i = 0; i < upcall_queue_num; ++i) {
            queue = &upcall_queue->rx_queues[i];
            hinic3_take_virtual_queue_back_manager(queue->variant.virtqueue);
            queue->variant.virtqueue = NULL;
        }
    }
}

void
hinic3_reset_queue_info(struct hinic3_queue *queue)
{
    queue->queue_id = INVALID_UPCALL_QUEUE_ID;
    queue->vport_id = HINIC3_PORT_ID_INVALID;
    queue->dpdk_port_id = INVALID_DPDK_PORT_ID;
    queue->dpdk_index_id = INVALID_DPDK_PORT_ID;
    queue->type = HINIC3_UNKNOWN_QUEUE_TYPE;
    rte_atomic64_set(&queue->pkt_stats, 0);
}

uint16_t
hinic3_queue_get_hiovs_queue_id(struct hinic3_queue *queue)
{
    if (hinic3_queue_valid(queue) != 0)
        return INVALID_UPCALL_QUEUE_ID;

    if (g_virtual_queue_mode_enabled == false)
        return queue->variant.stdqueue->hiovs_queue_id;
    else
        return queue->variant.virtqueue->physical_queue->hiovs_queue_id;
}
