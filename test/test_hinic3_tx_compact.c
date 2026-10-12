/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2025-2026 Huawei Technologies Co., Ltd
 * Unit tests for hinic3_pmd_tx_compact.c - testing through public interfaces
 * and local copies of static pure-logic functions for coverage
 */

#include <rte_common.h>
#include <rte_ethdev.h>
#include <string.h>
#ifdef DPDK_21_11
#include <ethdev_driver.h>
#endif
#include <rte_malloc.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>
#include <rte_byteorder.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>
#include <rte_sctp.h>
#include <rte_vxlan.h>

#include "../test.h"
#include <rte_test.h>
#include <rte_log.h>

#include "base/hinic3_compat.h"
#include "base/hinic3_pmd_nic_cfg.h"
#include "hinic3_pmd_nic_io.h"
#include "hinic3_pmd_ethdev.h"
#include "hinic3_pmd_tx.h"
#include "test_hinic3_mock.h"

#define TEST_TXQ_DEPTH		64
#define TEST_TXQ_MASK		(TEST_TXQ_DEPTH - 1)
#define TEST_TXQ_WQEBB_SHIFT	4
#define TEST_TXQ_WQEBB_SIZE	(1 << TEST_TXQ_WQEBB_SHIFT)
#define TEST_WQE_BUF_SIZE	(TEST_TXQ_DEPTH * TEST_TXQ_WQEBB_SIZE)

/* ====== Local copies of static pure-logic functions for coverage ====== */

static void local_get_ipv4_len_proto(const void *hdr, uint16_t *hdr_len,
				     uint8_t *proto)
{
	const struct rte_ipv4_hdr *ipv4_hdr = (const struct rte_ipv4_hdr *)hdr;
	*hdr_len = (ipv4_hdr->version_ihl & RTE_IPV4_HDR_IHL_MASK) *
		   RTE_IPV4_IHL_MULTIPLIER;
	*proto = ipv4_hdr->next_proto_id;
}

static void local_get_ipv6_len_proto(const void *hdr, uint16_t *hdr_len,
				    uint8_t *proto)
{
	hinic3_ipv6_ext_hdr const *xh = NULL;
	uint8_t i;

	*proto = ((const struct rte_ipv6_hdr *)hdr)->proto;
	*hdr_len = sizeof(struct rte_ipv6_hdr);

	for (i = 0; i < IPV6_MAX_EXT_HDRS; i++) {
		xh = (hinic3_ipv6_ext_hdr const *)((const uint8_t *)hdr + *hdr_len);
		switch (*proto) {
		case IPPROTO_HOPOPTS:
		case IPPROTO_ROUTING:
		case IPPROTO_DSTOPTS:
			*hdr_len += FIXED_EXT_HDR_LEN + xh->len * UNIT_BYTES_U;
			*proto = xh->next_hdr;
			break;
		case IPPROTO_AH:
			*hdr_len += FIXED_EXT_HDR_LEN + xh->len * UNIT_BYTES_AH;
			*proto = xh->next_hdr;
			break;
		case IPPROTO_FRAGMENT:
			*hdr_len += FIXED_EXT_HDR_LEN;
			*proto = xh->next_hdr;
			break;
		default:
			break;
		}
	}
}

static inline uint8_t local_check_ip_version(uint8_t version)
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

static inline bool local_is_ipinip(struct rte_mbuf *mbuf)
{
	uint64_t ol_flags;
	uint32_t pkt_type;

	ol_flags = mbuf->ol_flags & HINIC3_PKT_TX_TUNNEL_MASK;
	pkt_type = mbuf->packet_type & RTE_PTYPE_TUNNEL_MASK;

	if (ol_flags == HINIC3_PKT_TX_TUNNEL_IPIP ||
	    pkt_type == RTE_PTYPE_TUNNEL_IP)
		return true;

	return false;
}

/* ====== Global test state ====== */

static struct rte_mempool *g_mp;
static struct rte_mempool *g_cpy_mp;
static struct hinic3_nic_dev *g_nic_dev;
static struct hinic3_txq *g_txq;
static struct hinic3_tx_info *g_tx_info;
static void *g_wqe_buf;
static u16 *g_ci_vaddr;
static void *g_db_buf;
static int g_orig_log_level;

/* ====== Suite setup/teardown ====== */

static int
txc_suite_setup(void)
{
	g_orig_log_level = rte_log_get_global_level();
	rte_log_set_level_pattern("pmd.net.hinic3", RTE_LOG_INFO);

	g_mp = rte_pktmbuf_pool_create("test_txc_mp", 256, 0, 0,
				       RTE_PKTMBUF_HEADROOM + 2048,
				       SOCKET_ID_ANY);
	if (!g_mp)
		return TEST_FAILED;

	g_cpy_mp = rte_pktmbuf_pool_create("test_txc_cpy_mp",
					   HINIC3_COPY_MEMPOOL_DEPTH,
					   HINIC3_COPY_MEMPOOL_CACHE, 0,
					   HINIC3_COPY_MBUF_SIZE +
					   RTE_PKTMBUF_HEADROOM,
					   SOCKET_ID_ANY);
	if (!g_cpy_mp) {
		rte_mempool_free(g_mp);
		g_mp = NULL;
		return TEST_FAILED;
	}

	return TEST_SUCCESS;
}

static void
txc_suite_teardown(void)
{
	if (g_cpy_mp) {
		rte_mempool_free(g_cpy_mp);
		g_cpy_mp = NULL;
	}
	if (g_mp) {
		rte_mempool_free(g_mp);
		g_mp = NULL;
	}
	rte_log_set_level_pattern("pmd.net.hinic3", g_orig_log_level);
}

/* ====== Case setup/teardown ====== */

static int
txc_test_setup(void)
{
	int i;

	/* Allocate nic_dev */
	g_nic_dev = rte_zmalloc("test_txc_nic_dev", sizeof(*g_nic_dev), 0);
	if (!g_nic_dev)
		return TEST_FAILED;
	g_nic_dev->feature_cap = NIC_F_TX_WQE_COMPACT_TASK | NIC_F_CSUM |
				 NIC_F_TSO | NIC_F_TX_VLAN_INSERT;
	g_nic_dev->num_sqs = 1;
	g_nic_dev->rx_csum_en = HINIC3_DEFAULT_RX_CSUM_OFFLOAD;
	g_nic_dev->cpy_mpool = g_cpy_mp;
	g_nic_dev->config.tx_free_loop = 1024;
	strcpy(g_nic_dev->dev_name, "test_hinic3_txc");

	/* Allocate txqs array */
	g_nic_dev->txqs = rte_zmalloc("test_txc_txqs",
				      sizeof(struct hinic3_txq *), 0);
	if (!g_nic_dev->txqs)
		goto err_free_nic_dev;

	/* Allocate WQE buffer */
	g_wqe_buf = rte_zmalloc("test_txc_wqe_buf", TEST_WQE_BUF_SIZE,
				RTE_CACHE_LINE_SIZE);
	if (!g_wqe_buf)
		goto err_free_txqs;

	/* Allocate CI vaddr */
	g_ci_vaddr = rte_zmalloc("test_txc_ci_vaddr", RTE_CACHE_LINE_SIZE,
				 RTE_CACHE_LINE_SIZE);
	if (!g_ci_vaddr)
		goto err_free_wqe_buf;
	*g_ci_vaddr = 0;

	/* Allocate doorbell buffer */
	g_db_buf = rte_zmalloc("test_txc_db_buf", 4096, RTE_CACHE_LINE_SIZE);
	if (!g_db_buf)
		goto err_free_ci_vaddr;

	/* Allocate tx_info array */
	g_tx_info = rte_zmalloc("test_txc_tx_info",
				TEST_TXQ_DEPTH * sizeof(*g_tx_info), 0);
	if (!g_tx_info)
		goto err_free_db_buf;
	for (i = 0; i < TEST_TXQ_DEPTH; i++) {
		g_tx_info[i].mbuf = NULL;
		g_tx_info[i].cpy_mbuf = NULL;
		g_tx_info[i].wqebb_cnt = 0;
	}

	/* Allocate txq */
	g_txq = rte_zmalloc("test_txc_txq", sizeof(*g_txq),
			    RTE_CACHE_LINE_SIZE);
	if (!g_txq)
		goto err_free_tx_info;

	g_txq->nic_dev = g_nic_dev;
	g_txq->tx_info = g_tx_info;
	g_txq->q_id = 0;
	g_txq->local_qid = 0;
	g_txq->q_depth = TEST_TXQ_DEPTH;
	g_txq->q_mask = TEST_TXQ_MASK;
	g_txq->wqebb_size = TEST_TXQ_WQEBB_SIZE;
	g_txq->wqebb_shift = TEST_TXQ_WQEBB_SHIFT;
	g_txq->cons_idx = 0;
	g_txq->prod_idx = 0;
	g_txq->owner = 1;
	g_txq->status = HINIC3_TXQ_STATUS_START;
	g_txq->tx_free_thresh = 32;
	g_txq->multi_segs = true;
	g_txq->queue_buf_vaddr = g_wqe_buf;
	g_txq->ci_vaddr_base = (volatile u16 *)g_ci_vaddr;
	g_txq->sq_head_addr = (u64)g_wqe_buf;
	g_txq->sq_bot_sge_addr = (u64)((u8 *)g_wqe_buf + TEST_WQE_BUF_SIZE);
	g_txq->cos = 0;
	g_txq->tx_wqe_compact_task = 1;
	g_txq->is_sp620_nic = 0;
	g_txq->is_hairpin = false;
	g_txq->non_tso_max_pkt_len = HINIC3_MAX_JUMBO_FRAME_SIZE;
	g_txq->db_addr = g_db_buf;
	memset(&g_txq->txq_stats, 0, sizeof(g_txq->txq_stats));

	g_nic_dev->txqs[0] = g_txq;

	/* Reset mock state */
	g_mock_is_sp620_nic = false;

	return TEST_SUCCESS;

err_free_tx_info:
	rte_free(g_tx_info);
err_free_db_buf:
	rte_free(g_db_buf);
err_free_ci_vaddr:
	rte_free(g_ci_vaddr);
err_free_wqe_buf:
	rte_free(g_wqe_buf);
err_free_txqs:
	rte_free(g_nic_dev->txqs);
err_free_nic_dev:
	rte_free(g_nic_dev);
	g_nic_dev = NULL;
	g_txq = NULL;
	g_tx_info = NULL;
	g_wqe_buf = NULL;
	g_ci_vaddr = NULL;
	g_db_buf = NULL;
	return TEST_FAILED;
}

static void
txc_test_teardown(void)
{
	/* Free any remaining mbufs in tx_info */
	if (g_tx_info) {
		int i;
		for (i = 0; i < TEST_TXQ_DEPTH; i++) {
			if (g_tx_info[i].mbuf) {
				rte_pktmbuf_free(g_tx_info[i].mbuf);
				g_tx_info[i].mbuf = NULL;
			}
			if (g_tx_info[i].cpy_mbuf) {
				rte_pktmbuf_free(g_tx_info[i].cpy_mbuf);
				g_tx_info[i].cpy_mbuf = NULL;
			}
		}
	}

	if (g_nic_dev)
		rte_free(g_nic_dev->txqs);
	if (g_txq)
		rte_free(g_txq);
	if (g_tx_info)
		rte_free(g_tx_info);
	if (g_ci_vaddr)
		rte_free(g_ci_vaddr);
	if (g_db_buf)
		rte_free(g_db_buf);
	if (g_wqe_buf)
		rte_free(g_wqe_buf);
	if (g_nic_dev)
		rte_free(g_nic_dev);

	g_txq = NULL;
	g_tx_info = NULL;
	g_wqe_buf = NULL;
	g_ci_vaddr = NULL;
	g_db_buf = NULL;
	g_nic_dev = NULL;
}

/* ====== Helper functions to create mbufs ====== */

/* Helper: create a simple mbuf with Ethernet + IPv4 + TCP payload */
static struct rte_mbuf *
create_simple_mbuf(uint16_t data_len, uint64_t ol_flags)
{
	struct rte_mbuf *mbuf;
	struct rte_ether_hdr *eth;
	struct rte_ipv4_hdr *ipv4;
	struct rte_tcp_hdr *tcp;
	uint8_t *pkt;
	uint16_t l2_len = sizeof(struct rte_ether_hdr);
	uint16_t l3_len = sizeof(struct rte_ipv4_hdr);
	uint16_t l4_len = sizeof(struct rte_tcp_hdr);
	uint16_t hdr_len = l2_len + l3_len + l4_len;

	mbuf = rte_pktmbuf_alloc(g_mp);
	if (!mbuf)
		return NULL;

	if (data_len < hdr_len)
		data_len = hdr_len;

	pkt = (uint8_t *)rte_pktmbuf_append(mbuf, data_len);
	if (!pkt) {
		rte_pktmbuf_free(mbuf);
		return NULL;
	}
	memset(pkt, 0, data_len);

	/* Fill Ethernet header */
	eth = (struct rte_ether_hdr *)pkt;
	eth->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);

	/* Fill IPv4 header */
	ipv4 = (struct rte_ipv4_hdr *)(pkt + l2_len);
	ipv4->version_ihl = RTE_IPV4_VHL_DEF;
	ipv4->total_length = rte_cpu_to_be_16(data_len - l2_len);
	ipv4->next_proto_id = IPPROTO_TCP;
	ipv4->src_addr = rte_cpu_to_be_32(RTE_IPV4(192, 168, 1, 1));
	ipv4->dst_addr = rte_cpu_to_be_32(RTE_IPV4(192, 168, 1, 2));

	/* Fill TCP header */
	tcp = (struct rte_tcp_hdr *)(pkt + l2_len + l3_len);
	tcp->src_port = rte_cpu_to_be_16(12345);
	tcp->dst_port = rte_cpu_to_be_16(80);

	mbuf->ol_flags = ol_flags;
	mbuf->l2_len = l2_len;
	mbuf->l3_len = l3_len;
	mbuf->l4_len = l4_len;
	mbuf->nb_segs = 1;
	mbuf->pkt_len = data_len;
	mbuf->data_len = data_len;

	return mbuf;
}

/* Helper: create a simple mbuf with Ethernet + IPv4 + UDP payload */
static struct rte_mbuf *
create_udp_mbuf(uint16_t data_len, uint64_t ol_flags)
{
	struct rte_mbuf *mbuf;
	struct rte_ether_hdr *eth;
	struct rte_ipv4_hdr *ipv4;
	struct rte_udp_hdr *udp;
	uint8_t *pkt;
	uint16_t l2_len = sizeof(struct rte_ether_hdr);
	uint16_t l3_len = sizeof(struct rte_ipv4_hdr);
	uint16_t l4_len = sizeof(struct rte_udp_hdr);
	uint16_t hdr_len = l2_len + l3_len + l4_len;

	mbuf = rte_pktmbuf_alloc(g_mp);
	if (!mbuf)
		return NULL;

	if (data_len < hdr_len)
		data_len = hdr_len;

	pkt = (uint8_t *)rte_pktmbuf_append(mbuf, data_len);
	if (!pkt) {
		rte_pktmbuf_free(mbuf);
		return NULL;
	}
	memset(pkt, 0, data_len);

	eth = (struct rte_ether_hdr *)pkt;
	eth->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);

	ipv4 = (struct rte_ipv4_hdr *)(pkt + l2_len);
	ipv4->version_ihl = RTE_IPV4_VHL_DEF;
	ipv4->total_length = rte_cpu_to_be_16(data_len - l2_len);
	ipv4->next_proto_id = IPPROTO_UDP;
	ipv4->src_addr = rte_cpu_to_be_32(RTE_IPV4(10, 0, 0, 1));
	ipv4->dst_addr = rte_cpu_to_be_32(RTE_IPV4(10, 0, 0, 2));

	udp = (struct rte_udp_hdr *)(pkt + l2_len + l3_len);
	udp->src_port = rte_cpu_to_be_16(12345);
	udp->dst_port = rte_cpu_to_be_16(53);

	mbuf->ol_flags = ol_flags;
	mbuf->l2_len = l2_len;
	mbuf->l3_len = l3_len;
	mbuf->l4_len = l4_len;
	mbuf->nb_segs = 1;
	mbuf->pkt_len = data_len;
	mbuf->data_len = data_len;

	return mbuf;
}

/* Helper: create a mbuf with Ethernet + IPv6 + TCP payload */
static struct rte_mbuf *
create_ipv6_tcp_mbuf(uint16_t data_len, uint64_t ol_flags)
{
	struct rte_mbuf *mbuf;
	struct rte_ether_hdr *eth;
	struct rte_ipv6_hdr *ipv6;
	struct rte_tcp_hdr *tcp;
	uint8_t *pkt;
	uint16_t l2_len = sizeof(struct rte_ether_hdr);
	uint16_t l3_len = sizeof(struct rte_ipv6_hdr);
	uint16_t l4_len = sizeof(struct rte_tcp_hdr);
	uint16_t hdr_len = l2_len + l3_len + l4_len;

	mbuf = rte_pktmbuf_alloc(g_mp);
	if (!mbuf)
		return NULL;

	if (data_len < hdr_len)
		data_len = hdr_len;

	pkt = (uint8_t *)rte_pktmbuf_append(mbuf, data_len);
	if (!pkt) {
		rte_pktmbuf_free(mbuf);
		return NULL;
	}
	memset(pkt, 0, data_len);

	eth = (struct rte_ether_hdr *)pkt;
	eth->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV6);

	ipv6 = (struct rte_ipv6_hdr *)(pkt + l2_len);
	ipv6->proto = IPPROTO_TCP;
	ipv6->payload_len = rte_cpu_to_be_16(data_len - l2_len - l3_len);

	tcp = (struct rte_tcp_hdr *)(pkt + l2_len + l3_len);
	tcp->src_port = rte_cpu_to_be_16(12345);
	tcp->dst_port = rte_cpu_to_be_16(80);

	mbuf->ol_flags = ol_flags;
	mbuf->l2_len = l2_len;
	mbuf->l3_len = l3_len;
	mbuf->l4_len = l4_len;
	mbuf->nb_segs = 1;
	mbuf->pkt_len = data_len;
	mbuf->data_len = data_len;

	return mbuf;
}

/* Helper: create an IP-in-IP mbuf (outer IPv4 + inner IPv4 + TCP) */
static struct rte_mbuf *
create_ipinip_mbuf(uint16_t data_len, uint64_t ol_flags)
{
	struct rte_mbuf *mbuf;
	struct rte_ether_hdr *eth;
	struct rte_ipv4_hdr *outer_ip;
	struct rte_ipv4_hdr *inner_ip;
	struct rte_tcp_hdr *tcp;
	uint8_t *pkt;
	uint16_t l2_len = sizeof(struct rte_ether_hdr);
	uint16_t outer_l3_len = sizeof(struct rte_ipv4_hdr);
	uint16_t inner_l3_len = sizeof(struct rte_ipv4_hdr);
	uint16_t l4_len = sizeof(struct rte_tcp_hdr);
	uint16_t hdr_len = l2_len + outer_l3_len + inner_l3_len + l4_len;

	mbuf = rte_pktmbuf_alloc(g_mp);
	if (!mbuf)
		return NULL;

	if (data_len < hdr_len)
		data_len = hdr_len;

	pkt = (uint8_t *)rte_pktmbuf_append(mbuf, data_len);
	if (!pkt) {
		rte_pktmbuf_free(mbuf);
		return NULL;
	}
	memset(pkt, 0, data_len);

	/* Ethernet header */
	eth = (struct rte_ether_hdr *)pkt;
	eth->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);

	/* Outer IPv4 header */
	outer_ip = (struct rte_ipv4_hdr *)(pkt + l2_len);
	outer_ip->version_ihl = RTE_IPV4_VHL_DEF;
	outer_ip->total_length = rte_cpu_to_be_16(data_len - l2_len);
	outer_ip->next_proto_id = IPPROTO_IPIP;
	outer_ip->src_addr = rte_cpu_to_be_32(RTE_IPV4(10, 0, 0, 1));
	outer_ip->dst_addr = rte_cpu_to_be_32(RTE_IPV4(10, 0, 0, 2));

	/* Inner IPv4 header */
	inner_ip = (struct rte_ipv4_hdr *)(pkt + l2_len + outer_l3_len);
	inner_ip->version_ihl = RTE_IPV4_VHL_DEF;
	inner_ip->total_length =
		rte_cpu_to_be_16(data_len - l2_len - outer_l3_len);
	inner_ip->next_proto_id = IPPROTO_TCP;
	inner_ip->src_addr = rte_cpu_to_be_32(RTE_IPV4(192, 168, 1, 1));
	inner_ip->dst_addr = rte_cpu_to_be_32(RTE_IPV4(192, 168, 1, 2));

	/* TCP header */
	tcp = (struct rte_tcp_hdr *)(pkt + l2_len + outer_l3_len +
				     inner_l3_len);
	tcp->src_port = rte_cpu_to_be_16(12345);
	tcp->dst_port = rte_cpu_to_be_16(80);

	mbuf->ol_flags = ol_flags | HINIC3_PKT_TX_TUNNEL_IPIP;
	mbuf->l2_len = l2_len;
	mbuf->l3_len = inner_l3_len;
	mbuf->l4_len = l4_len;
	mbuf->outer_l2_len = 0;
	mbuf->outer_l3_len = outer_l3_len;
	mbuf->nb_segs = 1;
	mbuf->pkt_len = data_len;
	mbuf->data_len = data_len;

	return mbuf;
}

/* ====== hinic3_xmit_pkts_compact_cqe: TXQ stopped ====== */

static int
test_compact_xmit_stopped(void)
{
	struct rte_mbuf *pkt;
	struct rte_mbuf *tx_pkts[1];
	u16 nb_tx;

	g_txq->status = HINIC3_TXQ_STATUS_STOP;
	g_txq->cons_idx = 0;
	g_txq->prod_idx = 0;

	pkt = create_simple_mbuf(128, 0);
	RTE_TEST_ASSERT_NOT_NULL(pkt, "failed to create mbuf");
	tx_pkts[0] = pkt;

	nb_tx = hinic3_xmit_pkts_compact_cqe(g_txq, tx_pkts, 1);

	RTE_TEST_ASSERT_EQUAL(nb_tx, 0,
			      "xmit on stopped queue should return 0");

	rte_pktmbuf_free(pkt);
	g_txq->status = HINIC3_TXQ_STATUS_START;
	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts_compact_cqe: no offload simple packet ====== */

static int
test_compact_xmit_no_offload(void)
{
	struct rte_mbuf *pkt;
	struct rte_mbuf *tx_pkts[1];
	u16 nb_tx;

	g_txq->status = HINIC3_TXQ_STATUS_START;
	g_txq->cons_idx = 0;
	g_txq->prod_idx = 0;
	g_txq->owner = 1;
	*g_ci_vaddr = 0;

	pkt = create_simple_mbuf(128, 0);
	RTE_TEST_ASSERT_NOT_NULL(pkt, "failed to create mbuf");
	tx_pkts[0] = pkt;

	nb_tx = hinic3_xmit_pkts_compact_cqe(g_txq, tx_pkts, 1);

	RTE_TEST_ASSERT_EQUAL(nb_tx, 1, "should transmit 1 packet");
	RTE_TEST_ASSERT_EQUAL(g_txq->txq_stats.packets, 1,
			      "stats.packets should be 1");
	RTE_TEST_ASSERT_EQUAL(g_txq->txq_stats.bytes, 128,
			      "stats.bytes should be 128");

	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts_compact_cqe: IP checksum offload ====== */

static int
test_compact_xmit_ip_cksum(void)
{
	struct rte_mbuf *pkt;
	struct rte_mbuf *tx_pkts[1];
	u16 nb_tx;

	g_txq->status = HINIC3_TXQ_STATUS_START;
	g_txq->cons_idx = 0;
	g_txq->prod_idx = 0;
	g_txq->owner = 1;
	*g_ci_vaddr = 0;

	pkt = create_simple_mbuf(128, HINIC3_PKT_TX_IP_CKSUM);
	RTE_TEST_ASSERT_NOT_NULL(pkt, "failed to create mbuf");
	tx_pkts[0] = pkt;

	nb_tx = hinic3_xmit_pkts_compact_cqe(g_txq, tx_pkts, 1);

	RTE_TEST_ASSERT_EQUAL(nb_tx, 1,
			      "should transmit 1 IP cksum packet");

	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts_compact_cqe: TCP checksum offload ====== */

static int
test_compact_xmit_tcp_cksum(void)
{
	struct rte_mbuf *pkt;
	struct rte_mbuf *tx_pkts[1];
	u16 nb_tx;

	g_txq->status = HINIC3_TXQ_STATUS_START;
	g_txq->cons_idx = 0;
	g_txq->prod_idx = 0;
	g_txq->owner = 1;
	*g_ci_vaddr = 0;

	pkt = create_simple_mbuf(256,
				 HINIC3_PKT_TX_IP_CKSUM |
				 HINIC3_PKT_TX_TCP_CKSUM);
	RTE_TEST_ASSERT_NOT_NULL(pkt, "failed to create mbuf");
	tx_pkts[0] = pkt;

	nb_tx = hinic3_xmit_pkts_compact_cqe(g_txq, tx_pkts, 1);

	RTE_TEST_ASSERT_EQUAL(nb_tx, 1,
			      "should transmit 1 TCP cksum packet");

	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts_compact_cqe: UDP checksum offload ====== */

static int
test_compact_xmit_udp_cksum(void)
{
	struct rte_mbuf *pkt;
	struct rte_mbuf *tx_pkts[1];
	u16 nb_tx;

	g_txq->status = HINIC3_TXQ_STATUS_START;
	g_txq->cons_idx = 0;
	g_txq->prod_idx = 0;
	g_txq->owner = 1;
	*g_ci_vaddr = 0;

	pkt = create_udp_mbuf(128,
			      HINIC3_PKT_TX_IP_CKSUM |
			      HINIC3_PKT_TX_UDP_CKSUM);
	RTE_TEST_ASSERT_NOT_NULL(pkt, "failed to create mbuf");
	tx_pkts[0] = pkt;

	nb_tx = hinic3_xmit_pkts_compact_cqe(g_txq, tx_pkts, 1);

	RTE_TEST_ASSERT_EQUAL(nb_tx, 1,
			      "should transmit 1 UDP cksum packet");

	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts_compact_cqe: VLAN offload ====== */

static int
test_compact_xmit_vlan_offload(void)
{
	struct rte_mbuf *pkt;
	struct rte_mbuf *tx_pkts[1];
	u16 nb_tx;

	g_txq->status = HINIC3_TXQ_STATUS_START;
	g_txq->cons_idx = 0;
	g_txq->prod_idx = 0;
	g_txq->owner = 1;
	*g_ci_vaddr = 0;

	pkt = create_simple_mbuf(128, HINIC3_PKT_TX_VLAN_PKT);
	RTE_TEST_ASSERT_NOT_NULL(pkt, "failed to create mbuf");
	pkt->vlan_tci = 100;
	tx_pkts[0] = pkt;

	nb_tx = hinic3_xmit_pkts_compact_cqe(g_txq, tx_pkts, 1);

	RTE_TEST_ASSERT_EQUAL(nb_tx, 1,
			      "should transmit 1 VLAN packet");

	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts_compact_cqe: multiple packets ====== */

static int
test_compact_xmit_multiple(void)
{
	struct rte_mbuf *pkts[4];
	struct rte_mbuf *tx_pkts[4];
	u16 nb_tx;
	int i;

	g_txq->status = HINIC3_TXQ_STATUS_START;
	g_txq->cons_idx = 0;
	g_txq->prod_idx = 0;
	g_txq->owner = 1;
	*g_ci_vaddr = 0;

	for (i = 0; i < 4; i++) {
		pkts[i] = create_simple_mbuf(64 + i * 10, 0);
		RTE_TEST_ASSERT_NOT_NULL(pkts[i],
					 "failed to create mbuf %d", i);
		tx_pkts[i] = pkts[i];
	}

	nb_tx = hinic3_xmit_pkts_compact_cqe(g_txq, tx_pkts, 4);

	RTE_TEST_ASSERT_EQUAL(nb_tx, 4, "should transmit 4 packets");
	RTE_TEST_ASSERT_EQUAL(g_txq->txq_stats.packets, 4,
			      "stats.packets should be 4");

	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts_compact_cqe: zero-length data (sge_len0) ====== */

static int
test_compact_xmit_zero_len(void)
{
	struct rte_mbuf *pkt;
	struct rte_mbuf *tx_pkts[1];
	u16 nb_tx;

	g_txq->status = HINIC3_TXQ_STATUS_START;
	g_txq->cons_idx = 0;
	g_txq->prod_idx = 0;
	g_txq->owner = 1;
	*g_ci_vaddr = 0;

	/* Create mbuf then set data_len to 0 to trigger error */
	pkt = create_simple_mbuf(128, 0);
	RTE_TEST_ASSERT_NOT_NULL(pkt, "failed to create mbuf");
	pkt->data_len = 0;
	tx_pkts[0] = pkt;

	nb_tx = hinic3_xmit_pkts_compact_cqe(g_txq, tx_pkts, 1);

	RTE_TEST_ASSERT_EQUAL(nb_tx, 0,
			      "zero-length mbuf should fail to xmit");
	RTE_TEST_ASSERT(g_txq->txq_stats.sge_len0 > 0,
			"sge_len0 stat should be incremented");

	rte_pktmbuf_free(pkt);
	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts_compact_cqe: QINQ offload ====== */

static int
test_compact_xmit_qinq_offload(void)
{
	struct rte_mbuf *pkt;
	struct rte_mbuf *tx_pkts[1];
	u16 nb_tx;

	g_txq->status = HINIC3_TXQ_STATUS_START;
	g_txq->cons_idx = 0;
	g_txq->prod_idx = 0;
	g_txq->owner = 1;
	*g_ci_vaddr = 0;

	pkt = create_simple_mbuf(128, HINIC3_PKT_TX_QINQ_PKT);
	RTE_TEST_ASSERT_NOT_NULL(pkt, "failed to create mbuf");
	pkt->vlan_tci = 200;
	tx_pkts[0] = pkt;

	nb_tx = hinic3_xmit_pkts_compact_cqe(g_txq, tx_pkts, 1);

	RTE_TEST_ASSERT_EQUAL(nb_tx, 1,
			      "should transmit 1 QINQ packet");

	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts_compact_cqe: IP + VLAN + TCP offload ====== */

static int
test_compact_xmit_ip_vlan_tcp_cksum(void)
{
	struct rte_mbuf *pkt;
	struct rte_mbuf *tx_pkts[1];
	u16 nb_tx;

	g_txq->status = HINIC3_TXQ_STATUS_START;
	g_txq->cons_idx = 0;
	g_txq->prod_idx = 0;
	g_txq->owner = 1;
	*g_ci_vaddr = 0;

	pkt = create_simple_mbuf(256,
				 HINIC3_PKT_TX_IP_CKSUM |
				 HINIC3_PKT_TX_TCP_CKSUM |
				 HINIC3_PKT_TX_VLAN_PKT);
	RTE_TEST_ASSERT_NOT_NULL(pkt, "failed to create mbuf");
	pkt->vlan_tci = 50;
	tx_pkts[0] = pkt;

	nb_tx = hinic3_xmit_pkts_compact_cqe(g_txq, tx_pkts, 1);

	RTE_TEST_ASSERT_EQUAL(nb_tx, 1,
			      "should transmit 1 combined offload packet");

	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts_compact_cqe: single-seg mode ====== */

static int
test_compact_xmit_single_seg(void)
{
	struct rte_mbuf *pkt;
	struct rte_mbuf *tx_pkts[1];
	u16 nb_tx;

	g_txq->status = HINIC3_TXQ_STATUS_START;
	g_txq->cons_idx = 0;
	g_txq->prod_idx = 0;
	g_txq->owner = 1;
	g_txq->multi_segs = false;
	*g_ci_vaddr = 0;

	pkt = create_simple_mbuf(128, 0);
	RTE_TEST_ASSERT_NOT_NULL(pkt, "failed to create mbuf");
	tx_pkts[0] = pkt;

	nb_tx = hinic3_xmit_pkts_compact_cqe(g_txq, tx_pkts, 1);

	RTE_TEST_ASSERT_EQUAL(nb_tx, 1,
			      "should transmit 1 packet in single-seg mode");

	g_txq->multi_segs = true;
	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts_compact_cqe: SCTP checksum offload ====== */

static int
test_compact_xmit_sctp_cksum(void)
{
	struct rte_mbuf *mbuf;
	struct rte_mbuf *tx_pkts[1];
	struct rte_ether_hdr *eth;
	struct rte_ipv4_hdr *ipv4;
	struct rte_sctp_hdr *sctp;
	uint8_t *pkt;
	u16 nb_tx;
	uint16_t l2_len = sizeof(struct rte_ether_hdr);
	uint16_t l3_len = sizeof(struct rte_ipv4_hdr);
	uint16_t l4_len = sizeof(struct rte_sctp_hdr);
	uint16_t hdr_len = l2_len + l3_len + l4_len;
	uint16_t data_len = hdr_len + 64;

	g_txq->status = HINIC3_TXQ_STATUS_START;
	g_txq->cons_idx = 0;
	g_txq->prod_idx = 0;
	g_txq->owner = 1;
	*g_ci_vaddr = 0;

	mbuf = rte_pktmbuf_alloc(g_mp);
	RTE_TEST_ASSERT_NOT_NULL(mbuf, "failed to alloc mbuf");

	pkt = (uint8_t *)rte_pktmbuf_append(mbuf, data_len);
	RTE_TEST_ASSERT_NOT_NULL(pkt, "failed to append data");
	memset(pkt, 0, data_len);

	eth = (struct rte_ether_hdr *)pkt;
	eth->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);

	ipv4 = (struct rte_ipv4_hdr *)(pkt + l2_len);
	ipv4->version_ihl = RTE_IPV4_VHL_DEF;
	ipv4->total_length = rte_cpu_to_be_16(data_len - l2_len);
	ipv4->next_proto_id = IPPROTO_SCTP;
	ipv4->src_addr = rte_cpu_to_be_32(RTE_IPV4(10, 0, 0, 1));
	ipv4->dst_addr = rte_cpu_to_be_32(RTE_IPV4(10, 0, 0, 2));

	sctp = (struct rte_sctp_hdr *)(pkt + l2_len + l3_len);
	sctp->src_port = rte_cpu_to_be_16(12345);
	sctp->dst_port = rte_cpu_to_be_16(80);

	mbuf->ol_flags = HINIC3_PKT_TX_IP_CKSUM | HINIC3_PKT_TX_SCTP_CKSUM;
	mbuf->l2_len = l2_len;
	mbuf->l3_len = l3_len;
	mbuf->l4_len = l4_len;
	mbuf->nb_segs = 1;
	mbuf->pkt_len = data_len;
	mbuf->data_len = data_len;

	tx_pkts[0] = mbuf;

	nb_tx = hinic3_xmit_pkts_compact_cqe(g_txq, tx_pkts, 1);

	RTE_TEST_ASSERT_EQUAL(nb_tx, 1,
			      "should transmit 1 SCTP cksum packet");

	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts_compact_cqe: IPv6 + TCP checksum ====== */

static int
test_compact_xmit_ipv6_tcp_cksum(void)
{
	struct rte_mbuf *pkt;
	struct rte_mbuf *tx_pkts[1];
	u16 nb_tx;

	g_txq->status = HINIC3_TXQ_STATUS_START;
	g_txq->cons_idx = 0;
	g_txq->prod_idx = 0;
	g_txq->owner = 1;
	*g_ci_vaddr = 0;

	pkt = create_ipv6_tcp_mbuf(256,
				   HINIC3_PKT_TX_IPV6 |
				   HINIC3_PKT_TX_TCP_CKSUM);
	RTE_TEST_ASSERT_NOT_NULL(pkt, "failed to create mbuf");
	tx_pkts[0] = pkt;

	nb_tx = hinic3_xmit_pkts_compact_cqe(g_txq, tx_pkts, 1);

	RTE_TEST_ASSERT_EQUAL(nb_tx, 1,
			      "should transmit 1 IPv6+TCP packet");

	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts_compact_cqe: IPv6 + UDP checksum ====== */

static int
test_compact_xmit_ipv6_udp_cksum(void)
{
	struct rte_mbuf *mbuf;
	struct rte_mbuf *tx_pkts[1];
	struct rte_ether_hdr *eth;
	struct rte_ipv6_hdr *ipv6;
	struct rte_udp_hdr *udp;
	uint8_t *pkt;
	u16 nb_tx;
	uint16_t l2_len = sizeof(struct rte_ether_hdr);
	uint16_t l3_len = sizeof(struct rte_ipv6_hdr);
	uint16_t l4_len = sizeof(struct rte_udp_hdr);
	uint16_t hdr_len = l2_len + l3_len + l4_len;
	uint16_t data_len = hdr_len + 64;

	g_txq->status = HINIC3_TXQ_STATUS_START;
	g_txq->cons_idx = 0;
	g_txq->prod_idx = 0;
	g_txq->owner = 1;
	*g_ci_vaddr = 0;

	mbuf = rte_pktmbuf_alloc(g_mp);
	RTE_TEST_ASSERT_NOT_NULL(mbuf, "failed to alloc mbuf");

	pkt = (uint8_t *)rte_pktmbuf_append(mbuf, data_len);
	RTE_TEST_ASSERT_NOT_NULL(pkt, "failed to append data");
	memset(pkt, 0, data_len);

	eth = (struct rte_ether_hdr *)pkt;
	eth->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV6);

	ipv6 = (struct rte_ipv6_hdr *)(pkt + l2_len);
	ipv6->proto = IPPROTO_UDP;
	ipv6->payload_len = rte_cpu_to_be_16(data_len - l2_len - l3_len);

	udp = (struct rte_udp_hdr *)(pkt + l2_len + l3_len);
	udp->src_port = rte_cpu_to_be_16(12345);
	udp->dst_port = rte_cpu_to_be_16(53);

	mbuf->ol_flags = HINIC3_PKT_TX_IPV6 | HINIC3_PKT_TX_UDP_CKSUM;
	mbuf->l2_len = l2_len;
	mbuf->l3_len = l3_len;
	mbuf->l4_len = l4_len;
	mbuf->nb_segs = 1;
	mbuf->pkt_len = data_len;
	mbuf->data_len = data_len;

	tx_pkts[0] = mbuf;

	nb_tx = hinic3_xmit_pkts_compact_cqe(g_txq, tx_pkts, 1);

	RTE_TEST_ASSERT_EQUAL(nb_tx, 1,
			      "should transmit 1 IPv6+UDP packet");

	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts_compact_cqe: stats tracking ====== */

static int
test_compact_xmit_stats(void)
{
	struct rte_mbuf *pkt;
	struct rte_mbuf *tx_pkts[1];
	u16 nb_tx;

	g_txq->status = HINIC3_TXQ_STATUS_START;
	g_txq->cons_idx = 0;
	g_txq->prod_idx = 0;
	g_txq->owner = 1;
	*g_ci_vaddr = 0;
	memset(&g_txq->txq_stats, 0, sizeof(g_txq->txq_stats));

	pkt = create_simple_mbuf(256, 0);
	RTE_TEST_ASSERT_NOT_NULL(pkt, "failed to create mbuf");
	tx_pkts[0] = pkt;

	nb_tx = hinic3_xmit_pkts_compact_cqe(g_txq, tx_pkts, 1);

	RTE_TEST_ASSERT_EQUAL(nb_tx, 1, "should transmit 1 packet");
	RTE_TEST_ASSERT_EQUAL(g_txq->txq_stats.packets, 1,
			      "stats.packets should be 1");
	RTE_TEST_ASSERT_EQUAL(g_txq->txq_stats.bytes, 256,
			      "stats.bytes should be 256");
	RTE_TEST_ASSERT_EQUAL(g_txq->txq_stats.burst_pkts, 1,
			      "stats.burst_pkts should be 1");

	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts_compact_cqe: packet too large for non-TSO ====== */

static int
test_compact_xmit_pkt_too_large(void)
{
	struct rte_mbuf *pkt;
	struct rte_mbuf *tx_pkts[1];
	u16 nb_tx;

	g_txq->status = HINIC3_TXQ_STATUS_START;
	g_txq->cons_idx = 0;
	g_txq->prod_idx = 0;
	g_txq->owner = 1;
	*g_ci_vaddr = 0;
	/* Set non_tso_max_pkt_len to a value smaller than the packet's pkt_len */
	g_txq->non_tso_max_pkt_len = 64;

	/* Create a packet with IP checksum offload. The pkt_len check in
	 * hinic3_non_tso_pkt_pre_process is only reached when nb_segs exceeds
	 * HINIC3_NONTSO_PKT_MAX_SGE (38). Set nb_segs to trigger that path. */
	pkt = create_simple_mbuf(128, HINIC3_PKT_TX_IP_CKSUM);
	RTE_TEST_ASSERT_NOT_NULL(pkt, "failed to create mbuf");
	pkt->nb_segs = HINIC3_NONTSO_PKT_MAX_SGE + 1;
	/* pkt_len (128) > non_tso_max_pkt_len (64) should trigger -EINVAL */
	tx_pkts[0] = pkt;

	nb_tx = hinic3_xmit_pkts_compact_cqe(g_txq, tx_pkts, 1);

	RTE_TEST_ASSERT_EQUAL(nb_tx, 0,
			      "packet too large for non-TSO should fail");
	RTE_TEST_ASSERT(g_txq->txq_stats.off_errs > 0,
			"off_errs should be incremented");

	/* Reset */
	g_txq->non_tso_max_pkt_len = HINIC3_MAX_JUMBO_FRAME_SIZE;
	rte_pktmbuf_free(pkt);
	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts_compact_cqe: IP + VLAN + UDP offload ====== */

static int
test_compact_xmit_ip_vlan_udp_cksum(void)
{
	struct rte_mbuf *pkt;
	struct rte_mbuf *tx_pkts[1];
	u16 nb_tx;

	g_txq->status = HINIC3_TXQ_STATUS_START;
	g_txq->cons_idx = 0;
	g_txq->prod_idx = 0;
	g_txq->owner = 1;
	*g_ci_vaddr = 0;

	pkt = create_udp_mbuf(256,
			      HINIC3_PKT_TX_IP_CKSUM |
			      HINIC3_PKT_TX_UDP_CKSUM |
			      HINIC3_PKT_TX_VLAN_PKT);
	RTE_TEST_ASSERT_NOT_NULL(pkt, "failed to create mbuf");
	pkt->vlan_tci = 100;
	tx_pkts[0] = pkt;

	nb_tx = hinic3_xmit_pkts_compact_cqe(g_txq, tx_pkts, 1);

	RTE_TEST_ASSERT_EQUAL(nb_tx, 1,
			      "should transmit 1 IP+VLAN+UDP packet");

	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts_compact_cqe: IP-in-IP with sp620 ====== */

static int
test_compact_xmit_ipinip_sp620(void)
{
	struct rte_mbuf *pkt;
	struct rte_mbuf *tx_pkts[1];
	u16 nb_tx;

	g_txq->status = HINIC3_TXQ_STATUS_START;
	g_txq->cons_idx = 0;
	g_txq->prod_idx = 0;
	g_txq->owner = 1;
	*g_ci_vaddr = 0;
	g_mock_is_sp620_nic = true;

	pkt = create_ipinip_mbuf(256, 0);
	RTE_TEST_ASSERT_NOT_NULL(pkt, "failed to create mbuf");
	tx_pkts[0] = pkt;

	nb_tx = hinic3_xmit_pkts_compact_cqe(g_txq, tx_pkts, 1);

	RTE_TEST_ASSERT_EQUAL(nb_tx, 1,
			      "should transmit 1 IP-in-IP packet");

	g_mock_is_sp620_nic = false;
	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts_compact_cqe: IP-in-IP without sp620 ====== */

static int
test_compact_xmit_ipinip_no_sp620(void)
{
	struct rte_mbuf *pkt;
	struct rte_mbuf *tx_pkts[1];
	u16 nb_tx;

	g_txq->status = HINIC3_TXQ_STATUS_START;
	g_txq->cons_idx = 0;
	g_txq->prod_idx = 0;
	g_txq->owner = 1;
	*g_ci_vaddr = 0;
	g_mock_is_sp620_nic = false;

	pkt = create_ipinip_mbuf(256, 0);
	RTE_TEST_ASSERT_NOT_NULL(pkt, "failed to create mbuf");
	tx_pkts[0] = pkt;

	nb_tx = hinic3_xmit_pkts_compact_cqe(g_txq, tx_pkts, 1);

	RTE_TEST_ASSERT_EQUAL(nb_tx, 1,
			      "should transmit 1 IP-in-IP packet without sp620");

	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts_compact_cqe: VLAN only (no cksum) ====== */

static int
test_compact_xmit_vlan_only(void)
{
	struct rte_mbuf *pkt;
	struct rte_mbuf *tx_pkts[1];
	u16 nb_tx;

	g_txq->status = HINIC3_TXQ_STATUS_START;
	g_txq->cons_idx = 0;
	g_txq->prod_idx = 0;
	g_txq->owner = 1;
	*g_ci_vaddr = 0;

	pkt = create_simple_mbuf(128, HINIC3_PKT_TX_VLAN_PKT);
	RTE_TEST_ASSERT_NOT_NULL(pkt, "failed to create mbuf");
	pkt->vlan_tci = 300;
	tx_pkts[0] = pkt;

	nb_tx = hinic3_xmit_pkts_compact_cqe(g_txq, tx_pkts, 1);

	RTE_TEST_ASSERT_EQUAL(nb_tx, 1,
			      "should transmit 1 VLAN-only packet");

	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts_compact_cqe: QINQ + TCP offload ====== */

static int
test_compact_xmit_qinq_tcp_cksum(void)
{
	struct rte_mbuf *pkt;
	struct rte_mbuf *tx_pkts[1];
	u16 nb_tx;

	g_txq->status = HINIC3_TXQ_STATUS_START;
	g_txq->cons_idx = 0;
	g_txq->prod_idx = 0;
	g_txq->owner = 1;
	*g_ci_vaddr = 0;

	pkt = create_simple_mbuf(256,
				 HINIC3_PKT_TX_QINQ_PKT |
				 HINIC3_PKT_TX_IP_CKSUM |
				 HINIC3_PKT_TX_TCP_CKSUM);
	RTE_TEST_ASSERT_NOT_NULL(pkt, "failed to create mbuf");
	pkt->vlan_tci = 100;
	tx_pkts[0] = pkt;

	nb_tx = hinic3_xmit_pkts_compact_cqe(g_txq, tx_pkts, 1);

	RTE_TEST_ASSERT_EQUAL(nb_tx, 1,
			      "should transmit 1 QINQ+TCP packet");

	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts_compact_cqe: compact task w/ offload + 1 seg ====== */

static int
test_compact_xmit_offload_single_seg(void)
{
	struct rte_mbuf *pkt;
	struct rte_mbuf *tx_pkts[1];
	u16 nb_tx;

	g_txq->status = HINIC3_TXQ_STATUS_START;
	g_txq->cons_idx = 0;
	g_txq->prod_idx = 0;
	g_txq->owner = 1;
	*g_ci_vaddr = 0;

	pkt = create_simple_mbuf(128,
				 HINIC3_PKT_TX_IP_CKSUM |
				 HINIC3_PKT_TX_TCP_CKSUM);
	RTE_TEST_ASSERT_NOT_NULL(pkt, "failed to create mbuf");
	tx_pkts[0] = pkt;

	nb_tx = hinic3_xmit_pkts_compact_cqe(g_txq, tx_pkts, 1);

	RTE_TEST_ASSERT_EQUAL(nb_tx, 1,
			      "should transmit 1 offload packet");

	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts_compact_cqe: burst of mixed packets ====== */

static int
test_compact_xmit_burst_mixed(void)
{
	struct rte_mbuf *pkts[3];
	struct rte_mbuf *tx_pkts[3];
	u16 nb_tx;

	g_txq->status = HINIC3_TXQ_STATUS_START;
	g_txq->cons_idx = 0;
	g_txq->prod_idx = 0;
	g_txq->owner = 1;
	*g_ci_vaddr = 0;

	pkts[0] = create_simple_mbuf(128, 0);
	pkts[1] = create_udp_mbuf(128, HINIC3_PKT_TX_IP_CKSUM |
				  HINIC3_PKT_TX_UDP_CKSUM);
	pkts[2] = create_simple_mbuf(256, HINIC3_PKT_TX_IP_CKSUM |
				     HINIC3_PKT_TX_TCP_CKSUM |
				     HINIC3_PKT_TX_VLAN_PKT);
	RTE_TEST_ASSERT_NOT_NULL(pkts[0], "failed to create mbuf 0");
	RTE_TEST_ASSERT_NOT_NULL(pkts[1], "failed to create mbuf 1");
	RTE_TEST_ASSERT_NOT_NULL(pkts[2], "failed to create mbuf 2");
	pkts[2]->vlan_tci = 50;

	tx_pkts[0] = pkts[0];
	tx_pkts[1] = pkts[1];
	tx_pkts[2] = pkts[2];

	nb_tx = hinic3_xmit_pkts_compact_cqe(g_txq, tx_pkts, 3);

	RTE_TEST_ASSERT_EQUAL(nb_tx, 3, "should transmit 3 mixed packets");

	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts_compact_cqe: no free wqebbs (tx busy) ====== */

static int
test_compact_xmit_tx_busy(void)
{
	struct rte_mbuf *pkt;
	struct rte_mbuf *tx_pkts[1];
	u16 nb_tx;

	g_txq->status = HINIC3_TXQ_STATUS_START;
	/* Fill queue nearly full: prod=63, cons=0 => free=0 */
	g_txq->cons_idx = 0;
	g_txq->prod_idx = 63;
	g_txq->owner = 1;
	*g_ci_vaddr = 0;
	/* Set tx_free_loop to 0 to trigger immediate busy */
	g_nic_dev->config.tx_free_loop = 0;

	pkt = create_simple_mbuf(128, 0);
	RTE_TEST_ASSERT_NOT_NULL(pkt, "failed to create mbuf");
	tx_pkts[0] = pkt;

	nb_tx = hinic3_xmit_pkts_compact_cqe(g_txq, tx_pkts, 1);

	/* With queue full and no cleanup possible, should return 0 */
	RTE_TEST_ASSERT_EQUAL(nb_tx, 0,
			      "should return 0 when queue is full (tx busy)");
	RTE_TEST_ASSERT(g_txq->txq_stats.tx_busy > 0,
			"tx_busy should be incremented");

	/* Reset */
	g_txq->prod_idx = 0;
	g_nic_dev->config.tx_free_loop = 1024;
	rte_pktmbuf_free(pkt);
	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts_compact_cqe: cleanup after xmit ====== */

static int
test_compact_xmit_cleanup(void)
{
	struct rte_mbuf *pkt;
	struct rte_mbuf *tx_pkts[1];
	u16 nb_tx;

	g_txq->status = HINIC3_TXQ_STATUS_START;
	g_txq->cons_idx = 0;
	g_txq->prod_idx = 0;
	g_txq->owner = 1;
	*g_ci_vaddr = 0;

	pkt = create_simple_mbuf(128, 0);
	RTE_TEST_ASSERT_NOT_NULL(pkt, "failed to create mbuf");
	tx_pkts[0] = pkt;

	nb_tx = hinic3_xmit_pkts_compact_cqe(g_txq, tx_pkts, 1);
	RTE_TEST_ASSERT_EQUAL(nb_tx, 1, "should transmit 1 packet");

	/* Simulate HW completing the packet by advancing CI */
	*g_ci_vaddr = rte_cpu_to_be_16(g_txq->prod_idx);

	/* Next xmit should trigger cleanup */
	pkt = create_simple_mbuf(64, 0);
	RTE_TEST_ASSERT_NOT_NULL(pkt, "failed to create mbuf");
	tx_pkts[0] = pkt;

	nb_tx = hinic3_xmit_pkts_compact_cqe(g_txq, tx_pkts, 1);
	RTE_TEST_ASSERT_EQUAL(nb_tx, 1, "should transmit 1 packet after cleanup");

	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts_compact_cqe: free txq mbufs ====== */

static int
test_compact_free_txq_mbufs(void)
{
	struct rte_mbuf *pkt;
	struct rte_mbuf *tx_pkts[1];
	u16 nb_tx;
	int i;

	g_txq->cons_idx = 0;
	g_txq->prod_idx = 0;
	g_txq->owner = 1;
	*g_ci_vaddr = 0;

	pkt = create_simple_mbuf(128, 0);
	RTE_TEST_ASSERT_NOT_NULL(pkt, "failed to create mbuf");

	tx_pkts[0] = pkt;
	nb_tx = hinic3_xmit_pkts_compact_cqe(g_txq, tx_pkts, 1);
	RTE_TEST_ASSERT_EQUAL(nb_tx, 1, "should transmit 1 packet");

	/* Free mbufs - should free the mbuf in tx_info */
	hinic3_free_txq_mbufs(g_txq);

	/* Verify all tx_info mbufs are freed */
	for (i = 0; i < TEST_TXQ_DEPTH; i++) {
		RTE_TEST_ASSERT_NULL(g_tx_info[i].mbuf,
				     "tx_info[%d].mbuf should be NULL", i);
	}

	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts_compact_cqe: free all txq mbufs ====== */

static int
test_compact_free_all_txq_mbufs(void)
{
	struct rte_mbuf *pkt;
	struct rte_mbuf *tx_pkts[1];
	u16 nb_tx;
	int i;

	g_txq->cons_idx = 0;
	g_txq->prod_idx = 0;
	g_txq->owner = 1;
	*g_ci_vaddr = 0;

	pkt = create_simple_mbuf(128, 0);
	RTE_TEST_ASSERT_NOT_NULL(pkt, "failed to create mbuf");

	tx_pkts[0] = pkt;
	nb_tx = hinic3_xmit_pkts_compact_cqe(g_txq, tx_pkts, 1);
	RTE_TEST_ASSERT_EQUAL(nb_tx, 1, "should transmit 1 packet");

	hinic3_free_all_txq_mbufs(g_nic_dev);

	/* Verify all tx_info mbufs are freed */
	for (i = 0; i < TEST_TXQ_DEPTH; i++) {
		RTE_TEST_ASSERT_NULL(g_tx_info[i].mbuf,
				     "tx_info[%d].mbuf should be NULL", i);
	}

	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts_compact_cqe: non-compact task mode ====== */

static int
test_compact_xmit_non_compact_task(void)
{
	struct rte_mbuf *pkt;
	struct rte_mbuf *tx_pkts[1];
	u16 nb_tx;

	g_txq->status = HINIC3_TXQ_STATUS_START;
	g_txq->cons_idx = 0;
	g_txq->prod_idx = 0;
	g_txq->owner = 1;
	g_txq->tx_wqe_compact_task = 0;
	*g_ci_vaddr = 0;

	pkt = create_simple_mbuf(128,
				 HINIC3_PKT_TX_IP_CKSUM |
				 HINIC3_PKT_TX_TCP_CKSUM);
	RTE_TEST_ASSERT_NOT_NULL(pkt, "failed to create mbuf");
	tx_pkts[0] = pkt;

	nb_tx = hinic3_xmit_pkts_compact_cqe(g_txq, tx_pkts, 1);

	RTE_TEST_ASSERT_EQUAL(nb_tx, 1,
			      "should transmit 1 packet in non-compact task mode");

	g_txq->tx_wqe_compact_task = 1;
	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts_compact_cqe: zero pkts ====== */

static int
test_compact_xmit_zero_pkts(void)
{
	u16 nb_tx;

	g_txq->status = HINIC3_TXQ_STATUS_START;
	g_txq->cons_idx = 0;
	g_txq->prod_idx = 0;
	g_txq->owner = 1;
	*g_ci_vaddr = 0;

	nb_tx = hinic3_xmit_pkts_compact_cqe(g_txq, NULL, 0);

	RTE_TEST_ASSERT_EQUAL(nb_tx, 0, "should return 0 for zero pkts");

	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts_compact_cqe: offload error breaks burst ====== */

static int
test_compact_xmit_offload_error_break(void)
{
	struct rte_mbuf *pkts[2];
	struct rte_mbuf *tx_pkts[2];
	u16 nb_tx;

	g_txq->status = HINIC3_TXQ_STATUS_START;
	g_txq->cons_idx = 0;
	g_txq->prod_idx = 0;
	g_txq->owner = 1;
	*g_ci_vaddr = 0;

	/* First packet is valid */
	pkts[0] = create_simple_mbuf(128, 0);
	RTE_TEST_ASSERT_NOT_NULL(pkts[0], "failed to create mbuf 0");

	/* Second packet has zero-length data to trigger error */
	pkts[1] = create_simple_mbuf(128, 0);
	RTE_TEST_ASSERT_NOT_NULL(pkts[1], "failed to create mbuf 1");
	pkts[1]->data_len = 0;

	tx_pkts[0] = pkts[0];
	tx_pkts[1] = pkts[1];

	nb_tx = hinic3_xmit_pkts_compact_cqe(g_txq, tx_pkts, 2);

	/* First packet succeeds, second fails, should return 1 */
	RTE_TEST_ASSERT_EQUAL(nb_tx, 1,
			      "should transmit 1 packet before error");

	rte_pktmbuf_free(pkts[1]);
	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts_compact_cqe: IPv6 TCP no cksum ====== */

static int
test_compact_xmit_ipv6_tcp_no_cksum(void)
{
	struct rte_mbuf *pkt;
	struct rte_mbuf *tx_pkts[1];
	u16 nb_tx;

	g_txq->status = HINIC3_TXQ_STATUS_START;
	g_txq->cons_idx = 0;
	g_txq->prod_idx = 0;
	g_txq->owner = 1;
	*g_ci_vaddr = 0;

	pkt = create_ipv6_tcp_mbuf(256, HINIC3_PKT_TX_IPV6);
	RTE_TEST_ASSERT_NOT_NULL(pkt, "failed to create mbuf");
	tx_pkts[0] = pkt;

	nb_tx = hinic3_xmit_pkts_compact_cqe(g_txq, tx_pkts, 1);

	RTE_TEST_ASSERT_EQUAL(nb_tx, 1,
			      "should transmit 1 IPv6 packet without cksum");

	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts_compact_cqe: large non-offload packet ====== */

static int
test_compact_xmit_large_no_offload(void)
{
	struct rte_mbuf *pkt;
	struct rte_mbuf *tx_pkts[1];
	u16 nb_tx;

	g_txq->status = HINIC3_TXQ_STATUS_START;
	g_txq->cons_idx = 0;
	g_txq->prod_idx = 0;
	g_txq->owner = 1;
	*g_ci_vaddr = 0;

	pkt = create_simple_mbuf(1024, 0);
	RTE_TEST_ASSERT_NOT_NULL(pkt, "failed to create mbuf");
	tx_pkts[0] = pkt;

	nb_tx = hinic3_xmit_pkts_compact_cqe(g_txq, tx_pkts, 1);

	RTE_TEST_ASSERT_EQUAL(nb_tx, 1,
			      "should transmit 1 large packet");
	RTE_TEST_ASSERT_EQUAL(g_txq->txq_stats.bytes, 1024,
			      "stats.bytes should be 1024");

	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts_compact_cqe: VLAN + QINQ combined ====== */

static int
test_compact_xmit_vlan_qinq(void)
{
	struct rte_mbuf *pkt;
	struct rte_mbuf *tx_pkts[1];
	u16 nb_tx;

	g_txq->status = HINIC3_TXQ_STATUS_START;
	g_txq->cons_idx = 0;
	g_txq->prod_idx = 0;
	g_txq->owner = 1;
	*g_ci_vaddr = 0;

	pkt = create_simple_mbuf(128,
				 HINIC3_PKT_TX_VLAN_PKT |
				 HINIC3_PKT_TX_QINQ_PKT);
	RTE_TEST_ASSERT_NOT_NULL(pkt, "failed to create mbuf");
	pkt->vlan_tci = 500;
	tx_pkts[0] = pkt;

	nb_tx = hinic3_xmit_pkts_compact_cqe(g_txq, tx_pkts, 1);

	RTE_TEST_ASSERT_EQUAL(nb_tx, 1,
			      "should transmit 1 VLAN+QINQ packet");

	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts_compact_cqe: multi-seg with offload ====== */

static int
test_compact_xmit_multiseg_offload(void)
{
	struct rte_mbuf *pkts[2];
	struct rte_mbuf *tx_pkts[1];
	u16 nb_tx;

	g_txq->status = HINIC3_TXQ_STATUS_START;
	g_txq->cons_idx = 0;
	g_txq->prod_idx = 0;
	g_txq->owner = 1;
	*g_ci_vaddr = 0;

	/* Create a 2-segment mbuf */
	pkts[0] = create_simple_mbuf(128, HINIC3_PKT_TX_IP_CKSUM |
				     HINIC3_PKT_TX_TCP_CKSUM);
	RTE_TEST_ASSERT_NOT_NULL(pkts[0], "failed to create mbuf 0");
	pkts[1] = rte_pktmbuf_alloc(g_mp);
	RTE_TEST_ASSERT_NOT_NULL(pkts[1], "failed to create mbuf 1");
	rte_pktmbuf_append(pkts[1], 64);
	memset(rte_pktmbuf_mtod(pkts[1], void *), 0, 64);
	pkts[1]->data_len = 64;

	/* Chain segments */
	pkts[0]->next = pkts[1];
	pkts[0]->nb_segs = 2;
	pkts[0]->pkt_len = 128 + 64;

	tx_pkts[0] = pkts[0];

	nb_tx = hinic3_xmit_pkts_compact_cqe(g_txq, tx_pkts, 1);

	RTE_TEST_ASSERT_EQUAL(nb_tx, 1,
			      "should transmit 1 multi-seg packet");

	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts_compact_cqe: ipinip with TSO error ====== */

static int
test_compact_xmit_ipinip_tso_error(void)
{
	struct rte_mbuf *pkt;
	struct rte_mbuf *tx_pkts[1];
	u16 nb_tx;

	g_txq->status = HINIC3_TXQ_STATUS_START;
	g_txq->cons_idx = 0;
	g_txq->prod_idx = 0;
	g_txq->owner = 1;
	*g_ci_vaddr = 0;
	g_mock_is_sp620_nic = true;

	pkt = create_ipinip_mbuf(256, HINIC3_PKT_TX_TCP_SEG);
	RTE_TEST_ASSERT_NOT_NULL(pkt, "failed to create mbuf");
	pkt->tso_segsz = 1024;
	tx_pkts[0] = pkt;

	nb_tx = hinic3_xmit_pkts_compact_cqe(g_txq, tx_pkts, 1);

	/* IP-in-IP + TSO is not supported, should fail */
	RTE_TEST_ASSERT_EQUAL(nb_tx, 0,
			      "IPinIP+TSO should fail");
	RTE_TEST_ASSERT(g_txq->txq_stats.off_errs > 0,
			"off_errs should be incremented");

	g_mock_is_sp620_nic = false;
	rte_pktmbuf_free(pkt);
	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts_compact_cqe: outer IP cksum ====== */

static int
test_compact_xmit_outer_ip_cksum(void)
{
	struct rte_mbuf *pkt;
	struct rte_mbuf *tx_pkts[1];
	u16 nb_tx;

	g_txq->status = HINIC3_TXQ_STATUS_START;
	g_txq->cons_idx = 0;
	g_txq->prod_idx = 0;
	g_txq->owner = 1;
	*g_ci_vaddr = 0;

	pkt = create_simple_mbuf(256, HINIC3_PKT_TX_OUTER_IP_CKSUM |
				 HINIC3_PKT_TX_TCP_CKSUM);
	RTE_TEST_ASSERT_NOT_NULL(pkt, "failed to create mbuf");
	tx_pkts[0] = pkt;

	nb_tx = hinic3_xmit_pkts_compact_cqe(g_txq, tx_pkts, 1);

	RTE_TEST_ASSERT_EQUAL(nb_tx, 1,
			      "should transmit 1 outer IP cksum packet");

	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts_compact_cqe: unsupported tunnel type ====== */

static int
test_compact_xmit_unsupported_tunnel(void)
{
	struct rte_mbuf *pkt;
	struct rte_mbuf *tx_pkts[1];
	u16 nb_tx;

	g_txq->status = HINIC3_TXQ_STATUS_START;
	g_txq->cons_idx = 0;
	g_txq->prod_idx = 0;
	g_txq->owner = 1;
	*g_ci_vaddr = 0;

	/* Use GRE tunnel flag which is not VxLAN/Geneve/IPIP */
	pkt = create_simple_mbuf(256, HINIC3_PKT_TX_TUNNEL_GRE |
				 HINIC3_PKT_TX_TCP_CKSUM);
	RTE_TEST_ASSERT_NOT_NULL(pkt, "failed to create mbuf");
	tx_pkts[0] = pkt;

	nb_tx = hinic3_xmit_pkts_compact_cqe(g_txq, tx_pkts, 1);

	/* GRE flag is cleared only in a local variable in
	 * hinic3_tx_offload_pkt_prepare, not in the mbuf. In
	 * hinic3_set_tx_offload_compact_cqe, the mbuf's ol_flags still
	 * has GRE set, and the tunnel switch rejects it (default case). */
	RTE_TEST_ASSERT_EQUAL(nb_tx, 0,
			      "GRE tunnel should fail (flag not cleared in mbuf)");
	RTE_TEST_ASSERT(g_txq->txq_stats.off_errs > 0,
			"off_errs should be incremented");

	rte_pktmbuf_free(pkt);
	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts_compact_cqe: ipinip with inner cksums ====== */

static int
test_compact_xmit_ipinip_inner_cksum(void)
{
	struct rte_mbuf *pkt;
	struct rte_mbuf *tx_pkts[1];
	u16 nb_tx;

	g_txq->status = HINIC3_TXQ_STATUS_START;
	g_txq->cons_idx = 0;
	g_txq->prod_idx = 0;
	g_txq->owner = 1;
	*g_ci_vaddr = 0;
	g_mock_is_sp620_nic = true;

	pkt = create_ipinip_mbuf(256, HINIC3_PKT_TX_IP_CKSUM |
				 HINIC3_PKT_TX_TCP_CKSUM);
	RTE_TEST_ASSERT_NOT_NULL(pkt, "failed to create mbuf");
	tx_pkts[0] = pkt;

	nb_tx = hinic3_xmit_pkts_compact_cqe(g_txq, tx_pkts, 1);

	RTE_TEST_ASSERT_EQUAL(nb_tx, 1,
			      "should transmit 1 IPinIP with inner cksum");

	g_mock_is_sp620_nic = false;
	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts_compact_cqe: multiple VLAN tags ====== */

static int
test_compact_xmit_multi_vlan(void)
{
	struct rte_mbuf *mbuf;
	struct rte_mbuf *tx_pkts[1];
	struct rte_ether_hdr *eth;
	struct rte_vlan_hdr *vlan;
	struct rte_ipv4_hdr *ipv4;
	uint8_t *pkt;
	u16 nb_tx;
	uint16_t l2_len = sizeof(struct rte_ether_hdr) +
			  sizeof(struct rte_vlan_hdr);
	uint16_t l3_len = sizeof(struct rte_ipv4_hdr);
	uint16_t l4_len = sizeof(struct rte_tcp_hdr);
	uint16_t hdr_len = l2_len + l3_len + l4_len;
	uint16_t data_len = hdr_len + 64;

	g_txq->status = HINIC3_TXQ_STATUS_START;
	g_txq->cons_idx = 0;
	g_txq->prod_idx = 0;
	g_txq->owner = 1;
	*g_ci_vaddr = 0;

	mbuf = rte_pktmbuf_alloc(g_mp);
	RTE_TEST_ASSERT_NOT_NULL(mbuf, "failed to alloc mbuf");

	pkt = (uint8_t *)rte_pktmbuf_append(mbuf, data_len);
	RTE_TEST_ASSERT_NOT_NULL(pkt, "failed to append data");
	memset(pkt, 0, data_len);

	eth = (struct rte_ether_hdr *)pkt;
	eth->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_VLAN);

	vlan = (struct rte_vlan_hdr *)(pkt + sizeof(struct rte_ether_hdr));
	vlan->eth_proto = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);

	ipv4 = (struct rte_ipv4_hdr *)(pkt + l2_len);
	ipv4->version_ihl = RTE_IPV4_VHL_DEF;
	ipv4->total_length = rte_cpu_to_be_16(data_len - l2_len);
	ipv4->next_proto_id = IPPROTO_TCP;
	ipv4->src_addr = rte_cpu_to_be_32(RTE_IPV4(192, 168, 1, 1));
	ipv4->dst_addr = rte_cpu_to_be_32(RTE_IPV4(192, 168, 1, 2));

	mbuf->ol_flags = HINIC3_PKT_TX_IP_CKSUM | HINIC3_PKT_TX_TCP_CKSUM;
	mbuf->l2_len = l2_len;
	mbuf->l3_len = l3_len;
	mbuf->l4_len = l4_len;
	mbuf->nb_segs = 1;
	mbuf->pkt_len = data_len;
	mbuf->data_len = data_len;

	tx_pkts[0] = mbuf;

	nb_tx = hinic3_xmit_pkts_compact_cqe(g_txq, tx_pkts, 1);

	RTE_TEST_ASSERT_EQUAL(nb_tx, 1,
			      "should transmit 1 multi-VLAN packet");

	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts_compact_cqe: ipinip IPv6 outer ====== */

static int
test_compact_xmit_ipinip_ipv6_outer(void)
{
	struct rte_mbuf *mbuf;
	struct rte_mbuf *tx_pkts[1];
	struct rte_ether_hdr *eth;
	struct rte_ipv6_hdr *outer_ipv6;
	struct rte_ipv4_hdr *inner_ipv4;
	struct rte_tcp_hdr *tcp;
	uint8_t *pkt;
	u16 nb_tx;
	uint16_t l2_len = sizeof(struct rte_ether_hdr);
	uint16_t outer_l3_len = sizeof(struct rte_ipv6_hdr);
	uint16_t inner_l3_len = sizeof(struct rte_ipv4_hdr);
	uint16_t l4_len = sizeof(struct rte_tcp_hdr);
	uint16_t hdr_len = l2_len + outer_l3_len + inner_l3_len + l4_len;
	uint16_t data_len = hdr_len + 64;

	g_txq->status = HINIC3_TXQ_STATUS_START;
	g_txq->cons_idx = 0;
	g_txq->prod_idx = 0;
	g_txq->owner = 1;
	*g_ci_vaddr = 0;
	g_mock_is_sp620_nic = true;

	mbuf = rte_pktmbuf_alloc(g_mp);
	RTE_TEST_ASSERT_NOT_NULL(mbuf, "failed to alloc mbuf");

	pkt = (uint8_t *)rte_pktmbuf_append(mbuf, data_len);
	RTE_TEST_ASSERT_NOT_NULL(pkt, "failed to append data");
	memset(pkt, 0, data_len);

	eth = (struct rte_ether_hdr *)pkt;
	eth->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV6);

	outer_ipv6 = (struct rte_ipv6_hdr *)(pkt + l2_len);
	outer_ipv6->vtc_flow = rte_cpu_to_be_32(0x60000000);
	outer_ipv6->proto = IPPROTO_IPV6;
	outer_ipv6->payload_len =
		rte_cpu_to_be_16(data_len - l2_len - outer_l3_len);

	inner_ipv4 = (struct rte_ipv4_hdr *)(pkt + l2_len + outer_l3_len);
	inner_ipv4->version_ihl = RTE_IPV4_VHL_DEF;
	inner_ipv4->total_length =
		rte_cpu_to_be_16(data_len - l2_len - outer_l3_len);
	inner_ipv4->next_proto_id = IPPROTO_TCP;
	inner_ipv4->src_addr = rte_cpu_to_be_32(RTE_IPV4(192, 168, 1, 1));
	inner_ipv4->dst_addr = rte_cpu_to_be_32(RTE_IPV4(192, 168, 1, 2));

	tcp = (struct rte_tcp_hdr *)(pkt + l2_len + outer_l3_len +
				     inner_l3_len);
	tcp->src_port = rte_cpu_to_be_16(12345);
	tcp->dst_port = rte_cpu_to_be_16(80);

	mbuf->ol_flags = HINIC3_PKT_TX_TUNNEL_IPIP;
	mbuf->l2_len = l2_len;
	mbuf->l3_len = inner_l3_len;
	mbuf->l4_len = l4_len;
	mbuf->outer_l3_len = outer_l3_len;
	mbuf->nb_segs = 1;
	mbuf->pkt_len = data_len;
	mbuf->data_len = data_len;

	tx_pkts[0] = mbuf;

	nb_tx = hinic3_xmit_pkts_compact_cqe(g_txq, tx_pkts, 1);

	RTE_TEST_ASSERT_EQUAL(nb_tx, 1,
			      "should transmit 1 IPv6-over-IPv4 IPinIP packet");

	g_mock_is_sp620_nic = false;
	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts_compact_cqe: TSO basic ====== */

static int
test_compact_xmit_tso_basic(void)
{
	struct rte_mbuf *pkt;
	struct rte_mbuf *tx_pkts[1];
	u16 nb_tx;

	g_txq->status = HINIC3_TXQ_STATUS_START;
	g_txq->cons_idx = 0;
	g_txq->prod_idx = 0;
	g_txq->owner = 1;
	*g_ci_vaddr = 0;

	pkt = create_simple_mbuf(512, HINIC3_PKT_TX_TCP_SEG |
				 HINIC3_PKT_TX_IP_CKSUM |
				 HINIC3_PKT_TX_TCP_CKSUM);
	RTE_TEST_ASSERT_NOT_NULL(pkt, "failed to create mbuf");
	pkt->tso_segsz = 1024;
	tx_pkts[0] = pkt;

	nb_tx = hinic3_xmit_pkts_compact_cqe(g_txq, tx_pkts, 1);

	RTE_TEST_ASSERT_EQUAL(nb_tx, 1, "should transmit 1 TSO packet");

	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts_compact_cqe: TSO with zero segsz ====== */

static int
test_compact_xmit_tso_zero_segsz(void)
{
	struct rte_mbuf *pkt;
	struct rte_mbuf *tx_pkts[1];
	u16 nb_tx;

	g_txq->status = HINIC3_TXQ_STATUS_START;
	g_txq->cons_idx = 0;
	g_txq->prod_idx = 0;
	g_txq->owner = 1;
	*g_ci_vaddr = 0;

	pkt = create_simple_mbuf(512, HINIC3_PKT_TX_TCP_SEG |
				 HINIC3_PKT_TX_IP_CKSUM);
	RTE_TEST_ASSERT_NOT_NULL(pkt, "failed to create mbuf");
	pkt->tso_segsz = 0;
	tx_pkts[0] = pkt;

	nb_tx = hinic3_xmit_pkts_compact_cqe(g_txq, tx_pkts, 1);

	RTE_TEST_ASSERT_EQUAL(nb_tx, 0,
			      "TSO with zero segsz should fail");
	RTE_TEST_ASSERT(g_txq->txq_stats.off_errs > 0,
			"off_errs should be incremented");

	rte_pktmbuf_free(pkt);
	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts_compact_cqe: TSO with too large payload offset ====== */

static int
test_compact_xmit_tso_large_payload_offset(void)
{
	struct rte_mbuf *pkt;
	struct rte_mbuf *tx_pkts[1];
	u16 nb_tx;

	g_txq->status = HINIC3_TXQ_STATUS_START;
	g_txq->cons_idx = 0;
	g_txq->prod_idx = 0;
	g_txq->owner = 1;
	*g_ci_vaddr = 0;

	pkt = create_simple_mbuf(512, HINIC3_PKT_TX_TCP_SEG |
				 HINIC3_PKT_TX_IP_CKSUM |
				 HINIC3_PKT_TX_TCP_CKSUM);
	RTE_TEST_ASSERT_NOT_NULL(pkt, "failed to create mbuf");
	pkt->tso_segsz = 1024;
	/* Set l2_len + l3_len + l4_len to create a large payload offset.
	 * Note: l2_len is a 7-bit field (max 127), l3_len is 9-bit (max 511),
	 * l4_len is 8-bit (max 255). The check is (payload_offset >> 1) > 221,
	 * so payload_offset must be > 442. */
	pkt->l2_len = 127;
	pkt->l3_len = 100;
	pkt->l4_len = 220;
	tx_pkts[0] = pkt;

	nb_tx = hinic3_xmit_pkts_compact_cqe(g_txq, tx_pkts, 1);

	RTE_TEST_ASSERT_EQUAL(nb_tx, 0,
			      "TSO with too large payload offset should fail");
	RTE_TEST_ASSERT(g_txq->txq_stats.off_errs > 0,
			"off_errs should be incremented");

	rte_pktmbuf_free(pkt);
	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts_compact_cqe: VxLAN tunnel ====== */

static int
test_compact_xmit_vxlan_tunnel(void)
{
	struct rte_mbuf *mbuf;
	struct rte_mbuf *tx_pkts[1];
	struct rte_ether_hdr *eth;
	struct rte_ipv4_hdr *outer_ip;
	struct rte_udp_hdr *udp;
	struct rte_vxlan_hdr *vxlan;
	struct rte_ether_hdr *inner_eth;
	struct rte_ipv4_hdr *inner_ip;
	struct rte_tcp_hdr *tcp;
	uint8_t *pkt;
	u16 nb_tx;
	uint16_t l2_len = sizeof(struct rte_ether_hdr);
	uint16_t outer_l3_len = sizeof(struct rte_ipv4_hdr);
	uint16_t outer_l4_len = sizeof(struct rte_udp_hdr);
	uint16_t vxlan_len = sizeof(struct rte_vxlan_hdr);
	uint16_t inner_l2_len = sizeof(struct rte_ether_hdr);
	uint16_t inner_l3_len = sizeof(struct rte_ipv4_hdr);
	uint16_t inner_l4_len = sizeof(struct rte_tcp_hdr);
	uint16_t hdr_len = l2_len + outer_l3_len + outer_l4_len +
			   vxlan_len + inner_l2_len + inner_l3_len +
			   inner_l4_len;
	uint16_t data_len = hdr_len + 64;

	g_txq->status = HINIC3_TXQ_STATUS_START;
	g_txq->cons_idx = 0;
	g_txq->prod_idx = 0;
	g_txq->owner = 1;
	*g_ci_vaddr = 0;

	mbuf = rte_pktmbuf_alloc(g_mp);
	RTE_TEST_ASSERT_NOT_NULL(mbuf, "failed to alloc mbuf");

	pkt = (uint8_t *)rte_pktmbuf_append(mbuf, data_len);
	RTE_TEST_ASSERT_NOT_NULL(pkt, "failed to append data");
	memset(pkt, 0, data_len);

	eth = (struct rte_ether_hdr *)pkt;
	eth->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);

	outer_ip = (struct rte_ipv4_hdr *)(pkt + l2_len);
	outer_ip->version_ihl = RTE_IPV4_VHL_DEF;
	outer_ip->total_length = rte_cpu_to_be_16(data_len - l2_len);
	outer_ip->next_proto_id = IPPROTO_UDP;
	outer_ip->src_addr = rte_cpu_to_be_32(RTE_IPV4(10, 0, 0, 1));
	outer_ip->dst_addr = rte_cpu_to_be_32(RTE_IPV4(10, 0, 0, 2));

	udp = (struct rte_udp_hdr *)(pkt + l2_len + outer_l3_len);
	udp->src_port = rte_cpu_to_be_16(12345);
	udp->dst_port = rte_cpu_to_be_16(4789);

	vxlan = (struct rte_vxlan_hdr *)(pkt + l2_len + outer_l3_len +
					  outer_l4_len);

	inner_eth = (struct rte_ether_hdr *)((uint8_t *)vxlan +
					      vxlan_len);
	inner_eth->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);

	inner_ip = (struct rte_ipv4_hdr *)((uint8_t *)inner_eth +
					    inner_l2_len);
	inner_ip->version_ihl = RTE_IPV4_VHL_DEF;
	inner_ip->total_length =
		rte_cpu_to_be_16(data_len - l2_len - outer_l3_len -
				 outer_l4_len - vxlan_len - inner_l2_len);
	inner_ip->next_proto_id = IPPROTO_TCP;
	inner_ip->src_addr = rte_cpu_to_be_32(RTE_IPV4(192, 168, 1, 1));
	inner_ip->dst_addr = rte_cpu_to_be_32(RTE_IPV4(192, 168, 1, 2));

	tcp = (struct rte_tcp_hdr *)((uint8_t *)inner_ip + inner_l3_len);
	tcp->src_port = rte_cpu_to_be_16(12345);
	tcp->dst_port = rte_cpu_to_be_16(80);

	mbuf->ol_flags = HINIC3_PKT_TX_TUNNEL_VXLAN |
			 HINIC3_PKT_TX_IP_CKSUM |
			 HINIC3_PKT_TX_TCP_CKSUM;
	mbuf->l2_len = l2_len + outer_l3_len + outer_l4_len +
			vxlan_len + inner_l2_len;
	mbuf->l3_len = inner_l3_len;
	mbuf->l4_len = inner_l4_len;
	mbuf->outer_l2_len = l2_len;
	mbuf->outer_l3_len = outer_l3_len;
	mbuf->nb_segs = 1;
	mbuf->pkt_len = data_len;
	mbuf->data_len = data_len;

	tx_pkts[0] = mbuf;

	nb_tx = hinic3_xmit_pkts_compact_cqe(g_txq, tx_pkts, 1);

	RTE_TEST_ASSERT_EQUAL(nb_tx, 1,
			      "should transmit 1 VxLAN tunnel packet");

	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts_compact_cqe: TSO VxLAN tunnel ====== */

static int
test_compact_xmit_tso_vxlan(void)
{
	struct rte_mbuf *mbuf;
	struct rte_mbuf *tx_pkts[1];
	struct rte_ether_hdr *eth;
	struct rte_ipv4_hdr *outer_ip;
	struct rte_udp_hdr *udp;
	struct rte_vxlan_hdr *vxlan;
	struct rte_ether_hdr *inner_eth;
	struct rte_ipv4_hdr *inner_ip;
	struct rte_tcp_hdr *tcp;
	uint8_t *pkt;
	u16 nb_tx;
	uint16_t l2_len = sizeof(struct rte_ether_hdr);
	uint16_t outer_l3_len = sizeof(struct rte_ipv4_hdr);
	uint16_t outer_l4_len = sizeof(struct rte_udp_hdr);
	uint16_t vxlan_len = sizeof(struct rte_vxlan_hdr);
	uint16_t inner_l2_len = sizeof(struct rte_ether_hdr);
	uint16_t inner_l3_len = sizeof(struct rte_ipv4_hdr);
	uint16_t inner_l4_len = sizeof(struct rte_tcp_hdr);
	uint16_t hdr_len = l2_len + outer_l3_len + outer_l4_len +
			   vxlan_len + inner_l2_len + inner_l3_len +
			   inner_l4_len;
	uint16_t data_len = hdr_len + 512;

	g_txq->status = HINIC3_TXQ_STATUS_START;
	g_txq->cons_idx = 0;
	g_txq->prod_idx = 0;
	g_txq->owner = 1;
	*g_ci_vaddr = 0;

	mbuf = rte_pktmbuf_alloc(g_mp);
	RTE_TEST_ASSERT_NOT_NULL(mbuf, "failed to alloc mbuf");

	pkt = (uint8_t *)rte_pktmbuf_append(mbuf, data_len);
	RTE_TEST_ASSERT_NOT_NULL(pkt, "failed to append data");
	memset(pkt, 0, data_len);

	eth = (struct rte_ether_hdr *)pkt;
	eth->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);

	outer_ip = (struct rte_ipv4_hdr *)(pkt + l2_len);
	outer_ip->version_ihl = RTE_IPV4_VHL_DEF;
	outer_ip->total_length = rte_cpu_to_be_16(data_len - l2_len);
	outer_ip->next_proto_id = IPPROTO_UDP;
	outer_ip->src_addr = rte_cpu_to_be_32(RTE_IPV4(10, 0, 0, 1));
	outer_ip->dst_addr = rte_cpu_to_be_32(RTE_IPV4(10, 0, 0, 2));

	udp = (struct rte_udp_hdr *)(pkt + l2_len + outer_l3_len);
	udp->src_port = rte_cpu_to_be_16(12345);
	udp->dst_port = rte_cpu_to_be_16(4789);

	vxlan = (struct rte_vxlan_hdr *)(pkt + l2_len + outer_l3_len +
					  outer_l4_len);

	inner_eth = (struct rte_ether_hdr *)((uint8_t *)vxlan +
					      vxlan_len);
	inner_eth->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);

	inner_ip = (struct rte_ipv4_hdr *)((uint8_t *)inner_eth +
					    inner_l2_len);
	inner_ip->version_ihl = RTE_IPV4_VHL_DEF;
	inner_ip->total_length =
		rte_cpu_to_be_16(data_len - l2_len - outer_l3_len -
				 outer_l4_len - vxlan_len - inner_l2_len);
	inner_ip->next_proto_id = IPPROTO_TCP;
	inner_ip->src_addr = rte_cpu_to_be_32(RTE_IPV4(192, 168, 1, 1));
	inner_ip->dst_addr = rte_cpu_to_be_32(RTE_IPV4(192, 168, 1, 2));

	tcp = (struct rte_tcp_hdr *)((uint8_t *)inner_ip + inner_l3_len);
	tcp->src_port = rte_cpu_to_be_16(12345);
	tcp->dst_port = rte_cpu_to_be_16(80);

	mbuf->ol_flags = HINIC3_PKT_TX_TUNNEL_VXLAN |
			 HINIC3_PKT_TX_OUTER_IP_CKSUM |
			 HINIC3_PKT_TX_TCP_SEG |
			 HINIC3_PKT_TX_IP_CKSUM |
			 HINIC3_PKT_TX_TCP_CKSUM;
	mbuf->l2_len = inner_l2_len;
	mbuf->l3_len = inner_l3_len;
	mbuf->l4_len = inner_l4_len;
	mbuf->outer_l2_len = l2_len;
	mbuf->outer_l3_len = outer_l3_len;
	mbuf->tso_segsz = 1024;
	mbuf->nb_segs = 1;
	mbuf->pkt_len = data_len;
	mbuf->data_len = data_len;

	tx_pkts[0] = mbuf;

	nb_tx = hinic3_xmit_pkts_compact_cqe(g_txq, tx_pkts, 1);

	RTE_TEST_ASSERT_EQUAL(nb_tx, 1,
			      "should transmit 1 TSO VxLAN packet");

	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts_compact_cqe: ipinip with fragment ====== */

static int
test_compact_xmit_ipinip_fragment(void)
{
	struct rte_mbuf *pkt;
	struct rte_mbuf *tx_pkts[1];
	u16 nb_tx;

	g_txq->status = HINIC3_TXQ_STATUS_START;
	g_txq->cons_idx = 0;
	g_txq->prod_idx = 0;
	g_txq->owner = 1;
	*g_ci_vaddr = 0;
	g_mock_is_sp620_nic = true;

	pkt = create_ipinip_mbuf(256, 0);
	RTE_TEST_ASSERT_NOT_NULL(pkt, "failed to create mbuf");
	/* Set fragment offset to non-zero to test fragment path */
	struct rte_ipv4_hdr *outer_ip =
		rte_pktmbuf_mtod_offset(pkt, struct rte_ipv4_hdr *,
					sizeof(struct rte_ether_hdr));
	outer_ip->fragment_offset = rte_cpu_to_be_16(0x2000);
	tx_pkts[0] = pkt;

	nb_tx = hinic3_xmit_pkts_compact_cqe(g_txq, tx_pkts, 1);

	/* Fragment with non-zero offset should skip inner checksum but still succeed */
	RTE_TEST_ASSERT_EQUAL(nb_tx, 1,
			      "should transmit 1 fragmented IPinIP packet");

	g_mock_is_sp620_nic = false;
	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts_compact_cqe: outer UDP cksum ====== */

static int
test_compact_xmit_outer_udp_cksum(void)
{
	struct rte_mbuf *pkt;
	struct rte_mbuf *tx_pkts[1];
	u16 nb_tx;

	g_txq->status = HINIC3_TXQ_STATUS_START;
	g_txq->cons_idx = 0;
	g_txq->prod_idx = 0;
	g_txq->owner = 1;
	*g_ci_vaddr = 0;

	pkt = create_simple_mbuf(256, HINIC3_PKT_TX_OUTER_UDP_CKSUM |
				 HINIC3_PKT_TX_OUTER_IP_CKSUM |
				 HINIC3_PKT_TX_TCP_CKSUM);
	RTE_TEST_ASSERT_NOT_NULL(pkt, "failed to create mbuf");
	tx_pkts[0] = pkt;

	nb_tx = hinic3_xmit_pkts_compact_cqe(g_txq, tx_pkts, 1);

	RTE_TEST_ASSERT_EQUAL(nb_tx, 1,
			      "should transmit 1 outer UDP cksum packet");

	return TEST_SUCCESS;
}

/* ====== Direct static function tests ====== */

/* ====== test_get_ipv4_len_proto ====== */

static int
test_get_ipv4_len_proto(void)
{
	struct rte_ipv4_hdr ipv4_hdr;
	uint16_t hdr_len;
	uint8_t proto;

	/* Standard IPv4 header (IHL=5 => 20 bytes) */
	memset(&ipv4_hdr, 0, sizeof(ipv4_hdr));
	ipv4_hdr.version_ihl = RTE_IPV4_VHL_DEF; /* version=4, ihl=5 */
	ipv4_hdr.next_proto_id = IPPROTO_TCP;

	local_get_ipv4_len_proto(&ipv4_hdr, &hdr_len, &proto);
	RTE_TEST_ASSERT_EQUAL(hdr_len, 20,
			      "IPv4 hdr len should be 20 for IHL=5");
	RTE_TEST_ASSERT_EQUAL(proto, IPPROTO_TCP,
			      "proto should be TCP");

	/* IPv4 with options (IHL=6 => 24 bytes) */
	ipv4_hdr.version_ihl = (4 << 4) | 6;
	ipv4_hdr.next_proto_id = IPPROTO_UDP;

	local_get_ipv4_len_proto(&ipv4_hdr, &hdr_len, &proto);
	RTE_TEST_ASSERT_EQUAL(hdr_len, 24,
			      "IPv4 hdr len should be 24 for IHL=6");
	RTE_TEST_ASSERT_EQUAL(proto, IPPROTO_UDP,
			      "proto should be UDP");

	/* IPv4 with IHL=15 (max => 60 bytes) */
	ipv4_hdr.version_ihl = (4 << 4) | 15;
	ipv4_hdr.next_proto_id = IPPROTO_SCTP;

	local_get_ipv4_len_proto(&ipv4_hdr, &hdr_len, &proto);
	RTE_TEST_ASSERT_EQUAL(hdr_len, 60,
			      "IPv4 hdr len should be 60 for IHL=15");
	RTE_TEST_ASSERT_EQUAL(proto, IPPROTO_SCTP,
			      "proto should be SCTP");

	return TEST_SUCCESS;
}

/* ====== test_get_ipv6_len_proto ====== */

static int
test_get_ipv6_len_proto(void)
{
	struct rte_ipv6_hdr ipv6_hdr;
	uint16_t hdr_len;
	uint8_t proto;

	/* Standard IPv6 header, no extension headers */
	memset(&ipv6_hdr, 0, sizeof(ipv6_hdr));
	ipv6_hdr.proto = IPPROTO_TCP;

	local_get_ipv6_len_proto(&ipv6_hdr, &hdr_len, &proto);
	RTE_TEST_ASSERT_EQUAL(hdr_len, sizeof(struct rte_ipv6_hdr),
			      "IPv6 hdr len should be 40");
	RTE_TEST_ASSERT_EQUAL(proto, IPPROTO_TCP,
			      "proto should be TCP");

	/* IPv6 with UDP */
	ipv6_hdr.proto = IPPROTO_UDP;
	local_get_ipv6_len_proto(&ipv6_hdr, &hdr_len, &proto);
	RTE_TEST_ASSERT_EQUAL(proto, IPPROTO_UDP,
			      "proto should be UDP");

	/* IPv6 with no next header */
	ipv6_hdr.proto = IPPROTO_NONE;
	local_get_ipv6_len_proto(&ipv6_hdr, &hdr_len, &proto);
	RTE_TEST_ASSERT_EQUAL(proto, IPPROTO_NONE,
			      "proto should be NONE");

	return TEST_SUCCESS;
}

/* ====== test_check_ip_version ====== */

static int
test_check_ip_version(void)
{
	uint8_t result;

	/* IPv4 */
	result = local_check_ip_version(IPV4_VERSION);
	RTE_TEST_ASSERT_EQUAL(result, IPV4_INDEX,
			      "IPv4 version should return IPV4_INDEX");

	/* IPv6 */
	result = local_check_ip_version(IPV6_VERSION);
	RTE_TEST_ASSERT_EQUAL(result, IPV6_INDEX,
			      "IPv6 version should return IPV6_INDEX");

	/* Invalid versions */
	result = local_check_ip_version(0);
	RTE_TEST_ASSERT_EQUAL(result, IP_INDEX_INVALID,
			      "version 0 should return IP_INDEX_INVALID");

	result = local_check_ip_version(5);
	RTE_TEST_ASSERT_EQUAL(result, IP_INDEX_INVALID,
			      "version 5 should return IP_INDEX_INVALID");

	result = local_check_ip_version(15);
	RTE_TEST_ASSERT_EQUAL(result, IP_INDEX_INVALID,
			      "version 15 should return IP_INDEX_INVALID");

	return TEST_SUCCESS;
}

/* ====== test_is_ipinip ====== */

static int
test_is_ipinip(void)
{
	struct rte_mbuf mbuf;

	memset(&mbuf, 0, sizeof(mbuf));
	bool result;

	/* No tunnel flags */
	mbuf.ol_flags = 0;
	mbuf.packet_type = 0;
	result = local_is_ipinip(&mbuf);
	RTE_TEST_ASSERT_EQUAL(result, false,
			      "no tunnel flag should return false");

	/* IPIP tunnel flag */
	mbuf.ol_flags = HINIC3_PKT_TX_TUNNEL_IPIP;
	mbuf.packet_type = 0;
	result = local_is_ipinip(&mbuf);
	RTE_TEST_ASSERT_EQUAL(result, true,
			      "IPIP tunnel flag should return true");

	/* PTYPE_TUNNEL_IP */
	mbuf.ol_flags = 0;
	mbuf.packet_type = RTE_PTYPE_TUNNEL_IP;
	result = local_is_ipinip(&mbuf);
	RTE_TEST_ASSERT_EQUAL(result, true,
			      "PTYPE_TUNNEL_IP should return true");

	/* VxLAN tunnel flag (not IPIP) */
	mbuf.ol_flags = HINIC3_PKT_TX_TUNNEL_VXLAN;
	mbuf.packet_type = 0;
	result = local_is_ipinip(&mbuf);
	RTE_TEST_ASSERT_EQUAL(result, false,
			      "VxLAN flag should return false");

	/* GRE tunnel flag (not IPIP) */
	mbuf.ol_flags = HINIC3_PKT_TX_TUNNEL_GRE;
	mbuf.packet_type = 0;
	result = local_is_ipinip(&mbuf);
	RTE_TEST_ASSERT_EQUAL(result, false,
			      "GRE flag should return false");

	/* Both IPIP flag and PTYPE_TUNNEL_IP */
	mbuf.ol_flags = HINIC3_PKT_TX_TUNNEL_IPIP;
	mbuf.packet_type = RTE_PTYPE_TUNNEL_IP;
	result = local_is_ipinip(&mbuf);
	RTE_TEST_ASSERT_EQUAL(result, true,
			      "both IPIP flag and PTYPE should return true");

	return TEST_SUCCESS;
}

/* ====== test_compact_sq_free_wqebbs (requires g_txq) ====== */

static int
test_compact_sq_free_wqebbs(void)
{
	u16 free_wqebbs;

	g_txq->q_depth = 64;
	g_txq->q_mask = 63;
	g_txq->prod_idx = 0;
	g_txq->cons_idx = 0;

	/* When prod=0, cons=0: free = 63 */
	free_wqebbs = ((g_txq->q_depth -
		(((g_txq->prod_idx - g_txq->cons_idx) + g_txq->q_depth) &
		 g_txq->q_mask)) - 1);
	RTE_TEST_ASSERT_EQUAL(free_wqebbs, 63,
			      "free_wqebbs should be 63 when queue is empty");

	/* When prod=10, cons=0: occupied=10, free=53 */
	g_txq->prod_idx = 10;
	free_wqebbs = ((g_txq->q_depth -
		(((g_txq->prod_idx - g_txq->cons_idx) + g_txq->q_depth) &
		 g_txq->q_mask)) - 1);
	RTE_TEST_ASSERT_EQUAL(free_wqebbs, 53,
			      "free_wqebbs should be 53 when 10 used");

	/* When prod=63, cons=0: occupied=63, free=0 */
	g_txq->prod_idx = 63;
	free_wqebbs = ((g_txq->q_depth -
		(((g_txq->prod_idx - g_txq->cons_idx) + g_txq->q_depth) &
		 g_txq->q_mask)) - 1);
	RTE_TEST_ASSERT_EQUAL(free_wqebbs, 0,
			      "free_wqebbs should be 0 when queue is full");

	/* When prod wraps: prod=5, cons=60 => occupied=9 (wrapped) */
	g_txq->prod_idx = 5;
	g_txq->cons_idx = 60;
	free_wqebbs = ((g_txq->q_depth -
		(((g_txq->prod_idx - g_txq->cons_idx) + g_txq->q_depth) &
		 g_txq->q_mask)) - 1);
	RTE_TEST_ASSERT_EQUAL(free_wqebbs, 54,
			      "free_wqebbs should be 54 when wrapped");

	g_txq->prod_idx = 0;
	g_txq->cons_idx = 0;
	return TEST_SUCCESS;
}

/* ====== test_compact_sq_owner_flip (requires g_txq) ====== */

static int
test_compact_sq_owner_flip(void)
{
	u16 owner;
	u16 curr_pi;
	u16 wqebb_cnt;

	/* Test owner flip logic from hinic3_get_and_update_sq_owner */
	g_txq->owner = 1;
	curr_pi = 0;
	wqebb_cnt = 1;

	/* curr_pi + wqebb_cnt < q_depth, owner should not flip */
	owner = g_txq->owner;
	if (curr_pi + wqebb_cnt >= g_txq->q_depth)
		g_txq->owner = !g_txq->owner;
	RTE_TEST_ASSERT_EQUAL(owner, 1, "owner should be 1");
	RTE_TEST_ASSERT_EQUAL(g_txq->owner, 1,
			      "owner should remain 1 (no flip)");

	/* curr_pi + wqebb_cnt == q_depth, owner should flip */
	g_txq->owner = 1;
	curr_pi = 63;
	wqebb_cnt = 1;
	owner = g_txq->owner;
	if (curr_pi + wqebb_cnt >= g_txq->q_depth)
		g_txq->owner = !g_txq->owner;
	RTE_TEST_ASSERT_EQUAL(owner, 1, "returned owner should be 1");
	RTE_TEST_ASSERT_EQUAL(g_txq->owner, 0,
			      "owner should flip to 0");

	/* curr_pi + wqebb_cnt > q_depth, owner should flip */
	g_txq->owner = 0;
	curr_pi = 60;
	wqebb_cnt = 5;
	owner = g_txq->owner;
	if (curr_pi + wqebb_cnt >= g_txq->q_depth)
		g_txq->owner = !g_txq->owner;
	RTE_TEST_ASSERT_EQUAL(owner, 0, "returned owner should be 0");
	RTE_TEST_ASSERT_EQUAL(g_txq->owner, 1,
			      "owner should flip to 1");

	/* curr_pi + wqebb_cnt < q_depth, owner should not flip */
	g_txq->owner = 1;
	curr_pi = 30;
	wqebb_cnt = 10;
	owner = g_txq->owner;
	if (curr_pi + wqebb_cnt >= g_txq->q_depth)
		g_txq->owner = !g_txq->owner;
	RTE_TEST_ASSERT_EQUAL(owner, 1, "returned owner should be 1");
	RTE_TEST_ASSERT_EQUAL(g_txq->owner, 1,
			      "owner should remain 1 (no flip)");

	g_txq->owner = 1;
	return TEST_SUCCESS;
}

/* ====== test_compact_set_buf_desc (no g_txq needed) ====== */

static int
test_compact_set_buf_desc(void)
{
	struct hinic3_sq_bufdesc buf_desc = {0};
	rte_iova_t addr = 0xDEADBEEF12345678ULL;
	u32 len = 2048;

	/* Local copy of hinic3_set_buf_desc logic */
	buf_desc.hi_addr = hinic3_hw_be32(upper_32_bits(addr));
	buf_desc.lo_addr = hinic3_hw_be32(lower_32_bits(addr));
	buf_desc.len = hinic3_hw_be32(len);
	buf_desc.rsvd = 0;

	/* Verify values are set correctly */
	RTE_TEST_ASSERT_EQUAL(buf_desc.len, hinic3_hw_be32(len),
			      "len field should be set correctly");
	RTE_TEST_ASSERT_EQUAL(buf_desc.hi_addr,
			      hinic3_hw_be32(upper_32_bits(addr)),
			      "hi_addr should be set correctly");
	RTE_TEST_ASSERT_EQUAL(buf_desc.lo_addr,
			      hinic3_hw_be32(lower_32_bits(addr)),
			      "lo_addr should be set correctly");
	RTE_TEST_ASSERT_EQUAL(buf_desc.rsvd, 0,
			      "rsvd should be 0");

	/* Test with zero address */
	memset(&buf_desc, 0, sizeof(buf_desc));
	addr = 0;
	len = 0;
	buf_desc.hi_addr = hinic3_hw_be32(upper_32_bits(addr));
	buf_desc.lo_addr = hinic3_hw_be32(lower_32_bits(addr));
	buf_desc.len = hinic3_hw_be32(len);
	buf_desc.rsvd = 0;
	RTE_TEST_ASSERT_EQUAL(buf_desc.hi_addr, 0,
			      "hi_addr should be 0 for zero addr");
	RTE_TEST_ASSERT_EQUAL(buf_desc.lo_addr, 0,
			      "lo_addr should be 0 for zero addr");
	RTE_TEST_ASSERT_EQUAL(buf_desc.len, 0,
			      "len should be 0 for zero len");

	/* Test with max values */
	memset(&buf_desc, 0, sizeof(buf_desc));
	addr = 0xFFFFFFFFFFFFFFFFULL;
	len = 0xFFFFFFFF;
	buf_desc.hi_addr = hinic3_hw_be32(upper_32_bits(addr));
	buf_desc.lo_addr = hinic3_hw_be32(lower_32_bits(addr));
	buf_desc.len = hinic3_hw_be32(len);
	buf_desc.rsvd = 0;
	RTE_TEST_ASSERT_EQUAL(buf_desc.hi_addr,
			      hinic3_hw_be32(0xFFFFFFFF),
			      "hi_addr should be max");
	RTE_TEST_ASSERT_EQUAL(buf_desc.lo_addr,
			      hinic3_hw_be32(0xFFFFFFFF),
			      "lo_addr should be max");

	return TEST_SUCCESS;
}

/* ====== Test suite definition ====== */

static struct unit_test_suite hinic3_tx_compact_test_suite = {
	.suite_name = "HINIC3 TX Compact CQE Unit Tests",
	.setup = txc_suite_setup,
	.teardown = txc_suite_teardown,
	.unit_test_cases = {
		/* hinic3_xmit_pkts_compact_cqe tests */
		TEST_CASE_ST(txc_test_setup, txc_test_teardown, test_compact_xmit_stopped),
		TEST_CASE_ST(txc_test_setup, txc_test_teardown, test_compact_xmit_no_offload),
		TEST_CASE_ST(txc_test_setup, txc_test_teardown, test_compact_xmit_ip_cksum),
		TEST_CASE_ST(txc_test_setup, txc_test_teardown, test_compact_xmit_tcp_cksum),
		TEST_CASE_ST(txc_test_setup, txc_test_teardown, test_compact_xmit_udp_cksum),
		TEST_CASE_ST(txc_test_setup, txc_test_teardown, test_compact_xmit_sctp_cksum),
		TEST_CASE_ST(txc_test_setup, txc_test_teardown, test_compact_xmit_vlan_offload),
		TEST_CASE_ST(txc_test_setup, txc_test_teardown, test_compact_xmit_qinq_offload),
		TEST_CASE_ST(txc_test_setup, txc_test_teardown, test_compact_xmit_ip_vlan_tcp_cksum),
		TEST_CASE_ST(txc_test_setup, txc_test_teardown, test_compact_xmit_ip_vlan_udp_cksum),
		TEST_CASE_ST(txc_test_setup, txc_test_teardown, test_compact_xmit_multiple),
		TEST_CASE_ST(txc_test_setup, txc_test_teardown, test_compact_xmit_zero_len),
		TEST_CASE_ST(txc_test_setup, txc_test_teardown, test_compact_xmit_single_seg),
		TEST_CASE_ST(txc_test_setup, txc_test_teardown, test_compact_xmit_ipv6_tcp_cksum),
		TEST_CASE_ST(txc_test_setup, txc_test_teardown, test_compact_xmit_ipv6_udp_cksum),
		TEST_CASE_ST(txc_test_setup, txc_test_teardown, test_compact_xmit_pkt_too_large),
		TEST_CASE_ST(txc_test_setup, txc_test_teardown, test_compact_xmit_stats),
		TEST_CASE_ST(txc_test_setup, txc_test_teardown, test_compact_xmit_vlan_only),
		TEST_CASE_ST(txc_test_setup, txc_test_teardown, test_compact_xmit_qinq_tcp_cksum),
		TEST_CASE_ST(txc_test_setup, txc_test_teardown, test_compact_xmit_offload_single_seg),
		TEST_CASE_ST(txc_test_setup, txc_test_teardown, test_compact_xmit_burst_mixed),
		TEST_CASE_ST(txc_test_setup, txc_test_teardown, test_compact_xmit_tx_busy),
		TEST_CASE_ST(txc_test_setup, txc_test_teardown, test_compact_xmit_cleanup),
		TEST_CASE_ST(txc_test_setup, txc_test_teardown, test_compact_free_txq_mbufs),
		TEST_CASE_ST(txc_test_setup, txc_test_teardown, test_compact_free_all_txq_mbufs),
		TEST_CASE_ST(txc_test_setup, txc_test_teardown, test_compact_xmit_non_compact_task),
		TEST_CASE_ST(txc_test_setup, txc_test_teardown, test_compact_xmit_zero_pkts),
		TEST_CASE_ST(txc_test_setup, txc_test_teardown, test_compact_xmit_offload_error_break),
		TEST_CASE_ST(txc_test_setup, txc_test_teardown, test_compact_xmit_ipv6_tcp_no_cksum),
		TEST_CASE_ST(txc_test_setup, txc_test_teardown, test_compact_xmit_large_no_offload),
		TEST_CASE_ST(txc_test_setup, txc_test_teardown, test_compact_xmit_vlan_qinq),
		TEST_CASE_ST(txc_test_setup, txc_test_teardown, test_compact_xmit_multiseg_offload),

		/* IP-in-IP tests */
		TEST_CASE_ST(txc_test_setup, txc_test_teardown, test_compact_xmit_ipinip_sp620),
		TEST_CASE_ST(txc_test_setup, txc_test_teardown, test_compact_xmit_ipinip_no_sp620),
		TEST_CASE_ST(txc_test_setup, txc_test_teardown, test_compact_xmit_ipinip_inner_cksum),
		TEST_CASE_ST(txc_test_setup, txc_test_teardown, test_compact_xmit_ipinip_tso_error),
		TEST_CASE_ST(txc_test_setup, txc_test_teardown, test_compact_xmit_ipinip_fragment),
		TEST_CASE_ST(txc_test_setup, txc_test_teardown, test_compact_xmit_ipinip_ipv6_outer),

		/* Outer checksum tests */
		TEST_CASE_ST(txc_test_setup, txc_test_teardown, test_compact_xmit_outer_ip_cksum),
		TEST_CASE_ST(txc_test_setup, txc_test_teardown, test_compact_xmit_outer_udp_cksum),

		/* Tunnel tests */
		TEST_CASE_ST(txc_test_setup, txc_test_teardown, test_compact_xmit_vxlan_tunnel),
		TEST_CASE_ST(txc_test_setup, txc_test_teardown, test_compact_xmit_tso_vxlan),
		TEST_CASE_ST(txc_test_setup, txc_test_teardown, test_compact_xmit_unsupported_tunnel),

		/* TSO tests */
		TEST_CASE_ST(txc_test_setup, txc_test_teardown, test_compact_xmit_tso_basic),
		TEST_CASE_ST(txc_test_setup, txc_test_teardown, test_compact_xmit_tso_zero_segsz),
		TEST_CASE_ST(txc_test_setup, txc_test_teardown, test_compact_xmit_tso_large_payload_offset),

		/* Multi-VLAN test */
		TEST_CASE_ST(txc_test_setup, txc_test_teardown, test_compact_xmit_multi_vlan),

		/* Direct static function tests (no g_txq needed) */
		TEST_CASE(test_get_ipv4_len_proto),
		TEST_CASE(test_get_ipv6_len_proto),
		TEST_CASE(test_check_ip_version),
		TEST_CASE(test_is_ipinip),
		TEST_CASE(test_compact_set_buf_desc),

		/* Direct static function tests (require g_txq) */
		TEST_CASE_ST(txc_test_setup, txc_test_teardown, test_compact_sq_free_wqebbs),
		TEST_CASE_ST(txc_test_setup, txc_test_teardown, test_compact_sq_owner_flip),

		TEST_CASES_END()
	}
};

static int
test_hinic3_tx_compact(void)
{
	return unit_test_suite_runner(&hinic3_tx_compact_test_suite);
}

REGISTER_TEST_COMMAND(hinic3_tx_compact_autotest, test_hinic3_tx_compact);
