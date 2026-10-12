/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2025-2026 Huawei Technologies Co., Ltd
 * Unit tests for hinic3_pmd_nic_io.c
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
#include "hinic3_pmd_ethdev.h"
#include "hinic3_pmd_rx.h"
#include "hinic3_pmd_nic_io.h"
#include "test_hinic3_mock.h"

/* ====== Static test structures ====== */

static struct hinic3_nic_dev g_io_nic_dev;
static struct hinic3_hwdev g_io_hwdev;

/* Mock rxq structures for hinic3_get_func_rx_buf_size tests */
static struct hinic3_rxq g_io_rxqs[4];
static struct hinic3_rxq *g_io_rxq_ptrs[4];

/* Structures for hinic3_sq_prepare_ctxt tests */
static struct hinic3_txq g_test_txq;
static struct hinic3_sq_ctxt g_test_sq_ctxt;

/* Structures for hinic3_rq_prepare_ctxt tests */
static struct hinic3_rxq g_test_rxq;
static struct hinic3_rq_ctxt g_test_rq_ctxt;

/* Structure for hinic3_init_rq_cqe_ctxts tests */
static struct hinic3_rxq g_cqe_rxq;

/* ====== Setup / Teardown ====== */

static int
nic_io_test_setup(void)
{
	/* Zero all test structures */
	memset(&g_io_nic_dev, 0, sizeof(g_io_nic_dev));
	memset(&g_io_hwdev, 0, sizeof(g_io_hwdev));
	memset(g_io_rxqs, 0, sizeof(g_io_rxqs));
	memset(g_io_rxq_ptrs, 0, sizeof(g_io_rxq_ptrs));
	memset(&g_test_txq, 0, sizeof(g_test_txq));
	memset(&g_test_sq_ctxt, 0, sizeof(g_test_sq_ctxt));
	memset(&g_test_rxq, 0, sizeof(g_test_rxq));
	memset(&g_test_rq_ctxt, 0, sizeof(g_test_rq_ctxt));
	memset(&g_cqe_rxq, 0, sizeof(g_cqe_rxq));

	/* Wire up pointers */
	g_io_nic_dev.hwdev = &g_io_hwdev;
	g_io_hwdev.dev_handle = &g_io_nic_dev;
	g_test_txq.nic_dev = &g_io_nic_dev;
	g_test_rxq.nic_dev = &g_io_nic_dev;

	/* Set up rxqs array */
	g_io_rxq_ptrs[0] = &g_io_rxqs[0];
	g_io_rxq_ptrs[1] = &g_io_rxqs[1];
	g_io_rxq_ptrs[2] = &g_io_rxqs[2];
	g_io_rxq_ptrs[3] = &g_io_rxqs[3];
	g_io_nic_dev.rxqs = g_io_rxq_ptrs;

	/* Set mock variables to defaults */
	g_mock_is_sp620_nic = true;
	g_mock_is_sp230_nic = false;
	g_mock_mgmt_sync_ret = 0;
	g_mock_msg_to_mgmt_sync_ret = 0;
	g_mock_mbox_to_pf_ret = 0;
	g_mock_func_type = TYPE_PF;
	g_mock_global_func_id = 0;

	return TEST_SUCCESS;
}

static void
nic_io_test_teardown(void)
{
	/* Reset mock variables */
	g_mock_is_sp620_nic = false;
	g_mock_is_sp230_nic = false;
	g_mock_mgmt_sync_ret = 0;
	g_mock_msg_to_mgmt_sync_ret = 0;
	g_mock_mbox_to_pf_ret = 0;
	g_mock_func_type = TYPE_PF;
	g_mock_global_func_id = 0;

	/* Zero structures */
	memset(&g_io_nic_dev, 0, sizeof(g_io_nic_dev));
	memset(&g_io_hwdev, 0, sizeof(g_io_hwdev));
	memset(g_io_rxqs, 0, sizeof(g_io_rxqs));
	memset(g_io_rxq_ptrs, 0, sizeof(g_io_rxq_ptrs));
	memset(&g_test_txq, 0, sizeof(g_test_txq));
	memset(&g_test_sq_ctxt, 0, sizeof(g_test_sq_ctxt));
	memset(&g_test_rxq, 0, sizeof(g_test_rxq));
	memset(&g_test_rq_ctxt, 0, sizeof(g_test_rq_ctxt));
	memset(&g_cqe_rxq, 0, sizeof(g_cqe_rxq));
}

/* ====== A. hinic3_update_driver_feature (2 tests) ====== */

static int
test_update_driver_feature(void)
{
	u64 feature = 0xABCD1234;

	hinic3_update_driver_feature(&g_io_nic_dev, feature);
	RTE_TEST_ASSERT_EQUAL(g_io_nic_dev.feature_cap, feature,
			     "feature_cap should match set value");
	return TEST_SUCCESS;
}

static int
test_update_driver_feature_null(void)
{
	/* NULL dev should not crash */
	hinic3_update_driver_feature(NULL, 0xABCD);
	RTE_TEST_ASSERT_EQUAL(g_io_nic_dev.feature_cap, 0,
			     "feature_cap should remain 0 with NULL dev");
	return TEST_SUCCESS;
}

/* ====== B. hinic3_get_func_rx_buf_size (5 tests) ====== */

static int
test_get_rx_buf_size_min(void)
{
	g_io_nic_dev.num_rqs = 4;
	g_io_rxqs[0].buf_len = 2048;
	g_io_rxqs[1].buf_len = 1024;
	g_io_rxqs[2].buf_len = 4096;
	g_io_rxqs[3].buf_len = 1500;

	hinic3_get_func_rx_buf_size(&g_io_nic_dev);
	RTE_TEST_ASSERT_EQUAL(g_io_nic_dev.rx_buff_len, 1024,
			     "min buf_len should be 1024");
	return TEST_SUCCESS;
}

static int
test_get_rx_buf_size_null_rxq(void)
{
	/* rxq[1] is NULL, should be skipped */
	g_io_nic_dev.num_rqs = 4;
	g_io_rxqs[0].buf_len = 2048;
	g_io_rxq_ptrs[1] = NULL;
	g_io_rxqs[2].buf_len = 1024;
	g_io_rxqs[3].buf_len = 4096;

	hinic3_get_func_rx_buf_size(&g_io_nic_dev);
	RTE_TEST_ASSERT_EQUAL(g_io_nic_dev.rx_buff_len, 1024,
			     "min buf_len should be 1024 (NULL rxq skipped)");

	/* Restore for teardown */
	g_io_rxq_ptrs[1] = &g_io_rxqs[1];
	return TEST_SUCCESS;
}

static int
test_get_rx_buf_size_hairpin(void)
{
	/* rxq[1] is hairpin, should be skipped */
	g_io_nic_dev.num_rqs = 4;
	g_io_rxqs[0].buf_len = 2048;
	g_io_rxqs[1].buf_len = 1024;
	g_io_rxqs[1].is_hairpin = true;
	g_io_rxqs[2].buf_len = 4096;
	g_io_rxqs[3].buf_len = 1500;

	hinic3_get_func_rx_buf_size(&g_io_nic_dev);
	RTE_TEST_ASSERT_EQUAL(g_io_nic_dev.rx_buff_len, 1500,
			     "min buf_len should be 1500 (hairpin rxq skipped)");
	return TEST_SUCCESS;
}

static int
test_get_rx_buf_size_zero_rqs(void)
{
	g_io_nic_dev.num_rqs = 0;

	hinic3_get_func_rx_buf_size(&g_io_nic_dev);
	RTE_TEST_ASSERT_EQUAL(g_io_nic_dev.rx_buff_len, 0,
			     "zero rqs should give buf_len=0");
	return TEST_SUCCESS;
}

static int
test_get_rx_buf_size_all_same(void)
{
	g_io_nic_dev.num_rqs = 4;
	g_io_rxqs[0].buf_len = 2048;
	g_io_rxqs[1].buf_len = 2048;
	g_io_rxqs[2].buf_len = 2048;
	g_io_rxqs[3].buf_len = 2048;

	hinic3_get_func_rx_buf_size(&g_io_nic_dev);
	RTE_TEST_ASSERT_EQUAL(g_io_nic_dev.rx_buff_len, 2048,
			     "all same buf_len should return that value");
	return TEST_SUCCESS;
}

/* ====== C. hinic3_set_rq_enable (3 tests) ====== */

static int
test_set_rq_enable_success(void)
{
	int ret = hinic3_set_rq_enable(&g_io_nic_dev, 0, true);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "set_rq_enable should succeed");
	return TEST_SUCCESS;
}

static int
test_set_rq_enable_fail(void)
{
	g_mock_mgmt_sync_ret = -1;

	int ret = hinic3_set_rq_enable(&g_io_nic_dev, 0, true);
	RTE_TEST_ASSERT_EQUAL(ret, -EFAULT,
			      "mgmt failure should return -EFAULT");

	g_mock_mgmt_sync_ret = 0;
	return TEST_SUCCESS;
}

static int
test_set_rq_enable_null(void)
{
	int ret = hinic3_set_rq_enable(NULL, 0, true);
	RTE_TEST_ASSERT_EQUAL(ret, -EINVAL,
			      "NULL nic_dev should return -EINVAL");
	return TEST_SUCCESS;
}

/* ====== D. hinic3_sq_prepare_ctxt (4 tests) ====== */

static int
test_sq_prepare_ctxt_htn(void)
{
	g_mock_is_sp230_nic = true;
	g_test_txq.cons_idx = 0;
	g_test_txq.prod_idx = 100;
	g_test_txq.q_mask = 0x3FF;
	g_test_txq.queue_buf_paddr = 0x100000000ULL;

	hinic3_sq_prepare_ctxt(&g_test_txq, 5, &g_test_sq_ctxt);

	RTE_TEST_ASSERT_EQUAL(be32_to_cpu(g_test_sq_ctxt.global_sq_id), 5,
			     "global_sq_id should match sq_id");
	return TEST_SUCCESS;
}

static int
test_sq_prepare_ctxt_stn(void)
{
	g_mock_is_sp230_nic = false;
	g_test_txq.cons_idx = 0;
	g_test_txq.prod_idx = 100;
	g_test_txq.q_mask = 0x3FF;
	g_test_txq.queue_buf_paddr = 0x100000000ULL;

	hinic3_sq_prepare_ctxt(&g_test_txq, 5, &g_test_sq_ctxt);

	RTE_TEST_ASSERT_EQUAL(be32_to_cpu(g_test_sq_ctxt.global_sq_id), 5,
			     "global_sq_id should match sq_id");
	return TEST_SUCCESS;
}

static int
test_sq_prepare_ctxt_zero_indices(void)
{
	g_mock_is_sp230_nic = true;
	g_test_txq.cons_idx = 0;
	g_test_txq.prod_idx = 0;
	g_test_txq.q_mask = 0x3FF;
	g_test_txq.queue_buf_paddr = 0x100000000ULL;

	hinic3_sq_prepare_ctxt(&g_test_txq, 0, &g_test_sq_ctxt);

	RTE_TEST_ASSERT_EQUAL(be32_to_cpu(g_test_sq_ctxt.ci_pi), 0,
			     "ci_pi should be 0 with zero indices");
	return TEST_SUCCESS;
}

static int
test_sq_prepare_ctxt_max_indices(void)
{
	g_mock_is_sp230_nic = true;
	g_test_txq.cons_idx = 0x3FF;
	g_test_txq.prod_idx = 0x3FF;
	g_test_txq.q_mask = 0x3FF;
	g_test_txq.queue_buf_paddr = 0x100000000ULL;

	hinic3_sq_prepare_ctxt(&g_test_txq, 0, &g_test_sq_ctxt);

	RTE_TEST_ASSERT_EQUAL(be32_to_cpu(g_test_sq_ctxt.ci_pi), 0x03FF03FF,
			     "ci_pi should have masked ci and pi values");
	return TEST_SUCCESS;
}

/* ====== E. hinic3_rq_prepare_ctxt (5 tests) ====== */

static int
test_rq_prepare_ctxt_extend_htn(void)
{
	g_mock_is_sp230_nic = true;
	g_test_rxq.wqe_type = HINIC3_EXTEND_RQ_WQE;
	g_test_rxq.cons_idx = 0;
	g_test_rxq.prod_idx = 100;
	g_test_rxq.q_mask = 0x3FF;
	g_test_rxq.queue_buf_paddr = 0x100000000ULL;

	hinic3_rq_prepare_ctxt(&g_test_rxq, &g_test_rq_ctxt);

	/* Verify it completes without crash */
	return TEST_SUCCESS;
}

static int
test_rq_prepare_ctxt_normal_htn(void)
{
	g_mock_is_sp230_nic = true;
	g_test_rxq.wqe_type = HINIC3_NORMAL_RQ_WQE;
	g_test_rxq.cons_idx = 0;
	g_test_rxq.prod_idx = 100;
	g_test_rxq.q_mask = 0x3FF;
	g_test_rxq.queue_buf_paddr = 0x100000000ULL;

	hinic3_rq_prepare_ctxt(&g_test_rxq, &g_test_rq_ctxt);

	return TEST_SUCCESS;
}

static int
test_rq_prepare_ctxt_compact_htn(void)
{
	g_mock_is_sp230_nic = true;
	g_test_rxq.wqe_type = HINIC3_COMPACT_RQ_WQE;
	g_test_rxq.cons_idx = 0;
	g_test_rxq.prod_idx = 100;
	g_test_rxq.q_mask = 0x3FF;
	g_test_rxq.queue_buf_paddr = 0x100000000ULL;

	hinic3_rq_prepare_ctxt(&g_test_rxq, &g_test_rq_ctxt);

	return TEST_SUCCESS;
}

static int
test_rq_prepare_ctxt_compact_stn(void)
{
	g_mock_is_sp230_nic = false;
	g_test_rxq.wqe_type = HINIC3_COMPACT_RQ_WQE;
	g_test_rxq.cons_idx = 0;
	g_test_rxq.prod_idx = 100;
	g_test_rxq.q_mask = 0x3FF;
	g_test_rxq.queue_buf_paddr = 0x100000000ULL;
	g_io_nic_dev.config.rx_cqe_compact_en = 1;

	hinic3_rq_prepare_ctxt(&g_test_rxq, &g_test_rq_ctxt);

	return TEST_SUCCESS;
}

static int
test_rq_prepare_ctxt_pi_dma_addr(void)
{
	g_mock_is_sp230_nic = true;
	g_test_rxq.wqe_type = HINIC3_EXTEND_RQ_WQE;
	g_test_rxq.cons_idx = 0;
	g_test_rxq.prod_idx = 0;
	g_test_rxq.q_mask = 0x3FF;
	g_test_rxq.queue_buf_paddr = 0x100000000ULL;
	g_test_rxq.pi_dma_addr = 0x1234567890ABULL;

	hinic3_rq_prepare_ctxt(&g_test_rxq, &g_test_rq_ctxt);

	RTE_TEST_ASSERT_EQUAL(be32_to_cpu(g_test_rq_ctxt.pi_paddr_hi),
			     (u32)0x1234,
			     "pi_paddr_hi should match upper 32 bits");
	RTE_TEST_ASSERT_EQUAL(be32_to_cpu(g_test_rq_ctxt.pi_paddr_lo),
			     (u32)0x567890AB,
			     "pi_paddr_lo should match lower 32 bits");
	return TEST_SUCCESS;
}

/* ====== F. hinic3_free_qp_ctxts (2 tests) ====== */

static int
test_free_qp_ctxts_null(void)
{
	hinic3_free_qp_ctxts(NULL);
	/* Should not crash with NULL hwdev */
	return TEST_SUCCESS;
}

static int
test_free_qp_ctxts_success(void)
{
	g_mock_msg_to_mgmt_sync_ret = 0;

	hinic3_free_qp_ctxts(&g_io_hwdev);
	/* Should complete without crash */
	return TEST_SUCCESS;
}

/* ====== G. hinic3_init_rq_cqe_ctxts (6 tests) ====== */

static int
test_init_rq_cqe_null(void)
{
	int ret = hinic3_init_rq_cqe_ctxts(NULL);
	RTE_TEST_ASSERT_EQUAL(ret, -EINVAL,
			     "NULL nic_dev should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_init_rq_cqe_compact(void)
{
	g_io_nic_dev.num_rqs = 1;
	g_io_rxq_ptrs[0] = &g_cqe_rxq;
	g_cqe_rxq.wqe_type = HINIC3_COMPACT_RQ_WQE;
	g_io_nic_dev.config.rx_cqe_coalesce_num = 7;
	g_io_nic_dev.config.rx_cqe_timer_loop = 8;

	int ret = hinic3_init_rq_cqe_ctxts(&g_io_nic_dev);
	RTE_TEST_ASSERT_EQUAL(ret, 0,
			     "compact wqe_type should succeed");
	return TEST_SUCCESS;
}

static int
test_init_rq_cqe_normal(void)
{
	g_io_nic_dev.num_rqs = 1;
	g_io_rxq_ptrs[0] = &g_cqe_rxq;
	g_cqe_rxq.wqe_type = HINIC3_NORMAL_RQ_WQE;

	int ret = hinic3_init_rq_cqe_ctxts(&g_io_nic_dev);
	RTE_TEST_ASSERT_EQUAL(ret, 0,
			     "normal wqe_type should succeed");
	return TEST_SUCCESS;
}

static int
test_init_rq_cqe_extend(void)
{
	g_io_nic_dev.num_rqs = 1;
	g_io_rxq_ptrs[0] = &g_cqe_rxq;
	g_cqe_rxq.wqe_type = HINIC3_EXTEND_RQ_WQE;

	int ret = hinic3_init_rq_cqe_ctxts(&g_io_nic_dev);
	RTE_TEST_ASSERT_EQUAL(ret, 0,
			     "extend wqe_type should succeed");
	return TEST_SUCCESS;
}

static int
test_init_rq_cqe_null_rxq(void)
{
	g_io_nic_dev.num_rqs = 1;
	g_io_rxq_ptrs[0] = NULL;

	int ret = hinic3_init_rq_cqe_ctxts(&g_io_nic_dev);
	RTE_TEST_ASSERT_EQUAL(ret, -EINVAL,
			     "NULL rxq should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_init_rq_cqe_mgmt_fail(void)
{
	g_io_nic_dev.num_rqs = 1;
	g_io_rxq_ptrs[0] = &g_cqe_rxq;
	g_cqe_rxq.wqe_type = HINIC3_COMPACT_RQ_WQE;
	g_mock_mgmt_sync_ret = -1;

	int ret = hinic3_init_rq_cqe_ctxts(&g_io_nic_dev);
	RTE_TEST_ASSERT_EQUAL(ret, -EFAULT,
			     "mgmt sync failure should return -EFAULT");

	g_mock_mgmt_sync_ret = 0;
	return TEST_SUCCESS;
}

/* ====== Test suite ====== */

static struct unit_test_suite hinic3_nic_io_test_suite = {
	.suite_name = "HINIC3 NIC IO Unit Tests",
	.setup = NULL,
	.teardown = NULL,
	.unit_test_cases = {
		/* hinic3_update_driver_feature */
		TEST_CASE_ST(nic_io_test_setup, nic_io_test_teardown,
			     test_update_driver_feature),
		TEST_CASE_ST(nic_io_test_setup, nic_io_test_teardown,
			     test_update_driver_feature_null),

		/* hinic3_get_func_rx_buf_size */
		TEST_CASE_ST(nic_io_test_setup, nic_io_test_teardown,
			     test_get_rx_buf_size_min),
		TEST_CASE_ST(nic_io_test_setup, nic_io_test_teardown,
			     test_get_rx_buf_size_null_rxq),
		TEST_CASE_ST(nic_io_test_setup, nic_io_test_teardown,
			     test_get_rx_buf_size_hairpin),
		TEST_CASE_ST(nic_io_test_setup, nic_io_test_teardown,
			     test_get_rx_buf_size_zero_rqs),
		TEST_CASE_ST(nic_io_test_setup, nic_io_test_teardown,
			     test_get_rx_buf_size_all_same),

		/* hinic3_set_rq_enable */
		TEST_CASE_ST(nic_io_test_setup, nic_io_test_teardown,
			     test_set_rq_enable_success),
		TEST_CASE_ST(nic_io_test_setup, nic_io_test_teardown,
			     test_set_rq_enable_fail),
		TEST_CASE_ST(nic_io_test_setup, nic_io_test_teardown,
			     test_set_rq_enable_null),

		/* hinic3_sq_prepare_ctxt */
		TEST_CASE_ST(nic_io_test_setup, nic_io_test_teardown,
			     test_sq_prepare_ctxt_htn),
		TEST_CASE_ST(nic_io_test_setup, nic_io_test_teardown,
			     test_sq_prepare_ctxt_stn),
		TEST_CASE_ST(nic_io_test_setup, nic_io_test_teardown,
			     test_sq_prepare_ctxt_zero_indices),
		TEST_CASE_ST(nic_io_test_setup, nic_io_test_teardown,
			     test_sq_prepare_ctxt_max_indices),

		/* hinic3_rq_prepare_ctxt */
		TEST_CASE_ST(nic_io_test_setup, nic_io_test_teardown,
			     test_rq_prepare_ctxt_extend_htn),
		TEST_CASE_ST(nic_io_test_setup, nic_io_test_teardown,
			     test_rq_prepare_ctxt_normal_htn),
		TEST_CASE_ST(nic_io_test_setup, nic_io_test_teardown,
			     test_rq_prepare_ctxt_compact_htn),
		TEST_CASE_ST(nic_io_test_setup, nic_io_test_teardown,
			     test_rq_prepare_ctxt_compact_stn),
		TEST_CASE_ST(nic_io_test_setup, nic_io_test_teardown,
			     test_rq_prepare_ctxt_pi_dma_addr),

		/* hinic3_free_qp_ctxts */
		TEST_CASE_ST(nic_io_test_setup, nic_io_test_teardown,
			     test_free_qp_ctxts_null),
		TEST_CASE_ST(nic_io_test_setup, nic_io_test_teardown,
			     test_free_qp_ctxts_success),

		/* hinic3_init_rq_cqe_ctxts */
		TEST_CASE_ST(nic_io_test_setup, nic_io_test_teardown,
			     test_init_rq_cqe_null),
		TEST_CASE_ST(nic_io_test_setup, nic_io_test_teardown,
			     test_init_rq_cqe_compact),
		TEST_CASE_ST(nic_io_test_setup, nic_io_test_teardown,
			     test_init_rq_cqe_normal),
		TEST_CASE_ST(nic_io_test_setup, nic_io_test_teardown,
			     test_init_rq_cqe_extend),
		TEST_CASE_ST(nic_io_test_setup, nic_io_test_teardown,
			     test_init_rq_cqe_null_rxq),
		TEST_CASE_ST(nic_io_test_setup, nic_io_test_teardown,
			     test_init_rq_cqe_mgmt_fail),

		TEST_CASES_END()
	}
};

static int
test_hinic3_nic_io(void)
{
	return unit_test_suite_runner(&hinic3_nic_io_test_suite);
}

REGISTER_TEST_COMMAND(hinic3_nic_io_autotest, test_hinic3_nic_io);
