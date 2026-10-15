/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2025-2026 Huawei Technologies Co., Ltd
 * Unit tests for hinic3_pmd_dcb.c
 */

#include <rte_common.h>
#include <rte_ethdev.h>
#ifdef DPDK_21_11
#include <ethdev_driver.h>
#endif
#include <rte_malloc.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>

#include "../test.h"
#include <rte_test.h>
#include <rte_log.h>

#include "base/hinic3_compat.h"
#include "base/hinic3_pmd_nic_cfg.h"
#include "base/hinic3_pmd_hwdev.h"
#include "base/hinic3_pmd_hwif.h"
#include "base/hinic3_pmd_hw_cfg.h"
#include "hinic3_pmd_ethdev.h"
#include "hinic3_pmd_dcb.h"
#include "hinic3_pmd_tm.h"
#include "test_hinic3_mock.h"

/* g_qinfo_type controls IS_QPOOL_MODE() in driver TUs outside
 * hinic3_pmd_nic_cfg.c (where g_qinfo_type is defined). */

#ifndef DPDK_21_11
extern struct rte_eth_dev rte_eth_devices[];
#endif

/* ====== Static test structures ====== */

static struct hinic3_nic_dev g_dcb_nic_dev;
static struct hinic3_dcb g_dcb;
static struct hinic3_ets g_ets;
static struct hinic3_hwdev g_dcb_hwdev;
static struct hinic3_hwif g_dcb_hwif;
static struct cfg_mgmt_info g_dcb_cfg_mgmt;

/* For hinic3_get_dcb_info tests */
static struct rte_eth_dev g_dcb_eth_dev;
static struct rte_eth_dev_data g_dcb_dev_data;

/* ====== Setup / Teardown ====== */

static int
dcb_test_setup(void)
{
	/* Zero all test structures */
	memset(&g_dcb_nic_dev, 0, sizeof(g_dcb_nic_dev));
	memset(&g_dcb, 0, sizeof(g_dcb));
	memset(&g_ets, 0, sizeof(g_ets));
	memset(&g_dcb_hwdev, 0, sizeof(g_dcb_hwdev));
	memset(&g_dcb_hwif, 0, sizeof(g_dcb_hwif));
	memset(&g_dcb_cfg_mgmt, 0, sizeof(g_dcb_cfg_mgmt));

	/* Wire up pointers */
	g_dcb_nic_dev.dcb = &g_dcb;
	g_dcb_nic_dev.ets = &g_ets;
	g_dcb_nic_dev.hwdev = &g_dcb_hwdev;
	g_dcb_hwdev.hwif = &g_dcb_hwif;
	g_dcb_hwdev.cfg_mgmt = &g_dcb_cfg_mgmt;
	g_dcb_hwdev.dev_handle = &g_dcb_nic_dev;

	/* PF type by default */
	g_dcb_hwif.attr.func_type = TYPE_PF;

	/* Set mock variables to defaults */
	g_mock_is_sp620_nic = true;
	g_mock_mgmt_sync_ret = 0;
	g_mock_msg_to_mgmt_sync_ret = 0;
	g_mock_mbox_to_pf_ret = 0;
	g_mock_rss_get_indir_tbl_ret = 0;
	g_mock_rss_set_indir_tbl_ret = 0;
	g_mock_rss_cfg_ret = 0;
	g_mock_func_type = TYPE_PF;
	g_mock_global_func_id = 0;

	/* Set qinfo type to normal */
	g_qinfo_type = HINIC3_QINFO_TYPE_NORMAL;
	g_dcb_hwdev.qinfo_type = HINIC3_QINFO_TYPE_NORMAL;

	/* Set cos_valid_bitmap in svc_cap */
	g_dcb_cfg_mgmt.svc_cap.cos_valid_bitmap = 0xFF;

	return TEST_SUCCESS;
}

static void
dcb_test_teardown(void)
{
	/* Reset all mock variables to defaults */
	g_mock_is_sp620_nic = false;
	g_mock_mgmt_sync_ret = 0;
	g_mock_msg_to_mgmt_sync_ret = 0;
	g_mock_mbox_to_pf_ret = 0;
	g_mock_rss_get_indir_tbl_ret = 0;
	g_mock_rss_set_indir_tbl_ret = 0;
	g_mock_rss_cfg_ret = 0;
	g_mock_func_type = TYPE_PF;
	g_mock_global_func_id = 0;

	/* Reset qinfo type to normal */
	g_qinfo_type = HINIC3_QINFO_TYPE_NORMAL;
	g_dcb_hwdev.qinfo_type = HINIC3_QINFO_TYPE_NORMAL;

	/* Zero out test structs */
	memset(&g_dcb_nic_dev, 0, sizeof(g_dcb_nic_dev));
	memset(&g_dcb, 0, sizeof(g_dcb));
	memset(&g_ets, 0, sizeof(g_ets));
	memset(&g_dcb_hwdev, 0, sizeof(g_dcb_hwdev));
	memset(&g_dcb_hwif, 0, sizeof(g_dcb_hwif));
	memset(&g_dcb_cfg_mgmt, 0, sizeof(g_dcb_cfg_mgmt));
}

/* ====== 1. hinic3_txq_mapped_tc_get tests ====== */

static int
test_txq_mapped_tc_basic(void)
{
	struct hinic3_tc_queue_info *tcq;

	/* TC0: offset=0, count=4 */
	tcq = &g_ets.tc_queue[0];
	tcq->enable = 1;
	tcq->tqp_offset = 0;
	tcq->tqp_count = 4;
	tcq->tc = 0;

	/* TC1: offset=4, count=4 */
	tcq = &g_ets.tc_queue[1];
	tcq->enable = 1;
	tcq->tqp_offset = 4;
	tcq->tqp_count = 4;
	tcq->tc = 1;

	RTE_TEST_ASSERT_EQUAL(hinic3_txq_mapped_tc_get(&g_dcb_nic_dev, 2), 0,
			      "txq_no=2 should map to TC0");
	RTE_TEST_ASSERT_EQUAL(hinic3_txq_mapped_tc_get(&g_dcb_nic_dev, 5), 1,
			      "txq_no=5 should map to TC1");
	return TEST_SUCCESS;
}

static int
test_txq_mapped_tc_not_found(void)
{
	struct hinic3_tc_queue_info *tcq = &g_ets.tc_queue[0];
	tcq->enable = 1;
	tcq->tqp_offset = 0;
	tcq->tqp_count = 2;
	tcq->tc = 0;

	/* txq_no=5 not in any TC, returns 0 (default) */
	RTE_TEST_ASSERT_EQUAL(hinic3_txq_mapped_tc_get(&g_dcb_nic_dev, 5), 0,
			      "txq_no=5 not in any TC should return 0");
	return TEST_SUCCESS;
}

static int
test_txq_mapped_tc_all_disabled(void)
{
	/* All TCs disabled (zeroed), any txq returns 0 */
	RTE_TEST_ASSERT_EQUAL(hinic3_txq_mapped_tc_get(&g_dcb_nic_dev, 0), 0,
			      "all disabled should return 0");
	return TEST_SUCCESS;
}

static int
test_txq_mapped_tc_boundary(void)
{
	struct hinic3_tc_queue_info *tcq = &g_ets.tc_queue[0];
	tcq->enable = 1;
	tcq->tqp_offset = 0;
	tcq->tqp_count = 4;
	tcq->tc = 0;

	/* txq_no=0 and txq_no=3 in TC0 */
	RTE_TEST_ASSERT_EQUAL(hinic3_txq_mapped_tc_get(&g_dcb_nic_dev, 0), 0,
			      "txq_no=0 should map to TC0");
	RTE_TEST_ASSERT_EQUAL(hinic3_txq_mapped_tc_get(&g_dcb_nic_dev, 3), 0,
			      "txq_no=3 should map to TC0");
	/* txq_no=4 not in any TC, returns 0 */
	RTE_TEST_ASSERT_EQUAL(hinic3_txq_mapped_tc_get(&g_dcb_nic_dev, 4), 0,
			      "txq_no=4 should return 0 (default)");
	return TEST_SUCCESS;
}

/* ====== 2. hinic3_get_dev_valid_cos_map tests ====== */

static int
test_cos_map_pcp(void)
{
	g_dcb.hw_dcb_cfg.trust = HINIC3_DCB_PCP;
	g_dcb.hw_dcb_cfg.pcp_valid_cos_map = 0x0F;
	RTE_TEST_ASSERT_EQUAL(hinic3_get_dev_valid_cos_map(&g_dcb_nic_dev), 0x0F,
			      "PCP trust should return pcp_valid_cos_map");
	return TEST_SUCCESS;
}

static int
test_cos_map_dscp(void)
{
	g_dcb.hw_dcb_cfg.trust = HINIC3_DCB_DSCP;
	g_dcb.hw_dcb_cfg.dscp_valid_cos_map = 0xF0;
	RTE_TEST_ASSERT_EQUAL(hinic3_get_dev_valid_cos_map(&g_dcb_nic_dev), 0xF0,
			      "DSCP trust should return dscp_valid_cos_map");
	return TEST_SUCCESS;
}

static int
test_cos_map_unknown_trust(void)
{
	g_dcb.hw_dcb_cfg.trust = 2; /* invalid */
	RTE_TEST_ASSERT_EQUAL(hinic3_get_dev_valid_cos_map(&g_dcb_nic_dev), 0,
			      "unknown trust should return 0");
	return TEST_SUCCESS;
}

/* ====== 3. hinic3_dcb_alloc tests ====== */

static int
test_dcb_alloc_success(void)
{
	struct hinic3_nic_dev tmp_dev;
	memset(&tmp_dev, 0, sizeof(tmp_dev));
	tmp_dev.dcb = NULL;
	tmp_dev.ets = NULL;

	int ret = hinic3_dcb_alloc(&tmp_dev);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "dcb_alloc should succeed");
	RTE_TEST_ASSERT_NOT_NULL(tmp_dev.dcb, "dcb should be allocated");
	RTE_TEST_ASSERT_NOT_NULL(tmp_dev.ets, "ets should be allocated");

	rte_free(tmp_dev.dcb);
	rte_free(tmp_dev.ets);
	return TEST_SUCCESS;
}

/* ====== 4. hinic3_update_qp_cos_cfg tests ====== */

static int
test_update_qp_cos_even(void)
{
	g_dcb.hw_dcb_cfg.trust = HINIC3_DCB_PCP;
	g_dcb.hw_dcb_cfg.pcp_valid_cos_map = 0x03; /* cos 0 and 1 */
	g_dcb_nic_dev.num_sqs = 8;

	hinic3_update_qp_cos_cfg(&g_dcb_nic_dev, 2);

	RTE_TEST_ASSERT_EQUAL(g_dcb.hw_dcb_cfg.cos_qp_offset[0], 0,
			      "cos0 offset should be 0");
	RTE_TEST_ASSERT_EQUAL(g_dcb.hw_dcb_cfg.cos_qp_num[0], 4,
			      "cos0 num should be 4");
	RTE_TEST_ASSERT_EQUAL(g_dcb.hw_dcb_cfg.cos_qp_offset[1], 4,
			      "cos1 offset should be 4");
	RTE_TEST_ASSERT_EQUAL(g_dcb.hw_dcb_cfg.cos_qp_num[1], 4,
			      "cos1 num should be 4");
	return TEST_SUCCESS;
}

static int
test_update_qp_cos_remainder(void)
{
	g_dcb.hw_dcb_cfg.trust = HINIC3_DCB_PCP;
	g_dcb.hw_dcb_cfg.pcp_valid_cos_map = 0x07; /* cos 0, 1, 2 */
	g_dcb_nic_dev.num_sqs = 10;

	hinic3_update_qp_cos_cfg(&g_dcb_nic_dev, 3);

	/* num_sq_per_cos = 10/3 = 3, remainder = 10%3 = 1 */
	/* cos0: 4 (3+1), cos1: 3, cos2: 3 */
	RTE_TEST_ASSERT_EQUAL(g_dcb.hw_dcb_cfg.cos_qp_num[0], 4,
			      "cos0 num should be 4 (remainder)");
	RTE_TEST_ASSERT_EQUAL(g_dcb.hw_dcb_cfg.cos_qp_num[1], 3,
			      "cos1 num should be 3");
	RTE_TEST_ASSERT_EQUAL(g_dcb.hw_dcb_cfg.cos_qp_num[2], 3,
			      "cos2 num should be 3");
	/* offsets: cos0=0, cos1=4, cos2=7 */
	RTE_TEST_ASSERT_EQUAL(g_dcb.hw_dcb_cfg.cos_qp_offset[0], 0,
			      "cos0 offset should be 0");
	RTE_TEST_ASSERT_EQUAL(g_dcb.hw_dcb_cfg.cos_qp_offset[1], 4,
			      "cos1 offset should be 4");
	RTE_TEST_ASSERT_EQUAL(g_dcb.hw_dcb_cfg.cos_qp_offset[2], 7,
			      "cos2 offset should be 7");
	return TEST_SUCCESS;
}

static int
test_update_qp_cos_zero_cos(void)
{
	g_dcb.hw_dcb_cfg.trust = HINIC3_DCB_PCP;
	g_dcb.hw_dcb_cfg.pcp_valid_cos_map = 0x03;
	g_dcb_nic_dev.num_sqs = 8;
	u16 saved_offset = 99;
	g_dcb.hw_dcb_cfg.cos_qp_offset[0] = saved_offset;

	hinic3_update_qp_cos_cfg(&g_dcb_nic_dev, 0);

	/* Early return, no changes */
	RTE_TEST_ASSERT_EQUAL(g_dcb.hw_dcb_cfg.cos_qp_offset[0], saved_offset,
			      "zero cos should not modify offsets");
	return TEST_SUCCESS;
}

static int
test_update_qp_cos_zero_sq(void)
{
	g_dcb.hw_dcb_cfg.trust = HINIC3_DCB_PCP;
	g_dcb.hw_dcb_cfg.pcp_valid_cos_map = 0x03;
	g_dcb_nic_dev.num_sqs = 0;
	u16 saved_offset = 77;
	g_dcb.hw_dcb_cfg.cos_qp_offset[0] = saved_offset;

	hinic3_update_qp_cos_cfg(&g_dcb_nic_dev, 2);

	/* num_sq_per_cos = 0/2 = 0, early return */
	RTE_TEST_ASSERT_EQUAL(g_dcb.hw_dcb_cfg.cos_qp_offset[0], saved_offset,
			      "zero sqs should not modify offsets");
	return TEST_SUCCESS;
}

/* ====== 5. hinic3_set_txq_cos tests ====== */

static int
test_set_txq_cos_normal(void)
{
	hinic3_set_txq_cos(&g_dcb_nic_dev, 0, 4, 2);

	RTE_TEST_ASSERT_EQUAL(g_dcb.txq_cos[0], 2, "txq_cos[0] should be 2");
	RTE_TEST_ASSERT_EQUAL(g_dcb.txq_cos[1], 2, "txq_cos[1] should be 2");
	RTE_TEST_ASSERT_EQUAL(g_dcb.txq_cos[2], 2, "txq_cos[2] should be 2");
	RTE_TEST_ASSERT_EQUAL(g_dcb.txq_cos[3], 2, "txq_cos[3] should be 2");
	RTE_TEST_ASSERT_EQUAL(g_dcb.txq_cos[4], 0, "txq_cos[4] should be 0");
	return TEST_SUCCESS;
}

static int
test_set_txq_cos_out_of_bounds(void)
{
	/* start_qid=254, q_num=4 => 258 > 256, should early return */
	u8 saved = g_dcb.txq_cos[254];
	hinic3_set_txq_cos(&g_dcb_nic_dev, 254, 4, 5);

	RTE_TEST_ASSERT_EQUAL(g_dcb.txq_cos[254], saved,
			      "out of bounds should not write");
	return TEST_SUCCESS;
}

static int
test_set_txq_cos_offset(void)
{
	hinic3_set_txq_cos(&g_dcb_nic_dev, 10, 5, 3);

	RTE_TEST_ASSERT_EQUAL(g_dcb.txq_cos[10], 3, "txq_cos[10] should be 3");
	RTE_TEST_ASSERT_EQUAL(g_dcb.txq_cos[14], 3, "txq_cos[14] should be 3");
	RTE_TEST_ASSERT_EQUAL(g_dcb.txq_cos[9], 0, "txq_cos[9] should be 0");
	RTE_TEST_ASSERT_EQUAL(g_dcb.txq_cos[15], 0, "txq_cos[15] should be 0");
	return TEST_SUCCESS;
}

/* ====== 6. hinic3_update_tx_db_cos tests ====== */

static int
test_update_tx_db_dcb_disabled(void)
{
	g_dcb.hw_dcb_cfg.default_cos = 1;
	g_dcb_nic_dev.num_sqs = 8;

	hinic3_update_tx_db_cos(&g_dcb_nic_dev, 0);

	int i;
	for (i = 0; i < 8; i++)
		RTE_TEST_ASSERT_EQUAL(g_dcb.txq_cos[i], 1,
				      "dcb disabled, all txq_cos should be default_cos");
	return TEST_SUCCESS;
}

static int
test_update_tx_db_dcb_enabled(void)
{
	g_dcb.hw_dcb_cfg.default_cos = 0;
	g_dcb.hw_dcb_cfg.cos_qp_num[0] = 4;
	g_dcb.hw_dcb_cfg.cos_qp_offset[0] = 0;
	g_dcb.hw_dcb_cfg.cos_qp_num[1] = 4;
	g_dcb.hw_dcb_cfg.cos_qp_offset[1] = 4;
	g_dcb_nic_dev.num_sqs = 8;

	hinic3_update_tx_db_cos(&g_dcb_nic_dev, 1);

	int i;
	for (i = 0; i < 4; i++)
		RTE_TEST_ASSERT_EQUAL(g_dcb.txq_cos[i], 0,
				      "dcb enabled, txq_cos[0..3] should be cos0");
	for (i = 4; i < 8; i++)
		RTE_TEST_ASSERT_EQUAL(g_dcb.txq_cos[i], 1,
				      "dcb enabled, txq_cos[4..7] should be cos1");
	return TEST_SUCCESS;
}

static int
test_update_tx_db_mixed(void)
{
	g_dcb.hw_dcb_cfg.default_cos = 0;
	g_dcb.hw_dcb_cfg.cos_qp_num[0] = 0; /* skipped */
	g_dcb.hw_dcb_cfg.cos_qp_offset[0] = 0;
	g_dcb.hw_dcb_cfg.cos_qp_num[1] = 4;
	g_dcb.hw_dcb_cfg.cos_qp_offset[1] = 4;
	g_dcb_nic_dev.num_sqs = 8;

	hinic3_update_tx_db_cos(&g_dcb_nic_dev, 1);

	/* cos0 has q_num=0 so skipped; cos1 gets q4-7 */
	int i;
	for (i = 0; i < 4; i++)
		RTE_TEST_ASSERT_EQUAL(g_dcb.txq_cos[i], 0,
				      "cos0 skipped, default_cos applied");
	for (i = 4; i < 8; i++)
		RTE_TEST_ASSERT_EQUAL(g_dcb.txq_cos[i], 1,
				      "cos1 active, txq_cos[4..7] should be 1");
	return TEST_SUCCESS;
}

/* ====== 7. hinic3_get_dev_user_cos_num tests ====== */

static int
test_user_cos_num_pcp(void)
{
	g_dcb.hw_dcb_cfg.trust = HINIC3_DCB_PCP;
	g_dcb.hw_dcb_cfg.pcp_user_cos_num = 4;
	RTE_TEST_ASSERT_EQUAL(hinic3_get_dev_user_cos_num(&g_dcb_nic_dev), 4,
			      "PCP trust should return pcp_user_cos_num");
	return TEST_SUCCESS;
}

static int
test_user_cos_num_dscp(void)
{
	g_dcb.hw_dcb_cfg.trust = HINIC3_DCB_DSCP;
	g_dcb.hw_dcb_cfg.dscp_user_cos_num = 2;
	RTE_TEST_ASSERT_EQUAL(hinic3_get_dev_user_cos_num(&g_dcb_nic_dev), 2,
			      "DSCP trust should return dscp_user_cos_num");
	return TEST_SUCCESS;
}

static int
test_user_cos_num_unknown(void)
{
	g_dcb.hw_dcb_cfg.trust = 2; /* invalid */
	RTE_TEST_ASSERT_EQUAL(hinic3_get_dev_user_cos_num(&g_dcb_nic_dev), 0,
			      "unknown trust should return 0");
	return TEST_SUCCESS;
}

/* ====== 8. hinic3_dcb_rss_init tests ====== */

static int
test_dcb_rss_init_qpool(void)
{
	/* g_qinfo_type controls IS_QPOOL_MODE() in hinic3_pmd_dcb.c
	 * (where hinic3_dcb_rss_init is defined). Setting it to QPOOL makes
	 * the function return -EINVAL immediately without calling any
	 * hardware functions. */
	g_qinfo_type = HINIC3_QINFO_TYPE_QPOOL;
	g_dcb_hwdev.qinfo_type = HINIC3_QINFO_TYPE_QPOOL;
	int ret = hinic3_dcb_rss_init(&g_dcb_nic_dev, 0);
	RTE_TEST_ASSERT_EQUAL(ret, -EINVAL,
			      "QPOOL mode should return -EINVAL");
	g_qinfo_type = HINIC3_QINFO_TYPE_NORMAL;
	g_dcb_hwdev.qinfo_type = HINIC3_QINFO_TYPE_NORMAL;
	return TEST_SUCCESS;
}

static int
test_dcb_rss_init_disabled(void)
{
	g_dcb.hw_dcb_cfg.trust = HINIC3_DCB_PCP;
	int ret = hinic3_dcb_rss_init(&g_dcb_nic_dev, 0);
	RTE_TEST_ASSERT_EQUAL(ret, 0,
			      "dcb disabled, rss_init should succeed");
	return TEST_SUCCESS;
}

static int
test_dcb_rss_init_pcp(void)
{
	g_dcb.hw_dcb_cfg.trust = HINIC3_DCB_PCP;
	g_dcb.hw_dcb_cfg.pcp_user_cos_num = 4;
	g_dcb.hw_dcb_cfg.pcp2cos[0] = 0;
	g_dcb.hw_dcb_cfg.pcp2cos[1] = 1;
	g_dcb.hw_dcb_cfg.pcp2cos[2] = 2;
	g_dcb.hw_dcb_cfg.pcp2cos[3] = 3;
	g_dcb.hw_dcb_cfg.pcp2cos[4] = 0;
	g_dcb.hw_dcb_cfg.pcp2cos[5] = 1;
	g_dcb.hw_dcb_cfg.pcp2cos[6] = 2;
	g_dcb.hw_dcb_cfg.pcp2cos[7] = 3;
	g_dcb_nic_dev.num_rqs = 8;

	int ret = hinic3_dcb_rss_init(&g_dcb_nic_dev, 1);
	RTE_TEST_ASSERT_EQUAL(ret, 0,
			      "dcb enabled with PCP, rss_init should succeed");
	return TEST_SUCCESS;
}

/* ====== 9. hinic3_configure_dcb_hw tests ====== */

static int
test_configure_dcb_hw_pf_success(void)
{
	g_dcb.hw_dcb_cfg.trust = HINIC3_DCB_PCP;
	g_dcb.hw_dcb_cfg.default_cos = 0;
	g_dcb.hw_dcb_cfg.pcp_user_cos_num = 8;
	g_dcb_hwif.attr.func_type = TYPE_PF;
	g_mock_is_sp620_nic = true;
	g_dcb_cfg_mgmt.svc_cap.cos_valid_bitmap = 0xFF;
	g_dcb_nic_dev.num_sqs = 256;

	int ret = hinic3_configure_dcb_hw(&g_dcb_nic_dev, 1);
	RTE_TEST_ASSERT_EQUAL(ret, 0,
			      "PF configure_dcb_hw should succeed");
	return TEST_SUCCESS;
}

static int
test_configure_dcb_hw_vf_not_sp620(void)
{
	g_dcb.hw_dcb_cfg.trust = HINIC3_DCB_PCP;
	g_dcb.hw_dcb_cfg.default_cos = 0;
	g_dcb.hw_dcb_cfg.pcp_user_cos_num = 4;
	g_dcb_hwif.attr.func_type = TYPE_VF;
	g_mock_is_sp620_nic = false;
	g_dcb_cfg_mgmt.svc_cap.cos_valid_bitmap = 0xFF;
	g_dcb_nic_dev.num_sqs = 256;

	int ret = hinic3_configure_dcb_hw(&g_dcb_nic_dev, 1);
	RTE_TEST_ASSERT_EQUAL(ret, 0,
			      "VF (not sp620) configure_dcb_hw should succeed");
	return TEST_SUCCESS;
}

static int
test_configure_dcb_hw_sync_fail(void)
{
	g_dcb.hw_dcb_cfg.trust = HINIC3_DCB_PCP;
	g_dcb.hw_dcb_cfg.default_cos = 0;
	g_dcb.hw_dcb_cfg.pcp_user_cos_num = 8;
	g_dcb_hwif.attr.func_type = TYPE_PF;
	g_mock_is_sp620_nic = true;
	g_dcb_cfg_mgmt.svc_cap.cos_valid_bitmap = 0xFF;
	g_dcb_nic_dev.num_sqs = 256;
	/* l2nic_msg_to_mgmt_sync is defined in the same TU as
	 * hinic3_sync_dcb_state/hinic3_sync_qos_map, so --wrap is
	 * bypassed. The real function calls hinic3_msg_to_mgmt_sync
	 * (which IS wrapped), so we must use g_mock_msg_to_mgmt_sync_ret. */
	g_mock_msg_to_mgmt_sync_ret = -1;

	int ret = hinic3_configure_dcb_hw(&g_dcb_nic_dev, 1);
	RTE_TEST_ASSERT(ret != 0,
			"sync_dcb_state failure should propagate error");
	return TEST_SUCCESS;
}

static int
test_configure_dcb_hw_small_sqs(void)
{
	g_dcb.hw_dcb_cfg.trust = HINIC3_DCB_PCP;
	g_dcb.hw_dcb_cfg.default_cos = 0;
	g_dcb.hw_dcb_cfg.pcp_user_cos_num = 8;
	g_dcb_hwif.attr.func_type = TYPE_PF;
	g_mock_is_sp620_nic = true;
	g_dcb_cfg_mgmt.svc_cap.cos_valid_bitmap = 0xFF;
	/* hinic3_dcb_init_tm uses ets->tqps_num (=HINIC3_MAX_QUEUE_NUM=256),
	 * not nic_dev->num_sqs, so even with num_sqs=2 the function succeeds. */
	g_dcb_nic_dev.num_sqs = 2;

	int ret = hinic3_configure_dcb_hw(&g_dcb_nic_dev, 1);
	RTE_TEST_ASSERT_EQUAL(ret, 0,
			      "configure_dcb_hw should succeed even with small num_sqs");
	return TEST_SUCCESS;
}

/* ====== 10. hinic3_setup_cos tests ====== */

static int
test_setup_cos_invalid(void)
{
	g_dcb.cos_config_num_max = 4;

	int ret = hinic3_setup_cos(&g_dcb_nic_dev, 5);
	RTE_TEST_ASSERT_EQUAL(ret, -EINVAL,
			      "cos > cos_config_num_max should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_setup_cos_zero(void)
{
	g_dcb.hw_dcb_cfg.trust = HINIC3_DCB_PCP;
	g_dcb.hw_dcb_cfg.default_cos = 0;
	g_dcb.hw_dcb_cfg.pcp_user_cos_num = 0;
	g_dcb.cos_config_num_max = 8;
	g_dcb_hwif.attr.func_type = TYPE_PF;
	g_mock_is_sp620_nic = true;
	g_dcb_cfg_mgmt.svc_cap.cos_valid_bitmap = 0xFF;
	g_dcb_nic_dev.num_sqs = 256;

	int ret = hinic3_setup_cos(&g_dcb_nic_dev, 0);
	RTE_TEST_ASSERT_EQUAL(ret, 0,
			      "cos=0 should succeed (dcb_en=0)");
	return TEST_SUCCESS;
}

static int
test_setup_cos_valid(void)
{
	g_dcb.hw_dcb_cfg.trust = HINIC3_DCB_PCP;
	g_dcb.hw_dcb_cfg.default_cos = 0;
	g_dcb.hw_dcb_cfg.pcp_user_cos_num = 2;
	g_dcb.cos_config_num_max = 8;
	g_dcb_hwif.attr.func_type = TYPE_PF;
	g_mock_is_sp620_nic = true;
	g_dcb_cfg_mgmt.svc_cap.cos_valid_bitmap = 0xFF;
	g_dcb_nic_dev.num_sqs = 256;

	int ret = hinic3_setup_cos(&g_dcb_nic_dev, 2);
	RTE_TEST_ASSERT_EQUAL(ret, 0,
			      "cos=2 should succeed (dcb_en=1)");
	return TEST_SUCCESS;
}

/* ====== 11. hinic3_get_dcb_info tests ====== */

static int
test_get_dcb_info_dcb_mode(void)
{
	struct rte_eth_dcb_info dcb_info;

	memset(&g_dcb_eth_dev, 0, sizeof(g_dcb_eth_dev));
	memset(&g_dcb_dev_data, 0, sizeof(g_dcb_dev_data));
	g_dcb_eth_dev.data = &g_dcb_dev_data;
	g_dcb_dev_data.dev_private = &g_dcb_nic_dev;
	g_dcb_dev_data.dev_conf.rxmode.mq_mode = RTE_ETH_MQ_RX_DCB_FLAG;
	g_dcb_dev_data.dev_conf.rx_adv_conf.dcb_rx_conf.nb_tcs =
		(enum rte_eth_nb_tcs)8;

	g_dcb.hw_dcb_cfg.trust = HINIC3_DCB_PCP;
	g_dcb.hw_dcb_cfg.pcp_user_cos_num = 8;
	g_dcb.hw_dcb_cfg.cos_qp_offset[0] = 0;
	g_dcb.hw_dcb_cfg.cos_qp_num[0] = 32;
	g_ets.tc_bw[0] = 50;

	int ret = hinic3_get_dcb_info(&g_dcb_eth_dev, &dcb_info);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "get_dcb_info should succeed");
	RTE_TEST_ASSERT_EQUAL(dcb_info.nb_tcs, 8,
			      "DCB mode should have nb_tcs=8");
	RTE_TEST_ASSERT_EQUAL(dcb_info.tc_queue.tc_rxq[0][0].base, 0,
			      "tc_rxq base should be cos_qp_offset[0]");
	RTE_TEST_ASSERT_EQUAL(dcb_info.tc_queue.tc_rxq[0][0].nb_queue, 32,
			      "tc_rxq nb_queue should be cos_qp_num[0]");
	RTE_TEST_ASSERT_EQUAL(dcb_info.tc_bws[0], 50,
			      "tc_bws[0] should be tc_bw[0]");
	return TEST_SUCCESS;
}

static int
test_get_dcb_info_non_dcb(void)
{
	struct rte_eth_dcb_info dcb_info;

	memset(&g_dcb_eth_dev, 0, sizeof(g_dcb_eth_dev));
	memset(&g_dcb_dev_data, 0, sizeof(g_dcb_dev_data));
	g_dcb_eth_dev.data = &g_dcb_dev_data;
	g_dcb_dev_data.dev_private = &g_dcb_nic_dev;
	/* No DCB flag set */
	g_dcb_dev_data.dev_conf.rxmode.mq_mode = 0;
	g_dcb.hw_dcb_cfg.trust = HINIC3_DCB_PCP;
	g_dcb.hw_dcb_cfg.pcp_user_cos_num = 4;

	int ret = hinic3_get_dcb_info(&g_dcb_eth_dev, &dcb_info);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "get_dcb_info should succeed");
	RTE_TEST_ASSERT_EQUAL(dcb_info.nb_tcs, 1,
			      "non-DCB mode should have nb_tcs=1");
	return TEST_SUCCESS;
}

static int
test_get_dcb_info_queue_info(void)
{
	struct rte_eth_dcb_info dcb_info;

	memset(&g_dcb_eth_dev, 0, sizeof(g_dcb_eth_dev));
	memset(&g_dcb_dev_data, 0, sizeof(g_dcb_dev_data));
	g_dcb_eth_dev.data = &g_dcb_dev_data;
	g_dcb_dev_data.dev_private = &g_dcb_nic_dev;
	g_dcb_dev_data.dev_conf.rxmode.mq_mode = RTE_ETH_MQ_RX_DCB_FLAG;
	g_dcb_dev_data.dev_conf.rx_adv_conf.dcb_rx_conf.nb_tcs =
		(enum rte_eth_nb_tcs)8;

	g_dcb.hw_dcb_cfg.trust = HINIC3_DCB_PCP;
	g_dcb.hw_dcb_cfg.pcp_user_cos_num = 8;
	/* Set specific values for cos 0 and 1 */
	g_dcb.hw_dcb_cfg.cos_qp_offset[0] = 0;
	g_dcb.hw_dcb_cfg.cos_qp_num[0] = 16;
	g_dcb.hw_dcb_cfg.cos_qp_offset[1] = 16;
	g_dcb.hw_dcb_cfg.cos_qp_num[1] = 16;
	g_ets.tc_bw[0] = 25;
	g_ets.tc_bw[1] = 75;

	int ret = hinic3_get_dcb_info(&g_dcb_eth_dev, &dcb_info);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "get_dcb_info should succeed");

	/* Verify TC0 queue info */
	RTE_TEST_ASSERT_EQUAL(dcb_info.tc_queue.tc_rxq[0][0].base, 0,
			      "tc0 rx base should be 0");
	RTE_TEST_ASSERT_EQUAL(dcb_info.tc_queue.tc_rxq[0][0].nb_queue, 16,
			      "tc0 rx nb_queue should be 16");
	RTE_TEST_ASSERT_EQUAL(dcb_info.tc_queue.tc_txq[0][0].base, 0,
			      "tc0 tx base should be 0");
	RTE_TEST_ASSERT_EQUAL(dcb_info.tc_queue.tc_txq[0][0].nb_queue, 16,
			      "tc0 tx nb_queue should be 16");

	/* Verify TC1 queue info */
	RTE_TEST_ASSERT_EQUAL(dcb_info.tc_queue.tc_rxq[0][1].base, 16,
			      "tc1 rx base should be 16");
	RTE_TEST_ASSERT_EQUAL(dcb_info.tc_queue.tc_rxq[0][1].nb_queue, 16,
			      "tc1 rx nb_queue should be 16");
	RTE_TEST_ASSERT_EQUAL(dcb_info.tc_queue.tc_txq[0][1].base, 16,
			      "tc1 tx base should be 16");
	RTE_TEST_ASSERT_EQUAL(dcb_info.tc_queue.tc_txq[0][1].nb_queue, 16,
			      "tc1 tx nb_queue should be 16");

	/* Verify tc_bws */
	RTE_TEST_ASSERT_EQUAL(dcb_info.tc_bws[0], 25,
			      "tc_bws[0] should be 25");
	RTE_TEST_ASSERT_EQUAL(dcb_info.tc_bws[1], 75,
			      "tc_bws[1] should be 75");
	return TEST_SUCCESS;
}

/* ====== Test suite ====== */

static struct unit_test_suite hinic3_dcb_test_suite = {
	.suite_name = "HINIC3 DCB Unit Tests",
	.setup = NULL,
	.teardown = NULL,
	.unit_test_cases = {
		/* hinic3_txq_mapped_tc_get */
		TEST_CASE_ST(dcb_test_setup, dcb_test_teardown,
			     test_txq_mapped_tc_basic),
		TEST_CASE_ST(dcb_test_setup, dcb_test_teardown,
			     test_txq_mapped_tc_not_found),
		TEST_CASE_ST(dcb_test_setup, dcb_test_teardown,
			     test_txq_mapped_tc_all_disabled),
		TEST_CASE_ST(dcb_test_setup, dcb_test_teardown,
			     test_txq_mapped_tc_boundary),

		/* hinic3_get_dev_valid_cos_map */
		TEST_CASE_ST(dcb_test_setup, dcb_test_teardown,
			     test_cos_map_pcp),
		TEST_CASE_ST(dcb_test_setup, dcb_test_teardown,
			     test_cos_map_dscp),
		TEST_CASE_ST(dcb_test_setup, dcb_test_teardown,
			     test_cos_map_unknown_trust),

		/* hinic3_dcb_alloc */
		TEST_CASE_ST(dcb_test_setup, dcb_test_teardown,
			     test_dcb_alloc_success),

		/* hinic3_update_qp_cos_cfg */
		TEST_CASE_ST(dcb_test_setup, dcb_test_teardown,
			     test_update_qp_cos_even),
		TEST_CASE_ST(dcb_test_setup, dcb_test_teardown,
			     test_update_qp_cos_remainder),
		TEST_CASE_ST(dcb_test_setup, dcb_test_teardown,
			     test_update_qp_cos_zero_cos),
		TEST_CASE_ST(dcb_test_setup, dcb_test_teardown,
			     test_update_qp_cos_zero_sq),

		/* hinic3_set_txq_cos */
		TEST_CASE_ST(dcb_test_setup, dcb_test_teardown,
			     test_set_txq_cos_normal),
		TEST_CASE_ST(dcb_test_setup, dcb_test_teardown,
			     test_set_txq_cos_out_of_bounds),
		TEST_CASE_ST(dcb_test_setup, dcb_test_teardown,
			     test_set_txq_cos_offset),

		/* hinic3_update_tx_db_cos */
		TEST_CASE_ST(dcb_test_setup, dcb_test_teardown,
			     test_update_tx_db_dcb_disabled),
		TEST_CASE_ST(dcb_test_setup, dcb_test_teardown,
			     test_update_tx_db_dcb_enabled),
		TEST_CASE_ST(dcb_test_setup, dcb_test_teardown,
			     test_update_tx_db_mixed),

		/* hinic3_get_dev_user_cos_num */
		TEST_CASE_ST(dcb_test_setup, dcb_test_teardown,
			     test_user_cos_num_pcp),
		TEST_CASE_ST(dcb_test_setup, dcb_test_teardown,
			     test_user_cos_num_dscp),
		TEST_CASE_ST(dcb_test_setup, dcb_test_teardown,
			     test_user_cos_num_unknown),

		/* hinic3_dcb_rss_init */
		TEST_CASE_ST(dcb_test_setup, dcb_test_teardown,
			     test_dcb_rss_init_qpool),
		TEST_CASE_ST(dcb_test_setup, dcb_test_teardown,
			     test_dcb_rss_init_disabled),
		TEST_CASE_ST(dcb_test_setup, dcb_test_teardown,
			     test_dcb_rss_init_pcp),

		/* hinic3_configure_dcb_hw */
		TEST_CASE_ST(dcb_test_setup, dcb_test_teardown,
			     test_configure_dcb_hw_pf_success),
		TEST_CASE_ST(dcb_test_setup, dcb_test_teardown,
			     test_configure_dcb_hw_vf_not_sp620),
		TEST_CASE_ST(dcb_test_setup, dcb_test_teardown,
			     test_configure_dcb_hw_sync_fail),
		TEST_CASE_ST(dcb_test_setup, dcb_test_teardown,
			     test_configure_dcb_hw_small_sqs),

		/* hinic3_setup_cos */
		TEST_CASE_ST(dcb_test_setup, dcb_test_teardown,
			     test_setup_cos_invalid),
		TEST_CASE_ST(dcb_test_setup, dcb_test_teardown,
			     test_setup_cos_zero),
		TEST_CASE_ST(dcb_test_setup, dcb_test_teardown,
			     test_setup_cos_valid),

		/* hinic3_get_dcb_info */
		TEST_CASE_ST(dcb_test_setup, dcb_test_teardown,
			     test_get_dcb_info_dcb_mode),
		TEST_CASE_ST(dcb_test_setup, dcb_test_teardown,
			     test_get_dcb_info_non_dcb),
		TEST_CASE_ST(dcb_test_setup, dcb_test_teardown,
			     test_get_dcb_info_queue_info),

		TEST_CASES_END()
	}
};

static int
test_hinic3_dcb(void)
{
	return unit_test_suite_runner(&hinic3_dcb_test_suite);
}

REGISTER_TEST_COMMAND(hinic3_dcb_autotest, test_hinic3_dcb);
