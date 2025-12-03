#ifndef _HINIC3_PMD_HAIRPIN_H
#define _HINIC3_PMD_HAIRPIN_H
#include <rte_ethdev.h>

int hinic3_hairpin_cap_get(struct rte_eth_dev *dev, struct rte_eth_hairpin_cap *cap);

int hinic3_rx_hairpin_queue_setup(struct rte_eth_dev *dev, uint16_t qid, uint16_t nb_desc, const struct rte_eth_hairpin_conf *conf);

int hinic3_tx_hairpin_queue_setup(struct rte_eth_dev *dev, uint16_t qid, uint16_t nb_desc, const struct rte_eth_hairpin_conf *conf);

#endif /* _HINIC3_PMD_HAIRPIN_H_ */
