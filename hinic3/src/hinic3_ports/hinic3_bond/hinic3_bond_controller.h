/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2025 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_BOND_CONTROLLER_H
#define HINIC3_BOND_CONTROLLER_H

#include <ethdev_driver.h>
#include "hinic3_queue.h"
#include "hinic3_age_delete_flow.h"

#define HINIC3_BOND_DEFAULT_NB_QUEUES  1
#define HINIC3_BOND_MTU_MAX            9058
#define HINIC3_BOND_MTU_MIN            1500
#define HINIC3_BOND_DEFAULT_MTU        HINIC3_BOND_MTU_MAX
#define HINIC3_BOND_SIZE_OFFSET        96   /* vxlan头(74) + 内层eth头(14) + 内层vlan(4) + FCS(4) */

#define HINIC3_BOND_SLAVE_NUM                 4
#define HINIC3_BOND_ARG_SLAVE_NAME_LEN        64
#define HINIC3_BOND_ARG_SLAVE_PCI_LEN         14
#define HINIC3_BOND_SLAVE_NAME_LEN            256

#define MAX_NAME                    32
#define HW_PTR_ADD(ptr, x) ((void *)((uintptr_t)(ptr) + (x)))

#define HINIC3_PORTS_MEM_NAME       "hinic3_ports"



/* 此处将bond口及VF端口私有数据的首个uint16数据作为端口的vport_id，注意不要更换vport_id的位置 */
struct hinic3_bond_dev {
    uint16_t vport_id;
    uint16_t bond_id;
    char bond_name[MAX_NAME];
    uint16_t port_ifindex;
    struct rte_pci_addr uplink_pci_addr;
    uint16_t dpdk_port_id;
    uint32_t mtu;
    struct hinic3_upcall_queue upcall_queue;
    struct hinic3_reinject_queue reinject_queue;
    uint8_t n_upcall_queue;
    uint8_t n_txq;
    rte_atomic64_t bond_upcall_pk_num;
    rte_atomic64_t bond_upcall_pk_byt;
    rte_atomic64_t bond_reinject_pk_num;
    rte_atomic64_t bond_reinject_pk_byt;
    bool priority_upcall;
    struct rte_aged_flow_list aged_flow_list;
};

static inline struct hinic3_bond_dev *
hinic3_ethdev_get_bond_private(struct rte_eth_dev *dev)
{
    return (struct hinic3_bond_dev *)dev->data->dev_private;
}

typedef struct hinic3_port_stats_ hinic3_port_stats;

/* bond控制器类初始化 */
void hinic3_bond_flow_ops_construct(void);
/* bond控制器类逆初始化 */
void hinic3_bond_flow_ops_destroy(void);
/* 获取设备信息 */
int hinic3_bond_dev_infos_get(struct rte_eth_dev *dev, struct rte_eth_dev_info *info);
/* 对设备进行同步设置 */
int hinic3_bond_dev_configure(struct rte_eth_dev *dev);
/* 设备启动 */
int hinic3_bond_dev_start(struct rte_eth_dev *dev);
/* 设备停止 */
int hinic3_bond_dev_stop(struct rte_eth_dev *dev);
/* 设置设备的连接状态为down */
int hinic3_bond_set_link_down(struct rte_eth_dev *dev);
/* 设置设备的连接状态为up */
int hinic3_bond_set_link_up(struct rte_eth_dev *dev);
/* 设备退出 */
int hinic3_bond_dev_close(struct rte_eth_dev *dev);
/* 更新设备连接状态 */
int hinic3_bond_link_update(struct rte_eth_dev *dev, int wait_to_complete);
/* 设置设备的mtu */
int hinic3_bond_dev_set_mtu(struct rte_eth_dev *dev, uint16_t mtu);

/* 释放设备的rx队列 */
void hinic3_bond_rx_queue_release(struct rte_eth_dev *dev, uint16_t queue_id);
/* 释放设备的tx队列 */
void hinic3_bond_tx_queue_release(struct rte_eth_dev *dev, uint16_t queue_id);

/* 设置设备的rx队列 */
int hinic3_bond_rx_queue_setup(struct rte_eth_dev *dev, uint16_t idx, uint16_t desc,
    unsigned int socket_id, const struct rte_eth_rxconf *conf, struct rte_mempool *mp);
/* 设置设备的tx队列 */
int hinic3_bond_tx_queue_setup(struct rte_eth_dev *dev, uint16_t idx, uint16_t desc,
    unsigned int socket_id, const struct rte_eth_txconf *conf);
/* rte_flow接口 */
int hinic3_bond_dev_flow_ops_get(struct rte_eth_dev *dev __rte_unused, const struct rte_flow_ops **ops);
/* 获取bond口 link状态 */
int hinic3_get_bond_link_status(const char *bond_name, uint32_t *status);
#endif /* HINIC3_BOND_CONTROLLER_H */
