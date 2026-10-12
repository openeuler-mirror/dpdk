/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2025-2026 Huawei Technologies Co., Ltd
 * Unit tests for hinic3_pmd_rx.c - testing through public interfaces
 */

/* Match driver's endianness convention (driver meson.build defines HW_CONVERT_ENDIAN) */
#define HW_CONVERT_ENDIAN

#include <rte_common.h>
#include <rte_ethdev.h>
#ifdef DPDK_21_11
#include <ethdev_driver.h>
#endif
#include <rte_malloc.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>
#include <rte_byteorder.h>

#include "../test.h"
#include <rte_test.h>
#include <rte_log.h>

#include "base/hinic3_compat.h"
#include "base/hinic3_pmd_nic_cfg.h"
#include "hinic3_pmd_nic_io.h"
#include "hinic3_pmd_ethdev.h"
#include "hinic3_pmd_rx.h"
#include "test_hinic3_mock.h"

#define TEST_RXQ_DEPTH 64
#define TEST_RXQ_MASK  (TEST_RXQ_DEPTH - 1)
#define TEST_PKT_NUM   8

/* Build CQE status word with RXDONE bit set */
#define MAKE_CQE_STATUS(csum_err, lro_num, rxdone) \
	((((csum_err) & RQ_CQE_STATUS_CSUM_ERR_MASK) << RQ_CQE_STATUS_CSUM_ERR_SHIFT) | \
	(((lro_num) & RQ_CQE_STATUS_NUM_LRO_MASK) << RQ_CQE_STATUS_NUM_LRO_SHIFT) | \
	(((rxdone) & RQ_CQE_STATUS_RXDONE_MASK) << RQ_CQE_STATUS_RXDONE_SHIFT))

/* Build CQE offload_type word */
#define MAKE_CQE_OFFLOAD(ptype_offload, vlan_en, rss_type) \
	((((ptype_offload) & RQ_CQE_OFFOLAD_TYPE_PTYPE_OFFLOAD_MASK) << RQ_CQE_OFFOLAD_TYPE_PTYPE_OFFLOAD_SHIFT) | \
	(((vlan_en) & RQ_CQE_OFFOLAD_TYPE_VLAN_EN_MASK) << RQ_CQE_OFFOLAD_TYPE_VLAN_EN_SHIFT) | \
	(((rss_type) & RQ_CQE_OFFOLAD_TYPE_RSS_TYPE_MASK) << RQ_CQE_OFFOLAD_TYPE_RSS_TYPE_SHIFT))

/* Build CQE vlan_len word */
#define MAKE_CQE_VLAN_LEN(pkt_len, vlan_tag) \
	((((vlan_tag) & RQ_CQE_SGE_VLAN_MASK) << RQ_CQE_SGE_VLAN_SHIFT) | \
	(((pkt_len) & RQ_CQE_SGE_LEN_MASK) << RQ_CQE_SGE_LEN_SHIFT))


/* Build compact CQE dw0 (status) for integrated CQE */
#define MAKE_COMPACT_CQE_DW0(rxdone, cqe_type, vlan_en, \
			     pkt_fmt, ip_type, cqe_len, csum_err, \
			     pkt_type, pkt_len) \
	((((rxdone) & 1U) << RQ_COMPACT_CQE_STATUS_RXDONE_SHIFT) | \
	 (((cqe_type) & 1U) << RQ_COMPACT_CQE_STATUS_CQE_TYPE_SHIFT) | \
	 (((vlan_en) & 1U) << RQ_COMPACT_CQE_STATUS_VLAN_EN_SHIFT) | \
	 (((pkt_fmt) & RQ_COMPACT_CQE_STATUS_PKT_FORMAT_MASK) << RQ_COMPACT_CQE_STATUS_PKT_FORMAT_SHIFT) | \
	 (((ip_type) & 1U) << RQ_COMPACT_CQE_STATUS_IP_TYPE_SHIFT) | \
	 (((cqe_len) & 1U) << RQ_COMPACT_CQE_STATUS_CQE_LEN_SHIFT) | \
	 (((csum_err) & RQ_COMPACT_CQE_STATUS_CSUM_ERR_MASK) << RQ_COMPACT_CQE_STATUS_CSUM_ERR_SHIFT) | \
	 (((pkt_type) & RQ_COMPACT_CQE_STATUS_PKT_TYPE_MASK) << RQ_COMPACT_CQE_STATUS_PKT_TYPE_SHIFT) | \
	 ((pkt_len) & RQ_COMPACT_CQE_STATUS_PKT_LEN_MASK))

/* Build compact CQE dw2 (offload) with vlan tag and lro num */
#define MAKE_COMPACT_CQE_DW2(lro_num, vlan_tag) \
	((((lro_num) & RQ_COMPACT_CQE_OFFLOAD_NUM_LRO_MASK) << RQ_COMPACT_CQE_OFFLOAD_NUM_LRO_SHIFT) | \
	 (((vlan_tag) & RQ_COMPACT_CQE_OFFLOAD_VLAN_MASK) << RQ_COMPACT_CQE_OFFLOAD_VLAN_SHIFT))

static struct rte_mempool *g_mp;
static struct hinic3_nic_dev *g_nic_dev;
static struct hinic3_rxq *g_rxq;
static struct hinic3_rx_info *g_rx_info;
static struct hinic3_rq_cqe *g_rx_cqe;
static int g_orig_log_level;

/* Globals for compact CQE tests */
static struct hinic3_rq_ci_wb *g_rq_ci;

/* Globals for free_all_rxq_mbufs test */
static struct hinic3_rxq *g_rxq2;
static struct hinic3_rx_info *g_rx_info2;
static struct hinic3_rq_cqe *g_rx_cqe2;
static struct hinic3_rxq **g_rxqs_arr;

static int
rx_suite_setup(void)
{
    g_orig_log_level = rte_log_get_global_level();
    rte_log_set_level_pattern("pmd.net.hinic3", RTE_LOG_INFO);

    g_mp = rte_pktmbuf_pool_create("test_rx_mp", 256, 0, 0,
                                   RTE_PKTMBUF_HEADROOM + 2048,
                                   SOCKET_ID_ANY);
    if (!g_mp)
        return TEST_FAILED;

    return 0;
}

static void
rx_suite_teardown(void)
{
    rte_mempool_free(g_mp);
    g_mp = NULL;
    rte_log_set_level_pattern("pmd.net.hinic3", g_orig_log_level);
}

/* Construct a fake rxq + rte_eth_dev environment for hinic3_recv_pkts.
 * Key: set delta very small so hinic3_rearm_rxq_mbuf returns -ENOMEM
 * early (free_wqebbs < rx_free_thresh) and skips WQE fill + doorbell.
 */
static int
rx_recv_setup(void)
{
    int i;

    /* Allocate nic_dev */
    g_nic_dev = rte_zmalloc("test_nic_dev", sizeof(*g_nic_dev), 0);
    if (!g_nic_dev)
        return TEST_FAILED;
    g_nic_dev->rx_csum_en = HINIC3_DEFAULT_RX_CSUM_OFFLOAD;
    g_nic_dev->feature_cap = NIC_F_TX_WQE_COMPACT_TASK;
    g_nic_dev->num_rss = 0;
    g_nic_dev->rss_state = HINIC3_RSS_DISABLE;
    g_nic_dev->num_rqs = 0;
    g_nic_dev->config.rx_empty_threshold = 0;
    strcpy(g_nic_dev->dev_name, "test_hinic3_rx");

    /* Set up rte_eth_devices[port_id] with dev_private pointing to nic_dev */
    rte_eth_devices[0].data = rte_zmalloc("test_eth_data",
                                           sizeof(struct rte_eth_dev_data), 0);
    if (!rte_eth_devices[0].data)
        return TEST_FAILED;
    rte_eth_devices[0].data->dev_private = g_nic_dev;
    rte_eth_devices[0].data->scattered_rx = 0;

    /* Initialize ptype table via the public API */
    if (hinic3_init_rx_ptype_table(&rte_eth_devices[0]) != 0)
        return TEST_FAILED;

    /* Allocate rx_info array */
    g_rx_info = rte_zmalloc("test_rx_info",
                             TEST_RXQ_DEPTH * sizeof(*g_rx_info), 0);
    if (!g_rx_info)
        return TEST_FAILED;

    for (i = 0; i < TEST_RXQ_DEPTH; i++) {
        g_rx_info[i].mbuf = rte_pktmbuf_alloc(g_mp);
        if (!g_rx_info[i].mbuf)
            return TEST_FAILED;
    }

    /* Allocate rx_cqe array */
    g_rx_cqe = rte_zmalloc_socket("test_rx_cqe",
                                   TEST_RXQ_DEPTH * sizeof(*g_rx_cqe),
                                   RTE_CACHE_LINE_SIZE, SOCKET_ID_ANY);
    if (!g_rx_cqe)
        return TEST_FAILED;
    for (i = 0; i < TEST_RXQ_DEPTH; i++)
        g_rx_cqe[i].status = 0;

    /* Allocate rxq */
    g_rxq = rte_zmalloc("test_rxq", sizeof(*g_rxq), 0);
    if (!g_rxq)
        return TEST_FAILED;

    g_rxq->nic_dev = g_nic_dev;
    g_rxq->rx_info = g_rx_info;
    g_rxq->rx_cqe = g_rx_cqe;
    g_rxq->mb_pool = g_mp;
    g_rxq->port_id = 0;
    g_rxq->q_id = 0;
    g_rxq->local_qid = 0;
    g_rxq->q_depth = TEST_RXQ_DEPTH;
    g_rxq->q_mask = TEST_RXQ_MASK;
    g_rxq->buf_len = 2048;
    g_rxq->cons_idx = 0;
    g_rxq->prod_idx = TEST_RXQ_DEPTH;
    g_rxq->delta = 1; /* free_wqebbs = delta-1 = 0 < rx_free_thresh, rearm skips */
    g_rxq->rx_free_thresh = 32;
    g_rxq->wqe_type = HINIC3_EXTEND_RQ_WQE;
    g_rxq->is_scattered_rx = 0;
    g_rxq->wait_time_cycle = 0;
    g_rxq->rxq_stats.empty = 0;
    g_rxq->rxq_stats.tsc = 0;

    return TEST_SUCCESS;
}

static void
rx_recv_teardown(void)
{
    int i;

    /* Free mbufs in rx_info (safe: mbuf alloc failure leaves slot NULL) */
    if (g_rx_info) {
        for (i = 0; i < TEST_RXQ_DEPTH; i++) {
            if (g_rx_info[i].mbuf)
                rte_pktmbuf_free(g_rx_info[i].mbuf);
        }
        rte_free(g_rx_info);
    }

    /* Free ptype_tbl before nic_dev (it's a separate allocation) */
    if (g_nic_dev && g_nic_dev->ptype_tbl)
        rte_free(g_nic_dev->ptype_tbl);

    if (rte_eth_devices[0].data) {
        rte_free(rte_eth_devices[0].data);
        rte_eth_devices[0].data = NULL;
    }

    if (g_rx_cqe)
        rte_free(g_rx_cqe);
    if (g_rxq)
        rte_free(g_rxq);
    if (g_nic_dev)
        rte_free(g_nic_dev);

    g_rx_info = NULL;
    g_rx_cqe = NULL;
    g_rxq = NULL;
    g_nic_dev = NULL;
}

static void
fill_cqe_slot(u16 slot, u32 status, u32 vlan_len, u32 offload_type, u32 hash_val)
{
    g_rx_cqe[slot].status = status;
    g_rx_cqe[slot].vlan_len = vlan_len;
    g_rx_cqe[slot].offload_type = offload_type;
    g_rx_cqe[slot].hash_val = hash_val;
}

/* ====== hinic3_recv_pkts: empty queue ====== */

static int
test_recv_pkts_empty_queue(void)
{
    struct rte_mbuf *rx_pkts[TEST_PKT_NUM];
    u16 count;

    g_rxq->cons_idx = 0;
    g_rxq->rxq_stats.empty = 0;

    count = hinic3_recv_pkts(g_rxq, rx_pkts, TEST_PKT_NUM);

    RTE_TEST_ASSERT_EQUAL(count, 0, "empty queue should return 0 pkts");
    return TEST_SUCCESS;
}

/* ====== hinic3_recv_pkts: single IPv4 TCP packet ====== */

static int
test_recv_pkts_single_pkt(void)
{
    struct rte_mbuf *rx_pkts[TEST_PKT_NUM];
    u32 status, offload_type, vlan_len;
    u16 count;

    g_rxq->cons_idx = 0;
    g_rxq->rxq_stats.empty = 0;

    status = MAKE_CQE_STATUS(0, 0, 1);
    vlan_len = MAKE_CQE_VLAN_LEN(128, 0);
    offload_type = MAKE_CQE_OFFLOAD(
        (IPSU_METADATA_FMT_NO_ENC << RQ_CQE_OFFOLAD_TYPE_PKT_FORMAT_SHIFT) |
        (IPSU_METADATA_L3_TP_IPV4 << RQ_CQE_OFFOLAD_TYPE_IP_TYPE_SHIFT) |
        (IPSU_PKT_TYPE_TCP << RQ_CQE_OFFOLAD_TYPE_PKT_TYPE_SHIFT),
        0, 1);

    fill_cqe_slot(0, status, vlan_len, offload_type, 0x12345678);

    count = hinic3_recv_pkts(g_rxq, rx_pkts, TEST_PKT_NUM);

    RTE_TEST_ASSERT_EQUAL(count, 1, "should receive 1 packet");
    RTE_TEST_ASSERT_EQUAL(rx_pkts[0]->pkt_len, 128, "pkt_len should be 128");
    RTE_TEST_ASSERT_EQUAL(rx_pkts[0]->data_len, 128, "data_len should be 128");
    RTE_TEST_ASSERT_EQUAL(rx_pkts[0]->port, 0, "port should be 0");

    /* Verify checksum offload: csum_err=0 -> GOOD */
    RTE_TEST_ASSERT((rx_pkts[0]->ol_flags & HINIC3_PKT_RX_IP_CKSUM_GOOD) != 0,
                    "should have IP_CKSUM_GOOD");
    RTE_TEST_ASSERT((rx_pkts[0]->ol_flags & HINIC3_PKT_RX_L4_CKSUM_GOOD) != 0,
                    "should have L4_CKSUM_GOOD");

    /* Verify ptype */
    RTE_TEST_ASSERT((rx_pkts[0]->packet_type & RTE_PTYPE_L3_IPV4_EXT_UNKNOWN) != 0,
                    "should have L3_IPV4");
    RTE_TEST_ASSERT((rx_pkts[0]->packet_type & RTE_PTYPE_L4_TCP) != 0,
                    "should have L4_TCP");

    /* Verify RSS */
    RTE_TEST_ASSERT((rx_pkts[0]->ol_flags & HINIC3_PKT_RX_RSS_HASH) != 0,
                    "should have RSS_HASH flag");

    /* Verify no VLAN */
    RTE_TEST_ASSERT_EQUAL(rx_pkts[0]->vlan_tci, 0,
                          "vlan_tci should be 0 without vlan");

    g_rx_cqe[0].status = 0;
    return TEST_SUCCESS;
}

/* ====== hinic3_recv_pkts: multiple IPv6 UDP packets ====== */

static int
test_recv_pkts_multiple_pkts(void)
{
    struct rte_mbuf *rx_pkts[TEST_PKT_NUM];
    u32 status, vlan_len, offload_type;
    u16 count;
    int i;

    g_rxq->cons_idx = 0;
    g_rxq->rxq_stats.empty = 0;

    for (i = 0; i < 3; i++) {
        status = MAKE_CQE_STATUS(0, 0, 1);
        vlan_len = MAKE_CQE_VLAN_LEN(64 + i * 10, 0);
        offload_type = MAKE_CQE_OFFLOAD(
            (IPSU_METADATA_FMT_NO_ENC << RQ_CQE_OFFOLAD_TYPE_PKT_FORMAT_SHIFT) |
            (IPSU_METADATA_L3_TP_IPV6 << RQ_CQE_OFFOLAD_TYPE_IP_TYPE_SHIFT) |
            (IPSU_PKT_TYPE_UDP << RQ_CQE_OFFOLAD_TYPE_PKT_TYPE_SHIFT),
            0, 1);
        fill_cqe_slot(i, status, vlan_len, offload_type, 0xAA000000 + i);
    }

    count = hinic3_recv_pkts(g_rxq, rx_pkts, TEST_PKT_NUM);

    RTE_TEST_ASSERT_EQUAL(count, 3, "should receive 3 packets");
    RTE_TEST_ASSERT_EQUAL(rx_pkts[0]->pkt_len, 64, "pkt0 len should be 64");
    RTE_TEST_ASSERT_EQUAL(rx_pkts[1]->pkt_len, 74, "pkt1 len should be 74");
    RTE_TEST_ASSERT_EQUAL(rx_pkts[2]->pkt_len, 84, "pkt2 len should be 84");

    for (i = 0; i < 3; i++) {
        RTE_TEST_ASSERT((rx_pkts[i]->packet_type & RTE_PTYPE_L3_IPV6_EXT_UNKNOWN) != 0,
                        "pkt%d should have L3_IPV6", i);
        RTE_TEST_ASSERT((rx_pkts[i]->packet_type & RTE_PTYPE_L4_UDP) != 0,
                        "pkt%d should have L4_UDP", i);
    }

    for (i = 0; i < 3; i++)
        g_rx_cqe[i].status = 0;
    return TEST_SUCCESS;
}

/* ====== hinic3_recv_pkts: VLAN offload ====== */

static int
test_recv_pkts_vlan_offload(void)
{
    struct rte_mbuf *rx_pkts[TEST_PKT_NUM];
    u32 status, vlan_len, offload_type;
    u16 count;

    g_rxq->cons_idx = 0;
    g_rxq->rxq_stats.empty = 0;

    status = MAKE_CQE_STATUS(0, 0, 1);
    vlan_len = MAKE_CQE_VLAN_LEN(64, 100);
    offload_type = MAKE_CQE_OFFLOAD(
        (IPSU_METADATA_FMT_NO_ENC << RQ_CQE_OFFOLAD_TYPE_PKT_FORMAT_SHIFT) |
        (IPSU_METADATA_L3_TP_IPV4 << RQ_CQE_OFFOLAD_TYPE_IP_TYPE_SHIFT) |
        (IPSU_PKT_TYPE_TCP << RQ_CQE_OFFOLAD_TYPE_PKT_TYPE_SHIFT),
        1, 0);

    fill_cqe_slot(0, status, vlan_len, offload_type, 0);

    count = hinic3_recv_pkts(g_rxq, rx_pkts, 1);

    RTE_TEST_ASSERT_EQUAL(count, 1, "should receive 1 packet");
    RTE_TEST_ASSERT_EQUAL(rx_pkts[0]->vlan_tci, 100, "vlan_tci should be 100");
    RTE_TEST_ASSERT((rx_pkts[0]->ol_flags & HINIC3_PKT_RX_VLAN) != 0,
                    "should have RX_VLAN flag");
    RTE_TEST_ASSERT((rx_pkts[0]->ol_flags & HINIC3_PKT_RX_VLAN_STRIPPED) != 0,
                    "should have RX_VLAN_STRIPPED flag");

    g_rx_cqe[0].status = 0;
    return TEST_SUCCESS;
}

/* ====== hinic3_recv_pkts: IP checksum error ====== */

static int
test_recv_pkts_csum_ip_error(void)
{
    struct rte_mbuf *rx_pkts[TEST_PKT_NUM];
    u32 status, vlan_len, offload_type;
    u16 count;

    g_rxq->cons_idx = 0;
    g_rxq->rxq_stats.empty = 0;

    status = MAKE_CQE_STATUS(HINIC3_RX_CSUM_IP_CSUM_ERR, 0, 1);
    vlan_len = MAKE_CQE_VLAN_LEN(64, 0);
    offload_type = MAKE_CQE_OFFLOAD(
        (IPSU_METADATA_FMT_NO_ENC << RQ_CQE_OFFOLAD_TYPE_PKT_FORMAT_SHIFT) |
        (IPSU_METADATA_L3_TP_IPV4 << RQ_CQE_OFFOLAD_TYPE_IP_TYPE_SHIFT) |
        (IPSU_PKT_TYPE_TCP << RQ_CQE_OFFOLAD_TYPE_PKT_TYPE_SHIFT),
        0, 0);

    fill_cqe_slot(0, status, vlan_len, offload_type, 0);

    count = hinic3_recv_pkts(g_rxq, rx_pkts, 1);

    RTE_TEST_ASSERT_EQUAL(count, 1, "should receive packet even with csum error");
    RTE_TEST_ASSERT((rx_pkts[0]->ol_flags & HINIC3_PKT_RX_IP_CKSUM_BAD) != 0,
                    "should have IP_CKSUM_BAD for IP csum error");

    g_rx_cqe[0].status = 0;
    return TEST_SUCCESS;
}

/* ====== hinic3_recv_pkts: TCP checksum error ====== */

static int
test_recv_pkts_csum_tcp_error(void)
{
    struct rte_mbuf *rx_pkts[TEST_PKT_NUM];
    u32 status, vlan_len, offload_type;
    u16 count;

    g_rxq->cons_idx = 0;
    g_rxq->rxq_stats.empty = 0;

    status = MAKE_CQE_STATUS(HINIC3_RX_CSUM_TCP_CSUM_ERR, 0, 1);
    vlan_len = MAKE_CQE_VLAN_LEN(64, 0);
    offload_type = MAKE_CQE_OFFLOAD(
        (IPSU_METADATA_FMT_NO_ENC << RQ_CQE_OFFOLAD_TYPE_PKT_FORMAT_SHIFT) |
        (IPSU_METADATA_L3_TP_IPV4 << RQ_CQE_OFFOLAD_TYPE_IP_TYPE_SHIFT) |
        (IPSU_PKT_TYPE_TCP << RQ_CQE_OFFOLAD_TYPE_PKT_TYPE_SHIFT),
        0, 0);

    fill_cqe_slot(0, status, vlan_len, offload_type, 0);

    count = hinic3_recv_pkts(g_rxq, rx_pkts, 1);

    RTE_TEST_ASSERT_EQUAL(count, 1, "should receive packet");
    RTE_TEST_ASSERT((rx_pkts[0]->ol_flags & HINIC3_PKT_RX_L4_CKSUM_BAD) != 0,
                    "should have L4_CKSUM_BAD for TCP csum error");

    g_rx_cqe[0].status = 0;
    return TEST_SUCCESS;
}

/* ====== hinic3_recv_pkts: LRO ====== */

static int
test_recv_pkts_lro(void)
{
    struct rte_mbuf *rx_pkts[TEST_PKT_NUM];
    u32 status, vlan_len, offload_type;
    u16 count;

    g_rxq->cons_idx = 0;
    g_rxq->rxq_stats.empty = 0;

    status = MAKE_CQE_STATUS(0, 4, 1);
    vlan_len = MAKE_CQE_VLAN_LEN(1024, 0);
    offload_type = MAKE_CQE_OFFLOAD(
        (IPSU_METADATA_FMT_NO_ENC << RQ_CQE_OFFOLAD_TYPE_PKT_FORMAT_SHIFT) |
        (IPSU_METADATA_L3_TP_IPV4 << RQ_CQE_OFFOLAD_TYPE_IP_TYPE_SHIFT) |
        (IPSU_PKT_TYPE_TCP << RQ_CQE_OFFOLAD_TYPE_PKT_TYPE_SHIFT),
        0, 0);

    fill_cqe_slot(0, status, vlan_len, offload_type, 0);

    count = hinic3_recv_pkts(g_rxq, rx_pkts, 1);

    RTE_TEST_ASSERT_EQUAL(count, 1, "should receive 1 LRO packet");
    RTE_TEST_ASSERT((rx_pkts[0]->ol_flags & HINIC3_PKT_RX_LRO) != 0,
                    "should have LRO flag");
    RTE_TEST_ASSERT_EQUAL(rx_pkts[0]->tso_segsz, 256,
                          "tso_segsz should be 1024/4=256");

    g_rx_cqe[0].status = 0;
    return TEST_SUCCESS;
}

/* ====== hinic3_recv_pkts: FC ptype ====== */

static int
test_recv_pkts_ptype_fc(void)
{
    struct rte_mbuf *rx_pkts[TEST_PKT_NUM];
    u32 status, vlan_len, offload_type;
    u16 count;

    g_rxq->cons_idx = 0;
    g_rxq->rxq_stats.empty = 0;

    status = MAKE_CQE_STATUS(0, 0, 1);
    vlan_len = MAKE_CQE_VLAN_LEN(64, 0);
    offload_type = MAKE_CQE_OFFLOAD(
        (IPSU_METADATA_FMT_FC << RQ_CQE_OFFOLAD_TYPE_PKT_FORMAT_SHIFT),
        0, 0);

    fill_cqe_slot(0, status, vlan_len, offload_type, 0);

    count = hinic3_recv_pkts(g_rxq, rx_pkts, 1);

    RTE_TEST_ASSERT_EQUAL(count, 1, "should receive 1 FC packet");
    RTE_TEST_ASSERT_EQUAL(rx_pkts[0]->packet_type, RTE_PTYPE_L2_ETHER_FCOE,
                          "FC ptype should be L2_ETHER_FCOE");

    g_rx_cqe[0].status = 0;
    return TEST_SUCCESS;
}

/* ====== hinic3_recv_pkts: nb_pkts limit ====== */

static int
test_recv_pkts_nb_pkts_limit(void)
{
    struct rte_mbuf *rx_pkts[2];
    u32 status, vlan_len, offload_type;
    u16 count;
    int i;

    g_rxq->cons_idx = 0;
    g_rxq->rxq_stats.empty = 0;

    for (i = 0; i < 5; i++) {
        status = MAKE_CQE_STATUS(0, 0, 1);
        vlan_len = MAKE_CQE_VLAN_LEN(64, 0);
        offload_type = MAKE_CQE_OFFLOAD(
            (IPSU_METADATA_FMT_NO_ENC << RQ_CQE_OFFOLAD_TYPE_PKT_FORMAT_SHIFT) |
            (IPSU_METADATA_L3_TP_IPV4 << RQ_CQE_OFFOLAD_TYPE_IP_TYPE_SHIFT) |
            (IPSU_PKT_TYPE_TCP << RQ_CQE_OFFOLAD_TYPE_PKT_TYPE_SHIFT),
            0, 0);
        fill_cqe_slot(i, status, vlan_len, offload_type, 0);
    }

    count = hinic3_recv_pkts(g_rxq, rx_pkts, 2);

    RTE_TEST_ASSERT_EQUAL(count, 2, "should receive exactly 2 packets");

    for (i = 0; i < 5; i++)
        g_rx_cqe[i].status = 0;
    return TEST_SUCCESS;
}

/* ====== hinic3_recv_pkts: stats update ====== */

static int
test_recv_pkts_stats_update(void)
{
    struct rte_mbuf *rx_pkts[TEST_PKT_NUM];
    u32 status, vlan_len, offload_type;
    u16 count;

    g_rxq->cons_idx = 0;
    g_rxq->rxq_stats.empty = 0;
    g_rxq->rxq_stats.packets = 0;
    g_rxq->rxq_stats.bytes = 0;

    status = MAKE_CQE_STATUS(0, 0, 1);
    vlan_len = MAKE_CQE_VLAN_LEN(100, 0);
    offload_type = MAKE_CQE_OFFLOAD(
        (IPSU_METADATA_FMT_NO_ENC << RQ_CQE_OFFOLAD_TYPE_PKT_FORMAT_SHIFT) |
        (IPSU_METADATA_L3_TP_IPV4 << RQ_CQE_OFFOLAD_TYPE_IP_TYPE_SHIFT) |
        (IPSU_PKT_TYPE_TCP << RQ_CQE_OFFOLAD_TYPE_PKT_TYPE_SHIFT),
        0, 0);

    fill_cqe_slot(0, status, vlan_len, offload_type, 0);

    count = hinic3_recv_pkts(g_rxq, rx_pkts, 1);

    RTE_TEST_ASSERT_EQUAL(count, 1, "should receive 1 packet");
    RTE_TEST_ASSERT_EQUAL(g_rxq->rxq_stats.packets, 1,
                          "stats.packets should increment");
    RTE_TEST_ASSERT_EQUAL(g_rxq->rxq_stats.bytes, 100,
                          "stats.bytes should be pkt_len");
    RTE_TEST_ASSERT_EQUAL(g_rxq->rxq_stats.empty, 0,
                          "stats.empty should be reset to 0");

    g_rx_cqe[0].status = 0;
    return TEST_SUCCESS;
}

/* ====== hinic3_init_rx_ptype_table tests ====== */

static int
test_init_rx_ptype_table_ipv4_tcp(void)
{
    struct hinic3_nic_dev nic_dev = {0};
    struct rte_eth_dev_data eth_data = {0};
    struct rte_eth_dev eth_dev = {0};

    nic_dev.feature_cap = NIC_F_TX_WQE_COMPACT_TASK;
    eth_data.dev_private = &nic_dev;
    eth_dev.data = &eth_data;

    RTE_TEST_ASSERT_EQUAL(hinic3_init_rx_ptype_table(&eth_dev), 0,
                          "init_ptype_table should succeed");

    u32 idx = (IPSU_METADATA_FMT_NO_ENC << RQ_CQE_OFFOLAD_TYPE_PKT_FORMAT_SHIFT) |
              (IPSU_METADATA_L3_TP_IPV4 << RQ_CQE_OFFOLAD_TYPE_IP_TYPE_SHIFT) |
              (IPSU_PKT_TYPE_TCP << RQ_CQE_OFFOLAD_TYPE_PKT_TYPE_SHIFT);

    RTE_TEST_ASSERT((nic_dev.ptype_tbl->ptype[idx] & RTE_PTYPE_L2_ETHER) != 0,
                    "ptype_tbl[IPv4+TCP] should have L2_ETHER");
    RTE_TEST_ASSERT((nic_dev.ptype_tbl->ptype[idx] & RTE_PTYPE_L3_IPV4_EXT_UNKNOWN) != 0,
                    "ptype_tbl[IPv4+TCP] should have L3_IPV4");
    RTE_TEST_ASSERT((nic_dev.ptype_tbl->ptype[idx] & RTE_PTYPE_L4_TCP) != 0,
                    "ptype_tbl[IPv4+TCP] should have L4_TCP");

    rte_free(nic_dev.ptype_tbl);
    return TEST_SUCCESS;
}

static int
test_init_rx_ptype_table_fc(void)
{
    struct hinic3_nic_dev nic_dev = {0};
    struct rte_eth_dev_data eth_data = {0};
    struct rte_eth_dev eth_dev = {0};

    nic_dev.feature_cap = NIC_F_TX_WQE_COMPACT_TASK;
    eth_data.dev_private = &nic_dev;
    eth_dev.data = &eth_data;

    RTE_TEST_ASSERT_EQUAL(hinic3_init_rx_ptype_table(&eth_dev), 0,
                          "init_ptype_table should succeed");

    u32 idx = IPSU_METADATA_FMT_FC << RQ_CQE_OFFOLAD_TYPE_PKT_FORMAT_SHIFT;

    RTE_TEST_ASSERT_EQUAL(nic_dev.ptype_tbl->ptype[idx], RTE_PTYPE_L2_ETHER_FCOE,
                          "ptype_tbl[FC] should be L2_ETHER_FCOE");

    rte_free(nic_dev.ptype_tbl);
    return TEST_SUCCESS;
}

/* ====== hinic3_rx_queue_list tests (public API) ====== */

static int
test_init_rx_queue_list(void)
{
    struct hinic3_nic_dev nic_dev = {0};
    hinic3_init_rx_queue_list(&nic_dev);
    RTE_TEST_ASSERT_EQUAL(nic_dev.num_rss, 0, "num_rss should be 0 after init");
    return TEST_SUCCESS;
}

static int
test_add_rq_to_rx_queue_list(void)
{
    struct hinic3_nic_dev nic_dev = {0};
    hinic3_init_rx_queue_list(&nic_dev);
    hinic3_add_rq_to_rx_queue_list(&nic_dev, 0);
    RTE_TEST_ASSERT_EQUAL(nic_dev.num_rss, 1, "num_rss should be 1");
    hinic3_add_rq_to_rx_queue_list(&nic_dev, 1);
    RTE_TEST_ASSERT_EQUAL(nic_dev.num_rss, 2, "num_rss should be 2");
    return TEST_SUCCESS;
}

static int
test_remove_rq_from_rx_queue_list(void)
{
    struct hinic3_nic_dev nic_dev = {0};
    hinic3_init_rx_queue_list(&nic_dev);
    hinic3_add_rq_to_rx_queue_list(&nic_dev, 0);
    hinic3_add_rq_to_rx_queue_list(&nic_dev, 1);
    hinic3_add_rq_to_rx_queue_list(&nic_dev, 2);
    hinic3_remove_rq_from_rx_queue_list(&nic_dev, 1);
    RTE_TEST_ASSERT_EQUAL(nic_dev.num_rss, 2, "num_rss should be 2 after remove");
    RTE_TEST_ASSERT_EQUAL(nic_dev.rx_queue_list[0], 0, "queue_list[0] should be 0");
    RTE_TEST_ASSERT_EQUAL(nic_dev.rx_queue_list[1], 2, "queue_list[1] should be 2");
    return TEST_SUCCESS;
}

static int
test_remove_rq_not_in_list(void)
{
    struct hinic3_nic_dev nic_dev = {0};
    hinic3_init_rx_queue_list(&nic_dev);
    hinic3_add_rq_to_rx_queue_list(&nic_dev, 0);
    hinic3_add_rq_to_rx_queue_list(&nic_dev, 2);
    hinic3_remove_rq_from_rx_queue_list(&nic_dev, 1);
    RTE_TEST_ASSERT_EQUAL(nic_dev.num_rss, 2, "num_rss should remain 2");
    return TEST_SUCCESS;
}

static int
test_add_remove_add_cycle(void)
{
    struct hinic3_nic_dev nic_dev = {0};
    hinic3_init_rx_queue_list(&nic_dev);
    hinic3_add_rq_to_rx_queue_list(&nic_dev, 0);
    hinic3_add_rq_to_rx_queue_list(&nic_dev, 1);
    hinic3_remove_rq_from_rx_queue_list(&nic_dev, 0);
    RTE_TEST_ASSERT_EQUAL(nic_dev.num_rss, 1, "num_rss should be 1");
    hinic3_add_rq_to_rx_queue_list(&nic_dev, 0);
    RTE_TEST_ASSERT_EQUAL(nic_dev.num_rss, 2, "num_rss should be 2 after re-add");
    return TEST_SUCCESS;
}

/* ================================================================
 * Section A: Additional hinic3_recv_pkts scenarios
 * ================================================================ */

/* ====== hinic3_recv_pkts: UDP checksum error ====== */

static int
test_recv_pkts_udp_csum_error(void)
{
    struct rte_mbuf *rx_pkts[TEST_PKT_NUM];
    u32 status, vlan_len, offload_type;
    u16 count;

    g_rxq->cons_idx = 0;
    g_rxq->rxq_stats.empty = 0;

    status = MAKE_CQE_STATUS(HINIC3_RX_CSUM_UDP_CSUM_ERR, 0, 1);
    vlan_len = MAKE_CQE_VLAN_LEN(64, 0);
    offload_type = MAKE_CQE_OFFLOAD(
        (IPSU_METADATA_FMT_NO_ENC << RQ_CQE_OFFOLAD_TYPE_PKT_FORMAT_SHIFT) |
        (IPSU_METADATA_L3_TP_IPV4 << RQ_CQE_OFFOLAD_TYPE_IP_TYPE_SHIFT) |
        (IPSU_PKT_TYPE_UDP << RQ_CQE_OFFOLAD_TYPE_PKT_TYPE_SHIFT),
        0, 0);

    fill_cqe_slot(0, status, vlan_len, offload_type, 0);

    count = hinic3_recv_pkts(g_rxq, rx_pkts, 1);

    RTE_TEST_ASSERT_EQUAL(count, 1, "should receive packet even with csum error");
    RTE_TEST_ASSERT((rx_pkts[0]->ol_flags & HINIC3_PKT_RX_L4_CKSUM_BAD) != 0,
                    "should have L4_CKSUM_BAD for UDP csum error");

    g_rx_cqe[0].status = 0;
    return TEST_SUCCESS;
}

/* ====== hinic3_recv_pkts: SCTP checksum error ====== */

static int
test_recv_pkts_sctp_csum_error(void)
{
    struct rte_mbuf *rx_pkts[TEST_PKT_NUM];
    u32 status, vlan_len, offload_type;
    u16 count;

    g_rxq->cons_idx = 0;
    g_rxq->rxq_stats.empty = 0;

    status = MAKE_CQE_STATUS(HINIC3_RX_CSUM_SCTP_CRC_ERR, 0, 1);
    vlan_len = MAKE_CQE_VLAN_LEN(64, 0);
    offload_type = MAKE_CQE_OFFLOAD(
        (IPSU_METADATA_FMT_NO_ENC << RQ_CQE_OFFOLAD_TYPE_PKT_FORMAT_SHIFT) |
        (IPSU_METADATA_L3_TP_IPV4 << RQ_CQE_OFFOLAD_TYPE_IP_TYPE_SHIFT) |
        (IPSU_PKT_TYPE_SCTP << RQ_CQE_OFFOLAD_TYPE_PKT_TYPE_SHIFT),
        0, 0);

    fill_cqe_slot(0, status, vlan_len, offload_type, 0);

    count = hinic3_recv_pkts(g_rxq, rx_pkts, 1);

    RTE_TEST_ASSERT_EQUAL(count, 1, "should receive packet even with csum error");
    RTE_TEST_ASSERT((rx_pkts[0]->ol_flags & HINIC3_PKT_RX_L4_CKSUM_BAD) != 0,
                    "should have L4_CKSUM_BAD for SCTP csum error");

    g_rx_cqe[0].status = 0;
    return TEST_SUCCESS;
}

/* ====== hinic3_recv_pkts: no RSS ====== */

static int
test_recv_pkts_no_rss(void)
{
    struct rte_mbuf *rx_pkts[TEST_PKT_NUM];
    u32 status, vlan_len, offload_type;
    u16 count;

    g_rxq->cons_idx = 0;
    g_rxq->rxq_stats.empty = 0;

    status = MAKE_CQE_STATUS(0, 0, 1);
    vlan_len = MAKE_CQE_VLAN_LEN(64, 0);
    offload_type = MAKE_CQE_OFFLOAD(
        (IPSU_METADATA_FMT_NO_ENC << RQ_CQE_OFFOLAD_TYPE_PKT_FORMAT_SHIFT) |
        (IPSU_METADATA_L3_TP_IPV4 << RQ_CQE_OFFOLAD_TYPE_IP_TYPE_SHIFT) |
        (IPSU_PKT_TYPE_TCP << RQ_CQE_OFFOLAD_TYPE_PKT_TYPE_SHIFT),
        0, 0);

    fill_cqe_slot(0, status, vlan_len, offload_type, 0);

    count = hinic3_recv_pkts(g_rxq, rx_pkts, 1);

    RTE_TEST_ASSERT_EQUAL(count, 1, "should receive 1 packet");
    RTE_TEST_ASSERT((rx_pkts[0]->ol_flags & HINIC3_PKT_RX_RSS_HASH) == 0,
                    "should NOT have RSS_HASH flag when rss_type=0");

    g_rx_cqe[0].status = 0;
    return TEST_SUCCESS;
}

/* ====== hinic3_recv_pkts: checksum disabled ====== */

static int
test_recv_pkts_csum_disabled(void)
{
    struct rte_mbuf *rx_pkts[TEST_PKT_NUM];
    u32 status, vlan_len, offload_type;
    u16 count;

    g_rxq->cons_idx = 0;
    g_rxq->rxq_stats.empty = 0;
    g_nic_dev->rx_csum_en = 0;

    status = MAKE_CQE_STATUS(0, 0, 1);
    vlan_len = MAKE_CQE_VLAN_LEN(64, 0);
    offload_type = MAKE_CQE_OFFLOAD(
        (IPSU_METADATA_FMT_NO_ENC << RQ_CQE_OFFOLAD_TYPE_PKT_FORMAT_SHIFT) |
        (IPSU_METADATA_L3_TP_IPV4 << RQ_CQE_OFFOLAD_TYPE_IP_TYPE_SHIFT) |
        (IPSU_PKT_TYPE_TCP << RQ_CQE_OFFOLAD_TYPE_PKT_TYPE_SHIFT),
        0, 0);

    fill_cqe_slot(0, status, vlan_len, offload_type, 0);

    count = hinic3_recv_pkts(g_rxq, rx_pkts, 1);

    RTE_TEST_ASSERT_EQUAL(count, 1, "should receive 1 packet");
    RTE_TEST_ASSERT((rx_pkts[0]->ol_flags & (HINIC3_PKT_RX_IP_CKSUM_BAD |
                                             HINIC3_PKT_RX_IP_CKSUM_GOOD)) == 0,
                    "should have IP_CKSUM_UNKNOWN when csum disabled");

    g_rx_cqe[0].status = 0;
    return TEST_SUCCESS;
}

/* ====== hinic3_recv_pkts: VXLAN tunnel ptype ====== */

static int
test_recv_pkts_tunnel_vxlan(void)
{
    struct rte_mbuf *rx_pkts[TEST_PKT_NUM];
    u32 status, vlan_len, offload_type;
    u16 count;

    g_rxq->cons_idx = 0;
    g_rxq->rxq_stats.empty = 0;

    status = MAKE_CQE_STATUS(0, 0, 1);
    vlan_len = MAKE_CQE_VLAN_LEN(64, 0);
    offload_type = MAKE_CQE_OFFLOAD(
        (IPSU_METADATA_FMT_VXLAN << RQ_CQE_OFFOLAD_TYPE_PKT_FORMAT_SHIFT) |
        (IPSU_METADATA_L3_TP_IPV4 << RQ_CQE_OFFOLAD_TYPE_ENC_L3_TYPE_SHIFT) |
        (IPSU_METADATA_L3_TP_IPV4 << RQ_CQE_OFFOLAD_TYPE_IP_TYPE_SHIFT) |
        (IPSU_PKT_TYPE_TCP << RQ_CQE_OFFOLAD_TYPE_PKT_TYPE_SHIFT),
        0, 0);

    fill_cqe_slot(0, status, vlan_len, offload_type, 0);

    count = hinic3_recv_pkts(g_rxq, rx_pkts, 1);

    RTE_TEST_ASSERT_EQUAL(count, 1, "should receive 1 packet");
    RTE_TEST_ASSERT((rx_pkts[0]->packet_type & RTE_PTYPE_TUNNEL_VXLAN) != 0,
                    "should have TUNNEL_VXLAN ptype");
    RTE_TEST_ASSERT((rx_pkts[0]->packet_type & RTE_PTYPE_L2_ETHER) != 0,
                    "should have L2_ETHER ptype");
    RTE_TEST_ASSERT((rx_pkts[0]->packet_type & RTE_PTYPE_L4_UDP) != 0,
                    "should have L4_UDP (outer) ptype for VXLAN");

    g_rx_cqe[0].status = 0;
    return TEST_SUCCESS;
}

/* ====== hinic3_recv_pkts: jumbo frame (scattered) ====== */

static int
test_recv_pkts_jumbo(void)
{
    struct rte_mbuf *rx_pkts[TEST_PKT_NUM];
    u32 status, vlan_len, offload_type;
    u16 count;

    g_rxq->cons_idx = 0;
    g_rxq->rxq_stats.empty = 0;
    g_rxq->is_scattered_rx = 1;
    rte_eth_devices[0].data->scattered_rx = 1;

    status = MAKE_CQE_STATUS(0, 0, 1);
    vlan_len = MAKE_CQE_VLAN_LEN(3000, 0);
    offload_type = MAKE_CQE_OFFLOAD(
        (IPSU_METADATA_FMT_NO_ENC << RQ_CQE_OFFOLAD_TYPE_PKT_FORMAT_SHIFT) |
        (IPSU_METADATA_L3_TP_IPV4 << RQ_CQE_OFFOLAD_TYPE_IP_TYPE_SHIFT) |
        (IPSU_PKT_TYPE_TCP << RQ_CQE_OFFOLAD_TYPE_PKT_TYPE_SHIFT),
        0, 0);

    fill_cqe_slot(0, status, vlan_len, offload_type, 0);

    count = hinic3_recv_pkts(g_rxq, rx_pkts, TEST_PKT_NUM);

    RTE_TEST_ASSERT_EQUAL(count, 1, "should receive 1 jumbo packet");
    RTE_TEST_ASSERT_EQUAL(rx_pkts[0]->pkt_len, 3000, "pkt_len should be 3000");
    RTE_TEST_ASSERT_EQUAL(rx_pkts[0]->data_len, 2048,
                          "first seg data_len should be buf_len");
    RTE_TEST_ASSERT_EQUAL(rx_pkts[0]->nb_segs, 2,
                          "should have 2 segments");
    RTE_TEST_ASSERT(rx_pkts[0]->next != NULL, "should have next segment");
    RTE_TEST_ASSERT_EQUAL(rx_pkts[0]->next->data_len, 952,
                          "second seg data_len should be 3000-2048=952");

    rte_pktmbuf_free(rx_pkts[0]);
    g_rx_info[0].mbuf = NULL;
    g_rx_info[1].mbuf = NULL;
    g_rx_cqe[0].status = 0;

    return TEST_SUCCESS;
}


/* ================================================================
 * Section B: hinic3_free_rxq_mbufs
 * ================================================================ */

static int
test_free_rxq_mbufs_all(void)
{
    int i;

    hinic3_free_rxq_mbufs(g_rxq);

    for (i = 0; i < TEST_RXQ_DEPTH - 1; i++) {
        RTE_TEST_ASSERT_EQUAL(g_rx_info[i].mbuf, NULL,
                              "slot %d mbuf should be NULL after free", i);
    }
    RTE_TEST_ASSERT(g_rx_info[TEST_RXQ_DEPTH - 1].mbuf != NULL,
                    "last slot mbuf should still exist");

    return TEST_SUCCESS;
}

static int
test_free_rxq_mbufs_empty(void)
{
    int i;

    g_rxq->delta = TEST_RXQ_DEPTH;

    hinic3_free_rxq_mbufs(g_rxq);

    for (i = 0; i < TEST_RXQ_DEPTH; i++) {
        RTE_TEST_ASSERT(g_rx_info[i].mbuf != NULL,
                        "slot %d mbuf should still exist", i);
    }

    return TEST_SUCCESS;
}

/* ================================================================
 * Section C: hinic3_free_all_rxq_mbufs
 * ================================================================ */

static int
rx_free_all_setup(void)
{
    int i;

    if (rx_recv_setup() != TEST_SUCCESS)
        return TEST_FAILED;

    g_rx_info2 = rte_zmalloc("test_rx_info2",
                              TEST_RXQ_DEPTH * sizeof(*g_rx_info2), 0);
    if (!g_rx_info2)
        return TEST_FAILED;
    for (i = 0; i < TEST_RXQ_DEPTH; i++) {
        g_rx_info2[i].mbuf = rte_pktmbuf_alloc(g_mp);
        if (!g_rx_info2[i].mbuf)
            return TEST_FAILED;
    }

    g_rx_cqe2 = rte_zmalloc_socket("test_rx_cqe2",
                                     TEST_RXQ_DEPTH * sizeof(*g_rx_cqe2),
                                     RTE_CACHE_LINE_SIZE, SOCKET_ID_ANY);
    if (!g_rx_cqe2)
        return TEST_FAILED;
    for (i = 0; i < TEST_RXQ_DEPTH; i++)
        g_rx_cqe2[i].status = 0;

    g_rxq2 = rte_zmalloc("test_rxq2", sizeof(*g_rxq2), 0);
    if (!g_rxq2)
        return TEST_FAILED;

    g_rxq2->nic_dev = g_nic_dev;
    g_rxq2->rx_info = g_rx_info2;
    g_rxq2->rx_cqe = g_rx_cqe2;
    g_rxq2->mb_pool = g_mp;
    g_rxq2->port_id = 0;
    g_rxq2->q_id = 1;
    g_rxq2->local_qid = 1;
    g_rxq2->q_depth = TEST_RXQ_DEPTH;
    g_rxq2->q_mask = TEST_RXQ_MASK;
    g_rxq2->buf_len = 2048;
    g_rxq2->cons_idx = 0;
    g_rxq2->prod_idx = TEST_RXQ_DEPTH;
    g_rxq2->delta = 1;
    g_rxq2->rx_free_thresh = 32;
    g_rxq2->wqe_type = HINIC3_EXTEND_RQ_WQE;
    g_rxq2->is_scattered_rx = 0;
    g_rxq2->is_hairpin = false;

    g_rxqs_arr = rte_zmalloc("test_rxqs", 2 * sizeof(*g_rxqs_arr), 0);
    if (!g_rxqs_arr)
        return TEST_FAILED;
    g_rxqs_arr[0] = g_rxq;
    g_rxqs_arr[1] = g_rxq2;
    g_nic_dev->rxqs = g_rxqs_arr;
    g_nic_dev->num_rqs = 2;

    return TEST_SUCCESS;
}

static void
rx_free_all_teardown(void)
{
    int i;

    if (g_rx_info2) {
        for (i = 0; i < TEST_RXQ_DEPTH; i++) {
            if (g_rx_info2[i].mbuf)
                rte_pktmbuf_free(g_rx_info2[i].mbuf);
        }
        rte_free(g_rx_info2);
    }
    if (g_rx_cqe2)
        rte_free(g_rx_cqe2);
    if (g_rxq2)
        rte_free(g_rxq2);
    if (g_rxqs_arr)
        rte_free(g_rxqs_arr);

    g_rx_info2 = NULL;
    g_rx_cqe2 = NULL;
    g_rxq2 = NULL;
    g_rxqs_arr = NULL;

    if (g_nic_dev) {
        g_nic_dev->rxqs = NULL;
        g_nic_dev->num_rqs = 0;
    }

    rx_recv_teardown();
}

static int
test_free_all_rxq_mbufs(void)
{
    int i;

    RTE_TEST_ASSERT_EQUAL(g_nic_dev->num_rqs, 2, "should have 2 queues");

    hinic3_free_all_rxq_mbufs(g_nic_dev);

    for (i = 0; i < TEST_RXQ_DEPTH - 1; i++) {
        RTE_TEST_ASSERT_EQUAL(g_rx_info[i].mbuf, NULL,
                              "queue 0 slot %d mbuf should be NULL", i);
    }
    RTE_TEST_ASSERT(g_rx_info[TEST_RXQ_DEPTH - 1].mbuf != NULL,
                    "queue 0 last slot mbuf should still exist");

    for (i = 0; i < TEST_RXQ_DEPTH - 1; i++) {
        RTE_TEST_ASSERT_EQUAL(g_rx_info2[i].mbuf, NULL,
                              "queue 1 slot %d mbuf should be NULL", i);
    }
    RTE_TEST_ASSERT(g_rx_info2[TEST_RXQ_DEPTH - 1].mbuf != NULL,
                    "queue 1 last slot mbuf should still exist");

    return TEST_SUCCESS;
}

/* ================================================================
 * Section D: hinic3_refill_indir_rqid
 * ================================================================ */

static int
test_refill_indir_normal(void)
{
    int ret;

    g_nic_dev->hwdev = (struct hinic3_hwdev *)g_nic_dev;
    g_nic_dev->num_rss = 2;
    g_nic_dev->rx_queue_list[0] = 0;
    g_nic_dev->rx_queue_list[1] = 1;
    g_qinfo_type = HINIC3_QINFO_TYPE_NORMAL;
    g_mock_rss_set_indir_tbl_ret = 0;

    ret = hinic3_refill_indir_rqid(g_rxq);

    RTE_TEST_ASSERT_EQUAL(ret, 0, "refill_indir should succeed in normal mode");

    g_qinfo_type = HINIC3_QINFO_TYPE_NORMAL;
    return TEST_SUCCESS;
}

static int
test_refill_indir_qpool(void)
{
    int ret;

    g_nic_dev->hwdev = (struct hinic3_hwdev *)g_nic_dev;
    g_nic_dev->num_rss = 2;
    g_nic_dev->rx_queue_list[0] = 0;
    g_nic_dev->rx_queue_list[1] = 1;
    g_qinfo_type = HINIC3_QINFO_TYPE_QPOOL;
    g_mock_rss_set_indir_tbl_qpool_ret = 0;

    ret = hinic3_refill_indir_rqid(g_rxq);

    RTE_TEST_ASSERT_EQUAL(ret, 0, "refill_indir should succeed in QPOOL mode");

    g_qinfo_type = HINIC3_QINFO_TYPE_NORMAL;
    return TEST_SUCCESS;
}

static int
test_refill_indir_fail(void)
{
    int ret;

    g_nic_dev->hwdev = (struct hinic3_hwdev *)g_nic_dev;
    g_nic_dev->num_rss = 2;
    g_nic_dev->rx_queue_list[0] = 0;
    g_nic_dev->rx_queue_list[1] = 1;
    g_qinfo_type = HINIC3_QINFO_TYPE_NORMAL;
    g_mock_rss_set_indir_tbl_ret = -1;

    ret = hinic3_refill_indir_rqid(g_rxq);

    RTE_TEST_ASSERT(ret != 0, "refill_indir should propagate error");

    g_mock_rss_set_indir_tbl_ret = 0;
    return TEST_SUCCESS;
}

static int
test_refill_indir_empty(void)
{
    int ret;

    g_nic_dev->hwdev = (struct hinic3_hwdev *)g_nic_dev;
    g_nic_dev->num_rss = 0;
    g_qinfo_type = HINIC3_QINFO_TYPE_NORMAL;
    g_mock_rss_set_indir_tbl_ret = 0;

    ret = hinic3_refill_indir_rqid(g_rxq);

    RTE_TEST_ASSERT_EQUAL(ret, 0, "refill_indir should succeed with empty list");

    return TEST_SUCCESS;
}


/* ================================================================
 * Section E: hinic3_recv_pkts_compact_cqe
 * ================================================================ */

static int
rx_compact_setup(void)
{
    int ret = rx_recv_setup();
    if (ret != TEST_SUCCESS)
        return ret;

    if (g_nic_dev->ptype_tbl) {
        rte_free(g_nic_dev->ptype_tbl);
        g_nic_dev->ptype_tbl = NULL;
    }
    g_nic_dev->feature_cap = NIC_F_TX_WQE_COMPACT_TASK |
                             NIC_F_RX_HW_COMPACT_CQE;
    if (hinic3_init_rx_ptype_table(&rte_eth_devices[0]) != 0)
        return TEST_FAILED;

    g_rxq->wqe_type = HINIC3_COMPACT_RQ_WQE;

    g_rq_ci = rte_zmalloc_socket("test_rq_ci", sizeof(*g_rq_ci),
                                  RTE_CACHE_LINE_SIZE, SOCKET_ID_ANY);
    if (!g_rq_ci)
        return TEST_FAILED;
    g_rxq->rq_ci = g_rq_ci;
    g_rxq->hw_cons_idx = 0;
    g_rxq->prefetch_flag = false;

    return TEST_SUCCESS;
}

static void
rx_compact_teardown(void)
{
    if (g_rq_ci) {
        rte_free(g_rq_ci);
        g_rq_ci = NULL;
    }
    rx_recv_teardown();
}

static void
fill_compact_cqe(struct rte_mbuf *mbuf, u32 dw0, u32 dw1, u32 dw2)
{
    volatile struct hinic3_rq_cqe *cqe =
        (volatile struct hinic3_rq_cqe *)
        ((char *)mbuf->buf_addr + RTE_PKTMBUF_HEADROOM);

    cqe->status = cpu_to_be32(dw0);
    cqe->vlan_len = cpu_to_be32(dw1);
    cqe->offload_type = cpu_to_be32(dw2);
    cqe->hash_val = 0;
    cqe->mark_id_0 = 0;
    cqe->mark_id_1 = 0;
    cqe->mark_id_2 = 0;
    cqe->pkt_info = 0;
}

static void
set_hw_ci(u16 hw_ci)
{
    struct hinic3_rq_ci_wb ci;

    ci.dw1.bs.cqe_num = 0;
    ci.dw1.bs.hw_ci = hw_ci;
    g_rxq->rq_ci->dw1.value = hinic3_hw_be32(ci.dw1.value);
}

static int
test_recv_compact_empty(void)
{
    struct rte_mbuf *rx_pkts[TEST_PKT_NUM];
    u16 count;

    g_rxq->cons_idx = 0;
    g_rxq->hw_cons_idx = 0;
    g_rxq->prefetch_flag = false;
    g_rxq->rxq_stats.empty = 0;

    set_hw_ci(0);

    count = hinic3_recv_pkts_compact_cqe(g_rxq, rx_pkts, TEST_PKT_NUM);

    RTE_TEST_ASSERT_EQUAL(count, 0, "empty compact queue should return 0 pkts");
    return TEST_SUCCESS;
}

static int
test_recv_compact_single_pkt(void)
{
    struct rte_mbuf *rx_pkts[TEST_PKT_NUM];
    u32 dw0, dw2;
    u16 count;

    g_rxq->cons_idx = 0;
    g_rxq->hw_cons_idx = 0;
    g_rxq->prefetch_flag = false;
    g_rxq->rxq_stats.empty = 0;
    g_rxq->delta = 1;

    dw0 = MAKE_COMPACT_CQE_DW0(
        1,
        HINIC3_RQ_CQE_INTEGRATE,
        0,
        IPSU_METADATA_FMT_NO_ENC,
        IPSU_METADATA_L3_TP_IPV4,
        HINIC3_RQ_COMPACT_CQE_16BYTE,
        HINIC3_RX_COMPACT_CSUM_NO_ERROR,
        IPSU_PKT_TYPE_TCP,
        128);
    dw2 = MAKE_COMPACT_CQE_DW2(0, 0);

    fill_compact_cqe(g_rx_info[0].mbuf, dw0, 0, dw2);
    set_hw_ci(1);

    count = hinic3_recv_pkts_compact_cqe(g_rxq, rx_pkts, TEST_PKT_NUM);

    RTE_TEST_ASSERT_EQUAL(count, 1, "should receive 1 packet");
    RTE_TEST_ASSERT_EQUAL(rx_pkts[0]->pkt_len, 128, "pkt_len should be 128");
    RTE_TEST_ASSERT_EQUAL(rx_pkts[0]->data_len, 128, "data_len should be 128");
    RTE_TEST_ASSERT_EQUAL(rx_pkts[0]->port, 0, "port should be 0");

    RTE_TEST_ASSERT((rx_pkts[0]->ol_flags & HINIC3_PKT_RX_IP_CKSUM_GOOD) != 0,
                    "should have IP_CKSUM_GOOD");
    RTE_TEST_ASSERT((rx_pkts[0]->ol_flags & HINIC3_PKT_RX_L4_CKSUM_GOOD) != 0,
                    "should have L4_CKSUM_GOOD");

    RTE_TEST_ASSERT((rx_pkts[0]->packet_type & RTE_PTYPE_L3_IPV4_EXT_UNKNOWN) != 0,
                    "should have L3_IPV4");
    RTE_TEST_ASSERT((rx_pkts[0]->packet_type & RTE_PTYPE_L4_TCP) != 0,
                    "should have L4_TCP");

    return TEST_SUCCESS;
}

static int
test_recv_compact_csum_error(void)
{
    struct rte_mbuf *rx_pkts[TEST_PKT_NUM];
    u32 dw0, dw2;
    u16 count;

    g_rxq->cons_idx = 0;
    g_rxq->hw_cons_idx = 0;
    g_rxq->prefetch_flag = false;
    g_rxq->rxq_stats.empty = 0;
    g_rxq->delta = 1;

    dw0 = MAKE_COMPACT_CQE_DW0(
        1,
        HINIC3_RQ_CQE_INTEGRATE,
        0,
        IPSU_METADATA_FMT_NO_ENC,
        IPSU_METADATA_L3_TP_IPV4,
        HINIC3_RQ_COMPACT_CQE_16BYTE,
        HINIC3_RX_COMPACT_L3_L4_CSUM_ERROR,
        IPSU_PKT_TYPE_TCP,
        64);
    dw2 = MAKE_COMPACT_CQE_DW2(0, 0);

    fill_compact_cqe(g_rx_info[0].mbuf, dw0, 0, dw2);
    set_hw_ci(1);

    count = hinic3_recv_pkts_compact_cqe(g_rxq, rx_pkts, TEST_PKT_NUM);

    RTE_TEST_ASSERT_EQUAL(count, 1, "should receive packet even with csum error");
    RTE_TEST_ASSERT((rx_pkts[0]->ol_flags & HINIC3_PKT_RX_IP_CKSUM_BAD) != 0,
                    "should have IP_CKSUM_BAD for compact csum error");

    return TEST_SUCCESS;
}

static int
test_recv_compact_vlan(void)
{
    struct rte_mbuf *rx_pkts[TEST_PKT_NUM];
    u32 dw0, dw2;
    u16 count;

    g_rxq->cons_idx = 0;
    g_rxq->hw_cons_idx = 0;
    g_rxq->prefetch_flag = false;
    g_rxq->rxq_stats.empty = 0;
    g_rxq->delta = 1;

    dw0 = MAKE_COMPACT_CQE_DW0(
        1,
        HINIC3_RQ_CQE_INTEGRATE,
        1,
        IPSU_METADATA_FMT_NO_ENC,
        IPSU_METADATA_L3_TP_IPV4,
        HINIC3_RQ_COMPACT_CQE_16BYTE,
        HINIC3_RX_COMPACT_CSUM_NO_ERROR,
        IPSU_PKT_TYPE_TCP,
        64);
    dw2 = MAKE_COMPACT_CQE_DW2(0, 200);

    fill_compact_cqe(g_rx_info[0].mbuf, dw0, 0, dw2);
    set_hw_ci(1);

    count = hinic3_recv_pkts_compact_cqe(g_rxq, rx_pkts, TEST_PKT_NUM);

    RTE_TEST_ASSERT_EQUAL(count, 1, "should receive 1 packet");
    RTE_TEST_ASSERT_EQUAL(rx_pkts[0]->vlan_tci, 200, "vlan_tci should be 200");
    RTE_TEST_ASSERT((rx_pkts[0]->ol_flags & HINIC3_PKT_RX_VLAN) != 0,
                    "should have RX_VLAN flag");
    RTE_TEST_ASSERT((rx_pkts[0]->ol_flags & HINIC3_PKT_RX_VLAN_STRIPPED) != 0,
                    "should have RX_VLAN_STRIPPED flag");

    return TEST_SUCCESS;
}


static struct unit_test_suite hinic3_rx_test_suite = {
    .suite_name = "HINIC3 RX Unit Tests",
    .setup = rx_suite_setup,
    .teardown = rx_suite_teardown,
    .unit_test_cases = {
        /* hinic3_recv_pkts tests */
        TEST_CASE_ST(rx_recv_setup, rx_recv_teardown, test_recv_pkts_empty_queue),
        TEST_CASE_ST(rx_recv_setup, rx_recv_teardown, test_recv_pkts_single_pkt),
        TEST_CASE_ST(rx_recv_setup, rx_recv_teardown, test_recv_pkts_multiple_pkts),
        TEST_CASE_ST(rx_recv_setup, rx_recv_teardown, test_recv_pkts_vlan_offload),
        TEST_CASE_ST(rx_recv_setup, rx_recv_teardown, test_recv_pkts_csum_ip_error),
        TEST_CASE_ST(rx_recv_setup, rx_recv_teardown, test_recv_pkts_csum_tcp_error),
        TEST_CASE_ST(rx_recv_setup, rx_recv_teardown, test_recv_pkts_lro),
        TEST_CASE_ST(rx_recv_setup, rx_recv_teardown, test_recv_pkts_ptype_fc),
        TEST_CASE_ST(rx_recv_setup, rx_recv_teardown, test_recv_pkts_nb_pkts_limit),
        TEST_CASE_ST(rx_recv_setup, rx_recv_teardown, test_recv_pkts_stats_update),

        /* hinic3_init_rx_ptype_table */
        TEST_CASE(test_init_rx_ptype_table_ipv4_tcp),
        TEST_CASE(test_init_rx_ptype_table_fc),

        /* hinic3_rx_queue_list */
        TEST_CASE(test_init_rx_queue_list),
        TEST_CASE(test_add_rq_to_rx_queue_list),
        TEST_CASE(test_remove_rq_from_rx_queue_list),
        TEST_CASE(test_remove_rq_not_in_list),
        TEST_CASE(test_add_remove_add_cycle),

        /* Section A: Additional hinic3_recv_pkts scenarios */
        TEST_CASE_ST(rx_recv_setup, rx_recv_teardown, test_recv_pkts_udp_csum_error),
        TEST_CASE_ST(rx_recv_setup, rx_recv_teardown, test_recv_pkts_sctp_csum_error),
        TEST_CASE_ST(rx_recv_setup, rx_recv_teardown, test_recv_pkts_no_rss),
        TEST_CASE_ST(rx_recv_setup, rx_recv_teardown, test_recv_pkts_csum_disabled),
        TEST_CASE_ST(rx_recv_setup, rx_recv_teardown, test_recv_pkts_tunnel_vxlan),
        TEST_CASE_ST(rx_recv_setup, rx_recv_teardown, test_recv_pkts_jumbo),

        /* Section B: hinic3_free_rxq_mbufs */
        TEST_CASE_ST(rx_recv_setup, rx_recv_teardown, test_free_rxq_mbufs_all),
        TEST_CASE_ST(rx_recv_setup, rx_recv_teardown, test_free_rxq_mbufs_empty),

        /* Section C: hinic3_free_all_rxq_mbufs */
        TEST_CASE_ST(rx_free_all_setup, rx_free_all_teardown, test_free_all_rxq_mbufs),

        /* Section D: hinic3_refill_indir_rqid */
        TEST_CASE_ST(rx_recv_setup, rx_recv_teardown, test_refill_indir_normal),
        TEST_CASE_ST(rx_recv_setup, rx_recv_teardown, test_refill_indir_qpool),
        TEST_CASE_ST(rx_recv_setup, rx_recv_teardown, test_refill_indir_fail),
        TEST_CASE_ST(rx_recv_setup, rx_recv_teardown, test_refill_indir_empty),

        /* Section E: hinic3_recv_pkts_compact_cqe */
        TEST_CASE_ST(rx_compact_setup, rx_compact_teardown, test_recv_compact_empty),
        TEST_CASE_ST(rx_compact_setup, rx_compact_teardown, test_recv_compact_single_pkt),
        TEST_CASE_ST(rx_compact_setup, rx_compact_teardown, test_recv_compact_csum_error),
        TEST_CASE_ST(rx_compact_setup, rx_compact_teardown, test_recv_compact_vlan),


        TEST_CASES_END()
    }
};

static int
test_hinic3_rx(void)
{
    return unit_test_suite_runner(&hinic3_rx_test_suite);
}

REGISTER_TEST_COMMAND(hinic3_rx_autotest, test_hinic3_rx);
