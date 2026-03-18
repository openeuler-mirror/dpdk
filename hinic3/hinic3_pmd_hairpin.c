#include "base/hinic3_compat.h"
#include "base/hinic3_pmd_mgmt.h"

#include "hinic3_pmd_ethdev.h"
#include "hinic3_pmd_rx.h"
#include "hinic3_pmd_tx.h"
#include "hinic3_pmd_hairpin.h"

/**
 * DPDK callback to retrieve hairpin capabilities.
 *
 * @param dev
 *   Pointer to Ethernet device structure.
 * @param[out] cap
 *   Storage for hairpin capability data.
 *
 * @return
 *   0 on success, a negative errno value otherwise and rte_errno is set.
 */
int
hinic3_hairpin_cap_get(struct rte_eth_dev *dev, struct rte_eth_hairpin_cap *cap)
{
	struct hinic3_nic_dev *nic_dev;
    nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	if (!(nic_dev->feature_cap & NIC_F_HAIRPIN)) {
		PMD_DRV_LOG(ERR, "current firmware not support hairpin");
		rte_errno = ENOTSUP;
		return -rte_errno;
	}
    cap->max_nb_queues = UINT16_MAX;
    cap->max_rx_2_tx = 1;
    cap->max_tx_2_rx = 1;
    cap->max_nb_desc = HINIC3_MAX_QUEUE_DEPTH;
#ifdef DPDK_22_11
	/*not support yet*/
	cap->rx_cap.locked_device_memory = 0;
	cap->rx_cap.rte_memory = 0;
	cap->tx_cap.locked_device_memory = 0;
	/*not support yet*/
	cap->tx_cap.rte_memory = 0;
#endif
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
int
hinic3_hairpin_get_peer_ports(struct rte_eth_dev *dev, uint16_t *peer_ports,
							  size_t len, uint32_t direction)
{
	struct rte_eth_dev_data *data = dev->data;
	struct hinic3_rxq **rxq = (struct hinic3_rxq **)data->rx_queues;
	struct hinic3_txq **txq = (struct hinic3_txq **)data->tx_queues;
	uint16_t rxq_num = data->nb_rx_queues;
	uint16_t txq_num = data->nb_tx_queues;
	uint16_t i, peer_cnt = 0;

	if (direction) {
		for (i = 0; i < txq_num; i++) {
			if (txq[i] == NULL || !txq[i]->is_hairpin || txq[i]->hairpin_conf.peer_count == 0)
				continue;

			if (peer_cnt >= len) {
				rte_errno = ERANGE;
				PMD_DRV_LOG(ERR, "port %u queue %u peer port out of range %lu",
					data->port_id, i, len);
				return -rte_errno;
			}
			peer_ports[peer_cnt++] = txq[i]->hairpin_conf.peers[0].port;	
		}
	} else {
		for (i = 0; i < rxq_num; i++) {
			if (rxq[i] == NULL || !rxq[i]->is_hairpin || rxq[i]->hairpin_conf.peer_count == 0)
				continue;

			if (peer_cnt >= len) {
				rte_errno = ERANGE;
				PMD_DRV_LOG(ERR, "port %u queue %u peer port out of range %lu",
					data->port_id, i, len);
				return -rte_errno;
			}
			peer_ports[peer_cnt++] = rxq[i]->hairpin_conf.peers[0].port;
		}
	}
	return peer_cnt;
}

/**
 *
 * @param dev
 *   Pointer to Ethernet device structure.
 * @param idx
 *   RX queue index.
 * @param desc
 *   Number of descriptors to configure in queue.
 * @param hairpin_conf
 *   Hairpin configuration parameters.
 *
 * @return
 *   0 on success, a negative errno value otherwise and rte_errno is set.
 */
int
hinic3_rx_hairpin_queue_setup(struct rte_eth_dev *dev, uint16_t qid,
							  uint16_t nb_desc,
							  const struct rte_eth_hairpin_conf *conf)
{
    struct hinic3_rxq *rxq = NULL;
    struct hinic3_nic_dev *nic_dev;
	u16 rq_depth;

    nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);

    /* Queue depth must be power of 2, otherwise will be aligned up */
	rq_depth = (nb_desc & (nb_desc - 1)) ?
		((u16)(1U << (ilog2(nb_desc) + 1))) : nb_desc;

	if (rq_depth > HINIC3_MAX_QUEUE_DEPTH ||
		rq_depth < HINIC3_MIN_QUEUE_DEPTH) {
		PMD_DRV_LOG(ERR, "RX queue depth is out of range from %d to %d,"
			    "(nb_desc: %d, q_depth: %d, port: %d queue: %d)",
			    HINIC3_MIN_QUEUE_DEPTH, HINIC3_MAX_QUEUE_DEPTH,
			    (int)nb_desc, (int)rq_depth,
			    (int)dev->data->port_id, (int)qid);
		return -EINVAL;
	}

    if (conf->peer_count > 1) {
		rte_errno = EINVAL;
		PMD_DRV_LOG(ERR, "port %u unable to setup Rx hairpin queue index %u"
			" peer count is %u", dev->data->port_id,
			qid, conf->peer_count);
		return -rte_errno;
	}
	if (conf->peers[0].port == dev->data->port_id) {
		if (conf->peers[0].queue >= dev->data->nb_tx_queues) {
			rte_errno = EINVAL;
			PMD_DRV_LOG(ERR, "port %u unable to setup Rx hairpin queue"
				" index %u, Tx %u is larger than %u",
				dev->data->port_id, qid,
				conf->peers[0].queue, dev->data->nb_tx_queues);
			return -rte_errno;
		}
#ifdef DPDK_20_11
	} else {
		if (conf->manual_bind == 0 ||
		    conf->tx_explicit == 0) {
			rte_errno = EINVAL;
			PMD_DRV_LOG(ERR, "port %u unable to setup Rx hairpin queue"
				" index %u peer port %u with attributes %u %u",
				dev->data->port_id, qid,
				conf->peers[0].port,
				conf->manual_bind,
				conf->tx_explicit);
			return -rte_errno;
		}
#endif
	}
    rxq = rte_zmalloc_socket("hinic3_rq", sizeof(struct hinic3_rxq),
				 RTE_CACHE_LINE_SIZE, SOCKET_ID_ANY);
    if (!rxq) {
		PMD_DRV_LOG(ERR, "Allocate rxq[%d] failed, dev_name: %s",
			    qid, dev->data->name);
		return -ENOMEM;
	}
    rxq->nic_dev = nic_dev;
    nic_dev->rxqs[qid] = rxq;
    rxq->q_id = qid;
	rxq->q_depth = rq_depth;
	rxq->hairpin_conf = *conf;
    rxq->is_hairpin = true;

    dev->data->rx_queues[qid] = rxq;
	dev->data->rx_queue_state[qid] = RTE_ETH_QUEUE_STATE_HAIRPIN;

    return 0;
}

/**
 * DPDK callback to configure a TX hairpin queue.
 *
 * @param dev
 *   Pointer to Ethernet device structure.
 * @param idx
 *   TX queue index.
 * @param desc
 *   Number of descriptors to configure in queue.
 * @param[in] hairpin_conf
 *   The hairpin binding configuration.
 *
 * @return
 *   0 on success, a negative errno value otherwise and rte_errno is set.
 */
int
hinic3_tx_hairpin_queue_setup(struct rte_eth_dev *dev, uint16_t qid,
							  uint16_t nb_desc,
							  const struct rte_eth_hairpin_conf *conf)
{
    struct hinic3_txq *txq = NULL;
    struct hinic3_nic_dev *nic_dev;
    u16 sq_depth;

    nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);

    /* Queue depth must be power of 2, otherwise will be aligned up */
	sq_depth = (nb_desc & (nb_desc - 1)) ?
		   ((u16)(1U << (ilog2(nb_desc) + 1))) : nb_desc;

    /*
	 * Validate number of transmit descriptors.
	 * It must not exceed hardware maximum and minimum.
	 */
	if (sq_depth > HINIC3_MAX_QUEUE_DEPTH ||
		sq_depth < HINIC3_MIN_QUEUE_DEPTH) {
		PMD_DRV_LOG(ERR, "TX queue depth is out of range from %d to %d,"
			    "(nb_desc: %d, q_depth: %d, port: %d queue: %d)",
			    HINIC3_MIN_QUEUE_DEPTH, HINIC3_MAX_QUEUE_DEPTH,
			    (int)nb_desc, (int)sq_depth,
			    (int)dev->data->port_id, (int)qid);
		return -EINVAL;
	}
    if (conf->peer_count > 1) {
		rte_errno = EINVAL;
		PMD_DRV_LOG(ERR, "port %u unable to setup Tx hairpin queue index %u"
			" peer count is %u", dev->data->port_id,
			qid, conf->peer_count);
		return -rte_errno;
	}
	if (conf->peers[0].port == dev->data->port_id) {
		if (conf->peers[0].queue >= dev->data->nb_rx_queues) {
			rte_errno = EINVAL;
			PMD_DRV_LOG(ERR, "port %u unable to setup Tx hairpin queue"
				" index %u, Rx %u is larger than %u",
				dev->data->port_id, qid,
				conf->peers[0].queue, dev->data->nb_rx_queues);
			return -rte_errno;
		}
#ifdef DPDK_20_11
	} else {
		if (conf->manual_bind == 0 ||
		    conf->tx_explicit == 0) {
			rte_errno = EINVAL;
			PMD_DRV_LOG(ERR, "port %u unable to setup Tx hairpin queue"
				" index %u peer port %u with attributes %u %u",
				dev->data->port_id, qid,
				conf->peers[0].port,
				conf->manual_bind,
				conf->tx_explicit);
			return -rte_errno;
		}
#endif
	}
    txq = rte_zmalloc_socket("hinic3_tq", sizeof(struct hinic3_txq),
                RTE_CACHE_LINE_SIZE, SOCKET_ID_ANY);
    if (!txq) {
		PMD_DRV_LOG(ERR, "Allocate txq[%d] failed, dev_name: %s",
			    qid, dev->data->name);
		return -ENOMEM;
	}
    nic_dev->txqs[qid] = txq;
    txq->nic_dev = nic_dev;
	txq->q_id = qid;
	txq->q_depth = sq_depth;
	txq->hairpin_conf = *conf;
    txq->is_hairpin = true;

    dev->data->tx_queues[qid] = txq;
	dev->data->tx_queue_state[qid] = RTE_ETH_QUEUE_STATE_HAIRPIN;
    return 0;
}

int hinic3_hairpin_bind(struct rte_eth_dev *dev, uint16_t rx_port)
{
	(void)dev;
	(void)rx_port;
	return 0;
}

int hinic3_hairpin_unbind(struct rte_eth_dev *dev, uint16_t rx_port)
{
	(void)dev;
	(void)rx_port;
	return 0;
}