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
    /* Forwarded directly through firmware without creating actual queues. 
        we use "rxq == 0" to detemine whether rxq is hairpin queue. */
    (void) dev;
    (void) qid;
    (void) nb_desc;
    (void) conf;
    return 0;
}

int
hinic3_tx_hairpin_queue_setup(struct rte_eth_dev *dev, uint16_t qid, uint16_t nb_desc, const struct rte_eth_hairpin_conf *conf)
{
    /* Forwarded directly through firmware without creating actual queues.
        we use "txq == 0" to detemine whether txq is hairpin queue. */
    (void) dev;
    (void) qid;
    (void) nb_desc;
    (void) conf;
    return 0;
}