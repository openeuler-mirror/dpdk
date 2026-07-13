/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2025-2026 Huawei Technologies Co., Ltd
 */

#include <rte_ether.h>
#include <rte_mbuf.h>
#include <rte_io.h>
#include <rte_net.h>
#include <rte_vxlan.h>

#include "base/hinic3_compat.h"
#include "base/hinic3_pmd_mgmt.h"
#include "base/hinic3_pmd_nic_cfg.h"
#include "base/hinic3_pmd_hwdev.h"
#include "hinic3_pmd_nic_io.h"
#include "hinic3_pmd_ethdev.h"
#include "hinic3_pmd_tx.h"

/**
 * Get send queue free wqebb cnt
 *
 * @param[in] sq
 *   Send queue
 * @return
 *   Number of free wqebb
 */
static inline u16 hinic3_get_sq_free_wqebbs(struct hinic3_txq *sq)
{
	return ((sq->q_depth -
		(((sq->prod_idx - sq->cons_idx) + sq->q_depth) & sq->q_mask)) -
		1);
}

/**
 * Update send queue local ci
 *
 * @param[in] sq
 *   Send queue
 * @param[in] wqe_cnt
 *   Number of wqebb
 */
static inline void hinic3_update_sq_local_ci(struct hinic3_txq *sq, u16 wqe_cnt)
{
	sq->cons_idx += wqe_cnt;
}

/**
 * Get send queue local ci
 *
 * @param[in] sq
 *   Send queue
 * @return
 *   Local ci
 */
static inline u16 hinic3_get_sq_local_ci(struct hinic3_txq *sq)
{
	return MASKED_QUEUE_IDX(sq, sq->cons_idx);
}

/**
 * Get send queue hardware ci
 *
 * @param[in] sq
 *   Send queue
 * @return
 *   Hardware ci
 */
static inline u16 hinic3_get_sq_hw_ci(struct hinic3_txq *sq)
{
	return MASKED_QUEUE_IDX(sq, hinic3_hw_cpu16(*(sq->ci_vaddr_base)));
}

static void *hinic3_sq_get_wqebbs(struct hinic3_txq *sq, u16 num_wqebbs, u16 *prod_idx)
{
	*prod_idx = MASKED_QUEUE_IDX(sq, sq->prod_idx);
	sq->prod_idx += num_wqebbs;

	return NIC_WQE_ADDR(sq, *prod_idx); /*lint !e701 !e647*/
}

static inline u16 hinic3_get_and_update_sq_owner(struct hinic3_txq *sq, u16 curr_pi, u16 wqebb_cnt)
{
	u16 owner = sq->owner;
	u32 sum = (u32)curr_pi + (u32)wqebb_cnt;

	if (unlikely(sum >= sq->q_depth || sum < curr_pi))
		sq->owner = !sq->owner;

	return owner;
}

/**
 * Put send queue wqe
 *
 * @param[in] sq
 *   Send queue
 * @param[in] wqe_info
 *   Wqe info
 */
static inline void hinic3_put_sq_wqe(struct hinic3_txq *sq,
				     struct hinic3_wqe_info *wqe_info)
{
	if (wqe_info->owner != sq->owner)
		sq->owner = wqe_info->owner;

	sq->prod_idx -= wqe_info->wqebb_cnt;
}

static void hinic3_set_wqe_combo_compact_cqe(struct hinic3_txq *sq,
				 struct hinic3_sq_wqe_combo *wqe_combo,
				 struct hinic3_wqe_info *wqe_info)
{
	u16 tmp_pi;

	wqe_combo->hdr = hinic3_sq_get_wqebbs(sq, 1, &wqe_info->pi);

	if (wqe_info->wqebb_cnt == 1) {
		/* compact wqe */
		wqe_combo->wqe_type = SQ_WQE_COMPACT_TYPE;
		wqe_combo->task_type = SQ_WQE_TASKSECT_4BYTES;
		wqe_combo->task = (struct hinic3_sq_task *)&wqe_combo->hdr->queue_info;
		wqe_info->owner = hinic3_get_and_update_sq_owner(sq, wqe_info->pi, 1);
		return;
	}

	/* extend normal wqe */
	wqe_combo->wqe_type = SQ_WQE_EXTENDED_TYPE;
	wqe_combo->task_type = SQ_WQE_TASKSECT_16BYTES;
	wqe_combo->task = hinic3_sq_get_wqebbs(sq, 1, &tmp_pi);
	if (wqe_info->sge_cnt > 1)
		wqe_combo->bds_head = hinic3_sq_get_wqebbs(sq, wqe_info->sge_cnt - 1, &tmp_pi);

	wqe_info->owner = hinic3_get_and_update_sq_owner(sq, wqe_info->pi, wqe_info->wqebb_cnt);
}

static inline void hinic3_free_cpy_mbuf(struct hinic3_nic_dev *nic_dev __rte_unused, struct rte_mbuf *cpy_skb)
{
	rte_pktmbuf_free(cpy_skb);
}

static int hinic3_xmit_mbuf_cleanup(struct hinic3_txq *txq, u32 free_cnt)
{
	struct hinic3_tx_info *tx_info = NULL;
	struct rte_mbuf *mbuf = NULL;
	struct rte_mbuf *mbuf_temp = NULL;
	struct rte_mbuf *mbuf_free[HINIC3_MAX_TX_FREE_BULK];
	int nb_free = 0;
	int wqebb_cnt = 0;
	u16 hw_ci, sw_ci, sq_mask;
	u32 i;

	hw_ci = hinic3_get_sq_hw_ci(txq);
	sw_ci = hinic3_get_sq_local_ci(txq);
	sq_mask = txq->q_mask;

	for (i = 0; i < free_cnt; ++i) {
		tx_info = &txq->tx_info[sw_ci];
		if (hw_ci == sw_ci ||
		    (((hw_ci - sw_ci) & sq_mask) < tx_info->wqebb_cnt))
			break;

		if (unlikely(tx_info->cpy_mbuf != NULL)) {
			hinic3_free_cpy_mbuf(txq->nic_dev, tx_info->cpy_mbuf);
			tx_info->cpy_mbuf = NULL;
		}
		sw_ci = (sw_ci + tx_info->wqebb_cnt) & sq_mask;

		wqebb_cnt += tx_info->wqebb_cnt;
		mbuf = tx_info->mbuf;

		if (likely(mbuf->nb_segs == 1)) {
			mbuf_temp = rte_pktmbuf_prefree_seg(mbuf);
			tx_info->mbuf = NULL;

			if (unlikely(mbuf_temp == NULL))
				continue;

			mbuf_free[nb_free++] = mbuf_temp;
			if (unlikely(mbuf_temp->pool != mbuf_free[0]->pool ||
			    nb_free >= HINIC3_MAX_TX_FREE_BULK)) {
				rte_mempool_put_bulk(mbuf_free[0]->pool,
					(void **)mbuf_free, (nb_free - 1));
				nb_free = 0;
				mbuf_free[nb_free++] = mbuf_temp;
			}
		} else {
			rte_pktmbuf_free(mbuf);
			tx_info->mbuf = NULL;
		}
	}

	if (nb_free > 0)
		rte_mempool_put_bulk(mbuf_free[0]->pool, (void **)mbuf_free,
				     nb_free);

	hinic3_update_sq_local_ci(txq, wqebb_cnt);

	return i;
}

static inline void hinic3_tx_free_mbuf_force(struct hinic3_txq *txq __rte_unused, struct rte_mbuf *m)
{
	rte_pktmbuf_free(m);
}

static void hinic3_get_ipv4_len_proto(const void *hdr, uint16_t *hdr_len, uint8_t *proto)
{
	const struct rte_ipv4_hdr *ipv4_hdr = (const struct rte_ipv4_hdr *)hdr;
	*hdr_len = (ipv4_hdr->version_ihl & RTE_IPV4_HDR_IHL_MASK) * RTE_IPV4_IHL_MULTIPLIER;
	*proto = ipv4_hdr->next_proto_id;
}

static void hinic3_get_ipv6_len_proto(const void *hdr, uint16_t *hdr_len, uint8_t *proto)
{
	hinic3_ipv6_ext_hdr *xh = NULL;
	uint8_t i;

	*proto = ((const struct rte_ipv6_hdr *)hdr)->proto;
	*hdr_len = sizeof(struct rte_ipv6_hdr);

	/*
	 * Consistent with ucode's IPV6_MAX_EXT_HDRS,
	 * maximum parsing up to IPV6_MAX_EXT_HDRS layer.
	 */
	for (i = 0; i < IPV6_MAX_EXT_HDRS; i++) {
		xh = (hinic3_ipv6_ext_hdr *)((const uint8_t *)hdr + *hdr_len);
		switch (*proto) {
			case IPPROTO_HOPOPTS:
			case IPPROTO_ROUTING:
			case IPPROTO_DSTOPTS:
				/* hdr len is a multiple of 8, excluding the first 8 bytes */
				*hdr_len += FIXED_EXT_HDR_LEN + xh->len * UNIT_BYTES_U;
				*proto = xh->next_hdr;
				break;
			case IPPROTO_AH:
				/* hdr len is a multiple of 4, excluding the first 8 bytes */
				*hdr_len += FIXED_EXT_HDR_LEN + xh->len * UNIT_BYTES_AH;
				*proto = xh->next_hdr;
				break;
			case IPPROTO_FRAGMENT:
				/* hdr len is fixed 8 bytes */
				*hdr_len += FIXED_EXT_HDR_LEN;
				*proto = xh->next_hdr;
				break;
			default:
				break;
		}
	}
}

static uint16_t
hinic3_ipv6_phdr_cksum(const struct rte_ipv6_hdr *ipv6_hdr, uint64_t ol_flags)
{
	uint32_t   sum;
	uint8_t	   proto;
	uint16_t   l3_len;
	rte_be32_t l4_len;
	rte_be32_t l4_proto;

	hinic3_get_ipv6_len_proto((const void *)ipv6_hdr, &l3_len, &proto);
	l4_proto = rte_cpu_to_be_16(proto);
	if (ol_flags & HINIC3_PKT_TX_TCP_SEG)
		l4_len = 0;
	else
		l4_len = rte_cpu_to_be_16(rte_be_to_cpu_16(ipv6_hdr->payload_len) - l3_len + sizeof(*ipv6_hdr));

#ifdef DPDK_24_11
	sum = __rte_raw_cksum(ipv6_hdr->src_addr.a, sizeof(ipv6_hdr->src_addr.a) + sizeof(ipv6_hdr->dst_addr.a), 0);
#else
	sum = __rte_raw_cksum(ipv6_hdr->src_addr, sizeof(ipv6_hdr->src_addr) + sizeof(ipv6_hdr->dst_addr), 0);
#endif
	sum = __rte_raw_cksum(&l4_len, sizeof(l4_len), sum);
	sum = __rte_raw_cksum(&l4_proto, sizeof(l4_proto), sum);

	return __rte_raw_cksum_reduce(sum);
}

static hinic3_ip_cs_handler_t g_ip_cs_handlers[] = {
	[IPV4_INDEX] = {
		.cksum_func = (uint16_t (*)(const void *, uint64_t))rte_ipv4_phdr_cksum,
		.get_len_proto = hinic3_get_ipv4_len_proto,
		.hdr_len = sizeof(struct rte_ipv4_hdr),
	},
	[IPV6_INDEX] = {
		.cksum_func = (uint16_t (*)(const void *, uint64_t))hinic3_ipv6_phdr_cksum,
		.get_len_proto = hinic3_get_ipv6_len_proto,
		.hdr_len = sizeof(struct rte_ipv6_hdr),
	}
};

static inline void hinic3_calculate_tcp_checksum(struct rte_mbuf *mbuf,
					u16 inner_l3_offset)
{
	struct rte_ipv4_hdr *ipv4_hdr;
	struct rte_ipv6_hdr *ipv6_hdr;
	struct rte_tcp_hdr *tcp_hdr;
	uint64_t ol_flags = mbuf->ol_flags;

	if (ol_flags & HINIC3_PKT_TX_IPV4) {
		ipv4_hdr = rte_pktmbuf_mtod_offset(mbuf, struct rte_ipv4_hdr *,
							inner_l3_offset);

		if (ol_flags & HINIC3_PKT_TX_IP_CKSUM)
			ipv4_hdr->hdr_checksum = 0;

		tcp_hdr = (struct rte_tcp_hdr *)((char *)ipv4_hdr +
						mbuf->l3_len);
		tcp_hdr->cksum = rte_ipv4_phdr_cksum(ipv4_hdr, ol_flags);
	} else {
		ipv6_hdr = rte_pktmbuf_mtod_offset(mbuf, struct rte_ipv6_hdr *,
							inner_l3_offset);
		tcp_hdr = rte_pktmbuf_mtod_offset(mbuf, struct rte_tcp_hdr *,
							(inner_l3_offset +
							mbuf->l3_len));
		tcp_hdr->cksum = hinic3_ipv6_phdr_cksum(ipv6_hdr, ol_flags);
	}

	return;
}

static inline void hinic3_calculate_udp_checksum(struct rte_mbuf *mbuf,
					u16 inner_l3_offset)
{
	struct rte_ipv4_hdr *ipv4_hdr;
	struct rte_ipv6_hdr *ipv6_hdr;
	struct rte_udp_hdr *udp_hdr;
	uint64_t ol_flags = mbuf->ol_flags;

	if (ol_flags & HINIC3_PKT_TX_IPV4) {
		ipv4_hdr = rte_pktmbuf_mtod_offset(mbuf, struct rte_ipv4_hdr *,
							inner_l3_offset);

		if (ol_flags & HINIC3_PKT_TX_IP_CKSUM)
			ipv4_hdr->hdr_checksum = 0;

		udp_hdr = (struct rte_udp_hdr *)((char *)ipv4_hdr +
						mbuf->l3_len);
		udp_hdr->dgram_cksum = rte_ipv4_phdr_cksum(ipv4_hdr, ol_flags);
	} else {
		ipv6_hdr = rte_pktmbuf_mtod_offset(mbuf, struct rte_ipv6_hdr *,
							inner_l3_offset);
		udp_hdr = rte_pktmbuf_mtod_offset(mbuf, struct rte_udp_hdr *,
							(inner_l3_offset +
							mbuf->l3_len));
		udp_hdr->dgram_cksum = hinic3_ipv6_phdr_cksum(ipv6_hdr, ol_flags);
	}

	return;
}

static inline void hinic3_calculate_checksum(struct rte_mbuf *mbuf,
					u16 inner_l3_offset)
{
	uint64_t ol_flags = mbuf->ol_flags;

	switch (ol_flags & HINIC3_PKT_TX_L4_MASK) {
		case HINIC3_PKT_TX_UDP_CKSUM:
			hinic3_calculate_udp_checksum(mbuf, inner_l3_offset);
			break;

		case HINIC3_PKT_TX_TCP_CKSUM:
			hinic3_calculate_tcp_checksum(mbuf, inner_l3_offset);
			break;

		case HINIC3_PKT_TX_SCTP_CKSUM:
			/* Sctp csum no need to calculate pseudo-header */
			break;
		default:
			if (ol_flags & HINIC3_PKT_TX_TCP_SEG)
				hinic3_calculate_tcp_checksum(mbuf, inner_l3_offset);
			break;
	}

	return;
}

static inline bool
hinic3_is_ipinip(struct rte_mbuf *mbuf)
{
	uint64_t ol_flags;
	uint32_t pkt_type;

	ol_flags = mbuf->ol_flags & HINIC3_PKT_TX_TUNNEL_MASK;
	pkt_type = mbuf->packet_type & RTE_PTYPE_TUNNEL_MASK;

	if (ol_flags == HINIC3_PKT_TX_TUNNEL_IPIP || pkt_type == RTE_PTYPE_TUNNEL_IP)
		return true;

	return false;
}

static int
hinic3_tx_offload_pkt_prepare(struct rte_mbuf *mbuf, u16 *inner_l3_offset)
{
	uint64_t ol_flags = mbuf->ol_flags;

	/* Tunnel flag should be deleted in outer gre checksum */
	if ((ol_flags & HINIC3_PKT_TX_TUNNEL_MASK) == HINIC3_PKT_TX_TUNNEL_GRE) {
		ol_flags &= ~ HINIC3_PKT_TX_TUNNEL_MASK;
	}

	/* Vxlan and Geneve offload */
	if ((ol_flags & HINIC3_PKT_TX_TUNNEL_MASK) &&
		!(ol_flags & (HINIC3_PKT_TX_TUNNEL_VXLAN | HINIC3_PKT_TX_TUNNEL_GENEVE | HINIC3_PKT_TX_TUNNEL_VXLAN_GPE)))
		return -EINVAL;

	if (hinic3_is_ipinip(mbuf))
		return 0;

#ifdef RTE_LIBRTE_ETHDEV_DEBUG
	if (rte_validate_tx_offload(mbuf) != 0)
		return -EINVAL;
#endif
	if ((ol_flags & HINIC3_PKT_TX_TUNNEL_MASK)) {
		if ((ol_flags & HINIC3_PKT_TX_OUTER_IP_CKSUM) ||
		    (ol_flags & HINIC3_PKT_TX_OUTER_IPV6) ||
		    (ol_flags & HINIC3_PKT_TX_TCP_SEG)) {
			/*
			 * For this senmatic, l2_len of mbuf means
			 * len(out_udp + vxlan/geneve + in_eth)
			 */
			*inner_l3_offset = mbuf->l2_len + mbuf->outer_l2_len +
					   mbuf->outer_l3_len;
		} else {
			/*
			 * For this senmatic, l2_len of mbuf means
			 * len(out_eth + out_ip + out_udp + vxlan + in_eth)
			 */
			*inner_l3_offset = mbuf->l2_len;
		}
	} else {
		/* For non-tunnel type pkts */
		*inner_l3_offset = mbuf->l2_len;
	}

	/* Process the pseudo-header checksum */
	hinic3_calculate_checksum(mbuf, *inner_l3_offset);

	return 0;
}

static void hinic3_tx_set_compact_task_offload(struct hinic3_wqe_info *wqe_info,
					struct hinic3_sq_wqe_combo *wqe_combo)
{
	struct hinic3_sq_task *task = wqe_combo->task;
	struct hinic3_offload_info *offload_info = &wqe_info->offload_info;

	task->pkt_info0 = 0;
	wqe_combo->task->pkt_info0 =
			SQ_TASK_INFO_SET(offload_info->out_l3_en, OUT_L3_EN) |
			SQ_TASK_INFO_SET(offload_info->out_l4_en, OUT_L4_EN) |
			SQ_TASK_INFO_SET(offload_info->inner_l3_en, INNER_L3_EN) |
			SQ_TASK_INFO_SET(offload_info->inner_l4_en, INNER_L4_EN) |
			SQ_TASK_INFO_SET(offload_info->vlan_valid, VLAN_VALID) |
			SQ_TASK_INFO_SET(offload_info->vlan_sel, VLAN_SEL) |
			SQ_TASK_INFO_SET(offload_info->vlan_tag, VLAN_TAG);

	task->pkt_info0 = hinic3_hw_be32(task->pkt_info0);
}

static bool hinic3_vxlan_out_udp_cksum_needed(hinic3_ip_cs_handler_t *ip_handler, uint8_t *pkt_data,
	struct rte_mbuf *mbuf) {
	struct rte_udp_hdr *udp_hdr = NULL;
	uint8_t proto;

	ip_handler->get_len_proto(pkt_data, &(ip_handler->hdr_len), &proto);
	if (proto == IPPROTO_UDP) {
		udp_hdr = (struct rte_udp_hdr*)(pkt_data + ip_handler->hdr_len);
		if (udp_hdr->dgram_cksum == 0x0 && ((mbuf->ol_flags & HINIC3_PKT_TX_OUTER_UDP_CKSUM) == 0)) {
			return false;
		}
	}

	return true;
}

static uint8_t hinic3_ip_phdr_cksum(hinic3_ip_cs_handler_t *ip_handler, uint8_t *pkt_data, struct rte_mbuf *mbuf)
{
	struct rte_tcp_hdr *tcp_hdr = NULL;
	struct rte_udp_hdr *udp_hdr = NULL;
	uint8_t proto;

	ip_handler->get_len_proto(pkt_data, &(ip_handler->hdr_len), &proto);
	if (proto == IPPROTO_TCP) {
		tcp_hdr = (struct rte_tcp_hdr *)(pkt_data + ip_handler->hdr_len);
		tcp_hdr->cksum = ip_handler->cksum_func(pkt_data, mbuf->ol_flags);
	} else if (proto == IPPROTO_UDP) {
		udp_hdr = (struct rte_udp_hdr *)(pkt_data + ip_handler->hdr_len);
		udp_hdr->dgram_cksum = ip_handler->cksum_func(pkt_data, mbuf->ol_flags);
	}

	return proto;
}

static inline uint8_t hinic3_check_ip_version(uint8_t version)
{
	switch (version) {
		case IPV4_VERSION:
			return IPV4_INDEX;
		case IPV6_VERSION:
			return IPV6_INDEX;
		default:
			return IP_INDEX_INVALID;
	}
}

static hinic3_ip_cs_handler_t* hinic3_get_outer_l3_hdr(struct rte_mbuf *mbuf, uint16_t *offset, uint8_t **ip_hdr)
{
	struct rte_ether_hdr *eth_hdr = NULL;
	struct rte_vlan_hdr *vlan_hdr = NULL;
	uint8_t version, ver_index;
	uint16_t ether_type;

	uint8_t *pkt_data = rte_pktmbuf_mtod(mbuf, uint8_t *);
	eth_hdr = (struct rte_ether_hdr *)pkt_data;
	*offset += sizeof(struct rte_ether_hdr);
	ether_type = eth_hdr->ether_type;
	while (ether_type == rte_cpu_to_be_16(RTE_ETHER_TYPE_VLAN) || ether_type == rte_cpu_to_be_16(RTE_ETHER_TYPE_QINQ)) {
		vlan_hdr = (struct rte_vlan_hdr *)(pkt_data + *offset);
		ether_type = vlan_hdr->eth_proto;
		*offset += sizeof(struct rte_vlan_hdr);
	}

	*ip_hdr = (uint8_t *)(pkt_data + *offset);
	version = (**ip_hdr >> 4) & 0x0F;
	ver_index = hinic3_check_ip_version(version);
	if (ver_index == IP_INDEX_INVALID) {
		PMD_DRV_LOG(ERR, "not support outer l3 version(%u) by IPinIP checksum", version);
		return NULL;
	}

	return &g_ip_cs_handlers[ver_index];
}

static int hinic3_vxlan_tso_ip_phdr_cksum(struct rte_mbuf *mbuf)
{
    uint8_t *outer_ip_hdr = NULL;
	uint8_t *ip_hdr = NULL;
	uint8_t version, ver_index, l4_proto;
	uint16_t offset = 0;
	hinic3_ip_cs_handler_t *ip_handler = NULL;
	uint8_t *pkt_data = rte_pktmbuf_mtod(mbuf, uint8_t *);

	/* outer UDP phdr checksum */
	ip_handler = hinic3_get_outer_l3_hdr(mbuf, &offset, &outer_ip_hdr);
	if (ip_handler == NULL) {
		PMD_DRV_LOG(INFO, "not support outer l3 proto by vxlan checksum, check packet");
		return -EINVAL;
	}
	/* if outer UDP checksum is 0 and OUT_UDP is not set, not calculate */
	if (hinic3_vxlan_out_udp_cksum_needed(ip_handler, outer_ip_hdr, mbuf)) {
		l4_proto = hinic3_ip_phdr_cksum(ip_handler, outer_ip_hdr, mbuf);
		if (unlikely((mbuf->ol_flags & HINIC3_PKT_TX_TUNNEL_MASK) &&
		   (l4_proto != IPPROTO_UDP))) {
			PMD_DRV_LOG(INFO, "not support outer l4 proto(%u) by vxlan checksum", l4_proto);
			return -EINVAL;
		}
	}
	if (mbuf->ol_flags & HINIC3_PKT_TX_TUNNEL_MASK) {
		offset += ip_handler->hdr_len + sizeof(struct rte_udp_hdr) + sizeof(struct rte_vxlan_hdr) +
			sizeof(struct rte_ether_hdr);
		ip_hdr = (uint8_t *)(pkt_data + offset);
		version = (*ip_hdr >> 4) & 0x0F;
		ver_index = hinic3_check_ip_version(version);
		if (ver_index == IP_INDEX_INVALID) {
			PMD_DRV_LOG(INFO, "not support inner l3 version(%u) by vxlan checksum", version);
			return -EINVAL;
		}

		/* calculate outer TCP phdr checksum */
		ip_handler = &g_ip_cs_handlers[ver_index];
		l4_proto = hinic3_ip_phdr_cksum(ip_handler, ip_hdr, mbuf);
		if (unlikely(l4_proto != IPPROTO_TCP)) {
			PMD_DRV_LOG(INFO, "not support inner l4 proto(%u) by vxlan checksum", l4_proto);
			return -EINVAL;
		}
	}

	return 0;
}

static u16 hinic3_ipv6_udptcp_cksum(struct rte_mbuf *mbuf, const struct rte_ipv6_hdr *ipv6_hdr, const void *l4_hdr)
{
	u16 cksum;
	u32 sum, l4_len;

	hinic3_ip_cs_handler_t *ip_handler = NULL;
	ip_handler = &g_ip_cs_handlers[IPV6_INDEX];
	l4_len = rte_be_to_cpu_16(ipv6_hdr->payload_len);
	sum = __rte_raw_cksum(l4_hdr, l4_len, 0);
	sum = __rte_raw_cksum_reduce(sum);
	sum += ip_handler->cksum_func(ipv6_hdr, mbuf->ol_flags);

	cksum = (u16)((sum & 0xffff0000) >> 16) + (sum & 0xffff);
	cksum = ~cksum;

	/*
	 * Per RFC 768: If the computed checksum is zero for UDP,
	 * it is transmitted as all ones
	 * (the equivalent in one's complement arithmetic).
	 */
	if (cksum == 0 && ipv6_hdr->proto == IPPROTO_UDP)
		cksum = 0xffff;

	return cksum;
}

static u16 hinic3_get_udptcp_checksum(struct rte_mbuf *mbuf, void *l3_hdr, void *l4_hdr)
{
	u8 version;
	version = *(u8*)l3_hdr >> 4;
	if (version == 4)
		return rte_ipv4_udptcp_cksum(l3_hdr, l4_hdr);
	else
		return hinic3_ipv6_udptcp_cksum(mbuf, l3_hdr, l4_hdr);
}

static void hinic3_process_inner_cksums(void *l3_hdr, struct rte_mbuf *mbuf)
{
	u8 l4_proto, version, ver_index;
	struct rte_udp_hdr *udp_hdr = NULL;
	struct rte_tcp_hdr *tcp_hdr = NULL;
	struct rte_ipv4_hdr *ipv4_hdr = NULL;
	hinic3_ip_cs_handler_t *ip_handler = NULL;
	uint16_t inner_ip_total_len = 0;
	uint16_t mbuf_data_len = 0;
	bool is_inner_fragmented = false;

	version = (*(uint8_t *)l3_hdr) >> 4;
	ver_index = hinic3_check_ip_version(version);
	if (ver_index == IP_INDEX_INVALID) {
		PMD_DRV_LOG(ERR, "Invalid IP version %u", version);
		return;
	}
	ip_handler = &g_ip_cs_handlers[ver_index];
	if (version == IPV4_VERSION) {
		ip_handler->get_len_proto(l3_hdr, &(ip_handler->hdr_len), &l4_proto);
		ipv4_hdr = l3_hdr;

		/* Check if inner IP packet is fragmented */
		inner_ip_total_len = rte_be_to_cpu_16(ipv4_hdr->total_length);

		/* Get actual data length in mbuf (from inner IP header to end) */
		mbuf_data_len = rte_pktmbuf_data_len(mbuf) -
				((uint8_t *)l3_hdr - rte_pktmbuf_mtod(mbuf, uint8_t *));

		/* If inner IP total length > mbuf data length, inner IP is fragmented */
		if (inner_ip_total_len > mbuf_data_len) {
			is_inner_fragmented = true;
			PMD_DRV_LOG(DEBUG, "Inner IP is fragmented: total_len=%u, mbuf_len=%u",
				    inner_ip_total_len, mbuf_data_len);
		}

		ipv4_hdr->hdr_checksum = 0;
		ipv4_hdr->hdr_checksum = rte_ipv4_cksum(ipv4_hdr);
	} else {
		ip_handler->get_len_proto(l3_hdr, &(ip_handler->hdr_len), &l4_proto);
	}

	if (l4_proto == IPPROTO_UDP) {
		udp_hdr = (struct rte_udp_hdr *)((char *)l3_hdr + ip_handler->hdr_len);
		udp_hdr->dgram_cksum = 0;
		/* For fragmented inner IP (outer IP fragmented), rte_ipv4_udptcp_cksum
		 * uses inner IP total_length field. However, if mbuf data is incomplete,
		 * we need to temporarily adjust inner IP total_length to match mbuf
		 * data length for correct checksum calculation.
		 */
		if (is_inner_fragmented && version == IPV4_VERSION) {
			uint16_t saved_total_len = ipv4_hdr->total_length;
			ipv4_hdr->total_length = rte_cpu_to_be_16(mbuf_data_len);
			udp_hdr->dgram_cksum = hinic3_get_udptcp_checksum(mbuf, l3_hdr, udp_hdr);
			ipv4_hdr->total_length = saved_total_len;
		} else {
			udp_hdr->dgram_cksum = hinic3_get_udptcp_checksum(mbuf, l3_hdr, udp_hdr);
		}
	} else if (l4_proto == IPPROTO_TCP){
		tcp_hdr = (struct rte_tcp_hdr *)((char *)l3_hdr + ip_handler->hdr_len);
		tcp_hdr->cksum = 0;
		/* For fragmented inner IP (outer IP fragmented), rte_ipv4_udptcp_cksum
		 * uses inner IP total_length field. However, if mbuf data is incomplete,
		 * we need to temporarily adjust inner IP total_length to match mbuf
		 * data length for correct checksum calculation.
		 * This ensures TCP checksum is calculated based on actual data in mbuf,
		 * not the full inner IP packet length.
		 */
		if (is_inner_fragmented && version == 4) {
			uint16_t saved_total_len = ipv4_hdr->total_length;
			ipv4_hdr->total_length = rte_cpu_to_be_16(mbuf_data_len);
			tcp_hdr->cksum = hinic3_get_udptcp_checksum(mbuf, l3_hdr, tcp_hdr);
			ipv4_hdr->total_length = saved_total_len;
		} else {
			tcp_hdr->cksum = hinic3_get_udptcp_checksum(mbuf, l3_hdr, tcp_hdr);
		}
	} else if (l4_proto == IPPROTO_SCTP) {
		PMD_DRV_LOG(ERR, "sctp cksum not support");
	} else {
		switch (l4_proto) {
		case IPPROTO_HOPOPTS:
		case IPPROTO_ROUTING:
		case IPPROTO_DSTOPTS:
		case IPPROTO_AH:
		case IPPROTO_FRAGMENT:
			PMD_DRV_LOG(ERR, "ext hdr exceed the number of parsed");
			break;
		default:
			break;
		}
	}
}

static int hinic3_ipinip_cksum(struct rte_mbuf *mbuf)
{
	hinic3_ip_cs_handler_t *ip_handler = NULL;
	uint8_t *ip_hdr = NULL;
	void *inner_ip_hdr = NULL;
	uint16_t offset = 0;
	u8 proto;
	uint16_t fragment_offset;
	bool is_first_fragment = true;
	struct rte_ipv4_hdr *ipv4_hdr = NULL;
	uint8_t *pkt_data = rte_pktmbuf_mtod(mbuf, uint8_t *);

	ip_handler = hinic3_get_outer_l3_hdr(mbuf, &offset, &ip_hdr);
	if (ip_handler == NULL) {
		PMD_DRV_LOG(ERR, "not support outer l3 proto by IPinIP checksum, check packet");
		return -EINVAL;
	}

	ip_hdr = pkt_data + offset;
	ip_handler->get_len_proto(ip_hdr, &(ip_handler->hdr_len), &proto);
	if (proto != IPPROTO_IPIP && proto != IPPROTO_IPV6) {
		PMD_DRV_LOG(ERR, "packet is wrong, outer IP proto=%u, expected 4 or 41", proto);
		return -EINVAL;
	}

	/* Check if this is the first fragment (only for inner checksum calculation) */
	if (ip_handler == &g_ip_cs_handlers[IPV4_INDEX]) {
		ipv4_hdr = (struct rte_ipv4_hdr *)ip_hdr;
		fragment_offset = rte_be_to_cpu_16(ipv4_hdr->fragment_offset);
		/* Check if this is the first fragment
		 * IPv4 fragment_offset field: lower 13 bits are offset (in 8-byte units)
		 */
		is_first_fragment = ((fragment_offset & 0x1FFF) == 0);

		/* Skip outer IP header checksum calculation - not needed */

		/* If fragment_offset != 0, this is not the first fragment,
		 * and inner IP header is not present in this fragment.
		 * Skip inner checksum processing to avoid segmentation fault.
		 */
		if (!is_first_fragment) {
			PMD_DRV_LOG(INFO, "Outer IP fragment (offset=%u), skip inner checksum",
				    fragment_offset & 0x1FFF);
			return 0;
		}
	}
	/* For IPv6, there is no IP header checksum */
	else if (ip_handler == &g_ip_cs_handlers[IPV6_INDEX]) {
		/* IPv6 fragmentation is handled via Fragment Extension Header,
		 * which is parsed by hinic3_get_ipv6_len_proto.
		 * If fragment extension header exists, proto will be set accordingly.
		 * For simplicity, we assume inner IP header is present if proto is 4 or 41.
		 */
		/* IPv6 outer header checksum is not needed (IPv6 has no header checksum) */
	}

	/* Process inner IP and L4 checksums (only for first fragment) */
	offset += ip_handler->hdr_len;
	inner_ip_hdr = (uint8_t *)(pkt_data + offset);
	hinic3_process_inner_cksums(inner_ip_hdr, mbuf);

	return 0;
}

static int hinic3_set_tx_offload_compact_cqe(struct hinic3_nic_dev *nic_dev,
				 struct rte_mbuf *mbuf,
				 struct hinic3_sq_wqe_combo *wqe_combo,
				 struct hinic3_wqe_info *wqe_info)
{
	uint64_t ol_flags = mbuf->ol_flags;
	struct hinic3_offload_info *offload_info = &wqe_info->offload_info;

	/* Vlan offload. */
	if (unlikely(ol_flags & HINIC3_PKT_TX_VLAN_PKT)) {
		offload_info->vlan_valid = 1;
		offload_info->vlan_tag = mbuf->vlan_tci;
		offload_info->vlan_sel = HINIC3_TX_TPID0;
	}

	if (hinic3_is_ipinip(mbuf) && is_sp620_nic(nic_dev)) {
		if(hinic3_ipinip_cksum(mbuf) != 0)
			return -EINVAL;
	}
	if (!(ol_flags & HINIC3_TX_CKSUM_OFFLOAD_MASK))
		goto set_tx_wqe_offload;

	/* Tso offload. */
	if (ol_flags & HINIC3_PKT_TX_TCP_SEG) {
		if (hinic3_is_ipinip(mbuf)) {
			PMD_DRV_LOG(ERR, "IPinIP not support TSO");
			return -EINVAL;
		}
		wqe_info->queue_info.payload_offset = wqe_info->payload_offset >> 1;
		if ((wqe_info->payload_offset >> 1) > MAX_PAYLOAD_OFFSET)
			return -EINVAL;

		offload_info->inner_l3_en = 1;
		offload_info->inner_l4_en = 1;
		wqe_info->queue_info.tso = 1;
		wqe_info->queue_info.mss = mbuf->tso_segsz;
		/*
		 * In VXLAN TSO scene, checksum of pseudo header in inner/outer L4 layers
		 * must not include length of L4, should be set to zero.
		 */
		if (unlikely(hinic3_vxlan_tso_ip_phdr_cksum(mbuf)))
				return -EINVAL;

	} else {
		if (ol_flags & HINIC3_PKT_TX_IP_CKSUM)
			offload_info->inner_l3_en = 1;

		switch (ol_flags & HINIC3_PKT_TX_L4_MASK) {
		case HINIC3_PKT_TX_TCP_CKSUM:
		case HINIC3_PKT_TX_UDP_CKSUM:
		case HINIC3_PKT_TX_SCTP_CKSUM:
			offload_info->inner_l4_en = 1;
			break;
		case HINIC3_PKT_TX_L4_NO_CKSUM:
			break;
		default:
			PMD_DRV_LOG(INFO, "not support pkt type");
			return -EINVAL;
		}
	}

	switch (ol_flags & HINIC3_PKT_TX_TUNNEL_MASK) {
	case HINIC3_PKT_TX_TUNNEL_VXLAN:
	case HINIC3_PKT_TX_TUNNEL_VXLAN_GPE:
	case HINIC3_PKT_TX_TUNNEL_GENEVE:
		offload_info->encapsulation = 1;
		wqe_info->queue_info.udp_dp_en = 1;
		break;
	case HINIC3_PKT_TX_TUNNEL_IPIP:
		offload_info->encapsulation = 1;
		break;
	case 0:
		break;
	default:
		PMD_DRV_LOG(INFO, "not support tunnel pkt type");
		return -EINVAL;
	}

	if (ol_flags & HINIC3_PKT_TX_OUTER_IP_CKSUM)
		offload_info->out_l3_en = 1;

	if (ol_flags & HINIC3_PKT_TX_OUTER_UDP_CKSUM)
		offload_info->out_l4_en = 1;

set_tx_wqe_offload:
	hinic3_tx_set_compact_task_offload(wqe_info, wqe_combo);
	return 0;
}

static inline void hinic3_set_vlan_tx_offload(struct hinic3_sq_task *task,
					      u16 vlan_tag, u8 vlan_type)
{
	task->vlan_offload = SQ_TASK_INFO3_SET(vlan_tag, VLAN_TAG) |
			     SQ_TASK_INFO3_SET(vlan_type, VLAN_TYPE) |
			     SQ_TASK_INFO3_SET(1U, VLAN_TAG_VALID);
}

static bool hinic3_is_tso_sge_valid(struct rte_mbuf *mbuf,
				    struct hinic3_wqe_info *wqe_info)
{
	u32 total_len, limit_len, checked_len, left_len, adjust_mss;
	u32 i, max_sges, left_sges, first_len, payload_len, frag_num;
	struct rte_mbuf *mbuf_head, *mbuf_first;
	struct rte_mbuf *mbuf_pre = mbuf;

	left_sges = mbuf->nb_segs;
	mbuf_head = mbuf_first = mbuf;

	/* calculate the number of message payload frag, if it exceeds the hardware limit of 10 bits,
	 * perform packet discard processing.
	 */
	payload_len = mbuf_head->pkt_len - wqe_info->payload_offset;
	frag_num = (payload_len + mbuf_head->tso_segsz - 1) / mbuf_head->tso_segsz;
	if (frag_num > MAX_TSO_NUM_FRAG) {
		PMD_DRV_LOG(WARNING, "tso frag num over hw limit, frag_num: 0x%x", frag_num);
		return false;
	}

	/* tso sge number validation */
	if (unlikely(left_sges >= HINIC3_NONTSO_PKT_MAX_SGE)) {
		checked_len = 0;
		total_len = 0;
		first_len = 0;
		adjust_mss = mbuf->tso_segsz >= TX_MSS_MIN ?
			     mbuf->tso_segsz : TX_MSS_MIN;
		max_sges = HINIC3_NONTSO_PKT_MAX_SGE - 1;
		limit_len = adjust_mss + wqe_info->payload_offset;

		for (i = 0; (i < max_sges) && (total_len < limit_len); i++) {
			total_len += mbuf->data_len;
			mbuf_pre = mbuf;
			mbuf = mbuf->next;
		}

		/* each continues 38 mbufs segmust do one check */
		while (left_sges >= HINIC3_NONTSO_PKT_MAX_SGE) {
			if (total_len >= limit_len) {
				/* update the limit len */
				limit_len = adjust_mss;
				/* update checked len */
				checked_len += first_len;
				/* record the first len */
				first_len = mbuf_first->data_len;
				/* first mbuf move to the next */
				mbuf_first = mbuf_first->next;
				/* update total len */
				total_len -= first_len;
				left_sges--;
				i--;
				for (; (i < max_sges) &&
				     (total_len < limit_len); i++) {
					total_len += mbuf->data_len;
					mbuf_pre = mbuf;
					mbuf = mbuf->next;
				}
			} else {
				/* try to copy if not valid */
				checked_len += (total_len - mbuf_pre->data_len);

				left_len = mbuf_head->pkt_len - checked_len;
				if (left_len > HINIC3_COPY_MBUF_SIZE)
					return false;
				wqe_info->sge_cnt = (u16)(mbuf_head->nb_segs +
						    i - left_sges);
				wqe_info->cpy_mbuf_cnt = 1;

				return true;
			}
		} /* end of while */
	}

	wqe_info->sge_cnt = mbuf_head->nb_segs;
	return true;
}

static int
hinic3_non_tso_pkt_pre_process(struct rte_mbuf *mbuf,
			       struct hinic3_wqe_info *wqe_info)
{
	u16 i;
	u32 total_len = 0;
	struct rte_mbuf *mbuf_pkt = mbuf;

	if (likely(HINIC3_NONTSO_SEG_NUM_VALID(mbuf->nb_segs)))
		/* valid non-tso mbuf */
		return 0;

	/* Non-tso packet length must less than 64KB. */
	if (unlikely(mbuf->pkt_len > MAX_SINGLE_SGE_SIZE))
		return -EINVAL;

	/*
	 * Mbuf number of non-tso packet must less than the sge number
	 * that nic can support. The excess part will be copied to another
	 * mbuf.
	 */
	for (i = 0; i < (HINIC3_NONTSO_PKT_MAX_SGE - 1); i++) {
		total_len += mbuf_pkt->data_len;
		mbuf_pkt = mbuf_pkt->next;
	}

	/*
	 * Max copy mbuf size is 4KB, packet will be dropped directly,
	 * if total copy length is more than it.
	 */
	if ((u32)(total_len + HINIC3_COPY_MBUF_SIZE) < mbuf->pkt_len)
		return -EINVAL;

	wqe_info->sge_cnt = HINIC3_NONTSO_PKT_MAX_SGE;
	wqe_info->cpy_mbuf_cnt = 1;

	return 0;
}

static int
hinic3_get_tx_offload_compact_cqe(struct rte_mbuf *mbuf,
		      struct hinic3_wqe_info *wqe_info)
{
	uint64_t ol_flags = mbuf->ol_flags;
	uint16_t inner_l3_offset = 0;
	int err;

	wqe_info->sge_cnt = mbuf->nb_segs;
	wqe_info->cpy_mbuf_cnt = 0;
	/* Check if the packet set available offload flags. */
	if (!(ol_flags & HINIC3_TX_OFFLOAD_MASK)) {
		wqe_info->offload = 0;
		return hinic3_non_tso_pkt_pre_process(mbuf, wqe_info);
	}

	wqe_info->offload = 1;
	err = hinic3_tx_offload_pkt_prepare(mbuf, &inner_l3_offset);
	if (err)
		return err;

	/* Non-tso mbuf only check sge num. */
	if (likely(!(mbuf->ol_flags & HINIC3_PKT_TX_TCP_SEG))) 
		return hinic3_non_tso_pkt_pre_process(mbuf, wqe_info);

	/* Tso mbuf. */
	wqe_info->payload_offset =
		inner_l3_offset + mbuf->l3_len + mbuf->l4_len;

	/* Too many mbuf segs. */
	if (unlikely(HINIC3_TSO_SEG_NUM_INVALID(mbuf->nb_segs)))
		return -EINVAL;

	/* Check whether can cover all tso mbuf segs or not. */
	if (unlikely(!hinic3_is_tso_sge_valid(mbuf, wqe_info)))
		return -EINVAL;

	return 0;
}

static inline void hinic3_set_buf_desc(struct hinic3_sq_bufdesc *buf_descs,
				       rte_iova_t addr, u32 len)
{
	buf_descs->hi_addr = hinic3_hw_be32(upper_32_bits(addr));
	buf_descs->lo_addr = hinic3_hw_be32(lower_32_bits(addr));
	buf_descs->len  = hinic3_hw_be32(len);
	buf_descs->rsvd = 0;
}

static inline struct rte_mbuf *hinic3_alloc_cpy_mbuf(struct hinic3_nic_dev *nic_dev)
{
	return rte_pktmbuf_alloc(nic_dev->cpy_mpool);
}

static void *hinic3_copy_tx_mbuf(struct hinic3_nic_dev *nic_dev,
				 struct rte_mbuf *mbuf, u16 sge_cnt)
{
	struct rte_mbuf *dst_mbuf;
	u32 offset = 0;
	u16 i;

	if (unlikely(!nic_dev->cpy_mpool))
		return NULL;

	dst_mbuf = hinic3_alloc_cpy_mbuf(nic_dev);
	if (unlikely(!dst_mbuf))
		return NULL;

	dst_mbuf->data_off = 0;
	dst_mbuf->data_len = 0;
	for (i = 0; i < sge_cnt; i++) {
		rte_memcpy((u8 *)dst_mbuf->buf_addr + offset,
			   (u8 *)mbuf->buf_addr + mbuf->data_off,
			   mbuf->data_len);
		dst_mbuf->data_len += mbuf->data_len;
		offset += mbuf->data_len;
		mbuf = mbuf->next;
	}
	dst_mbuf->pkt_len = dst_mbuf->data_len;
	return dst_mbuf;
}

static int hinic3_mbuf_dma_map_sge(struct hinic3_txq *txq,
				   struct rte_mbuf *mbuf,
				   struct hinic3_sq_wqe_combo *wqe_combo,
				   struct hinic3_wqe_info *wqe_info)
{
	struct hinic3_sq_wqe_desc *wqe_desc = wqe_combo->hdr;
	struct hinic3_sq_bufdesc *buf_desc = wqe_combo->bds_head;
	uint16_t nb_segs = wqe_info->sge_cnt - wqe_info->cpy_mbuf_cnt;
	uint16_t real_segs = mbuf->nb_segs;
	rte_iova_t dma_addr;
	u32 i;

	for (i = 0; i < nb_segs; i++) {
		if (unlikely(mbuf == NULL)) {
			txq->txq_stats.mbuf_null++;
			return -EINVAL;
		}

		if (unlikely(mbuf->data_len == 0)) {
			txq->txq_stats.sge_len0++;
			return -EINVAL;
		}

		dma_addr = rte_mbuf_data_iova(mbuf);
		if (i == 0) {
			if ((wqe_combo->wqe_type == SQ_WQE_COMPACT_TYPE) &&
			    (mbuf->data_len > COMPACT_WQE_MAX_CTRL_LEN)) {
				txq->txq_stats.sge_len_too_large++;
				return -EINVAL;
			}

			wqe_desc->hi_addr =
				hinic3_hw_be32(upper_32_bits(dma_addr));
			wqe_desc->lo_addr =
				hinic3_hw_be32(lower_32_bits(dma_addr));
			wqe_desc->ctrl_len = mbuf->data_len;
		} else {
			/*
			 * Parts of wqe is in sq bottom while parts
			 * of wqe is in sq head
			 */
			if (unlikely(wqe_info->wrapped &&
				(u64)buf_desc == txq->sq_bot_sge_addr))
				buf_desc = (struct hinic3_sq_bufdesc *)txq->sq_head_addr;
			hinic3_set_buf_desc(buf_desc, dma_addr, mbuf->data_len);
			buf_desc++;
		}

		/*
		 * SP620: For wqe compact type, no need to prepare
		 * sq ctrl info. Need set queue_info = 0.
		 */
		if(is_sp620_nic(txq->nic_dev))
			wqe_desc->queue_info = 0;

		mbuf = mbuf->next;
	}

	/* for now: support over 38 sge, copy the last 2 mbuf */
	if (unlikely(wqe_info->cpy_mbuf_cnt != 0)) {
		/* copy invalid mbuf segs to a valid buffer, lost performance */
		txq->txq_stats.cpy_pkts += 1;
		mbuf = hinic3_copy_tx_mbuf(txq->nic_dev, mbuf,
					   real_segs - nb_segs);
		if (unlikely(!mbuf))
			return -EINVAL;

		txq->tx_info[wqe_info->pi].cpy_mbuf = mbuf;

		/* deal with the last mbuf */
		dma_addr = rte_mbuf_data_iova(mbuf);
		if (unlikely(mbuf->data_len == 0)) {
			txq->txq_stats.sge_len0++;
			return -EINVAL;
		}
		/*
		 * Parts of wqe is in sq bottom while parts
		 * of wqe is in sq head
		 */
		if (i == 0) {
			wqe_desc->hi_addr =
				hinic3_hw_be32(upper_32_bits(dma_addr));
			wqe_desc->lo_addr =
				hinic3_hw_be32(lower_32_bits(dma_addr));
			wqe_desc->ctrl_len = mbuf->data_len;
		} else {
			if (unlikely(wqe_info->wrapped && 
				(u64)buf_desc == txq->sq_bot_sge_addr))
				buf_desc = (struct hinic3_sq_bufdesc *)txq->sq_head_addr;

			hinic3_set_buf_desc(buf_desc, dma_addr, mbuf->data_len);
		}
	}

	return 0;
}

static int hinic3_mbuf_dma_map_single(struct hinic3_txq *txq,
					struct rte_mbuf *mbuf,
					struct hinic3_sq_wqe_combo *wqe_combo)
{
	struct hinic3_sq_wqe_desc *wqe_desc = wqe_combo->hdr;
	rte_iova_t dma_addr;

	if (unlikely(mbuf == NULL)) {
		txq->txq_stats.mbuf_null++;
		return -EINVAL;
	}

	if (unlikely(mbuf->data_len == 0)) {
		txq->txq_stats.sge_len0++;
		return -EINVAL;
	}

	dma_addr = rte_mbuf_data_iova(mbuf);
	wqe_desc->hi_addr = hinic3_hw_be32(upper_32_bits(dma_addr));
	wqe_desc->lo_addr = hinic3_hw_be32(lower_32_bits(dma_addr));
	wqe_desc->ctrl_len = mbuf->data_len;
	wqe_desc->queue_info = 0;

	return 0;
}

static void hinic3_prepare_sq_ctrl_compact_cqe(struct hinic3_sq_wqe_combo *wqe_combo,
				   struct hinic3_wqe_info *wqe_info)
{
	struct hinic3_queue_info *queue_info = &wqe_info->queue_info;
	struct hinic3_sq_wqe_desc *wqe_desc = wqe_combo->hdr;
	u32 *qsf = &wqe_desc->queue_info;

	wqe_desc->ctrl_len |= SQ_CTRL_SET(SQ_NORMAL_WQE, DIRECT) |
			      SQ_CTRL_SET(wqe_combo->wqe_type, EXTENDED) |
			      SQ_CTRL_SET(wqe_info->owner, OWNER);

	if (wqe_combo->wqe_type == SQ_WQE_EXTENDED_TYPE) {
		wqe_desc->ctrl_len |= SQ_CTRL_SET(wqe_info->sge_cnt, BUFDESC_NUM) |
				      SQ_CTRL_SET(wqe_combo->task_type, TASKSECT_LEN) |
				      SQ_CTRL_SET(SQ_WQE_SGL, DATA_FORMAT);

		*qsf = SQ_CTRL_QUEUE_INFO_SET(1, UC) |
		       SQ_CTRL_QUEUE_INFO_SET(queue_info->sctp, SCTP) |
		       SQ_CTRL_QUEUE_INFO_SET(queue_info->udp_dp_en, TCPUDP_CS) |
		       SQ_CTRL_QUEUE_INFO_SET(queue_info->tso, TSO) |
		       SQ_CTRL_QUEUE_INFO_SET(queue_info->ufo, UFO) |
		       SQ_CTRL_QUEUE_INFO_SET(queue_info->payload_offset, PLDOFF) |
		       SQ_CTRL_QUEUE_INFO_SET(queue_info->pkt_type, PKT_TYPE) |
		       SQ_CTRL_QUEUE_INFO_SET(queue_info->mss, MSS);

		if (!SQ_CTRL_QUEUE_INFO_GET(*qsf, MSS)) {
			*qsf |= SQ_CTRL_QUEUE_INFO_SET(TX_MSS_DEFAULT, MSS);
		} else if (SQ_CTRL_QUEUE_INFO_GET(*qsf, MSS) < TX_MSS_MIN) {
			/* MSS should not less than 80. */
			*qsf = SQ_CTRL_QUEUE_INFO_CLEAR(*qsf, MSS);
			*qsf |= SQ_CTRL_QUEUE_INFO_SET(TX_MSS_MIN, MSS);
		}
		*qsf = hinic3_hw_be32(*qsf);
	} else {
		wqe_desc->ctrl_len |= SQ_CTRL_COMPACT_QUEUE_INFO_SET(queue_info->sctp, SCTP) |
			    	      SQ_CTRL_COMPACT_QUEUE_INFO_SET(queue_info->udp_dp_en, UDP_DP_EN) |
			    	      SQ_CTRL_COMPACT_QUEUE_INFO_SET(queue_info->ufo, UFO) |
			    	      SQ_CTRL_COMPACT_QUEUE_INFO_SET(queue_info->pkt_type, PKT_TYPE);
	}

	wqe_desc->ctrl_len = hinic3_hw_be32(wqe_desc->ctrl_len);
}

u16 hinic3_xmit_pkts_compact_cqe(void *tx_queue, struct rte_mbuf **tx_pkts, u16 nb_pkts)
{
	struct hinic3_txq *txq = tx_queue;
	struct hinic3_nic_dev *nic_dev;

	if (unlikely(txq == NULL))
		return 0;

	nic_dev = txq->nic_dev;
	struct hinic3_tx_info *tx_info = NULL;
	struct rte_mbuf *mbuf_pkt = NULL;
	struct hinic3_sq_wqe_combo wqe_combo = {0};
	struct hinic3_wqe_info wqe_info = {0};
	u32 offload_err, free_cnt;
	u64 total_segments = 0;
	u64 header_len = 0;
	u64 payload_len = 0;
	u64 tx_bytes = 0;
	u16 free_wqebb_cnt, nb_tx;
	u64 tx_free_loop = 0;
	int err;

#ifdef  HINIC3_XSTAT_PROF_TX
	uint64_t t1, t2;
	t1 = rte_get_tsc_cycles();
#endif

	if (unlikely(!HINIC3_TXQ_IS_STARTED(txq)))
		return 0;

	free_cnt = txq->tx_free_thresh;
	/* Reclaim tx mbuf before xmit new packets */
	if (hinic3_get_sq_free_wqebbs(txq) < txq->tx_free_thresh)
		hinic3_xmit_mbuf_cleanup(txq, free_cnt);

	/* Tx loop routine */
	for (nb_tx = 0; nb_tx < nb_pkts; nb_tx++) {
		mbuf_pkt = *tx_pkts++;
		if (unlikely(hinic3_get_tx_offload_compact_cqe(mbuf_pkt, &wqe_info))) {
			txq->txq_stats.off_errs++;
			break;
		}

		wqe_info.wqebb_cnt = wqe_info.sge_cnt;
		if (likely(wqe_info.offload || wqe_info.wqebb_cnt > 1)) {
			if (txq->tx_wqe_compact_task) {
				/**
				 * One more wqebb is needed for compact task under two situations:
				 * 1. TSO: MSS field is needed, no available space for
				 *    compact task in compact wqe.
				 * 2. SGE number > 1: wqe is handlerd as extented wqe by nic.
				 */
				if (mbuf_pkt->ol_flags & HINIC3_PKT_TX_TCP_SEG || wqe_info.wqebb_cnt > 1)
					wqe_info.wqebb_cnt++;
			} else
				/* Use extended sq wqe with normal TS */
				wqe_info.wqebb_cnt++;
		}

		free_wqebb_cnt = hinic3_get_sq_free_wqebbs(txq);
		while (wqe_info.wqebb_cnt > free_wqebb_cnt) {
			/* Reclaim again */
			hinic3_xmit_mbuf_cleanup(txq, free_cnt);
			free_wqebb_cnt = hinic3_get_sq_free_wqebbs(txq);
			if ((tx_free_loop++) > nic_dev->config.tx_free_loop) {
				txq->txq_stats.tx_busy += (nb_pkts - nb_tx);
				goto end;
			}
		}

		/* Task or bd section maybe warpped for one wqe */
		hinic3_set_wqe_combo_compact_cqe(txq, &wqe_combo, &wqe_info);

		/* Fill tx packet offload into qsf and task field. */
		offload_err = hinic3_set_tx_offload_compact_cqe(txq->nic_dev, mbuf_pkt, &wqe_combo, &wqe_info);
		if (unlikely(offload_err)) {
			hinic3_put_sq_wqe(txq, &wqe_info);
			txq->txq_stats.off_errs++;
			break;
		}

		/* Fill sq_wqe buf_desc and bd_desc */
		if (txq->multi_segs)
			err = hinic3_mbuf_dma_map_sge(txq, mbuf_pkt, &wqe_combo, &wqe_info);
		else
			err = hinic3_mbuf_dma_map_single(txq, mbuf_pkt, &wqe_combo);
		if (err) {
			hinic3_put_sq_wqe(txq, &wqe_info);
			txq->txq_stats.off_errs++;
			break;
		}

		/* Record tx info */
		tx_info = &txq->tx_info[wqe_info.pi];
		tx_info->mbuf = mbuf_pkt;
		tx_info->wqebb_cnt = wqe_info.wqebb_cnt;

		/*
		 * For wqe compact type, no need to prepare
		 * sq ctrl info.
		 */

		hinic3_prepare_sq_ctrl_compact_cqe(&wqe_combo, &wqe_info);

		if (mbuf_pkt->ol_flags & HINIC3_PKT_TX_TCP_SEG) {
			header_len = mbuf_pkt->l2_len + mbuf_pkt->l3_len + mbuf_pkt->l4_len;
			payload_len = mbuf_pkt->pkt_len - header_len;
			total_segments += (payload_len + mbuf_pkt->tso_segsz - 1) / mbuf_pkt->tso_segsz;
		} else {
			total_segments += 1;
		}

		tx_bytes += mbuf_pkt->pkt_len;
	}

end:
	/* Update txq stats */
	if (nb_tx) {
		hinic3_write_db(txq->db_addr, txq->local_qid, (int)(txq->cos),
				SQ_CFLAG_DP,
				MASKED_QUEUE_IDX(txq, txq->prod_idx));
		txq->txq_stats.packets += total_segments;
		txq->txq_stats.bytes += tx_bytes;
	}
	txq->txq_stats.burst_pkts = nb_tx;

#ifdef HINIC3_XSTAT_PROF_TX
	/* do profiling stats */
	t2 = rte_get_tsc_cycles();
	txq->txq_stats.app_tsc = t1 - txq->prof_tx_end_tsc;
	txq->prof_tx_end_tsc = t2;
	txq->txq_stats.pmd_tsc = t2 - t1;
	txq->txq_stats.burst_pkts = nb_tx;
#endif

	return nb_tx;
}
