/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2025-2026 Huawei Technologies Co., Ltd
 */

#include "hinic3_compat.h"
#include "hinic3_pmd_cmd.h"
#include "hinic3_pmd_nic_cfg.h"
#include "hinic3_pmd_hwif.h"
#include "hinic3_stn_cmdq.h"
#include "hinic3_pmd_rx.h"

void hinic3_prepare_sq_ctxt_drop_and_prefetch(struct hinic3_sq_ctxt *sq_ctxt)
{
	sq_ctxt->pkt_drop_thd = SQ_CTXT_PKT_DROP_THD_SET(HINIC3_DEAULT_DROP_THD_ON, THD_ON) |
				SQ_CTXT_PKT_DROP_THD_SET(HINIC3_DEAULT_DROP_THD_OFF, THD_OFF);

	sq_ctxt->pref_cache = SQ_CTXT_PREF_SET(WQ_PREFETCH_MIN, CACHE_MIN) |
			      SQ_CTXT_PREF_SET(WQ_PREFETCH_MAX, CACHE_MAX) |
			      SQ_CTXT_PREF_SET(WQ_PREFETCH_THRESHOLD, CACHE_THRESHOLD);
}

void
hinic3_prepare_rq_ctxt_ceq_and_prefetch(struct hinic3_rxq *rq,
					struct hinic3_rq_ctxt *rq_ctxt,
					bool support_rq_sw_compact_cqe,
					u8 intr_disable)
{
	u16 msix_entry_idx = rq->dp_intr_en ? rq->msix_entry_idx : RQ_CTXT_INVALID_INTR_NUM;

	rq_ctxt->ceq_attr = RQ_CTXT_CEQ_ATTR_SET(intr_disable, EN) |
			    RQ_CTXT_CEQ_ATTR_SET(0, INTR_ARM) |
			    RQ_CTXT_CEQ_ATTR_SET(msix_entry_idx, INTR);

	if (rq->wqe_type == HINIC3_COMPACT_RQ_WQE && support_rq_sw_compact_cqe) {
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
