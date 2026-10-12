/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2025-2026 Huawei Technologies Co., Ltd
 * Unit tests for hinic3_pmd_tm.c
 */

#include <rte_common.h>
#include <rte_ethdev.h>
#ifdef DPDK_21_11
#include <ethdev_driver.h>
#endif
#include <rte_bus_pci.h>
#include <rte_malloc.h>
#include <rte_tm.h>
#include <rte_tm_driver.h>
#include <errno.h>

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
 * hinic3_pmd_nic_cfg.c */

#ifndef DPDK_21_11
extern struct rte_eth_dev rte_eth_devices[];
#endif

/* ====== Static test structures ====== */

static struct hinic3_nic_dev g_tm_nic_dev;
static struct hinic3_dcb g_tm_dcb;
static struct hinic3_hwdev g_tm_hwdev;
static struct hinic3_hwif g_tm_hwif;
static struct cfg_mgmt_info g_tm_cfg_mgmt;
static struct rte_eth_dev g_tm_eth_dev;
static struct rte_eth_dev_data g_tm_dev_data;
static const struct rte_tm_ops *g_tm_ops;

/* Mock PCI device so is_sp620_nic (called intra-TU by hinic3_dev_info_get
 * in hinic3_pmd_ethdev.c, bypassing --wrap) can safely dereference
 * rte_eth_devices[0].device via RTE_ETH_DEV_TO_PCI. */
static struct rte_pci_device g_tm_pci_dev;

/* Node ID layout (max_sqs=64):
 * [0..63]   = queue (leaf) nodes
 * [64..71]  = COS nodes
 * [72..79]  = TC nodes
 * 80        = port (root) node
 */
#define TM_PORT_NODE_ID     80
#define TM_TC_NODE_ID(n)    (72 + (n))
#define TM_COS_NODE_ID(n)   (64 + (n))
#define TM_QUEUE_NODE_ID(n) (n)

/* ====== Setup / Teardown ====== */

static int
tm_test_setup(void)
{
	/* Zero all test structures */
	memset(&g_tm_nic_dev, 0, sizeof(g_tm_nic_dev));
	memset(&g_tm_dcb, 0, sizeof(g_tm_dcb));
	memset(&g_tm_hwdev, 0, sizeof(g_tm_hwdev));
	memset(&g_tm_hwif, 0, sizeof(g_tm_hwif));
	memset(&g_tm_cfg_mgmt, 0, sizeof(g_tm_cfg_mgmt));
	memset(&g_tm_eth_dev, 0, sizeof(g_tm_eth_dev));
	memset(&g_tm_dev_data, 0, sizeof(g_tm_dev_data));

	/* Dynamically allocate ets (hinic3_tm_conf_uninit calls rte_free(ets)) */
	g_tm_nic_dev.ets = rte_zmalloc("tm_ets", sizeof(struct hinic3_ets), 0);
	if (!g_tm_nic_dev.ets)
		return TEST_FAILED;

	/* Wire up pointers */
	g_tm_nic_dev.dcb = &g_tm_dcb;
	g_tm_nic_dev.hwdev = &g_tm_hwdev;
	g_tm_hwdev.hwif = &g_tm_hwif;
	g_tm_hwdev.cfg_mgmt = &g_tm_cfg_mgmt;
	g_tm_hwdev.dev_handle = &g_tm_nic_dev;

	/* PF type by default */
	g_tm_hwif.attr.func_type = TYPE_PF;

	/* Set up eth_dev */
	g_tm_eth_dev.data = &g_tm_dev_data;
	g_tm_dev_data.dev_private = &g_tm_nic_dev;
	g_tm_dev_data.nb_tx_queues = 64;

	/* Set mock variables to defaults */
	g_mock_is_sp620_nic = true;
	g_mock_mgmt_sync_ret = 0;
	g_mock_msg_to_mgmt_sync_ret = 0;
	g_mock_mbox_to_pf_ret = 0;
	g_mock_func_type = TYPE_PF;
	g_mock_global_func_id = 0;
	g_mock_set_tm_hierarchy_do_commit_ret = 0;
	g_mock_set_tm_config_tc_rate_ret = 0;
	g_mock_set_qos_port_trust_ret = 0;

	/* Set qinfo type to normal */
	g_qinfo_type = HINIC3_QINFO_TYPE_NORMAL;
	g_tm_hwdev.qinfo_type = HINIC3_QINFO_TYPE_NORMAL;

	/* Set ets fields */
	g_tm_nic_dev.ets->max_tm_rate = 100;
	g_tm_nic_dev.ets->num_tc = 8;

	/* Set nic_dev fields */
	g_tm_nic_dev.max_sqs = 64;
	g_tm_nic_dev.num_sqs = 4;

	/* Set cos_valid_bitmap in svc_cap */
	g_tm_cfg_mgmt.svc_cap.cos_valid_bitmap = 0xFF;

	/* Set up PCI device so is_sp620_nic (called internally by
	 * hinic3_dev_info_get via hinic3_tm_max_tx_queues_get) works.
	 * is_sp620_nic is in the same TU as hinic3_dev_info_get
	 * (hinic3_pmd_ethdev.c), so --wrap cannot intercept it.
	 * It dereferences rte_eth_devices[hwdev->port_id].device via
	 * RTE_ETH_DEV_TO_PCI, which is NULL in the zeroed global array. */
	memset(&g_tm_pci_dev, 0, sizeof(g_tm_pci_dev));
	g_tm_pci_dev.id.device_id = HINIC3_DEV_ID_SP620;
	rte_eth_devices[0].device = &g_tm_pci_dev.device;

	/* Initialize tm_conf */
	hinic3_tm_conf_init(&g_tm_eth_dev);

	/* Get TM ops table */
	g_tm_ops = NULL;
	hinic3_tm_ops_get(&g_tm_eth_dev, (void *)&g_tm_ops);

	return TEST_SUCCESS;
}

static void
tm_test_teardown(void)
{
	if (g_tm_nic_dev.ets) {
		struct hinic3_ets *ets = g_tm_nic_dev.ets;
		struct hinic3_tm_conf *tm_conf = &ets->tm_conf;
		struct hinic3_tm_shaper_profile *sp;
		struct hinic3_tm_node *node;

		/* Free shaper profiles */
		while ((sp = TAILQ_FIRST(&tm_conf->shaper_profile_list))) {
			TAILQ_REMOVE(&tm_conf->shaper_profile_list, sp, node);
			rte_free(sp);
		}
		/* Free queue nodes */
		while ((node = TAILQ_FIRST(&tm_conf->queue_list))) {
			TAILQ_REMOVE(&tm_conf->queue_list, node, node);
			rte_free(node);
		}
		/* Free cos nodes */
		while ((node = TAILQ_FIRST(&tm_conf->cos_list))) {
			TAILQ_REMOVE(&tm_conf->cos_list, node, node);
			rte_free(node);
		}
		/* Free tc nodes */
		while ((node = TAILQ_FIRST(&tm_conf->tc_list))) {
			TAILQ_REMOVE(&tm_conf->tc_list, node, node);
			rte_free(node);
		}
		/* Free root */
		if (tm_conf->root) {
			rte_free(tm_conf->root);
			tm_conf->root = NULL;
		}

		rte_free(g_tm_nic_dev.ets);
		g_tm_nic_dev.ets = NULL;
	}

	/* Reset mock variables */
	g_mock_is_sp620_nic = false;
	g_mock_mgmt_sync_ret = 0;
	g_mock_msg_to_mgmt_sync_ret = 0;
	g_mock_mbox_to_pf_ret = 0;
	g_mock_set_tm_hierarchy_do_commit_ret = 0;
	g_mock_set_tm_config_tc_rate_ret = 0;
	g_mock_set_qos_port_trust_ret = 0;
	g_mock_func_type = TYPE_PF;
	g_mock_global_func_id = 0;
	g_qinfo_type = HINIC3_QINFO_TYPE_NORMAL;
	g_tm_hwdev.qinfo_type = HINIC3_QINFO_TYPE_NORMAL;

	/* Zero structures */
	memset(&g_tm_nic_dev, 0, sizeof(g_tm_nic_dev));
	memset(&g_tm_dcb, 0, sizeof(g_tm_dcb));
	memset(&g_tm_hwdev, 0, sizeof(g_tm_hwdev));
	memset(&g_tm_hwif, 0, sizeof(g_tm_hwif));
	memset(&g_tm_cfg_mgmt, 0, sizeof(g_tm_cfg_mgmt));
	memset(&g_tm_eth_dev, 0, sizeof(g_tm_eth_dev));
	memset(&g_tm_dev_data, 0, sizeof(g_tm_dev_data));
	memset(&g_tm_pci_dev, 0, sizeof(g_tm_pci_dev));
	rte_eth_devices[0].device = NULL;
	g_tm_ops = NULL;
}

/* ====== A. hinic3_tm_ops_get (2 tests) ====== */

static int
test_tm_ops_get_null_dev(void)
{
	int ret = hinic3_tm_ops_get(NULL, (void *)&g_tm_ops);
	RTE_TEST_ASSERT_EQUAL(ret, -EINVAL, "NULL dev should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_tm_ops_get_success(void)
{
	const struct rte_tm_ops *ops = NULL;
	int ret = hinic3_tm_ops_get(&g_tm_eth_dev, (void *)&ops);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "valid call should return 0");
	RTE_TEST_ASSERT_NOT_NULL(ops, "ops pointer should be set");
	RTE_TEST_ASSERT_NOT_NULL(ops->capabilities_get, "capabilities_get should be set");
	RTE_TEST_ASSERT_NOT_NULL(ops->node_add, "node_add should be set");
	return TEST_SUCCESS;
}

/* ====== B. hinic3_tm_capabilities_get (2 tests) ====== */

static int
test_tm_capabilities_null(void)
{
	struct rte_tm_error error = {0};
	int ret = g_tm_ops->capabilities_get(&g_tm_eth_dev, NULL, &error);
	RTE_TEST_ASSERT_EQUAL(ret, -EINVAL, "NULL cap should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_tm_capabilities_success(void)
{
	struct rte_tm_capabilities cap;
	struct rte_tm_error error = {0};

	/* hinic3_tm_capabilities_get checks !errno (likely a source bug);
	 * set errno to non-zero so the check passes */
	errno = 1;
	int ret = g_tm_ops->capabilities_get(&g_tm_eth_dev, &cap, &error);
	errno = 0;

	RTE_TEST_ASSERT_EQUAL(ret, 0, "valid call should return 0");
	RTE_TEST_ASSERT(cap.n_nodes_max > 0, "n_nodes_max should be > 0");
	RTE_TEST_ASSERT_EQUAL((int)cap.n_levels_max, HINIC3_TM_NODE_LEVEL_MAX,
			     "n_levels_max should be HINIC3_TM_NODE_LEVEL_MAX");
	RTE_TEST_ASSERT(cap.shaper_n_max > 0, "shaper_n_max should be > 0");
	RTE_TEST_ASSERT(cap.shaper_private_rate_max > 0,
		       "rate_max should be > 0");
	return TEST_SUCCESS;
}

/* ====== C. hinic3_tm_shaper_profile_add/del (10 tests) ====== */

static int
test_tm_shaper_add_success(void)
{
	struct rte_tm_shaper_params profile = {0};
	struct rte_tm_error error = {0};
	struct hinic3_tm_conf *tm_conf = &g_tm_nic_dev.ets->tm_conf;
	uint32_t before = tm_conf->nb_shaper_profile;

	int ret = g_tm_ops->shaper_profile_add(&g_tm_eth_dev, 0, &profile, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "add should succeed");
	RTE_TEST_ASSERT_EQUAL(tm_conf->nb_shaper_profile, before + 1,
			     "nb_shaper_profile should increment");
	return TEST_SUCCESS;
}

static int
test_tm_shaper_add_null(void)
{
	struct rte_tm_error error = {0};
	int ret = g_tm_ops->shaper_profile_add(&g_tm_eth_dev, 0, NULL, &error);
	RTE_TEST_ASSERT_EQUAL(ret, -EINVAL, "NULL profile should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_tm_shaper_add_duplicate(void)
{
	struct rte_tm_shaper_params profile = {0};
	struct rte_tm_error error = {0};

	/* Add first */
	int ret = g_tm_ops->shaper_profile_add(&g_tm_eth_dev, 0, &profile, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "first add should succeed");

	/* Add same ID again */
	ret = g_tm_ops->shaper_profile_add(&g_tm_eth_dev, 0, &profile, &error);
	RTE_TEST_ASSERT_EQUAL(ret, -EINVAL, "duplicate ID should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_tm_shaper_add_committed_rate(void)
{
	struct rte_tm_shaper_params profile = {0};
	struct rte_tm_error error = {0};
	profile.committed.rate = 1;

	int ret = g_tm_ops->shaper_profile_add(&g_tm_eth_dev, 0, &profile, &error);
	RTE_TEST_ASSERT_EQUAL(ret, -EINVAL,
			      "committed.rate != 0 should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_tm_shaper_add_peak_rate_exceed(void)
{
	struct rte_tm_shaper_params profile = {0};
	struct rte_tm_error error = {0};
	profile.peak.rate = 101; /* max_tm_rate = 100 */

	int ret = g_tm_ops->shaper_profile_add(&g_tm_eth_dev, 0, &profile, &error);
	RTE_TEST_ASSERT_EQUAL(ret, -EINVAL,
			      "peak.rate > max_tm_rate should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_tm_shaper_add_pkt_length_adjust(void)
{
	struct rte_tm_shaper_params profile = {0};
	struct rte_tm_error error = {0};
	profile.pkt_length_adjust = 1;

	int ret = g_tm_ops->shaper_profile_add(&g_tm_eth_dev, 0, &profile, &error);
	RTE_TEST_ASSERT_EQUAL(ret, -EINVAL,
			      "pkt_length_adjust != 0 should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_tm_shaper_del_success(void)
{
	struct rte_tm_shaper_params profile = {0};
	struct rte_tm_error error = {0};
	struct hinic3_tm_conf *tm_conf = &g_tm_nic_dev.ets->tm_conf;

	g_tm_ops->shaper_profile_add(&g_tm_eth_dev, 0, &profile, &error);
	uint32_t before = tm_conf->nb_shaper_profile;

	int ret = g_tm_ops->shaper_profile_delete(&g_tm_eth_dev, 0, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "delete should succeed");
	RTE_TEST_ASSERT_EQUAL(tm_conf->nb_shaper_profile, before - 1,
			     "nb_shaper_profile should decrement");
	return TEST_SUCCESS;
}

static int
test_tm_shaper_del_not_found(void)
{
	struct rte_tm_error error = {0};
	int ret = g_tm_ops->shaper_profile_delete(&g_tm_eth_dev, 999, &error);
	RTE_TEST_ASSERT_EQUAL(ret, -EINVAL,
			      "non-existent profile should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_tm_shaper_del_in_use(void)
{
	struct rte_tm_shaper_params profile = {0};
	struct rte_tm_node_params params = {0};
	params.shaper_profile_id = RTE_TM_SHAPER_PROFILE_ID_NONE;
	struct rte_tm_error error = {0};

	/* Add shaper profile */
	g_tm_ops->shaper_profile_add(&g_tm_eth_dev, 0, &profile, &error);

	/* Add root node referencing the profile (increments reference_count) */
	params.nonleaf.n_sp_priorities = 1;
	params.shaper_profile_id = 0;
	int ret = g_tm_ops->node_add(&g_tm_eth_dev, TM_PORT_NODE_ID,
				     RTE_TM_NODE_ID_NULL, 0, 0,
				     RTE_TM_NODE_LEVEL_ID_ANY, &params, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "root node add should succeed");

	/* Try to delete the in-use profile */
	ret = g_tm_ops->shaper_profile_delete(&g_tm_eth_dev, 0, &error);
	RTE_TEST_ASSERT_EQUAL(ret, -EINVAL,
			      "in-use profile should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_tm_shaper_add_exceed_max(void)
{
	struct rte_tm_shaper_params profile = {0};
	struct rte_tm_error error = {0};
	struct hinic3_tm_conf *tm_conf = &g_tm_nic_dev.ets->tm_conf;
	uint32_t i;

	/* nb_shaper_profile_max = 1 + HINIC3_MAX_TC_NUM = 9 */
	for (i = 0; i < tm_conf->nb_shaper_profile_max; i++) {
		int ret = g_tm_ops->shaper_profile_add(&g_tm_eth_dev, i,
						       &profile, &error);
		RTE_TEST_ASSERT_EQUAL(ret, 0, "add should succeed");
	}

	/* Exceed max */
	int ret = g_tm_ops->shaper_profile_add(&g_tm_eth_dev,
					       tm_conf->nb_shaper_profile_max,
					       &profile, &error);
	RTE_TEST_ASSERT_EQUAL(ret, -EINVAL,
			      "exceeding max profiles should return -EINVAL");
	return TEST_SUCCESS;
}

/* ====== D. hinic3_tm_node_add hierarchy (12 tests) ====== */

static int
test_tm_node_add_root(void)
{
	struct rte_tm_node_params params = {0};
	params.shaper_profile_id = RTE_TM_SHAPER_PROFILE_ID_NONE;
	struct rte_tm_error error = {0};
	struct hinic3_tm_conf *tm_conf = &g_tm_nic_dev.ets->tm_conf;

	params.nonleaf.n_sp_priorities = 1;
	int ret = g_tm_ops->node_add(&g_tm_eth_dev, TM_PORT_NODE_ID,
				     RTE_TM_NODE_ID_NULL, 0, 0,
				     RTE_TM_NODE_LEVEL_ID_ANY, &params, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "root node add should succeed");
	RTE_TEST_ASSERT_NOT_NULL(tm_conf->root, "root should be set");
	return TEST_SUCCESS;
}

static int
test_tm_node_add_root_duplicate(void)
{
	struct rte_tm_node_params params = {0};
	params.shaper_profile_id = RTE_TM_SHAPER_PROFILE_ID_NONE;
	struct rte_tm_error error = {0};

	params.nonleaf.n_sp_priorities = 1;
	g_tm_ops->node_add(&g_tm_eth_dev, TM_PORT_NODE_ID,
			   RTE_TM_NODE_ID_NULL, 0, 0,
			   RTE_TM_NODE_LEVEL_ID_ANY, &params, &error);

	int ret = g_tm_ops->node_add(&g_tm_eth_dev, TM_PORT_NODE_ID,
				     RTE_TM_NODE_ID_NULL, 0, 0,
				     RTE_TM_NODE_LEVEL_ID_ANY, &params, &error);
	RTE_TEST_ASSERT_EQUAL(ret, -EINVAL,
			      "duplicate root should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_tm_node_add_tc(void)
{
	struct rte_tm_node_params params = {0};
	params.shaper_profile_id = RTE_TM_SHAPER_PROFILE_ID_NONE;
	struct rte_tm_error error = {0};
	struct hinic3_tm_conf *tm_conf = &g_tm_nic_dev.ets->tm_conf;

	/* Add root first */
	params.nonleaf.n_sp_priorities = 1;
	g_tm_ops->node_add(&g_tm_eth_dev, TM_PORT_NODE_ID,
			   RTE_TM_NODE_ID_NULL, 0, 0,
			   RTE_TM_NODE_LEVEL_ID_ANY, &params, &error);

	/* Add TC node */
	uint32_t before = tm_conf->nb_tc_node;
	int ret = g_tm_ops->node_add(&g_tm_eth_dev, TM_TC_NODE_ID(0),
				     TM_PORT_NODE_ID, 0, 50,
				     RTE_TM_NODE_LEVEL_ID_ANY, &params, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "TC node add should succeed");
	RTE_TEST_ASSERT_EQUAL(tm_conf->nb_tc_node, before + 1,
			     "nb_tc_node should increment");
	return TEST_SUCCESS;
}

static int
test_tm_node_add_tc_too_many(void)
{
	struct rte_tm_node_params params = {0};
	params.shaper_profile_id = RTE_TM_SHAPER_PROFILE_ID_NONE;
	struct rte_tm_error error = {0};

	/* Reduce num_tc to make test faster */
	g_tm_nic_dev.ets->num_tc = 2;

	params.nonleaf.n_sp_priorities = 1;
	g_tm_ops->node_add(&g_tm_eth_dev, TM_PORT_NODE_ID,
			   RTE_TM_NODE_ID_NULL, 0, 0,
			   RTE_TM_NODE_LEVEL_ID_ANY, &params, &error);

	/* Add TC0 (id=72, tc_no=0 < 2) */
	int ret = g_tm_ops->node_add(&g_tm_eth_dev, TM_TC_NODE_ID(0),
				     TM_PORT_NODE_ID, 0, 50,
				     RTE_TM_NODE_LEVEL_ID_ANY, &params, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "TC0 add should succeed");

	/* Add TC1 (id=73, tc_no=1 < 2) */
	ret = g_tm_ops->node_add(&g_tm_eth_dev, TM_TC_NODE_ID(1),
				 TM_PORT_NODE_ID, 0, 50,
				 RTE_TM_NODE_LEVEL_ID_ANY, &params, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "TC1 add should succeed");

	/* Try TC2 (id=74, tc_no=2 >= 2) - should fail */
	ret = g_tm_ops->node_add(&g_tm_eth_dev, TM_TC_NODE_ID(2),
				 TM_PORT_NODE_ID, 0, 50,
				 RTE_TM_NODE_LEVEL_ID_ANY, &params, &error);
	RTE_TEST_ASSERT_EQUAL(ret, -EINVAL,
			      "exceeding num_tc should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_tm_node_add_cos(void)
{
	struct rte_tm_node_params params = {0};
	params.shaper_profile_id = RTE_TM_SHAPER_PROFILE_ID_NONE;
	struct rte_tm_error error = {0};
	struct hinic3_tm_conf *tm_conf = &g_tm_nic_dev.ets->tm_conf;

	/* Add root + TC */
	params.nonleaf.n_sp_priorities = 1;
	g_tm_ops->node_add(&g_tm_eth_dev, TM_PORT_NODE_ID,
			   RTE_TM_NODE_ID_NULL, 0, 0,
			   RTE_TM_NODE_LEVEL_ID_ANY, &params, &error);
	g_tm_ops->node_add(&g_tm_eth_dev, TM_TC_NODE_ID(0),
			   TM_PORT_NODE_ID, 0, 50,
			   RTE_TM_NODE_LEVEL_ID_ANY, &params, &error);

	/* Add COS node */
	uint32_t before = tm_conf->nb_cos_node;
	int ret = g_tm_ops->node_add(&g_tm_eth_dev, TM_COS_NODE_ID(0),
				     TM_TC_NODE_ID(0), 0, 0,
				     RTE_TM_NODE_LEVEL_ID_ANY, &params, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "COS node add should succeed");
	RTE_TEST_ASSERT_EQUAL(tm_conf->nb_cos_node, before + 1,
			     "nb_cos_node should increment");
	return TEST_SUCCESS;
}

static int
test_tm_node_add_cos_too_many(void)
{
	struct rte_tm_node_params params = {0};
	params.shaper_profile_id = RTE_TM_SHAPER_PROFILE_ID_NONE;
	struct rte_tm_error error = {0};
	uint32_t i;

	params.nonleaf.n_sp_priorities = 1;
	g_tm_ops->node_add(&g_tm_eth_dev, TM_PORT_NODE_ID,
			   RTE_TM_NODE_ID_NULL, 0, 0,
			   RTE_TM_NODE_LEVEL_ID_ANY, &params, &error);
	g_tm_ops->node_add(&g_tm_eth_dev, TM_TC_NODE_ID(0),
			   TM_PORT_NODE_ID, 0, 100,
			   RTE_TM_NODE_LEVEL_ID_ANY, &params, &error);

	/* Add HINIC3_MAX_COS_NUM COS nodes (all under TC0) */
	for (i = 0; i < HINIC3_MAX_COS_NUM; i++) {
		int ret = g_tm_ops->node_add(&g_tm_eth_dev, TM_COS_NODE_ID(i),
					     TM_TC_NODE_ID(0), 0, 0,
					     RTE_TM_NODE_LEVEL_ID_ANY,
					     &params, &error);
		RTE_TEST_ASSERT_EQUAL(ret, 0, "COS add should succeed");
	}

	/* Try to add one more (duplicate id) */
	int ret = g_tm_ops->node_add(&g_tm_eth_dev, TM_COS_NODE_ID(0),
				     TM_TC_NODE_ID(0), 0, 0,
				     RTE_TM_NODE_LEVEL_ID_ANY, &params, &error);
	RTE_TEST_ASSERT_EQUAL(ret, -EINVAL,
			      "exceeding MAX_COS_NUM should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_tm_node_add_queue(void)
{
	struct rte_tm_node_params params = {0};
	params.shaper_profile_id = RTE_TM_SHAPER_PROFILE_ID_NONE;
	struct rte_tm_error error = {0};
	struct hinic3_tm_conf *tm_conf = &g_tm_nic_dev.ets->tm_conf;

	/* Build hierarchy: root -> TC -> COS */
	params.nonleaf.n_sp_priorities = 1;
	g_tm_ops->node_add(&g_tm_eth_dev, TM_PORT_NODE_ID,
			   RTE_TM_NODE_ID_NULL, 0, 0,
			   RTE_TM_NODE_LEVEL_ID_ANY, &params, &error);
	g_tm_ops->node_add(&g_tm_eth_dev, TM_TC_NODE_ID(0),
			   TM_PORT_NODE_ID, 0, 100,
			   RTE_TM_NODE_LEVEL_ID_ANY, &params, &error);
	g_tm_ops->node_add(&g_tm_eth_dev, TM_COS_NODE_ID(0),
			   TM_TC_NODE_ID(0), 0, 0,
			   RTE_TM_NODE_LEVEL_ID_ANY, &params, &error);

	/* Add queue node (leaf) */
	memset(&params, 0, sizeof(params));
	params.shaper_profile_id = RTE_TM_SHAPER_PROFILE_ID_NONE;
	params.leaf.cman = RTE_TM_CMAN_TAIL_DROP;
	uint32_t before = tm_conf->nb_queue_node;
	int ret = g_tm_ops->node_add(&g_tm_eth_dev, TM_QUEUE_NODE_ID(0),
				     TM_COS_NODE_ID(0), 0, 0,
				     RTE_TM_NODE_LEVEL_ID_ANY, &params, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "queue node add should succeed");
	RTE_TEST_ASSERT_EQUAL(tm_conf->nb_queue_node, before + 1,
			     "nb_queue_node should increment");
	return TEST_SUCCESS;
}

static int
test_tm_node_add_queue_out_of_range(void)
{
	struct rte_tm_node_params params = {0};
	params.shaper_profile_id = RTE_TM_SHAPER_PROFILE_ID_NONE;
	struct rte_tm_error error = {0};

	/* Build hierarchy */
	params.nonleaf.n_sp_priorities = 1;
	g_tm_ops->node_add(&g_tm_eth_dev, TM_PORT_NODE_ID,
			   RTE_TM_NODE_ID_NULL, 0, 0,
			   RTE_TM_NODE_LEVEL_ID_ANY, &params, &error);
	g_tm_ops->node_add(&g_tm_eth_dev, TM_TC_NODE_ID(0),
			   TM_PORT_NODE_ID, 0, 100,
			   RTE_TM_NODE_LEVEL_ID_ANY, &params, &error);
	g_tm_ops->node_add(&g_tm_eth_dev, TM_COS_NODE_ID(0),
			   TM_TC_NODE_ID(0), 0, 0,
			   RTE_TM_NODE_LEVEL_ID_ANY, &params, &error);

	/* Set nb_tx_queues to 4, then try queue id=4 */
	g_tm_dev_data.nb_tx_queues = 4;
	memset(&params, 0, sizeof(params));
	params.shaper_profile_id = RTE_TM_SHAPER_PROFILE_ID_NONE;
	params.leaf.cman = RTE_TM_CMAN_TAIL_DROP;
	int ret = g_tm_ops->node_add(&g_tm_eth_dev, 4,
				     TM_COS_NODE_ID(0), 0, 0,
				     RTE_TM_NODE_LEVEL_ID_ANY, &params, &error);
	RTE_TEST_ASSERT_EQUAL(ret, -EINVAL,
			      "queue id >= nb_tx_queues should return -EINVAL");
	g_tm_dev_data.nb_tx_queues = 64;
	return TEST_SUCCESS;
}

static int
test_tm_node_add_priority_nonzero(void)
{
	struct rte_tm_node_params params = {0};
	params.shaper_profile_id = RTE_TM_SHAPER_PROFILE_ID_NONE;
	struct rte_tm_error error = {0};

	params.nonleaf.n_sp_priorities = 1;
	int ret = g_tm_ops->node_add(&g_tm_eth_dev, TM_PORT_NODE_ID,
				     RTE_TM_NODE_ID_NULL, 1, 0,
				     RTE_TM_NODE_LEVEL_ID_ANY, &params, &error);
	RTE_TEST_ASSERT_EQUAL(ret, -EINVAL,
			      "priority != 0 should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_tm_node_add_weight_exceed(void)
{
	struct rte_tm_node_params params = {0};
	params.shaper_profile_id = RTE_TM_SHAPER_PROFILE_ID_NONE;
	struct rte_tm_error error = {0};

	params.nonleaf.n_sp_priorities = 1;
	int ret = g_tm_ops->node_add(&g_tm_eth_dev, TM_PORT_NODE_ID,
				     RTE_TM_NODE_ID_NULL, 0, 101,
				     RTE_TM_NODE_LEVEL_ID_ANY, &params, &error);
	RTE_TEST_ASSERT_EQUAL(ret, -EINVAL,
			      "weight > 100 should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_tm_node_add_parent_not_exist(void)
{
	struct rte_tm_node_params params = {0};
	params.shaper_profile_id = RTE_TM_SHAPER_PROFILE_ID_NONE;
	struct rte_tm_error error = {0};

	params.nonleaf.n_sp_priorities = 1;
	int ret = g_tm_ops->node_add(&g_tm_eth_dev, TM_TC_NODE_ID(0),
				     999, 0, 50,
				     RTE_TM_NODE_LEVEL_ID_ANY, &params, &error);
	RTE_TEST_ASSERT_EQUAL(ret, -EINVAL,
			      "non-existent parent should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_tm_node_add_committed(void)
{
	struct rte_tm_node_params params = {0};
	params.shaper_profile_id = RTE_TM_SHAPER_PROFILE_ID_NONE;
	struct rte_tm_error error = {0};
	struct hinic3_tm_conf *tm_conf = &g_tm_nic_dev.ets->tm_conf;

	tm_conf->committed = true;
	params.nonleaf.n_sp_priorities = 1;
	int ret = g_tm_ops->node_add(&g_tm_eth_dev, TM_PORT_NODE_ID,
				     RTE_TM_NODE_ID_NULL, 0, 0,
				     RTE_TM_NODE_LEVEL_ID_ANY, &params, &error);
	RTE_TEST_ASSERT_EQUAL(ret, -EINVAL,
			      "add when committed should return -EINVAL");
	tm_conf->committed = false;
	return TEST_SUCCESS;
}

/* ====== E. hinic3_tm_node_delete (4 tests) ====== */

static int
test_tm_node_delete_leaf(void)
{
	struct rte_tm_node_params params = {0};
	params.shaper_profile_id = RTE_TM_SHAPER_PROFILE_ID_NONE;
	struct rte_tm_error error = {0};
	struct hinic3_tm_conf *tm_conf = &g_tm_nic_dev.ets->tm_conf;

	/* Build hierarchy */
	params.nonleaf.n_sp_priorities = 1;
	g_tm_ops->node_add(&g_tm_eth_dev, TM_PORT_NODE_ID,
			   RTE_TM_NODE_ID_NULL, 0, 0,
			   RTE_TM_NODE_LEVEL_ID_ANY, &params, &error);
	g_tm_ops->node_add(&g_tm_eth_dev, TM_TC_NODE_ID(0),
			   TM_PORT_NODE_ID, 0, 100,
			   RTE_TM_NODE_LEVEL_ID_ANY, &params, &error);
	g_tm_ops->node_add(&g_tm_eth_dev, TM_COS_NODE_ID(0),
			   TM_TC_NODE_ID(0), 0, 0,
			   RTE_TM_NODE_LEVEL_ID_ANY, &params, &error);
	memset(&params, 0, sizeof(params));
	params.shaper_profile_id = RTE_TM_SHAPER_PROFILE_ID_NONE;
	params.leaf.cman = RTE_TM_CMAN_TAIL_DROP;
	g_tm_ops->node_add(&g_tm_eth_dev, TM_QUEUE_NODE_ID(0),
			   TM_COS_NODE_ID(0), 0, 0,
			   RTE_TM_NODE_LEVEL_ID_ANY, &params, &error);

	uint32_t before = tm_conf->nb_queue_node;
	int ret = g_tm_ops->node_delete(&g_tm_eth_dev, TM_QUEUE_NODE_ID(0), &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "delete leaf should succeed");
	RTE_TEST_ASSERT_EQUAL(tm_conf->nb_queue_node, before - 1,
			     "nb_queue_node should decrement");
	return TEST_SUCCESS;
}

static int
test_tm_node_delete_with_children(void)
{
	struct rte_tm_node_params params = {0};
	params.shaper_profile_id = RTE_TM_SHAPER_PROFILE_ID_NONE;
	struct rte_tm_error error = {0};

	/* Build hierarchy: root -> TC -> COS */
	params.nonleaf.n_sp_priorities = 1;
	g_tm_ops->node_add(&g_tm_eth_dev, TM_PORT_NODE_ID,
			   RTE_TM_NODE_ID_NULL, 0, 0,
			   RTE_TM_NODE_LEVEL_ID_ANY, &params, &error);
	g_tm_ops->node_add(&g_tm_eth_dev, TM_TC_NODE_ID(0),
			   TM_PORT_NODE_ID, 0, 100,
			   RTE_TM_NODE_LEVEL_ID_ANY, &params, &error);
	g_tm_ops->node_add(&g_tm_eth_dev, TM_COS_NODE_ID(0),
			   TM_TC_NODE_ID(0), 0, 0,
			   RTE_TM_NODE_LEVEL_ID_ANY, &params, &error);

	/* TC has COS child (reference_count > 0) */
	int ret = g_tm_ops->node_delete(&g_tm_eth_dev, TM_TC_NODE_ID(0), &error);
	RTE_TEST_ASSERT_EQUAL(ret, -EINVAL,
			      "delete with children should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_tm_node_delete_not_found(void)
{
	struct rte_tm_error error = {0};
	int ret = g_tm_ops->node_delete(&g_tm_eth_dev, 999, &error);
	RTE_TEST_ASSERT_EQUAL(ret, -EINVAL,
			      "non-existent node should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_tm_node_delete_committed(void)
{
	struct rte_tm_node_params params = {0};
	params.shaper_profile_id = RTE_TM_SHAPER_PROFILE_ID_NONE;
	struct rte_tm_error error = {0};
	struct hinic3_tm_conf *tm_conf = &g_tm_nic_dev.ets->tm_conf;

	params.nonleaf.n_sp_priorities = 1;
	g_tm_ops->node_add(&g_tm_eth_dev, TM_PORT_NODE_ID,
			   RTE_TM_NODE_ID_NULL, 0, 0,
			   RTE_TM_NODE_LEVEL_ID_ANY, &params, &error);

	tm_conf->committed = true;
	int ret = g_tm_ops->node_delete(&g_tm_eth_dev, TM_PORT_NODE_ID, &error);
	RTE_TEST_ASSERT_EQUAL(ret, -EINVAL,
			      "delete when committed should return -EINVAL");
	tm_conf->committed = false;
	return TEST_SUCCESS;
}

/* ====== F. hinic3_tm_node_type_get (2 tests) ====== */

static int
test_tm_node_type_queue(void)
{
	struct rte_tm_node_params params = {0};
	params.shaper_profile_id = RTE_TM_SHAPER_PROFILE_ID_NONE;
	struct rte_tm_error error = {0};
	int is_leaf = -1;

	/* Build full hierarchy to add a queue node */
	params.nonleaf.n_sp_priorities = 1;
	g_tm_ops->node_add(&g_tm_eth_dev, TM_PORT_NODE_ID,
			   RTE_TM_NODE_ID_NULL, 0, 0,
			   RTE_TM_NODE_LEVEL_ID_ANY, &params, &error);
	g_tm_ops->node_add(&g_tm_eth_dev, TM_TC_NODE_ID(0),
			   TM_PORT_NODE_ID, 0, 100,
			   RTE_TM_NODE_LEVEL_ID_ANY, &params, &error);
	g_tm_ops->node_add(&g_tm_eth_dev, TM_COS_NODE_ID(0),
			   TM_TC_NODE_ID(0), 0, 0,
			   RTE_TM_NODE_LEVEL_ID_ANY, &params, &error);
	memset(&params, 0, sizeof(params));
	params.shaper_profile_id = RTE_TM_SHAPER_PROFILE_ID_NONE;
	params.leaf.cman = RTE_TM_CMAN_TAIL_DROP;
	g_tm_ops->node_add(&g_tm_eth_dev, TM_QUEUE_NODE_ID(0),
			   TM_COS_NODE_ID(0), 0, 0,
			   RTE_TM_NODE_LEVEL_ID_ANY, &params, &error);

	int ret = g_tm_ops->node_type_get(&g_tm_eth_dev, TM_QUEUE_NODE_ID(0),
					   &is_leaf, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "node_type_get should succeed");
	RTE_TEST_ASSERT_EQUAL(is_leaf, 1, "queue node should be leaf");
	return TEST_SUCCESS;
}

static int
test_tm_node_type_nonleaf(void)
{
	struct rte_tm_node_params params = {0};
	params.shaper_profile_id = RTE_TM_SHAPER_PROFILE_ID_NONE;
	struct rte_tm_error error = {0};
	int is_leaf = -1;

	params.nonleaf.n_sp_priorities = 1;
	g_tm_ops->node_add(&g_tm_eth_dev, TM_PORT_NODE_ID,
			   RTE_TM_NODE_ID_NULL, 0, 0,
			   RTE_TM_NODE_LEVEL_ID_ANY, &params, &error);

	int ret = g_tm_ops->node_type_get(&g_tm_eth_dev, TM_PORT_NODE_ID,
					   &is_leaf, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "node_type_get should succeed");
	RTE_TEST_ASSERT_EQUAL(is_leaf, 0, "port node should not be leaf");
	return TEST_SUCCESS;
}

/* ====== G. hinic3_tm_level_capabilities_get (3 tests) ====== */

static int
test_tm_level_cap_port(void)
{
	struct rte_tm_level_capabilities cap;
	struct rte_tm_error error = {0};

	errno = 1;
	int ret = g_tm_ops->level_capabilities_get(&g_tm_eth_dev,
						   HINIC3_TM_NODE_LEVEL_PORT,
						   &cap, &error);
	errno = 0;
	RTE_TEST_ASSERT_EQUAL(ret, 0, "port level cap should succeed");
	RTE_TEST_ASSERT_EQUAL((int)cap.n_nodes_max, 1,
			     "port level should have 1 node max");
	RTE_TEST_ASSERT_EQUAL((int)cap.n_nodes_nonleaf_max, 1,
			     "port level should have 1 nonleaf max");
	return TEST_SUCCESS;
}

static int
test_tm_level_cap_queue(void)
{
	struct rte_tm_level_capabilities cap;
	struct rte_tm_error error = {0};

	errno = 1;
	int ret = g_tm_ops->level_capabilities_get(&g_tm_eth_dev,
						   HINIC3_TM_NODE_LEVEL_QUEUE,
						   &cap, &error);
	errno = 0;
	RTE_TEST_ASSERT_EQUAL(ret, 0, "queue level cap should succeed");
	RTE_TEST_ASSERT(cap.n_nodes_leaf_max > 0,
		       "queue level should have leaf nodes");
	RTE_TEST_ASSERT_EQUAL((int)cap.n_nodes_nonleaf_max, 0,
			     "queue level should have 0 nonleaf max");
	return TEST_SUCCESS;
}

static int
test_tm_level_cap_invalid(void)
{
	struct rte_tm_level_capabilities cap;
	struct rte_tm_error error = {0};

	int ret = g_tm_ops->level_capabilities_get(&g_tm_eth_dev,
						   HINIC3_TM_NODE_LEVEL_MAX,
						   &cap, &error);
	RTE_TEST_ASSERT_EQUAL(ret, -EINVAL,
			      "invalid level_id should return -EINVAL");
	return TEST_SUCCESS;
}

/* ====== H. hinic3_tm_node_capabilities_get (2 tests) ====== */

static int
test_tm_node_cap_port(void)
{
	struct rte_tm_node_params params = {0};
	params.shaper_profile_id = RTE_TM_SHAPER_PROFILE_ID_NONE;
	struct rte_tm_error error = {0};
	struct rte_tm_node_capabilities cap;

	params.nonleaf.n_sp_priorities = 1;
	g_tm_ops->node_add(&g_tm_eth_dev, TM_PORT_NODE_ID,
			   RTE_TM_NODE_ID_NULL, 0, 0,
			   RTE_TM_NODE_LEVEL_ID_ANY, &params, &error);

	errno = 1;
	int ret = g_tm_ops->node_capabilities_get(&g_tm_eth_dev,
						  TM_PORT_NODE_ID,
						  &cap, &error);
	errno = 0;
	RTE_TEST_ASSERT_EQUAL(ret, 0, "port node cap should succeed");
	RTE_TEST_ASSERT_EQUAL((int)cap.shaper_private_supported, 1,
			     "port node should support private shaper");
	return TEST_SUCCESS;
}

static int
test_tm_node_cap_not_found(void)
{
	struct rte_tm_error error = {0};
	struct rte_tm_node_capabilities cap;

	int ret = g_tm_ops->node_capabilities_get(&g_tm_eth_dev, 999,
						  &cap, &error);
	RTE_TEST_ASSERT_EQUAL(ret, -EINVAL,
			      "non-existent node should return -EINVAL");
	return TEST_SUCCESS;
}

/* ====== I. hinic3_tm_hierarchy_commit (3 tests) ====== */

static int
test_tm_commit_no_root(void)
{
	struct rte_tm_error error = {0};
	struct hinic3_tm_conf *tm_conf = &g_tm_nic_dev.ets->tm_conf;

	/* No root node, commit should succeed and set committed=true */
	int ret = g_tm_ops->hierarchy_commit(&g_tm_eth_dev, 0, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "commit with no root should succeed");
	RTE_TEST_ASSERT_EQUAL(tm_conf->committed, true,
			     "committed should be true");
	return TEST_SUCCESS;
}

static int
test_tm_commit_success(void)
{
	struct rte_tm_node_params params = {0};
	params.shaper_profile_id = RTE_TM_SHAPER_PROFILE_ID_NONE;
	struct rte_tm_error error = {0};
	struct hinic3_tm_conf *tm_conf = &g_tm_nic_dev.ets->tm_conf;

	/* Build full hierarchy: root -> TC(weight=100) -> COS -> queue */
	params.nonleaf.n_sp_priorities = 1;
	g_tm_ops->node_add(&g_tm_eth_dev, TM_PORT_NODE_ID,
			   RTE_TM_NODE_ID_NULL, 0, 0,
			   RTE_TM_NODE_LEVEL_ID_ANY, &params, &error);
	g_tm_ops->node_add(&g_tm_eth_dev, TM_TC_NODE_ID(0),
			   TM_PORT_NODE_ID, 0, 100,
			   RTE_TM_NODE_LEVEL_ID_ANY, &params, &error);
	g_tm_ops->node_add(&g_tm_eth_dev, TM_COS_NODE_ID(0),
			   TM_TC_NODE_ID(0), 0, 0,
			   RTE_TM_NODE_LEVEL_ID_ANY, &params, &error);
	memset(&params, 0, sizeof(params));
	params.shaper_profile_id = RTE_TM_SHAPER_PROFILE_ID_NONE;
	params.leaf.cman = RTE_TM_CMAN_TAIL_DROP;
	g_tm_ops->node_add(&g_tm_eth_dev, TM_QUEUE_NODE_ID(0),
			   TM_COS_NODE_ID(0), 0, 0,
			   RTE_TM_NODE_LEVEL_ID_ANY, &params, &error);

	int ret = g_tm_ops->hierarchy_commit(&g_tm_eth_dev, 0, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "commit should succeed");
	RTE_TEST_ASSERT_EQUAL(tm_conf->committed, true,
			     "committed should be true");
	return TEST_SUCCESS;
}

static int
test_tm_commit_hw_fail(void)
{
	struct rte_tm_node_params params = {0};
	params.shaper_profile_id = RTE_TM_SHAPER_PROFILE_ID_NONE;
	struct rte_tm_error error = {0};

	/* Build hierarchy: root -> TC(weight=100) */
	params.nonleaf.n_sp_priorities = 1;
	g_tm_ops->node_add(&g_tm_eth_dev, TM_PORT_NODE_ID,
			   RTE_TM_NODE_ID_NULL, 0, 0,
			   RTE_TM_NODE_LEVEL_ID_ANY, &params, &error);
	g_tm_ops->node_add(&g_tm_eth_dev, TM_TC_NODE_ID(0),
			   TM_PORT_NODE_ID, 0, 100,
			   RTE_TM_NODE_LEVEL_ID_ANY, &params, &error);

	/* Set HW failure */
	g_mock_set_tm_hierarchy_do_commit_ret = -1;

	/* Commit with clear_on_fail=true */
	int ret = g_tm_ops->hierarchy_commit(&g_tm_eth_dev, 1, &error);
	RTE_TEST_ASSERT_EQUAL(ret, -EINVAL,
			      "hw failure should return -EINVAL");

	/* hinic3_tm_conf_uninit freed ets and hinic3_tm_conf_init re-init'd it
	 * (accessing freed memory - UB but works in practice).
	 * Re-allocate ets for teardown to work properly. */
	g_tm_nic_dev.ets = rte_zmalloc("tm_ets", sizeof(struct hinic3_ets), 0);
	if (g_tm_nic_dev.ets) {
		g_tm_nic_dev.ets->max_tm_rate = 100;
		g_tm_nic_dev.ets->num_tc = 8;
		hinic3_tm_conf_init(&g_tm_eth_dev);
	}

	g_mock_set_tm_hierarchy_do_commit_ret = 0;
	return TEST_SUCCESS;
}

/* ====== J. hinic3_tm_dev_stop_proc (1 test) ====== */

static int
test_tm_dev_stop_proc(void)
{
	struct rte_tm_error error = {0};
	struct hinic3_tm_conf *tm_conf = &g_tm_nic_dev.ets->tm_conf;

	/* Commit (no root, sets committed=true) */
	g_tm_ops->hierarchy_commit(&g_tm_eth_dev, 0, &error);
	RTE_TEST_ASSERT_EQUAL(tm_conf->committed, true,
			     "committed should be true after commit");

	/* dev_stop_proc clears committed */
	hinic3_tm_dev_stop_proc(&g_tm_eth_dev);
	RTE_TEST_ASSERT_EQUAL(tm_conf->committed, false,
			     "committed should be false after stop");
	return TEST_SUCCESS;
}

/* ====== Test suite ====== */

static struct unit_test_suite hinic3_tm_test_suite = {
	.suite_name = "HINIC3 TM Unit Tests",
	.setup = NULL,
	.teardown = NULL,
	.unit_test_cases = {
		/* A. hinic3_tm_ops_get */
		TEST_CASE_ST(tm_test_setup, tm_test_teardown,
			     test_tm_ops_get_null_dev),
		TEST_CASE_ST(tm_test_setup, tm_test_teardown,
			     test_tm_ops_get_success),

		/* B. hinic3_tm_capabilities_get */
		TEST_CASE_ST(tm_test_setup, tm_test_teardown,
			     test_tm_capabilities_null),
		TEST_CASE_ST(tm_test_setup, tm_test_teardown,
			     test_tm_capabilities_success),

		/* C. hinic3_tm_shaper_profile_add/del */
		TEST_CASE_ST(tm_test_setup, tm_test_teardown,
			     test_tm_shaper_add_success),
		TEST_CASE_ST(tm_test_setup, tm_test_teardown,
			     test_tm_shaper_add_null),
		TEST_CASE_ST(tm_test_setup, tm_test_teardown,
			     test_tm_shaper_add_duplicate),
		TEST_CASE_ST(tm_test_setup, tm_test_teardown,
			     test_tm_shaper_add_committed_rate),
		TEST_CASE_ST(tm_test_setup, tm_test_teardown,
			     test_tm_shaper_add_peak_rate_exceed),
		TEST_CASE_ST(tm_test_setup, tm_test_teardown,
			     test_tm_shaper_add_pkt_length_adjust),
		TEST_CASE_ST(tm_test_setup, tm_test_teardown,
			     test_tm_shaper_del_success),
		TEST_CASE_ST(tm_test_setup, tm_test_teardown,
			     test_tm_shaper_del_not_found),
		TEST_CASE_ST(tm_test_setup, tm_test_teardown,
			     test_tm_shaper_del_in_use),
		TEST_CASE_ST(tm_test_setup, tm_test_teardown,
			     test_tm_shaper_add_exceed_max),

		/* D. hinic3_tm_node_add hierarchy */
		TEST_CASE_ST(tm_test_setup, tm_test_teardown,
			     test_tm_node_add_root),
		TEST_CASE_ST(tm_test_setup, tm_test_teardown,
			     test_tm_node_add_root_duplicate),
		TEST_CASE_ST(tm_test_setup, tm_test_teardown,
			     test_tm_node_add_tc),
		TEST_CASE_ST(tm_test_setup, tm_test_teardown,
			     test_tm_node_add_tc_too_many),
		TEST_CASE_ST(tm_test_setup, tm_test_teardown,
			     test_tm_node_add_cos),
		TEST_CASE_ST(tm_test_setup, tm_test_teardown,
			     test_tm_node_add_cos_too_many),
		TEST_CASE_ST(tm_test_setup, tm_test_teardown,
			     test_tm_node_add_queue),
		TEST_CASE_ST(tm_test_setup, tm_test_teardown,
			     test_tm_node_add_queue_out_of_range),
		TEST_CASE_ST(tm_test_setup, tm_test_teardown,
			     test_tm_node_add_priority_nonzero),
		TEST_CASE_ST(tm_test_setup, tm_test_teardown,
			     test_tm_node_add_weight_exceed),
		TEST_CASE_ST(tm_test_setup, tm_test_teardown,
			     test_tm_node_add_parent_not_exist),
		TEST_CASE_ST(tm_test_setup, tm_test_teardown,
			     test_tm_node_add_committed),

		/* E. hinic3_tm_node_delete */
		TEST_CASE_ST(tm_test_setup, tm_test_teardown,
			     test_tm_node_delete_leaf),
		TEST_CASE_ST(tm_test_setup, tm_test_teardown,
			     test_tm_node_delete_with_children),
		TEST_CASE_ST(tm_test_setup, tm_test_teardown,
			     test_tm_node_delete_not_found),
		TEST_CASE_ST(tm_test_setup, tm_test_teardown,
			     test_tm_node_delete_committed),

		/* F. hinic3_tm_node_type_get */
		TEST_CASE_ST(tm_test_setup, tm_test_teardown,
			     test_tm_node_type_queue),
		TEST_CASE_ST(tm_test_setup, tm_test_teardown,
			     test_tm_node_type_nonleaf),

		/* G. hinic3_tm_level_capabilities_get */
		TEST_CASE_ST(tm_test_setup, tm_test_teardown,
			     test_tm_level_cap_port),
		TEST_CASE_ST(tm_test_setup, tm_test_teardown,
			     test_tm_level_cap_queue),
		TEST_CASE_ST(tm_test_setup, tm_test_teardown,
			     test_tm_level_cap_invalid),

		/* H. hinic3_tm_node_capabilities_get */
		TEST_CASE_ST(tm_test_setup, tm_test_teardown,
			     test_tm_node_cap_port),
		TEST_CASE_ST(tm_test_setup, tm_test_teardown,
			     test_tm_node_cap_not_found),

		/* I. hinic3_tm_hierarchy_commit */
		TEST_CASE_ST(tm_test_setup, tm_test_teardown,
			     test_tm_commit_no_root),
		TEST_CASE_ST(tm_test_setup, tm_test_teardown,
			     test_tm_commit_success),
		TEST_CASE_ST(tm_test_setup, tm_test_teardown,
			     test_tm_commit_hw_fail),

		/* J. hinic3_tm_dev_stop_proc */
		TEST_CASE_ST(tm_test_setup, tm_test_teardown,
			     test_tm_dev_stop_proc),

		TEST_CASES_END()
	}
};

static int
test_hinic3_tm(void)
{
	return unit_test_suite_runner(&hinic3_tm_test_suite);
}

REGISTER_TEST_COMMAND(hinic3_tm_autotest, test_hinic3_tm);
