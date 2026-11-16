/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026-2027 Huawei Technologies Co., Ltd
 */

#include <rte_errno.h>

#include "hns5_poe.h"
#include "hns5_eth.h"
#include "hns5_log.h"
#include "hns5_hairpin.h"

#define HNS5_HAIRPIN_MAX_NB_QUEUES 2

/**
 * DPDK callback to retrieve hairpin capabilities.
 *
 * Hairpin queues consume no hardware queue resource, but the ethdev framework
 * manages them as slots of dev->data->rx_queues[] (queue id must be less than
 * nb_rx_queues, which never exceeds max_rx_queues = HNS5_POE_CPU_GRP_QUE_NUM),
 * shared with normal queues. So the per-port hairpin queue budget is
 * HNS5_POE_CPU_GRP_QUE_NUM.
 *
 * @param dev
 *   Pointer to Ethernet device structure.
 * @param[out] cap
 *   Storage for hairpin capability data.
 *
 * @return
 *   0 on success, a negative errno value otherwise and rte_errno is set.
 */
int hns5_eth_hairpin_cap_get(struct rte_eth_dev *dev __rte_unused, struct rte_eth_hairpin_cap *cap)
{
	cap->max_nb_queues = HNS5_HAIRPIN_MAX_NB_QUEUES;
	cap->max_rx_2_tx = 1;
	cap->max_tx_2_rx = 1;
	cap->max_nb_desc = UINT16_MAX;
	return 0;
}

/**
 * DPDK callback to configure a RX hairpin queue.
 *
 * @param dev
 *   Pointer to Ethernet device structure.
 * @param rx_queue_id
 *   RX queue index.
 * @param nb_rx_desc
 *   Number of descriptors to configure in queue. Hairpin queues own no
 *   hardware ring, so the depth is not used.
 * @param conf
 *   Hairpin configuration parameters.
 *
 * @return
 *   0 on success, a negative errno value otherwise and rte_errno is set.
 */
int hns5_eth_rx_hairpin_queue_setup(struct rte_eth_dev *dev, uint16_t rx_queue_id,
				uint16_t nb_rx_desc __rte_unused,
				const struct rte_eth_hairpin_conf *conf)
{
	struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);
	struct hns5_eth_rx_queue *rxq = NULL;
	uint16_t peer_port = 0;

	if (conf->peer_count != 1) {
		hns5_err(dev,
			 "port %u unable to setup Rx hairpin queue index %u"
			 " peer count is %u",
			 dev->data->port_id, rx_queue_id, conf->peer_count);
		rte_errno = EINVAL;
		return -rte_errno;
	}

	peer_port = conf->peers[0].port;
	if (!hns5_eth_dev_is_hns5(peer_port)) {
		hns5_err(dev, "port:%u Rx hairpin queue:%u peer port:%u is not a hns5 port\n",
			 dev->data->port_id, rx_queue_id, peer_port);
		rte_errno = EINVAL;
		return -rte_errno;
	}

	if (peer_port == dev->data->port_id) {
		if (conf->peers[0].queue >= dev->data->nb_tx_queues) {
			hns5_err(dev,
				 "port:%u Rx hairpin queue:%u Tx peer queue:%u exceeds "
				 "nb_tx_queues:%u\n",
				 dev->data->port_id, rx_queue_id, conf->peers[0].queue,
				 dev->data->nb_tx_queues);
			rte_errno = EINVAL;
			return -rte_errno;
		}
	} else if (conf->manual_bind == 0 || conf->tx_explicit == 0) {
		/*
			* Cross-port hairpin needs manual bind, and the user is
			* responsible for inserting the Tx part flows (rte_flow
			* PORT_ID/QUEUE actions), i.e. tx_explicit must be set.
			*/
		hns5_err(dev,
			 "port:%u Rx hairpin queue:%u peer port:%u needs manual_bind and "
			 "tx_explicit\n",
			 dev->data->port_id, rx_queue_id, peer_port);
		rte_errno = EINVAL;
		return -rte_errno;
	}

	rxq = priv->rxq + rx_queue_id;
	rxq->port_id = dev->data->port_id;
	rxq->is_hairpin = true;
	rxq->hairpin_conf = *conf;
	dev->data->rx_queues[rx_queue_id] = rxq;

	hns5_dbg(dev, "Rx hairpin queue:%u peer(tx) port:%u queue:%u\n", rx_queue_id,
		 conf->peers[0].port, conf->peers[0].queue);

	return 0;
}

/**
 * DPDK callback to configure a TX hairpin queue.
 *
 * @param dev
 *   Pointer to Ethernet device structure.
 * @param tx_queue_id
 *   TX queue index.
 * @param nb_tx_desc
 *   Number of descriptors to configure in queue. Hairpin queues own no
 *   hardware ring, so the depth is not used.
 * @param conf
 *   Hairpin configuration parameters.
 *
 * @return
 *   0 on success, a negative errno value otherwise and rte_errno is set.
 */
int hns5_eth_tx_hairpin_queue_setup(struct rte_eth_dev *dev, uint16_t tx_queue_id,
				uint16_t nb_tx_desc __rte_unused,
				const struct rte_eth_hairpin_conf *conf)
{
	struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);
	struct hns5_eth_tx_queue *txq = NULL;
	uint16_t peer_port = 0;

	if (conf->peer_count != 1) {
		hns5_err(dev,
			 "port %u unable to setup Tx hairpin queue index %u"
			 " peer count is %u",
			 dev->data->port_id, tx_queue_id, conf->peer_count);
		rte_errno = EINVAL;
		return -rte_errno;
	}

	peer_port = conf->peers[0].port;
	if (!hns5_eth_dev_is_hns5(peer_port)) {
		hns5_err(dev, "port:%u Tx hairpin queue:%u peer port:%u is not a hns5 port\n",
			 dev->data->port_id, tx_queue_id, peer_port);
		rte_errno = EINVAL;
		return -rte_errno;
	}

	if (peer_port == dev->data->port_id) {
		if (conf->peers[0].queue >= dev->data->nb_rx_queues) {
			hns5_err(dev,
				 "port:%u Tx hairpin queue:%u Rx peer queue:%u exceeds "
				 "nb_rx_queues:%u\n",
				 dev->data->port_id, tx_queue_id, conf->peers[0].queue,
				 dev->data->nb_rx_queues);
			rte_errno = EINVAL;
			return -rte_errno;
		}
	} else if (conf->manual_bind == 0 || conf->tx_explicit == 0) {
		hns5_err(dev,
			 "port:%u Tx hairpin queue:%u peer port:%u needs manual_bind and "
			 "tx_explicit\n",
			 dev->data->port_id, tx_queue_id, peer_port);
		rte_errno = EINVAL;
		return -rte_errno;
	}

	txq = priv->txq + tx_queue_id;
	txq->port_id = dev->data->port_id;
	txq->priv = priv;
	txq->is_hairpin = true;
	txq->hairpin_conf = *conf;
	dev->data->tx_queues[tx_queue_id] = txq;

	hns5_dbg(dev, "Tx hairpin queue:%u peer(rx) port:%u queue:%u\n", tx_queue_id,
		 conf->peers[0].port, conf->peers[0].queue);

	return 0;
}

/*
 * DPDK callback to get the hairpin peer ports list.
 * This will return the actual number of peer ports and save the identifiers
 * into the array (sorted, may be different from that when setting up the
 * hairpin peer queues).
 * The peer port ID could be the same as the port ID of the current device.
 *
 * @param dev
 *   Pointer to Ethernet device structure.
 * @param peer_ports
 *   Pointer to array to save the port identifiers.
 * @param len
 *   The length of the array.
 * @param direction
 *   Current port to peer port direction.
 *   positive - current used as Tx to get all peer Rx ports.
 *   zero - current used as Rx to get all peer Tx ports.
 *
 * @return
 *   0 or positive value on success, actual number of peer ports.
 *   a negative errno value otherwise and rte_errno is set.
 */
int hns5_eth_hairpin_get_peer_ports(struct rte_eth_dev *dev, uint16_t *peer_ports, size_t len,
				uint32_t direction)
{
	struct rte_eth_dev_data *data = dev->data;
	struct hns5_eth_rx_queue **rxq = (struct hns5_eth_rx_queue **)data->rx_queues;
	struct hns5_eth_tx_queue **txq = (struct hns5_eth_tx_queue **)data->tx_queues;
	uint16_t rxq_num = data->nb_rx_queues;
	uint16_t txq_num = data->nb_tx_queues;
	uint16_t i, peer_cnt = 0;

	if (direction) {
		/* current port used as Tx: list peer Rx ports of hairpin Tx queues */
		for (i = 0; i < txq_num; i++) {
			if (txq[i] == NULL || !txq[i]->is_hairpin)
				continue;

			if (peer_cnt >= len) {
				rte_errno = ERANGE;
				hns5_err(dev, "port %u queue %u peer port out of range %lu\n",
					 data->port_id, i, len);
				return -rte_errno;
			}
			peer_ports[peer_cnt++] = txq[i]->hairpin_conf.peers[0].port;
		}
	} else {
		/* current port used as Rx: list peer Tx ports of hairpin Rx queues */
		for (i = 0; i < rxq_num; i++) {
			if (rxq[i] == NULL || !rxq[i]->is_hairpin)
				continue;

			if (peer_cnt >= len) {
				rte_errno = ERANGE;
				hns5_err(dev, "port %u queue %u peer port out of range %lu\n",
					 data->port_id, i, len);
				return -rte_errno;
			}
			peer_ports[peer_cnt++] = rxq[i]->hairpin_conf.peers[0].port;
		}
	}

	return (int)peer_cnt;
}

/*
 * 空实现占位：本PMD的跨口转发由 rte_flow(PORT_ID/QUEUE动作)直接下发硬件重定向表项，
 * bind/unbind本身无硬件动作。必须注册，否则ethdev公共层(RTE_FUNC_PTR_OR_ERR_RET)会返回
 * ENOTSUP，导致上层认为不支持hairpin绑定而中断跨口转发流程。
 */
int hns5_eth_hairpin_bind(struct rte_eth_dev *dev __rte_unused, uint16_t rx_port __rte_unused)
{
	return 0;
}

int hns5_eth_hairpin_unbind(struct rte_eth_dev *dev __rte_unused, uint16_t rx_port __rte_unused)
{
	return 0;
}
