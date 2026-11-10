/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026-2027 Huawei Technologies Co., Ltd
 */

#ifndef _HNS5_RSS_H_
#define _HNS5_RSS_H_

#include "hns5_eth.h"
#include "hns5_log.h"
#include "ndrv_ppe_ppp_pf_api.h"
#include "ndrv_ppe_lsw_pf_api.h"

/* RSS related constants */
#define HNS5_RSS_RETA_SIZE        1024        /* RSS redirection table size */
#define HNS5_RSS_INDIRECT_TABLE_SIZE 1024    /* RSS indirect table size */
#define HNS5_RSS_RETA_GROUP_SIZE  64          /* RETA entry group size */

/* RSS supported features */
#define HNS5_ETH_RSS_SUPPORT ( \
    ETH_RSS_IPV4 | \
    ETH_RSS_NONFRAG_IPV4_TCP | \
    ETH_RSS_NONFRAG_IPV4_UDP | \
    ETH_RSS_NONFRAG_IPV4_SCTP | \
    ETH_RSS_NONFRAG_IPV4_OTHER | \
    ETH_RSS_FRAG_IPV4 | \
    ETH_RSS_IPV6 | \
    ETH_RSS_NONFRAG_IPV6_TCP | \
    ETH_RSS_NONFRAG_IPV6_UDP | \
    ETH_RSS_NONFRAG_IPV6_SCTP | \
    ETH_RSS_NONFRAG_IPV6_OTHER | \
    ETH_RSS_FRAG_IPV6 | \
    ETH_RSS_L2_PAYLOAD)

/* Default RSS key */
extern const uint8_t hns5_default_rss_key[HNS5_RSS_KEY_SIZE];

/* RSS function declarations */
void hns5_rss_set_default_args(struct hns5_eth_priv *priv);
void hns5_rss_uninit(struct rte_eth_dev *dev);
int hns5_rss_apply_conf(struct rte_eth_dev *dev);
int hns5_config_rss(struct rte_eth_dev *dev);
int hns5_rss_reta_update(struct rte_eth_dev *dev, struct rte_eth_rss_reta_entry64 *reta_conf, uint16_t reta_size);
int hns5_rss_reta_query(struct rte_eth_dev *dev, struct rte_eth_rss_reta_entry64 *reta_conf, uint16_t reta_size);
int hns5_rss_hash_update(struct rte_eth_dev *dev, struct rte_eth_rss_conf *rss_conf);
int hns5_rss_hash_conf_get(struct rte_eth_dev *dev, struct rte_eth_rss_conf *rss_conf);

#endif /* _HNS5_RSS_H_ */
