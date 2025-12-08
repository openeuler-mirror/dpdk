#include "hinic3_pmd_hairpin.h"
#include "hinic3_compat.h"
#include "hinic3_pmd_mgmt.h"
#include "hinic3_pmd_ethdev.h"
#include "hinic3_pmd_rx.h"
#include "hinic3_pmd_tx.h"
#include "hinic3_pmd_hwdev.h"
#include "hinic3_pmd_hw_comm.h"
#include "hinic3_pmd_hwif.h"
#include "hinic3_pmd_dcb.h"

int
hinic3_hairpin_cap_get(struct rte_eth_dev *dev, struct rte_eth_hairpin_cap *cap)
{
    (void) dev;
    cap->max_nb_queues = UINT16_MAX;
    cap->max_rx_2_tx = 1;
    cap->max_tx_2_rx = 1;
    cap->max_nb_desc = 8192;
    return 0;
}

int
hinic3_rx_hairpin_queue_setup(struct rte_eth_dev *dev, uint16_t qid, uint16_t nb_desc, const struct rte_eth_hairpin_conf *conf)
{
    struct hinic3_rxq *rxq = NULL;
    struct hinic3_nic_dev *nic_dev;
    nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
    u16 rq_depth;
    
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

    if (conf->peer_count != 1) {
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
    rxq->is_hairpin = true;

    dev->data->rx_queues[qid] = rxq;
    return 0;
}

int
hinic3_tx_hairpin_queue_setup(struct rte_eth_dev *dev, uint16_t qid, uint16_t nb_desc, const struct rte_eth_hairpin_conf *conf)
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
    if (conf->peer_count != 1) {
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
    txq->is_hairpin = true;

    dev->data->tx_queues[qid] = txq;
    return 0;
}