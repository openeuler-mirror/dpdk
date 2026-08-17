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

static u32 hinic3_rx_alloc_mbuf_bulk_vec(struct hinic3_rxq *rxq,
                     struct rte_mbuf **mbufs,
                     u32 exp_mbuf_cnt)
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

static int hinic3_rearm_rxq_mbuf_vec_neon(struct hinic3_rxq *rxq)
{
        struct hinic3_rq_wqe *rq_wqe = NULL;
        volatile struct hinic3_rq_cqe *rx_cqe = NULL;
        struct rte_mbuf **rearm_mbufs;
        u32 i, free_wqebbs, rearm_wqebbs, exp_wqebbs;
        rte_iova_t dma_addr;
        rte_iova_t align_dma_addr;
        u16 pi;
        struct hinic3_nic_dev *nic_dev = rxq->nic_dev;

        /* Check free wqebb cnt fo rearm */
        free_wqebbs = rxq->delta - 1;
        if (unlikely(free_wqebbs < rxq->rx_free_thresh))
                return -ENOMEM;

        /* Get rearm mbuf array */
        pi = MASKED_QUEUE_IDX(rxq, rxq->prod_idx);
        rearm_mbufs = (struct rte_mbuf **)(&rxq->rx_info[pi]);
        rx_cqe = &rxq->rx_cqe[pi];

        /* Check rxq free wqebbs turn around */
        exp_wqebbs = rxq->q_depth - pi;
        if (free_wqebbs < exp_wqebbs)
                exp_wqebbs = free_wqebbs;

        /* Alloc mbuf in bulk */
        rearm_wqebbs = hinic3_rx_alloc_mbuf_bulk_vec(rxq, rearm_mbufs, exp_wqebbs);
        if (unlikely(rearm_wqebbs == 0))
                return -ENOMEM;

        /* Rearm rx mbuf */
        rq_wqe = NIC_WQE_ADDR(rxq, pi);
        for (i = 0; i < rearm_wqebbs; i++) {
                dma_addr = rte_mbuf_data_iova_default(rearm_mbufs[i]);
                /* Keep consistent with the normal path: fold the alignment
                 * offset into data_off when alignment is enabled so the vec
                 * RX path data address matches the DMA start address. */
                if (rxq->rx_dma_align) {
                        align_dma_addr = RTE_ALIGN(dma_addr, rxq->rx_dma_align);
                        rearm_mbufs[i]->data_off = (u16)(RTE_PKTMBUF_HEADROOM +
                                (align_dma_addr - dma_addr));
                        dma_addr = align_dma_addr;
                } else {
                        rearm_mbufs[i]->data_off = RTE_PKTMBUF_HEADROOM;
                }
                rearm_mbufs[i]->port = rxq->port_id;
                rearm_mbufs[i]->ol_flags = 0;
                rx_cqe[i].status = 0;

                /* Fill buffer address only */
                if (rxq->wqe_type == HINIC3_EXTEND_RQ_WQE) {
                        rq_wqe->extend_wqe.buf_desc.sge.hi_addr =
                                hinic3_hw_be32(upper_32_bits(dma_addr));
                        rq_wqe->extend_wqe.buf_desc.sge.lo_addr =
                                hinic3_hw_be32(lower_32_bits(dma_addr));
                        rq_wqe->extend_wqe.buf_desc.sge.len =
                                nic_dev->rx_buff_len;
                } else {
                        rq_wqe->normal_wqe.buf_hi_addr =
                                hinic3_hw_be32(upper_32_bits(dma_addr));
                        rq_wqe->normal_wqe.buf_lo_addr =
                                hinic3_hw_be32(lower_32_bits(dma_addr));
                }

                rq_wqe = (struct hinic3_rq_wqe *)((u64)rq_wqe + rxq->wqebb_size);
        }
        rxq->prod_idx += rearm_wqebbs;
        rxq->delta -= rearm_wqebbs;

        hinic3_write_db(rxq->db_addr, rxq->q_id, 0, RQ_CFLAG_DP,
                ((pi + rearm_wqebbs) & rxq->q_mask) << rxq->wqe_type);

        return 0;
}

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
                hinic3_rearm_rxq_mbuf_vec_neon(rxq);

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
                        hinic3_rearm_rxq_mbuf_vec_neon(rxq);
        }

        return nb_rx;
}