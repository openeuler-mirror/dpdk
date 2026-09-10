/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2025-2026 Huawei Technologies Co., Ltd
 */

#include <sys/ioctl.h>
#include "mml/hinic3_pmd_mml_lib.h"
#include "hinic3_compat.h"
#include "hinic3_pmd_cmd.h"
#include "hinic3_pmd_mbox.h"
#include "hinic3_pmd_nic_cfg.h"
#include "hinic3_pmd_hwif.h"
#include "hinic3_pmd_hwdev.h"
#include "hinic3_stn_cmdq.h"
#include "hinic3_pmd_rx.h"

#define HINIC3_DEAULT_DROP_THD_ON			0xFFFF
#define HINIC3_DEAULT_DROP_THD_OFF			0
#define WQ_PREFETCH_MAX					6
#define WQ_PREFETCH_MIN					1
#define WQ_PREFETCH_THRESHOLD				256

#define RQ_CTXT_CEQ_ATTR_CI_WR_SHIFT			0
#define RQ_CTXT_CEQ_ATTR_INTR_SHIFT			21
#define RQ_CTXT_CEQ_ATTR_INTR_ARM_SHIFT			30
#define RQ_CTXT_CEQ_ATTR_EN_SHIFT			31

#define RQ_CTXT_CEQ_ATTR_CI_WR_MASK			0x1U
#define RQ_CTXT_CEQ_ATTR_INTR_MASK			0x3FFU
#define RQ_CTXT_CEQ_ATTR_INTR_ARM_MASK			0x1U
#define RQ_CTXT_CEQ_ATTR_EN_MASK			0x1U
/* Indicate ucode that this is an interrupt in the DPDK scenario. */
#define RQ_CTXT_INVALID_INTR_NUM			0x1FFU

#define SQ_CTXT_SIZE(num_sqs)	((u16)(sizeof(struct hinic3_qp_ctxt_header) \
				+ (num_sqs) * sizeof(struct hinic3_sq_ctxt)))

#define RQ_CTXT_SIZE(num_rqs)	((u16)(sizeof(struct hinic3_qp_ctxt_header) \
				+ (num_rqs) * sizeof(struct hinic3_rq_ctxt)))

static inline u16 get_local_qid(struct hinic3_nic_dev *nic_dev,
				u16 start_qid,
				enum hinic3_qp_ctxt_type ctxt_type)
{
	return ctxt_type == HINIC3_QP_CTXT_TYPE_RQ ?
			    nic_dev->rxqs[start_qid]->local_qid:
			    nic_dev->txqs[start_qid]->local_qid;
}

static void hinic3_qp_prepare_cmdq_header_stn(struct hinic3_qp_ctxt_header *qp_ctxt_hdr,
					      enum hinic3_qp_ctxt_type ctxt_type, u16 num_queues,
					      u16 q_id)
{
	qp_ctxt_hdr->queue_type = ctxt_type;
	qp_ctxt_hdr->num_queues = num_queues;
	qp_ctxt_hdr->start_qid = q_id;
	qp_ctxt_hdr->rsvd = 0;

	rte_mb();
	hinic3_cpu_to_be32(qp_ctxt_hdr, sizeof(*qp_ctxt_hdr));
}

u8 hinic3_prepare_cmd_buf_qp_context_multi_store_stn(struct hinic3_nic_dev *nic_dev,
						     struct hinic3_cmd_buf *cmd_buf,
						     enum hinic3_qp_ctxt_type ctxt_type,
						     u16 start_qid, u16 max_ctxts)
{
	struct hinic3_qp_ctxt_block *qp_ctxt_block = NULL;
	u16 i;

	qp_ctxt_block = cmd_buf->buf;

	hinic3_qp_prepare_cmdq_header_stn(&qp_ctxt_block->cmdq_hdr, ctxt_type,
					  max_ctxts, get_local_qid(nic_dev, start_qid, ctxt_type));

	for (i = 0; i < max_ctxts; i++) {
		if (ctxt_type == HINIC3_QP_CTXT_TYPE_RQ) {
			if (nic_dev->rxqs[start_qid + i] != NULL &&
			   !nic_dev->rxqs[start_qid + i]->is_hairpin)
			hinic3_rq_prepare_ctxt(nic_dev->rxqs[start_qid + i],
					       &qp_ctxt_block->rq_ctxt[i]);
		} else {
			if (nic_dev->txqs[start_qid + i] != NULL &&
			   !nic_dev->txqs[start_qid + i]->is_hairpin)
			hinic3_sq_prepare_ctxt(nic_dev->txqs[start_qid + i],
					       get_local_qid(nic_dev, start_qid + i, ctxt_type),
					       &qp_ctxt_block->sq_ctxt[i]);
		}
	}

	if (ctxt_type == HINIC3_QP_CTXT_TYPE_RQ)
		cmd_buf->size = RQ_CTXT_SIZE(max_ctxts);
	else
		cmd_buf->size = SQ_CTXT_SIZE(max_ctxts);

	return HINIC3_UCODE_CMD_MODIFY_QUEUE_CTX;
}

int hinic3_cmd_modify_queue_ctx_stn(struct hinic3_nic_dev *nic_dev,
				    struct hinic3_qp_ctxt_block *ctxt_block)
{
	struct msg_module msg_to_kernel = {0};
	int err;

	fill_ioctl_msg(&msg_to_kernel, SEND_TO_NPU, 0,
			sizeof(struct hinic3_qp_ctxt_block),
			sizeof(struct hinic3_qp_ctxt_block),
			ctxt_block, ctxt_block);
	msg_to_kernel.npu_cmd.direct_resp = 1;
	msg_to_kernel.npu_cmd.mod = HINIC3_MOD_L2NIC;
	msg_to_kernel.npu_cmd.cmd = HINIC3_UCODE_CMD_MODIFY_QUEUE_CTX;
	msg_to_kernel.npu_cmd.ack_type = HINIC3_ACK_TYPE_CMDQ;

	err = ioctl(nic_dev->fd, 0, &msg_to_kernel);
	if (err < 0)
		PMD_DRV_LOG(ERR, "Modify tx queue ctx error: %d.", errno);
	return err;
}

u8 hinic3_prepare_cmd_buf_clean_tso_lro_space_stn(struct hinic3_nic_dev *nic_dev,
						  struct hinic3_cmd_buf *cmd_buf,
						  enum hinic3_qp_ctxt_type ctxt_type)
{
	struct hinic3_clean_queue_ctxt *ctxt_block = NULL;

	ctxt_block = cmd_buf->buf;
	ctxt_block->cmdq_hdr.num_queues = nic_dev->max_sqs;
	ctxt_block->cmdq_hdr.queue_type = ctxt_type;
	ctxt_block->cmdq_hdr.start_qid = 0;

	rte_mb();
	hinic3_cpu_to_be32(ctxt_block, sizeof(*ctxt_block));

	cmd_buf->size = sizeof(*ctxt_block);
	return HINIC3_UCODE_CMD_CLEAN_QUEUE_CONTEXT;
}

u8 hinic3_prepare_cmd_buf_set_rss_indir_table_stn(struct hinic3_nic_dev *nic_dev,
						  const u32 *indir_table,
						  struct hinic3_cmd_buf *cmd_buf,
						  u32 indir_table_size)
{
	(void)nic_dev;
	u32 i, size;
	u32 *temp = NULL;
	struct nic_rss_indirect_tbl *indir_tbl = NULL;

	indir_tbl = (struct nic_rss_indirect_tbl *)cmd_buf->buf;
	cmd_buf->size = sizeof(struct nic_rss_indirect_tbl);
	memset(indir_tbl, 0, sizeof(*indir_tbl));

	if (indir_table_size > HINIC3_RSS_INDIR_SIZE) {
		PMD_DRV_LOG(ERR, "indir_table_size %u exceeds max %u",
			    indir_table_size, HINIC3_RSS_INDIR_SIZE);
		return -EINVAL;
	}

	for (i = 0; i < indir_table_size; i++) {
		indir_tbl->entry[i] = (u16)(*(indir_table + i));
	}
	rte_mb();
	size = (size_t)sizeof(indir_tbl->entry) / sizeof(u32);
	temp = (u32 *)indir_tbl->entry;
	for (i = 0; i < size; i++)
		temp[i] = cpu_to_be32(temp[i]);

	return HINIC3_UCODE_CMD_SET_RSS_INDIR_TABLE;
}

u8 hinic3_prepare_cmd_buf_get_rss_indir_table_stn(struct hinic3_nic_dev *nic_dev,
						  struct hinic3_cmd_buf *cmd_buf)
{
	(void)nic_dev;
	memset(cmd_buf->buf, 0, cmd_buf->size);

	return HINIC3_UCODE_CMD_GET_RSS_INDIR_TABLE;
}

void hinic3_cmd_buf_to_rss_indir_table_stn(struct hinic3_hwdev *hwdev, const struct hinic3_cmd_buf *cmd_buf, u32 *indir_table, u16 indir_table_size)
{
	u32 i;
	u16 *indir_tbl = NULL;
	struct hinic3_indir_tbl_qid_lqid *entry = NULL;
	indir_tbl = (u16 *)cmd_buf->buf;

	if (IS_QPOOL_MODE(hwdev)) {
		for (i = 0; i < indir_table_size; i++) {
			entry = hinic3_find_by_local_qid(*(indir_tbl + i));
			indir_table[i] = entry ? entry->q_id : 0xFFF;
		}
	} else {
		for (i = 0; i < indir_table_size; i++)
			indir_table[i] = *(indir_tbl + i);
	}
}

void hinic3_prepare_sq_ctxt_drop_and_prefetch_stn(struct hinic3_sq_ctxt *sq_ctxt)
{
	sq_ctxt->pkt_drop_thd = SQ_CTXT_PKT_DROP_THD_SET(HINIC3_DEAULT_DROP_THD_ON, THD_ON) |
				SQ_CTXT_PKT_DROP_THD_SET(HINIC3_DEAULT_DROP_THD_OFF, THD_OFF);

	sq_ctxt->pref_cache = SQ_CTXT_PREF_SET(WQ_PREFETCH_MIN, CACHE_MIN) |
			      SQ_CTXT_PREF_SET(WQ_PREFETCH_MAX, CACHE_MAX) |
			      SQ_CTXT_PREF_SET(WQ_PREFETCH_THRESHOLD, CACHE_THRESHOLD);
}

void hinic3_prepare_rq_ctxt_ceq_and_prefetch_stn(struct hinic3_rxq *rq,
						 struct hinic3_rq_ctxt *rq_ctxt)
{
	u16 msix_entry_idx = rq->dp_intr_en ? rq->msix_entry_idx : RQ_CTXT_INVALID_INTR_NUM;

	rq_ctxt->ceq_attr = RQ_CTXT_CEQ_ATTR_SET(rq->dp_intr_en ? 0 : 1, EN) |
			    RQ_CTXT_CEQ_ATTR_SET(0, INTR_ARM) |
			    RQ_CTXT_CEQ_ATTR_SET(msix_entry_idx, INTR);

	if (rq->wqe_type == HINIC3_COMPACT_RQ_WQE && rq->nic_dev->config.rx_cqe_compact_en) {
		rq_ctxt->ceq_attr |= RQ_CTXT_CEQ_ATTR_SET(1, EN);
		rq_ctxt->ceq_attr |= RQ_CTXT_CEQ_ATTR_SET(1, CI_WR);
		rq_ctxt->ceq_attr |= RQ_CTXT_CEQ_ATTR_SET(1, INTR_ARM);
		rq_ctxt->cqe_sge_len |= RQ_CTXT_CQE_LEN_SET(RQ_CQE_AGGREGATE_NUM, MAX_COUNT);
		rq_ctxt->pi_paddr_hi = upper_32_bits(rq->rq_ci_paddr >> RQ_CI_ADDR_SHIFT);
		rq_ctxt->pi_paddr_lo = lower_32_bits(rq->rq_ci_paddr >> RQ_CI_ADDR_SHIFT);
	}

	rq_ctxt->pref_cache = RQ_CTXT_PREF_SET(WQ_PREFETCH_MIN, CACHE_MIN) |
			      RQ_CTXT_PREF_SET(WQ_PREFETCH_MAX, CACHE_MAX) |
			      RQ_CTXT_PREF_SET(WQ_PREFETCH_THRESHOLD, CACHE_THRESHOLD);
}
