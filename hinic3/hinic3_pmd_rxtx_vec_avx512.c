/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2026 Huawei Technologies Co., Ltd
 */

/*
 * hinic3 compact CQE 收包路径的 AVX512 实现。
 *
 * 本文件只实现 NEON 路径（hinic3_pmd_rxtx_vec_neon.c）中 compact CQE 收包
 * 已有的功能：批量 mbuf 分配 / rearm、内嵌 CQE 地址计算、128bit 字段重排
 * 填充 rx_descriptor_fields1、RXDONE 连续完成计数、data_off 跳过内嵌 CQE、
 * 下一批预取以及 cons_idx/delta/packets 统计。不引入 offload 补全、空队列
 * 退避、诊断日志、8 包批、hw_ci 门控等扩展。
 *
 * compact CQE 内嵌在每个报文缓冲区的 buf_addr + data_off 处（sp560 DMA
 * 对齐模式下在其下方 16B），4 个 CQE 地址不连续，因此逐包 load 16B；
 * 字段重排用 _mm_shuffle_epi8（对应 NEON 的 vqtbl1q_u8），完成检查用
 * AVX512 掩码 + __builtin_ctz（对应 NEON 的 sign-extend + ctzl）。
 * 收包后必须清 CQE 的 RXDONE 握手位（与标量路径 rx_cqe->status = 0
 * 一致），否则硬件写新 CQE 前轮询会把残留 DD 误判为新包完成。
 *
 * 所有使用向量指令的函数用函数级 target("avx512f") 限定，构建系统不需要
 * 给本文件加 -mavx512f；只有运行期确认 CPU 支持后才会选中这条路径。
 */

#include <rte_ether.h>
#include <rte_mbuf.h>
#include <rte_prefetch.h>

#include "base/hinic3_compat.h"
#include "base/hinic3_pmd_hwif.h"
#include "base/hinic3_pmd_hwdev.h"
#include "base/hinic3_pmd_wq.h"
#include "base/hinic3_pmd_mgmt.h"
#include "base/hinic3_pmd_nic_cfg.h"
#include "hinic3_pmd_nic_io.h"
#include "hinic3_pmd_dcb.h"
#include "hinic3_pmd_ethdev.h"
#include "hinic3_pmd_tx.h"
#include "hinic3_pmd_rx.h"

#include <immintrin.h>

/* 函数级指令集限定：只有探测到 AVX512F 后才会真正执行到下面的向量指令 */
#define HINIC3_AVX512_TARGET	__attribute__((target("avx512f")))

static u32
hinic3_rx_alloc_mbuf_bulk_vec_avx(struct hinic3_rxq *rxq,
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

static int
hinic3_rearm_rxq_mbuf_vec_avx(struct hinic3_rxq *rxq)
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

	rearm_wqebbs = hinic3_rx_alloc_mbuf_bulk_vec_avx(rxq, rearm_mbufs,
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
			/* compact CQE 场景 wqe_type 恒为 COMPACT_RQ_WQE，
			 * 与 normal_wqe 的地址字段在 union 前 8 字节重叠；
			 * sp560 DMA 对齐时 WQE 地址同样要下移 16B，与标量
			 * rearm 一致。 */
			if (is_sp560_nic(nic_dev) && rxq->rx_dma_align)
				dma_addr -= HINIC3_RX_DMA_ALIGN_CQE_OFFSET;
			rq_wqe->compact_wqe.buf_hi_addr =
				hinic3_hw_be32(upper_32_bits(dma_addr));
			rq_wqe->compact_wqe.buf_lo_addr =
				hinic3_hw_be32(lower_32_bits(dma_addr));
		}

		rq_wqe = (struct hinic3_rq_wqe *)((u64)rq_wqe + rxq->wqebb_size);
	}
	rxq->prod_idx += rearm_wqebbs;
	rxq->delta -= rearm_wqebbs;

	hinic3_write_db(rxq->db_addr, rxq->local_qid, 0, RQ_CFLAG_DP,
		((pi + rearm_wqebbs) & rxq->q_mask) << rxq->wqe_type);

	return 0;
}

static inline uint8_t *
hinic3_vec_compact_cqe_addr(struct hinic3_rxq *rxq, struct rte_mbuf *mbuf)
{
	uint8_t *addr;

#ifdef DPDK_21_11
	addr = (uint8_t *)rte_mbuf_buf_addr(mbuf, mbuf->pool) + mbuf->data_off;
#else
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
	addr = (uint8_t *)rte_mbuf_buf_addr(mbuf, mbuf->pool) + mbuf->data_off;
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
#endif

	/*
	 * sp560 NIC with DMA alignment enabled: rearm shifts the WQE DMA
	 * address below the aligned address while data_off still points at
	 * the aligned address, so the CQE sits 16 bytes below data_off
	 * (same correction as the scalar path).
	 */
	if (is_sp560_nic(rxq->nic_dev) && rxq->rx_dma_align)
		addr -= HINIC3_RX_DMA_ALIGN_CQE_OFFSET;

	return addr;
}

/*
 * 批量处理 4 个内嵌 compact CQE。
 *
 * 16B CQE 的字节布局（与 NEON 侧 shuf_mask 逐字节一致）：
 *   dw0: pkt_len=b3,b2 | cqe_len=b2 bit7 | rxdone=b0 bit7
 *   dw1: rss hash = b7,b6,b5,b4
 *   dw2: vlan_tci = b10,b9
 * 重排后写入 rx_descriptor_fields1 的前 16B：
 *   packet_type=0 | pkt_len | data_len | vlan_tci | rss
 */
static inline HINIC3_AVX512_TARGET u16
hinic3_recv_burst_compact_cqe_vec_avx(struct hinic3_rxq *rxq,
				      struct rte_mbuf **rx_pkts, u16 nb_pkts)
{
	struct hinic3_rx_info *rx_info;
	u16 sw_ci = MASKED_QUEUE_IDX(rxq, rxq->cons_idx);
	u16 pos, valid_pkts, nb_rx = 0;
	u32 i;

	/* ptype=0 | pkt_len=[b3,b2,0,0] | data_len=[b3,b2]
	 * | vlan_tci=[b10,b9] | rss=[b7,b6,b5,b4] */
	const __m128i shuf_mask = _mm_setr_epi8(
		0xFF, 0xFF, 0xFF, 0xFF,
		3, 2, 0xFF, 0xFF,
		3, 2,
		10, 9,
		7, 6, 5, 4);

	rx_info = &rxq->rx_info[sw_ci];

	for (pos = 0; pos < nb_pkts; pos += HINIC3_DEFAULT_DESCS_PER_LOOP) {
		__m128i cqe[HINIC3_DEFAULT_DESCS_PER_LOOP];
		__m128i pkt_mb;
		u32 status_arr[HINIC3_DEFAULT_DESCS_PER_LOOP];
		__mmask16 done_mask;
		u32 not_done;

		/* A. 4 个 mbuf 指针搬到调用者数组（4*8B，两条 128bit 搬移） */
		_mm_storeu_si128((__m128i *)&rx_pkts[pos],
			_mm_loadu_si128((const __m128i *)&rx_info[pos]));
		_mm_storeu_si128((__m128i *)&rx_pkts[pos + 2],
			_mm_loadu_si128((const __m128i *)&rx_info[pos + 2]));

		/* B. 逐包 load 16B 内嵌 CQE（地址不连续） */
		cqe[0] = _mm_loadu_si128((const __m128i *)
			hinic3_vec_compact_cqe_addr(rxq, rx_pkts[pos + 0]));
		cqe[1] = _mm_loadu_si128((const __m128i *)
			hinic3_vec_compact_cqe_addr(rxq, rx_pkts[pos + 1]));
		cqe[2] = _mm_loadu_si128((const __m128i *)
			hinic3_vec_compact_cqe_addr(rxq, rx_pkts[pos + 2]));
		cqe[3] = _mm_loadu_si128((const __m128i *)
			hinic3_vec_compact_cqe_addr(rxq, rx_pkts[pos + 3]));

		/* acquire 后重读 status dword：看到新 DD 位即保证数据也是新的
		 * （i40e/iavf 的 acquire + reload 模式，与 NEON 侧一致） */
#ifdef DPDK_20_11
		rte_atomic_thread_fence(__ATOMIC_ACQUIRE);
#else
		__atomic_thread_fence(__ATOMIC_ACQUIRE);
#endif
		status_arr[0] = *(volatile u32 *)
			hinic3_vec_compact_cqe_addr(rxq, rx_pkts[pos + 0]);
		status_arr[1] = *(volatile u32 *)
			hinic3_vec_compact_cqe_addr(rxq, rx_pkts[pos + 1]);
		status_arr[2] = *(volatile u32 *)
			hinic3_vec_compact_cqe_addr(rxq, rx_pkts[pos + 2]);
		status_arr[3] = *(volatile u32 *)
			hinic3_vec_compact_cqe_addr(rxq, rx_pkts[pos + 3]);

		/* 把重读的 dw0 回写进 cqe 向量：pkt_len/data_len 都在 dw0，
		 * 保证 shuffle 输出与 status 同批新鲜（对应 NEON 的
		 * vld1q_lane_u32 reload）。 */
		for (i = 0; i < HINIC3_DEFAULT_DESCS_PER_LOOP; i++)
			cqe[i] = _mm_blend_epi16(cqe[i],
					_mm_cvtsi32_si128((int)status_arr[i]),
					0x03);

		/* C. 用 AVX512 掩码批量取 4 个 RXDONE（dw0 原始 byte0 bit7），
		 * __builtin_ctz 定位第一个未完成的槽位，得到连续完成包数。 */
		done_mask = _mm512_test_epi32_mask(
			_mm512_set_epi32(0, 0, 0, 0,
					 0, 0, 0, 0,
					 0, 0, 0, 0,
					 status_arr[3], status_arr[2],
					 status_arr[1], status_arr[0]),
			_mm512_set1_epi32(BIT(7)));
		not_done = (~(u32)done_mask) & 0xF;
		valid_pkts = not_done ? __builtin_ctz(not_done) :
			     HINIC3_DEFAULT_DESCS_PER_LOOP;

		/* D. 逐包 128bit shuffle + 16B store 填 mbuf 描述符字段 */
		for (i = 0; i < HINIC3_DEFAULT_DESCS_PER_LOOP; i++) {
			pkt_mb = _mm_shuffle_epi8(cqe[i], shuf_mask);
			_mm_storeu_si128((__m128i *)
				&rx_pkts[pos + i]->rx_descriptor_fields1, pkt_mb);
		}

		/* E. 跳过内嵌 CQE 并清 RXDONE 握手位。
		 * cqe_len 位段在 be32 后的 bit23，对应 raw dword 的 bit15
		 * （BIT(23) 在 raw 上命中的是 pkt_len 的最高位）：cqe_len=1
		 * 时 8B，否则 16B。
		 * RXDONE 是"硬件置位、软件清零"的握手位，与标量路径
		 * rx_cqe->status = 0 一致；不清零的话硬件写新 CQE 前
		 * 轮询会把残留 DD 当作新包完成，收到垃圾数据。
		 * CQE 地址必须在 data_off 被修改之前计算。 */
		for (i = 0; i < valid_pkts; i++) {
			struct rte_mbuf *mb = rx_pkts[pos + i];
			volatile u32 *cqe_status = (volatile u32 *)
				hinic3_vec_compact_cqe_addr(rxq, mb);
			u16 data_offset = (status_arr[i] & BIT(15)) ? 8 : 16;

			mb->data_off += data_offset;
			*cqe_status = 0;
		}

		/* F. 预取下一批的 4 个内嵌 CQE（与 NEON 的窗口一致） */
		if (likely(pos + 2 * HINIC3_DEFAULT_DESCS_PER_LOOP < nb_pkts)) {
			rte_prefetch_non_temporal(
				hinic3_vec_compact_cqe_addr(rxq,
					rx_info[pos + 4].mbuf));
			rte_prefetch_non_temporal(
				hinic3_vec_compact_cqe_addr(rxq,
					rx_info[pos + 5].mbuf));
			rte_prefetch_non_temporal(
				hinic3_vec_compact_cqe_addr(rxq,
					rx_info[pos + 6].mbuf));
			rte_prefetch_non_temporal(
				hinic3_vec_compact_cqe_addr(rxq,
					rx_info[pos + 7].mbuf));
		}

		nb_rx += valid_pkts;
		if (valid_pkts < HINIC3_DEFAULT_DESCS_PER_LOOP)
			break;
	}

	rxq->cons_idx += nb_rx;
	rxq->delta += nb_rx;
	rxq->rxq_stats.packets += nb_rx;

	return nb_rx;
}

HINIC3_AVX512_TARGET
u16
hinic3_recv_pkts_compact_cqe_vec_avx(void *rx_queue, struct rte_mbuf **rx_pkts,
				     u16 nb_pkts)
{
	struct hinic3_rxq *rxq = rx_queue;
	struct rte_mbuf *rxm;
	volatile struct hinic3_rq_cqe *rx_cqe;
	u16 sw_ci;
	u32 status;
	u16 nb_rx = 0;

	sw_ci = MASKED_QUEUE_IDX(rxq, rxq->cons_idx);
	rxm = rxq->rx_info[sw_ci].mbuf;

	/* 预取第一个内嵌 CQE（DMA 写入，非时间局部性） */
	rx_cqe = (volatile struct hinic3_rq_cqe *)
		hinic3_vec_compact_cqe_addr(rxq, rxm);
	rte_prefetch_non_temporal(rx_cqe);

	nb_pkts = RTE_ALIGN_FLOOR(nb_pkts, HINIC3_DEFAULT_DESCS_PER_LOOP);

	if (rxq->delta > rxq->rx_free_thresh)
		hinic3_rearm_rxq_mbuf_vec_avx(rxq);

	/* 检查第一个 CQE 完成（acquire）。compact RXDONE 是 dw0 byte0 bit7。 */
	status = (u32)__atomic_load_n(&rx_cqe->status, __ATOMIC_ACQUIRE);
	if (unlikely(!((status >> 7) & 1)))
		return 0;

	rte_prefetch0(rxq->rx_info[sw_ci + 0].mbuf);
	rte_prefetch0(rxq->rx_info[sw_ci + 1].mbuf);
	rte_prefetch0(rxq->rx_info[sw_ci + 2].mbuf);
	rte_prefetch0(rxq->rx_info[sw_ci + 3].mbuf);

	while (nb_pkts > 0) {
		u16 n, ret;

		n = RTE_MIN(nb_pkts, HINIC3_DEFAULT_RX_BURST);
		ret = hinic3_recv_burst_compact_cqe_vec_avx(rxq,
							    &rx_pkts[nb_rx], n);

		nb_pkts -= ret;
		nb_rx += ret;

		if (ret < n)
			break;

		if (rxq->delta > rxq->rx_free_thresh)
			hinic3_rearm_rxq_mbuf_vec_avx(rxq);
	}

	return nb_rx;
}
