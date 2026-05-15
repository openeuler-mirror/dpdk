/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2019 Huawei Technologies Co., Ltd
 */

#include <rte_io.h>
#include <rte_pci.h>
#include <rte_bus_pci.h>
#include <rte_mbuf.h>
#include <rte_malloc.h>
#include <rte_mempool.h>
#include <rte_config.h>
#include <rte_errno.h>
#include <rte_ether.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include "../mml/hinic3_pmd_mml_lib.h"
#include "htn_adapt/hinic3_htn_cmdq.h"
#include "base/hinic3_compat.h"
#include "base/hinic3_pmd_cmd.h"
#include "base/hinic3_pmd_wq.h"
#include "base/hinic3_pmd_mgmt.h"
#include "base/hinic3_pmd_cmdq.h"
#include "base/hinic3_pmd_hwdev.h"
#include "base/hinic3_pmd_hw_comm.h"
#include "base/hinic3_pmd_nic_cfg.h"
#include "base/hinic3_pmd_hwif.h"
#include "hinic3_pmd_nic_io.h"
#include "hinic3_pmd_tx.h"
#include "hinic3_pmd_rx.h"
#include "hinic3_pmd_ethdev.h"

#define HINIC3_DEAULT_TX_CI_PENDING_LIMIT	2
#define HINIC3_DEAULT_TX_CI_COALESCING_TIME	2

#define CI_IDX_HIGH_SHIFH				12

#define CI_HIGN_IDX(val)		((val) >> CI_IDX_HIGH_SHIFH)

#define SQ_CTXT_PI_IDX_SHIFT				0
#define SQ_CTXT_CI_IDX_SHIFT				16

#define SQ_CTXT_PI_IDX_MASK				0xFFFFU
#define SQ_CTXT_CI_IDX_MASK				0xFFFFU

#define SQ_CTXT_CI_PI_SET(val, member)			(((val) & \
					SQ_CTXT_##member##_MASK) \
					<< SQ_CTXT_##member##_SHIFT)

#define SQ_CTXT_MODE_SP_FLAG_SHIFT			0
#define SQ_CTXT_MODE_PKT_DROP_SHIFT			1

#define SQ_CTXT_MODE_SP_FLAG_MASK			0x1U
#define SQ_CTXT_MODE_PKT_DROP_MASK			0x1U

#define SQ_CTXT_MODE_SET(val, member)	(((val) & \
					SQ_CTXT_MODE_##member##_MASK) \
					<< SQ_CTXT_MODE_##member##_SHIFT)

#define SQ_CTXT_WQ_PAGE_HI_PFN_SHIFT			0
#define SQ_CTXT_WQ_PAGE_OWNER_SHIFT			23

#define SQ_CTXT_WQ_PAGE_HI_PFN_MASK			0xFFFFFU
#define SQ_CTXT_WQ_PAGE_OWNER_MASK			0x1U

#define SQ_CTXT_WQ_PAGE_SET(val, member)		(((val) & \
					SQ_CTXT_WQ_PAGE_##member##_MASK) \
					<< SQ_CTXT_WQ_PAGE_##member##_SHIFT)

#define SQ_CTXT_GLOBAL_SQ_ID_SHIFT			0

#define SQ_CTXT_GLOBAL_SQ_ID_MASK			0x1FFFU

#define SQ_CTXT_GLOBAL_QUEUE_ID_SET(val, member)	(((val) & \
					SQ_CTXT_##member##_MASK) \
					<< SQ_CTXT_##member##_SHIFT)


#define SQ_CTXT_VLAN_TAG_SHIFT				0
#define SQ_CTXT_VLAN_TYPE_SEL_SHIFT			16
#define SQ_CTXT_VLAN_INSERT_MODE_SHIFT			19
#define SQ_CTXT_VLAN_CEQ_EN_SHIFT			23

#define SQ_CTXT_VLAN_TAG_MASK				0xFFFFU
#define SQ_CTXT_VLAN_TYPE_SEL_MASK			0x7U
#define SQ_CTXT_VLAN_INSERT_MODE_MASK			0x3U
#define SQ_CTXT_VLAN_CEQ_EN_MASK			0x1U

#define SQ_CTXT_VLAN_CEQ_SET(val, member)		(((val) & \
					SQ_CTXT_VLAN_##member##_MASK) \
					<< SQ_CTXT_VLAN_##member##_SHIFT)

#define SQ_CTXT_PREF_CI_HI_SHIFT			0
#define SQ_CTXT_PREF_OWNER_SHIFT			4

#define SQ_CTXT_PREF_CI_HI_MASK				0xFU
#define SQ_CTXT_PREF_OWNER_MASK				0x1U

#define SQ_CTXT_PREF_WQ_PFN_HI_SHIFT			0
#define SQ_CTXT_PREF_CI_LOW_SHIFT			20

#define SQ_CTXT_PREF_WQ_PFN_HI_MASK			0xFFFFFU
#define SQ_CTXT_PREF_CI_LOW_MASK			0xFFFU

#define SQ_CTXT_WQ_BLOCK_PFN_HI_SHIFT			0

#define SQ_CTXT_WQ_BLOCK_PFN_HI_MASK			0x7FFFFFU

#define SQ_CTXT_WQ_BLOCK_SET(val, member)		(((val) & \
					SQ_CTXT_WQ_BLOCK_##member##_MASK) \
					<< SQ_CTXT_WQ_BLOCK_##member##_SHIFT)

#define RQ_CTXT_PI_IDX_SHIFT				0
#define RQ_CTXT_CI_IDX_SHIFT				16

#define RQ_CTXT_PI_IDX_MASK				0xFFFFU
#define RQ_CTXT_CI_IDX_MASK				0xFFFFU

#define RQ_CTXT_CI_PI_SET(val, member)			(((val) & \
					RQ_CTXT_##member##_MASK) \
					<< RQ_CTXT_##member##_SHIFT)

#define RQ_CTXT_WQ_PAGE_HI_PFN_SHIFT			0
#define RQ_CTXT_WQ_PAGE_WQE_TYPE_SHIFT			28
#define RQ_CTXT_WQ_PAGE_OWNER_SHIFT			31

#define RQ_CTXT_WQ_PAGE_HI_PFN_MASK			0xFFFFFU
#define RQ_CTXT_WQ_PAGE_WQE_TYPE_MASK			0x3U
#define RQ_CTXT_WQ_PAGE_OWNER_MASK			0x1U

#define RQ_CTXT_WQ_PAGE_SET(val, member)		(((val) & \
					RQ_CTXT_WQ_PAGE_##member##_MASK) << \
					RQ_CTXT_WQ_PAGE_##member##_SHIFT)

#define RQ_CTXT_CQE_LEN_SHIFT				28
#define RQ_CTXT_MAX_COUNT_SHIFT				18

#define RQ_CTXT_CQE_LEN_MASK				0x3U
#define RQ_CTXT_MAX_COUNT_MASK				0x3FFU
#define RQ_CTXT_CQE_LEN_SET(val, member)		(((val) & \
					RQ_CTXT_##member##_MASK) << \
					RQ_CTXT_##member##_SHIFT)

#define RQ_CTXT_PREF_CI_HI_SHIFT			0
#define RQ_CTXT_PREF_OWNER_SHIFT			4

#define RQ_CTXT_PREF_CI_HI_MASK				0xFU
#define RQ_CTXT_PREF_OWNER_MASK				0x1U

#define RQ_CTXT_PREF_WQ_PFN_HI_SHIFT			0
#define RQ_CTXT_PREF_CI_LOW_SHIFT			20

#define RQ_CTXT_PREF_WQ_PFN_HI_MASK			0xFFFFFU
#define RQ_CTXT_PREF_CI_LOW_MASK			0xFFFU

#define RQ_CTXT_WQ_BLOCK_PFN_HI_SHIFT			0

#define RQ_CTXT_WQ_BLOCK_PFN_HI_MASK			0x7FFFFFU

#define RQ_CTXT_WQ_BLOCK_SET(val, member)		(((val) & \
					RQ_CTXT_WQ_BLOCK_##member##_MASK) << \
					RQ_CTXT_WQ_BLOCK_##member##_SHIFT)

#define SIZE_16BYTES(size)		(RTE_ALIGN((size), 16) >> 4)

#define	WQ_PAGE_PFN_SHIFT				12

#define WQ_PAGE_PFN(page_addr)		((page_addr) >> WQ_PAGE_PFN_SHIFT)


#define SQ_CI_ADDR_SHIFT	2
#define RQ_CI_ADDR_SHIFT	4
#define RQ_CQE_AGGREGATE_NUM 		768
void hinic3_sq_prepare_ctxt(struct hinic3_txq *sq, u16 sq_id, struct hinic3_sq_ctxt *sq_ctxt)
{
	u64 wq_page_addr;
	u64 wq_page_pfn, wq_block_pfn;
	u32 wq_page_pfn_hi, wq_page_pfn_lo;
	u32 wq_block_pfn_hi, wq_block_pfn_lo;
	u16 pi_start, ci_start;

	sq->nic_dev->cmdq_ops->prepare_sq_ctxt_drop_and_prefetch(sq_ctxt);

	ci_start = sq->cons_idx & sq->q_mask;
	pi_start = sq->prod_idx & sq->q_mask;

	/* Read the first page from hardware table */
	wq_page_addr = sq->queue_buf_paddr;

	wq_page_pfn = WQ_PAGE_PFN(wq_page_addr);
	wq_page_pfn_hi = upper_32_bits(wq_page_pfn);
	wq_page_pfn_lo = lower_32_bits(wq_page_pfn);

	/* Use 0-level CLA */
	wq_block_pfn = WQ_BLOCK_PFN(wq_page_addr);
	wq_block_pfn_hi = upper_32_bits(wq_block_pfn);
	wq_block_pfn_lo = lower_32_bits(wq_block_pfn);

	sq_ctxt->ci_pi = SQ_CTXT_CI_PI_SET(ci_start, CI_IDX) |
			 SQ_CTXT_CI_PI_SET(pi_start, PI_IDX);

	sq_ctxt->drop_mode_sp = SQ_CTXT_MODE_SET(0, SP_FLAG) |
				SQ_CTXT_MODE_SET(0, PKT_DROP);

	sq_ctxt->wq_pfn_hi_owner = SQ_CTXT_WQ_PAGE_SET(wq_page_pfn_hi, HI_PFN) |
				   SQ_CTXT_WQ_PAGE_SET(1, OWNER);

	sq_ctxt->wq_pfn_lo = wq_page_pfn_lo;

	sq_ctxt->global_sq_id =
		SQ_CTXT_GLOBAL_QUEUE_ID_SET(sq_id, GLOBAL_SQ_ID);

	/* Insert c-vlan in default */
	sq_ctxt->vlan_ceq_attr = SQ_CTXT_VLAN_CEQ_SET(0, CEQ_EN) |
				 SQ_CTXT_VLAN_CEQ_SET(1, INSERT_MODE);

	sq_ctxt->rsvd0 = 0;

	sq_ctxt->pref_ci_owner =
		SQ_CTXT_PREF_SET(CI_HIGN_IDX(ci_start), CI_HI) |
		SQ_CTXT_PREF_SET(1, OWNER);

	sq_ctxt->pref_wq_pfn_hi_ci =
		SQ_CTXT_PREF_SET(ci_start, CI_LOW) |
		SQ_CTXT_PREF_SET(wq_page_pfn_hi, WQ_PFN_HI);

	sq_ctxt->pref_wq_pfn_lo = wq_page_pfn_lo;

	sq_ctxt->wq_block_pfn_hi =
		SQ_CTXT_WQ_BLOCK_SET(wq_block_pfn_hi, PFN_HI);

	sq_ctxt->wq_block_pfn_lo = wq_block_pfn_lo;

	rte_mb();

	hinic3_cpu_to_be32(sq_ctxt, sizeof(*sq_ctxt));
}

void hinic3_rq_prepare_ctxt(struct hinic3_rxq *rq, struct hinic3_rq_ctxt *rq_ctxt)
{
	u64 wq_page_addr, wq_page_pfn, wq_block_pfn;
	u32 wq_page_pfn_hi, wq_page_pfn_lo, wq_block_pfn_hi, wq_block_pfn_lo;
	u16 pi_start, ci_start;
	u16 wqe_type = rq->wqe_type;
	u8 intr_disable;
	bool support_rq_sw_compact_cqe = false;

	/* RQ depth is in unit of 8 Bytes */
	ci_start = (u16)((rq->cons_idx & rq->q_mask) << wqe_type);
	pi_start = (u16)((rq->prod_idx & rq->q_mask) << wqe_type);

	/* Read the first page from hardware table */
	wq_page_addr = rq->queue_buf_paddr;

	wq_page_pfn = WQ_PAGE_PFN(wq_page_addr);
	wq_page_pfn_hi = upper_32_bits(wq_page_pfn);
	wq_page_pfn_lo = lower_32_bits(wq_page_pfn);

	/* Use 0-level CLA */
	wq_block_pfn = WQ_BLOCK_PFN(wq_page_addr);

	wq_block_pfn_hi = upper_32_bits(wq_block_pfn);
	wq_block_pfn_lo = lower_32_bits(wq_block_pfn);

	rq_ctxt->ci_pi = RQ_CTXT_CI_PI_SET(ci_start, CI_IDX) | RQ_CTXT_CI_PI_SET(pi_start, PI_IDX);

	/* RQ doesn't need ceq, msix_entry_idx set 1, but mask not enable */
	intr_disable = rq->dp_intr_en ? 0 : 1;
	support_rq_sw_compact_cqe = HINIC3_SUPPORT_RX_SW_COMPACT_CQE(rq->nic_dev);
	rq->nic_dev->cmdq_ops->prepare_rq_ctxt_ceq_and_prefetch(
		rq_ctxt, wqe_type, rq->msix_entry_idx, support_rq_sw_compact_cqe, intr_disable);

	/* Use 32Byte WQE with SGE for CQE in default */
	rq_ctxt->wq_pfn_hi_type_owner = RQ_CTXT_WQ_PAGE_SET(wq_page_pfn_hi, HI_PFN) |
		RQ_CTXT_WQ_PAGE_SET(1, OWNER);
	rq_ctxt->pi_paddr_hi = upper_32_bits(rq->pi_dma_addr);
	rq_ctxt->pi_paddr_lo = lower_32_bits(rq->pi_dma_addr);

	switch (wqe_type) {
	case HINIC3_EXTEND_RQ_WQE:
		/* Use 32Byte WQE with SGE for CQE */
		rq_ctxt->wq_pfn_hi_type_owner |= RQ_CTXT_WQ_PAGE_SET(0, WQE_TYPE);
		break;
	case HINIC3_NORMAL_RQ_WQE:
		/* Use 16Byte WQE with 32Bytes SGE for CQE */
		rq_ctxt->wq_pfn_hi_type_owner |= RQ_CTXT_WQ_PAGE_SET(2, WQE_TYPE);
		rq_ctxt->cqe_sge_len = RQ_CTXT_CQE_LEN_SET(1, CQE_LEN);
		break;
	case HINIC3_COMPACT_RQ_WQE:
		/* Use 8Byte WQE without SGE for CQE */
		rq_ctxt->wq_pfn_hi_type_owner |= RQ_CTXT_WQ_PAGE_SET(3, WQE_TYPE);
		if (HINIC3_SUPPORT_RX_SW_COMPACT_CQE(rq->nic_dev)) {
			rq_ctxt->cqe_sge_len |= RQ_CTXT_CQE_LEN_SET(RQ_CQE_AGGREGATE_NUM, MAX_COUNT);
			rq_ctxt->pi_paddr_hi = upper_32_bits(rq->rq_ci_paddr >> RQ_CI_ADDR_SHIFT);
			rq_ctxt->pi_paddr_lo = lower_32_bits(rq->rq_ci_paddr >> RQ_CI_ADDR_SHIFT);
		}
		break;
	default:
		PMD_DRV_LOG(INFO, "Invalid rq wqe type: %u", wqe_type);
	}

	rq_ctxt->wq_pfn_lo = wq_page_pfn_lo;

	rq_ctxt->pref_ci_owner = RQ_CTXT_PREF_SET(CI_HIGN_IDX(ci_start), CI_HI) |
				 RQ_CTXT_PREF_SET(1, OWNER);

	rq_ctxt->pref_wq_pfn_hi_ci = RQ_CTXT_PREF_SET(wq_page_pfn_hi, WQ_PFN_HI) |
				     RQ_CTXT_PREF_SET(ci_start, CI_LOW);

	rq_ctxt->pref_wq_pfn_lo = wq_page_pfn_lo;



	rq_ctxt->wq_block_pfn_hi = RQ_CTXT_WQ_BLOCK_SET(wq_block_pfn_hi, PFN_HI);

	rq_ctxt->wq_block_pfn_lo = wq_block_pfn_lo;
	rte_mb();

	hinic3_cpu_to_be32(rq_ctxt, sizeof(*rq_ctxt));
}

static int hinic3_cmd_modify_queue_ctx(struct hinic3_nic_dev *nic_dev,
				          struct hinic3_qp_ctxt_block_htn *ctxt_block,
					  int qid, int fd)
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
	err = ioctl(fd, 0, &msg_to_kernel);
	return err;
}

static int init_sq_ctxts(struct hinic3_nic_dev *nic_dev)
{
	struct hinic3_cmd_buf *cmd_buf = NULL;
	u64 out_param = 0;
	u16 q_id, max_ctxts;
	int err = 0;
	u8 cmd;

	cmd_buf = hinic3_alloc_cmd_buf(nic_dev->hwdev);
	if (!cmd_buf) {
		PMD_DRV_LOG(ERR, "Allocate cmd buf for sq ctx failed");
		return -ENOMEM;
	}

	q_id = 0;
	while (q_id < nic_dev->num_sqs) {
		max_ctxts = (nic_dev->num_sqs - q_id) > HINIC3_Q_CTXT_MAX ?
					HINIC3_Q_CTXT_MAX : (nic_dev->num_sqs - q_id);

		cmd = nic_dev->cmdq_ops->prepare_cmd_buf_qp_context_multi_store(nic_dev, cmd_buf,
					HINIC3_QP_CTXT_TYPE_SQ, q_id, max_ctxts);
		rte_mb();
		if (!IS_QPOOL_MODE(nic_dev))
			err = hinic3_cmdq_direct_resp(nic_dev->hwdev, HINIC3_MOD_L2NIC, cmd, cmd_buf, &out_param, 0);
		else
			err = hinic3_cmd_modify_queue_ctx(nic_dev, cmd_buf->buf, nic_dev->txqs[q_id]->local_qid, nic_dev->fd);
		if (err || out_param != 0) {
			PMD_DRV_LOG(ERR, "Set SQ ctxts failed, err: %d, out_param: %"PRIu64, err, out_param);

			err = -EFAULT;
			break;
		}
		q_id += max_ctxts;
	}

	hinic3_free_cmd_buf(cmd_buf);
	return err;
}


static int init_rq_ctxts(struct hinic3_nic_dev *nic_dev)
{
	struct hinic3_cmd_buf *cmd_buf = NULL;
	u64 out_param = 0;
	u16 q_id, max_ctxts;
	u8 cmd;
	int err = 0;

	cmd_buf = hinic3_alloc_cmd_buf(nic_dev->hwdev);
	if (!cmd_buf) {
		PMD_DRV_LOG(ERR, "Allocate cmd buf for rq ctx failed");
		return -ENOMEM;
	}

	q_id = 0;
	while (q_id < nic_dev->num_rqs) {
		max_ctxts = (nic_dev->num_rqs - q_id) > HINIC3_Q_CTXT_MAX ?
			    HINIC3_Q_CTXT_MAX : (nic_dev->num_rqs - q_id);
		cmd = nic_dev->cmdq_ops->prepare_cmd_buf_qp_context_multi_store(nic_dev, cmd_buf,
					HINIC3_QP_CTXT_TYPE_RQ, q_id, max_ctxts);
		rte_mb();
		if (!IS_QPOOL_MODE(nic_dev))
			err = hinic3_cmdq_direct_resp(nic_dev->hwdev, HINIC3_MOD_L2NIC, cmd, cmd_buf, &out_param, 0);
		else
			err = hinic3_cmd_modify_queue_ctx(nic_dev, cmd_buf->buf, nic_dev->rxqs[q_id]->local_qid, nic_dev->fd);
		if (err || out_param != 0) {
			PMD_DRV_LOG(ERR, "Set RQ ctxts failed, err: %d, out_param: %"PRIu64, err, out_param);
			err = -EFAULT;
			break;
		}
		q_id += max_ctxts;
	}

	hinic3_free_cmd_buf(cmd_buf);
	return err;
}

static int clean_queue_offload_ctxt(struct hinic3_nic_dev *nic_dev,
				    enum hinic3_qp_ctxt_type ctxt_type)
{
	struct hinic3_cmd_buf *cmd_buf;
	u64 out_param = 0;
	u8 cmd;
	int err;

	cmd_buf = hinic3_alloc_cmd_buf(nic_dev->hwdev);
	if (!cmd_buf) {
		PMD_DRV_LOG(ERR, "Allocate cmd buf for LRO/TSO space failed");
		return -ENOMEM;
	}

	cmd = nic_dev->cmdq_ops->prepare_cmd_buf_clean_tso_lro_space(nic_dev, cmd_buf, ctxt_type);

	err = hinic3_cmdq_direct_resp(nic_dev->hwdev, HINIC3_MOD_L2NIC, cmd, cmd_buf, &out_param, 0);
	if ((err) || (out_param)) {
		PMD_DRV_LOG(ERR, "Clean queue offload ctxts failed, "
			    "err: %d, out_param: %"PRIu64, err, out_param); /*lint !e10*/
		err = -EFAULT;
	}

	hinic3_free_cmd_buf(cmd_buf);
	return err;
}

static int clean_qp_offload_ctxt(struct hinic3_nic_dev *nic_dev)
{
	/* Clean LRO/TSO context space */
	return (clean_queue_offload_ctxt(nic_dev, HINIC3_QP_CTXT_TYPE_SQ) ||
		clean_queue_offload_ctxt(nic_dev, HINIC3_QP_CTXT_TYPE_RQ));
}

void hinic3_get_func_rx_buf_size(void *dev)
{
	struct hinic3_nic_dev *nic_dev = (struct hinic3_nic_dev *)dev;
	struct hinic3_rxq *rxq = NULL;
	u16 q_id;
	u16 buf_size = 0;

	for (q_id = 0; q_id < nic_dev->num_rqs; q_id++) {
		rxq = nic_dev->rxqs[q_id];

		if (rxq == NULL)
			continue;

		if (q_id == 0)
			buf_size = rxq->buf_len;

		buf_size = buf_size > rxq->buf_len ? rxq->buf_len : buf_size;
	}

	nic_dev->rx_buff_len = buf_size;
}

#define HINIC3_RX_CQE_TIMER_LOOP 		8
#define HINIC3_RX_CQE_COALESCE_NUM		7

int hinic3_init_rq_cqe_ctxts(struct hinic3_nic_dev *nic_dev)
{
	struct hinic3_hwdev *hwdev = NULL;
	struct hinic3_rxq *rxq = NULL;
	struct hinic3_rq_cqe_ctx cqe_ctx = {0};
	rte_iova_t rq_ci_paddr;
	u16 out_size = sizeof(cqe_ctx);
	u16 q_id = 0;
	u16 cmd;
	int err;

	if (!nic_dev)
		return -EINVAL;

	hwdev = nic_dev->hwdev;

	if (hinic3_get_driver_feature(nic_dev) & NIC_F_HTN_CMDQ)
		cmd = HINIC3_NIC_CMD_SET_RQ_CI_CTX_HTN;
	else {
		cmd = HINIC3_NIC_CMD_SET_RQ_CI_CTX;
	}

	while (q_id < nic_dev->num_rqs) {
		rxq = nic_dev->rxqs[q_id];
		if (rxq->wqe_type == HINIC3_COMPACT_RQ_WQE) {
			rq_ci_paddr = rxq->rq_ci_paddr >> RQ_CI_ADDR_SHIFT;
			cqe_ctx.ci_addr_hi = upper_32_bits(rq_ci_paddr);
			cqe_ctx.ci_addr_lo = lower_32_bits(rq_ci_paddr);
			cqe_ctx.threshold_cqe_num = HINIC3_RX_CQE_COALESCE_NUM;
			cqe_ctx.timer_loop = HINIC3_RX_CQE_TIMER_LOOP;
		} else {
			cqe_ctx.threshold_cqe_num = 0;
			cqe_ctx.timer_loop = 0;
		}

		cqe_ctx.cqe_type = (rxq->wqe_type == HINIC3_COMPACT_RQ_WQE);
		cqe_ctx.msix_entry_idx = rxq->msix_entry_idx;
		cqe_ctx.rq_id = hwdev->bifur_mode == HINIC3_BIFUR_MODE_QPOOL ? 
				rxq->local_qid : q_id;

		err = l2nic_msg_to_mgmt_sync(hwdev, cmd,
					     &cqe_ctx, sizeof(cqe_ctx),
					     &cqe_ctx, &out_size);
		if (err || !out_size || cqe_ctx.msg_head.status) {
			PMD_DRV_LOG(ERR, "Set rq cqe context failed, qid: %d, err: %d, status: 0x%x, out_size: 0x%x",
				    q_id, err, cqe_ctx.msg_head.status, out_size);
			return -EFAULT;
		}
		q_id++;
	}

	return 0;
}

/* Init qps ctxt and set sq ci attr and arm all sq */
int hinic3_init_qp_ctxts(void *dev)
{
	struct hinic3_nic_dev *nic_dev = NULL;
	struct hinic3_hwdev *hwdev = NULL;
	struct hinic3_sq_attr sq_attr;
	u32 rq_depth = 0;
	u32 sq_depth = 0;
	u16 q_id;
	int err;

	if (!dev)
		return -EINVAL;

	nic_dev = (struct hinic3_nic_dev *)dev;
	hwdev = nic_dev->hwdev;

	err = init_sq_ctxts(nic_dev);
	if (err) {
		PMD_DRV_LOG(ERR, "Init SQ ctxts failed");
		return err;
	}

	err = init_rq_ctxts(nic_dev);
	if (err) {
		PMD_DRV_LOG(ERR, "Init RQ ctxts failed");
		return err;
	}
	if (!IS_QPOOL_MODE(nic_dev)) {
		err = clean_qp_offload_ctxt(nic_dev);
		if (err) {
			PMD_DRV_LOG(ERR, "Clean qp offload ctxts failed");
			return err;
		}

		if (nic_dev->num_rqs != 0)
			rq_depth = ((u32)nic_dev->rxqs[0]->q_depth) << nic_dev->rxqs[0]->wqe_type;

		if (nic_dev->num_sqs != 0)
			sq_depth = nic_dev->txqs[0]->q_depth;

		err = hinic3_set_root_ctxt(hwdev, rq_depth, sq_depth, nic_dev->rx_buff_len);
		if (err) {
			PMD_DRV_LOG(ERR, "Set root context failed");
			return err;
		}
	}

	for (q_id = 0; q_id < nic_dev->num_sqs; q_id++) {
		sq_attr.ci_dma_base = nic_dev->txqs[q_id]->ci_dma_base >> SQ_CI_ADDR_SHIFT;
		sq_attr.pending_limit = HINIC3_DEAULT_TX_CI_PENDING_LIMIT;
		sq_attr.coalescing_time = HINIC3_DEAULT_TX_CI_COALESCING_TIME;
		sq_attr.intr_en = 0;
		sq_attr.intr_idx = 0; /* Tx doesn't need intr */
		sq_attr.l2nic_sqn = hwdev->bifur_mode == HINIC3_BIFUR_MODE_QPOOL ? 
				    nic_dev->txqs[q_id]->local_qid : q_id;
		sq_attr.dma_attr_off = 0;
		err = hinic3_set_ci_table(hwdev, &sq_attr);
		if (err) {
			PMD_DRV_LOG(ERR, "Set ci table failed");
			goto set_cons_idx_table_err;
		}
	}

	if (HINIC3_SUPPORT_RX_HW_COMPACT_CQE(nic_dev)) {
		/* Init Rxq CQE context. */
		err = hinic3_init_rq_cqe_ctxts(nic_dev);
		if (err) {
			PMD_DRV_LOG(ERR, "Set rq cqe context failed");
			goto set_cqe_ctx_fail;
		}
	}

	return 0;

set_cqe_ctx_fail:
set_cons_idx_table_err:
	hinic3_clean_root_ctxt(hwdev);
	return err;
}

int
hinic3_set_rq_enable(struct hinic3_nic_dev *nic_dev, u16 q_id, bool enable)
{
	struct hinic3_hwdev *hwdev = NULL;
	struct hinic3_rq_enable msg;
	u16 out_size = sizeof(msg);
	int err;

	if (!nic_dev)
		return -EINVAL;

	hwdev = nic_dev->hwdev;

	memset(&msg, 0, sizeof(msg));
	msg.rq_enable = enable;
	msg.rq_id = q_id;
	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC3_NIC_CMD_SET_RQ_ENABLE_HTN,
				     &msg, sizeof(msg), &msg, &out_size);
	if (err || !out_size || msg.msg_head.status) {
		PMD_DRV_LOG(ERR, "Set rq enable failed, qid: %u, enable: %d, err: %d, status: 0x%x, out_size: 0x%x",
			    q_id, enable, err, msg.msg_head.status, out_size);
		return -EFAULT;
	}

	return 0;
}

void hinic3_free_qp_ctxts(void *hwdev)
{
	if (!hwdev)
		return;

	hinic3_clean_root_ctxt(hwdev);
}

void hinic3_update_driver_feature(void *dev, u64 s_feature)
{
	struct hinic3_nic_dev *nic_dev = NULL;

	if (!dev)
		return;

	nic_dev = (struct hinic3_nic_dev *)dev;
	nic_dev->feature_cap = s_feature;

	PMD_DRV_LOG(INFO, "Update nic feature to 0x%lx\n",
		    nic_dev->feature_cap);
}

u64 hinic3_get_driver_feature(void *dev)
{
	struct hinic3_nic_dev *nic_dev = NULL;

	nic_dev = (struct hinic3_nic_dev *)dev;

	return nic_dev->feature_cap;
}

