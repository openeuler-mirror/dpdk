/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2026 Huawei Technologies Co., Ltd
 */

/*
 * hinic3 compact CQE 收包路径的 AVX512 实现。
 *
 * 本文件只实现 NEON 路径（hinic3_pmd_rxtx_vec_neon.c）中 compact CQE 收包
 * 已有的功能：批量 mbuf 分配 / rearm、内嵌 CQE 地址计算、128bit 字段重排
 * 填充 rx_descriptor_fields1、RXDONE 连续完成计数、data_off 跳过内嵌 CQE、
 * 下一批预取以及 cons_idx/delta/packets 统计。另按实测结论引入了 hw_ci
 * 门控（与标量路径同口径的硬件确认前缀收窄）。另按需求实现了 checksum
 * offload：compact csum_err（raw dw0 bit[12:11]）→ ol_flags，语义对齐
 * 标量 hinic3_rx_csum，rx_csum_en 关闭时为零开销 no-op；并补置 RSS
 * HASH flag（hash 值本就由 shuffle 写入 hash.rss）。另实现了 ptype 查表
 * （desc-ready 同款方案 A）：从 dw0 提取 12 位 ptype 索引查 ptype_tbl，
 * _mm_insert_epi32 插进 lane0 后随描述符整块宽存。仍不引入 vlan offload、
 * 空队列退避、诊断日志、8 包批等扩展。
 *
 * compact CQE 内嵌在每个报文缓冲区的 buf_addr + data_off 处（sp560 DMA
 * 对齐模式下在其下方 16B），4 个 CQE 地址不连续，因此逐包 load 16B；
 * 字段重排用 _mm_shuffle_epi8（对应 NEON 的 vqtbl1q_u8），完成检查对
 * 每次 load 到的 dw0 做标量位测试（RXDONE = raw bit7）后用
 * __builtin_ctz 取连续完成数：RXDONE、pkt_len、cqe_len 同在 dw0，
 * 一次 load 同时得到判定与数据，x86 TSO 下无需 fence + 重读。
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

#define HINIC3_AVX_COMPACT_RXDONE_BIT	7

/*
 * compact CQE 的 csum_err 位段：主机序 dw0 的 bit[20:19]
 * （RQ_COMPACT_CQE_STATUS_CSUM_ERR_SHIFT=19, MASK=0x3），换算到 raw
 * 小端 dword 即 bit[12:11]（H.bit(n) = R.bit(31-n)）。
 * 值域与标量 get_compact_cqe_info 的映射对应：
 *   0=无错  1=L3/L4 校验错  2=OTHER(→IPSU_OTHER_ERR)  3=BYPASS(→HW_CHECK_NONE)
 */
#define HINIC3_AVX_COMPACT_CSUM_ERR_SHIFT	11

/*
 * compact CQE 的 ptype 索引位段：主机序 dw0 的 bit[27:16]
 * （RQ_COMPACT_CQE_STATUS_PTYPE_SHIFT=16, MASK=0xFFF），查表对象为
 * nic_dev->ptype_tbl->ptype[4096]，标量 compact 路径每包无条件查表填
 * packet_type（8B/16B CQE 无差别），这里逐包同语义。
 * raw 小端 dword 的字节级换算（H 字节 j = B(3-j)）：
 *   H[27:24] = B0[3:0] → idx[11:8]，H[23:16] = B1 → idx[7:0]
 *   idx = ((dw0 & 0xF) << 8) | ((dw0 >> 8) & 0xFF)
 */
#define HINIC3_AVX_COMPACT_PTYPE_IDX(dw0) \
	((((dw0) & 0xFu) << 8) | (((dw0) >> 8) & 0xFFu))

/*
 * 队列级常量：sp560 NIC 且 rx_dma_align 使能时，rearm 把 WQE DMA 地址压到
 * 对齐地址下方 16B，CQE 写在该地址处而 data_off 仍指向对齐地址，因此
 * CQE 地址 = buf_addr + data_off - 16（与标量路径一致）。
 * is_sp560_nic 是跨编译单元调用（读 hwdev/port_id/pci id），绝不能放进
 * 逐包热路径；只在批量入口算一次，其余调用点直接使用现成偏移。
 */
static inline s32
hinic3_vec_cqe_addr_off(struct hinic3_rxq *rxq)
{
	if (is_sp560_nic(rxq->nic_dev) && rxq->rx_dma_align)
		return -(s32)HINIC3_RX_DMA_ALIGN_CQE_OFFSET;

	return 0;
}

static inline uint8_t *
hinic3_vec_compact_cqe_addr(struct rte_mbuf *mbuf, s32 cqe_off)
{
#ifdef DPDK_21_11
	return (uint8_t *)rte_mbuf_buf_addr(mbuf, mbuf->pool) +
	       mbuf->data_off + cqe_off;
#else
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
	return (uint8_t *)rte_mbuf_buf_addr(mbuf, mbuf->pool) +
	       mbuf->data_off + cqe_off;
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
#endif
}

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
	s32 cqe_fix = hinic3_vec_cqe_addr_off(rxq);
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
			 * rearm 一致（偏移已在入口算好）。 */
			dma_addr += cqe_fix;
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

/*
 * 批量处理 4 个内嵌 compact CQE。
 *
 * 16B CQE 的字节布局（与 NEON 侧 shuf_mask 逐字节一致）：
 *   dw0: pkt_len=b2,b3 | cqe_len=b1 bit7 (raw bit15) | rxdone=b0 bit7 (raw bit7)
 *        | ptype 索引 = b0[3:0]<<8 | b1（host bit[27:16]）
 *   dw1: rss hash = b7,b6,b5,b4
 *   dw2: vlan_tci = b10,b9
 * 重排后写入 rx_descriptor_fields1 的前 16B：
 *   packet_type ← ptype 查表（lane0，insert_epi32，desc-ready 同款方案 A）
 *   | pkt_len | data_len | vlan_tci | rss
 */
static inline HINIC3_AVX512_TARGET u16
hinic3_recv_burst_compact_cqe_vec_avx(struct hinic3_rxq *rxq,
				      struct rte_mbuf **rx_pkts, u16 nb_pkts,
				      s32 cqe_off)
{
	struct hinic3_rx_info *rx_info;
	struct hinic3_nic_dev *nic_dev = rxq->nic_dev;
	u16 sw_ci = MASKED_QUEUE_IDX(rxq, rxq->cons_idx);
	u16 pos, valid_pkts, nb_rx = 0;
	u32 i;
	/* 队列级开关：每批算一次。csum 关闭时整段为 no-op（UNKNOWN=0，
	 * ol_flags 已由 rearm 清零），与标量 hinic3_rx_csum 的入口
	 * 判断同语义。 */
	u16 csum_en = (nic_dev->rx_csum_en & HINIC3_DEFAULT_RX_CSUM_OFFLOAD) != 0;
	u16 rss_en = nic_dev->rss_type.val != 0;
	/* RSS flag 批内恒定，与逐包 csum flags 合并成一次 ol_flags 写 */
	u16 rss_flag = rss_en ? (u16)HINIC3_PKT_RX_RSS_HASH : 0;
	/* ptype 查表指针批内恒定。表 16KB（4096 项，cache 对齐分配），真实
	 * 流量的 ptype 热集通常只有几种，命中行常驻 L1（~4-5 cycle），
	 * 冷 miss 每种 ptype 只发生一次。 */
	const struct hinic3_ptype_table *ptype_tbl = nic_dev->ptype_tbl;

	/* ptype=0 | pkt_len=[b3,b2,0,0] | data_len=[b3,b2]
	 * | vlan_tci=[b10,b9] | rss=[b7,b6,b5,b4]
	 * （lane0 的 ptype 由查表结果 insert_epi32 覆盖，掩码不必置 0） */
	const __m128i shuf_mask = _mm_setr_epi8(
		0xFF, 0xFF, 0xFF, 0xFF,
		3, 2, 0xFF, 0xFF,
		3, 2,
		10, 9,
		7, 6, 5, 4);

	rx_info = &rxq->rx_info[sw_ci];

	for (pos = 0; pos < nb_pkts; pos += HINIC3_DEFAULT_DESCS_PER_LOOP) {
		__m128i cqe[HINIC3_DEFAULT_DESCS_PER_LOOP];
		uint8_t *cqe_addr[HINIC3_DEFAULT_DESCS_PER_LOOP];
		__m128i pkt_mb;
		u32 status_arr[HINIC3_DEFAULT_DESCS_PER_LOOP];
		u32 not_done = 0;

		/* A. 4 个 mbuf 指针搬到调用者数组（4*8B，两条 128bit 搬移） */
		_mm_storeu_si128((__m128i *)&rx_pkts[pos],
			_mm_loadu_si128((const __m128i *)&rx_info[pos]));
		_mm_storeu_si128((__m128i *)&rx_pkts[pos + 2],
			_mm_loadu_si128((const __m128i *)&rx_info[pos + 2]));

		/* B. 逐包 load 16B 内嵌 CQE（地址不连续）。
		 * RXDONE、pkt_len、cqe_len 都在 dw0，单次 load 的快照
		 * 自洽：观察到 DD=1 即同一次读取里其余字段也是硬件
		 * 写入后的新值（x86 TSO，与 i40e/iavf/ice 向量路径
		 * 的单 load 模式一致），无需 fence + volatile 重读。 */
		for (i = 0; i < HINIC3_DEFAULT_DESCS_PER_LOOP; i++) {
			cqe_addr[i] = hinic3_vec_compact_cqe_addr(
				rx_pkts[pos + i], cqe_off);
			cqe[i] = _mm_loadu_si128(
				(const __m128i *)cqe_addr[i]);
			status_arr[i] = (u32)_mm_cvtsi128_si32(cqe[i]);

			if (!((status_arr[i] >>
			       HINIC3_AVX_COMPACT_RXDONE_BIT) & 1U))
				not_done |= 1U << i;
		}

		valid_pkts = (u16)__builtin_ctz(not_done |
				(1U << HINIC3_DEFAULT_DESCS_PER_LOOP));

		if (unlikely(valid_pkts == 0))
			break;

		/* D. 逐包 128bit shuffle + ptype 查表插 lane0 + 16B 宽存。
		 * insert 紧跟 shuffle、在宽存之前：packet_type 与其余描述符
		 * 字段同一次 16B store 交付，宽存后无窄写跟随，不存在
		 * store-forwarding 停顿（desc-ready 已实证的布局）。
		 * 未完成的槽位 status_arr 是残留旧值，索引经 12 位掩码恒在
		 * 表界内，查出的垃圾 ptype 只写进不会被交付的槽位，无害。 */
		for (i = 0; i < HINIC3_DEFAULT_DESCS_PER_LOOP; i++) {
			u32 ptype_idx = HINIC3_AVX_COMPACT_PTYPE_IDX(
							status_arr[i]);

			pkt_mb = _mm_shuffle_epi8(cqe[i], shuf_mask);
			pkt_mb = _mm_insert_epi32(pkt_mb,
					(int)ptype_tbl->ptype[ptype_idx], 0);
			_mm_storeu_si128((__m128i *)
				&rx_pkts[pos + i]->rx_descriptor_fields1,
				pkt_mb);
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
			volatile u32 *cqe_status = (volatile u32 *)cqe_addr[i];
			u16 data_offset = (status_arr[i] & BIT(15)) ? 8 : 16;
			u16 flags = 0;

			mb->data_off += data_offset;

			/* checksum offload：csum_err 在 raw dw0 的 bit[12:11]，
			 * 语义与标量 hinic3_rx_csum + compact 映射逐值对齐：
			 *   0 → IP/L4 GOOD；1 → IP_BAD（csum_errors++）；
			 *   2 → OTHER_ERR（other_errors++，flag 为 0 即
			 *       UNKNOWN）；3 → HW BYPASS → UNKNOWN（无操作）。
			 * GOOD/BAD 位在 ol_flags 上互不冲突，直接 |=。 */
			if (csum_en) {
				u32 csum_err = (status_arr[i] >>
					HINIC3_AVX_COMPACT_CSUM_ERR_SHIFT) & 0x3;

				if (csum_err == 0) {
					flags = HINIC3_PKT_RX_IP_CKSUM_GOOD |
						HINIC3_PKT_RX_L4_CKSUM_GOOD;
				} else if (csum_err == 1) {
					flags = HINIC3_PKT_RX_IP_CKSUM_BAD;
					rxq->rxq_stats.csum_errors++;
				} else if (csum_err == 2) {
					rxq->rxq_stats.other_errors++;
				}
			}

			/* csum 与 RSS 合并为一次 ol_flags 读改写（原来各
			 * 写一次）；两者都未使能时整体跳过，ol_flags 已
			 * 由 rearm 清零，无需写入。 */
			if (csum_en | rss_en)
				mb->ol_flags |= flags | rss_flag;

			*cqe_status = 0;
		}

		/* F 段已删除：窗口预取（入口按 chunk 调用）已覆盖本批
		 * 全部 CQE 行，批内再逐批预取同一批行是纯冗余，还多付
		 * 每包 1 条 prefetch 指令。 */

		nb_rx += valid_pkts;
		if (valid_pkts < HINIC3_DEFAULT_DESCS_PER_LOOP)
			break;
	}

	rxq->cons_idx += nb_rx;
	rxq->delta += nb_rx;
	rxq->rxq_stats.packets += nb_rx;

	return nb_rx;
}

/*
 * 按 hw_ci 把本轮长度收窄到"硬件确认已完成"的连续前缀。
 *
 * compact CQE 的完成位在报文缓冲区里：mbuf 归还 mempool 再补充出来时，
 * buf + data_off 处仍是上一轮的 CQE，RXDONE 可能残留 1，环回绕后会被
 * 误判成新包（每包收尾清 DD 只是缩小了窗口，清完到硬件写回之间仍可能
 * 被误轮询）。与标量路径同一套缓存策略：缓存住上次读到的 hw_ci，
 * 绝大多数调用不用再摸 CI 写回区那一行（网卡 DMA 写过，每轮无条件读
 * 就是每轮一个必然 miss 的 load）。
 *
 * 但缓存只在"缓存值还能凑出一批"时才可信，这里必须刷新——不能只判
 * sw_ci == hw_cons_idx：写回值既有 rx_cqe_coalesce_num 的批量触发，
 * 也有 rx_cqe_timer_loop 的定时触发，后者带下来的计数是任意值。只要
 * (hw_ci - sw_ci) 不是 4 的整数倍，按 4 对齐收窄就会剩下 1~3 个包，
 * sw_ci 停在 hw_cons_idx - 1..3 上；若这时不刷新缓存，avail 永远是那
 * 1~3，永远收窄成 0，队列就此卡死。所以"凑不出一批"必须触发刷新。
 */
static inline u16
hinic3_vec_compact_cqe_limit(struct hinic3_rxq *rxq, u16 sw_ci, u16 vec_pkts)
{
	struct hinic3_rq_ci_wb rq_ci;
	u16 avail;
	u32 value;

	avail = (u16)((rxq->hw_cons_idx - sw_ci) & rxq->q_mask);
	if (avail < HINIC3_DEFAULT_DESCS_PER_LOOP) {
		value = __atomic_load_n(&rxq->rq_ci->dw1.value,
					__ATOMIC_ACQUIRE);
		rq_ci.dw1.value = hinic3_hw_cpu32(value);
		rxq->hw_cons_idx = rq_ci.dw1.bs.hw_ci;
		avail = (u16)((rxq->hw_cons_idx - sw_ci) & rxq->q_mask);
	}

	if (avail < vec_pkts)
		vec_pkts = (u16)RTE_ALIGN_FLOOR(avail,
					HINIC3_DEFAULT_DESCS_PER_LOOP);

	return vec_pkts;
}

/*
 * 整窗预取：compact CQE 的地址要经 rx_info→mbuf→buf_addr/data_off 两级
 * 依赖才能算出来，只提前 4 个的预取量盖不住访存延迟（批内循环最多只
 * 能提前一批）。标量路径进来就把窗口内所有 mbuf 和缓冲区头预取掉
 * （rx.c 的 prefetch_flag 机制），这里对齐它。
 *
 * 只预取 hw_ci 确认前缀内的行：未确认的 CQE 行硬件还没写，提前拉进来
 * 会被写回无效化，白 miss 一次。T0 提示：这些行马上就要读，不做非时间
 * 局部性提示。
 */
static inline void
hinic3_vec_prefetch_window(struct hinic3_rxq *rxq, u16 sw_ci, u16 nb_pkts,
			   s32 cqe_off)
{
	u16 n = (u16)RTE_MIN(nb_pkts, (u16)HINIC3_DEFAULT_RX_BURST);
	u16 i;

	for (i = 0; i < n; i++) {
		struct rte_mbuf *mbuf = rxq->rx_info[sw_ci + i].mbuf;

		rte_prefetch0(mbuf);
		rte_prefetch0(hinic3_vec_compact_cqe_addr(mbuf, cqe_off));
	}
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
	s32 cqe_off = hinic3_vec_cqe_addr_off(rxq);

	sw_ci = MASKED_QUEUE_IDX(rxq, rxq->cons_idx);

	nb_pkts = RTE_ALIGN_FLOOR(nb_pkts, HINIC3_DEFAULT_DESCS_PER_LOOP);

	if (rxq->delta > rxq->rx_free_thresh)
		hinic3_rearm_rxq_mbuf_vec_avx(rxq);

	/*
	 * hw_ci 门控：硬件没确认任何完成时直接返回，连第一个 CQE 的行都
	 * 不摸——缓存命中的有效轮询在进 burst 前零访存。
	 */
	nb_pkts = hinic3_vec_compact_cqe_limit(rxq, sw_ci, nb_pkts);
	if (unlikely(nb_pkts == 0))
		return 0;

	rxm = rxq->rx_info[sw_ci].mbuf;

	/* 预取第一个内嵌 CQE（DMA 写入，马上要读，用 T0） */
	rx_cqe = (volatile struct hinic3_rq_cqe *)
		hinic3_vec_compact_cqe_addr(rxm, cqe_off);
	rte_prefetch0(rx_cqe);

	/*
	 * 检查第一个 CQE 完成（acquire），吸收 CI 写回与 CQE 数据写回之间
	 * 的可见窗口。compact RXDONE 是 dw0 byte0 bit7。
	 */
	status = (u32)__atomic_load_n(&rx_cqe->status, __ATOMIC_ACQUIRE);
	if (unlikely(!((status >> 7) & 1)))
		return 0;

	while (nb_pkts > 0) {
		u16 n, ret;

		n = RTE_MIN(nb_pkts, HINIC3_DEFAULT_RX_BURST);

		/*
		 * 每个分批窗口处理前整窗预取一次（hw_ci 确认前缀内，
		 * T0）：burst ≤ 32 时窗口预取覆盖全部包，批内的下一批
		 * 预取不再需要；burst > 32 时后续窗口由这里逐窗覆盖。
		 */
		hinic3_vec_prefetch_window(rxq, sw_ci + nb_rx, n, cqe_off);

		ret = hinic3_recv_burst_compact_cqe_vec_avx(rxq,
							    &rx_pkts[nb_rx], n,
							    cqe_off);

		nb_pkts -= ret;
		nb_rx += ret;

		if (ret < n)
			break;

		if (rxq->delta > rxq->rx_free_thresh)
			hinic3_rearm_rxq_mbuf_vec_avx(rxq);
	}

	return nb_rx;
}
