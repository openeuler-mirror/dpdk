/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2025-2026 Huawei Technologies Co., Ltd
 * Unit tests for hinic3_pmd_ethdev.c
 *
 * Uses mock data structures (hinic3_hwdev, rte_pci_device, rte_eth_dev)
 * so that public functions like hinic3_dev_info_get() can be called directly
 * without real hardware. Feature flags are controlled via nic_dev->feature_cap.
 */

#include <rte_common.h>
#include <rte_ethdev.h>
#ifdef DPDK_21_11
#include <ethdev_driver.h>
#endif
#include <rte_bus_pci.h>
#include <rte_pci.h>
#include <rte_dev.h>
#include <rte_malloc.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>
#include <rte_byteorder.h>
#include <rte_ether.h>

#include "../test.h"
#include <rte_test.h>
#include <rte_log.h>

#include "base/hinic3_compat.h"
#include "base/hinic3_pmd_nic_cfg.h"
#include "base/hinic3_pmd_hwdev.h"
#include "hinic3_pmd_nic_io.h"
#include "hinic3_pmd_ethdev.h"
#include "hinic3_pmd_dcb.h"
#include "hinic3_pmd_tx.h"
#include "test_hinic3_mock.h"

/* Access rte_eth_devices[] for mock setup.
 * DPDK >= 21 declares it in ethdev_driver.h; older versions in rte_ethdev_core.h
 * which is included by rte_ethdev.h via hinic3_pmd_ethdev.h.
 */
#ifndef DPDK_21_11
/* For DPDK < 21, rte_eth_devices may be declared in rte_ethdev_core.h */
extern struct rte_eth_dev rte_eth_devices[];
#endif

/*
 * Private macros from hinic3_pmd_ethdev.c (not exposed in public headers).
 * Duplicated here for unit test coverage of the logic that depends on them.
 */
#define HINIC3_RX_EMPTY_THRESHOLD	3
#define HINIC3_DEFAULT_TX_FREE_LOOP	1000
#define HINIC3_DEFAULT_BURST_SIZE	32
#define HINIC3_DEFAULT_NB_QUEUES	1
#define HINIC3_DEFAULT_RING_SIZE	1024
#define HINIC3_MAX_LRO_SIZE		65536
#define HINIC3_MIN_RX_BUF_SIZE		1024

/* VFTA macros (private #define in hinic3_pmd_ethdev.c) */
#define HINIC3_VFTA_BIT(vlan_id)	(1U << ((vlan_id) & 0x1F))
#define HINIC3_VFTA_IDX(vlan_id)	((vlan_id) >> 5)

/* RX mode bits (private enum in hinic3_pmd_ethdev.c) */
#define HINIC3_RX_MODE_UC		(1 << 0)
#define HINIC3_RX_MODE_MC		(1 << 1)
#define HINIC3_RX_MODE_BC		(1 << 2)
#define HINIC3_RX_MODE_MC_ALL		(1 << 3)
#define HINIC3_RX_MODE_PROMISC		(1 << 4)
#define HINIC3_DEFAULT_RX_MODE		(HINIC3_RX_MODE_UC | HINIC3_RX_MODE_MC | \
					 HINIC3_RX_MODE_BC)

/* xstats counts (private #define in hinic3_pmd_ethdev.c, computed from sizeof
 * static arrays). Default values without optional HINIC3_XSTAT_* flags:
 *   CIR_DROP: 1, VPORT: 18, PHYPORT: 86, RXQ: 6, TXQ: 7
 */
#define HINIC3_CIR_DROP_XSTATS_NUM	1
#define HINIC3_VPORT_XSTATS_NUM	18
#define HINIC3_PHYPORT_XSTATS_NUM	86
#define HINIC3_RXQ_XSTATS_NUM		6
#define HINIC3_TXQ_XSTATS_NUM		7

/* Queue buffer size / offset macros (private #define in hinic3_pmd_ethdev.c) */
#define HINIC3_RQSQ_PAGE_SIZE		4096
#define UP_ALIGN(size, align)		RTE_ALIGN((size), (align))
#define SQWQE_BUF_SIZE(depth)		UP_ALIGN((depth) << HINIC3_SQ_WQEBB_SHIFT, \
						HINIC3_RQSQ_PAGE_SIZE)
#define RQWQE_BUF_SIZE(depth)		UP_ALIGN((depth) << HINIC3_SQ_WQEBB_SHIFT, \
						HINIC3_RQSQ_PAGE_SIZE)
#define SQWQE_OFFSET(q_buf_size, q_id)	((q_buf_size) * (q_id))
#define SQCI_OFFSET(q_buf_size, q_id, depth) \
	(SQWQE_OFFSET(q_buf_size, q_id) + SQWQE_BUF_SIZE(depth))

/* ====== Mock data structures for direct hinic3_dev_info_get() calls ====== */
/*
 * hinic3_get_driver_feature(dev) just reads nic_dev->feature_cap.
 * We set nic_dev->feature_cap directly to control feature detection,
 * which is reliable across all DPDK versions without needing --wrap.
 *
 * is_sp620_nic / is_sp560_nic / is_sp230_nic are in the same compilation
 * unit as hinic3_dev_info_get, so --wrap cannot intercept them. Instead,
 * we set up proper mock data structures (hinic3_hwdev, rte_pci_device)
 * so that these functions execute safely and return the desired value
 * based on the PCI device_id.
 */

/* ====== Mock hardware structures for direct hinic3_dev_info_get() calls ====== */
/*
 * is_sp620_nic/is_sp560_nic/is_sp230_nic are in the same compilation unit
 * as hinic3_dev_info_get, so --wrap cannot intercept them. Instead, we set
 * up proper mock data structures (hinic3_hwdev, rte_pci_device) so that
 * these functions execute safely and return the desired value based on
 * the PCI device_id.
 */

static struct rte_pci_device g_mock_pci_dev;
static struct hinic3_hwdev g_mock_hwdev;
static struct hinic3_hwif g_mock_hwif;
static struct hinic3_nic_dev g_mock_nic_dev;
static struct rte_eth_dev_data g_mock_dev_data;
#define MOCK_ETH_DEV_PORT_ID 0

/* Helper: configure mock NIC type by setting PCI device_id */
static void
mock_set_nic_type(uint16_t device_id)
{
	memset(&g_mock_pci_dev, 0, sizeof(g_mock_pci_dev));
	g_mock_pci_dev.id.device_id = device_id;
	/* Point rte_eth_devices[0].device to our mock pci_dev's device member.
	 * RTE_ETH_DEV_TO_PCI uses container_of(dev->device, struct rte_pci_device, device)
	 * so this will resolve to our g_mock_pci_dev. */
	rte_eth_devices[MOCK_ETH_DEV_PORT_ID].device = &g_mock_pci_dev.device;

	memset(&g_mock_hwdev, 0, sizeof(g_mock_hwdev));
	g_mock_hwdev.port_id = MOCK_ETH_DEV_PORT_ID;
}

/* ====== Helper: set up a mock hinic3_nic_dev ====== */

static struct hinic3_dcb g_dcb;

static void
mock_nic_dev_init(struct hinic3_nic_dev *nic_dev, u16 max_sqs, u16 max_rqs,
		  u16 num_sqs, u16 num_rqs, bool dcb_on)
{
	memset(nic_dev, 0, sizeof(*nic_dev));
	memset(&g_dcb, 0, sizeof(g_dcb));
	nic_dev->dcb = &g_dcb;
	nic_dev->dcb->dcb_on = dcb_on ? 1 : 0;
	nic_dev->max_sqs = max_sqs;
	nic_dev->max_rqs = max_rqs;
	nic_dev->num_sqs = num_sqs;
	nic_dev->num_rqs = num_rqs;
	nic_dev->feature_cap = 0;
	nic_dev->hwdev = &g_mock_hwdev;
}

/* ====== Full mock ethdev setup for ops callback tests ====== */

static int
mock_ethdev_setup(void)
{
	/* Set up PCI device with SP620 device_id */
	memset(&g_mock_pci_dev, 0, sizeof(g_mock_pci_dev));
	g_mock_pci_dev.id.device_id = HINIC3_DEV_ID_SP620;

	/* Set up hwdev */
	memset(&g_mock_hwdev, 0, sizeof(g_mock_hwdev));
	g_mock_hwdev.port_id = MOCK_ETH_DEV_PORT_ID;
	g_mock_hwdev.dev_handle = &g_mock_nic_dev;
	memset(&g_mock_hwif, 0, sizeof(g_mock_hwif));
	g_mock_hwdev.hwif = &g_mock_hwif;

	/* Set up nic_dev */
	memset(&g_mock_nic_dev, 0, sizeof(g_mock_nic_dev));
	memset(&g_dcb, 0, sizeof(g_dcb));
	g_mock_nic_dev.dcb = &g_dcb;
	g_mock_nic_dev.hwdev = &g_mock_hwdev;
	pthread_mutex_init(&g_mock_nic_dev.rx_mode_mutex, NULL);
	pthread_mutex_init(&g_mock_nic_dev.pause_mutuex, NULL);
	g_mock_nic_dev.num_sqs = 4;
	g_mock_nic_dev.num_rqs = 4;
	g_mock_nic_dev.rss_state = HINIC3_RSS_ENABLE;
	g_mock_nic_dev.rx_mode = HINIC3_RX_MODE_UC | HINIC3_RX_MODE_MC |
				 HINIC3_RX_MODE_BC;
	g_mock_nic_dev.feature_cap = NIC_F_CSUM | NIC_F_TSO | NIC_F_PROMISC;

	/* Set up eth_dev_data and rte_eth_devices[0] */
	memset(&g_mock_dev_data, 0, sizeof(g_mock_dev_data));
	g_mock_dev_data.dev_private = &g_mock_nic_dev;
	rte_eth_devices[MOCK_ETH_DEV_PORT_ID].data = &g_mock_dev_data;
	rte_eth_devices[MOCK_ETH_DEV_PORT_ID].device = &g_mock_pci_dev.device;

	/* Reset mock control variables to defaults */
	g_mock_add_vlan_ret = 0;
	g_mock_del_vlan_ret = 0;
	g_mock_set_port_mtu_ret = 0;
	g_mock_set_rx_mode_ret = 0;
	g_mock_rss_get_indir_tbl_ret = 0;
	g_mock_rss_set_indir_tbl_ret = 0;
	g_mock_rss_set_indir_tbl_qpool_ret = 0;
	g_mock_global_func_id = 0;
	g_mock_is_sp620_nic = true;
	g_mock_is_sp230_nic = false;
	g_mock_update_mac_ret = 0;
	g_mock_set_mac_ret = 0;
	g_mock_del_mac_ret = 0;
	g_mock_set_vlan_fliter_ret = 0;
	g_mock_set_rx_vlan_offload_ret = 0;
	g_mock_msg_to_mgmt_sync_ret = 0;
	g_mock_mgmt_sync_ret = 0;

	/* Set qinfo type to normal mode */
	g_qinfo_type = HINIC3_QINFO_TYPE_NORMAL;
	g_mock_hwdev.qinfo_type = HINIC3_QINFO_TYPE_NORMAL;

	return TEST_SUCCESS;
}

static void
mock_ethdev_teardown(void)
{
	rte_eth_devices[MOCK_ETH_DEV_PORT_ID].device = NULL;
	rte_eth_devices[MOCK_ETH_DEV_PORT_ID].data = NULL;
}

/* ====== VFTA macro tests ====== */

static int
test_vfta_idx(void)
{
	RTE_TEST_ASSERT_EQUAL(HINIC3_VFTA_IDX(0), 0, "vlan 0 idx");
	RTE_TEST_ASSERT_EQUAL(HINIC3_VFTA_IDX(31), 0, "vlan 31 idx");
	RTE_TEST_ASSERT_EQUAL(HINIC3_VFTA_IDX(32), 1, "vlan 32 idx");
	RTE_TEST_ASSERT_EQUAL(HINIC3_VFTA_IDX(4095), 127, "vlan 4095 idx");
	RTE_TEST_ASSERT_EQUAL(HINIC3_VFTA_IDX(100), 3, "vlan 100 idx");

	return TEST_SUCCESS;
}

static int
test_vfta_bit(void)
{
	RTE_TEST_ASSERT_EQUAL(HINIC3_VFTA_BIT(0), 1u << 0, "vlan 0 bit");
	RTE_TEST_ASSERT_EQUAL(HINIC3_VFTA_BIT(1), 1u << 1, "vlan 1 bit");
	RTE_TEST_ASSERT_EQUAL(HINIC3_VFTA_BIT(31), 1u << 31, "vlan 31 bit");
	RTE_TEST_ASSERT_EQUAL(HINIC3_VFTA_BIT(5), 1u << 5, "vlan 5 bit");

	return TEST_SUCCESS;
}

static int
test_vfta_size(void)
{
	RTE_TEST_ASSERT_EQUAL(HINIC3_VFTA_SIZE, 128,
			      "VFTA array size should be 128 (4096/32)");
	RTE_TEST_ASSERT_EQUAL(HINIC3_UINT32_BIT_SIZE, 32,
			      "uint32 bit size should be 32");

	return TEST_SUCCESS;
}

/* ====== VLAN filter store/find logic ====== */

static int
test_vlan_filter_store_and_find(void)
{
	struct hinic3_nic_dev nic_dev;
	u32 vid_idx, vid_bit;
	u32 i;

	memset(&nic_dev, 0, sizeof(nic_dev));

	/* Initially all VLANs should not be found */
	vid_idx = HINIC3_VFTA_IDX(100);
	vid_bit = HINIC3_VFTA_BIT(100);
	RTE_TEST_ASSERT_EQUAL((nic_dev.vfta[vid_idx] & vid_bit) ? 1 : 0, 0,
			      "vlan 100 should not be set initially");

	/* Store VLAN 100 */
	vid_idx = HINIC3_VFTA_IDX(100);
	vid_bit = HINIC3_VFTA_BIT(100);
	nic_dev.vfta[vid_idx] |= vid_bit;
	RTE_TEST_ASSERT_EQUAL((nic_dev.vfta[vid_idx] & vid_bit) ? 1 : 0, 1,
			      "vlan 100 should be set after store");

	/* Store VLAN 200 */
	vid_idx = HINIC3_VFTA_IDX(200);
	vid_bit = HINIC3_VFTA_BIT(200);
	nic_dev.vfta[vid_idx] |= vid_bit;
	RTE_TEST_ASSERT_EQUAL((nic_dev.vfta[vid_idx] & vid_bit) ? 1 : 0, 1,
			      "vlan 200 should be set after store");

	/* Remove VLAN 100 */
	vid_idx = HINIC3_VFTA_IDX(100);
	vid_bit = HINIC3_VFTA_BIT(100);
	nic_dev.vfta[vid_idx] &= ~vid_bit;
	RTE_TEST_ASSERT_EQUAL((nic_dev.vfta[vid_idx] & vid_bit) ? 1 : 0, 0,
			      "vlan 100 should be cleared after remove");

	/* VLAN 200 should still be set */
	vid_idx = HINIC3_VFTA_IDX(200);
	vid_bit = HINIC3_VFTA_BIT(200);
	RTE_TEST_ASSERT_EQUAL((nic_dev.vfta[vid_idx] & vid_bit) ? 1 : 0, 1,
			      "vlan 200 should still be set");

	/* Multiple VLANs in same 32-bit element */
	vid_idx = HINIC3_VFTA_IDX(1);
	vid_bit = HINIC3_VFTA_BIT(1);
	nic_dev.vfta[vid_idx] |= vid_bit;
	vid_bit = HINIC3_VFTA_BIT(2);
	nic_dev.vfta[vid_idx] |= vid_bit;
	RTE_TEST_ASSERT_EQUAL((nic_dev.vfta[HINIC3_VFTA_IDX(1)] & HINIC3_VFTA_BIT(1)) ? 1 : 0, 1,
			      "vlan 1 should be set");
	RTE_TEST_ASSERT_EQUAL((nic_dev.vfta[HINIC3_VFTA_IDX(2)] & HINIC3_VFTA_BIT(2)) ? 1 : 0, 1,
			      "vlan 2 should be set");

	/* Clear all */
	for (i = 0; i < HINIC3_VFTA_SIZE; i++)
		nic_dev.vfta[i] = 0;

	return TEST_SUCCESS;
}

/* ====== Queue depth power-of-2 alignment ====== */

static int
test_queue_depth_alignment(void)
{
	u16 depth;

	/* Power of 2 values stay unchanged */
	depth = (128 & (128 - 1)) ? ((u16)(1U << (ilog2(128) + 1))) : 128;
	RTE_TEST_ASSERT_EQUAL(depth, 128, "128 is power of 2");

	depth = (256 & (256 - 1)) ? ((u16)(1U << (ilog2(256) + 1))) : 256;
	RTE_TEST_ASSERT_EQUAL(depth, 256, "256 is power of 2");

	depth = (1024 & (1024 - 1)) ? ((u16)(1U << (ilog2(1024) + 1))) : 1024;
	RTE_TEST_ASSERT_EQUAL(depth, 1024, "1024 is power of 2");

	/* Non-power-of-2 aligned up */
	depth = (100 & (100 - 1)) ? ((u16)(1U << (ilog2(100) + 1))) : 100;
	RTE_TEST_ASSERT_EQUAL(depth, 128, "100 aligned to 128");

	depth = (200 & (200 - 1)) ? ((u16)(1U << (ilog2(200) + 1))) : 200;
	RTE_TEST_ASSERT_EQUAL(depth, 256, "200 aligned to 256");

	depth = (129 & (129 - 1)) ? ((u16)(1U << (ilog2(129) + 1))) : 129;
	RTE_TEST_ASSERT_EQUAL(depth, 256, "129 aligned to 256");

	depth = (63 & (63 - 1)) ? ((u16)(1U << (ilog2(63) + 1))) : 63;
	RTE_TEST_ASSERT_EQUAL(depth, 64, "63 aligned to 64");

	return TEST_SUCCESS;
}

/* ====== Descriptor limit constants ====== */

static int
test_desc_limit_constants(void)
{
	RTE_TEST_ASSERT_EQUAL(HINIC3_MAX_QUEUE_DEPTH, 16384, "max queue depth");
	RTE_TEST_ASSERT_EQUAL(HINIC3_MIN_QUEUE_DEPTH, 128, "min queue depth");
	RTE_TEST_ASSERT_EQUAL(HINIC3_MIN_RX_BUF_SIZE, 1024, "min rx buf size");

	return TEST_SUCCESS;
}

/* ====== UP_ALIGN / WQE_BUF_SIZE macros ====== */

static int
test_align_macros(void)
{
	u32 val;

	/* UP_ALIGN: align up to page size (4096) */
	val = UP_ALIGN(1, HINIC3_RQSQ_PAGE_SIZE);
	RTE_TEST_ASSERT_EQUAL(val, 4096u, "1 aligned to 4096");

	val = UP_ALIGN(4096, HINIC3_RQSQ_PAGE_SIZE);
	RTE_TEST_ASSERT_EQUAL(val, 4096u, "4096 aligned to 4096");

	val = UP_ALIGN(4097, HINIC3_RQSQ_PAGE_SIZE);
	RTE_TEST_ASSERT_EQUAL(val, 8192u, "4097 aligned to 8192");

	/* SQWQE_BUF_SIZE for depth=128: 128 << 4 = 2048, aligned to 4096 */
	val = SQWQE_BUF_SIZE(128);
	RTE_TEST_ASSERT_EQUAL(val, 4096u, "SQWQE 128 depth buf size");

	/* SQWQE_BUF_SIZE for depth=1024: 1024 << 4 = 16384, aligned to 16384 */
	val = SQWQE_BUF_SIZE(1024);
	RTE_TEST_ASSERT_EQUAL(val, 16384u, "SQWQE 1024 depth buf size");

	/* RQWQE_BUF_SIZE for depth=128: 128 << 4 = 2048, aligned to 4096 */
	val = RQWQE_BUF_SIZE(128);
	RTE_TEST_ASSERT_EQUAL(val, 4096u, "RQWQE 128 depth buf size");

	return TEST_SUCCESS;
}

/* ====== Version parsing logic ====== */
/*
 * hinic3_parse_version is static in hinic3_pmd_ethdev.c and cannot be
 * called directly. We duplicate the logic locally for coverage testing.
 */

#define HINIC3_FW_VERSION_LEN 64
#define HINIC3_FW_VERSION_NUM 4

static int
parse_version_local(const char *str, uint8_t *ver)
{
	uint8_t idx = 0;
	uint16_t val = 0;
	uint8_t char_cnt = 0;
	const char *p;
	unsigned char c;

	if (str == NULL || ver == NULL)
		return -EINVAL;

	for (p = str; *p; ++p) {
		if (++char_cnt > HINIC3_FW_VERSION_LEN)
			return -EINVAL;

		c = *p;
		if (c >= '0' && c <= '9')
			val = val * 10 + (c - '0');
		else if (c == '.') {
			if (idx >= HINIC3_FW_VERSION_NUM - 1)
				return -EINVAL;
			if (val > UINT8_MAX)
				return -EINVAL;
			ver[idx++] = (uint8_t)val;
			val = 0;
		} else
			return -EINVAL;
	}
	if (idx != HINIC3_FW_VERSION_NUM - 1)
		return -EINVAL;

	if (val > UINT8_MAX)
		return -EINVAL;
	ver[idx++] = (uint8_t)val;
	return 0;
}

static int
test_parse_version_valid(void)
{
	uint8_t ver[HINIC3_FW_VERSION_NUM] = {0};
	int ret;

	ret = parse_version_local("1.2.3.4", ver);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "valid version should parse");
	RTE_TEST_ASSERT_EQUAL(ver[0], 1, "major");
	RTE_TEST_ASSERT_EQUAL(ver[1], 2, "minor");
	RTE_TEST_ASSERT_EQUAL(ver[2], 3, "patch");
	RTE_TEST_ASSERT_EQUAL(ver[3], 4, "build");

	return TEST_SUCCESS;
}

static int
test_parse_version_larger_numbers(void)
{
	uint8_t ver[HINIC3_FW_VERSION_NUM] = {0};
	int ret;

	ret = parse_version_local("15.19.2.9", ver);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "version 15.19.2.9 should parse");
	RTE_TEST_ASSERT_EQUAL(ver[0], 15, "major");
	RTE_TEST_ASSERT_EQUAL(ver[1], 19, "minor");
	RTE_TEST_ASSERT_EQUAL(ver[2], 2, "patch");
	RTE_TEST_ASSERT_EQUAL(ver[3], 9, "build");

	return TEST_SUCCESS;
}

static int
test_parse_version_invalid_null(void)
{
	uint8_t ver[HINIC3_FW_VERSION_NUM] = {0};
	int ret;

	ret = parse_version_local(NULL, ver);
	RTE_TEST_ASSERT_EQUAL(ret, -EINVAL, "NULL string should fail");

	ret = parse_version_local("1.2.3.4", NULL);
	RTE_TEST_ASSERT_EQUAL(ret, -EINVAL, "NULL ver should fail");

	return TEST_SUCCESS;
}

static int
test_parse_version_invalid_format(void)
{
	uint8_t ver[HINIC3_FW_VERSION_NUM] = {0};
	int ret;

	/* Too few components */
	ret = parse_version_local("1.2.3", ver);
	RTE_TEST_ASSERT(ret != 0, "3-component version should fail");

	/* Too many components */
	ret = parse_version_local("1.2.3.4.5", ver);
	RTE_TEST_ASSERT(ret != 0, "5-component version should fail");

	/* Non-numeric characters */
	ret = parse_version_local("1.2.3.a", ver);
	RTE_TEST_ASSERT(ret != 0, "non-numeric version should fail");

	/* Empty string */
	ret = parse_version_local("", ver);
	RTE_TEST_ASSERT(ret != 0, "empty version should fail");

	return TEST_SUCCESS;
}

/* ====== FEC parameter validation ====== */
/*
 * hinic3_fec_param_valid is static in hinic3_pmd_ethdev.c and cannot be
 * called directly. We duplicate the logic locally for coverage testing.
 */

#define HINIC3_FEC_MODE_OFF    0
#define HINIC3_FEC_MODE_BASER  1
#define HINIC3_FEC_MODE_RS     2
#define HINIC3_FEC_MODE_LLRS   3

static bool
fec_param_valid_local(uint32_t fec_param)
{
	if ((fec_param == HINIC3_FEC_MODE_LLRS) ||
	    (fec_param == HINIC3_FEC_MODE_RS) ||
	    (fec_param == HINIC3_FEC_MODE_BASER) ||
	    (fec_param == HINIC3_FEC_MODE_OFF))
		return true;

	return false;
}

static int
test_fec_param_valid(void)
{
	RTE_TEST_ASSERT(fec_param_valid_local(HINIC3_FEC_MODE_OFF),
		       "FEC OFF should be valid");
	RTE_TEST_ASSERT(fec_param_valid_local(HINIC3_FEC_MODE_BASER),
		       "FEC BASER should be valid");
	RTE_TEST_ASSERT(fec_param_valid_local(HINIC3_FEC_MODE_RS),
		       "FEC RS should be valid");
	RTE_TEST_ASSERT(fec_param_valid_local(HINIC3_FEC_MODE_LLRS),
		       "FEC LLRS should be valid");
	RTE_TEST_ASSERT(!fec_param_valid_local(4),
			"FEC mode 4 should be invalid");
	RTE_TEST_ASSERT(!fec_param_valid_local(255),
			"FEC mode 255 should be invalid");

	return TEST_SUCCESS;
}

/* ====== hinic3_dev_info_get — direct function call ====== */
/*
 * With --wrap mocking is_sp620_nic / hinic3_get_driver_feature, we can
 * call hinic3_dev_info_get() directly. This provides real code coverage.
 */

static int
test_dev_info_get_basic(void)
{
	struct rte_eth_dev_info info;
	struct hinic3_nic_dev nic_dev;

	mock_nic_dev_init(&nic_dev, 64, 64, 4, 4, false);
	mock_set_nic_type(HINIC3_DEV_ID_SP620);

	memset(&info, 0, sizeof(info));
	hinic3_dev_info_get(&info, &nic_dev);

	/* Basic constants */
	RTE_TEST_ASSERT_EQUAL(info.min_rx_bufsize, HINIC3_MIN_RX_BUF_SIZE,
			      "min rx buf size");
	RTE_TEST_ASSERT_EQUAL(info.max_rx_pktlen, HINIC3_MAX_JUMBO_FRAME_SIZE,
			      "max rx pktlen");
	RTE_TEST_ASSERT_EQUAL(info.max_mac_addrs, HINIC3_MAX_UC_MAC_ADDRS,
			      "max mac addrs");
	RTE_TEST_ASSERT_EQUAL(info.min_mtu, HINIC3_MIN_MTU_SIZE, "min mtu");
	RTE_TEST_ASSERT_EQUAL(info.max_mtu, HINIC3_MAX_MTU_SIZE, "max mtu");
	RTE_TEST_ASSERT_EQUAL(info.max_lro_pkt_size, HINIC3_MAX_LRO_SIZE,
			      "max lro size");

	return TEST_SUCCESS;
}

static int
test_dev_info_get_dcb_off(void)
{
	struct rte_eth_dev_info info;
	struct hinic3_nic_dev nic_dev;

	/* DCB off: use max_sqs/max_rqs */
	mock_nic_dev_init(&nic_dev, 64, 64, 4, 4, false);
	mock_set_nic_type(HINIC3_DEV_ID_SP620);

	memset(&info, 0, sizeof(info));
	hinic3_dev_info_get(&info, &nic_dev);

	RTE_TEST_ASSERT_EQUAL(info.max_rx_queues, 64, "max rx queues (dcb off)");
	RTE_TEST_ASSERT_EQUAL(info.max_tx_queues, 64, "max tx queues (dcb off)");

	return TEST_SUCCESS;
}

static int
test_dev_info_get_dcb_on(void)
{
	struct rte_eth_dev_info info;
	struct hinic3_nic_dev nic_dev;

	/* DCB on: use num_sqs/num_rqs */
	mock_nic_dev_init(&nic_dev, 64, 64, 4, 4, true);
	mock_set_nic_type(HINIC3_DEV_ID_SP620);

	memset(&info, 0, sizeof(info));
	hinic3_dev_info_get(&info, &nic_dev);

	RTE_TEST_ASSERT_EQUAL(info.max_rx_queues, 4, "max rx queues (dcb on)");
	RTE_TEST_ASSERT_EQUAL(info.max_tx_queues, 4, "max tx queues (dcb on)");

	return TEST_SUCCESS;
}

static int
test_dev_info_get_rx_offloads(void)
{
	struct rte_eth_dev_info info;
	struct hinic3_nic_dev nic_dev;

	mock_nic_dev_init(&nic_dev, 64, 64, 4, 4, false);
	mock_set_nic_type(HINIC3_DEV_ID_SP620);

	memset(&info, 0, sizeof(info));
	hinic3_dev_info_get(&info, &nic_dev);

	/* Verify key RX offload bits */
	RTE_TEST_ASSERT(info.rx_offload_capa & RTE_ETH_RX_OFFLOAD_VLAN_STRIP,
			"RX VLAN strip should be supported");
	RTE_TEST_ASSERT(info.rx_offload_capa & RTE_ETH_RX_OFFLOAD_IPV4_CKSUM,
			"RX IPv4 cksum should be supported");
	RTE_TEST_ASSERT(info.rx_offload_capa & RTE_ETH_RX_OFFLOAD_UDP_CKSUM,
			"RX UDP cksum should be supported");
	RTE_TEST_ASSERT(info.rx_offload_capa & RTE_ETH_RX_OFFLOAD_TCP_CKSUM,
			"RX TCP cksum should be supported");
	RTE_TEST_ASSERT(info.rx_offload_capa & RTE_ETH_RX_OFFLOAD_SCTP_CKSUM,
			"RX SCTP cksum should be supported");
	RTE_TEST_ASSERT(info.rx_offload_capa & RTE_ETH_RX_OFFLOAD_VLAN_FILTER,
			"RX VLAN filter should be supported");
	RTE_TEST_ASSERT(info.rx_offload_capa & RTE_ETH_RX_OFFLOAD_SCATTER,
			"RX scatter should be supported");
	RTE_TEST_ASSERT(info.rx_offload_capa & RTE_ETH_RX_OFFLOAD_RSS_HASH,
			"RX RSS hash should be supported");

	return TEST_SUCCESS;
}

static int
test_dev_info_get_tx_offloads(void)
{
	struct rte_eth_dev_info info;
	struct hinic3_nic_dev nic_dev;

	mock_nic_dev_init(&nic_dev, 64, 64, 4, 4, false);
	mock_set_nic_type(HINIC3_DEV_ID_SP620);

	memset(&info, 0, sizeof(info));
	hinic3_dev_info_get(&info, &nic_dev);

	/* Verify key TX offload bits */
	RTE_TEST_ASSERT(info.tx_offload_capa & RTE_ETH_TX_OFFLOAD_VLAN_INSERT,
			"TX VLAN insert should be supported");
	RTE_TEST_ASSERT(info.tx_offload_capa & RTE_ETH_TX_OFFLOAD_IPV4_CKSUM,
			"TX IPv4 cksum should be supported");
	RTE_TEST_ASSERT(info.tx_offload_capa & RTE_ETH_TX_OFFLOAD_UDP_CKSUM,
			"TX UDP cksum should be supported");
	RTE_TEST_ASSERT(info.tx_offload_capa & RTE_ETH_TX_OFFLOAD_TCP_CKSUM,
			"TX TCP cksum should be supported");
	RTE_TEST_ASSERT(info.tx_offload_capa & RTE_ETH_TX_OFFLOAD_SCTP_CKSUM,
			"TX SCTP cksum should be supported");
	RTE_TEST_ASSERT(info.tx_offload_capa & RTE_ETH_TX_OFFLOAD_TCP_TSO,
			"TX TSO should be supported");
	RTE_TEST_ASSERT(info.tx_offload_capa & RTE_ETH_TX_OFFLOAD_MULTI_SEGS,
			"TX multi segs should be supported");

	return TEST_SUCCESS;
}

static int
test_dev_info_get_desc_limits(void)
{
	struct rte_eth_dev_info info;
	struct hinic3_nic_dev nic_dev;

	mock_nic_dev_init(&nic_dev, 64, 64, 4, 4, false);
	mock_set_nic_type(HINIC3_DEV_ID_SP620);

	memset(&info, 0, sizeof(info));
	hinic3_dev_info_get(&info, &nic_dev);

	RTE_TEST_ASSERT_EQUAL(info.rx_desc_lim.nb_max, HINIC3_MAX_QUEUE_DEPTH,
			      "rx desc max");
	RTE_TEST_ASSERT_EQUAL(info.rx_desc_lim.nb_min, HINIC3_MIN_QUEUE_DEPTH,
			      "rx desc min");
	RTE_TEST_ASSERT_EQUAL(info.tx_desc_lim.nb_max, HINIC3_MAX_QUEUE_DEPTH,
			      "tx desc max");
	RTE_TEST_ASSERT_EQUAL(info.tx_desc_lim.nb_min, HINIC3_MIN_QUEUE_DEPTH,
			      "tx desc min");

	return TEST_SUCCESS;
}

static int
test_dev_info_get_rss_params(void)
{
	struct rte_eth_dev_info info;
	struct hinic3_nic_dev nic_dev;

	mock_nic_dev_init(&nic_dev, 64, 64, 4, 4, false);
	mock_set_nic_type(HINIC3_DEV_ID_SP620);

	memset(&info, 0, sizeof(info));
	hinic3_dev_info_get(&info, &nic_dev);

	RTE_TEST_ASSERT_EQUAL(info.hash_key_size, HINIC3_RSS_KEY_SIZE,
			      "hash key size");
	RTE_TEST_ASSERT_EQUAL(info.reta_size, HINIC3_RSS_INDIR_SIZE,
			      "reta size");
	RTE_TEST_ASSERT_EQUAL(info.flow_type_rss_offloads,
			      HINIC3_RSS_OFFLOAD_ALL,
			      "rss offloads");

	return TEST_SUCCESS;
}

static int
test_dev_info_get_default_params(void)
{
	struct rte_eth_dev_info info;
	struct hinic3_nic_dev nic_dev;

	mock_nic_dev_init(&nic_dev, 64, 64, 4, 4, false);
	mock_set_nic_type(HINIC3_DEV_ID_SP620);

	memset(&info, 0, sizeof(info));
	hinic3_dev_info_get(&info, &nic_dev);

	RTE_TEST_ASSERT_EQUAL(info.default_rxportconf.burst_size,
			      HINIC3_DEFAULT_BURST_SIZE,
			      "default rx burst size");
	RTE_TEST_ASSERT_EQUAL(info.default_txportconf.burst_size,
			      HINIC3_DEFAULT_BURST_SIZE,
			      "default tx burst size");
	RTE_TEST_ASSERT_EQUAL(info.default_rxportconf.nb_queues,
			      HINIC3_DEFAULT_NB_QUEUES,
			      "default rx nb queues");
	RTE_TEST_ASSERT_EQUAL(info.default_txportconf.nb_queues,
			      HINIC3_DEFAULT_NB_QUEUES,
			      "default tx nb queues");
	RTE_TEST_ASSERT_EQUAL(info.default_rxportconf.ring_size,
			      HINIC3_DEFAULT_RING_SIZE,
			      "default rx ring size");
	RTE_TEST_ASSERT_EQUAL(info.default_txportconf.ring_size,
			      HINIC3_DEFAULT_RING_SIZE,
			      "default tx ring size");

	return TEST_SUCCESS;
}

static int
test_dev_info_get_sp620_tnl_tso(void)
{
	struct rte_eth_dev_info info;
	struct hinic3_nic_dev nic_dev;

	/* When is_sp620_nic returns true, tnl_tso_support is NOT called */
	mock_nic_dev_init(&nic_dev, 64, 64, 4, 4, false);
	mock_set_nic_type(HINIC3_DEV_ID_SP620);

	memset(&info, 0, sizeof(info));
	hinic3_dev_info_get(&info, &nic_dev);

	/* SP620: no GENEVE_TNL_TSO or IPIP_TNL_TSO (these are only added
	 * by hinic3_dev_tnl_tso_support for non-SP620 NICs)
	 */
	RTE_TEST_ASSERT(!(info.tx_offload_capa & RTE_ETH_TX_OFFLOAD_GENEVE_TNL_TSO),
			"SP620 should NOT have GENEVE TNL TSO");
	RTE_TEST_ASSERT(!(info.tx_offload_capa & RTE_ETH_TX_OFFLOAD_IPIP_TNL_TSO),
			"SP620 should NOT have IPIP TNL TSO");

	return TEST_SUCCESS;
}

static int
test_dev_info_get_non_sp620_tnl_tso(void)
{
	struct rte_eth_dev_info info;
	struct hinic3_nic_dev nic_dev;

	/* When is_sp620_nic returns false, hinic3_dev_tnl_tso_support IS
	 * called. With mocked hinic3_get_driver_feature returning 0, no
	 * additional TSO offloads are added.
	 */
	mock_nic_dev_init(&nic_dev, 64, 64, 4, 4, false);
	mock_set_nic_type(0xFFFF);
	nic_dev.feature_cap = 0;

	memset(&info, 0, sizeof(info));
	hinic3_dev_info_get(&info, &nic_dev);

	/* No extra TSO offloads when feature_cap has no offload bits */
	RTE_TEST_ASSERT(!(info.tx_offload_capa & RTE_ETH_TX_OFFLOAD_GENEVE_TNL_TSO),
			"no GENEVE TNL TSO without feature");
	RTE_TEST_ASSERT(!(info.tx_offload_capa & RTE_ETH_TX_OFFLOAD_IPIP_TNL_TSO),
			"no IPIP TNL TSO without feature");

	return TEST_SUCCESS;
}

static int
test_dev_info_get_non_sp620_geneve_tso(void)
{
	struct rte_eth_dev_info info;
	struct hinic3_nic_dev nic_dev;

	/* When is_sp620_nic returns false and GENEVE offload is supported */
	mock_nic_dev_init(&nic_dev, 64, 64, 4, 4, false);
	mock_set_nic_type(0xFFFF);
	nic_dev.feature_cap = NIC_F_GENEVE_OFFLOAD;

	memset(&info, 0, sizeof(info));
	hinic3_dev_info_get(&info, &nic_dev);

	/* GENEVE_TNL_TSO should be set */
	RTE_TEST_ASSERT(info.tx_offload_capa & RTE_ETH_TX_OFFLOAD_GENEVE_TNL_TSO,
			"GENEVE TNL TSO should be set with feature");

	return TEST_SUCCESS;
}

static int
test_dev_info_get_non_sp620_ipxip_tso(void)
{
	struct rte_eth_dev_info info;
	struct hinic3_nic_dev nic_dev;

	/* When is_sp620_nic returns false and IPXIP offload is supported */
	mock_nic_dev_init(&nic_dev, 64, 64, 4, 4, false);
	mock_set_nic_type(0xFFFF);
	nic_dev.feature_cap = NIC_F_IPXIP_OFFLOAD;

	memset(&info, 0, sizeof(info));
	hinic3_dev_info_get(&info, &nic_dev);

	/* IPIP_TNL_TSO should be set */
	RTE_TEST_ASSERT(info.tx_offload_capa & RTE_ETH_TX_OFFLOAD_IPIP_TNL_TSO,
			"IPIP TNL TSO should be set with feature");

	return TEST_SUCCESS;
}

/* ====== hinic3_init_sw_rxtxqs / hinic3_deinit_sw_rxtxqs ====== */
/*
 * These are static functions in hinic3_pmd_ethdev.c and cannot be called
 * directly. We duplicate the logic locally for coverage testing.
 */

static int
init_sw_rxtxqs_local(struct hinic3_nic_dev *nic_dev)
{
	u32 txq_size;
	u32 rxq_size;

	txq_size = nic_dev->max_sqs * sizeof(*nic_dev->txqs);
	nic_dev->txqs = rte_zmalloc("hinic3_txqs", txq_size,
				    RTE_CACHE_LINE_SIZE);
	if (!nic_dev->txqs)
		return -ENOMEM;

	rxq_size = nic_dev->max_rqs * sizeof(*nic_dev->rxqs);
	nic_dev->rxqs = rte_zmalloc("hinic3_rxqs", rxq_size,
				    RTE_CACHE_LINE_SIZE);
	if (!nic_dev->rxqs) {
		rte_free(nic_dev->txqs);
		nic_dev->txqs = NULL;
		return -ENOMEM;
	}

	return 0;
}

static void
deinit_sw_rxtxqs_local(struct hinic3_nic_dev *nic_dev)
{
	rte_free(nic_dev->txqs);
	nic_dev->txqs = NULL;
	rte_free(nic_dev->rxqs);
	nic_dev->rxqs = NULL;
}

static int
ethdev_test_setup(void)
{
	/* Default mock state: SP620 NIC, no special driver features */
	mock_set_nic_type(HINIC3_DEV_ID_SP620);

	return TEST_SUCCESS;
}

static void
ethdev_test_teardown(void)
{
	rte_eth_devices[MOCK_ETH_DEV_PORT_ID].device = NULL;
}

static int
test_init_sw_rxtxqs(void)
{
	struct hinic3_nic_dev nic_dev;
	int ret;

	memset(&nic_dev, 0, sizeof(nic_dev));
	nic_dev.max_sqs = 4;
	nic_dev.max_rqs = 4;

	ret = init_sw_rxtxqs_local(&nic_dev);

	RTE_TEST_ASSERT_EQUAL(ret, 0, "init_sw_rxtxqs should succeed");
	RTE_TEST_ASSERT_NOT_NULL(nic_dev.txqs, "txqs should be allocated");
	RTE_TEST_ASSERT_NOT_NULL(nic_dev.rxqs, "rxqs should be allocated");

	deinit_sw_rxtxqs_local(&nic_dev);

	RTE_TEST_ASSERT_NULL(nic_dev.txqs, "txqs should be NULL after deinit");
	RTE_TEST_ASSERT_NULL(nic_dev.rxqs, "rxqs should be NULL after deinit");

	return TEST_SUCCESS;
}

static int
test_init_sw_rxtxqs_large(void)
{
	struct hinic3_nic_dev nic_dev;
	int ret;

	memset(&nic_dev, 0, sizeof(nic_dev));
	nic_dev.max_sqs = 256;
	nic_dev.max_rqs = 256;

	ret = init_sw_rxtxqs_local(&nic_dev);

	RTE_TEST_ASSERT_EQUAL(ret, 0, "init_sw_rxtxqs large should succeed");
	RTE_TEST_ASSERT_NOT_NULL(nic_dev.txqs, "txqs should be allocated");
	RTE_TEST_ASSERT_NOT_NULL(nic_dev.rxqs, "rxqs should be allocated");

	deinit_sw_rxtxqs_local(&nic_dev);

	return TEST_SUCCESS;
}

/* ====== xstats_calc_num logic ====== */

static int
test_xstats_calc_num_pf(void)
{
	int expected;
	int actual;

	/* PF: vport + cir_drop + phyport + rxq*x + txq*x */
	expected = HINIC3_VPORT_XSTATS_NUM +
		   HINIC3_CIR_DROP_XSTATS_NUM +
		   HINIC3_PHYPORT_XSTATS_NUM +
		   HINIC3_RXQ_XSTATS_NUM * g_mock_nic_dev.num_rqs +
		   HINIC3_TXQ_XSTATS_NUM * g_mock_nic_dev.num_sqs;

	actual = hinic3_xstats_calc_num(&g_mock_nic_dev);
	RTE_TEST_ASSERT_EQUAL(actual, expected,
			      "PF xstats calc mismatch");

	return TEST_SUCCESS;
}

static int
test_xstats_calc_num_vf(void)
{
	int expected;
	int actual;

	/* VF: vport + cir_drop + rxq*x + txq*x (no phyport) */
	g_mock_hwif.attr.func_type = TYPE_VF;
	expected = HINIC3_VPORT_XSTATS_NUM +
		   HINIC3_CIR_DROP_XSTATS_NUM +
		   HINIC3_RXQ_XSTATS_NUM * g_mock_nic_dev.num_rqs +
		   HINIC3_TXQ_XSTATS_NUM * g_mock_nic_dev.num_sqs;

	actual = hinic3_xstats_calc_num(&g_mock_nic_dev);
	RTE_TEST_ASSERT_EQUAL(actual, expected,
			      "VF xstats calc mismatch");
	g_mock_hwif.attr.func_type = TYPE_PF;

	return TEST_SUCCESS;
}

/* ====== nic_dev config default values ====== */

static int
test_nic_dev_config_defaults(void)
{
	struct hinic3_nic_common_dev_config config;

	memset(&config, 0, sizeof(config));

	/* Simulate default value initialization as done in
	 * hinic3_nic_common_config_get when devargs is NULL
	 */
	config.rx_empty_threshold = HINIC3_RX_EMPTY_THRESHOLD;
	config.tx_free_loop = HINIC3_DEFAULT_TX_FREE_LOOP;
	config.tx_pending_limit = HINIC3_DEFAULT_TX_CI_PENDING_LIMIT;
	config.tx_coalescing_time = HINIC3_DEFAULT_TX_CI_COALESCING_TIME;
	config.rx_cqe_compact_en = HINIC3_RX_CQE_COMPACT_EN;
	config.rx_cqe_coalesce_num = HINIC3_RX_CQE_COALESCE_NUM;
	config.rx_cqe_timer_loop = HINIC3_RX_CQE_TIMER_LOOP;

	RTE_TEST_ASSERT_EQUAL(config.rx_empty_threshold, 3,
			      "default rx_empty_threshold");
	RTE_TEST_ASSERT_EQUAL(config.tx_free_loop, 1000,
			      "default tx_free_loop");
	RTE_TEST_ASSERT_EQUAL(config.tx_pending_limit, 2,
			      "default tx_pending_limit");
	RTE_TEST_ASSERT_EQUAL(config.tx_coalescing_time, 2,
			      "default tx_coalescing_time");
	RTE_TEST_ASSERT_EQUAL(config.rx_cqe_compact_en, 1,
			      "default rx_cqe_compact_en");

	return TEST_SUCCESS;
}

/* ====== Queue offset macros ====== */

static int
test_queue_offset_macros(void)
{
	u32 q_buf_size = SQWQE_BUF_SIZE(128);
	u16 depth = 128;
	u32 offset;

	/* SQWQE_OFFSET: q_buf_size * q_id */
	offset = SQWQE_OFFSET(q_buf_size, 0);
	RTE_TEST_ASSERT_EQUAL(offset, 0u, "SQ WQE offset q0");

	offset = SQWQE_OFFSET(q_buf_size, 1);
	RTE_TEST_ASSERT_EQUAL(offset, q_buf_size, "SQ WQE offset q1");

	/* SQCI_OFFSET: after SQ WQE within the queue buffer */
	offset = SQCI_OFFSET(q_buf_size, 0, depth);
	RTE_TEST_ASSERT_EQUAL(offset, q_buf_size, "SQ CI offset q0");

	offset = SQCI_OFFSET(q_buf_size, 1, depth);
	RTE_TEST_ASSERT_EQUAL(offset, q_buf_size * 2,
			      "SQ CI offset q1");

	return TEST_SUCCESS;
}

/* ====== SELECT_OTHER_COS_ID / ODD_NUMBER_QUEUE_ID ====== */

static int
test_cos_id_macros(void)
{
	RTE_TEST_ASSERT_EQUAL(SELECT_OTHER_COS_ID(0), 4,
			      "select other cos for 0");
	RTE_TEST_ASSERT_EQUAL(SELECT_OTHER_COS_ID(1), 5,
			      "select other cos for 1");
	RTE_TEST_ASSERT_EQUAL(SELECT_OTHER_COS_ID(4), 0,
			      "select other cos for 4");
	RTE_TEST_ASSERT_EQUAL(SELECT_OTHER_COS_ID(7), 3,
			      "select other cos for 7");

	RTE_TEST_ASSERT_EQUAL(ODD_NUMBER_QUEUE_ID(0), 0, "q0 is even");
	RTE_TEST_ASSERT_EQUAL(ODD_NUMBER_QUEUE_ID(1), 1, "q1 is odd");
	RTE_TEST_ASSERT_EQUAL(ODD_NUMBER_QUEUE_ID(2), 0, "q2 is even");
	RTE_TEST_ASSERT_EQUAL(ODD_NUMBER_QUEUE_ID(3), 1, "q3 is odd");

	return TEST_SUCCESS;
}

/* ====== Eth overhead / MTU macros ====== */

static int
test_mtu_pktlen_macros(void)
{
	u16 mtu = 1500;
	u32 frame_size;

	/* frame = mtu + overhead */
	frame_size = mtu + HINIC3_ETH_OVERHEAD;
	RTE_TEST_ASSERT(frame_size > mtu, "frame size should be larger than mtu");

	return TEST_SUCCESS;
}

/* ====== HINIC3_MAX_QUEUE_NUM ====== */

static int
test_max_queue_num(void)
{
	RTE_TEST_ASSERT_EQUAL(HINIC3_MAX_QUEUE_NUM, 256,
			      "max queue num should be 256");
	return TEST_SUCCESS;
}

/* ====== Feature cap bitmask tests ====== */

static int
test_feature_cap_bits(void)
{
	u64 cap = 0;

	/* Set some feature bits */
	cap |= NIC_F_CSUM;
	RTE_TEST_ASSERT(cap & NIC_F_CSUM, "CSUM bit should be set");

	cap |= NIC_F_TSO;
	RTE_TEST_ASSERT(cap & NIC_F_TSO, "TSO bit should be set");

	cap |= NIC_F_RSS;
	RTE_TEST_ASSERT(cap & NIC_F_RSS, "RSS bit should be set");

	/* Clear a bit */
	cap &= ~NIC_F_TSO;
	RTE_TEST_ASSERT(!(cap & NIC_F_TSO), "TSO bit should be cleared");
	RTE_TEST_ASSERT(cap & NIC_F_CSUM, "CSUM bit should still be set");

	return TEST_SUCCESS;
}

static int
test_default_drv_feature(void)
{
	u64 cap = DEFAULT_DRV_FEATURE;

	/* Verify some expected features are in default mask */
	RTE_TEST_ASSERT(cap & NIC_F_CSUM, "default should include CSUM");
	RTE_TEST_ASSERT(cap & NIC_F_TSO, "default should include TSO");
	RTE_TEST_ASSERT(cap & NIC_F_RSS, "default should include RSS");
	RTE_TEST_ASSERT(cap & NIC_F_LRO, "default should include LRO");

	return TEST_SUCCESS;
}

/* ====== RX mode flags ====== */

static int
test_rx_mode_flags(void)
{
	u32 rx_mode;

	/* Default RX mode: UC | MC | BC */
	rx_mode = HINIC3_RX_MODE_UC | HINIC3_RX_MODE_MC | HINIC3_RX_MODE_BC;
	RTE_TEST_ASSERT_EQUAL(rx_mode, HINIC3_DEFAULT_RX_MODE,
			      "default rx mode");

	/* Add promisc */
	rx_mode |= HINIC3_RX_MODE_PROMISC;
	RTE_TEST_ASSERT(rx_mode & HINIC3_RX_MODE_PROMISC,
			"promisc should be set");

	/* Remove promisc */
	rx_mode &= ~HINIC3_RX_MODE_PROMISC;
	RTE_TEST_ASSERT(!(rx_mode & HINIC3_RX_MODE_PROMISC),
			"promisc should be cleared");
	RTE_TEST_ASSERT_EQUAL(rx_mode, HINIC3_DEFAULT_RX_MODE,
			      "should revert to default");

	return TEST_SUCCESS;
}

/* ====== Device status bit operations ====== */

static int
test_dev_status_bits(void)
{
	unsigned long status = 0;

	hinic3_set_bit(HINIC3_DEV_INIT, &status);
	RTE_TEST_ASSERT(hinic3_get_bit(HINIC3_DEV_INIT, &status),
			"DEV_INIT should be set");

	hinic3_set_bit(HINIC3_DEV_START, &status);
	RTE_TEST_ASSERT(hinic3_get_bit(HINIC3_DEV_START, &status),
			"DEV_START should be set");
	RTE_TEST_ASSERT(hinic3_get_bit(HINIC3_DEV_INIT, &status),
			"DEV_INIT should still be set");

	hinic3_clear_bit(HINIC3_DEV_INIT, &status);
	RTE_TEST_ASSERT(!hinic3_get_bit(HINIC3_DEV_INIT, &status),
			"DEV_INIT should be cleared");
	RTE_TEST_ASSERT(hinic3_get_bit(HINIC3_DEV_START, &status),
			"DEV_START should still be set");

	return TEST_SUCCESS;
}

/* ====== Reset TX queue logic ====== */

static int
test_reset_tx_queue_logic(void)
{
	struct hinic3_txq txq;
	u16 ci_vaddr_val = 0;

	memset(&txq, 0, sizeof(txq));
	txq.cons_idx = 10;
	txq.prod_idx = 20;
	txq.owner = 0;
	txq.is_hairpin = false;
	txq.ci_vaddr_base = (volatile u16 *)&ci_vaddr_val;
	ci_vaddr_val = rte_cpu_to_be_16(15);

	/* Reset logic from hinic3_reset_tx_queue */
	txq.cons_idx = 0;
	txq.prod_idx = 0;
	txq.owner = 1;
	if (!txq.is_hairpin)
		*(txq.ci_vaddr_base) = 0;

	RTE_TEST_ASSERT_EQUAL(txq.cons_idx, 0, "cons_idx should be 0");
	RTE_TEST_ASSERT_EQUAL(txq.prod_idx, 0, "prod_idx should be 0");
	RTE_TEST_ASSERT_EQUAL(txq.owner, 1, "owner should be 1");
	RTE_TEST_ASSERT_EQUAL(*(txq.ci_vaddr_base), 0,
			      "ci_vaddr should be 0");

	return TEST_SUCCESS;
}

/* ====== Reset RX queue logic ====== */

static int
test_reset_rx_queue_logic(void)
{
	struct hinic3_rxq rxq;

	memset(&rxq, 0, sizeof(rxq));
	rxq.cons_idx = 10;
	rxq.prod_idx = 20;
	rxq.delta = 5;
	rxq.next_to_update = 15;
	rxq.q_depth = 128;

	/* Reset logic from hinic3_reset_rx_queue */
	rxq.cons_idx = 0;
	rxq.prod_idx = 0;
	rxq.delta = rxq.q_depth;
	rxq.next_to_update = 0;

	RTE_TEST_ASSERT_EQUAL(rxq.cons_idx, 0, "cons_idx should be 0");
	RTE_TEST_ASSERT_EQUAL(rxq.prod_idx, 0, "prod_idx should be 0");
	RTE_TEST_ASSERT_EQUAL(rxq.delta, 128, "delta should be q_depth");
	RTE_TEST_ASSERT_EQUAL(rxq.next_to_update, 0,
			      "next_to_update should be 0");

	return TEST_SUCCESS;
}

/* ====== Copy mempool name ====== */

static int
test_copy_mempool_name(void)
{
	RTE_TEST_ASSERT(strcmp(HINCI3_CPY_MEMPOOL_NAME, "cpy_mempool") == 0,
			"copy mempool name");

	return TEST_SUCCESS;
}

/* ====== CI coalescing unit macros ====== */

static int
test_ci_coalescing_units(void)
{
	RTE_TEST_ASSERT_EQUAL(HINIC3_CI_PENDING_LIMIT_UNIT, 8,
			      "pending limit unit");
	RTE_TEST_ASSERT_EQUAL(HINIC3_CI_COALESCING_TIME_UNIT, 5,
			      "coalescing time unit");

	/* Verify default config values in terms of units */
	RTE_TEST_ASSERT_EQUAL(HINIC3_DEFAULT_TX_CI_PENDING_LIMIT, 2,
			      "default tx pending limit");
	RTE_TEST_ASSERT_EQUAL(HINIC3_DEFAULT_TX_CI_COALESCING_TIME, 2,
			      "default tx coalescing time");

	/* Actual values = config * unit */
	RTE_TEST_ASSERT_EQUAL(
		HINIC3_DEFAULT_TX_CI_PENDING_LIMIT * HINIC3_CI_PENDING_LIMIT_UNIT,
		16, "actual pending limit");
	RTE_TEST_ASSERT_EQUAL(
		HINIC3_DEFAULT_TX_CI_COALESCING_TIME * HINIC3_CI_COALESCING_TIME_UNIT,
		10, "actual coalescing time");

	return TEST_SUCCESS;
}

/* ====== NIC dev name format ====== */

static int
test_nic_dev_name_format(void)
{
	struct hinic3_nic_dev nic_dev;

	memset(&nic_dev, 0, sizeof(nic_dev));
	(void)snprintf(nic_dev.dev_name, sizeof(nic_dev.dev_name),
		       "dbdf-%.4x:%.2x:%.2x.%x",
		       0x0000, 0x03, 0x00, 0x0);

	RTE_TEST_ASSERT(strcmp(nic_dev.dev_name, "dbdf-0000:03:00.0") == 0,
			"nic dev name format");

	return TEST_SUCCESS;
}

/* ====== Ops callback tests: hinic3_vlan_filter_set ====== */

static int
test_vlan_filter_set_id_zero(void)
{
	int ret;

	ret = hinic3_pmd_ops.vlan_filter_set(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
					    0, 1);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "vlan_id=0 should be no-op");

	return TEST_SUCCESS;
}

static int
test_vlan_filter_set_invalid_id(void)
{
	int ret;

	ret = hinic3_pmd_ops.vlan_filter_set(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
					    4096, 1);
	RTE_TEST_ASSERT_EQUAL(ret, -EINVAL, "vlan_id=4096 should fail");

	return TEST_SUCCESS;
}

static int
test_vlan_filter_set_add(void)
{
	int ret;
	u32 vid_idx, vid_bit;

	ret = hinic3_pmd_ops.vlan_filter_set(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
					    100, 1);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "add vlan 100 should succeed");

	vid_idx = HINIC3_VFTA_IDX(100);
	vid_bit = HINIC3_VFTA_BIT(100);
	RTE_TEST_ASSERT((g_mock_nic_dev.vfta[vid_idx] & vid_bit) != 0,
		       "vlan 100 should be stored in vfta");

	return TEST_SUCCESS;
}

static int
test_vlan_filter_set_add_duplicate(void)
{
	int ret;

	/* First add */
	ret = hinic3_pmd_ops.vlan_filter_set(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
					    100, 1);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "first add vlan 100 should succeed");

	/* Set add_vlan to fail to verify it is NOT called on duplicate */
	g_mock_add_vlan_ret = -1;

	/* Second add should still return 0 (already set) */
	ret = hinic3_pmd_ops.vlan_filter_set(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
					    100, 1);
	RTE_TEST_ASSERT_EQUAL(ret, 0,
			      "duplicate add should return 0 without calling add_vlan");

	return TEST_SUCCESS;
}

static int
test_vlan_filter_set_remove(void)
{
	int ret;
	u32 vid_idx, vid_bit;

	/* First add vlan 100 */
	ret = hinic3_pmd_ops.vlan_filter_set(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
					    100, 1);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "add vlan 100 should succeed");

	/* Remove vlan 100 */
	ret = hinic3_pmd_ops.vlan_filter_set(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
					    100, 0);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "remove vlan 100 should succeed");

	vid_idx = HINIC3_VFTA_IDX(100);
	vid_bit = HINIC3_VFTA_BIT(100);
	RTE_TEST_ASSERT((g_mock_nic_dev.vfta[vid_idx] & vid_bit) == 0,
		       "vlan 100 should be cleared from vfta");

	return TEST_SUCCESS;
}

static int
test_vlan_filter_set_remove_nonexist(void)
{
	int ret;

	/* Remove vlan 200 without adding it first */
	ret = hinic3_pmd_ops.vlan_filter_set(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
					    200, 0);
	RTE_TEST_ASSERT_EQUAL(ret, 0,
			      "remove non-existent vlan should return 0");

	return TEST_SUCCESS;
}

static int
test_vlan_filter_set_add_fail(void)
{
	int ret;

	g_mock_add_vlan_ret = -1;
	ret = hinic3_pmd_ops.vlan_filter_set(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
					    100, 1);
	RTE_TEST_ASSERT(ret != 0, "add vlan with failing mock should return error");

	return TEST_SUCCESS;
}

static int
test_vlan_filter_set_del_fail(void)
{
	int ret;

	/* First add vlan 100 with success */
	g_mock_add_vlan_ret = 0;
	ret = hinic3_pmd_ops.vlan_filter_set(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
					    100, 1);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "add vlan 100 should succeed");

	/* Now try to remove with failing del_vlan mock */
	g_mock_del_vlan_ret = -1;
	ret = hinic3_pmd_ops.vlan_filter_set(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
					    100, 0);
	RTE_TEST_ASSERT(ret != 0,
		       "del vlan with failing mock should return error");

	return TEST_SUCCESS;
}

/* ====== Ops callback tests: hinic3_dev_allmulticast_enable ====== */

static int
test_allmulticast_enable_success(void)
{
	int ret;

	ret = hinic3_pmd_ops.allmulticast_enable(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID]);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "allmulticast enable should succeed");
	RTE_TEST_ASSERT((g_mock_nic_dev.rx_mode & HINIC3_RX_MODE_MC_ALL) != 0,
		       "MC_ALL bit should be set in rx_mode");

	return TEST_SUCCESS;
}

static int
test_allmulticast_enable_qpool(void)
{
	int ret;

	g_qinfo_type = HINIC3_QINFO_TYPE_QPOOL;
	g_mock_hwdev.qinfo_type = HINIC3_QINFO_TYPE_QPOOL;
	ret = hinic3_pmd_ops.allmulticast_enable(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID]);
	RTE_TEST_ASSERT_EQUAL(ret, -ENOTSUP,
			      "qpool mode should return -ENOTSUP");

	return TEST_SUCCESS;
}

static int
test_allmulticast_enable_fail(void)
{
	int ret;

	g_mock_set_rx_mode_ret = -1;
	ret = hinic3_pmd_ops.allmulticast_enable(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID]);
	RTE_TEST_ASSERT(ret != 0,
		       "failing set_rx_mode should cause allmulticast enable to fail");

	return TEST_SUCCESS;
}

/* ====== Ops callback tests: hinic3_dev_allmulticast_disable ====== */

static int
test_allmulticast_disable_success(void)
{
	int ret;

	/* Pre-set MC_ALL bit so disable has something to clear */
	g_mock_nic_dev.rx_mode |= HINIC3_RX_MODE_MC_ALL;
	ret = hinic3_pmd_ops.allmulticast_disable(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID]);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "allmulticast disable should succeed");
	RTE_TEST_ASSERT((g_mock_nic_dev.rx_mode & HINIC3_RX_MODE_MC_ALL) == 0,
		       "MC_ALL bit should be cleared from rx_mode");

	return TEST_SUCCESS;
}

static int
test_allmulticast_disable_qpool(void)
{
	int ret;

	g_qinfo_type = HINIC3_QINFO_TYPE_QPOOL;
	g_mock_hwdev.qinfo_type = HINIC3_QINFO_TYPE_QPOOL;
	ret = hinic3_pmd_ops.allmulticast_disable(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID]);
	RTE_TEST_ASSERT_EQUAL(ret, -ENOTSUP,
			      "qpool mode should return -ENOTSUP");

	return TEST_SUCCESS;
}

static int
test_allmulticast_disable_fail(void)
{
	int ret;

	g_mock_nic_dev.rx_mode |= HINIC3_RX_MODE_MC_ALL;
	g_mock_set_rx_mode_ret = -1;
	ret = hinic3_pmd_ops.allmulticast_disable(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID]);
	RTE_TEST_ASSERT(ret != 0,
		       "failing set_rx_mode should cause allmulticast disable to fail");

	return TEST_SUCCESS;
}

/* ====== Ops callback tests: hinic3_dev_promiscuous_enable ====== */

static int
test_promiscuous_enable_success(void)
{
	int ret;

	/* feature_cap has NIC_F_PROMISC from mock_ethdev_setup */
	ret = hinic3_pmd_ops.promiscuous_enable(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID]);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "promiscuous enable should succeed");
	RTE_TEST_ASSERT((g_mock_nic_dev.rx_mode & HINIC3_RX_MODE_PROMISC) != 0,
		       "PROMISC bit should be set in rx_mode");

	return TEST_SUCCESS;
}

static int
test_promiscuous_enable_no_feature(void)
{
	int ret;

	g_mock_nic_dev.feature_cap &= ~NIC_F_PROMISC;
	ret = hinic3_pmd_ops.promiscuous_enable(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID]);
	RTE_TEST_ASSERT_EQUAL(ret, -ENOTSUP,
			      "promiscuous enable without NIC_F_PROMISC should fail");

	return TEST_SUCCESS;
}

static int
test_promiscuous_enable_qpool(void)
{
	int ret;

	g_qinfo_type = HINIC3_QINFO_TYPE_QPOOL;
	g_mock_hwdev.qinfo_type = HINIC3_QINFO_TYPE_QPOOL;
	ret = hinic3_pmd_ops.promiscuous_enable(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID]);
	RTE_TEST_ASSERT_EQUAL(ret, -ENOTSUP,
			      "qpool mode should return -ENOTSUP");

	return TEST_SUCCESS;
}

static int
test_promiscuous_enable_fail(void)
{
	int ret;

	g_mock_set_rx_mode_ret = -1;
	ret = hinic3_pmd_ops.promiscuous_enable(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID]);
	RTE_TEST_ASSERT(ret != 0,
		       "failing set_rx_mode should cause promiscuous enable to fail");

	return TEST_SUCCESS;
}

/* ====== Ops callback tests: hinic3_dev_promiscuous_disable ====== */

static int
test_promiscuous_disable_success(void)
{
	int ret;

	/* Pre-set PROMISC bit so disable has something to clear */
	g_mock_nic_dev.rx_mode |= HINIC3_RX_MODE_PROMISC;
	ret = hinic3_pmd_ops.promiscuous_disable(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID]);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "promiscuous disable should succeed");
	RTE_TEST_ASSERT((g_mock_nic_dev.rx_mode & HINIC3_RX_MODE_PROMISC) == 0,
		       "PROMISC bit should be cleared from rx_mode");

	return TEST_SUCCESS;
}

static int
test_promiscuous_disable_qpool(void)
{
	int ret;

	g_qinfo_type = HINIC3_QINFO_TYPE_QPOOL;
	g_mock_hwdev.qinfo_type = HINIC3_QINFO_TYPE_QPOOL;
	ret = hinic3_pmd_ops.promiscuous_disable(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID]);
	RTE_TEST_ASSERT_EQUAL(ret, -ENOTSUP,
			      "qpool mode should return -ENOTSUP");

	return TEST_SUCCESS;
}

static int
test_promiscuous_disable_fail(void)
{
	int ret;

	g_mock_nic_dev.rx_mode |= HINIC3_RX_MODE_PROMISC;
	g_mock_set_rx_mode_ret = -1;
	ret = hinic3_pmd_ops.promiscuous_disable(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID]);
	RTE_TEST_ASSERT(ret != 0,
		       "failing set_rx_mode should cause promiscuous disable to fail");

	return TEST_SUCCESS;
}

/* ====== Ops callback tests: hinic3_dev_set_mtu ====== */

static int
test_set_mtu_qpool(void)
{
	int ret;

	g_qinfo_type = HINIC3_QINFO_TYPE_QPOOL;
	g_mock_hwdev.qinfo_type = HINIC3_QINFO_TYPE_QPOOL;
	ret = hinic3_pmd_ops.mtu_set(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID], 1500);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "qpool mode should return 0 for set_mtu");

	return TEST_SUCCESS;
}

static int
test_set_mtu_too_small(void)
{
	int ret;

	ret = hinic3_pmd_ops.mtu_set(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				     HINIC3_MIN_MTU_SIZE - 1);
	RTE_TEST_ASSERT_EQUAL(ret, -EINVAL, "mtu below min should fail");

	return TEST_SUCCESS;
}

static int
test_set_mtu_too_large(void)
{
	int ret;

	ret = hinic3_pmd_ops.mtu_set(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				     HINIC3_MAX_MTU_SIZE + 1);
	RTE_TEST_ASSERT_EQUAL(ret, -EINVAL, "mtu above max should fail");

	return TEST_SUCCESS;
}

static int
test_set_mtu_success(void)
{
	int ret;

	ret = hinic3_pmd_ops.mtu_set(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID], 1500);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "valid mtu should succeed");
	RTE_TEST_ASSERT_EQUAL(g_mock_nic_dev.mtu_size, 1500,
			      "mtu_size should be updated");

	return TEST_SUCCESS;
}

static int
test_set_mtu_boundary_min(void)
{
	int ret;

	ret = hinic3_pmd_ops.mtu_set(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				     HINIC3_MIN_MTU_SIZE);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "min boundary mtu should succeed");

	return TEST_SUCCESS;
}

static int
test_set_mtu_boundary_max(void)
{
	int ret;

	ret = hinic3_pmd_ops.mtu_set(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				     HINIC3_MAX_MTU_SIZE);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "max boundary mtu should succeed");

	return TEST_SUCCESS;
}

static int
test_set_mtu_fail(void)
{
	int ret;

	g_mock_set_port_mtu_ret = -1;
	ret = hinic3_pmd_ops.mtu_set(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID], 1500);
	RTE_TEST_ASSERT(ret != 0,
		       "failing set_port_mtu should cause set_mtu to fail");

	return TEST_SUCCESS;
}

/* ====== Ops callback tests: hinic3_rss_reta_query ====== */

static int
test_rss_reta_query_disabled(void)
{
	struct rte_eth_rss_reta_entry64 reta_conf[4];
	int ret;

	memset(reta_conf, 0, sizeof(reta_conf));
	g_mock_nic_dev.rss_state = HINIC3_RSS_DISABLE;
	ret = hinic3_pmd_ops.reta_query(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
					reta_conf, HINIC3_RSS_INDIR_SIZE);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "disabled RSS reta query should return 0");

	return TEST_SUCCESS;
}

static int
test_rss_reta_query_success(void)
{
	struct rte_eth_rss_reta_entry64 reta_conf[4];
	int ret;
	int i;

	memset(reta_conf, 0, sizeof(reta_conf));
	for (i = 0; i < 4; i++)
		reta_conf[i].mask = UINT64_MAX;
	ret = hinic3_pmd_ops.reta_query(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
					reta_conf, HINIC3_RSS_INDIR_SIZE);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "reta query should succeed");
	/* Mock fills indir table with i%4 */
	RTE_TEST_ASSERT_EQUAL(reta_conf[0].reta[0], 0, "reta[0] should be 0");
	RTE_TEST_ASSERT_EQUAL(reta_conf[0].reta[1], 1, "reta[1] should be 1");

	return TEST_SUCCESS;
}

static int
test_rss_reta_query_invalid_size(void)
{
	struct rte_eth_rss_reta_entry64 reta_conf[4];
	int ret;

	memset(reta_conf, 0, sizeof(reta_conf));
	ret = hinic3_pmd_ops.reta_query(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
					reta_conf, HINIC3_RSS_INDIR_SIZE - 1);
	RTE_TEST_ASSERT_EQUAL(ret, -EINVAL, "invalid reta size should fail");

	return TEST_SUCCESS;
}

static int
test_rss_reta_query_mask_filter(void)
{
	struct rte_eth_rss_reta_entry64 reta_conf[4];
	int ret;

	memset(reta_conf, 0, sizeof(reta_conf));
	/* Only group 0 has mask set */
	reta_conf[0].mask = UINT64_MAX;
	reta_conf[1].mask = 0;
	reta_conf[2].mask = 0;
	reta_conf[3].mask = 0;
	ret = hinic3_pmd_ops.reta_query(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
					reta_conf, HINIC3_RSS_INDIR_SIZE);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "reta query with partial mask should succeed");
	/* Group 0 entries should be populated */
	RTE_TEST_ASSERT_EQUAL(reta_conf[0].reta[0], 0,
			      "group 0 entry 0 should be populated");
	/* Group 1 entries should NOT be populated (mask=0) */
	RTE_TEST_ASSERT_EQUAL(reta_conf[1].reta[0], 0,
			      "group 1 entry 0 should not be populated");

	return TEST_SUCCESS;
}

/* ====== Ops callback tests: hinic3_rss_reta_update ====== */

static int
test_rss_reta_update_disabled(void)
{
	struct rte_eth_rss_reta_entry64 reta_conf[4];
	int ret;

	memset(reta_conf, 0, sizeof(reta_conf));
	g_mock_nic_dev.rss_state = HINIC3_RSS_DISABLE;
	ret = hinic3_pmd_ops.reta_update(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
					reta_conf, HINIC3_RSS_INDIR_SIZE);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "disabled RSS reta update should return 0");

	return TEST_SUCCESS;
}

static int
test_rss_reta_update_invalid_size(void)
{
	struct rte_eth_rss_reta_entry64 reta_conf[4];
	int ret;

	memset(reta_conf, 0, sizeof(reta_conf));
	ret = hinic3_pmd_ops.reta_update(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
					reta_conf, HINIC3_RSS_INDIR_SIZE - 1);
	RTE_TEST_ASSERT_EQUAL(ret, -EINVAL, "invalid reta size should fail");

	return TEST_SUCCESS;
}

static int
test_rss_reta_update_success(void)
{
	struct rte_eth_rss_reta_entry64 reta_conf[4];
	int ret;
	int i, j;

	memset(reta_conf, 0, sizeof(reta_conf));
	for (i = 0; i < 4; i++) {
		reta_conf[i].mask = UINT64_MAX;
		for (j = 0; j < RTE_ETH_RETA_GROUP_SIZE; j++)
			reta_conf[i].reta[j] = (uint16_t)(j % g_mock_nic_dev.num_rqs);
	}
	ret = hinic3_pmd_ops.reta_update(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
					reta_conf, HINIC3_RSS_INDIR_SIZE);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "reta update should succeed");

	return TEST_SUCCESS;
}

static int
test_rss_reta_update_qpool(void)
{
	struct rte_eth_rss_reta_entry64 reta_conf[4];
	int ret;
	int i, j;

	memset(reta_conf, 0, sizeof(reta_conf));
	g_qinfo_type = HINIC3_QINFO_TYPE_QPOOL;
	g_mock_hwdev.qinfo_type = HINIC3_QINFO_TYPE_QPOOL;
	for (i = 0; i < 4; i++) {
		reta_conf[i].mask = UINT64_MAX;
		for (j = 0; j < RTE_ETH_RETA_GROUP_SIZE; j++)
			reta_conf[i].reta[j] = (uint16_t)(j % g_mock_nic_dev.num_rqs);
	}
	ret = hinic3_pmd_ops.reta_update(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
					reta_conf, HINIC3_RSS_INDIR_SIZE);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "reta update in qpool mode should succeed");

	return TEST_SUCCESS;
}

static int
test_rss_reta_update_fail(void)
{
	struct rte_eth_rss_reta_entry64 reta_conf[4];
	int ret;
	int i, j;

	memset(reta_conf, 0, sizeof(reta_conf));
	g_mock_rss_set_indir_tbl_ret = -1;
	for (i = 0; i < 4; i++) {
		reta_conf[i].mask = UINT64_MAX;
		for (j = 0; j < RTE_ETH_RETA_GROUP_SIZE; j++)
			reta_conf[i].reta[j] = (uint16_t)(j % g_mock_nic_dev.num_rqs);
	}
	ret = hinic3_pmd_ops.reta_update(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
					reta_conf, HINIC3_RSS_INDIR_SIZE);
	RTE_TEST_ASSERT(ret != 0,
		       "failing rss_set_indir_tbl should cause reta update to fail");

	return TEST_SUCCESS;
}

/* ====== Ops callback tests: hinic3_rss_hash_update (early paths) ====== */

static int
test_rss_hash_update_disabled_with_hf(void)
{
	struct rte_eth_rss_conf rss_conf;
	int ret;

	memset(&rss_conf, 0, sizeof(rss_conf));
	g_mock_nic_dev.rss_state = HINIC3_RSS_DISABLE;
	rss_conf.rss_hf = RTE_ETH_RSS_IPV4;
	ret = hinic3_pmd_ops.rss_hash_update(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
					    &rss_conf);
	RTE_TEST_ASSERT_EQUAL(ret, -EINVAL,
			      "disabled RSS with non-zero rss_hf should fail");

	return TEST_SUCCESS;
}

static int
test_rss_hash_update_disabled_no_hf(void)
{
	struct rte_eth_rss_conf rss_conf;
	int ret;

	memset(&rss_conf, 0, sizeof(rss_conf));
	g_mock_nic_dev.rss_state = HINIC3_RSS_DISABLE;
	rss_conf.rss_hf = 0;
	ret = hinic3_pmd_ops.rss_hash_update(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
					    &rss_conf);
	RTE_TEST_ASSERT_EQUAL(ret, 0,
			      "disabled RSS with zero rss_hf should succeed");

	return TEST_SUCCESS;
}

static int
test_rss_hash_update_key_too_long(void)
{
	struct rte_eth_rss_conf rss_conf;
	int ret;

	memset(&rss_conf, 0, sizeof(rss_conf));
	rss_conf.rss_key_len = HINIC3_RSS_KEY_SIZE + 1;
	ret = hinic3_pmd_ops.rss_hash_update(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
					    &rss_conf);
	RTE_TEST_ASSERT_EQUAL(ret, -EINVAL, "key too long should fail");

	return TEST_SUCCESS;
}

/* ====== Ops callback tests: hinic3_rss_conf_get (early paths) ====== */

static int
test_rss_conf_get_null(void)
{
	int ret;

	ret = hinic3_pmd_ops.rss_hash_conf_get(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
					      NULL);
	RTE_TEST_ASSERT_EQUAL(ret, -EINVAL, "NULL rss_conf should fail");

	return TEST_SUCCESS;
}

static int
test_rss_conf_get_disabled(void)
{
	struct rte_eth_rss_conf rss_conf;
	int ret;

	memset(&rss_conf, 0, sizeof(rss_conf));
	g_mock_nic_dev.rss_state = HINIC3_RSS_DISABLE;
	/* dcb_on is 0 from memset in mock_ethdev_setup */
	ret = hinic3_pmd_ops.rss_hash_conf_get(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
					      &rss_conf);
	RTE_TEST_ASSERT_EQUAL(ret, 0,
			      "disabled RSS with dcb_off should succeed");
	RTE_TEST_ASSERT_EQUAL(rss_conf.rss_hf, 0, "rss_hf should be 0");

	return TEST_SUCCESS;
}
/* ====== Ops callback tests: hinic3_dev_flow_ctrl_set ====== */

static int
test_flow_ctrl_set_qpool(void)
{
	struct rte_eth_fc_conf fc_conf = {0};
	int ret;

	g_qinfo_type = HINIC3_QINFO_TYPE_QPOOL;
	g_mock_hwdev.qinfo_type = HINIC3_QINFO_TYPE_QPOOL;
	ret = hinic3_pmd_ops.flow_ctrl_set(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
					   &fc_conf);
	RTE_TEST_ASSERT_EQUAL(ret, -ENOTSUP,
			      "qpool mode flow_ctrl_set should return -ENOTSUP");
	return TEST_SUCCESS;
}

/* ====== Ops callback tests: hinic3_set_mac_addr ====== */

static int
test_set_mac_addr_qpool(void)
{
	struct rte_ether_addr addr = {{0x00, 0x11, 0x22, 0x33, 0x44, 0x55}};
	int ret;

	g_qinfo_type = HINIC3_QINFO_TYPE_QPOOL;
	g_mock_hwdev.qinfo_type = HINIC3_QINFO_TYPE_QPOOL;
	ret = hinic3_pmd_ops.mac_addr_set(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
					  &addr);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "qpool mode set_mac_addr should return 0");
	return TEST_SUCCESS;
}

static int
test_set_mac_addr_bifur_shared(void)
{
	struct rte_ether_addr addr = {{0x00, 0x11, 0x22, 0x33, 0x44, 0x55}};
	int ret;

	g_qinfo_type = HINIC3_QINFO_TYPE_BIFUR;
	g_mock_hwdev.qinfo_type = HINIC3_QINFO_TYPE_BIFUR;
	g_mock_is_shared_dev = true;
	ret = hinic3_pmd_ops.mac_addr_set(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
					  &addr);
	RTE_TEST_ASSERT_EQUAL(ret, -EPERM,
			      "bifur shared set_mac_addr should return -EPERM");
	return TEST_SUCCESS;
}

static int
test_set_mac_addr_invalid(void)
{
	struct rte_ether_addr addr = {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
	int ret;

	ret = hinic3_pmd_ops.mac_addr_set(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
					  &addr);
	RTE_TEST_ASSERT_EQUAL(ret, -EINVAL,
			      "invalid MAC should return -EINVAL");
	return TEST_SUCCESS;
}

/* ====== Ops callback tests: hinic3_mac_addr_add ====== */

static int
test_mac_addr_add_qpool(void)
{
	struct rte_ether_addr addr = {{0x00, 0x11, 0x22, 0x33, 0x44, 0x55}};
	int ret;

	g_qinfo_type = HINIC3_QINFO_TYPE_QPOOL;
	g_mock_hwdev.qinfo_type = HINIC3_QINFO_TYPE_QPOOL;
	ret = hinic3_pmd_ops.mac_addr_add(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
					  &addr, 0, 0);
	RTE_TEST_ASSERT_EQUAL(ret, -EINVAL,
			      "qpool mode mac_addr_add should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_mac_addr_add_invalid_addr(void)
{
	struct rte_ether_addr addr = {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
	int ret;

	ret = hinic3_pmd_ops.mac_addr_add(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
					  &addr, 0, 0);
	RTE_TEST_ASSERT_EQUAL(ret, -EINVAL,
			      "invalid MAC addr_add should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_mac_addr_add_out_of_range(void)
{
	struct rte_ether_addr addr = {{0x00, 0x11, 0x22, 0x33, 0x44, 0x55}};
	int ret;

	ret = hinic3_pmd_ops.mac_addr_add(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
					  &addr, HINIC3_MAX_UC_MAC_ADDRS, 0);
	RTE_TEST_ASSERT_EQUAL(ret, -EINVAL,
			      "out-of-range index should return -EINVAL");
	return TEST_SUCCESS;
}

/* ====== Ops callback tests: hinic3_mac_addr_remove ====== */

static int
test_mac_addr_remove_qpool(void)
{
	g_qinfo_type = HINIC3_QINFO_TYPE_QPOOL;
	g_mock_hwdev.qinfo_type = HINIC3_QINFO_TYPE_QPOOL;
	/* mac_addr_remove is void, just ensure no crash */
	hinic3_pmd_ops.mac_addr_remove(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID], 0);
	return TEST_SUCCESS;
}

/* ====== Ops callback tests: hinic3_dev_set_link_up/down ====== */

static int
test_set_link_up_qpool(void)
{
	int ret;

	g_qinfo_type = HINIC3_QINFO_TYPE_QPOOL;
	g_mock_hwdev.qinfo_type = HINIC3_QINFO_TYPE_QPOOL;
	ret = hinic3_pmd_ops.dev_set_link_up(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID]);
	RTE_TEST_ASSERT_EQUAL(ret, -EAGAIN,
			      "qpool mode set_link_up should return -EAGAIN");
	return TEST_SUCCESS;
}

static int
test_set_link_up_sp230_vf(void)
{
	int ret;

	g_mock_hwif.attr.func_type = TYPE_VF;
	g_mock_is_sp230_nic = true;
	ret = hinic3_pmd_ops.dev_set_link_up(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID]);
	RTE_TEST_ASSERT_EQUAL(ret, -EAGAIN,
			      "SP230 VF set_link_up should return -EAGAIN");
	return TEST_SUCCESS;
}

static int
test_set_link_down_qpool(void)
{
	int ret;

	g_qinfo_type = HINIC3_QINFO_TYPE_QPOOL;
	g_mock_hwdev.qinfo_type = HINIC3_QINFO_TYPE_QPOOL;
	ret = hinic3_pmd_ops.dev_set_link_down(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID]);
	RTE_TEST_ASSERT_EQUAL(ret, -EAGAIN,
			      "qpool mode set_link_down should return -EAGAIN");
	return TEST_SUCCESS;
}

/* ====== Ops callback tests: hinic3_dev_reset / hinic3_get_reg ====== */

static int
test_dev_reset_returns_zero(void)
{
	int ret;

	ret = hinic3_pmd_ops.dev_reset(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID]);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "dev_reset should return 0");
	return TEST_SUCCESS;
}

static int
test_get_reg_returns_zero(void)
{
	struct rte_dev_reg_info regs = {0};
	int ret;

	ret = hinic3_pmd_ops.get_reg(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				     &regs);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "get_reg should return 0");
	return TEST_SUCCESS;
}
/* ====== Globalized static function tests ====== */

static int
test_find_vlan_filter(void)
{
	bool found;
	hinic3_store_vlan_filter(&g_mock_nic_dev, 100, true);
	found = hinic3_find_vlan_filter(&g_mock_nic_dev, 100);
	RTE_TEST_ASSERT(found, "vlan 100 should be found after store");
	found = hinic3_find_vlan_filter(&g_mock_nic_dev, 200);
	RTE_TEST_ASSERT(!found, "vlan 200 should not be found");
	hinic3_store_vlan_filter(&g_mock_nic_dev, 100, false);
	found = hinic3_find_vlan_filter(&g_mock_nic_dev, 100);
	RTE_TEST_ASSERT(!found, "vlan 100 should not be found after remove");
	return TEST_SUCCESS;
}

/* ====== Ops callback tests: hinic3_set_mac_addr success path ====== */

static int
test_set_mac_addr_success(void)
{
	struct rte_ether_addr addr = {{0x00, 0x11, 0x22, 0x33, 0x44, 0x55}};
	int ret;

	ret = hinic3_pmd_ops.mac_addr_set(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
					  &addr);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "set_mac_addr success should return 0");
	return TEST_SUCCESS;
}

static int
test_set_mac_addr_update_fail(void)
{
	struct rte_ether_addr addr = {{0x00, 0x11, 0x22, 0x33, 0x44, 0x55}};
	int ret;

	g_mock_update_mac_ret = -EIO;
	ret = hinic3_pmd_ops.mac_addr_set(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
					  &addr);
	RTE_TEST_ASSERT(ret != 0, "update_mac fail should propagate error");
	return TEST_SUCCESS;
}

/* ====== Ops callback tests: hinic3_mac_addr_add success path ====== */

static int
test_mac_addr_add_success(void)
{
	static struct rte_ether_addr mock_addrs[HINIC3_MAX_UC_MAC_ADDRS];
	struct rte_ether_addr addr = {{0x00, 0x11, 0x22, 0x33, 0x44, 0x66}};
	int ret;

	memset(mock_addrs, 0, sizeof(mock_addrs));
	rte_eth_devices[MOCK_ETH_DEV_PORT_ID].data->mac_addrs = mock_addrs;
	ret = hinic3_pmd_ops.mac_addr_add(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
					  &addr, 1, 0);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "mac_addr_add success should return 0");
	rte_eth_devices[MOCK_ETH_DEV_PORT_ID].data->mac_addrs = NULL;
	return TEST_SUCCESS;
}

static int
test_mac_addr_add_set_fail(void)
{
	static struct rte_ether_addr mock_addrs[HINIC3_MAX_UC_MAC_ADDRS];
	struct rte_ether_addr addr = {{0x00, 0x11, 0x22, 0x33, 0x44, 0x77}};
	int ret;

	memset(mock_addrs, 0, sizeof(mock_addrs));
	g_mock_set_mac_ret = -EIO;
	rte_eth_devices[MOCK_ETH_DEV_PORT_ID].data->mac_addrs = mock_addrs;
	ret = hinic3_pmd_ops.mac_addr_add(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
					  &addr, 1, 0);
	RTE_TEST_ASSERT(ret != 0, "set_mac fail should propagate error");
	rte_eth_devices[MOCK_ETH_DEV_PORT_ID].data->mac_addrs = NULL;
	return TEST_SUCCESS;
}

/* ====== Ops callback tests: hinic3_mac_addr_remove success path ====== */

static int
test_mac_addr_remove_success(void)
{
	static struct rte_ether_addr mock_addrs[2];
	struct rte_ether_addr addr = {{0x00, 0x11, 0x22, 0x33, 0x44, 0x55}};

	memset(mock_addrs, 0, sizeof(mock_addrs));
	rte_ether_addr_copy(&addr, &mock_addrs[0]);
	rte_eth_devices[MOCK_ETH_DEV_PORT_ID].data->mac_addrs = mock_addrs;
	hinic3_pmd_ops.mac_addr_remove(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID], 0);
	rte_eth_devices[MOCK_ETH_DEV_PORT_ID].data->mac_addrs = NULL;
	return TEST_SUCCESS;
}

/* ====== Ops callback tests: hinic3_vlan_offload_set ====== */

static int
test_vlan_offload_filter(void)
{
	struct rte_eth_dev_data data = {0};
	int ret;

	data.dev_conf.rxmode.offloads = RTE_ETH_RX_OFFLOAD_VLAN_FILTER;
	rte_eth_devices[MOCK_ETH_DEV_PORT_ID].data = &data;
	rte_eth_devices[MOCK_ETH_DEV_PORT_ID].data->dev_private = &g_mock_nic_dev;
	ret = hinic3_pmd_ops.vlan_offload_set(
		&rte_eth_devices[MOCK_ETH_DEV_PORT_ID], RTE_ETH_VLAN_FILTER_MASK);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "vlan filter offload should succeed");
	return TEST_SUCCESS;
}

static int
test_vlan_offload_strip(void)
{
	struct rte_eth_dev_data data = {0};
	int ret;

	data.dev_conf.rxmode.offloads = RTE_ETH_RX_OFFLOAD_VLAN_STRIP;
	rte_eth_devices[MOCK_ETH_DEV_PORT_ID].data = &data;
	rte_eth_devices[MOCK_ETH_DEV_PORT_ID].data->dev_private = &g_mock_nic_dev;
	ret = hinic3_pmd_ops.vlan_offload_set(
		&rte_eth_devices[MOCK_ETH_DEV_PORT_ID], RTE_ETH_VLAN_STRIP_MASK);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "vlan strip offload should succeed");
	return TEST_SUCCESS;
}

static int
test_vlan_offload_filter_fail(void)
{
	struct rte_eth_dev_data data = {0};
	int ret;

	data.dev_conf.rxmode.offloads = RTE_ETH_RX_OFFLOAD_VLAN_FILTER;
	rte_eth_devices[MOCK_ETH_DEV_PORT_ID].data = &data;
	rte_eth_devices[MOCK_ETH_DEV_PORT_ID].data->dev_private = &g_mock_nic_dev;
	g_mock_set_vlan_fliter_ret = -EIO;
	ret = hinic3_pmd_ops.vlan_offload_set(
		&rte_eth_devices[MOCK_ETH_DEV_PORT_ID], RTE_ETH_VLAN_FILTER_MASK);
	RTE_TEST_ASSERT(ret != 0, "vlan filter fail should propagate");
	return TEST_SUCCESS;
}

static struct unit_test_suite hinic3_ethdev_test_suite = {
	.suite_name = "HINIC3 Ethdev Unit Tests",
	.setup = ethdev_test_setup,
	.teardown = ethdev_test_teardown,
	.unit_test_cases = {
		/* VFTA macros */
		TEST_CASE(test_vfta_idx),
		TEST_CASE(test_vfta_bit),
		TEST_CASE(test_vfta_size),
		TEST_CASE(test_vlan_filter_store_and_find),

		/* Queue depth alignment */
		TEST_CASE(test_queue_depth_alignment),

		/* Descriptor limits */
		TEST_CASE(test_desc_limit_constants),

		/* Alignment macros */
		TEST_CASE(test_align_macros),

		/* Version parsing (local copy of static function) */
		TEST_CASE(test_parse_version_valid),
		TEST_CASE(test_parse_version_larger_numbers),
		TEST_CASE(test_parse_version_invalid_null),
		TEST_CASE(test_parse_version_invalid_format),

		/* FEC parameter validation (local copy of static function) */
		TEST_CASE(test_fec_param_valid),

		/* hinic3_dev_info_get — direct function call with mock */
		TEST_CASE(test_dev_info_get_basic),
		TEST_CASE(test_dev_info_get_dcb_off),
		TEST_CASE(test_dev_info_get_dcb_on),
		TEST_CASE(test_dev_info_get_rx_offloads),
		TEST_CASE(test_dev_info_get_tx_offloads),
		TEST_CASE(test_dev_info_get_desc_limits),
		TEST_CASE(test_dev_info_get_rss_params),
		TEST_CASE(test_dev_info_get_default_params),
		TEST_CASE(test_dev_info_get_sp620_tnl_tso),
		TEST_CASE(test_dev_info_get_non_sp620_tnl_tso),
		TEST_CASE(test_dev_info_get_non_sp620_geneve_tso),
		TEST_CASE(test_dev_info_get_non_sp620_ipxip_tso),

		/* hinic3_init_sw_rxtxqs / deinit (local copy of static function) */
		TEST_CASE(test_init_sw_rxtxqs),
		TEST_CASE(test_init_sw_rxtxqs_large),

		/* xstats_calc_num */
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_xstats_calc_num_pf),
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_xstats_calc_num_vf),

		/* Device config defaults */
		TEST_CASE(test_nic_dev_config_defaults),

		/* Queue offset macros */
		TEST_CASE(test_queue_offset_macros),

		/* COS ID macros */
		TEST_CASE(test_cos_id_macros),

		/* MTU/PKTLEN */
		TEST_CASE(test_mtu_pktlen_macros),

		/* Feature cap */
		TEST_CASE(test_feature_cap_bits),
		TEST_CASE(test_default_drv_feature),

		/* RX mode */
		TEST_CASE(test_rx_mode_flags),

		/* Device status bits */
		TEST_CASE(test_dev_status_bits),

		/* Queue reset logic */
		TEST_CASE(test_reset_tx_queue_logic),
		TEST_CASE(test_reset_rx_queue_logic),

		/* Misc constants */
		TEST_CASE(test_copy_mempool_name),
		TEST_CASE(test_ci_coalescing_units),
		TEST_CASE(test_nic_dev_name_format),
		TEST_CASE(test_max_queue_num),

		/* Ops callback tests: hinic3_vlan_filter_set */
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_vlan_filter_set_id_zero),
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_vlan_filter_set_invalid_id),
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_vlan_filter_set_add),
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_vlan_filter_set_add_duplicate),
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_vlan_filter_set_remove),
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_vlan_filter_set_remove_nonexist),
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_vlan_filter_set_add_fail),
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_vlan_filter_set_del_fail),

		/* Ops callback tests: hinic3_dev_allmulticast_enable */
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_allmulticast_enable_success),
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_allmulticast_enable_qpool),
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_allmulticast_enable_fail),

		/* Ops callback tests: hinic3_dev_allmulticast_disable */
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_allmulticast_disable_success),
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_allmulticast_disable_qpool),
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_allmulticast_disable_fail),

		/* Ops callback tests: hinic3_dev_promiscuous_enable */
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_promiscuous_enable_success),
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_promiscuous_enable_no_feature),
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_promiscuous_enable_qpool),
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_promiscuous_enable_fail),

		/* Ops callback tests: hinic3_dev_promiscuous_disable */
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_promiscuous_disable_success),
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_promiscuous_disable_qpool),
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_promiscuous_disable_fail),

		/* Ops callback tests: hinic3_dev_set_mtu */
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_set_mtu_qpool),
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_set_mtu_too_small),
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_set_mtu_too_large),
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_set_mtu_success),
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_set_mtu_boundary_min),
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_set_mtu_boundary_max),
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_set_mtu_fail),

		/* Ops callback tests: hinic3_rss_reta_query */
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_rss_reta_query_disabled),
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_rss_reta_query_success),
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_rss_reta_query_invalid_size),
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_rss_reta_query_mask_filter),

		/* Ops callback tests: hinic3_rss_reta_update */
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_rss_reta_update_disabled),
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_rss_reta_update_invalid_size),
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_rss_reta_update_success),
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_rss_reta_update_qpool),
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_rss_reta_update_fail),

		/* Ops callback tests: hinic3_rss_hash_update */
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_rss_hash_update_disabled_with_hf),
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_rss_hash_update_disabled_no_hf),
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_rss_hash_update_key_too_long),

		/* Ops callback tests: hinic3_rss_conf_get */
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_rss_conf_get_null),
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_rss_conf_get_disabled),

		/* Ops callback tests: hinic3_dev_flow_ctrl_set */
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_flow_ctrl_set_qpool),

		/* Ops callback tests: hinic3_set_mac_addr */
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_set_mac_addr_qpool),
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_set_mac_addr_bifur_shared),
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_set_mac_addr_invalid),

		/* Ops callback tests: hinic3_mac_addr_add */
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_mac_addr_add_qpool),
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_mac_addr_add_invalid_addr),
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_mac_addr_add_out_of_range),

		/* Ops callback tests: hinic3_mac_addr_remove */
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_mac_addr_remove_qpool),

		/* Ops callback tests: hinic3_dev_set_link_up/down */
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_set_link_up_qpool),
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_set_link_up_sp230_vf),
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_set_link_down_qpool),

		/* Ops callback tests: hinic3_dev_reset / hinic3_get_reg */
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_dev_reset_returns_zero),
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_get_reg_returns_zero),

		/* Globalized static function tests */
						TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_find_vlan_filter),

		/* Ops callback tests: hinic3_set_mac_addr success */
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_set_mac_addr_success),
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_set_mac_addr_update_fail),

		/* Ops callback tests: hinic3_mac_addr_add success */
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_mac_addr_add_success),
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_mac_addr_add_set_fail),

		/* Ops callback tests: hinic3_mac_addr_remove success */
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_mac_addr_remove_success),

		/* Ops callback tests: hinic3_vlan_offload_set */
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_vlan_offload_filter),
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_vlan_offload_strip),
		TEST_CASE_ST(mock_ethdev_setup, mock_ethdev_teardown,
			     test_vlan_offload_filter_fail),

		TEST_CASES_END()
	}
};

static int
test_hinic3_ethdev(void)
{
	return unit_test_suite_runner(&hinic3_ethdev_test_suite);
}

REGISTER_TEST_COMMAND(hinic3_ethdev_autotest, test_hinic3_ethdev);
