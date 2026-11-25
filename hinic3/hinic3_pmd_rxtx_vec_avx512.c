/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2026 Huawei Technologies Co., Ltd
 */

#include <immintrin.h>

#include <rte_mbuf.h>
#include <rte_prefetch.h>

#include "base/hinic3_compat.h"
#include "hinic3_pmd_nic_io.h"
#include "hinic3_pmd_ethdev.h"
#include "hinic3_pmd_rx.h"
#include "hinic3_pmd_rxtx_vec_common.h"

/* Function-level ISA attribute; only reached after the AVX512F runtime probe. */
#define HINIC3_AVX512_TARGET	__attribute__((target("avx512f")))

#define HINIC3_AVX_COMPACT_RXDONE_BIT	7

/*
 * csum_err field: host dw0 bit[20:19] -> raw dword bit[12:11].
 * Values match the scalar compact mapping:
 *   0=OK  1=L3/L4 error  2=OTHER  3=BYPASS
 */
#define HINIC3_AVX_COMPACT_CSUM_ERR_SHIFT	11

/*
 * ptype index: host dw0 bit[27:16] -> raw bytes
 *   idx = ((dw0 & 0xF) << 8) | ((dw0 >> 8) & 0xFF)
 * indexing ptype_tbl->ptype[4096], same per-packet lookup as the
 * scalar compact path.
 */
#define HINIC3_AVX_COMPACT_PTYPE_IDX(dw0) \
	((((dw0) & 0xFu) << 8) | (((dw0) >> 8) & 0xFFu))

/*
 * Process 4 integrated compact CQEs per iteration.
 *
 * 16B CQE layout (identical to the NEON shuf_mask):
 *   dw0: pkt_len=b2,b3 | cqe_len=b1 bit7 (raw bit15) | rxdone=b0 bit7 (raw bit7)
 *        | ptype idx = b0[3:0]<<8 | b1 (host bit[27:16])
 *   dw1: rss hash = b7..b4
 *   dw2: vlan_tci = b10,b9
 * After the shuffle, the 16B written to rx_descriptor_fields1 holds:
 *   packet_type (from the ptype table, inserted into lane0)
 *   | pkt_len | data_len | vlan_tci | rss
 */
static inline HINIC3_AVX512_TARGET u16
hinic3_recv_burst_compact_cqe_vec_avx(struct hinic3_rxq *rxq,
				      struct rte_mbuf **rx_pkts, u16 nb_pkts)
{
	struct hinic3_rx_info *rx_info;
	struct hinic3_nic_dev *nic_dev = rxq->nic_dev;
	u16 sw_ci = MASKED_QUEUE_IDX(rxq, rxq->cons_idx);
	u16 pos, valid_pkts, nb_rx = 0;
	u64 rx_bytes = 0;
	u32 i;
	/* Queue-level switches, computed once per burst. No-op when csum
	 * offload is off (UNKNOWN=0, ol_flags cleared by rearm), same as
	 * the scalar hinic3_rx_csum entry check. */
	u16 csum_en = (nic_dev->rx_csum_en & HINIC3_DEFAULT_RX_CSUM_OFFLOAD) != 0;
	u16 rss_en = nic_dev->rss_type.val != 0;
	/* RSS flag is burst-wide constant; folded with the per-packet csum
	 * flags into a single ol_flags write. */
	u16 rss_flag = rss_en ? (u16)HINIC3_PKT_RX_RSS_HASH : 0;
	/* ptype table pointer is burst-wide constant. The 4K-entry table is
	 * cache-aligned; hot ptypes stay resident in L1 and cold misses
	 * happen once per ptype. */
	const struct hinic3_ptype_table *ptype_tbl = nic_dev->ptype_tbl;

	/* vpshufb byte shuffle: each element is a CQE byte index placed at
	 * the output position it should land in -- output byte i of the
	 * descriptor (rx_descriptor_fields1) takes CQE byte shuf_mask[i].
	 * 
	 * 0xFF has the high bit set: vpshufb zeroes such outputs, used to blank a field.
	 * Output layout: [0..3]=packet_type | [4..7]=pkt_len=[b3,b2,0,0]
	 * | [8..9]=data_len=[b3,b2] | [10..11]=vlan_tci=[b10,b9]
	 * | [12..15]=rss=[b7..b4] */
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

		/* A. copy the 4 mbuf pointers to the caller array (two
		 * 128-bit moves) */
		_mm_storeu_si128((__m128i *)&rx_pkts[pos],
			_mm_loadu_si128((const __m128i *)&rx_info[pos]));
		_mm_storeu_si128((__m128i *)&rx_pkts[pos + 2],
			_mm_loadu_si128((const __m128i *)&rx_info[pos + 2]));

		/* B. load the 16B integrated CQE of each packet (non-contiguous
		 * addresses). */
		for (i = 0; i < HINIC3_DEFAULT_DESCS_PER_LOOP; i++) {
			cqe_addr[i] =
				hinic3_vec_compact_cqe_addr(rx_pkts[pos + i]);
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

		/* D. per-packet 128-bit shuffle + ptype table insert into
		 * lane0 + 16B wide store. The insert sits between the
		 * shuffle and the wide store so packet_type is delivered by
		 * the same 16B store, with no narrow write after it (no
		 * store-forwarding stall).
		 * For not-yet-done slots status_arr holds stale values; the
		 * 12-bit masked index keeps the lookup in bounds and the
		 * garbage ptype only lands in slots that are never
		 * delivered. */
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

		/* E. skip the integrated CQE and clear the RXDONE handshake
		 * bit. cqe_len is host bit 23 of the BE status word, i.e.
		 * raw bit15 (raw BIT(23) would hit pkt_len's MSB); see
		 * vec_common.h. cqe_len=1 -> 8B CQE, else 16B.
		 * RXDONE is a hardware-set/software-clear handshake, same
		 * as the scalar rx_cqe->status = 0; leaving it set would
		 * make the poll treat stale DD as a new completion before
		 * the NIC writes the next CQE.
		 * The CQE address must be computed before data_off changes. */
		for (i = 0; i < valid_pkts; i++) {
			struct rte_mbuf *mb = rx_pkts[pos + i];
			volatile u32 *cqe_status = (volatile u32 *)cqe_addr[i];
			u16 data_offset = (status_arr[i] &
					   BIT(HINIC3_COMPACT_CQE_LEN_BIT)) ? 8 : 16;
			u16 flags = 0;

			mb->data_off += data_offset;

			rx_bytes += mb->pkt_len;

			/* checksum offload: csum_err is raw dw0 bit[12:11],
			 * aligned with the scalar hinic3_rx_csum compact
			 * mapping:
			 *   0 -> IP/L4 GOOD;
			 *   1 -> IP_BAD (csum_errors++);
			 *   2 -> OTHER_ERR (other_errors++);
			 *   3 -> HW BYPASS -> UNKNOWN (no-op). */
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

			if (csum_en | rss_en)
				mb->ol_flags |= flags | rss_flag;

			*cqe_status = 0;
		}

		nb_rx += valid_pkts;
		if (valid_pkts < HINIC3_DEFAULT_DESCS_PER_LOOP)
			break;
	}

	rxq->cons_idx += nb_rx;
	rxq->delta += nb_rx;
	rxq->rxq_stats.packets += nb_rx;
	rxq->rxq_stats.bytes += rx_bytes;

	return nb_rx;
}

/*
 * Narrow the burst to the prefix confirmed done by the NIC via hw_ci.
 *
 * The completion bit lives in the packet buffer: after an mbuf returns to
 * the mempool and is reused, the CQE at buf + data_off may still carry a
 * stale RXDONE=1 from the previous round, which would be mistaken for a
 * new packet after ring wraparound. Cache hw_ci like the scalar path and
 * refresh the cache when it covers less than one burst.
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
 * Prefetch the whole window: computing a compact CQE address takes two
 * dependent loads (rx_info -> mbuf -> buf_addr/data_off), so prefetching
 * only 4 ahead cannot hide the latency. Mirror the scalar path, which
 * prefetches all mbufs and buffer heads up front (rx.c prefetch_flag).
 */
static inline void
hinic3_vec_prefetch_window(struct hinic3_rxq *rxq, u16 sw_ci, u16 nb_pkts)
{
	u16 n = (u16)RTE_MIN(nb_pkts, (u16)HINIC3_DEFAULT_RX_BURST);
	u16 i;

	for (i = 0; i < n; i++) {
		struct rte_mbuf *mbuf = rxq->rx_info[sw_ci + i].mbuf;

		rte_prefetch0(mbuf);
		rte_prefetch0(hinic3_vec_compact_cqe_addr(mbuf));
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

	sw_ci = MASKED_QUEUE_IDX(rxq, rxq->cons_idx);

	nb_pkts = RTE_ALIGN_FLOOR(nb_pkts, HINIC3_DEFAULT_DESCS_PER_LOOP);

	if (rxq->delta > rxq->rx_free_thresh)
		hinic3_rearm_rxq_mbuf_vec(rxq);

	nb_pkts = hinic3_vec_compact_cqe_limit(rxq, sw_ci, nb_pkts);
	if (unlikely(nb_pkts == 0))
		return 0;

	rxm = rxq->rx_info[sw_ci].mbuf;

	rx_cqe = (volatile struct hinic3_rq_cqe *)
		hinic3_vec_compact_cqe_addr(rxm);
	rte_prefetch0(rx_cqe);

	status = (u32)__atomic_load_n(&rx_cqe->status, __ATOMIC_ACQUIRE);
	if (unlikely(!((status >> HINIC3_AVX_COMPACT_RXDONE_BIT) & 1)))
		return 0;

	while (nb_pkts > 0) {
		u16 n, done_pkts;

		n = RTE_MIN(nb_pkts, HINIC3_DEFAULT_RX_BURST);

		/*
		 * Prefetch the whole window before each burst (within the
		 * hw_ci-confirmed prefix). With burst <= 32 this covers
		 * all packets; larger bursts are covered window by window.
		 * The window start must be masked by q_depth: nb_pkts can
		 * be 512 (testpmd flush uses MAX_PKT_BURST), and the
		 * accumulated sw_ci + nb_rx would otherwise run past the
		 * rx_info padding area and read heap garbage as mbuf
		 * pointers (SIGSEGV at mbuf->pool).
		 */
		hinic3_vec_prefetch_window(rxq,
			(u16)((sw_ci + nb_rx) & rxq->q_mask), n);

		done_pkts = hinic3_recv_burst_compact_cqe_vec_avx(rxq,
							    &rx_pkts[nb_rx], n);

		nb_pkts -= done_pkts;
		nb_rx += done_pkts;

		if (done_pkts < n)
			break;

		if (rxq->delta > rxq->rx_free_thresh)
			hinic3_rearm_rxq_mbuf_vec(rxq);
	}

	return nb_rx;
}
