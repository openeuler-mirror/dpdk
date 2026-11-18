/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026-2027 Huawei Technologies Co., Ltd
 */

#ifndef _HNS5_HAIRPIN_H_
#define _HNS5_HAIRPIN_H_

#include <rte_ethdev.h>

int hns5_eth_hairpin_cap_get(struct rte_eth_dev *dev, struct rte_eth_hairpin_cap *cap);

int hns5_eth_rx_hairpin_queue_setup(struct rte_eth_dev *dev, uint16_t rx_queue_id, uint16_t nb_rx_desc,
				const struct rte_eth_hairpin_conf *conf);

int hns5_eth_tx_hairpin_queue_setup(struct rte_eth_dev *dev, uint16_t tx_queue_id, uint16_t nb_tx_desc,
				const struct rte_eth_hairpin_conf *conf);

int hns5_eth_hairpin_get_peer_ports(struct rte_eth_dev *dev, uint16_t *peer_ports, size_t len,
				uint32_t direction);

int hns5_eth_hairpin_bind(struct rte_eth_dev *dev, uint16_t rx_port);

int hns5_eth_hairpin_unbind(struct rte_eth_dev *dev, uint16_t rx_port);

#endif
