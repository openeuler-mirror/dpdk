/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2025-2026 Huawei Technologies Co., Ltd
 * Unit tests for hinic3_pmd_nic_cfg.c
 *
 * Tests the NIC configuration functions (MAC, VLAN, MTU, port, RSS, etc.)
 * by mocking mgmt_sync and hardware accessor functions.
 */

#include <rte_common.h>
#include <rte_ethdev.h>
#include <rte_pci.h>
#include <rte_malloc.h>
#include <rte_ether.h>

#include "../test.h"
#include <rte_test.h>

#include "base/hinic3_compat.h"
#include "base/hinic3_pmd_hwif.h"
#include "base/hinic3_pmd_nic_cfg.h"
#include "base/hinic3_pmd_hwdev.h"
#include "base/hinic3_pmd_hw_cfg.h"
#include "hinic3_pmd_dcb.h"
#include "hinic3_pmd_ethdev.h"
#include "test_hinic3_mock.h"

/* ====== Mock hardware structures ====== */

static struct hinic3_hwdev g_nic_cfg_hwdev;
static struct hinic3_hwif g_nic_cfg_hwif;
static struct hinic3_nic_dev g_nic_cfg_nic_dev;
static struct rte_pci_device g_nic_cfg_pci_dev;
static struct hinic3_dcb g_nic_cfg_dcb;

static void
nic_cfg_mock_setup(void)
{
	memset(&g_nic_cfg_hwdev, 0, sizeof(g_nic_cfg_hwdev));
	memset(&g_nic_cfg_hwif, 0, sizeof(g_nic_cfg_hwif));
	memset(&g_nic_cfg_nic_dev, 0, sizeof(g_nic_cfg_nic_dev));
	memset(&g_nic_cfg_pci_dev, 0, sizeof(g_nic_cfg_pci_dev));
	memset(&g_nic_cfg_dcb, 0, sizeof(g_nic_cfg_dcb));

	g_nic_cfg_hwdev.dev_handle = &g_nic_cfg_nic_dev;
	g_nic_cfg_hwdev.hwif = &g_nic_cfg_hwif;
	g_nic_cfg_hwdev.pci_dev = &g_nic_cfg_pci_dev;
	g_nic_cfg_hwdev.vf_valid_status = false;

	g_nic_cfg_nic_dev.hwdev = &g_nic_cfg_hwdev;
	g_nic_cfg_nic_dev.dcb = &g_nic_cfg_dcb;
	g_nic_cfg_nic_dev.max_sqs = 4;
	g_nic_cfg_nic_dev.max_rqs = 4;
	g_nic_cfg_nic_dev.num_sqs = 2;
	g_nic_cfg_nic_dev.num_rqs = 2;
	g_nic_cfg_pci_dev.id.device_id = HINIC3_DEV_ID_SP620;

	/* Reset all mock control variables */
	g_mock_func_type = TYPE_PF;
	g_mock_physical_port_id = 0;
	g_mock_global_func_id = 0;
	g_mock_mgmt_sync_ret = 0;
	g_mock_msg_to_mgmt_sync_ret = 0;
	g_mock_mbox_to_pf_ret = 0;
	g_mock_bifur_en = 0;
	g_mock_iso_en = 0;
	g_mock_bifur_type = 0;
	g_mock_is_shared_dev = false;
	g_mock_bifur_get_default_mac_ret = 0;
	g_mock_set_link_status_follow_ret = 0;
	g_mock_is_sp620_nic = false;
	g_qinfo_type = HINIC3_QINFO_TYPE_NORMAL;
	g_nic_cfg_hwdev.qinfo_type = HINIC3_QINFO_TYPE_NORMAL;
}

/* ====== hinic3_set_ci_table tests ====== */

static int
test_set_ci_table_null_hwdev(void)
{
	struct hinic3_sq_attr attr = {0};
	RTE_TEST_ASSERT(hinic3_set_ci_table(NULL, &attr) == -EINVAL,
			"NULL hwdev should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_set_ci_table_null_attr(void)
{
	RTE_TEST_ASSERT(hinic3_set_ci_table(&g_nic_cfg_hwdev, NULL) == -EINVAL,
			"NULL attr should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_set_ci_table_success(void)
{
	struct hinic3_sq_attr attr = {
		.dma_attr_off = 0,
		.pending_limit = 3,
		.coalescing_time = 10,
		.intr_en = 0,
		.l2nic_sqn = 0,
		.ci_dma_base = 0x1000,
	};

	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_set_ci_table(&g_nic_cfg_hwdev, &attr) == 0,
			"Valid params should succeed");
	return TEST_SUCCESS;
}

static int
test_set_ci_table_with_intr(void)
{
	struct hinic3_sq_attr attr = {
		.dma_attr_off = 1,
		.pending_limit = 5,
		.coalescing_time = 20,
		.intr_en = 1,
		.intr_idx = 2,
		.l2nic_sqn = 1,
		.ci_dma_base = 0x2000,
	};

	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_set_ci_table(&g_nic_cfg_hwdev, &attr) == 0,
			"Valid params with intr should succeed");
	return TEST_SUCCESS;
}

static int
test_set_ci_table_mgmt_fail(void)
{
	struct hinic3_sq_attr attr = {0};

	nic_cfg_mock_setup();
	g_mock_mgmt_sync_ret = -EFAULT;
	g_mock_msg_to_mgmt_sync_ret = -EFAULT;
	RTE_TEST_ASSERT(hinic3_set_ci_table(&g_nic_cfg_hwdev, &attr) == -EFAULT,
			"mgmt sync failure should return -EFAULT");
	return TEST_SUCCESS;
}

/* ====== hinic3_set_mac tests ====== */

static int
test_set_mac_null_hwdev(void)
{
	u8 mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
	RTE_TEST_ASSERT(hinic3_set_mac(NULL, mac, 0, 0) == -EINVAL,
			"NULL hwdev should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_set_mac_null_mac(void)
{
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_set_mac(&g_nic_cfg_hwdev, NULL, 0, 0) == -EINVAL,
			"NULL mac should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_set_mac_qpool_mode(void)
{
	u8 mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
	nic_cfg_mock_setup();
	g_qinfo_type = HINIC3_QINFO_TYPE_QPOOL;
	g_nic_cfg_hwdev.qinfo_type = HINIC3_QINFO_TYPE_QPOOL;
	RTE_TEST_ASSERT(hinic3_set_mac(&g_nic_cfg_hwdev, mac, 0, 0) == 0,
			"QPOOL mode should return 0");
	return TEST_SUCCESS;
}

static int
test_set_mac_bifur_shared(void)
{
	u8 mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
	nic_cfg_mock_setup();
	g_qinfo_type = HINIC3_QINFO_TYPE_BIFUR;
	g_nic_cfg_hwdev.qinfo_type = HINIC3_QINFO_TYPE_BIFUR;
	g_mock_is_shared_dev = true;
	RTE_TEST_ASSERT(hinic3_set_mac(&g_nic_cfg_hwdev, mac, 0, 0) == 0,
			"BIFUR shared mode should return 0");
	return TEST_SUCCESS;
}

static int
test_set_mac_invalid_vlan(void)
{
	u8 mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_set_mac(&g_nic_cfg_hwdev, mac, 4096, 0) == -EINVAL,
			"Invalid VLAN should return -EINVAL");
	RTE_TEST_ASSERT(hinic3_set_mac(&g_nic_cfg_hwdev, mac, 4096, 0) == -EINVAL,
			"VLAN >= 4096 should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_set_mac_success(void)
{
	u8 mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_set_mac(&g_nic_cfg_hwdev, mac, 0, 0) == 0,
			"Valid set mac should succeed");
	return TEST_SUCCESS;
}

static int
test_set_mac_mgmt_fail(void)
{
	u8 mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
	nic_cfg_mock_setup();
	g_mock_mgmt_sync_ret = -EIO;
	g_mock_msg_to_mgmt_sync_ret = -EIO;
	RTE_TEST_ASSERT(hinic3_set_mac(&g_nic_cfg_hwdev, mac, 0, 0) == -EIO,
			"mgmt sync failure should return -EIO");
	return TEST_SUCCESS;
}

/* ====== hinic3_del_mac tests ====== */

static int
test_del_mac_null_hwdev(void)
{
	u8 mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
	RTE_TEST_ASSERT(hinic3_del_mac(NULL, mac, 0, 0) == -EINVAL,
			"NULL hwdev should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_del_mac_null_mac(void)
{
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_del_mac(&g_nic_cfg_hwdev, NULL, 0, 0) == -EINVAL,
			"NULL mac should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_del_mac_invalid_vlan(void)
{
	u8 mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_del_mac(&g_nic_cfg_hwdev, mac, 4096, 0) == -EINVAL,
			"Invalid VLAN should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_del_mac_success(void)
{
	u8 mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_del_mac(&g_nic_cfg_hwdev, mac, 0, 0) == 0,
			"Valid del mac should succeed");
	return TEST_SUCCESS;
}

static int
test_del_mac_qpool_mode(void)
{
	u8 mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
	nic_cfg_mock_setup();
	g_qinfo_type = HINIC3_QINFO_TYPE_QPOOL;
	g_nic_cfg_hwdev.qinfo_type = HINIC3_QINFO_TYPE_QPOOL;
	RTE_TEST_ASSERT(hinic3_del_mac(&g_nic_cfg_hwdev, mac, 0, 0) == 0,
			"QPOOL mode should return 0");
	return TEST_SUCCESS;
}

/* ====== hinic3_update_mac tests ====== */

static int
test_update_mac_null_params(void)
{
	u8 mac1[6] = {0}, mac2[6] = {0};
	RTE_TEST_ASSERT(hinic3_update_mac(NULL, mac1, mac2, 0, 0) == -EINVAL,
			"NULL hwdev should return -EINVAL");
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_update_mac(&g_nic_cfg_hwdev, NULL, mac2, 0, 0) == -EINVAL,
			"NULL old_mac should return -EINVAL");
	RTE_TEST_ASSERT(hinic3_update_mac(&g_nic_cfg_hwdev, mac1, NULL, 0, 0) == -EINVAL,
			"NULL new_mac should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_update_mac_invalid_vlan(void)
{
	u8 mac1[6] = {0}, mac2[6] = {0};
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_update_mac(&g_nic_cfg_hwdev, mac1, mac2, 4096, 0) == -EINVAL,
			"Invalid VLAN should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_update_mac_success(void)
{
	u8 old_mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
	u8 new_mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x66};
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_update_mac(&g_nic_cfg_hwdev, old_mac, new_mac, 0, 0) == 0,
			"Valid update mac should succeed");
	return TEST_SUCCESS;
}

/* ====== hinic3_get_default_mac tests ====== */

static int
test_get_default_mac_null(void)
{
	u8 mac[6] = {0};
	RTE_TEST_ASSERT(hinic3_get_default_mac(NULL, mac, 6) == -EINVAL,
			"NULL hwdev should return -EINVAL");
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_get_default_mac(&g_nic_cfg_hwdev, NULL, 6) == -EINVAL,
			"NULL mac should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_get_default_mac_success(void)
{
	u8 mac[6] = {0};
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_get_default_mac(&g_nic_cfg_hwdev, mac, 6) == 0,
			"Valid get default mac should succeed");
	return TEST_SUCCESS;
}

static int
test_get_default_mac_bifur_shared(void)
{
	u8 mac[6] = {0};
	nic_cfg_mock_setup();
	g_qinfo_type = HINIC3_QINFO_TYPE_BIFUR;
	g_nic_cfg_hwdev.qinfo_type = HINIC3_QINFO_TYPE_BIFUR;
	g_mock_is_shared_dev = true;
	g_mock_bifur_get_default_mac_ret = 0;
	RTE_TEST_ASSERT(hinic3_get_default_mac(&g_nic_cfg_hwdev, mac, 6) == 0,
			"BIFUR shared mode should call bifur_get_default_mac");
	RTE_TEST_ASSERT(mac[0] == 0x00 && mac[5] == 0x55,
			"BIFUR default mac should match mock value");
	return TEST_SUCCESS;
}

static int
test_get_default_mac_mgmt_fail(void)
{
	u8 mac[6] = {0};
	nic_cfg_mock_setup();
	g_mock_mgmt_sync_ret = -EIO;
	g_mock_msg_to_mgmt_sync_ret = -EIO;
	RTE_TEST_ASSERT(hinic3_get_default_mac(&g_nic_cfg_hwdev, mac, 6) == -EINVAL,
			"mgmt failure should return -EINVAL");
	return TEST_SUCCESS;
}

/* ====== hinic3_get_port_info tests ====== */

static int
test_get_port_info_null(void)
{
	struct nic_port_info info = {0};
	RTE_TEST_ASSERT(hinic3_get_port_info(NULL, &info) == -EINVAL,
			"NULL hwdev should return -EINVAL");
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_get_port_info(&g_nic_cfg_hwdev, NULL) == -EINVAL,
			"NULL port_info should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_get_port_info_success(void)
{
	struct nic_port_info info = {0};
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_get_port_info(&g_nic_cfg_hwdev, &info) == 0,
			"Valid get port info should succeed");
	return TEST_SUCCESS;
}

static int
test_get_port_info_mgmt_fail(void)
{
	struct nic_port_info info = {0};
	nic_cfg_mock_setup();
	g_mock_msg_to_mgmt_sync_ret = -EIO;
	RTE_TEST_ASSERT(hinic3_get_port_info(&g_nic_cfg_hwdev, &info) == -EINVAL,
			"mgmt failure should return -EINVAL");
	return TEST_SUCCESS;
}

/* ====== hinic3_get_link_state tests ====== */

static int
test_get_link_state_null(void)
{
	u8 link = 0;
	RTE_TEST_ASSERT(hinic3_get_link_state(NULL, &link) == -EINVAL,
			"NULL hwdev should return -EINVAL");
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_get_link_state(&g_nic_cfg_hwdev, NULL) == -EINVAL,
			"NULL link_state should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_get_link_state_success(void)
{
	u8 link = 0;
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_get_link_state(&g_nic_cfg_hwdev, &link) == 0,
			"Valid get link state should succeed");
	return TEST_SUCCESS;
}

static int
test_get_link_state_mgmt_fail(void)
{
	u8 link = 0;
	nic_cfg_mock_setup();
	g_mock_msg_to_mgmt_sync_ret = -EFAULT;
	RTE_TEST_ASSERT(hinic3_get_link_state(&g_nic_cfg_hwdev, &link) == -EIO,
			"mgmt failure should return -EIO");
	return TEST_SUCCESS;
}

/* ====== hinic3_set_vport_enable tests ====== */

static int
test_set_vport_enable_null(void)
{
	RTE_TEST_ASSERT(hinic3_set_vport_enable(NULL, true) == -EINVAL,
			"NULL hwdev should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_set_vport_enable_qpool(void)
{
	nic_cfg_mock_setup();
	g_qinfo_type = HINIC3_QINFO_TYPE_QPOOL;
	g_nic_cfg_hwdev.qinfo_type = HINIC3_QINFO_TYPE_QPOOL;
	RTE_TEST_ASSERT(hinic3_set_vport_enable(&g_nic_cfg_hwdev, true) == 0,
			"QPOOL mode should return 0");
	return TEST_SUCCESS;
}

static int
test_set_vport_enable_success(void)
{
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_set_vport_enable(&g_nic_cfg_hwdev, true) == 0,
			"Enable vport should succeed");
	RTE_TEST_ASSERT(g_nic_cfg_hwdev.vf_valid_status == true,
			"vf_valid_status should be set to true");

	RTE_TEST_ASSERT(hinic3_set_vport_enable(&g_nic_cfg_hwdev, false) == 0,
			"Disable vport should succeed");
	RTE_TEST_ASSERT(g_nic_cfg_hwdev.vf_valid_status == false,
			"vf_valid_status should be set to false");
	return TEST_SUCCESS;
}

static int
test_set_vport_enable_mgmt_fail(void)
{
	nic_cfg_mock_setup();
	g_mock_mgmt_sync_ret = -EIO;
	g_mock_msg_to_mgmt_sync_ret = -EIO;
	RTE_TEST_ASSERT(hinic3_set_vport_enable(&g_nic_cfg_hwdev, true) == -EIO,
			"mgmt failure should return -EIO");
	return TEST_SUCCESS;
}

/* ====== hinic3_set_port_enable tests ====== */

static int
test_set_port_enable_null(void)
{
	RTE_TEST_ASSERT(hinic3_set_port_enable(NULL, true) == -EINVAL,
			"NULL hwdev should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_set_port_enable_vf(void)
{
	nic_cfg_mock_setup();
	g_mock_func_type = TYPE_VF;
	g_nic_cfg_hwif.attr.func_type = TYPE_VF;
	RTE_TEST_ASSERT(hinic3_set_port_enable(&g_nic_cfg_hwdev, true) == 0,
			"VF should return 0 (no-op)");
	return TEST_SUCCESS;
}

static int
test_set_port_enable_success(void)
{
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_set_port_enable(&g_nic_cfg_hwdev, true) == 0,
			"Enable port should succeed");
	return TEST_SUCCESS;
}

static int
test_set_port_enable_mgmt_fail(void)
{
	nic_cfg_mock_setup();
	g_mock_msg_to_mgmt_sync_ret = -EIO;
	RTE_TEST_ASSERT(hinic3_set_port_enable(&g_nic_cfg_hwdev, true) == -EIO,
			"mgmt failure should return -EIO");
	return TEST_SUCCESS;
}

/* ====== hinic3_set_port_mtu tests ====== */

static int
test_set_port_mtu_null(void)
{
	RTE_TEST_ASSERT(hinic3_set_port_mtu(NULL, 1500) == -EINVAL,
			"NULL hwdev should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_set_port_mtu_too_small(void)
{
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_set_port_mtu(&g_nic_cfg_hwdev, HINIC3_MIN_MTU_SIZE - 1) == -EINVAL,
			"MTU too small should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_set_port_mtu_too_large(void)
{
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_set_port_mtu(&g_nic_cfg_hwdev, HINIC3_MAX_JUMBO_FRAME_SIZE + 1) == -EINVAL,
			"MTU too large should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_set_port_mtu_success(void)
{
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_set_port_mtu(&g_nic_cfg_hwdev, 1500) == 0,
			"Valid MTU should succeed");
	return TEST_SUCCESS;
}

static int
test_set_port_mtu_boundary(void)
{
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_set_port_mtu(&g_nic_cfg_hwdev, HINIC3_MIN_MTU_SIZE) == 0,
			"Min MTU boundary should succeed");
	RTE_TEST_ASSERT(hinic3_set_port_mtu(&g_nic_cfg_hwdev, HINIC3_MAX_JUMBO_FRAME_SIZE) == 0,
			"Max MTU boundary should succeed");
	return TEST_SUCCESS;
}

/* ====== hinic3_set_rx_vlan_offload tests ====== */

static int
test_set_rx_vlan_offload_null(void)
{
	RTE_TEST_ASSERT(hinic3_set_rx_vlan_offload(NULL, 1) == -EINVAL,
			"NULL hwdev should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_set_rx_vlan_offload_qpool(void)
{
	nic_cfg_mock_setup();
	g_qinfo_type = HINIC3_QINFO_TYPE_QPOOL;
	g_nic_cfg_hwdev.qinfo_type = HINIC3_QINFO_TYPE_QPOOL;
	RTE_TEST_ASSERT(hinic3_set_rx_vlan_offload(&g_nic_cfg_hwdev, 1) == 0,
			"QPOOL mode should return 0");
	return TEST_SUCCESS;
}

static int
test_set_rx_vlan_offload_success(void)
{
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_set_rx_vlan_offload(&g_nic_cfg_hwdev, 1) == 0,
			"Enable vlan offload should succeed");
	RTE_TEST_ASSERT(hinic3_set_rx_vlan_offload(&g_nic_cfg_hwdev, 0) == 0,
			"Disable vlan offload should succeed");
	return TEST_SUCCESS;
}

/* ====== hinic3_set_vlan_fliter tests ====== */

static int
test_set_vlan_fliter_null(void)
{
	RTE_TEST_ASSERT(hinic3_set_vlan_fliter(NULL, 1) == -EINVAL,
			"NULL hwdev should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_set_vlan_fliter_qpool(void)
{
	nic_cfg_mock_setup();
	g_qinfo_type = HINIC3_QINFO_TYPE_QPOOL;
	g_nic_cfg_hwdev.qinfo_type = HINIC3_QINFO_TYPE_QPOOL;
	RTE_TEST_ASSERT(hinic3_set_vlan_fliter(&g_nic_cfg_hwdev, 1) == 0,
			"QPOOL mode should return 0");
	return TEST_SUCCESS;
}

static int
test_set_vlan_fliter_success(void)
{
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_set_vlan_fliter(&g_nic_cfg_hwdev, 1) == 0,
			"Set vlan filter should succeed");
	return TEST_SUCCESS;
}

/* ====== hinic3_set_rx_lro_state tests ====== */

static int
test_set_rx_lro_state_null(void)
{
	RTE_TEST_ASSERT(hinic3_set_rx_lro_state(NULL, true, 100, 32) == -EINVAL,
			"NULL hwdev should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_set_rx_lro_state_success(void)
{
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_set_rx_lro_state(&g_nic_cfg_hwdev, true, 100, 32) == 0,
			"Enable LRO should succeed");
	return TEST_SUCCESS;
}

static int
test_set_rx_lro_state_disabled(void)
{
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_set_rx_lro_state(&g_nic_cfg_hwdev, false, 100, 32) == 0,
			"Disable LRO should succeed (no timer set for disabled)");
	return TEST_SUCCESS;
}

static int
test_set_rx_lro_state_vf(void)
{
	nic_cfg_mock_setup();
	g_mock_func_type = TYPE_VF;
	g_nic_cfg_hwif.attr.func_type = TYPE_VF;
	RTE_TEST_ASSERT(hinic3_set_rx_lro_state(&g_nic_cfg_hwdev, true, 100, 32) == 0,
			"VF enable LRO should succeed (no timer set for VF)");
	return TEST_SUCCESS;
}

/* ====== hinic3_set_rx_mode tests ====== */

static int
test_set_rx_mode_null(void)
{
	RTE_TEST_ASSERT(hinic3_set_rx_mode(NULL, 0) == -EINVAL,
			"NULL hwdev should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_set_rx_mode_qpool(void)
{
	nic_cfg_mock_setup();
	g_qinfo_type = HINIC3_QINFO_TYPE_QPOOL;
	g_nic_cfg_hwdev.qinfo_type = HINIC3_QINFO_TYPE_QPOOL;
	RTE_TEST_ASSERT(hinic3_set_rx_mode(&g_nic_cfg_hwdev, 0) == 0,
			"QPOOL mode should return 0");
	return TEST_SUCCESS;
}

static int
test_set_rx_mode_success(void)
{
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_set_rx_mode(&g_nic_cfg_hwdev, 0x07) == 0,
			"Set rx mode should succeed");
	return TEST_SUCCESS;
}

/* ====== hinic3_flush_qps_res tests ====== */

static int
test_flush_qps_res_null(void)
{
	RTE_TEST_ASSERT(hinic3_flush_qps_res(NULL) == -EINVAL,
			"NULL hwdev should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_flush_qps_res_success(void)
{
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_flush_qps_res(&g_nic_cfg_hwdev) == 0,
			"Flush qps res should succeed");
	return TEST_SUCCESS;
}

static int
test_flush_qps_res_mgmt_fail(void)
{
	nic_cfg_mock_setup();
	g_mock_mgmt_sync_ret = -EIO;
	g_mock_msg_to_mgmt_sync_ret = -EIO;
	RTE_TEST_ASSERT(hinic3_flush_qps_res(&g_nic_cfg_hwdev) == -EIO,
			"mgmt failure should return -EIO");
	return TEST_SUCCESS;
}

/* ====== hinic3_set_pause_info / hinic3_get_pause_info tests ====== */

static int
test_set_pause_info_null(void)
{
	struct nic_pause_config pause = {0};
	RTE_TEST_ASSERT(hinic3_set_pause_info(NULL, pause) == -EINVAL,
			"NULL hwdev should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_set_pause_info_success(void)
{
	struct nic_pause_config pause = {.rx_pause = 1, .tx_pause = 1, .auto_neg = 0};
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_set_pause_info(&g_nic_cfg_hwdev, pause) == 0,
			"Set pause info should succeed");
	return TEST_SUCCESS;
}

static int
test_get_pause_info_null(void)
{
	struct nic_pause_config pause = {0};
	RTE_TEST_ASSERT(hinic3_get_pause_info(NULL, &pause) == -EINVAL,
			"NULL hwdev should return -EINVAL");
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_get_pause_info(&g_nic_cfg_hwdev, NULL) == -EINVAL,
			"NULL nic_pause should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_get_pause_info_success(void)
{
	struct nic_pause_config pause = {0};
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_get_pause_info(&g_nic_cfg_hwdev, &pause) == 0,
			"Get pause info should succeed");
	return TEST_SUCCESS;
}

/* ====== hinic3_get_vport_stats / hinic3_clear_vport_stats tests ====== */

static int
test_get_vport_stats_null(void)
{
	struct hinic3_vport_stats stats = {0};
	RTE_TEST_ASSERT(hinic3_get_vport_stats(NULL, &stats) == -EINVAL,
			"NULL hwdev should return -EINVAL");
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_get_vport_stats(&g_nic_cfg_hwdev, NULL) == -EINVAL,
			"NULL stats should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_get_vport_stats_success(void)
{
	struct hinic3_vport_stats stats = {0};
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_get_vport_stats(&g_nic_cfg_hwdev, &stats) == 0,
			"Get vport stats should succeed");
	return TEST_SUCCESS;
}

static int
test_clear_vport_stats_null(void)
{
	RTE_TEST_ASSERT(hinic3_clear_vport_stats(NULL) == -EINVAL,
			"NULL hwdev should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_clear_vport_stats_success(void)
{
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_clear_vport_stats(&g_nic_cfg_hwdev) == 0,
			"Clear vport stats should succeed");
	return TEST_SUCCESS;
}

/* ====== hinic3_init_function_table tests ====== */

static int
test_init_function_table_success(void)
{
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_init_function_table(&g_nic_cfg_hwdev, 1024) == 0,
			"Init function table should succeed");
	return TEST_SUCCESS;
}

static int
test_init_function_table_mgmt_fail(void)
{
	nic_cfg_mock_setup();
	g_mock_mgmt_sync_ret = -EFAULT;
	g_mock_msg_to_mgmt_sync_ret = -EFAULT;
	RTE_TEST_ASSERT(hinic3_init_function_table(&g_nic_cfg_hwdev, 1024) == -EFAULT,
			"mgmt failure should return -EFAULT");
	return TEST_SUCCESS;
}

/* ====== hinic3_init_nic_hwdev / hinic3_free_nic_hwdev tests ====== */

static int
test_init_nic_hwdev_pf(void)
{
	nic_cfg_mock_setup();
	g_mock_func_type = TYPE_PF;
	RTE_TEST_ASSERT(hinic3_init_nic_hwdev(&g_nic_cfg_hwdev) == 0,
			"PF init should succeed (no VF register)");
	return TEST_SUCCESS;
}

static int
test_init_nic_hwdev_vf(void)
{
	nic_cfg_mock_setup();
	g_mock_func_type = TYPE_VF;
	g_nic_cfg_hwif.attr.func_type = TYPE_VF;
	RTE_TEST_ASSERT(hinic3_init_nic_hwdev(&g_nic_cfg_hwdev) == 0,
			"VF init should succeed");
	return TEST_SUCCESS;
}

static int
test_init_nic_hwdev_vf_fail(void)
{
	nic_cfg_mock_setup();
	g_mock_func_type = TYPE_VF;
	g_nic_cfg_hwif.attr.func_type = TYPE_VF;
	g_mock_mgmt_sync_ret = -EFAULT;
	g_mock_msg_to_mgmt_sync_ret = -EFAULT;
	/* l2nic_msg_to_mgmt_sync 与 hinic3_vf_func_init 同编译单元，--wrap 无效，
	 * 真实函数对 VF 命令走 hinic3_mbox_to_pf 路径，需 mock 其返回值 */
	g_mock_mbox_to_pf_ret = -EFAULT;
	RTE_TEST_ASSERT(hinic3_init_nic_hwdev(&g_nic_cfg_hwdev) == -EFAULT,
			"VF init with mgmt failure should return -EFAULT");
	return TEST_SUCCESS;
}

static int
test_free_nic_hwdev_null(void)
{
	hinic3_free_nic_hwdev(NULL);
	/* Should not crash */
	return TEST_SUCCESS;
}

static int
test_free_nic_hwdev_pf(void)
{
	nic_cfg_mock_setup();
	g_mock_func_type = TYPE_PF;
	g_mock_set_link_status_follow_ret = 0;
	hinic3_free_nic_hwdev(&g_nic_cfg_hwdev);
	/* PF should call set_link_status_follow */
	return TEST_SUCCESS;
}

static int
test_free_nic_hwdev_vf(void)
{
	nic_cfg_mock_setup();
	g_mock_func_type = TYPE_VF;
	g_nic_cfg_hwif.attr.func_type = TYPE_VF;
	hinic3_free_nic_hwdev(&g_nic_cfg_hwdev);
	/* VF should call vf_func_free */
	return TEST_SUCCESS;
}

/* ====== hinic3_set_fec_mode / hinic3_get_fec_mode tests ====== */

static int
test_set_fec_mode_null(void)
{
	RTE_TEST_ASSERT(hinic3_set_fec_mode(NULL, HINIC3_FEC_MODE_AUTO) == -EINVAL,
			"NULL hwdev should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_set_fec_mode_success(void)
{
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_set_fec_mode(&g_nic_cfg_hwdev, HINIC3_FEC_MODE_AUTO) == 0,
			"Set FEC mode should succeed");
	return TEST_SUCCESS;
}

static int
test_set_fec_mode_mgmt_fail(void)
{
	nic_cfg_mock_setup();
	g_mock_msg_to_mgmt_sync_ret = -EFAULT;
	RTE_TEST_ASSERT(hinic3_set_fec_mode(&g_nic_cfg_hwdev, HINIC3_FEC_MODE_AUTO) == -EINVAL,
			"mgmt failure should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_get_fec_mode_null(void)
{
	u8 adv = 0, sup = 0;
	RTE_TEST_ASSERT(hinic3_get_fec_mode(NULL, &adv, &sup) == -EINVAL,
			"NULL hwdev should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_get_fec_mode_success(void)
{
	u8 adv = 0, sup = 0;
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_get_fec_mode(&g_nic_cfg_hwdev, &adv, &sup) == 0,
			"Get FEC mode should succeed");
	return TEST_SUCCESS;
}

static int
test_get_fec_mode_null_pointers(void)
{
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_get_fec_mode(&g_nic_cfg_hwdev, NULL, NULL) == 0,
			"Get FEC mode with NULL output pointers should succeed");
	return TEST_SUCCESS;
}

/* ====== hinic3_sync_dcb_state tests ====== */

static int
test_sync_dcb_state_null(void)
{
	RTE_TEST_ASSERT(hinic3_sync_dcb_state(NULL, 0, 0) == -EINVAL,
			"NULL hwdev should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_sync_dcb_state_vf_not_sp620(void)
{
	nic_cfg_mock_setup();
	g_mock_func_type = TYPE_VF;
	g_nic_cfg_hwif.attr.func_type = TYPE_VF;
	g_mock_is_sp620_nic = false;
	RTE_TEST_ASSERT(hinic3_sync_dcb_state(&g_nic_cfg_hwdev, 0, 0) == -EPERM,
			"VF (not SP620) should return -EPERM");
	return TEST_SUCCESS;
}

static int
test_sync_dcb_state_success(void)
{
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_sync_dcb_state(&g_nic_cfg_hwdev, 1, 1) == 0,
			"PF sync dcb state should succeed");
	return TEST_SUCCESS;
}

/* ====== hinic3_set_qos_port_trust tests ====== */

static int
test_set_qos_port_trust_null(void)
{
	RTE_TEST_ASSERT(hinic3_set_qos_port_trust(NULL, 0) == -EINVAL,
			"NULL hwdev should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_set_qos_port_trust_success(void)
{
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_set_qos_port_trust(&g_nic_cfg_hwdev, 0) == 0,
			"Set QoS port trust should succeed");
	return TEST_SUCCESS;
}

/* ====== hinic3_qinfo_type_init tests ====== */

static int
test_qinfo_type_init_normal(void)
{
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_qinfo_type_init("/nonexistent/path") == 0,
			"Non-existent path should result in normal mode");
	RTE_TEST_ASSERT(g_qinfo_type == HINIC3_QINFO_TYPE_NORMAL,
			"Should be normal mode");
	return TEST_SUCCESS;
}

/* ====== hinic3_rss_cfg tests ====== */

static int
test_rss_cfg_null(void)
{
	u8 prio_tc[8] = {0};
	RTE_TEST_ASSERT(hinic3_rss_cfg(NULL, 1, 2, prio_tc) == -EINVAL,
			"NULL hwdev should return -EINVAL");
	RTE_TEST_ASSERT(hinic3_rss_cfg(&g_nic_cfg_hwdev, 1, 2, NULL) == -EINVAL,
			"NULL prio_tc should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_rss_cfg_invalid_tc(void)
{
	u8 prio_tc[8] = {0};
	nic_cfg_mock_setup();
	/* tc_num=3 is not a power of 2 */
	RTE_TEST_ASSERT(hinic3_rss_cfg(&g_nic_cfg_hwdev, 1, 3, prio_tc) == -EINVAL,
			"Non-power-of-2 tc_num should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_rss_cfg_qpool(void)
{
	u8 prio_tc[8] = {0};
	nic_cfg_mock_setup();
	g_qinfo_type = HINIC3_QINFO_TYPE_QPOOL;
	g_nic_cfg_hwdev.qinfo_type = HINIC3_QINFO_TYPE_QPOOL;
	RTE_TEST_ASSERT(hinic3_rss_cfg(&g_nic_cfg_hwdev, 1, 2, prio_tc) == 0,
			"QPOOL mode should return 0");
	return TEST_SUCCESS;
}

/* ====== hinic3_rss_set_hash_key tests ====== */

static int
test_rss_set_hash_key_null(void)
{
	u8 key[HINIC3_RSS_KEY_SIZE] = {0};
	RTE_TEST_ASSERT(hinic3_rss_set_hash_key(NULL, key, HINIC3_RSS_KEY_SIZE) == -EINVAL,
			"NULL hwdev should return -EINVAL");
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_rss_set_hash_key(&g_nic_cfg_hwdev, NULL, HINIC3_RSS_KEY_SIZE) == -EINVAL,
			"NULL key should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_rss_set_hash_key_success(void)
{
	u8 key[HINIC3_RSS_KEY_SIZE] = {0};
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_rss_set_hash_key(&g_nic_cfg_hwdev, key, HINIC3_RSS_KEY_SIZE) == 0,
			"Set RSS hash key should succeed");
	return TEST_SUCCESS;
}

/* ====== hinic3_rss_get_hash_engine / hinic3_rss_set_hash_engine tests ====== */

static int
test_rss_get_hash_engine_null(void)
{
	u8 type = 0;
	RTE_TEST_ASSERT(hinic3_rss_get_hash_engine(NULL, &type) == -EINVAL,
			"NULL hwdev should return -EINVAL");
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_rss_get_hash_engine(&g_nic_cfg_hwdev, NULL) == -EINVAL,
			"NULL type should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_rss_get_hash_engine_success(void)
{
	u8 type = 0;
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_rss_get_hash_engine(&g_nic_cfg_hwdev, &type) == 0,
			"Get RSS hash engine should succeed");
	return TEST_SUCCESS;
}

static int
test_rss_set_hash_engine_null(void)
{
	RTE_TEST_ASSERT(hinic3_rss_set_hash_engine(NULL, 0) == -EINVAL,
			"NULL hwdev should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_rss_set_hash_engine_success(void)
{
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_rss_set_hash_engine(&g_nic_cfg_hwdev, HINIC3_RSS_HASH_ENGINE_TYPE_TOEP) == 0,
			"Set RSS hash engine should succeed");
	return TEST_SUCCESS;
}

/* ====== hinic3_get_rss_type tests ====== */

static int
test_get_rss_type_null(void)
{
	struct hinic3_rss_type type = {0};
	RTE_TEST_ASSERT(hinic3_get_rss_type(NULL, &type) == -EINVAL,
			"NULL hwdev should return -EINVAL");
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_get_rss_type(&g_nic_cfg_hwdev, NULL) == -EINVAL,
			"NULL rss_type should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_get_rss_type_success(void)
{
	struct hinic3_rss_type type = {0};
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_get_rss_type(&g_nic_cfg_hwdev, &type) == 0,
			"Get RSS type should succeed");
	return TEST_SUCCESS;
}

/* ====== hinic3_vf_get_default_cos tests ====== */

static int
test_vf_get_default_cos_success(void)
{
	u8 cos_id = 0;
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_vf_get_default_cos(&g_nic_cfg_hwdev, &cos_id) == 0,
			"Get VF default cos should succeed");
	return TEST_SUCCESS;
}

static int
test_vf_get_default_cos_fail(void)
{
	u8 cos_id = 0;
	nic_cfg_mock_setup();
	g_mock_mgmt_sync_ret = -EIO;
	g_mock_msg_to_mgmt_sync_ret = -EIO;
	RTE_TEST_ASSERT(hinic3_vf_get_default_cos(&g_nic_cfg_hwdev, &cos_id) == -EIO,
			"mgmt failure should return -EIO");
	return TEST_SUCCESS;
}

/* ====== hinic3_add_tcam_rule tests ====== */

static int
test_add_tcam_rule_null(void)
{
	struct hinic3_tcam_cfg_rule rule = {0};
	RTE_TEST_ASSERT(hinic3_add_tcam_rule(NULL, &rule, 0) == -EINVAL,
			"NULL hwdev should return -EINVAL");
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_add_tcam_rule(&g_nic_cfg_hwdev, NULL, 0) == -EINVAL,
			"NULL tcam_rule should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_add_tcam_rule_invalid_index(void)
{
	struct hinic3_tcam_cfg_rule rule = {0};
	nic_cfg_mock_setup();
	rule.index = HINIC3_MAX_TCAM_RULES_NUM;
	RTE_TEST_ASSERT(hinic3_add_tcam_rule(&g_nic_cfg_hwdev, &rule, 0) == -EINVAL,
			"Invalid tcam index should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_add_tcam_rule_success(void)
{
	struct hinic3_tcam_cfg_rule rule = {0};
	nic_cfg_mock_setup();
	rule.index = 0;
	RTE_TEST_ASSERT(hinic3_add_tcam_rule(&g_nic_cfg_hwdev, &rule, 0) == 0,
			"Add tcam rule should succeed");
	return TEST_SUCCESS;
}

/* ====== hinic3_del_tcam_rule tests ====== */

static int
test_del_tcam_rule_null(void)
{
	RTE_TEST_ASSERT(hinic3_del_tcam_rule(NULL, 0, 0) == -EINVAL,
			"NULL hwdev should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_del_tcam_rule_invalid_index(void)
{
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_del_tcam_rule(&g_nic_cfg_hwdev, HINIC3_MAX_TCAM_RULES_NUM, 0) == -EINVAL,
			"Invalid tcam index should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_del_tcam_rule_success(void)
{
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_del_tcam_rule(&g_nic_cfg_hwdev, 0, 0) == 0,
			"Del tcam rule should succeed");
	return TEST_SUCCESS;
}

/* ====== hinic3_flush_tcam_rule tests ====== */

static int
test_flush_tcam_rule_null(void)
{
	RTE_TEST_ASSERT(hinic3_flush_tcam_rule(NULL) == -EINVAL,
			"NULL hwdev should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_flush_tcam_rule_success(void)
{
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_flush_tcam_rule(&g_nic_cfg_hwdev) == 0,
			"Flush tcam rule should succeed");
	return TEST_SUCCESS;
}

/* ====== hinic3_fdir_alloc_sec_tcam_block tests ====== */

static int
test_fdir_alloc_sec_tcam_block_null(void)
{
	u16 index = 0;
	RTE_TEST_ASSERT(hinic3_fdir_alloc_sec_tcam_block(NULL, 0, &index) == -EINVAL,
			"NULL hwdev should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_fdir_alloc_sec_tcam_block_success(void)
{
	u16 index = 0;
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_fdir_alloc_sec_tcam_block(&g_nic_cfg_hwdev, 0, &index) == 0,
			"Alloc sec tcam block should succeed");
	return TEST_SUCCESS;
}

/* ====== hinic3_fdir_sec_tcam_block_free tests ====== */

static int
test_fdir_sec_tcam_block_free_null(void)
{
	u16 index = 0;
	RTE_TEST_ASSERT(hinic3_fdir_sec_tcam_block_free(NULL, 0, &index) == -EINVAL,
			"NULL hwdev should return -EINVAL");
	return TEST_SUCCESS;
}

/* ====== hinic3_set_fdir_tcam_rule_filter tests ====== */

static int
test_set_fdir_tcam_filter_null(void)
{
	RTE_TEST_ASSERT(hinic3_set_fdir_tcam_rule_filter(NULL, true) == -EINVAL,
			"NULL hwdev should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_set_fdir_tcam_filter_success(void)
{
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_set_fdir_tcam_rule_filter(&g_nic_cfg_hwdev, true) == 0,
			"Enable tcam filter should succeed");
	return TEST_SUCCESS;
}

/* ====== hinic3_get_cir_drop tests ====== */

static int
test_get_cir_drop_null(void)
{
	struct hinic3_cir_drop stats = {0};
	RTE_TEST_ASSERT(hinic3_get_cir_drop(NULL, &stats) == -EINVAL,
			"NULL hwdev should return -EINVAL");
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_get_cir_drop(&g_nic_cfg_hwdev, NULL) == -EINVAL,
			"NULL stats should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_get_cir_drop_success(void)
{
	struct hinic3_cir_drop stats = {0};
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_get_cir_drop(&g_nic_cfg_hwdev, &stats) == 0,
			"Get CIR drop should succeed");
	return TEST_SUCCESS;
}

/* ====== hinic3_cmd_vf_lag tests ====== */

static int
test_cmd_vf_lag_null(void)
{
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_cmd_vf_lag(NULL, 0, 0) == 0,
			"NULL hwdev should return 0");
	return TEST_SUCCESS;
}

static int
test_cmd_vf_lag_invalid_func_id(void)
{
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_cmd_vf_lag(&g_nic_cfg_hwdev, MAX_FUNCTION_NUM, 0) == 0,
			"Invalid func_id should return 0");
	return TEST_SUCCESS;
}

static int
test_cmd_vf_lag_success(void)
{
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_cmd_vf_lag(&g_nic_cfg_hwdev, 0, HINIC3_CMD_OPCODE_GET) == 0,
			"Get VF lag should succeed");
	return TEST_SUCCESS;
}

/* ====== hinic3_get_driver_feature tests ====== */

static int
test_get_driver_feature(void)
{
	nic_cfg_mock_setup();
	g_nic_cfg_nic_dev.feature_cap = 0xABCD;
	RTE_TEST_ASSERT(hinic3_get_driver_feature(&g_nic_cfg_nic_dev) == 0xABCD,
			"Should return feature_cap value");
	return TEST_SUCCESS;
}

/* ====== hinic3_indir_set_qid_mmap / hinic3_clear_qid_mmap tests ====== */

static int
test_indir_set_qid_mmap_success(void)
{
	int ret;
	nic_cfg_mock_setup();
	/* First insert should succeed */
	ret = hinic3_indir_set_qid_mmap(0, 0);
	RTE_TEST_ASSERT(ret == 0, "First insert should succeed");

	/* Duplicate insert should fail */
	ret = hinic3_indir_set_qid_mmap(0, 0);
	RTE_TEST_ASSERT(ret == -1, "Duplicate insert should fail");

	/* Clear and re-insert */
	hinic3_clear_qid_mmap(0);
	ret = hinic3_indir_set_qid_mmap(0, 0);
	RTE_TEST_ASSERT(ret == 0, "Re-insert after clear should succeed");

	/* Clean up */
	hinic3_clear_qid_mmap(0);
	return TEST_SUCCESS;
}

static int
test_indir_set_qid_mmap_multi(void)
{
	int ret;
	nic_cfg_mock_setup();
	/* Insert multiple entries */
	ret = hinic3_indir_set_qid_mmap(1, 10);
	RTE_TEST_ASSERT(ret == 0, "Insert q_id=1 should succeed");
	ret = hinic3_indir_set_qid_mmap(2, 20);
	RTE_TEST_ASSERT(ret == 0, "Insert q_id=2 should succeed");

	/* Clear them */
	hinic3_clear_qid_mmap(1);
	hinic3_clear_qid_mmap(2);
	return TEST_SUCCESS;
}

static int
test_indir_clear_nonexistent(void)
{
	nic_cfg_mock_setup();
	/* Clearing non-existent entry should not crash */
	hinic3_clear_qid_mmap(999);
	return TEST_SUCCESS;
}

/* ====== hinic3_rss_template_alloc tests ====== */

static int
test_rss_template_alloc_null(void)
{
	RTE_TEST_ASSERT(hinic3_rss_template_alloc(NULL, 0) == -EINVAL,
			"NULL hwdev should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_rss_template_alloc_qpool(void)
{
	nic_cfg_mock_setup();
	g_qinfo_type = HINIC3_QINFO_TYPE_QPOOL;
	g_nic_cfg_hwdev.qinfo_type = HINIC3_QINFO_TYPE_QPOOL;
	RTE_TEST_ASSERT(hinic3_rss_template_alloc(&g_nic_cfg_hwdev, 0) == 0,
			"QPOOL mode should return 0");
	return TEST_SUCCESS;
}

static int
test_rss_template_alloc_success(void)
{
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_rss_template_alloc(&g_nic_cfg_hwdev, 0) == 0,
			"Alloc RSS template should succeed");
	return TEST_SUCCESS;
}

static int
test_rss_template_alloc_full(void)
{
	nic_cfg_mock_setup();
	g_mock_mgmt_sync_ret = -ENOSPC;
	g_mock_msg_to_mgmt_sync_ret = -ENOSPC;
	/* Mock will set status to TABLE_FULL on success return, but with
	 * our simple mock, setting mgmt_sync_ret will trigger the err path.
	 * We test the error handling path. */
	RTE_TEST_ASSERT(hinic3_rss_template_alloc(&g_nic_cfg_hwdev, 0) != 0,
			"Alloc failure should not return 0");
	return TEST_SUCCESS;
}

/* ====== hinic3_rss_template_free tests ====== */

static int
test_rss_template_free_null(void)
{
	RTE_TEST_ASSERT(hinic3_rss_template_free(NULL, 0) == -EINVAL,
			"NULL hwdev should return -EINVAL");
	return TEST_SUCCESS;
}

/* ====== hinic3_sync_qos_map tests ====== */

static int
test_sync_qos_map_null(void)
{
	struct hinic3_dcb_config dcb_cfg = {0};
	RTE_TEST_ASSERT(hinic3_sync_qos_map(NULL, &dcb_cfg) == -EINVAL,
			"NULL hwdev should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_sync_qos_map_vf_not_sp620(void)
{
	struct hinic3_dcb_config dcb_cfg = {0};
	nic_cfg_mock_setup();
	g_mock_func_type = TYPE_VF;
	g_nic_cfg_hwif.attr.func_type = TYPE_VF;
	g_mock_is_sp620_nic = false;
	RTE_TEST_ASSERT(hinic3_sync_qos_map(&g_nic_cfg_hwdev, &dcb_cfg) == -EPERM,
			"VF (not SP620) should return -EPERM");
	return TEST_SUCCESS;
}

static int
test_sync_qos_map_success(void)
{
	struct hinic3_dcb_config dcb_cfg = {0};
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_sync_qos_map(&g_nic_cfg_hwdev, &dcb_cfg) == 0,
			"PF sync QoS map should succeed");
	return TEST_SUCCESS;
}

/* ====== hinic3_fdir_add_sec_tcam_rule tests ====== */

static int
test_fdir_add_sec_tcam_rule_null(void)
{
	struct hinic3_ext_tcam_cfg_rule rule = {0};
	RTE_TEST_ASSERT(hinic3_fdir_add_sec_tcam_rule(NULL, &rule, 0, 0) == -EINVAL,
			"NULL hwdev should return -EINVAL");
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_fdir_add_sec_tcam_rule(&g_nic_cfg_hwdev, NULL, 0, 0) == -EINVAL,
			"NULL tcam_rule should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_fdir_add_sec_tcam_rule_invalid_index(void)
{
	struct hinic3_ext_tcam_cfg_rule rule = {0};
	nic_cfg_mock_setup();
	rule.index = HINIC3_MAX_TCAM_RULES_NUM;
	RTE_TEST_ASSERT(hinic3_fdir_add_sec_tcam_rule(&g_nic_cfg_hwdev, &rule, 0, 0) == -EINVAL,
			"Invalid index should return -EINVAL");
	return TEST_SUCCESS;
}

/* ====== hinic3_fdir_del_sec_tcam_rule tests ====== */

static int
test_fdir_del_sec_tcam_rule_null(void)
{
	RTE_TEST_ASSERT(hinic3_fdir_del_sec_tcam_rule(NULL, 0, 0, 0) == -EINVAL,
			"NULL hwdev should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_fdir_del_sec_tcam_rule_invalid_index(void)
{
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_fdir_del_sec_tcam_rule(&g_nic_cfg_hwdev, HINIC3_MAX_TCAM_RULES_NUM, 0, 0) == -EINVAL,
			"Invalid index should return -EINVAL");
	return TEST_SUCCESS;
}

/* ====== hinic3_fdir_flush_sec_tcam_rule tests ====== */

static int
test_fdir_flush_sec_tcam_rule_null(void)
{
	RTE_TEST_ASSERT(hinic3_fdir_flush_sec_tcam_rule(NULL) == -EINVAL,
			"NULL hwdev should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_fdir_flush_sec_tcam_rule_success(void)
{
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_fdir_flush_sec_tcam_rule(&g_nic_cfg_hwdev) == 0,
			"Flush sec tcam rule should succeed");
	return TEST_SUCCESS;
}

/* ====== hinic3_fdir_cfg_sec_tcam tests ====== */

static int
test_fdir_cfg_sec_tcam_null(void)
{
	u8 en = 0;
	RTE_TEST_ASSERT(hinic3_fdir_cfg_sec_tcam(NULL, &en) == -EINVAL,
			"NULL hwdev should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_fdir_cfg_sec_tcam_not_sp620(void)
{
	u8 en = 0;
	nic_cfg_mock_setup();
	g_mock_is_sp620_nic = false;
	RTE_TEST_ASSERT(hinic3_fdir_cfg_sec_tcam(&g_nic_cfg_hwdev, &en) == 0,
			"Not SP620 should return 0 (early exit)");
	return TEST_SUCCESS;
}

/* ====== hinic3_set_link_status_follow tests ====== */

static int
test_set_link_status_follow_null(void)
{
	RTE_TEST_ASSERT(hinic3_set_link_status_follow(NULL, HINIC3_LINK_FOLLOW_DEFAULT) == -EINVAL,
			"NULL hwdev should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_set_link_status_follow_qpool(void)
{
	nic_cfg_mock_setup();
	g_qinfo_type = HINIC3_QINFO_TYPE_QPOOL;
	g_nic_cfg_hwdev.qinfo_type = HINIC3_QINFO_TYPE_QPOOL;
	RTE_TEST_ASSERT(hinic3_set_link_status_follow(&g_nic_cfg_hwdev, HINIC3_LINK_FOLLOW_DEFAULT) == 0,
			"QPOOL mode should return 0");
	return TEST_SUCCESS;
}

static int
test_set_link_status_follow_invalid_status(void)
{
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_set_link_status_follow(&g_nic_cfg_hwdev, HINIC3_LINK_FOLLOW_STATUS_MAX) == -EINVAL,
			"Invalid follow status should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_set_link_status_follow_success(void)
{
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_set_link_status_follow(&g_nic_cfg_hwdev, HINIC3_LINK_FOLLOW_PORT) == 0,
			"Valid follow status should succeed");
	return TEST_SUCCESS;
}

/* ====== hinic3_feature_nego tests ====== */

static int
test_get_feature_null(void)
{
	u64 s_feature[4] = {0};
	RTE_TEST_ASSERT(hinic3_get_feature_from_hw(NULL, s_feature, 4) == -EINVAL,
			"NULL hwdev should return -EINVAL");
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_get_feature_from_hw(&g_nic_cfg_hwdev, NULL, 4) == -EINVAL,
			"NULL s_feature should return -EINVAL");
	RTE_TEST_ASSERT(hinic3_get_feature_from_hw(&g_nic_cfg_hwdev, s_feature, 5) == -EINVAL,
			"size > MAX should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_get_feature_success(void)
{
	u64 s_feature[4] = {0};
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_get_feature_from_hw(&g_nic_cfg_hwdev, s_feature, 4) == 0,
			"Get feature should succeed");
	return TEST_SUCCESS;
}

static int
test_set_feature_null(void)
{
	u64 s_feature[4] = {0};
	RTE_TEST_ASSERT(hinic3_set_feature_to_hw(NULL, s_feature, 4) == -EINVAL,
			"NULL hwdev should return -EINVAL");
	return TEST_SUCCESS;
}

static int
test_set_feature_success(void)
{
	u64 s_feature[4] = {0};
	nic_cfg_mock_setup();
	RTE_TEST_ASSERT(hinic3_set_feature_to_hw(&g_nic_cfg_hwdev, s_feature, 4) == 0,
			"Set feature should succeed");
	return TEST_SUCCESS;
}

/* ====== Test suite definition ====== */

static struct unit_test_suite hinic3_nic_cfg_test_suite = {
	.suite_name = "HINIC3 NIC Cfg Tests",
	.setup = NULL,
	.teardown = NULL,
	.unit_test_cases = {
		/* hinic3_set_ci_table */
		TEST_CASE(test_set_ci_table_null_hwdev),
		TEST_CASE(test_set_ci_table_null_attr),
		TEST_CASE(test_set_ci_table_success),
		TEST_CASE(test_set_ci_table_with_intr),
		TEST_CASE(test_set_ci_table_mgmt_fail),

		/* hinic3_set_mac */
		TEST_CASE(test_set_mac_null_hwdev),
		TEST_CASE(test_set_mac_null_mac),
		TEST_CASE(test_set_mac_qpool_mode),
		TEST_CASE(test_set_mac_bifur_shared),
		TEST_CASE(test_set_mac_invalid_vlan),
		TEST_CASE(test_set_mac_success),
		TEST_CASE(test_set_mac_mgmt_fail),

		/* hinic3_del_mac */
		TEST_CASE(test_del_mac_null_hwdev),
		TEST_CASE(test_del_mac_null_mac),
		TEST_CASE(test_del_mac_invalid_vlan),
		TEST_CASE(test_del_mac_success),
		TEST_CASE(test_del_mac_qpool_mode),

		/* hinic3_update_mac */
		TEST_CASE(test_update_mac_null_params),
		TEST_CASE(test_update_mac_invalid_vlan),
		TEST_CASE(test_update_mac_success),

		/* hinic3_get_default_mac */
		TEST_CASE(test_get_default_mac_null),
		TEST_CASE(test_get_default_mac_success),
		TEST_CASE(test_get_default_mac_bifur_shared),
		TEST_CASE(test_get_default_mac_mgmt_fail),

		/* hinic3_get_port_info */
		TEST_CASE(test_get_port_info_null),
		TEST_CASE(test_get_port_info_success),
		TEST_CASE(test_get_port_info_mgmt_fail),

		/* hinic3_get_link_state */
		TEST_CASE(test_get_link_state_null),
		TEST_CASE(test_get_link_state_success),
		TEST_CASE(test_get_link_state_mgmt_fail),

		/* hinic3_set_vport_enable */
		TEST_CASE(test_set_vport_enable_null),
		TEST_CASE(test_set_vport_enable_qpool),
		TEST_CASE(test_set_vport_enable_success),
		TEST_CASE(test_set_vport_enable_mgmt_fail),

		/* hinic3_set_port_enable */
		TEST_CASE(test_set_port_enable_null),
		TEST_CASE(test_set_port_enable_vf),
		TEST_CASE(test_set_port_enable_success),
		TEST_CASE(test_set_port_enable_mgmt_fail),

		/* hinic3_set_port_mtu */
		TEST_CASE(test_set_port_mtu_null),
		TEST_CASE(test_set_port_mtu_too_small),
		TEST_CASE(test_set_port_mtu_too_large),
		TEST_CASE(test_set_port_mtu_success),
		TEST_CASE(test_set_port_mtu_boundary),

		/* hinic3_set_rx_vlan_offload */
		TEST_CASE(test_set_rx_vlan_offload_null),
		TEST_CASE(test_set_rx_vlan_offload_qpool),
		TEST_CASE(test_set_rx_vlan_offload_success),

		/* hinic3_set_vlan_fliter */
		TEST_CASE(test_set_vlan_fliter_null),
		TEST_CASE(test_set_vlan_fliter_qpool),
		TEST_CASE(test_set_vlan_fliter_success),

		/* hinic3_set_rx_lro_state */
		TEST_CASE(test_set_rx_lro_state_null),
		TEST_CASE(test_set_rx_lro_state_success),
		TEST_CASE(test_set_rx_lro_state_disabled),
		TEST_CASE(test_set_rx_lro_state_vf),

		/* hinic3_set_rx_mode */
		TEST_CASE(test_set_rx_mode_null),
		TEST_CASE(test_set_rx_mode_qpool),
		TEST_CASE(test_set_rx_mode_success),

		/* hinic3_flush_qps_res */
		TEST_CASE(test_flush_qps_res_null),
		TEST_CASE(test_flush_qps_res_success),
		TEST_CASE(test_flush_qps_res_mgmt_fail),

		/* hinic3_set/get_pause_info */
		TEST_CASE(test_set_pause_info_null),
		TEST_CASE(test_set_pause_info_success),
		TEST_CASE(test_get_pause_info_null),
		TEST_CASE(test_get_pause_info_success),

		/* hinic3_get/clear_vport_stats */
		TEST_CASE(test_get_vport_stats_null),
		TEST_CASE(test_get_vport_stats_success),
		TEST_CASE(test_clear_vport_stats_null),
		TEST_CASE(test_clear_vport_stats_success),

		/* hinic3_init_function_table */
		TEST_CASE(test_init_function_table_success),
		TEST_CASE(test_init_function_table_mgmt_fail),

		/* hinic3_init/free_nic_hwdev */
		TEST_CASE(test_init_nic_hwdev_pf),
		TEST_CASE(test_init_nic_hwdev_vf),
		TEST_CASE(test_init_nic_hwdev_vf_fail),
		TEST_CASE(test_free_nic_hwdev_null),
		TEST_CASE(test_free_nic_hwdev_pf),
		TEST_CASE(test_free_nic_hwdev_vf),

		/* hinic3_set/get_fec_mode */
		TEST_CASE(test_set_fec_mode_null),
		TEST_CASE(test_set_fec_mode_success),
		TEST_CASE(test_set_fec_mode_mgmt_fail),
		TEST_CASE(test_get_fec_mode_null),
		TEST_CASE(test_get_fec_mode_success),
		TEST_CASE(test_get_fec_mode_null_pointers),

		/* hinic3_sync_dcb_state */
		TEST_CASE(test_sync_dcb_state_null),
		TEST_CASE(test_sync_dcb_state_vf_not_sp620),
		TEST_CASE(test_sync_dcb_state_success),

		/* hinic3_set_qos_port_trust */
		TEST_CASE(test_set_qos_port_trust_null),
		TEST_CASE(test_set_qos_port_trust_success),

		/* hinic3_qinfo_type_init */
		TEST_CASE(test_qinfo_type_init_normal),

		/* hinic3_rss_cfg */
		TEST_CASE(test_rss_cfg_null),
		TEST_CASE(test_rss_cfg_invalid_tc),
		TEST_CASE(test_rss_cfg_qpool),

		/* hinic3_rss_set_hash_key */
		TEST_CASE(test_rss_set_hash_key_null),
		TEST_CASE(test_rss_set_hash_key_success),

		/* hinic3_rss_get/set_hash_engine */
		TEST_CASE(test_rss_get_hash_engine_null),
		TEST_CASE(test_rss_get_hash_engine_success),
		TEST_CASE(test_rss_set_hash_engine_null),
		TEST_CASE(test_rss_set_hash_engine_success),

		/* hinic3_get_rss_type */
		TEST_CASE(test_get_rss_type_null),
		TEST_CASE(test_get_rss_type_success),

		/* hinic3_vf_get_default_cos */
		TEST_CASE(test_vf_get_default_cos_success),
		TEST_CASE(test_vf_get_default_cos_fail),

		/* hinic3_add/del/flush_tcam_rule */
		TEST_CASE(test_add_tcam_rule_null),
		TEST_CASE(test_add_tcam_rule_invalid_index),
		TEST_CASE(test_add_tcam_rule_success),
		TEST_CASE(test_del_tcam_rule_null),
		TEST_CASE(test_del_tcam_rule_invalid_index),
		TEST_CASE(test_del_tcam_rule_success),
		TEST_CASE(test_flush_tcam_rule_null),
		TEST_CASE(test_flush_tcam_rule_success),

		/* hinic3_fdir_* */
		TEST_CASE(test_fdir_alloc_sec_tcam_block_null),
		TEST_CASE(test_fdir_alloc_sec_tcam_block_success),
		TEST_CASE(test_fdir_sec_tcam_block_free_null),
		TEST_CASE(test_set_fdir_tcam_filter_null),
		TEST_CASE(test_set_fdir_tcam_filter_success),
		TEST_CASE(test_fdir_add_sec_tcam_rule_null),
		TEST_CASE(test_fdir_add_sec_tcam_rule_invalid_index),
		TEST_CASE(test_fdir_del_sec_tcam_rule_null),
		TEST_CASE(test_fdir_del_sec_tcam_rule_invalid_index),
		TEST_CASE(test_fdir_flush_sec_tcam_rule_null),
		TEST_CASE(test_fdir_flush_sec_tcam_rule_success),
		TEST_CASE(test_fdir_cfg_sec_tcam_null),
		TEST_CASE(test_fdir_cfg_sec_tcam_not_sp620),

		/* hinic3_set_link_status_follow */
		TEST_CASE(test_set_link_status_follow_null),
		TEST_CASE(test_set_link_status_follow_qpool),
		TEST_CASE(test_set_link_status_follow_invalid_status),
		TEST_CASE(test_set_link_status_follow_success),

		/* hinic3_get_cir_drop */
		TEST_CASE(test_get_cir_drop_null),
		TEST_CASE(test_get_cir_drop_success),

		/* hinic3_cmd_vf_lag */
		TEST_CASE(test_cmd_vf_lag_null),
		TEST_CASE(test_cmd_vf_lag_invalid_func_id),
		TEST_CASE(test_cmd_vf_lag_success),

		/* hinic3_get_driver_feature */
		TEST_CASE(test_get_driver_feature),

		/* hinic3_indir_set_qid_mmap / hinic3_clear_qid_mmap */
		TEST_CASE(test_indir_set_qid_mmap_success),
		TEST_CASE(test_indir_set_qid_mmap_multi),
		TEST_CASE(test_indir_clear_nonexistent),

		/* hinic3_rss_template_alloc/free */
		TEST_CASE(test_rss_template_alloc_null),
		TEST_CASE(test_rss_template_alloc_qpool),
		TEST_CASE(test_rss_template_alloc_success),
		TEST_CASE(test_rss_template_alloc_full),
		TEST_CASE(test_rss_template_free_null),

		/* hinic3_sync_qos_map */
		TEST_CASE(test_sync_qos_map_null),
		TEST_CASE(test_sync_qos_map_vf_not_sp620),
		TEST_CASE(test_sync_qos_map_success),

		/* hinic3_feature_nego */
		TEST_CASE(test_get_feature_null),
		TEST_CASE(test_get_feature_success),
		TEST_CASE(test_set_feature_null),
		TEST_CASE(test_set_feature_success),

		TEST_CASES_END()
	}
};

static int
test_hinic3_nic_cfg(void)
{
	return unit_test_suite_runner(&hinic3_nic_cfg_test_suite);
}

REGISTER_TEST_COMMAND(hinic3_nic_cfg_autotest, test_hinic3_nic_cfg);
