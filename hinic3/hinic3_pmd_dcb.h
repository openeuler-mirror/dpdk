/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2025 Huawei Technologies Co., Ltd
 */

#ifndef _HINIC3_PMD_DCB_H_
#define _HINIC3_PMD_DCB_H_

#include "hinic3_pmd_ethdev.h"

#define HINIC3_ETHER_MAX_RATE 100 /* 100G */
#define DBG_DFLT_DSCP_VAL 0xFF
#define NIC_DCB_PRIO_DWRR   0x0
#define NIC_DCB_PRIO_STRICT 0x1
#define NIC_DCB_MAX_PFC_NUM 0x4
#define NIC_ETS_PERCENT_WEIGHT 100
#define HINIC3_DCB_PCP  0
#define HINIC3_DCB_DSCP 1
#define MAX_TX_QUEUE_NUM 64

struct hinic3_dcb {
	u8 dcb_on;
	u8 rsvd0;
	u8 cos_config_num_max;
	u8 func_dft_cos_bitmap;
	u16 port_dft_cos_bitmap; /* used to tool validity check */
	u8 txq_cos[HINIC3_MAX_QUEUE_NUM];
	struct hinic3_dcb_config hw_dcb_cfg;
	struct hinic3_dcb_config wanted_dcb_cfg;
	unsigned long dcb_flags;
};

int hinic3_dcb_init(struct hinic3_nic_dev *nic_dev);
int hinic3_dcb_alloc(struct hinic3_nic_dev *nic_dev);
int hinic3_get_dcb_info(struct rte_eth_dev *dev,
		     struct rte_eth_dcb_info *dcb_info);

u8 hinic3_get_dev_user_cos_num(struct hinic3_nic_dev *nic_dev);
void hinic3_update_qp_cos_cfg(struct hinic3_nic_dev *nic_dev, u8 num_cos);
void hinic3_set_txq_cos(struct hinic3_nic_dev *nic_dev, u16 start_qid, u16 q_num,
		     u8 cos);
void hinic3_update_tx_db_cos(struct hinic3_nic_dev *nic_dev, u8 dcb_en);
u8 hinic3_get_dev_user_cos_num(struct hinic3_nic_dev *nic_dev);
int hinic3_configure_dcb_hw(struct hinic3_nic_dev *nic_dev, u8 dcb_en);
u8 hinic3_get_dev_valid_cos_map(struct hinic3_nic_dev *nic_dev);
int hinic3_dcb_rss_init(struct hinic3_nic_dev *nic_dev, u8 dcb_en);
uint8_t hinic3_txq_mapped_tc_get(struct hinic3_nic_dev *nic_dev, u16 txq_no);
int hinic3_setup_cos(struct hinic3_nic_dev *nic_dev, u8 cos);

#endif /* _HINIC3_PMD_DCB_H_ */
