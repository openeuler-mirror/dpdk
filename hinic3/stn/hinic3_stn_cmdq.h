/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2025-2026 Huawei Technologies Co., Ltd
 */

#ifndef _HINIC3_STN_CMDQ_H_
#define _HINIC3_STN_CMDQ_H_

#include "hinic3_pmd_nic_io.h"
#include "base/hinic3_pmd_cmdq.h"
#include "mml/hinic3_pmd_mml_lib.h"

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
struct hinic3_nic_cmdq_ops *hinic3_nic_cmdq_get_stn_ops(void);

#endif /* _HINIC3_STN_CMDQ_H_ */
