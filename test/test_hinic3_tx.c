/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2025-2026 Huawei Technologies Co., Ltd
 * Unit tests for hinic3_pmd_tx.c - testing through public interfaces
 */

#include <rte_common.h>
#include <rte_ethdev.h>
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

#include "../test.h"
#include <rte_test.h>
#include <rte_log.h>

#include "base/hinic3_compat.h"
#include "base/hinic3_pmd_nic_cfg.h"
#include "hinic3_pmd_nic_io.h"
#include "hinic3_pmd_ethdev.h"
#include "hinic3_pmd_tx.h"

#define TEST_TXQ_DEPTH		64
#define TEST_TXQ_MASK		(TEST_TXQ_DEPTH - 1)
#define TEST_TXQ_WQEBB_SHIFT	4
#define TEST_TXQ_WQEBB_SIZE	(1 << TEST_TXQ_WQEBB_SHIFT)
#define TEST_WQE_BUF_SIZE	(TEST_TXQ_DEPTH * TEST_TXQ_WQEBB_SIZE)

static struct rte_mempool *g_mp;
static struct rte_mempool *g_cpy_mp;
static struct hinic3_nic_dev *g_nic_dev;
static struct hinic3_txq *g_txq;
static struct hinic3_tx_info *g_tx_info;
static void *g_wqe_buf;
static u16 *g_ci_vaddr;
static void *g_db_buf;
static int g_orig_log_level;

static int
tx_suite_setup(void)
{
	g_orig_log_level = rte_log_get_global_level();
	rte_log_set_level_pattern("pmd.net.hinic3", RTE_LOG_INFO);

	g_mp = rte_pktmbuf_pool_create("test_tx_mp", 256, 0, 0,
					RTE_PKTMBUF_HEADROOM + 2048,
					SOCKET_ID_ANY);
	if (!g_mp)
		return TEST_FAILED;

	g_cpy_mp = rte_pktmbuf_pool_create("test_tx_cpy_mp",
					    HINIC3_COPY_MEMPOOL_DEPTH,
					    HINIC3_COPY_MEMPOOL_CACHE, 0,
					    HINIC3_COPY_MBUF_SIZE + RTE_PKTMBUF_HEADROOM,
					    SOCKET_ID_ANY);
	if (!g_cpy_mp) {
		rte_mempool_free(g_mp);
		g_mp = NULL;
		return TEST_FAILED;
	}

	return TEST_SUCCESS;
}

static void
tx_suite_teardown(void)
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

static int
tx_test_setup(void)
{
	int i;

	/* Allocate nic_dev */
	g_nic_dev = rte_zmalloc("test_nic_dev", sizeof(*g_nic_dev), 0);
	if (!g_nic_dev)
		return TEST_FAILED;
	g_nic_dev->feature_cap = NIC_F_TX_WQE_COMPACT_TASK | NIC_F_CSUM |
				 NIC_F_TSO | NIC_F_TX_VLAN_INSERT;
	g_nic_dev->num_sqs = 1;
	g_nic_dev->rx_csum_en = HINIC3_DEFAULT_RX_CSUM_OFFLOAD;
	g_nic_dev->cpy_mpool = g_cpy_mp;
	strcpy(g_nic_dev->dev_name, "test_hinic3_tx");

	/* Allocate txqs array */
	g_nic_dev->txqs = rte_zmalloc("test_txqs", sizeof(struct hinic3_txq *), 0);
	if (!g_nic_dev->txqs)
		goto err_free_nic_dev;

	/* Allocate WQE buffer */
	g_wqe_buf = rte_zmalloc("test_wqe_buf", TEST_WQE_BUF_SIZE,
				RTE_CACHE_LINE_SIZE);
	if (!g_wqe_buf)
		goto err_free_txqs;

	/* Allocate CI vaddr */
	g_ci_vaddr = rte_zmalloc("test_ci_vaddr", RTE_CACHE_LINE_SIZE,
				 RTE_CACHE_LINE_SIZE);
	if (!g_ci_vaddr)
		goto err_free_wqe_buf;
	*g_ci_vaddr = 0;

	/* Allocate doorbell buffer */
	g_db_buf = rte_zmalloc("test_db_buf", 4096, RTE_CACHE_LINE_SIZE);
	if (!g_db_buf)
		goto err_free_ci_vaddr;

	/* Allocate tx_info array */
	g_tx_info = rte_zmalloc("test_tx_info",
				TEST_TXQ_DEPTH * sizeof(*g_tx_info), 0);
	if (!g_tx_info)
		goto err_free_db_buf;
	for (i = 0; i < TEST_TXQ_DEPTH; i++) {
		g_tx_info[i].mbuf = NULL;
		g_tx_info[i].cpy_mbuf = NULL;
		g_tx_info[i].wqebb_cnt = 0;
	}

	/* Allocate txq */
	g_txq = rte_zmalloc("test_txq", sizeof(*g_txq), RTE_CACHE_LINE_SIZE);
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
	g_txq->tx_wqe_compact_task = 0;
	g_txq->is_sp620_nic = 0;
	g_txq->is_hairpin = false;
	g_txq->db_addr = g_db_buf;
	memset(&g_txq->txq_stats, 0, sizeof(g_txq->txq_stats));

	g_nic_dev->txqs[0] = g_txq;

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
tx_test_teardown(void)
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

/* Helper: create a simple mbuf with Ethernet + IPv4 + SCTP payload */
static struct rte_mbuf *
create_sctp_mbuf(uint16_t data_len, uint64_t ol_flags)
{
	struct rte_mbuf *mbuf;
	struct rte_ether_hdr *eth;
	struct rte_ipv4_hdr *ipv4;
	struct rte_sctp_hdr *sctp;
	uint8_t *pkt;
	uint16_t l2_len = sizeof(struct rte_ether_hdr);
	uint16_t l3_len = sizeof(struct rte_ipv4_hdr);
	uint16_t l4_len = sizeof(struct rte_sctp_hdr);
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
	ipv4->next_proto_id = IPPROTO_SCTP;
	ipv4->src_addr = rte_cpu_to_be_32(RTE_IPV4(10, 0, 0, 1));
	ipv4->dst_addr = rte_cpu_to_be_32(RTE_IPV4(10, 0, 0, 2));

	sctp = (struct rte_sctp_hdr *)(pkt + l2_len + l3_len);
	sctp->src_port = rte_cpu_to_be_16(12345);
	sctp->dst_port = rte_cpu_to_be_16(80);

	mbuf->ol_flags = ol_flags;
	mbuf->l2_len = l2_len;
	mbuf->l3_len = l3_len;
	mbuf->l4_len = l4_len;
	mbuf->nb_segs = 1;
	mbuf->pkt_len = data_len;
	mbuf->data_len = data_len;

	return mbuf;
}

/* ====== hinic3_xmit_pkts: TXQ stopped ====== */

static int
test_xmit_pkts_stopped(void)
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

	nb_tx = hinic3_xmit_pkts(g_txq, tx_pkts, 1);

	RTE_TEST_ASSERT_EQUAL(nb_tx, 0, "xmit on stopped queue should return 0");

	rte_pktmbuf_free(pkt);
	g_txq->status = HINIC3_TXQ_STATUS_START;
	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts: no offload simple packet ====== */

static int
test_xmit_pkts_no_offload(void)
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

	nb_tx = hinic3_xmit_pkts(g_txq, tx_pkts, 1);

	RTE_TEST_ASSERT_EQUAL(nb_tx, 1, "should transmit 1 packet");
	RTE_TEST_ASSERT_EQUAL(g_txq->txq_stats.packets, 1,
			      "stats.packets should be 1");
	RTE_TEST_ASSERT_EQUAL(g_txq->txq_stats.bytes, 128,
			      "stats.bytes should be 128");

	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts: IP checksum offload ====== */

static int
test_xmit_pkts_ip_cksum(void)
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

	nb_tx = hinic3_xmit_pkts(g_txq, tx_pkts, 1);

	RTE_TEST_ASSERT_EQUAL(nb_tx, 1, "should transmit 1 IP cksum packet");

	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts: TCP checksum offload ====== */

static int
test_xmit_pkts_tcp_cksum(void)
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

	nb_tx = hinic3_xmit_pkts(g_txq, tx_pkts, 1);

	RTE_TEST_ASSERT_EQUAL(nb_tx, 1, "should transmit 1 TCP cksum packet");

	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts: UDP checksum offload ====== */

static int
test_xmit_pkts_udp_cksum(void)
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

	nb_tx = hinic3_xmit_pkts(g_txq, tx_pkts, 1);

	RTE_TEST_ASSERT_EQUAL(nb_tx, 1, "should transmit 1 UDP cksum packet");

	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts: VLAN offload ====== */

static int
test_xmit_pkts_vlan_offload(void)
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

	nb_tx = hinic3_xmit_pkts(g_txq, tx_pkts, 1);

	RTE_TEST_ASSERT_EQUAL(nb_tx, 1, "should transmit 1 VLAN packet");

	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts: multiple packets ====== */

static int
test_xmit_pkts_multiple(void)
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
		RTE_TEST_ASSERT_NOT_NULL(pkts[i], "failed to create mbuf %d", i);
		tx_pkts[i] = pkts[i];
	}

	nb_tx = hinic3_xmit_pkts(g_txq, tx_pkts, 4);

	RTE_TEST_ASSERT_EQUAL(nb_tx, 4, "should transmit 4 packets");
	RTE_TEST_ASSERT_EQUAL(g_txq->txq_stats.packets, 4,
			      "stats.packets should be 4");

	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts: zero-length data (sge_len0) ====== */

static int
test_xmit_pkts_zero_len(void)
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

	nb_tx = hinic3_xmit_pkts(g_txq, tx_pkts, 1);

	RTE_TEST_ASSERT_EQUAL(nb_tx, 0, "zero-length mbuf should fail to xmit");
	RTE_TEST_ASSERT(g_txq->txq_stats.sge_len0 > 0,
			"sge_len0 stat should be incremented");

	rte_pktmbuf_free(pkt);
	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts: QINQ offload ====== */

static int
test_xmit_pkts_qinq_offload(void)
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

	nb_tx = hinic3_xmit_pkts(g_txq, tx_pkts, 1);

	RTE_TEST_ASSERT_EQUAL(nb_tx, 1, "should transmit 1 QINQ packet");

	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts: IP + VLAN + TCP offload ====== */

static int
test_xmit_pkts_ip_vlan_tcp_cksum(void)
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

	nb_tx = hinic3_xmit_pkts(g_txq, tx_pkts, 1);

	RTE_TEST_ASSERT_EQUAL(nb_tx, 1, "should transmit 1 combined offload packet");

	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts: single-seg mode ====== */

static int
test_xmit_pkts_single_seg(void)
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

	nb_tx = hinic3_xmit_pkts(g_txq, tx_pkts, 1);

	RTE_TEST_ASSERT_EQUAL(nb_tx, 1, "should transmit 1 packet in single-seg mode");

	g_txq->multi_segs = true;
	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts: SCTP checksum offload ====== */

static int
test_xmit_pkts_sctp_cksum(void)
{
	struct rte_mbuf *pkt;
	struct rte_mbuf *tx_pkts[1];
	u16 nb_tx;

	g_txq->status = HINIC3_TXQ_STATUS_START;
	g_txq->cons_idx = 0;
	g_txq->prod_idx = 0;
	g_txq->owner = 1;
	*g_ci_vaddr = 0;

	pkt = create_sctp_mbuf(128,
			       HINIC3_PKT_TX_IP_CKSUM |
			       HINIC3_PKT_TX_SCTP_CKSUM);
	RTE_TEST_ASSERT_NOT_NULL(pkt, "failed to create mbuf");
	tx_pkts[0] = pkt;

	nb_tx = hinic3_xmit_pkts(g_txq, tx_pkts, 1);

	RTE_TEST_ASSERT_EQUAL(nb_tx, 1, "should transmit 1 SCTP cksum packet");

	return TEST_SUCCESS;
}

/* ====== hinic3_tx_done_cleanup: empty queue ====== */

static int
test_tx_done_cleanup_empty(void)
{
	int ret;

	g_txq->cons_idx = 0;
	g_txq->prod_idx = 0;
	*g_ci_vaddr = 0;

	ret = hinic3_tx_done_cleanup(g_txq, 0);

	RTE_TEST_ASSERT(ret >= 0, "cleanup on empty queue should succeed");

	return TEST_SUCCESS;
}

/* ====== hinic3_tx_done_cleanup: with transmitted packets ====== */

static int
test_tx_done_cleanup_with_pkts(void)
{
	struct rte_mbuf *pkt;
	struct rte_mbuf *tx_pkts[1];
	u16 nb_tx;
	int ret;

	g_txq->cons_idx = 0;
	g_txq->prod_idx = 0;
	g_txq->owner = 1;
	*g_ci_vaddr = 0;

	/* First, send a packet to populate tx_info */
	pkt = create_simple_mbuf(128, 0);
	RTE_TEST_ASSERT_NOT_NULL(pkt, "failed to create mbuf");

	tx_pkts[0] = pkt;
	nb_tx = hinic3_xmit_pkts(g_txq, tx_pkts, 1);
	RTE_TEST_ASSERT_EQUAL(nb_tx, 1, "should transmit 1 packet");

	/* Simulate HW completing the packet by advancing CI */
	*g_ci_vaddr = rte_cpu_to_be_16(g_txq->prod_idx);

	ret = hinic3_tx_done_cleanup(g_txq, 32);

	RTE_TEST_ASSERT(ret >= 0, "cleanup should succeed");

	return TEST_SUCCESS;
}

/* ====== hinic3_free_txq_mbufs: basic ====== */

static int
test_free_txq_mbufs(void)
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
	nb_tx = hinic3_xmit_pkts(g_txq, tx_pkts, 1);
	RTE_TEST_ASSERT_EQUAL(nb_tx, 1, "should transmit 1 packet");

	/* Free mbufs - should free the mbuf in tx_info */
	hinic3_free_txq_mbufs(g_txq);

	/* Verify all tx_info mbufs are freed */
	for (i = 0; i < TEST_TXQ_DEPTH; i++) {
		RTE_TEST_ASSERT_NULL(g_tx_info[i].mbuf,
				     "tx_info[%d].mbuf should be NULL after free", i);
	}

	return TEST_SUCCESS;
}

/* ====== hinic3_free_all_txq_mbufs ====== */

static int
test_free_all_txq_mbufs(void)
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
	nb_tx = hinic3_xmit_pkts(g_txq, tx_pkts, 1);
	RTE_TEST_ASSERT_EQUAL(nb_tx, 1, "should transmit 1 packet");

	hinic3_free_all_txq_mbufs(g_nic_dev);

	/* Verify all tx_info mbufs are freed */
	for (i = 0; i < TEST_TXQ_DEPTH; i++) {
		RTE_TEST_ASSERT_NULL(g_tx_info[i].mbuf,
				     "tx_info[%d].mbuf should be NULL after free_all", i);
	}

	return TEST_SUCCESS;
}

/* ====== hinic3_start_all_sqs ====== */

static int
test_start_all_sqs(void)
{
	struct rte_eth_dev eth_dev = {0};
	struct rte_eth_dev_data eth_data = {0};
	struct hinic3_txq txq = {0};
	struct hinic3_nic_dev nic_dev = {0};
	struct hinic3_txq *txqs[1] = {&txq};

	nic_dev.num_sqs = 1;
	nic_dev.txqs = txqs;

	eth_data.dev_private = &nic_dev;
	eth_data.tx_queues = rte_zmalloc("test_tx_queues",
					  sizeof(void *), 0);
	RTE_TEST_ASSERT_NOT_NULL(eth_data.tx_queues, "tx_queues alloc failed");

	eth_data.tx_queue_state[0] = RTE_ETH_QUEUE_STATE_STOPPED;

	eth_dev.data = &eth_data;

	txq.is_hairpin = false;
	txq.status = HINIC3_TXQ_STATUS_STOP;

	eth_data.tx_queues[0] = &txq;

	int ret = hinic3_start_all_sqs(&eth_dev);

	RTE_TEST_ASSERT_EQUAL(ret, 0, "start_all_sqs should succeed");
	RTE_TEST_ASSERT(HINIC3_TXQ_IS_STARTED(&txq), "txq should be started");
	RTE_TEST_ASSERT_EQUAL(eth_data.tx_queue_state[0],
			      RTE_ETH_QUEUE_STATE_STARTED,
			      "queue state should be STARTED");

	rte_free(eth_data.tx_queues);

	return TEST_SUCCESS;
}

/* ====== hinic3_start_all_sqs: skip hairpin ====== */

static int
test_start_all_sqs_skip_hairpin(void)
{
	struct rte_eth_dev eth_dev = {0};
	struct rte_eth_dev_data eth_data = {0};
	struct hinic3_txq txq = {0};
	struct hinic3_nic_dev nic_dev = {0};
	struct hinic3_txq *txqs[1] = {&txq};

	nic_dev.num_sqs = 1;
	nic_dev.txqs = txqs;

	eth_data.dev_private = &nic_dev;
	eth_data.tx_queues = rte_zmalloc("test_tx_queues",
					  sizeof(void *), 0);
	RTE_TEST_ASSERT_NOT_NULL(eth_data.tx_queues, "tx_queues alloc failed");

	eth_data.tx_queue_state[0] = RTE_ETH_QUEUE_STATE_STOPPED;

	eth_dev.data = &eth_data;

	txq.is_hairpin = true;
	txq.status = HINIC3_TXQ_STATUS_STOP;

	eth_data.tx_queues[0] = &txq;

	int ret = hinic3_start_all_sqs(&eth_dev);

	RTE_TEST_ASSERT_EQUAL(ret, 0, "start_all_sqs should succeed");
	RTE_TEST_ASSERT(HINIC3_TXQ_IS_STOPPED(&txq),
			"hairpin txq should remain stopped");

	rte_free(eth_data.tx_queues);

	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts: IPv6 + TCP checksum ====== */

static int
test_xmit_pkts_ipv6_tcp_cksum(void)
{
	struct rte_mbuf *mbuf;
	struct rte_mbuf *tx_pkts[1];
	u16 nb_tx;
	uint16_t l2_len = sizeof(struct rte_ether_hdr);
	uint16_t l3_len = sizeof(struct rte_ipv6_hdr);
	uint16_t l4_len = sizeof(struct rte_tcp_hdr);
	uint16_t hdr_len = l2_len + l3_len + l4_len;
	uint16_t data_len = hdr_len + 64;
	uint8_t *pkt;
	struct rte_ether_hdr *eth;
	struct rte_ipv6_hdr *ipv6;
	struct rte_tcp_hdr *tcp;

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
	ipv6->proto = IPPROTO_TCP;
	ipv6->payload_len = rte_cpu_to_be_16(data_len - l2_len - l3_len);

	tcp = (struct rte_tcp_hdr *)(pkt + l2_len + l3_len);
	tcp->src_port = rte_cpu_to_be_16(12345);
	tcp->dst_port = rte_cpu_to_be_16(80);

	mbuf->ol_flags = HINIC3_PKT_TX_IPV6 | HINIC3_PKT_TX_TCP_CKSUM;
	mbuf->l2_len = l2_len;
	mbuf->l3_len = l3_len;
	mbuf->l4_len = l4_len;
	mbuf->nb_segs = 1;
	mbuf->pkt_len = data_len;
	mbuf->data_len = data_len;

	tx_pkts[0] = mbuf;

	nb_tx = hinic3_xmit_pkts(g_txq, tx_pkts, 1);

	RTE_TEST_ASSERT_EQUAL(nb_tx, 1, "should transmit 1 IPv6+TCP packet");

	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts: IPv6 + UDP checksum ====== */

static int
test_xmit_pkts_ipv6_udp_cksum(void)
{
	struct rte_mbuf *mbuf;
	struct rte_mbuf *tx_pkts[1];
	u16 nb_tx;
	uint16_t l2_len = sizeof(struct rte_ether_hdr);
	uint16_t l3_len = sizeof(struct rte_ipv6_hdr);
	uint16_t l4_len = sizeof(struct rte_udp_hdr);
	uint16_t hdr_len = l2_len + l3_len + l4_len;
	uint16_t data_len = hdr_len + 64;
	uint8_t *pkt;
	struct rte_ether_hdr *eth;
	struct rte_ipv6_hdr *ipv6;
	struct rte_udp_hdr *udp;

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

	nb_tx = hinic3_xmit_pkts(g_txq, tx_pkts, 1);

	RTE_TEST_ASSERT_EQUAL(nb_tx, 1, "should transmit 1 IPv6+UDP packet");

	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts: stats tracking ====== */

static int
test_xmit_pkts_stats(void)
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

	nb_tx = hinic3_xmit_pkts(g_txq, tx_pkts, 1);

	RTE_TEST_ASSERT_EQUAL(nb_tx, 1, "should transmit 1 packet");
	RTE_TEST_ASSERT_EQUAL(g_txq->txq_stats.packets, 1,
			      "stats.packets should be 1");
	RTE_TEST_ASSERT_EQUAL(g_txq->txq_stats.bytes, 256,
			      "stats.bytes should be 256");
	RTE_TEST_ASSERT_EQUAL(g_txq->txq_stats.burst_pkts, 1,
			      "stats.burst_pkts should be 1");

	return TEST_SUCCESS;
}

/* ====== SQ free wqebbs calculation ====== */

static int
test_sq_free_wqebbs_calc(void)
{
	u16 free_wqebbs;

	g_txq->q_depth = 64;
	g_txq->q_mask = 63;
	g_txq->prod_idx = 0;
	g_txq->cons_idx = 0;

	/* When prod=0, cons=0: free = depth - (0 - 0 + depth) & mask - 1 = depth - 1 */
	free_wqebbs = ((g_txq->q_depth -
		(((g_txq->prod_idx - g_txq->cons_idx) + g_txq->q_depth) & g_txq->q_mask)) - 1);
	RTE_TEST_ASSERT_EQUAL(free_wqebbs, 63,
			      "free_wqebbs should be 63 when queue is empty");

	/* When prod=10, cons=0: occupied = 10, free = 64 - 10 - 1 = 53 */
	g_txq->prod_idx = 10;
	free_wqebbs = ((g_txq->q_depth -
		(((g_txq->prod_idx - g_txq->cons_idx) + g_txq->q_depth) & g_txq->q_mask)) - 1);
	RTE_TEST_ASSERT_EQUAL(free_wqebbs, 53,
			      "free_wqebbs should be 53 when 10 wqebbs used");

	/* When prod=63, cons=0: occupied = 63, free = 64 - 63 - 1 = 0 */
	g_txq->prod_idx = 63;
	free_wqebbs = ((g_txq->q_depth -
		(((g_txq->prod_idx - g_txq->cons_idx) + g_txq->q_depth) & g_txq->q_mask)) - 1);
	RTE_TEST_ASSERT_EQUAL(free_wqebbs, 0,
			      "free_wqebbs should be 0 when queue is full");

	g_txq->prod_idx = 0;
	return TEST_SUCCESS;
}

/* ====== SQ local CI update ====== */

static int
test_sq_local_ci_update(void)
{
	g_txq->q_mask = 63;
	g_txq->cons_idx = 0;

	g_txq->cons_idx += 5;
	RTE_TEST_ASSERT_EQUAL(g_txq->cons_idx, 5,
			      "cons_idx should be 5 after update");

	/* Wrap around */
	g_txq->cons_idx = 62;
	g_txq->cons_idx += 3;
	RTE_TEST_ASSERT_EQUAL(g_txq->cons_idx, 65,
			      "cons_idx should be 65 (unmasked)");

	g_txq->cons_idx = 0;
	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts: IP + VLAN + UDP offload ====== */

static int
test_xmit_pkts_ip_vlan_udp_cksum(void)
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

	nb_tx = hinic3_xmit_pkts(g_txq, tx_pkts, 1);

	RTE_TEST_ASSERT_EQUAL(nb_tx, 1, "should transmit 1 IP+VLAN+UDP packet");

	return TEST_SUCCESS;
}

/* ====== hinic3_stop_sq: basic ====== */

static int
test_stop_sq(void)
{
	int ret;

	g_txq->status = HINIC3_TXQ_STATUS_START;
	g_txq->cons_idx = 0;
	g_txq->prod_idx = 0;
	*g_ci_vaddr = 0;

	/* With empty queue, stop_sq should succeed quickly */
	ret = hinic3_stop_sq(g_txq);

	RTE_TEST_ASSERT_EQUAL(ret, 0, "stop_sq should succeed on empty queue");

	return TEST_SUCCESS;
}

/* ====== hinic3_stop_sq: NULL txq ====== */

static int
test_stop_sq_null(void)
{
	int ret;

	ret = hinic3_stop_sq(NULL);

	RTE_TEST_ASSERT_EQUAL(ret, 0, "stop_sq(NULL) should return 0");

	return TEST_SUCCESS;
}

/* ====== hinic3_stop_sq: hairpin skip ====== */

static int
test_stop_sq_hairpin(void)
{
	int ret;

	g_txq->is_hairpin = true;

	ret = hinic3_stop_sq(g_txq);

	RTE_TEST_ASSERT_EQUAL(ret, 0, "stop_sq should skip hairpin");

	g_txq->is_hairpin = false;
	return TEST_SUCCESS;
}

/* ====== hinic3_tx_burst_mode_get ====== */

static int
test_tx_burst_mode_get(void)
{
	struct rte_eth_dev eth_dev = {0};
	struct rte_eth_dev_data eth_data = {0};
	struct rte_eth_burst_mode mode;
	int ret;

	g_nic_dev->txqs[0] = g_txq;
	eth_data.dev_private = g_nic_dev;
	eth_dev.data = &eth_data;

	/* Set txmode offloads */
	eth_data.dev_conf.txmode.offloads = RTE_ETH_TX_OFFLOAD_MULTI_SEGS |
					    RTE_ETH_TX_OFFLOAD_IPV4_CKSUM |
					    RTE_ETH_TX_OFFLOAD_UDP_CKSUM |
					    RTE_ETH_TX_OFFLOAD_TCP_CKSUM |
					    RTE_ETH_TX_OFFLOAD_VLAN_INSERT;

	ret = hinic3_tx_burst_mode_get(&eth_dev, 0, &mode);

	RTE_TEST_ASSERT_EQUAL(ret, 0, "burst_mode_get should succeed");
	RTE_TEST_ASSERT(strstr(mode.info, "Scalar") != NULL,
			"mode.info should contain 'Scalar'");
	RTE_TEST_ASSERT(strstr(mode.info, "MULTI") != NULL,
			"mode.info should contain 'MULTI'");
	RTE_TEST_ASSERT(strstr(mode.info, "CKSUM") != NULL,
			"mode.info should contain 'CKSUM'");
	RTE_TEST_ASSERT(strstr(mode.info, "VLAN") != NULL,
			"mode.info should contain 'VLAN'");

	return TEST_SUCCESS;
}

/* ====== hinic3_tx_burst_mode_get: invalid queue ====== */

static int
test_tx_burst_mode_get_invalid_queue(void)
{
	struct rte_eth_dev eth_dev = {0};
	struct rte_eth_dev_data eth_data = {0};
	struct rte_eth_burst_mode mode;
	int ret;

	g_nic_dev->txqs[0] = g_txq;
	eth_data.dev_private = g_nic_dev;
	eth_dev.data = &eth_data;

	ret = hinic3_tx_burst_mode_get(&eth_dev, 1, &mode);

	RTE_TEST_ASSERT(ret != 0, "burst_mode_get should fail for invalid queue");

	return TEST_SUCCESS;
}

/* ====== hinic3_xmit_pkts: packet too large for non-TSO ====== */

static int
test_xmit_pkts_pkt_too_large(void)
{
	struct rte_mbuf *pkt;
	struct rte_mbuf *tx_pkts[1];
	u16 nb_tx;

	g_txq->status = HINIC3_TXQ_STATUS_START;
	g_txq->cons_idx = 0;
	g_txq->prod_idx = 0;
	g_txq->owner = 1;
	*g_ci_vaddr = 0;

	/* Create a packet with IP checksum offload but data_len > MAX_SINGLE_SGE_SIZE */
	pkt = create_simple_mbuf(128, HINIC3_PKT_TX_IP_CKSUM);
	RTE_TEST_ASSERT_NOT_NULL(pkt, "failed to create mbuf");
	/* Manually set pkt_len to exceed MAX_SINGLE_SGE_SIZE */
	pkt->pkt_len = MAX_SINGLE_SGE_SIZE + 1;
	tx_pkts[0] = pkt;

	nb_tx = hinic3_xmit_pkts(g_txq, tx_pkts, 1);

	RTE_TEST_ASSERT_EQUAL(nb_tx, 0,
			      "packet too large for non-TSO should fail");
	RTE_TEST_ASSERT(g_txq->txq_stats.off_errs > 0,
			"off_errs should be incremented");

	rte_pktmbuf_free(pkt);
	return TEST_SUCCESS;
}

static struct unit_test_suite hinic3_tx_test_suite = {
	.suite_name = "HINIC3 TX Unit Tests",
	.setup = tx_suite_setup,
	.teardown = tx_suite_teardown,
	.unit_test_cases = {
		/* hinic3_xmit_pkts tests */
		TEST_CASE_ST(tx_test_setup, tx_test_teardown, test_xmit_pkts_stopped),
		TEST_CASE_ST(tx_test_setup, tx_test_teardown, test_xmit_pkts_no_offload),
		TEST_CASE_ST(tx_test_setup, tx_test_teardown, test_xmit_pkts_ip_cksum),
		TEST_CASE_ST(tx_test_setup, tx_test_teardown, test_xmit_pkts_tcp_cksum),
		TEST_CASE_ST(tx_test_setup, tx_test_teardown, test_xmit_pkts_udp_cksum),
		TEST_CASE_ST(tx_test_setup, tx_test_teardown, test_xmit_pkts_sctp_cksum),
		TEST_CASE_ST(tx_test_setup, tx_test_teardown, test_xmit_pkts_vlan_offload),
		TEST_CASE_ST(tx_test_setup, tx_test_teardown, test_xmit_pkts_qinq_offload),
		TEST_CASE_ST(tx_test_setup, tx_test_teardown, test_xmit_pkts_ip_vlan_tcp_cksum),
		TEST_CASE_ST(tx_test_setup, tx_test_teardown, test_xmit_pkts_ip_vlan_udp_cksum),
		TEST_CASE_ST(tx_test_setup, tx_test_teardown, test_xmit_pkts_multiple),
		TEST_CASE_ST(tx_test_setup, tx_test_teardown, test_xmit_pkts_zero_len),
		TEST_CASE_ST(tx_test_setup, tx_test_teardown, test_xmit_pkts_single_seg),
		TEST_CASE_ST(tx_test_setup, tx_test_teardown, test_xmit_pkts_ipv6_tcp_cksum),
		TEST_CASE_ST(tx_test_setup, tx_test_teardown, test_xmit_pkts_ipv6_udp_cksum),
		TEST_CASE_ST(tx_test_setup, tx_test_teardown, test_xmit_pkts_pkt_too_large),
		TEST_CASE_ST(tx_test_setup, tx_test_teardown, test_xmit_pkts_stats),

		/* hinic3_tx_done_cleanup tests */
		TEST_CASE_ST(tx_test_setup, tx_test_teardown, test_tx_done_cleanup_empty),
		TEST_CASE_ST(tx_test_setup, tx_test_teardown, test_tx_done_cleanup_with_pkts),

		/* hinic3_free_txq_mbufs / hinic3_free_all_txq_mbufs */
		TEST_CASE_ST(tx_test_setup, tx_test_teardown, test_free_txq_mbufs),
		TEST_CASE_ST(tx_test_setup, tx_test_teardown, test_free_all_txq_mbufs),

		/* hinic3_start_all_sqs */
		TEST_CASE(test_start_all_sqs),
		TEST_CASE(test_start_all_sqs_skip_hairpin),

		/* hinic3_stop_sq */
		TEST_CASE_ST(tx_test_setup, tx_test_teardown, test_stop_sq),
		TEST_CASE(test_stop_sq_null),
		TEST_CASE_ST(tx_test_setup, tx_test_teardown, test_stop_sq_hairpin),

		/* hinic3_tx_burst_mode_get */
		TEST_CASE_ST(tx_test_setup, tx_test_teardown, test_tx_burst_mode_get),
		TEST_CASE_ST(tx_test_setup, tx_test_teardown, test_tx_burst_mode_get_invalid_queue),

		/* SQ internal calculations */
		TEST_CASE_ST(tx_test_setup, tx_test_teardown, test_sq_free_wqebbs_calc),
		TEST_CASE_ST(tx_test_setup, tx_test_teardown, test_sq_local_ci_update),

		TEST_CASES_END()
	}
};

static int
test_hinic3_tx(void)
{
	return unit_test_suite_runner(&hinic3_tx_test_suite);
}

REGISTER_TEST_COMMAND(hinic3_tx_autotest, test_hinic3_tx);
