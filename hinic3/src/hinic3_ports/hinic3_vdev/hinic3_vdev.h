/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2025 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_VDEV_H
#define HINIC3_VDEV_H

#include <ethdev_vdev.h>
#include "rte_kvargs.h"

/* 端口有效参数列表 */
const char * const *hinic3_vdev_valid_arguments_get(void);
/* vf/pf端口收包接口 */
uint16_t hinic3_vf_recv_pkts(void *rx_queue, struct rte_mbuf **rx_pkts, uint16_t nb_pkts);
uint16_t hinic3_vf_recv_pkts_in_virtual_queue(void *virtual_rx_queue, struct rte_mbuf **rx_pkts, uint16_t nb_pkts);
/* vf/pf端口发包接口 */
uint16_t hinic3_vf_xmit_pkts(void *tx_queue, struct rte_mbuf **tx_pkts, uint16_t nb_pkts);
/* bond端口收包接口 */
uint16_t hinic3_bond_recv_pkts(void *rx_queue, struct rte_mbuf **rx_pkts, uint16_t nb_pkts);
/* bond端口发包接口 */
uint16_t hinic3_bond_xmit_pkts(void *tx_queue, struct rte_mbuf **tx_pkts, uint16_t nb_pkts);

/* vf/pf端口初始化 */
int hinic3_vf_vdev_init(struct rte_eth_dev *eth_dev, struct rte_kvargs *kvlist);
/* bond端口初始化 */
int hinic3_bond_vdev_init(struct rte_eth_dev *eth_dev, struct rte_kvargs *kvlist);
/* vf/pf端口逆初始化 */
void hinic3_vf_vdev_uninit(struct rte_eth_dev *dev);
/* bond端口逆初始化 */
void hinic3_bond_vdev_uninit(struct rte_eth_dev *dev);

/* 端口模块初始化 */
int hinic3_vdev_port_module_init(void);
/* 端口模块逆初始化 */
void hinic3_vdev_port_module_uninit(void);
#endif /* HINIC3_VDEV_H */
