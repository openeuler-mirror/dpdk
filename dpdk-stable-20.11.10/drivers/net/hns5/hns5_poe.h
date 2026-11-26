/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026-2027 Huawei Technologies Co., Ltd
 */

#ifndef _HNS5_POE_H_
#define _HNS5_POE_H_

#include <stdint.h>
#include <stdbool.h>

#include <rte_ethdev.h>

#include "ndrv_poe_pf_api.h"
#include "ndrv_poe_vf_api.h"

#include "hns5_eth.h"

#define HNS5_ETH_GROUP_MAX         50
#define HNS5_POE_ORDER_TMOUT_TIME  3000
#define HNS5_POE_CPU_GROUP_MAX_NUM 24
#define HNS5_POE_ACC_GROUP_MAX_NUM 18
#define HNS5_POE_BP_GROUP_MAX_NUM  16

#define HNS5_POE_CPU_GRP_QUE_NUM   8
#define HNS5_POE_TH_TPMLTE_ID      0

#define HNS5_POE_BP_TH_MAX         0xFFFFFFU   // bp watermark upper bound (24bit field)
#define HNS5_POE_BP_SLOT_INVALID   0xFFU       // priv->bp_grp holds no slot

int hns5_eth_poe_init(struct rte_eth_dev *dev);
/**
 * @brief Init poe HW groups and their queues, and bind vcpu during RX queue setup
 * @attention Not concurrent-safe but reentrant; no duplicate init internally
*/
int hns5_eth_rx_queue_setup_poe(struct rte_eth_dev *dev, uint32_t group, uint16_t depth);

/** @brief Enable/disable the POE queue input of one RX queue
 *  @param rxq RX queue; enable true-enable, false-disable
 *  @return 0 on success, negative on failure */
int hns5_poe_rxq_enable(struct hns5_eth_rx_queue *rxq, bool enable);

/** @brief Apply this queue's equal share of the POE group bp budget
 *  @param dev ethdev; rxq RX queue */
void hns5_poe_rxq_bp_quota_apply(struct rte_eth_dev *dev, struct hns5_eth_rx_queue *rxq);

/** @brief Toggle forwarding on the fw_tnl of one RX queue
 *  @param dev ethdev; rxq RX queue; drop true-discard all, false-restore share
 *  @note Used while a queue is stopped so flow-table hits are not left queued. */
void hns5_poe_rxq_fw_tnl_drop_set(struct rte_eth_dev *dev,
    struct hns5_eth_rx_queue *rxq, bool drop);

/* per-queue RX watermark: claim/release the poe->ppe bp slot around dev life cycle */
int hns5_poe_bp_grp_claim(struct rte_eth_dev *dev);
int hns5_poe_bp_grp_release(struct rte_eth_dev *dev);

#endif /* _HNS5_POE_H_ */
