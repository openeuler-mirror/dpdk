/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2025-2026 Huawei Technologies Co., Ltd
 */

#ifndef _HINIC3_STN_CMDQ_H_
#define _HINIC3_STN_CMDQ_H_

#include "hinic3_pmd_nic_io.h"
#include "base/hinic3_pmd_cmdq.h"

struct hinic3_rxq;

struct hinic3_qp_ctxt_header {
	u16 num_queues;
	u16 queue_type;
	u16 start_qid;
	u16 rsvd;
};

struct hinic3_clean_queue_ctxt {
	struct hinic3_qp_ctxt_header cmdq_hdr;
	u32 rsvd;
};

struct hinic3_qp_ctxt_block {
	struct hinic3_qp_ctxt_header cmdq_hdr;
	union {
		struct hinic3_sq_ctxt sq_ctxt[HINIC3_Q_CTXT_MAX];
		struct hinic3_rq_ctxt rq_ctxt[HINIC3_Q_CTXT_MAX];
	};
};

struct hinic3_vlan_ctx {
	u32 func_id;
	u32 qid; /* if qid = 0xFFFF, config for all queues */
	u32 vlan_id;
	u32 vlan_mode;
	u32 vlan_sel;
};

/**
 * Get cmdq ops software tile NIC(stn) supported.
 *
 * @return
 * Pointer to ops.
 */
u8 hinic3_prepare_cmd_buf_clean_tso_lro_space_stn(struct hinic3_nic_dev *nic_dev,
						  struct hinic3_cmd_buf *cmd_buf,
						  enum hinic3_qp_ctxt_type ctxt_type);

u8 hinic3_prepare_cmd_buf_qp_context_multi_store_stn(struct hinic3_nic_dev *nic_dev,
						     struct hinic3_cmd_buf *cmd_buf,
						     enum hinic3_qp_ctxt_type ctxt_type,
						     u16 start_qid, u16 max_ctxts);
int hinic3_cmd_modify_queue_ctx_stn(struct hinic3_nic_dev *nic_dev,
				    struct hinic3_qp_ctxt_block *ctxt_block);
u8 hinic3_prepare_cmd_buf_set_rss_indir_table_stn(struct hinic3_nic_dev *nic_dev,
						  const u32 *indir_table,
						  struct hinic3_cmd_buf *cmd_buf,
						  u32 indir_table_size);

u8 hinic3_prepare_cmd_buf_get_rss_indir_table_stn(struct hinic3_nic_dev *nic_dev,
						  struct hinic3_cmd_buf *cmd_buf);

void hinic3_cmd_buf_to_rss_indir_table_stn(const struct hinic3_cmd_buf *cmd_buf,
					   u32 *indir_table,
					   u16 indir_table_size);

void hinic3_prepare_rq_ctxt_ceq_and_prefetch_stn(struct hinic3_rxq *rq,
						 struct hinic3_rq_ctxt *rq_ctxt);

void hinic3_prepare_sq_ctxt_drop_and_prefetch_stn(struct hinic3_sq_ctxt *sq_ctxt);

#endif /* _HINIC3_STN_CMDQ_H_ */
