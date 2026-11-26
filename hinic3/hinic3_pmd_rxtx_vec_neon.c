/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2026 Huawei Technologies Co., Ltd
 */
#include <rte_ether.h>
#include <rte_mbuf.h>
#include <rte_vect.h>

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
#include "hinic3_pmd_rxtx_vec_common.h"

static inline u16
hinic3_recv_burst_vec(struct hinic3_rxq *rxq, struct rte_mbuf **rx_pkts,
                      u16 nb_pkts)
{
        struct hinic3_rx_info *rx_info = NULL;
        volatile struct hinic3_rq_cqe *rx_cqe = NULL;
        u16 sw_ci = MASKED_QUEUE_IDX(rxq, rxq->cons_idx);
        u16 pos;
        u16 valid_pkts, nb_rx;

        rx_cqe = &rxq->rx_cqe[sw_ci];
        rx_info = &rxq->rx_info[sw_ci];

        nb_rx = 0;
        for (pos = 0; pos < nb_pkts; pos += HINIC3_DEFAULT_DESCS_PER_LOOP,
             rx_cqe += HINIC3_DEFAULT_DESCS_PER_LOOP) {
                uint32x4_t cqe[HINIC3_DEFAULT_DESCS_PER_LOOP];
                uint32x4x2_t cqe0, cqe1;
                uint32x4_t status;
                uint64x2_t mb0, mb1;
                uint8x16_t pkt_mb0, pkt_mb1, pkt_mb2, pkt_mb3;

                /* mask to shuffle from cqe to mbuf */
                uint8x16_t shuf_mask = {
                        0xFF, 0xFF,
                        0xFF, 0xFF,
                        6, 7,
                        0xFF, 0xFF,
                        6, 7,
                        4, 5,
                        12, 13,
                        14, 15
                };

                /* A.1 load cqe[0-3] */
                /* TODO: 注意读取顺序保序, CPU可能先读旧的cqe，然后再读有效DD bit */
                /* TODO：考虑连续地址load */
                cqe[0] = vld1q_u32((uint32_t *)(rx_cqe + 0));
                cqe[1] = vld1q_u32((uint32_t *)(rx_cqe + 1));
                cqe[2] = vld1q_u32((uint32_t *)(rx_cqe + 2));
                cqe[3] = vld1q_u32((uint32_t *)(rx_cqe + 3));

                /* B.1 load 4 mbuf point */
                mb0 = vld1q_u64((uint64_t *)&rx_info[pos]);
                mb1 = vld1q_u64((uint64_t *)&rx_info[pos + 2]);
                /* B.2 copy 4 mbuf point into rx_pkts */
                vst1q_u64((uint64_t *)&rx_pkts[pos], mb0);
                vst1q_u64((uint64_t *)&rx_pkts[pos + 2], mb1);

                /* C.1 check valid cqe status
                 * cqe0.val[0] {cqe[0].status, cqe[1].status, cqe[0].vlan_len, cqe[1].vlan_len}
                 * cqe0.val[1] {cqe[0].offload_type, cqe[1].offload_type, cqe[0].hash_val, cqe[1].hash_val}
                 * cqe1.val[0] {cqe[2].status, cqe[3].status, cqe[2].vlan_len, cqe[3].vlan_len}
                 * cqe1.val[1] {cqe[2].offload_type, cqe[3].offload_type, cqe[2].hash_val, cqe[3].hash_val}
                 */
                /* TODO: 考虑后续大小端转换 用TBL指令 */
                cqe0 = vzipq_u32(cqe[0], cqe[1]);
                cqe1 = vzipq_u32(cqe[2], cqe[3]);
                /* C.2 get rx done bit: (cqe->status >> 31) & 0x1U */
                status = vcombine_u32(vget_low_u32(cqe0.val[0]),
                                      vget_low_u32(cqe1.val[0]));
                valid_pkts = vaddvq_u32(vshrq_n_u32(status, 31));

                /* D.1 shuffle from cqe to mbuf */
                pkt_mb0 = vqtbl1q_u8(vreinterpretq_u8_u32(cqe[0]), shuf_mask);
                pkt_mb1 = vqtbl1q_u8(vreinterpretq_u8_u32(cqe[1]), shuf_mask);
                pkt_mb2 = vqtbl1q_u8(vreinterpretq_u8_u32(cqe[2]), shuf_mask);
                pkt_mb3 = vqtbl1q_u8(vreinterpretq_u8_u32(cqe[3]), shuf_mask);

                vst1q_u8((void *)&rx_pkts[pos + 0]->rx_descriptor_fields1, pkt_mb0);
                vst1q_u8((void *)&rx_pkts[pos + 1]->rx_descriptor_fields1, pkt_mb1);
                vst1q_u8((void *)&rx_pkts[pos + 2]->rx_descriptor_fields1, pkt_mb2);
                vst1q_u8((void *)&rx_pkts[pos + 3]->rx_descriptor_fields1, pkt_mb3);

                nb_rx += valid_pkts;
                if (valid_pkts < HINIC3_DEFAULT_DESCS_PER_LOOP)
                        break;
        }

        rxq->cons_idx += nb_rx;
        rxq->delta += nb_rx;
        rxq->rxq_stats.packets += nb_rx;

        return nb_rx;
}

u16
hinic3_recv_pkts_vec(void *rx_queue, struct rte_mbuf **rx_pkts, u16 nb_pkts)
{
        struct hinic3_rxq *rxq = rx_queue;
        volatile struct hinic3_rq_cqe *rx_cqe = NULL;
        u16 sw_ci;
        u32 status;
        u16 nb_rx = 0;

        sw_ci = MASKED_QUEUE_IDX(rxq, rxq->cons_idx);
        rx_cqe = &rxq->rx_cqe[sw_ci];

        rte_prefetch_non_temporal(rx_cqe);

        nb_pkts = RTE_ALIGN_FLOOR(nb_pkts, HINIC3_DEFAULT_DESCS_PER_LOOP);

        if (rxq->delta > rxq->rx_free_thresh)
                hinic3_rearm_rxq_mbuf_vec(rxq);

        status = hinic3_hw_cpu32((u32)(__atomic_load_n(&rx_cqe->status,
                                        __ATOMIC_ACQUIRE)));
        if (unlikely(!HINIC3_GET_RX_DONE(status)))
                return 0;

        rte_prefetch0(rxq->rx_info[sw_ci + 0].mbuf);
        rte_prefetch0(rxq->rx_info[sw_ci + 1].mbuf);
        rte_prefetch0(rxq->rx_info[sw_ci + 2].mbuf);
        rte_prefetch0(rxq->rx_info[sw_ci + 3].mbuf);

        while (nb_pkts > 0) {
                u16 n, ret;

                n = RTE_MIN(nb_pkts, HINIC3_DEFAULT_RX_BURST);
                ret = hinic3_recv_burst_vec(rxq, &rx_pkts[nb_rx], n);

                nb_pkts -= ret;
                nb_rx += ret;

                if (ret < n)
                	break;

                if (rxq->delta > rxq->rx_free_thresh)
                        hinic3_rearm_rxq_mbuf_vec(rxq);
        }

        return nb_rx;
}

/*
 * NEON accelerated compact CQE RX burst path, modelled on the i40e/iavf vec
 * RX patterns (load ordering, __builtin_ctzl completion count, vqtbl1q_u8
 * descriptor->mbuf shuffle, next-batch prefetch).
 *
 * The compact CQE is integrated at the head of each packet buffer at
 * buf_addr + data_off (set by rearm). Unlike a separate CQE ring it is not
 * contiguous, so the 4 CQEs are loaded individually; the field mapping and
 * completion check follow the reference vec implementations.
 *
 * CQE (16B, big-endian words):
 *   dw0: PKT_LEN=b2,b3 | CQE_LEN=b1 bit7 (raw bit15) | RXDONE=b0 bit7
 *   dw1: rss hash
 *   dw2: VLAN_TAG=b9,b10
 * Packet data follows at buf_addr + data_off + data_offset.
 *
 * Fast path: fills basic mbuf descriptor fields only (packet_type/pkt_len/
 * data_len/vlan_tci/rss), skips csum/vlan/rss/ptype offload and scattered RX.
 */
static inline u16
hinic3_recv_burst_compact_cqe_vec(struct hinic3_rxq *rxq,
                                  struct rte_mbuf **rx_pkts,
                                  u16 nb_pkts)
{
        struct hinic3_rx_info *rx_info = NULL;
        u16 sw_ci = MASKED_QUEUE_IDX(rxq, rxq->cons_idx);
        u16 pos;
        u16 valid_pkts, nb_rx;
        u32 i;

        rx_info = &rxq->rx_info[sw_ci];

        nb_rx = 0;
        for (pos = 0; pos < nb_pkts; pos += HINIC3_DEFAULT_DESCS_PER_LOOP) {
                uint8x16_t cqe0, cqe1, cqe2, cqe3;
                uint64x2_t mb0, mb1;
                uint8x16_t pkt_mb0, pkt_mb1, pkt_mb2, pkt_mb3;
                uint32x4_t status;
                uint32_t status_arr[HINIC3_DEFAULT_DESCS_PER_LOOP];

                /* shuffle compact CQE bytes into mbuf rx_descriptor_fields1
                 * (LE): packet_type=0 | pkt_len=[b3,b2,0,0] | data_len=[b3,b2]
                 * | vlan_tci=[b10,b9] | rss=[b7,b6,b5,b4] */
                const uint8x16_t shuf_mask = {
                        0xFF, 0xFF, 0xFF, 0xFF,
                        3, 2, 0xFF, 0xFF,
                        3, 2,
                        10, 9,
                        7, 6, 5, 4
                };

                /* A. copy 4 mbuf pointers into rx_pkts */
                mb0 = vld1q_u64((uint64_t *)&rx_info[pos]);
                mb1 = vld1q_u64((uint64_t *)&rx_info[pos + 2]);
                vst1q_u64((uint64_t *)&rx_pkts[pos], mb0);
                vst1q_u64((uint64_t *)&rx_pkts[pos + 2], mb1);

                /* B. load 4 integrated compact CQEs (16B) from each buffer */
                cqe0 = vld1q_u8(hinic3_vec_compact_cqe_addr(rx_pkts[pos + 0]));
                cqe1 = vld1q_u8(hinic3_vec_compact_cqe_addr(rx_pkts[pos + 1]));
                cqe2 = vld1q_u8(hinic3_vec_compact_cqe_addr(rx_pkts[pos + 2]));
                cqe3 = vld1q_u8(hinic3_vec_compact_cqe_addr(rx_pkts[pos + 3]));
                /* order the status dword (holds RXDONE and pkt_len) after the
                 * first load, so observing a fresh completion also guarantees
                 * fresh data (i40e/iavf acquire + reload pattern) */
#ifdef DPDK_20_11
                rte_atomic_thread_fence(__ATOMIC_ACQUIRE);
#else
                __atomic_thread_fence(__ATOMIC_ACQUIRE);
#endif
                cqe0 = vreinterpretq_u8_u32(vld1q_lane_u32(
                                (uint32_t *)hinic3_vec_compact_cqe_addr(
                                        rx_pkts[pos + 0]),
                                vreinterpretq_u32_u8(cqe0), 0));
                cqe1 = vreinterpretq_u8_u32(vld1q_lane_u32(
                                (uint32_t *)hinic3_vec_compact_cqe_addr(
                                        rx_pkts[pos + 1]),
                                vreinterpretq_u32_u8(cqe1), 0));
                cqe2 = vreinterpretq_u8_u32(vld1q_lane_u32(
                                (uint32_t *)hinic3_vec_compact_cqe_addr(
                                        rx_pkts[pos + 2]),
                                vreinterpretq_u32_u8(cqe2), 0));
                cqe3 = vreinterpretq_u8_u32(vld1q_lane_u32(
                                (uint32_t *)hinic3_vec_compact_cqe_addr(
                                        rx_pkts[pos + 3]),
                                vreinterpretq_u32_u8(cqe3), 0));

                /* C. count consecutive RXDONE (dw0 bit31 BE = raw bit7):
                 * sign-extend the bit into 16-bit slots and locate the first
                 * incomplete one with __builtin_ctzl (i40e/iavf pattern) */
                status_arr[0] = vgetq_lane_u32(vreinterpretq_u32_u8(cqe0), 0);
                status_arr[1] = vgetq_lane_u32(vreinterpretq_u32_u8(cqe1), 0);
                status_arr[2] = vgetq_lane_u32(vreinterpretq_u32_u8(cqe2), 0);
                status_arr[3] = vgetq_lane_u32(vreinterpretq_u32_u8(cqe3), 0);
                status = vld1q_u32(status_arr);
                {
                        uint16x4_t se;
                        uint64_t stat;

                        se = vmovn_u32(vandq_u32(vshrq_n_u32(status, 7),
                                                 vdupq_n_u32(1)));
                        se = vshl_n_u16(se, 15);
                        se = vreinterpret_u16_s16(
                                vshr_n_s16(vreinterpret_s16_u16(se), 15));
                        stat = ~vget_lane_u64(vreinterpret_u64_u16(se), 0);
                        if (stat == 0)
                                valid_pkts = HINIC3_DEFAULT_DESCS_PER_LOOP;
                        else
                                valid_pkts = __builtin_ctzl(stat) / 16;
                }

                /* D. shuffle CQE to mbuf descriptor fields */
                pkt_mb0 = vqtbl1q_u8(cqe0, shuf_mask);
                pkt_mb1 = vqtbl1q_u8(cqe1, shuf_mask);
                pkt_mb2 = vqtbl1q_u8(cqe2, shuf_mask);
                pkt_mb3 = vqtbl1q_u8(cqe3, shuf_mask);
                vst1q_u8((void *)&rx_pkts[pos + 0]->rx_descriptor_fields1,
                         pkt_mb0);
                vst1q_u8((void *)&rx_pkts[pos + 1]->rx_descriptor_fields1,
                         pkt_mb1);
                vst1q_u8((void *)&rx_pkts[pos + 2]->rx_descriptor_fields1,
                         pkt_mb2);
                vst1q_u8((void *)&rx_pkts[pos + 3]->rx_descriptor_fields1,
                         pkt_mb3);

                /* E. skip the integrated CQE: data_off += 16 (16B CQE) or 8
                 * (8B CQE); cqe_len is host bit 23 of the big-endian status
                 * word (scalar RQ_COMPACT_CQE_STATUS_CQE_LEN_SHIFT), which
                 * maps to raw bit 15 after the pure byte permutation of
                 * be32_to_cpu (host byte j = raw byte 3 - j, bits inside
                 * each byte preserved) */
                for (i = 0; i < valid_pkts; i++) {
                        u16 data_offset =
                                (status_arr[i] &
                                 BIT(HINIC3_COMPACT_CQE_LEN_BIT)) ? 8 : 16;

                        rx_pkts[pos + i]->data_off += data_offset;
                }

                /* F. prefetch the next batch's integrated CQEs (skip the last
                 * batch, i40e/iavf pattern) */
                if (likely(pos + 2 * HINIC3_DEFAULT_DESCS_PER_LOOP < nb_pkts)) {
                        rte_prefetch_non_temporal(
                                hinic3_vec_compact_cqe_addr(
                                        rx_info[pos + 4].mbuf));
                        rte_prefetch_non_temporal(
                                hinic3_vec_compact_cqe_addr(
                                        rx_info[pos + 5].mbuf));
                        rte_prefetch_non_temporal(
                                hinic3_vec_compact_cqe_addr(
                                        rx_info[pos + 6].mbuf));
                        rte_prefetch_non_temporal(
                                hinic3_vec_compact_cqe_addr(
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

u16
hinic3_recv_pkts_compact_cqe_vec(void *rx_queue, struct rte_mbuf **rx_pkts,
                                 u16 nb_pkts)
{
        struct hinic3_rxq *rxq = rx_queue;
        struct rte_mbuf *rxm = NULL;
        volatile struct hinic3_rq_cqe *rx_cqe = NULL;
        u16 sw_ci;
        u32 status;
        u16 nb_rx = 0;

        sw_ci = MASKED_QUEUE_IDX(rxq, rxq->cons_idx);
        rxm = rxq->rx_info[sw_ci].mbuf;

        /* Prefetch the first integrated CQE (non-temporal: DMA written) */
        rx_cqe = (volatile struct hinic3_rq_cqe *)
                hinic3_vec_compact_cqe_addr(rxm);
        rte_prefetch_non_temporal(rx_cqe);

        nb_pkts = RTE_ALIGN_FLOOR(nb_pkts, HINIC3_DEFAULT_DESCS_PER_LOOP);

        if (rxq->delta > rxq->rx_free_thresh)
                hinic3_rearm_rxq_mbuf_vec(rxq);

        /* Check the first CQE completion (acquire). RXDONE is compact CQE
         * dw0 bit31 (BE) = first byte bit7 of the raw status dword. */
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
                ret = hinic3_recv_burst_compact_cqe_vec(rxq,
                                                        &rx_pkts[nb_rx], n);

                nb_pkts -= ret;
                nb_rx += ret;

                if (ret < n)
                        break;

                if (rxq->delta > rxq->rx_free_thresh)
                        hinic3_rearm_rxq_mbuf_vec(rxq);
        }

        return nb_rx;
}