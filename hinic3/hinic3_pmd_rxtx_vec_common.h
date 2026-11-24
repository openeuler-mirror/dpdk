/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2026 Huawei Technologies Co., Ltd
 */

#ifndef _HINIC3_PMD_RXTX_VEC_COMMON_H_
#define _HINIC3_PMD_RXTX_VEC_COMMON_H_

#include <rte_mbuf.h>

#include "base/hinic3_compat.h"
#include "hinic3_pmd_rx.h"
#include "hinic3_pmd_nic_io.h"

/*
 * Helpers shared by the scalar RX path and the vector RX paths (AVX512 /
 * NEON), following the i40e/iavf *_rxtx_vec_common.h pattern: everything
 * is static inline so each translation unit keeps its own inlined copy
 * and no cross-TU call enters the per-burst hot path.
 */

/*
 * Compact CQE cqe_len bit position in the raw (in-memory, little-endian
 * loaded) dw0 of the CQE status word. The status word is big-endian and
 * the scalar path reads it through be32_to_cpu() with host-order shifts
 * (RQ_COMPACT_CQE_STATUS_CQE_LEN_SHIFT == 23); be32_to_cpu() is a pure
 * byte permutation (host byte j = raw byte 3 - j, bit order inside each
 * byte preserved), so host bit 23 (byte 2, bit 7) maps to raw bit 15
 * (byte 1, bit 7). Both vector paths test the raw value directly, so
 * they must all use this constant; testing BIT(23) on the raw word hits
 * pkt_len's MSB instead.
 */
#define HINIC3_COMPACT_CQE_LEN_BIT	15

/*
 * Address of the compact CQE integrated at the head of the packet buffer.
 * rte_mbuf_buf_addr() is deprecated since DPDK 20.11 (mbuf->pool is always
 * valid in this driver), silence the warning for the direct use here.
 */
static inline uint8_t *
hinic3_vec_compact_cqe_addr(struct rte_mbuf *mbuf)
{
#ifdef DPDK_21_11
	return (uint8_t *)rte_mbuf_buf_addr(mbuf, mbuf->pool) + mbuf->data_off;
#else
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
	return (uint8_t *)rte_mbuf_buf_addr(mbuf, mbuf->pool) + mbuf->data_off;
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
#endif
}

/*
 * Bulk-allocate mbufs straight into the rx_info window starting at the
 * rearm position. On allocation failure report zero so the caller skips
 * the rearm round instead of partially refilling the ring.
 */
static inline u32
hinic3_rx_alloc_mbuf_bulk(struct hinic3_rxq *rxq,
			  struct rte_mbuf **mbufs, u32 exp_mbuf_cnt)
{
	u32 avail_cnt;
	int err;

	err = rte_pktmbuf_alloc_bulk(rxq->mb_pool, mbufs, exp_mbuf_cnt);
	if (likely(err == 0)) {
		avail_cnt = exp_mbuf_cnt;
	} else {
		avail_cnt = 0;
		rxq->rxq_stats.rx_nombuf += exp_mbuf_cnt;
	}
#ifdef HINIC3_XSTAT_MBUF_USE
	rxq->rxq_stats.alloc_mbuf += avail_cnt;
#endif
	return avail_cnt;
}

/*
 * Shared vector rearm: bulk-allocate mbufs for the free WQEBB window,
 * fold rx_dma_align into data_off (offset mode shifts by exactly
 * rx_dma_align bytes, alignment mode rounds up), write the buffer
 * address into the WQE and ring the doorbell with the hardware queue
 * id (local_qid), which can differ from the DPDK q_id when the queue
 * is allocated by the kernel.
 *
 * In compact CQE mode the WQE keeps only the buffer address: the union
 * places compact_wqe.buf_hi_addr/buf_lo_addr at the same offsets as the
 * first two words of normal_wqe, so writing compact_wqe covers both.
 */
static inline int
hinic3_rearm_rxq_mbuf_vec(struct hinic3_rxq *rxq)
{
	struct hinic3_rq_wqe *rq_wqe = NULL;
	volatile struct hinic3_rq_cqe *rx_cqe = NULL;
	struct rte_mbuf **rearm_mbufs;
	u32 i, free_wqebbs, rearm_wqebbs, exp_wqebbs;
	rte_iova_t dma_addr;
	rte_iova_t align_dma_addr;
	u16 pi;
	struct hinic3_nic_dev *nic_dev = rxq->nic_dev;

	free_wqebbs = rxq->delta - 1;
	if (unlikely(free_wqebbs < rxq->rx_free_thresh))
		return -ENOMEM;

	pi = MASKED_QUEUE_IDX(rxq, rxq->prod_idx);
	rearm_mbufs = (struct rte_mbuf **)(&rxq->rx_info[pi]);
	rx_cqe = &rxq->rx_cqe[pi];

	exp_wqebbs = rxq->q_depth - pi;
	if (free_wqebbs < exp_wqebbs)
		exp_wqebbs = free_wqebbs;

	rearm_wqebbs = hinic3_rx_alloc_mbuf_bulk(rxq, rearm_mbufs,
						 exp_wqebbs);
	if (unlikely(rearm_wqebbs == 0))
		return -ENOMEM;

	/* compact CQE 内嵌在报文缓冲区头部，独立的 CQE ring 未分配，不能碰 */
	if (rxq->rx_cqe)
		for (i = 0; i < rearm_wqebbs; i++)
			rx_cqe[i].status = 0;

	rq_wqe = NIC_WQE_ADDR(rxq, pi);
	for (i = 0; i < rearm_wqebbs; i++) {
		dma_addr = rte_mbuf_data_iova_default(rearm_mbufs[i]);
		if (rxq->rx_dma_align) {
			if (hinic3_rx_dma_align_is_offset(rxq->rx_dma_align))
				align_dma_addr = dma_addr + rxq->rx_dma_align;
			else
				align_dma_addr = RTE_ALIGN(dma_addr,
							   rxq->rx_dma_align);
			rearm_mbufs[i]->data_off = (u16)(RTE_PKTMBUF_HEADROOM +
					(align_dma_addr - dma_addr));
			dma_addr = align_dma_addr;
		} else {
			rearm_mbufs[i]->data_off = RTE_PKTMBUF_HEADROOM;
		}
		rearm_mbufs[i]->port = rxq->port_id;
		rearm_mbufs[i]->ol_flags = 0;

		if (rxq->wqe_type == HINIC3_EXTEND_RQ_WQE) {
			rq_wqe->extend_wqe.buf_desc.sge.hi_addr =
				hinic3_hw_be32(upper_32_bits(dma_addr));
			rq_wqe->extend_wqe.buf_desc.sge.lo_addr =
				hinic3_hw_be32(lower_32_bits(dma_addr));
			rq_wqe->extend_wqe.buf_desc.sge.len =
				nic_dev->rx_buff_len;
		} else {
			rq_wqe->compact_wqe.buf_hi_addr =
				hinic3_hw_be32(upper_32_bits(dma_addr));
			rq_wqe->compact_wqe.buf_lo_addr =
				hinic3_hw_be32(lower_32_bits(dma_addr));
		}

		rq_wqe = (struct hinic3_rq_wqe *)((u64)rq_wqe +
						  rxq->wqebb_size);
	}
	rxq->prod_idx += rearm_wqebbs;
	rxq->delta -= rearm_wqebbs;

	hinic3_write_db(rxq->db_addr, rxq->local_qid, 0, RQ_CFLAG_DP,
		((pi + rearm_wqebbs) & rxq->q_mask) << rxq->wqe_type);

	return 0;
}

#endif /* _HINIC3_PMD_RXTX_VEC_COMMON_H_ */
