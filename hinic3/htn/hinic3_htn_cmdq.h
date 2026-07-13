/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2021 Huawei Technologies Co., Ltd */

#ifndef _HINIC3_HTN_CMDQ_H_
#define _HINIC3_HTN_CMDQ_H_

#include "hinic3_pmd_nic_io.h"

#define SQ_CTXT_SIZE(num_sqs)	((u16)(sizeof(struct hinic3_qp_ctxt_header_htn) \
				+ (num_sqs) * sizeof(struct hinic3_sq_ctxt)))

#define RQ_CTXT_SIZE(num_rqs)	((u16)(sizeof(struct hinic3_qp_ctxt_header_htn) \
				+ (num_rqs) * sizeof(struct hinic3_rq_ctxt)))
struct hinic3_qp_ctxt_header_htn {
	u32 rsvd[2];
	u16 num_queues;
	u16 queue_type;
	u16 start_qid;
	u16 dest_func_id;
};

struct hinic3_clean_queue_ctxt_htn {
	struct hinic3_qp_ctxt_header_htn cmdq_hdr;
};

struct hinic3_qp_ctxt_block_htn {
	struct hinic3_qp_ctxt_header_htn cmdq_hdr;
	union {
		struct hinic3_sq_ctxt  sq_ctxt[HINIC3_Q_CTXT_MAX];
		struct hinic3_rq_ctxt  rq_ctxt[HINIC3_Q_CTXT_MAX];
	};
};

struct hinic3_rss_cmd_header_htn {
	u32 rsv[3];
	u16 rsv1;
	u16 dest_func_id;
};

/* NIC HTN CMD */
enum hinic3_htn_cmd {
	HINIC3_HTN_CMD_SQ_RQ_CONTEXT_MULTI_ST = 0x20,
	HINIC3_HTN_CMD_SQ_RQ_CONTEXT_MULTI_LD,
	HINIC3_HTN_CMD_TSO_LRO_SPACE_CLEAN,
	HINIC3_HTN_CMD_SVLAN_MODIFY,
	HINIC3_HTN_CMD_SET_RSS_INDIR_TABLE,
	HINIC3_HTN_CMD_GET_RSS_INDIR_TABLE
};

struct hinic3_vlan_ctx_htn {
	u32 rsv[2];
	u16 vlan_tag;
	u8 vlan_sel;
	u8 vlan_mode;
	u16 start_qid;
	u16 dest_func_id;
};

u8 hinic3_prepare_cmd_buf_clean_tso_lro_space(struct hinic3_nic_dev *nic_dev,
					      struct hinic3_cmd_buf *cmd_buf,
					      enum hinic3_qp_ctxt_type ctxt_type);

u8 hinic3_prepare_cmd_buf_qp_context_multi_store(struct hinic3_nic_dev *nic_dev,
						 struct hinic3_cmd_buf *cmd_buf,
						 enum hinic3_qp_ctxt_type ctxt_type,
						 u16 start_qid, u16 max_ctxts);

u8 hinic3_prepare_cmd_buf_set_rss_indir_table(struct hinic3_nic_dev *nic_dev,
					      const u32 *indir_table,
					      struct hinic3_cmd_buf *cmd_buf);

u8 hinic3_prepare_cmd_buf_get_rss_indir_table(struct hinic3_nic_dev *nic_dev,
					      struct hinic3_cmd_buf *cmd_buf);

void hinic3_cmd_buf_to_rss_indir_table_qpool(const struct hinic3_cmd_buf *cmd_buf, u32 *indir_table, u16 indir_table_size);

void hinic3_cmd_buf_to_rss_indir_table(const struct hinic3_cmd_buf *cmd_buf, u32 *indir_table);

void hinic3_prepare_sq_ctxt_drop_and_prefetch(struct hinic3_sq_ctxt *sq_ctxt);

void hinic3_prepare_rq_ctxt_ceq_and_prefetch(struct hinic3_rxq *rq, struct hinic3_rq_ctxt *rq_ctxt);
#endif /* _HINIC3_HTN_CMDQ_H_ */