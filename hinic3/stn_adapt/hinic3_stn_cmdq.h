/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2021 Huawei Technologies Co., Ltd */

#ifndef _HINIC3_STN_CMDQ_H_
#define _HINIC3_STN_CMDQ_H_

#include "hinic3_pmd_nic_io.h"

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
		struct hinic3_sq_ctxt  sq_ctxt[HINIC3_Q_CTXT_MAX];
		struct hinic3_rq_ctxt  rq_ctxt[HINIC3_Q_CTXT_MAX];
	};
};

struct hinic3_vlan_ctx {
	u32 func_id;
	u32 qid; /* if qid = 0xFFFF, config for all queues */
	u32 vlan_id;
	u32 vlan_mode;
	u32 vlan_sel;
};

#endif /* _HINIC3_STN_CMDQ_H_ */
