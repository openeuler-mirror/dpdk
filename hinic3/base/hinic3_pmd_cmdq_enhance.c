/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2025-2026 Huawei Technologies Co., Ltd
 */

#include <rte_mbuf.h>

#include "hinic3_compat.h"
#include "hinic3_pmd_hwdev.h"
#include "hinic3_pmd_hwif.h"
#include "hinic3_pmd_wq.h"
#include "hinic3_pmd_cmd.h"
#include "hinic3_pmd_mgmt.h"
#include "hinic3_pmd_cmdq.h"
#include "hinic3_pmd_cmdq_enhance.h"

#define WQ_PREFETCH_MAX         4
#define WQ_PREFETCH_MIN         1
#define WQ_PREFETCH_THRESHOLD   256

void
hinic3_enhance_cmdq_init_queue_ctxt(struct hinic3_cmdq *cmdq)
{
	struct enhance_cmdq_ctxt_info *ctxt_info = &cmdq->cmdq_enhance_ctxt;
	struct hinic3_wq *wq = cmdq->wq;
	u64 cmdq_first_block_paddr, pfn;
	u16 start_ci = (u16)wq->cons_idx;
	u32 start_pi = (u16)wq->prod_idx;

	/* The data in HW is Big Endian Format */
	cmdq_first_block_paddr = wq->queue_buf_paddr;
	pfn = CMDQ_PFN(cmdq_first_block_paddr, RTE_PGSIZE_4K);

	/* First part 16B */
	ctxt_info->eq_cfg =
		ENHANCED_CMDQ_SET(pfn, CTXT0_CI_WQE_ADDR) |
		ENHANCED_CMDQ_SET(0, CTXT0_EQ) |
		ENHANCED_CMDQ_SET(0, CTXT0_CEQ_ARM) |
		ENHANCED_CMDQ_SET(0, CTXT0_CEQ_EN) |
		ENHANCED_CMDQ_SET(1, CTXT0_HW_BUSY_BIT);

	ctxt_info->dfx_pi_ci =
		ENHANCED_CMDQ_SET(0, CTXT1_Q_DIS) |
		ENHANCED_CMDQ_SET(0, CTXT1_ERR_CODE) |
		ENHANCED_CMDQ_SET(start_pi, CTXT1_PI) |
		ENHANCED_CMDQ_SET(start_ci, CTXT1_CI);

	/* Second part 16B */
	ctxt_info->pft_thd =
		ENHANCED_CMDQ_SET(CI_HIGN_IDX(start_ci), CTXT2_PFT_CI) |
		ENHANCED_CMDQ_SET(1, CTXT2_O_BIT) |
		ENHANCED_CMDQ_SET(WQ_PREFETCH_MIN, CTXT2_PFT_MIN) |
		ENHANCED_CMDQ_SET(WQ_PREFETCH_MAX, CTXT2_PFT_MAX) |
		ENHANCED_CMDQ_SET(WQ_PREFETCH_THRESHOLD, CTXT2_PFT_THD);
	ctxt_info->pft_ci =
		ENHANCED_CMDQ_SET(pfn, CTXT3_PFT_CI_ADDR) |
		ENHANCED_CMDQ_SET(start_ci, CTXT3_PFT_CI);

	/* Third part 16B */
	cmdq_first_block_paddr = cmdq_first_block_paddr;
	pfn = WQ_BLOCK_PFN(cmdq_first_block_paddr);

	ctxt_info->ci_cla_addr = ENHANCED_CMDQ_SET(pfn, CTXT4_CI_CLA_ADDR);
}

static void
enhance_cmdq_set_completion(struct cmdq_enhance_completion *completion,
			    const struct hinic3_cmd_buf *buf_out)
{
	completion->sge_resp_hi_addr = upper_32_bits(buf_out->dma_addr);
	completion->sge_resp_lo_addr = lower_32_bits(buf_out->dma_addr);
	completion->sge_resp_len = HINIC3_CMDQ_BUF_SIZE;
}

void
hinic3_enhance_cmdq_set_wqe(struct hinic3_cmdq_wqe *wqe,
			    enum cmdq_cmd_type cmd_type,
			    const struct hinic3_cmd_buf *buf_in,
			    const struct hinic3_cmd_buf *buf_out, int wrapped,
			    u8 mod, u8 cmd)
{
	struct enhanced_cmdq_wqe *enhanced_wqe = &wqe->enhanced_cmdq_wqe;

	if (buf_in == NULL) {
		PMD_DRV_LOG(ERR, "Cmdq buffer input is NULL");
		return;
	}

	enhanced_wqe->ctrl_sec.header =
		ENHANCE_CMDQ_WQE_HEADER_SET(buf_in->size, SEND_SGE_LEN) |
		ENHANCE_CMDQ_WQE_HEADER_SET(1, BDSL) |
		ENHANCE_CMDQ_WQE_HEADER_SET(DATA_SGE, DF) |
		ENHANCE_CMDQ_WQE_HEADER_SET(NORMAL_WQE_TYPE, DN) |
		ENHANCE_CMDQ_WQE_HEADER_SET(COMPACT_WQE_TYPE, EC) |
		ENHANCE_CMDQ_WQE_HEADER_SET((u32)wrapped, HW_BUSY_BIT);

	enhanced_wqe->ctrl_sec.sge_send_hi_addr = upper_32_bits(buf_in->dma_addr);
	enhanced_wqe->ctrl_sec.sge_send_lo_addr = lower_32_bits(buf_in->dma_addr);

	enhanced_wqe->completion.cs_format =
		ENHANCE_CMDQ_WQE_CS_SET(cmd, CMD) |
		ENHANCE_CMDQ_WQE_CS_SET(HINIC3_ACK_TYPE_CMDQ, ACK_TYPE) |
		ENHANCE_CMDQ_WQE_CS_SET(mod, MOD);

	switch (cmd_type) {
	case SYNC_CMD_DIRECT_RESP:
		enhanced_wqe->completion.cs_format |= ENHANCE_CMDQ_WQE_CS_SET(INLINE_DATA, CF);
		break;
	case SYNC_CMD_SGE_RESP:
		if (buf_out) {
			enhanced_wqe->completion.cs_format |=
				ENHANCE_CMDQ_WQE_CS_SET(SGE_RESPONSE, CF);
			enhance_cmdq_set_completion(&enhanced_wqe->completion, buf_out);
		}
		break;
	case ASYNC_CMD:
		break;
	}
}