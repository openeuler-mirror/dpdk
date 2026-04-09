/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2022 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_PORT_UTIL_H
#define HINIC3_PORT_UTIL_H

#include "hinic3_tlv_key.h"
#include "hinic3_meminfo.h"
#include "hinic3_hugepage_meminfo.h"
#include "hinic3_iface_port.h"
#include "ethdev_driver.h"

#define HINIC3_DEV_STATE_STOP       0
#define HINIC3_DEV_STATE_START      1
#define HINIC3_INVALID_QUEUE_NUM    0
#define HINIC3_MAX_UC_MAC_ADDRS     1
#define HINIC3_NETDEV_NAME_MAX_LENGTH         512

#define HINIC3_ETH_VDEV_DRV_NAME  "net_hwsp"

#define INVALID_UPCALL_QUEUE_ID    0xff
#define MAX_RX_QUEUE_PER_VPORT     16
#define MAX_TX_QUEUE_PER_VPORT     16

#define HW_NB_XSTATS               3
#define ETH_XSTATS_NAME_SIZE       64
#define XSTATS_UPCALL_PKT_INDEX    0
#define XSTATS_REINJECT_PKT_INDEX  1
#define XSTATS_UPCALL_DROP_INDEX   2

#define HINIC3_PORT_TYPE_MASK      0xF000
#define HINIC3_BOND_TYPE_PREFIX    0x3000
#define HINIC3_VF_TYPE_PREFIX      0x1000

/* u16前4bit位表示端口类型 */
static inline bool hinic3_is_bond_by_prefix(uint16_t vport_id)
{
    return (vport_id & HINIC3_PORT_TYPE_MASK) == HINIC3_BOND_TYPE_PREFIX;
}

static inline uint16_t hinic3_function_id_to_vport_id(int function_id){
    return function_id | HINIC3_VF_TYPE_PREFIX;
}

/* 端口有效性校验 */
bool hinic3_is_ethdev_valid(struct rte_eth_dev *dev);

void *hinic3_get_rte_eth_dev(uint16_t port_id);
void *hinic3_get_private_data(uint16_t port_id);
int hinic3_get_port_ifindex(uint16_t src_port, uint32_t *ifindex);

/* 获取端口id */
int hinic3_get_id_from_dev(struct rte_eth_dev *dev, uint16_t *port_id);
/* 获取端口xstats名称 */
int hinic3_port_xstats_get_names(struct rte_eth_dev *dev __rte_unused,
    struct rte_eth_xstat_name *xstats_names, unsigned int limit);
int hinic3_get_netdev_name(const char *port_name, char *netdev_name);

/* 从网卡驱动获取端口队列id列表 */
int hinic3_eth_get_upcall_queue_map(uint16_t vport_id, uint8_t n_upcall_queue, uint16_t *upcall_queue_id);
/* 从网卡驱动获取端口vport id到dpdk port id映射 */
int hinic3_eth_get_dpdk_port_id(uint16_t vport_id, uint16_t *dpdk_port_id);
/* 获取dpdk-dev设备索引 */
int hinic3_get_port_index_by_dev(struct rte_eth_dev *dev, uint16_t *port_index);

/* 获取upcall、reinject队列内堆积的报文数量 */
int hinic3_port_get_tx_queue_count(uint16_t port_id, uint16_t queue_id);
int hinic3_port_get_rx_queue_count(uint16_t port_id, uint16_t queue_id);
#endif /* HINIC3_PORT_UTIL_H */
