/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2025-2026 Huawei Technologies Co., Ltd
 */

#include <sys/ioctl.h>
#include "mml/hinic3_pmd_mml_lib.h"
#include "hinic3_compat.h"
#include "hinic3_pmd_mbox.h"
#include "hinic3_pmd_nic_cfg.h"
#include "hinic3_pmd_hwif.h"
#include "hinic3_htn_cmdq.h"
#include "hinic3_pmd_hwdev.h"
#include "hinic3_pmd_ethdev.h"
#include "hinic3_pmd_rx.h"

#define HINIC3_DEAULT_DROP_THD_OFF	0xFFFF
#define HINIC3_DEAULT_DROP_THD_ON	0xFFFF

#define SQ_PREFETCH_MAX			5
#define SQ_PREFETCH_MIN			4
#define SQ_PREFETCH_THRESHOLD		48

#define RQ_PREFETCH_MAX			4
#define RQ_PREFETCH_MIN			2
#define RQ_PREFETCH_THRESHOLD		32

#define RQ_PFH_TH			7

#define RQ_CTXT_CEQ_ATTR_PFH_TH_SHIFT			0
#define RQ_CTXT_CEQ_ATTR_INTR_SHIFT			21
#define RQ_CTXT_CEQ_ATTR_EN_SHIFT			31

#define RQ_CTXT_CEQ_ATTR_PFH_TH_MASK			0x1FU
#define RQ_CTXT_CEQ_ATTR_EN_MASK			0x1U
#define RQ_CTXT_CEQ_ATTR_INTR_MASK			0x3FFU

#define SQ_CTXT_SIZE(num_sqs)	((u16)(sizeof(struct hinic3_qp_ctxt_header_htn) \
				+ (num_sqs) * sizeof(struct hinic3_sq_ctxt)))

#define RQ_CTXT_SIZE(num_rqs)	((u16)(sizeof(struct hinic3_qp_ctxt_header_htn) \
				+ (num_rqs) * sizeof(struct hinic3_rq_ctxt)))

static void qp_prepare_cmdq_header(struct hinic3_qp_ctxt_header_htn *qp_ctxt_hdr,
				   enum hinic3_qp_ctxt_type ctxt_type, u16 num_queues,
				   u16 q_id, u16 func_id)
{
	qp_ctxt_hdr->queue_type = ctxt_type;
	qp_ctxt_hdr->num_queues = num_queues;
	qp_ctxt_hdr->start_qid = q_id;
	qp_ctxt_hdr->dest_func_id = func_id;

	rte_mb();
	hinic3_cpu_to_be32(qp_ctxt_hdr, sizeof(*qp_ctxt_hdr));
}

static inline u16 get_local_qid(struct hinic3_nic_dev *nic_dev,
				u16 start_qid,
				enum hinic3_qp_ctxt_type ctxt_type)
{
	return ctxt_type == HINIC3_QP_CTXT_TYPE_RQ ?
	       nic_dev->rxqs[start_qid]->local_qid:
	       nic_dev->txqs[start_qid]->local_qid;

}

u8 hinic3_prepare_cmd_buf_qp_context_multi_store_htn(struct hinic3_nic_dev *nic_dev,
						     struct hinic3_cmd_buf *cmd_buf,
						     enum hinic3_qp_ctxt_type ctxt_type,
						     u16 start_qid, u16 max_ctxts)
{
	struct hinic3_qp_ctxt_block_htn *qp_ctxt_block = NULL;
	u16 func_id;
	u16 i;

	qp_ctxt_block = cmd_buf->buf;
	func_id = hinic3_global_func_id(nic_dev->hwdev);
	qp_prepare_cmdq_header(&qp_ctxt_block->cmdq_hdr, ctxt_type,
			       max_ctxts, get_local_qid(nic_dev, start_qid, ctxt_type), func_id);

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

	return HINIC3_HTN_CMD_SQ_RQ_CONTEXT_MULTI_ST;
}

int hinic3_cmd_modify_queue_ctx_htn(struct hinic3_nic_dev *nic_dev,
				    struct hinic3_qp_ctxt_block_htn *ctxt_block,
				    int qid)
{
	struct msg_module msg_to_kernel = { 0 };
	int err;
	msg_to_kernel.qid = qid;
	msg_to_kernel.func_idx = hinic3_global_func_id(nic_dev->hwdev);
	msg_to_kernel.lcore_id = nic_dev->global_id;
	fill_ioctl_msg(&msg_to_kernel, SEND_TO_NPU, 0, sizeof(struct hinic3_qp_ctxt_block_htn),
		       sizeof(struct hinic3_qp_ctxt_block_htn), ctxt_block, ctxt_block);
	msg_to_kernel.npu_cmd.direct_resp = 1;
	msg_to_kernel.npu_cmd.mod = HINIC3_MOD_L2NIC;
	msg_to_kernel.npu_cmd.cmd = HINIC3_HTN_CMD_SQ_RQ_CONTEXT_MULTI_ST;
	msg_to_kernel.npu_cmd.ack_type = HINIC3_ACK_TYPE_CMDQ;
	err = ioctl(nic_dev->fd, 0, &msg_to_kernel);
	return err;
}

u8 hinic3_prepare_cmd_buf_clean_tso_lro_space_htn(struct hinic3_nic_dev *nic_dev,
						  struct hinic3_cmd_buf *cmd_buf,
						  enum hinic3_qp_ctxt_type ctxt_type)
{
	struct hinic3_clean_queue_ctxt_htn *ctxt_block = NULL;

	ctxt_block = cmd_buf->buf;
	ctxt_block->cmdq_hdr.num_queues = nic_dev->max_sqs;
	ctxt_block->cmdq_hdr.queue_type = ctxt_type;
	ctxt_block->cmdq_hdr.start_qid = 0;
	ctxt_block->cmdq_hdr.dest_func_id = hinic3_global_func_id(nic_dev->hwdev);

	rte_mb();
	hinic3_cpu_to_be32(ctxt_block, sizeof(*ctxt_block));

	cmd_buf->size = sizeof(*ctxt_block);
	return HINIC3_HTN_CMD_TSO_LRO_SPACE_CLEAN;
}

static void prepare_rss_indir_table_cmd_header_htn(struct hinic3_nic_dev *nic_dev,
						   struct hinic3_cmd_buf *cmd_buf)
{
	struct hinic3_rss_cmd_header_htn *header = cmd_buf->buf;

	header->dest_func_id = hinic3_global_func_id(nic_dev->hwdev);

	rte_mb();
	hinic3_cpu_to_be32(header, sizeof(*header));
}

u8 hinic3_prepare_cmd_buf_set_rss_indir_table_htn(struct hinic3_nic_dev *nic_dev,
						  const u32 *indir_table,
						  struct hinic3_cmd_buf *cmd_buf,
						  u32 indir_table_size)
{
	u32 i;
	u8 *indir_tbl = NULL;

	indir_tbl = (u8 *)cmd_buf->buf + sizeof(struct hinic3_rss_cmd_header_htn);
	cmd_buf->size = sizeof(struct hinic3_rss_cmd_header_htn) + indir_table_size;
	memset(indir_tbl, 0, indir_table_size);

	prepare_rss_indir_table_cmd_header_htn(nic_dev, cmd_buf);

	for (i = 0; i < indir_table_size; i++) {
		indir_tbl[i] = (u8)(*(indir_table + i));
	}

	rte_mb();
	hinic3_cpu_to_be32(indir_tbl, indir_table_size);

	return HINIC3_HTN_CMD_SET_RSS_INDIR_TABLE;
}

u8 hinic3_prepare_cmd_buf_get_rss_indir_table_htn(struct hinic3_nic_dev *nic_dev,
						  struct hinic3_cmd_buf *cmd_buf)
{
	memset(cmd_buf->buf, 0, cmd_buf->size);
	prepare_rss_indir_table_cmd_header_htn(nic_dev, cmd_buf);

	return HINIC3_HTN_CMD_GET_RSS_INDIR_TABLE;
}

static void hinic3_cmd_buf_to_rss_indir_table_htn_normal(const struct hinic3_cmd_buf *cmd_buf,
							 u32 *indir_table, u16 indir_table_size)
{
	u32 i;
	u8 *indir_tbl = NULL;

	indir_tbl = (u8 *)cmd_buf->buf;

	rte_mb();
	hinic3_be32_to_cpu(cmd_buf->buf, indir_table_size);
	for (i = 0; i < indir_table_size; i++) {
		indir_table[i] = *(indir_tbl + i);
	}
}

static void hinic3_cmd_buf_to_rss_indir_table_htn_qpool(const struct hinic3_cmd_buf *cmd_buf,
						 	u32 *indir_table, u16 indir_table_size)
{
	u32 i;
	u16 *indir_tbl = NULL;

	indir_tbl = (u16 *)cmd_buf->buf;
	rte_mb();
	for (i = 0; i < indir_table_size; i++) {
		indir_table[i] = indir_tbl[i];
	}
}

void hinic3_cmd_buf_to_rss_indir_table_htn(const struct hinic3_cmd_buf *cmd_buf,
					   u32 *indir_table, u16 indir_table_size)
{
	if (IS_QPOOL_MODE())
		hinic3_cmd_buf_to_rss_indir_table_htn_qpool(cmd_buf, indir_table, indir_table_size);
	else
		hinic3_cmd_buf_to_rss_indir_table_htn_normal(cmd_buf, indir_table, indir_table_size);

}

void hinic3_prepare_sq_ctxt_drop_and_prefetch_htn(struct hinic3_sq_ctxt *sq_ctxt)
{
	sq_ctxt->pkt_drop_thd = SQ_CTXT_PKT_DROP_THD_SET(HINIC3_DEAULT_DROP_THD_ON, THD_ON) |
				SQ_CTXT_PKT_DROP_THD_SET(HINIC3_DEAULT_DROP_THD_OFF, THD_OFF);

	sq_ctxt->pref_cache = SQ_CTXT_PREF_SET(SQ_PREFETCH_MIN, CACHE_MIN) |
			      SQ_CTXT_PREF_SET(SQ_PREFETCH_MAX, CACHE_MAX) |
			      SQ_CTXT_PREF_SET(SQ_PREFETCH_THRESHOLD, CACHE_THRESHOLD);
}

void hinic3_prepare_rq_ctxt_ceq_and_prefetch_htn(struct hinic3_rxq *rq,
						 struct hinic3_rq_ctxt *rq_ctxt)
{
	rq_ctxt->ceq_attr = RQ_CTXT_CEQ_ATTR_SET(rq->dp_intr_en ? 0 : 1, EN) |
			    RQ_CTXT_CEQ_ATTR_SET(RQ_PFH_TH, PFH_TH) |
			    RQ_CTXT_CEQ_ATTR_SET(rq->msix_entry_idx, INTR);

	rq_ctxt->pref_cache = RQ_CTXT_PREF_SET(RQ_PREFETCH_MIN, CACHE_MIN) |
			      RQ_CTXT_PREF_SET(RQ_PREFETCH_MAX, CACHE_MAX) |
			      RQ_CTXT_PREF_SET(RQ_PREFETCH_THRESHOLD, CACHE_THRESHOLD);
}