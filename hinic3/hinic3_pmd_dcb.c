/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2025 Huawei Technologies Co., Ltd
 */
#include "base/hinic3_compat.h"
#include "base/hinic3_pmd_cmd.h"
#include "base/hinic3_pmd_hw_cfg.h"
#include "base/hinic3_pmd_hwdev.h"
#include "base/hinic3_pmd_hwif.h"
#include "base/hinic3_pmd_nic_cfg.h"
#include "hinic3_pmd_dcb.h"

uint8_t
hinic3_txq_mapped_tc_get(struct hinic3_nic_dev *nic_dev, uint16_t txq_no)
{
	struct hinic3_ets *ets = nic_dev->ets;
	struct hinic3_tc_queue_info *tc_queue;
	uint8_t i;

	for (i = 0; i < HINIC3_MAX_TC_NUM; i++) {
		tc_queue = &ets->tc_queue[i];
		if (!tc_queue->enable)
			continue;

		if (txq_no >= tc_queue->tqp_offset &&
		    txq_no < tc_queue->tqp_offset + tc_queue->tqp_count)
			return i;
	}

	/* return TC0 in default case */
	return 0;
}

static int
hinic3_tc_queue_mapping_cfg(struct hinic3_nic_dev *nic_dev, uint16_t nb_tx_q)
{
	struct hinic3_ets *ets = nic_dev->ets;
	struct hinic3_tc_queue_info *tc_queue;
	uint16_t used_tx_queues;
	uint16_t tx_qnum_per_tc;
	uint8_t i;

	tx_qnum_per_tc = nb_tx_q / ets->num_tc;
	used_tx_queues = ets->num_tc * tx_qnum_per_tc;
	if (used_tx_queues != nb_tx_q) {
		PMD_DRV_LOG(ERR,
			    "tx queue number (%u) configured must be an "
			    "integral multiple of valid tc number (%u).",
			    nb_tx_q, ets->num_tc);
		return -EINVAL;
	}

	ets->used_tx_queues = used_tx_queues;
	ets->tx_qnum_per_tc = tx_qnum_per_tc;
	for (i = 0; i < HINIC3_MAX_TC_NUM; i++) {
		tc_queue = &ets->tc_queue[i];
		if (ets->hw_tc_map & BIT(i) && i < ets->num_tc) {
			tc_queue->enable = true;
			tc_queue->tqp_offset = i * ets->tx_qnum_per_tc;
			tc_queue->tqp_count = ets->tx_qnum_per_tc;
			tc_queue->tc = i;
		} else {
			/* Set to default queue if TC is disable */
			tc_queue->enable = false;
			tc_queue->tqp_offset = 0;
			tc_queue->tqp_count = 0;
			tc_queue->tc = 0;
		}
	}

	return 0;
}

static int
hinic3_queue_to_tc_mapping(struct hinic3_nic_dev *nic_dev, uint16_t nb_rx_q,
			uint16_t nb_tx_q)
{
	struct hinic3_ets *ets = nic_dev->ets;

	if (nb_rx_q < ets->num_tc) {
		PMD_DRV_LOG(ERR,
			    "number of Rx queues(%u) is less than number of "
			    "TC(%u).",
			    nb_rx_q, ets->num_tc);
		return -EINVAL;
	}

	if (nb_tx_q < ets->num_tc) {
		PMD_DRV_LOG(ERR,
			    "number of Tx queues(%u) is less than number of "
			    "TC(%u).",
			    nb_tx_q, ets->num_tc);
		return -EINVAL;
	}

	return hinic3_tc_queue_mapping_cfg(nic_dev, nb_tx_q);
}

u8
hinic3_get_dev_valid_cos_map(struct hinic3_nic_dev *nic_dev)
{
	struct hinic3_dcb *dcb = nic_dev->dcb;

	if (dcb->hw_dcb_cfg.trust == HINIC3_DCB_PCP)
		return dcb->hw_dcb_cfg.pcp_valid_cos_map;
	if (dcb->hw_dcb_cfg.trust == HINIC3_DCB_DSCP)
		return dcb->hw_dcb_cfg.dscp_valid_cos_map;
	return 0;
}

int
hinic3_dcb_alloc(struct hinic3_nic_dev *nic_dev)
{
	nic_dev->dcb = rte_zmalloc("dcb", sizeof(struct hinic3_dcb), 0);
	if (!nic_dev->dcb) {
		PMD_DRV_LOG(ERR, "Failed to create dcb.");
		return -EFAULT;
	}

	nic_dev->ets = rte_zmalloc("ets", sizeof(struct hinic3_ets), 0);
	if (!nic_dev->ets) {
		PMD_DRV_LOG(ERR, "Failed to create ets.");
		rte_free(nic_dev->dcb);
		return -EFAULT;
	}

	return 0;
}

static int
hinic3_cos_valid_bitmap(void *hwdev, u8 *func_dft_cos, u8 *port_cos_bitmap)
{
	struct hinic3_hwdev *dev = hwdev;

	if (!dev) {
		PMD_DRV_LOG(
			ERR,
			"Hwdev pointer is NULL for getting cos valid bitmap");
		return 1;
	}
	*func_dft_cos = dev->cfg_mgmt->svc_cap.cos_valid_bitmap;
	*port_cos_bitmap = dev->cfg_mgmt->svc_cap.port_cos_valid_bitmap;
	PMD_DRV_LOG(INFO, "func_dft_cos: %u, port_cos_bitmap: %u",
		    *func_dft_cos, *port_cos_bitmap);

	return 0;
}

static int
init_default_dcb_cfg(struct hinic3_nic_dev *nic_dev,
		     struct hinic3_dcb_config *dcb_cfg)
{
	struct hinic3_dcb *dcb = nic_dev->dcb;
	u8 i, hw_dft_cos_map, port_cos_bitmap, support_cos = 0;
	int err;

	err = hinic3_cos_valid_bitmap(nic_dev->hwdev, &hw_dft_cos_map,
				   &port_cos_bitmap);

	if (err) {
		PMD_DRV_LOG(ERR, "None cos supported");
		return -EFAULT;
	}

	dcb->func_dft_cos_bitmap = hw_dft_cos_map;
	dcb->port_dft_cos_bitmap = port_cos_bitmap;

	for (i = 0; i < NIC_DCB_COS_MAX; i++)
		if (hw_dft_cos_map & BIT(i))
			support_cos++;

	if (HINIC3_FUNC_TYPE(nic_dev->hwdev) == TYPE_VF)
		support_cos = 4;

	dcb->cos_config_num_max = support_cos;

	dcb_cfg->trust = nic_dev->dcb->hw_dcb_cfg.trust;
	dcb_cfg->default_cos = nic_dev->dcb->hw_dcb_cfg.default_cos;

	dcb_cfg->pcp_user_cos_num = dcb->cos_config_num_max;
	dcb_cfg->dscp_user_cos_num = dcb->cos_config_num_max;
	dcb_cfg->pcp_valid_cos_map = hw_dft_cos_map;
	dcb_cfg->dscp_valid_cos_map = hw_dft_cos_map;

	err = hinic3_sync_qos_map(nic_dev->hwdev, dcb_cfg);
	if (err) {
		PMD_DRV_LOG(ERR, "Set qos map failed");
		return err;
	}

	return 0;
}

static int
hinic3_dcb_init_tm(struct hinic3_nic_dev *nic_dev)
{
	struct hinic3_ets *ets = nic_dev->ets;

	ets->max_tm_rate = HINIC3_ETHER_MAX_RATE;
	ets->tqps_num = HINIC3_MAX_QUEUE_NUM;
	ets->hw_tc_map = 0xff;
	int ret;

	uint16_t default_tqp_num;

	/*
	 * The number of queues configured by default cannot exceed
	 * the maximum number of queues for a single TC.
	 */
	default_tqp_num = ets->tqps_num / HINIC3_MAX_TC_NUM;
	ets->num_tc = HINIC3_MAX_TC_NUM;
	ret = hinic3_queue_to_tc_mapping(nic_dev, default_tqp_num,
				      default_tqp_num);
	if (ret) {
		PMD_DRV_LOG(ERR, "update tc queue mapping failed, ret = %d.",
			    ret);
		return ret;
	}

	return 0;
}

int
hinic3_dcb_init(struct hinic3_nic_dev *nic_dev)
{
	int err;

	err = hinic3_dcb_alloc(nic_dev);
	if (err != 0) {
		PMD_DRV_LOG(ERR, "Dcb alloc failed.");
		return err;
	}

	return 0;
}

void
hinic3_update_qp_cos_cfg(struct hinic3_nic_dev *nic_dev, u8 num_cos)
{
	struct hinic3_dcb_config *hw_dcb_cfg = &nic_dev->dcb->hw_dcb_cfg;
	struct hinic3_dcb_config *wanted_dcb_cfg = &nic_dev->dcb->wanted_dcb_cfg;
	u8 i, remainder, num_sq_per_cos, cur_cos_num = 0;
	u8 valid_cos_map = hinic3_get_dev_valid_cos_map(nic_dev);
	if (num_cos == 0)
		return;

	num_sq_per_cos = (u8)(nic_dev->num_sqs / num_cos);
	if (num_sq_per_cos == 0)
		return;

	remainder = nic_dev->num_sqs % num_sq_per_cos;
	memset(hw_dcb_cfg->cos_qp_offset, 0, sizeof(hw_dcb_cfg->cos_qp_offset));
	memset(hw_dcb_cfg->cos_qp_num, 0, sizeof(hw_dcb_cfg->cos_qp_num));

	for (i = 0; i < PCP_MAX_UP; i++) {
		if (BIT(i) & valid_cos_map) {
			u8 cos_qp_num = num_sq_per_cos;
			u8 cos_qp_offset = (u8)(cur_cos_num * num_sq_per_cos);

			if (cur_cos_num < remainder) {
				cos_qp_num++;
				cos_qp_offset += cur_cos_num;
			} else {
				cos_qp_offset += remainder;
			}

			cur_cos_num++;
			valid_cos_map -= (u8)BIT(i);

			hw_dcb_cfg->cos_qp_offset[i] = cos_qp_offset;
			hw_dcb_cfg->cos_qp_num[i] = cos_qp_num;
			PMD_DRV_LOG(INFO,
				    "cos %u, cos_qp_offset=%u cos_qp_num=%u", i,
				    cos_qp_offset, cos_qp_num);
		}
	}

	memcpy(wanted_dcb_cfg->cos_qp_offset, hw_dcb_cfg->cos_qp_offset,
	       sizeof(hw_dcb_cfg->cos_qp_offset));
	memcpy(wanted_dcb_cfg->cos_qp_num, hw_dcb_cfg->cos_qp_num,
	       sizeof(hw_dcb_cfg->cos_qp_num));
}

void
hinic3_set_txq_cos(struct hinic3_nic_dev *nic_dev, u16 start_qid, u16 q_num, u8 cos)
{
	u16 idx;

	for (idx = 0; idx < q_num; idx++)
		nic_dev->dcb->txq_cos[idx + start_qid] = cos;
}

/**
 * q_num = cos_qp_num: cos per queue num, equal queue_num / 8
 *
 * @example
 * 64 queue:
 * 0-7 cos0
 * 8-15 cos1
 *
 * 16 queue:
 * 0-1 cos0
 * 2-3 cos1
 */
void
hinic3_update_tx_db_cos(struct hinic3_nic_dev *nic_dev, u8 dcb_en)
{
	struct hinic3_dcb_config *hw_dcb_cfg = &nic_dev->dcb->hw_dcb_cfg;
	u8 i;
	u16 start_qid, q_num;
	hinic3_set_txq_cos(nic_dev, 0, nic_dev->num_sqs, hw_dcb_cfg->default_cos);

	if (!dcb_en)
		return;

	for (i = 0; i < NIC_DCB_COS_MAX; i++) {
		q_num = (u16)hw_dcb_cfg->cos_qp_num[i];
		if (q_num) {
			start_qid = (u16)hw_dcb_cfg->cos_qp_offset[i];

			hinic3_set_txq_cos(nic_dev, start_qid, q_num, i);
			PMD_DRV_LOG(ERR,
				    "update tx db cos, start_qid %u, q_num=%u "
				    "cos=%u",
				    start_qid, q_num, i);
		}
	}
}

u8
hinic3_get_dev_user_cos_num(struct hinic3_nic_dev *nic_dev)
{
	struct hinic3_dcb *dcb = nic_dev->dcb;

	if (dcb->hw_dcb_cfg.trust == HINIC3_DCB_PCP)
		return dcb->hw_dcb_cfg.pcp_user_cos_num;
	if (dcb->hw_dcb_cfg.trust == HINIC3_DCB_DSCP)
		return dcb->hw_dcb_cfg.dscp_user_cos_num;
	return 0;
}

static void
hinic3_vf_fillout_indir_tbl(struct hinic3_nic_dev *nic_dev, u8 num_cos, u32 *indir)
{
	struct hinic3_dcb *dcb = nic_dev->dcb;
	u16 k, group_size, start_qid = 0, qp_num = 0;
	u32 i = 0;
	u8 vf_indir_num = NIC_RSS_INDIR_SIZE / 2;
	u8 j, cur_cos = 0, default_cos;
	u8 valid_cos_map = hinic3_get_dev_valid_cos_map(nic_dev);
	if (num_cos == 0) {
		for (i = 0; i < NIC_RSS_INDIR_SIZE; i++)
			indir[i] = i % nic_dev->num_rqs;
		return;
	}
	group_size = vf_indir_num / num_cos;

	for (j = 0; j < num_cos; j++) {
		while (cur_cos < NIC_VF_DCB_COS_MAX &&
		       dcb->hw_dcb_cfg.cos_qp_num[cur_cos] == 0)
			cur_cos++;

		if (cur_cos >= NIC_DCB_COS_MAX) {
			if (BIT(dcb->hw_dcb_cfg.default_cos) & valid_cos_map)
				default_cos = dcb->hw_dcb_cfg.default_cos;
			else
				default_cos = (u8)rte_fls_u32(valid_cos_map) - 1;

			start_qid = dcb->hw_dcb_cfg.cos_qp_offset[default_cos];
			qp_num = dcb->hw_dcb_cfg.cos_qp_num[default_cos];
		} else {
			start_qid = dcb->hw_dcb_cfg.cos_qp_offset[cur_cos];
			qp_num = dcb->hw_dcb_cfg.cos_qp_num[cur_cos];
		}

		for (k = 0; k < group_size; k++) {
			indir[i] = start_qid + k % qp_num;
			indir[i + vf_indir_num] = start_qid + k % qp_num;
			i++;
		}

		cur_cos++;
	}
}

static void
hinic3_fillout_indir_tbl(struct hinic3_nic_dev *nic_dev, u8 num_cos, u32 *indir)
{
	struct hinic3_dcb *dcb = nic_dev->dcb;
	u16 k, group_size, start_qid = 0, qp_num = 0;
	u32 i = 0;
	u8 j, cur_cos = 0, default_cos;
	u8 valid_cos_map = hinic3_get_dev_valid_cos_map(nic_dev);
	if (num_cos == 0) {
		for (i = 0; i < NIC_RSS_INDIR_SIZE; i++)
			indir[i] = i % nic_dev->num_rqs;
		return;
	}
	group_size = NIC_RSS_INDIR_SIZE / num_cos;
	for (j = 0; j < num_cos; j++) {
		while (cur_cos < NIC_DCB_COS_MAX &&
		       dcb->hw_dcb_cfg.cos_qp_num[cur_cos] == 0)
			cur_cos++;

		if (cur_cos >= NIC_DCB_COS_MAX) {
			if (BIT(dcb->hw_dcb_cfg.default_cos) & valid_cos_map)
				default_cos = dcb->hw_dcb_cfg.default_cos;
			else
				default_cos = (u8)rte_fls_u32(valid_cos_map) - 1;

			start_qid = dcb->hw_dcb_cfg.cos_qp_offset[default_cos];
			qp_num = dcb->hw_dcb_cfg.cos_qp_num[default_cos];
		} else {
			start_qid = dcb->hw_dcb_cfg.cos_qp_offset[cur_cos];
			qp_num = dcb->hw_dcb_cfg.cos_qp_num[cur_cos];
		}

		for (k = 0; k < group_size; k++)
			indir[i++] = start_qid + k % qp_num;

		cur_cos++;
	}
}

static int
hinic3_set_hw_rss_parameters(struct hinic3_nic_dev *nic_dev, u8 rss_en, u8 cos_num, u8 *cos_map)
{
	u32 indirtbl[HINIC3_RSS_INDIR_SIZE] = {0};
	int err;

	err = hinic3_rss_set_hash_key(nic_dev->hwdev, nic_dev->rss_key, HINIC3_RSS_KEY_SIZE);

	err = hinic3_rss_get_indir_tbl(nic_dev->hwdev, indirtbl, HINIC3_RSS_INDIR_SIZE);
	if (err) {
		PMD_DRV_LOG(ERR, "Get rss indir tbl failed");
		return err;
	}
	if (HINIC3_FUNC_TYPE(nic_dev->hwdev) == TYPE_VF)
		hinic3_vf_fillout_indir_tbl(nic_dev, cos_num, indirtbl);

	else
		hinic3_fillout_indir_tbl(nic_dev, cos_num, indirtbl);

	err = hinic3_rss_set_indir_tbl(nic_dev->hwdev, indirtbl, HINIC3_RSS_INDIR_SIZE);
	if (err) {
		PMD_DRV_LOG(ERR, "Set rss indir tbl failed");
		return err;
	}

	err = hinic3_set_rss_type(nic_dev->hwdev, nic_dev->rss_type);
	if (err) {
		PMD_DRV_LOG(ERR, "Set rss type failed");
		return err;
	}

	err = hinic3_rss_set_hash_engine(nic_dev->hwdev, HINIC3_RSS_HASH_ENGINE_TYPE_XOR);
	if (err) {
		PMD_DRV_LOG(ERR, "Set hash engine failed");
		return err;
	}

	/* Consist with hisdk3, both PF and VF indir divided into 8 groups. */
	err = hinic3_rss_cfg(nic_dev->hwdev, rss_en, HINIC3_MAX_COS_NUM, cos_map);
	if (err) {
		PMD_DRV_LOG(ERR, "Rss cfg failed");
		return err;
	}

	return err;
}

int
hinic3_dcb_rss_init(struct hinic3_nic_dev *nic_dev, u8 dcb_en)
{
	struct hinic3_dcb *dcb = nic_dev->dcb;
	u8 i, cos_num = 0;
	u8 cos_map[NIC_DCB_UP_MAX] = {0};
	u8 cfg_map[NIC_DCB_UP_MAX] = {0};

	if (IS_QPOOL_MODE()) {
		PMD_DRV_LOG(ERR, "Qpool mode not support DCB config");
		return -EINVAL;
	}

	if (dcb_en) {
		cos_num = hinic3_get_dev_user_cos_num(nic_dev);

		if (dcb->hw_dcb_cfg.trust == HINIC3_DCB_PCP)
			memcpy(cfg_map, dcb->hw_dcb_cfg.pcp2cos, sizeof(dcb->hw_dcb_cfg.pcp2cos));
		else if (dcb->hw_dcb_cfg.trust == HINIC3_DCB_DSCP) {
			for (i = 0; i < NIC_DCB_UP_MAX; i++)
				cfg_map[i] = dcb->hw_dcb_cfg.dscp2cos[i * NIC_DCB_DSCP_NUM];
		}

		for (i = 0; i < NIC_DCB_UP_MAX; i++)
			cos_map[i] = cfg_map[NIC_DCB_UP_MAX - (i + 1)];
		/* cos_num rounds down (2^n). */
		while (cos_num & (cos_num - 1))
			cos_num++;
	} else {
		cos_num = 0;
	}

	return hinic3_set_hw_rss_parameters(nic_dev, 1, cos_num, cos_map);
}

int
hinic3_configure_dcb_hw(struct hinic3_nic_dev *nic_dev, u8 dcb_en)
{
	int err;
	u8 user_cos_num = hinic3_get_dev_user_cos_num(nic_dev);
	struct hinic3_dcb_config *hw_dcb_cfg = &nic_dev->dcb->hw_dcb_cfg;

	err = init_default_dcb_cfg(nic_dev, hw_dcb_cfg);
	if (err) {
		PMD_DRV_LOG(ERR, "Initialize dcb configuration failed");
		nic_dev->dcb->dcb_on = 0;
		return err;
	}

	memcpy(&nic_dev->dcb->wanted_dcb_cfg, hw_dcb_cfg,
	       sizeof(struct hinic3_dcb_config));

	PMD_DRV_LOG(INFO, "Support num cos %u, default cos %u",
		    nic_dev->dcb->cos_config_num_max, hw_dcb_cfg->default_cos);

	err = hinic3_dcb_init_tm(nic_dev);
	if (err != 0) {
		PMD_DRV_LOG(ERR, "Dcb init tm configuration failed.");
		return err;
	}

	if(nic_dev->feature_cap == SP600_NIC_FEATURE ||
	   !HINIC3_IS_VF(nic_dev->hwdev)) {
		err = hinic3_sync_dcb_state(nic_dev->hwdev, CMD_QOS_OP_SET, dcb_en);
		if (err) {
			PMD_DRV_LOG(ERR, "Set dcb state failed");
			return err;
		}
	}

	hinic3_update_qp_cos_cfg(nic_dev, user_cos_num);
	hinic3_update_tx_db_cos(nic_dev, dcb_en);

	return err;
}

int
hinic3_setup_cos(struct hinic3_nic_dev *nic_dev, u8 cos)
{
	struct hinic3_dcb *dcb = nic_dev->dcb;

	if (cos > dcb->cos_config_num_max) {
		PMD_DRV_LOG(ERR, "Invalid num_tc: %u, max cos: %u", cos,
			    dcb->cos_config_num_max);
		return -EINVAL;
	}

	return hinic3_configure_dcb_hw(nic_dev, cos ? 1 : 0);
}

int
hinic3_get_dcb_info(struct rte_eth_dev *dev, struct rte_eth_dcb_info *dcb_info)
{
	struct hinic3_nic_dev *nic_dev;
	enum rte_eth_rx_mq_mode mq_mode;
	int i;
	u8 cos_num;

	nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	mq_mode = dev->data->dev_conf.rxmode.mq_mode;
	cos_num = hinic3_get_dev_user_cos_num(nic_dev);

	nic_dev->dcb->dcb_on = 1;

	if ((u32)mq_mode & ETH_MQ_RX_DCB_FLAG)
		dcb_info->nb_tcs =
			dev->data->dev_conf.rx_adv_conf.dcb_rx_conf.nb_tcs;
	else
		dcb_info->nb_tcs = 1;
	struct rte_eth_dcb_rx_conf *rx_conf =
		&dev->data->dev_conf.rx_adv_conf.dcb_rx_conf;
	for (i = 0; i < NIC_DCB_TC_MAX; i++)
		dcb_info->prio_tc[i] = rx_conf->dcb_tc[i];

	while (cos_num & (cos_num - 1))
		cos_num++;

	for (i = 0; i < dcb_info->nb_tcs; i++) {
		/* RX */
		dcb_info->tc_queue.tc_rxq[0][i].base =
			nic_dev->dcb->hw_dcb_cfg.cos_qp_offset[i];
		dcb_info->tc_queue.tc_rxq[0][i].nb_queue =
			nic_dev->dcb->hw_dcb_cfg.cos_qp_num[i];
		/* TX */
		dcb_info->tc_queue.tc_txq[0][i].base =
			nic_dev->dcb->hw_dcb_cfg.cos_qp_offset[i];
		dcb_info->tc_queue.tc_txq[0][i].nb_queue =
			nic_dev->dcb->hw_dcb_cfg.cos_qp_num[i];
	}

	for (i = 0; i < dcb_info->nb_tcs; i++)
		dcb_info->tc_bws[i] = nic_dev->ets->tc_bw[i];
	return 0;
}
