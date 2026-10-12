/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2025-2026 Huawei Technologies Co., Ltd
 * Unit tests for hinic3 flow API
 *
 * Tests cover:
 * - Flow attr validation (egress, priority, group, ingress)
 * - Null parameter validation (pattern, actions, attr)
 * - Ethertype flow operations (validate, create, destroy, flush)
 * - IPv4/IPv6 FDIR flow operations (validate, create)
 * - VXLAN/GENEVE tunnel flow operations
 * - RSS action handling (normal, qpool, sp230, error cases)
 * - RSS template management (alloc, free, refcount, reuse)
 * - Drop action handling (SP620 vs SP230)
 * - Flow destroy operations (null, fdir, rss, sp230)
 * - Flow flush operations (empty, mixed, fdir)
 * - Flow query operations (null, fdir, non-existent)
 * - Pattern matching (supported, unsupported, too many items)
 */

#include <rte_common.h>
#include <rte_ethdev.h>
#include <rte_flow.h>
#ifdef DPDK_21_11
#include <ethdev_driver.h>
#endif
#include <rte_flow_driver.h>
#include <rte_bus_pci.h>
#include <rte_pci.h>
#include <rte_malloc.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>
#include <rte_byteorder.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>
#include <rte_vxlan.h>
#include <string.h>

#include "../test.h"
#include <rte_test.h>
#include <rte_log.h>

#include "base/hinic3_compat.h"
#include "base/hinic3_pmd_nic_cfg.h"
#include "base/hinic3_pmd_hwdev.h"
#include "base/hinic3_pmd_hwif.h"
#include "base/hinic3_pmd_hw_cfg.h"
#include "hinic3_pmd_ethdev.h"
#include "hinic3_pmd_fdir.h"
#include "hinic3_pmd_flow.h"
#include "hinic3_pmd_rx.h"
#include "test_hinic3_mock.h"

#ifndef DPDK_21_11
extern struct rte_eth_dev rte_eth_devices[];
#endif

extern const struct rte_flow_ops hinic3_flow_ops;

#define MOCK_ETH_DEV_PORT_ID 0

/* ====== Mock device structures ====== */

static struct rte_pci_device g_mock_pci_dev;
static struct hinic3_hwif g_mock_hwif;
static struct cfg_mgmt_info g_mock_cfg_mgmt;
static struct hinic3_hwdev g_mock_hwdev;
static struct hinic3_nic_dev g_mock_nic_dev;
static struct rte_eth_dev_data g_mock_eth_dev_data;
static void *g_mock_rx_queues[128];
static void *g_mock_tx_queues[128];

/* Zeroed hinic3_rxq for QUEUE action tests: is_hairpin=0 so the
 * hairpin check passes. */
static struct hinic3_rxq g_mock_rxq;

/* ====== Mock device setup ====== */

static void
mock_flow_dev_setup(void)
{
	memset(&g_mock_pci_dev, 0, sizeof(g_mock_pci_dev));
	g_mock_pci_dev.id.device_id = HINIC3_DEV_ID_SP620;

	memset(&g_mock_hwif, 0, sizeof(g_mock_hwif));
	g_mock_hwif.attr.func_global_idx = 0x123;

	memset(&g_mock_cfg_mgmt, 0, sizeof(g_mock_cfg_mgmt));
	g_mock_cfg_mgmt.svc_cap.er_id = 1;

	memset(&g_mock_hwdev, 0, sizeof(g_mock_hwdev));
	g_mock_hwdev.hwif = &g_mock_hwif;
	g_mock_hwdev.cfg_mgmt = &g_mock_cfg_mgmt;
	g_mock_hwdev.dev_handle = &g_mock_nic_dev;
	g_mock_hwdev.pci_dev = &g_mock_pci_dev;
	g_mock_hwdev.port_id = MOCK_ETH_DEV_PORT_ID;

	memset(&g_mock_nic_dev, 0, sizeof(g_mock_nic_dev));
	g_mock_nic_dev.hwdev = &g_mock_hwdev;
	g_mock_nic_dev.feature_cap = 0;
	g_mock_nic_dev.rss_state = HINIC3_RSS_ENABLE;
	g_mock_nic_dev.num_rqs = 4;
	TAILQ_INIT(&g_mock_nic_dev.tcam.tcam_list);
	TAILQ_INIT(&g_mock_nic_dev.tcam.tcam_dynamic_info.tcam_dynamic_list);
	TAILQ_INIT(&g_mock_nic_dev.filter_ethertype_list);
	TAILQ_INIT(&g_mock_nic_dev.filter_fdir_rule_list);
	TAILQ_INIT(&g_mock_nic_dev.rss_template_list);

	/* Set up rte_eth_devices[0] */
	memset(&g_mock_eth_dev_data, 0, sizeof(g_mock_eth_dev_data));
	g_mock_eth_dev_data.dev_private = &g_mock_nic_dev;
	memset(&g_mock_rxq, 0, sizeof(g_mock_rxq));
	memset(g_mock_rx_queues, 0, sizeof(g_mock_rx_queues));
	memset(g_mock_tx_queues, 0, sizeof(g_mock_tx_queues));
	/* Point all rx_queues to the zeroed rxq so QUEUE action hairpin
	 * check passes. */
	for (int i = 0; i < 128; i++)
		g_mock_rx_queues[i] = &g_mock_rxq;
	g_mock_eth_dev_data.rx_queues = g_mock_rx_queues;
	g_mock_eth_dev_data.tx_queues = g_mock_tx_queues;
	g_mock_eth_dev_data.nb_rx_queues = 128;
	g_mock_eth_dev_data.nb_tx_queues = 128;
	rte_eth_devices[MOCK_ETH_DEV_PORT_ID].data = &g_mock_eth_dev_data;
	rte_eth_devices[MOCK_ETH_DEV_PORT_ID].device = &g_mock_pci_dev.device;

	/* Reset mock control variables */
	g_mock_bifur_en = 0;
	g_mock_iso_en = 0;
	g_mock_bifur_type = 0;
	g_mock_is_shared_dev = false;
	g_mock_mgmt_sync_ret = 0;
	g_mock_add_tcam_rule_ret = 0;
	g_mock_del_tcam_rule_ret = 0;
	g_mock_alloc_tcam_block_ret = 0;
	g_mock_alloc_tcam_block_id = 0;
	g_mock_sec_tcam_en = 0;
	g_mock_sec_tcam_ret = 0;
	g_mock_update_rss_config_ret = 0;
	g_mock_cmdq_set_rss_queue_type_ret = 0;
	g_mock_global_func_id = g_mock_hwif.attr.func_global_idx;
	g_mock_mgmt_cfg_qgrp_id_ret = 0;
	g_mock_mgmt_cfg_qgrp_id_val = 1;
	g_mock_rss_template_alloc_ret = 0;
	g_mock_rss_template_free_ret = 0;
	g_mock_rss_get_indir_tbl_ret = 0;
	g_mock_rss_set_indir_tbl_ret = 0;
	g_mock_rss_set_indir_tbl_qpool_ret = 0;
	g_mock_rss_queue_set_indir_tbl_ret = 0;
	g_mock_rss_cfg_ret = 0;
	g_mock_flow_query_fdir_ret = 0;
	g_mock_is_sp620_nic = true;
	g_mock_is_sp230_nic = false;

	/* Reset global mode */
	g_qinfo_type = HINIC3_QINFO_TYPE_NORMAL;
	g_mock_hwdev.qinfo_type = HINIC3_QINFO_TYPE_NORMAL;
}

static void
mock_setup_sp230(void)
{
	g_mock_pci_dev.id.device_id = HINIC3_DEV_ID_SP230;
	g_mock_is_sp620_nic = false;
	g_mock_is_sp230_nic = true;
}

static void
mock_setup_qpool(void)
{
	g_qinfo_type = HINIC3_QINFO_TYPE_QPOOL;
	g_mock_hwdev.qinfo_type = HINIC3_QINFO_TYPE_QPOOL;
}

static void
mock_setup_bifur(void)
{
	g_qinfo_type = HINIC3_QINFO_TYPE_BIFUR;
	g_mock_hwdev.qinfo_type = HINIC3_QINFO_TYPE_BIFUR;
	g_mock_bifur_en = 1;
	g_mock_bifur_type = 2;
}

static void
mock_prealloc_tcam_block(void)
{
	hinic3_alloc_dynamic_block_resource(&g_mock_nic_dev.tcam, 0, 0, false);
}

/* ====== Flow pattern/action helper functions ====== */

static const struct rte_flow_attr g_default_attr = {
	.ingress = 1,
};

static void
make_eth_item(struct rte_flow_item_eth *spec,
	       struct rte_flow_item_eth *mask,
	       uint16_t ether_type,
	       struct rte_flow_item *item)
{
	item->type = RTE_FLOW_ITEM_TYPE_ETH;
	item->last = NULL;
	if (ether_type == 0) {
		/* In non-bifur mode, ETH spec/mask must be NULL (fully masked) */
		item->spec = NULL;
		item->mask = NULL;
	} else {
		memset(spec, 0, sizeof(*spec));
		memset(mask, 0, sizeof(*mask));
		spec->type = rte_cpu_to_be_16(ether_type);
		mask->type = rte_cpu_to_be_16(0xFFFF);
		item->spec = spec;
		item->mask = mask;
	}
}

static void
make_ipv4_item(struct rte_flow_item_ipv4 *spec,
	       struct rte_flow_item_ipv4 *mask,
	       uint32_t src_ip, uint32_t dst_ip,
	       struct rte_flow_item *item)
{
	memset(spec, 0, sizeof(*spec));
	memset(mask, 0, sizeof(*mask));
	spec->hdr.src_addr = rte_cpu_to_be_32(src_ip);
	spec->hdr.dst_addr = rte_cpu_to_be_32(dst_ip);
	mask->hdr.src_addr = rte_cpu_to_be_32(0xFFFFFFFF);
	mask->hdr.dst_addr = rte_cpu_to_be_32(0xFFFFFFFF);
	item->type = RTE_FLOW_ITEM_TYPE_IPV4;
	item->spec = spec;
	item->mask = mask;
	item->last = NULL;
}

static void
make_ipv6_item(struct rte_flow_item_ipv6 *spec,
	       struct rte_flow_item_ipv6 *mask,
	       struct rte_flow_item *item)
{
	memset(spec, 0, sizeof(*spec));
	memset(mask, 0, sizeof(*mask));
	memset(&spec->hdr.src_addr, 0x11, 16);
	memset(&mask->hdr.src_addr, 0xFF, 16);
	item->type = RTE_FLOW_ITEM_TYPE_IPV6;
	item->spec = spec;
	item->mask = mask;
	item->last = NULL;
}

static void
make_tcp_item(struct rte_flow_item_tcp *spec,
	      struct rte_flow_item_tcp *mask,
	      uint16_t src_port, uint16_t dst_port,
	      struct rte_flow_item *item)
{
	memset(spec, 0, sizeof(*spec));
	memset(mask, 0, sizeof(*mask));
	spec->hdr.src_port = rte_cpu_to_be_16(src_port);
	spec->hdr.dst_port = rte_cpu_to_be_16(dst_port);
	mask->hdr.src_port = rte_cpu_to_be_16(0xFFFF);
	mask->hdr.dst_port = rte_cpu_to_be_16(0xFFFF);
	item->type = RTE_FLOW_ITEM_TYPE_TCP;
	item->spec = spec;
	item->mask = mask;
	item->last = NULL;
}

static void
make_udp_item(struct rte_flow_item_udp *spec,
	      struct rte_flow_item_udp *mask,
	      uint16_t src_port, uint16_t dst_port,
	      struct rte_flow_item *item)
{
	memset(spec, 0, sizeof(*spec));
	memset(mask, 0, sizeof(*mask));
	spec->hdr.src_port = rte_cpu_to_be_16(src_port);
	spec->hdr.dst_port = rte_cpu_to_be_16(dst_port);
	mask->hdr.src_port = rte_cpu_to_be_16(0xFFFF);
	mask->hdr.dst_port = rte_cpu_to_be_16(0xFFFF);
	item->type = RTE_FLOW_ITEM_TYPE_UDP;
	item->spec = spec;
	item->mask = mask;
	item->last = NULL;
}

static void
make_udp_any_item(struct rte_flow_item *item)
{
	item->type = RTE_FLOW_ITEM_TYPE_UDP;
	item->spec = NULL;
	item->mask = NULL;
	item->last = NULL;
}

static void
make_vxlan_item(struct rte_flow_item_vxlan *spec,
		struct rte_flow_item_vxlan *mask,
		struct rte_flow_item *item)
{
	memset(spec, 0, sizeof(*spec));
	memset(mask, 0, sizeof(*mask));
	mask->vni[0] = 0xFF;
	mask->vni[1] = 0xFF;
	mask->vni[2] = 0xFF;
	item->type = RTE_FLOW_ITEM_TYPE_VXLAN;
	item->spec = spec;
	item->mask = mask;
	item->last = NULL;
}

static void
make_geneve_item(struct rte_flow_item_geneve *spec,
		  struct rte_flow_item_geneve *mask,
		  struct rte_flow_item *item)
{
	memset(spec, 0, sizeof(*spec));
	memset(mask, 0, sizeof(*mask));
	mask->vni[0] = 0xFF;
	mask->vni[1] = 0xFF;
	mask->vni[2] = 0xFF;
	item->type = RTE_FLOW_ITEM_TYPE_GENEVE;
	item->spec = spec;
	item->mask = mask;
	item->last = NULL;
}

static void
make_end_item(struct rte_flow_item *item)
{
	item->type = RTE_FLOW_ITEM_TYPE_END;
	item->spec = NULL;
	item->mask = NULL;
	item->last = NULL;
}

static void __rte_unused
make_void_item(struct rte_flow_item *item)
{
	item->type = RTE_FLOW_ITEM_TYPE_VOID;
	item->spec = NULL;
	item->mask = NULL;
	item->last = NULL;
}

static void
make_queue_action(struct rte_flow_action_queue *conf,
		  uint16_t index,
		  struct rte_flow_action *action)
{
	memset(conf, 0, sizeof(*conf));
	conf->index = index;
	action->type = RTE_FLOW_ACTION_TYPE_QUEUE;
	action->conf = conf;
}

static void
make_rss_action(struct rte_flow_action_rss *conf,
		uint16_t *queues, uint16_t queue_num,
		struct rte_flow_action *action)
{
	memset(conf, 0, sizeof(*conf));
	conf->queue = queues;
	conf->queue_num = queue_num;
	conf->key = NULL;
	conf->key_len = 0;
	conf->types = RTE_ETH_RSS_IP;
	conf->level = 0;
	action->type = RTE_FLOW_ACTION_TYPE_RSS;
	action->conf = conf;
}

static void
make_drop_action(struct rte_flow_action *action)
{
	action->type = RTE_FLOW_ACTION_TYPE_DROP;
	action->conf = NULL;
}

static void __rte_unused
make_void_action(struct rte_flow_action *action)
{
	action->type = RTE_FLOW_ACTION_TYPE_VOID;
	action->conf = NULL;
}

static void
make_end_action(struct rte_flow_action *action)
{
	action->type = RTE_FLOW_ACTION_TYPE_END;
	action->conf = NULL;
}

/* ====== Setup/teardown for test suite ====== */

static int
flow_test_setup(void)
{
	return TEST_SUCCESS;
}

static void
flow_test_teardown(void)
{
}

/* ================================================================== */
/* A. Flow attr validation tests                                      */
/* ================================================================== */

static int
test_flow_attr_egress(void)
{
	struct rte_flow_attr attr;
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item pattern[3];
	struct rte_flow_action_queue q_conf;
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int ret;

	mock_flow_dev_setup();

	make_eth_item(&eth_spec, &eth_mask, RTE_ETHER_TYPE_ARP, &pattern[0]);
	make_end_item(&pattern[1]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	memset(&attr, 0, sizeof(attr));
	attr.ingress = 1;
	attr.egress = 1;

	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				       &attr, pattern, actions, &error);
	RTE_TEST_ASSERT(ret < 0, "egress=1 should fail");

	return TEST_SUCCESS;
}

static int
test_flow_attr_priority(void)
{
	struct rte_flow_attr attr;
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item pattern[3];
	struct rte_flow_action_queue q_conf;
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int ret;

	mock_flow_dev_setup();

	make_eth_item(&eth_spec, &eth_mask, RTE_ETHER_TYPE_ARP, &pattern[0]);
	make_end_item(&pattern[1]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	memset(&attr, 0, sizeof(attr));
	attr.ingress = 1;
	attr.priority = 1;

	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				       &attr, pattern, actions, &error);
	RTE_TEST_ASSERT(ret < 0, "priority!=0 should fail");

	return TEST_SUCCESS;
}

static int
test_flow_attr_group(void)
{
	struct rte_flow_attr attr;
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item pattern[3];
	struct rte_flow_action_queue q_conf;
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int ret;

	mock_flow_dev_setup();

	make_eth_item(&eth_spec, &eth_mask, RTE_ETHER_TYPE_ARP, &pattern[0]);
	make_end_item(&pattern[1]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	memset(&attr, 0, sizeof(attr));
	attr.ingress = 1;
	attr.group = 1;

	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				       &attr, pattern, actions, &error);
	RTE_TEST_ASSERT(ret < 0, "group!=0 should fail");

	return TEST_SUCCESS;
}

static int
test_flow_attr_no_ingress(void)
{
	struct rte_flow_attr attr;
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item pattern[3];
	struct rte_flow_action_queue q_conf;
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int ret;

	mock_flow_dev_setup();

	make_eth_item(&eth_spec, &eth_mask, RTE_ETHER_TYPE_ARP, &pattern[0]);
	make_end_item(&pattern[1]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	memset(&attr, 0, sizeof(attr));
	attr.ingress = 0;

	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				       &attr, pattern, actions, &error);
	RTE_TEST_ASSERT(ret < 0, "ingress=0 should fail");

	return TEST_SUCCESS;
}

/* ================================================================== */
/* B. Flow validate null parameter tests                              */
/* ================================================================== */

static int
test_flow_validate_null_pattern(void)
{
	struct rte_flow_attr attr = g_default_attr;
	struct rte_flow_action_queue q_conf;
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int ret;

	mock_flow_dev_setup();

	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				       &attr, NULL, actions, &error);
	RTE_TEST_ASSERT(ret < 0, "null pattern should fail");

	return TEST_SUCCESS;
}

static int
test_flow_validate_null_actions(void)
{
	struct rte_flow_attr attr = g_default_attr;
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item pattern[3];
	struct rte_flow_error error;
	int ret;

	mock_flow_dev_setup();

	make_eth_item(&eth_spec, &eth_mask, RTE_ETHER_TYPE_ARP, &pattern[0]);
	make_end_item(&pattern[1]);

	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				       &attr, pattern, NULL, &error);
	RTE_TEST_ASSERT(ret < 0, "null actions should fail");

	return TEST_SUCCESS;
}

static int
test_flow_validate_null_attr(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item pattern[3];
	struct rte_flow_action_queue q_conf;
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int ret;

	mock_flow_dev_setup();

	make_eth_item(&eth_spec, &eth_mask, RTE_ETHER_TYPE_ARP, &pattern[0]);
	make_end_item(&pattern[1]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				       NULL, pattern, actions, &error);
	RTE_TEST_ASSERT(ret < 0, "null attr should fail");

	return TEST_SUCCESS;
}

/* ================================================================== */
/* C. Ethertype flow tests                                            */
/* ================================================================== */

static int
test_flow_validate_ethertype(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item pattern[3];
	struct rte_flow_action_queue q_conf;
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int ret;

	mock_flow_dev_setup();

	make_eth_item(&eth_spec, &eth_mask, RTE_ETHER_TYPE_ARP, &pattern[0]);
	make_end_item(&pattern[1]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				       &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "validate ethertype should succeed");

	return TEST_SUCCESS;
}

static int
test_flow_create_ethertype(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item pattern[3];
	struct rte_flow_action_queue q_conf;
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	struct rte_flow *flow;
	int ret;

	mock_flow_dev_setup();

	make_eth_item(&eth_spec, &eth_mask, RTE_ETHER_TYPE_ARP, &pattern[0]);
	make_end_item(&pattern[1]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	flow = hinic3_flow_ops.create(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_NOT_NULL(flow, "create ethertype flow should succeed");

	/* Verify flow is in the ethertype list */
	RTE_TEST_ASSERT_EQUAL(flow->filter_type, RTE_ETH_FILTER_ETHERTYPE,
			      "filter type should be ethertype");

	ret = hinic3_flow_ops.destroy(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      flow, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "destroy ethertype flow should succeed");

	return TEST_SUCCESS;
}

static int
test_flow_destroy_ethertype(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item pattern[3];
	struct rte_flow_action_queue q_conf;
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	struct rte_flow *flow;
	struct rte_flow *entry;
	int ret;
	bool found = false;

	mock_flow_dev_setup();

	make_eth_item(&eth_spec, &eth_mask, RTE_ETHER_TYPE_LLDP, &pattern[0]);
	make_end_item(&pattern[1]);
	make_queue_action(&q_conf, 1, &actions[0]);
	make_end_action(&actions[1]);

	flow = hinic3_flow_ops.create(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_NOT_NULL(flow, "create ethertype flow");

	ret = hinic3_flow_ops.destroy(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      flow, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "destroy ethertype flow");

	/* Verify flow is removed from list */
	TAILQ_FOREACH(entry, &g_mock_nic_dev.filter_ethertype_list, node) {
		if (entry == flow) {
			found = true;
			break;
		}
	}
	RTE_TEST_ASSERT_EQUAL(found, false, "flow should be removed from list");

	return TEST_SUCCESS;
}

static int
test_flow_flush_ethertype(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item pattern[3];
	struct rte_flow_action_queue q_conf;
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	struct rte_flow *flow1, *flow2;
	int ret;

	mock_flow_dev_setup();

	make_eth_item(&eth_spec, &eth_mask, RTE_ETHER_TYPE_ARP, &pattern[0]);
	make_end_item(&pattern[1]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	flow1 = hinic3_flow_ops.create(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				       &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_NOT_NULL(flow1, "create ethertype flow 1");

	make_eth_item(&eth_spec, &eth_mask, RTE_ETHER_TYPE_LLDP, &pattern[0]);
	flow2 = hinic3_flow_ops.create(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				       &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_NOT_NULL(flow2, "create ethertype flow 2");

	ret = hinic3_flow_ops.flush(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID], &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "flush should succeed");

	RTE_TEST_ASSERT(TAILQ_EMPTY(&g_mock_nic_dev.filter_ethertype_list),
		       "ethertype list should be empty after flush");

	return TEST_SUCCESS;
}

/* ================================================================== */
/* D. IPv4 flow tests                                                 */
/* ================================================================== */

static int
test_flow_validate_ipv4_vlan(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_tcp tcp_spec, tcp_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action_queue q_conf;
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int ret;

	mock_flow_dev_setup();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_tcp_item(&tcp_spec, &tcp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				       &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "validate ETH+IPV4+TCP should succeed");

	return TEST_SUCCESS;
}

static int
test_flow_create_ipv4_vlan(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_tcp tcp_spec, tcp_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action_queue q_conf;
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	struct rte_flow *flow;
	int ret;

	mock_flow_dev_setup();
	mock_prealloc_tcam_block();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_tcp_item(&tcp_spec, &tcp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	flow = hinic3_flow_ops.create(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_NOT_NULL(flow, "create fdir flow should succeed");

	RTE_TEST_ASSERT_EQUAL(flow->filter_type, RTE_ETH_FILTER_FDIR,
			      "filter type should be fdir");

	ret = hinic3_flow_ops.destroy(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      flow, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "destroy fdir flow should succeed");

	return TEST_SUCCESS;
}

static int
test_flow_validate_ipv4vxlan_ipv4(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_vxlan vxlan_spec, vxlan_mask;
	struct rte_flow_item_eth inner_eth_spec, inner_eth_mask;
	struct rte_flow_item_ipv4 inner_ip_spec, inner_ip_mask;
	struct rte_flow_item pattern[8];
	struct rte_flow_action_queue q_conf;
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int ret;

	mock_flow_dev_setup();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_udp_any_item(&pattern[2]);
	make_vxlan_item(&vxlan_spec, &vxlan_mask, &pattern[3]);
	make_eth_item(&inner_eth_spec, &inner_eth_mask, 0, &pattern[4]);
	make_ipv4_item(&inner_ip_spec, &inner_ip_mask, 0x0A000001, 0x0A000002,
		       &pattern[5]);
	make_end_item(&pattern[6]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				       &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "validate VXLAN IPv4 should succeed");

	return TEST_SUCCESS;
}

static int
test_flow_create_ipv4vxlan_ipv4(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_vxlan vxlan_spec, vxlan_mask;
	struct rte_flow_item_eth inner_eth_spec, inner_eth_mask;
	struct rte_flow_item_ipv4 inner_ip_spec, inner_ip_mask;
	struct rte_flow_item pattern[8];
	struct rte_flow_action_queue q_conf;
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	struct rte_flow *flow;
	int ret;

	mock_flow_dev_setup();
	mock_prealloc_tcam_block();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_udp_any_item(&pattern[2]);
	make_vxlan_item(&vxlan_spec, &vxlan_mask, &pattern[3]);
	make_eth_item(&inner_eth_spec, &inner_eth_mask, 0, &pattern[4]);
	make_ipv4_item(&inner_ip_spec, &inner_ip_mask, 0x0A000001, 0x0A000002,
		       &pattern[5]);
	make_end_item(&pattern[6]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	flow = hinic3_flow_ops.create(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_NOT_NULL(flow, "create VXLAN fdir flow should succeed");

	ret = hinic3_flow_ops.destroy(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      flow, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "destroy VXLAN fdir flow");

	return TEST_SUCCESS;
}

static int
test_flow_validate_ipv4vxlan_ipv4_tcp(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_vxlan vxlan_spec, vxlan_mask;
	struct rte_flow_item_eth inner_eth_spec, inner_eth_mask;
	struct rte_flow_item_ipv4 inner_ip_spec, inner_ip_mask;
	struct rte_flow_item_tcp inner_tcp_spec, inner_tcp_mask;
	struct rte_flow_item pattern[9];
	struct rte_flow_action_queue q_conf;
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int ret;

	mock_flow_dev_setup();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_udp_any_item(&pattern[2]);
	make_vxlan_item(&vxlan_spec, &vxlan_mask, &pattern[3]);
	make_eth_item(&inner_eth_spec, &inner_eth_mask, 0, &pattern[4]);
	make_ipv4_item(&inner_ip_spec, &inner_ip_mask, 0x0A000001, 0x0A000002,
		       &pattern[5]);
	make_tcp_item(&inner_tcp_spec, &inner_tcp_mask, 100, 200, &pattern[6]);
	make_end_item(&pattern[7]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				       &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "validate VXLAN+TCP should succeed");

	return TEST_SUCCESS;
}

static int
test_flow_create_ipv4vxlan_ipv4_tcp(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_vxlan vxlan_spec, vxlan_mask;
	struct rte_flow_item_eth inner_eth_spec, inner_eth_mask;
	struct rte_flow_item_ipv4 inner_ip_spec, inner_ip_mask;
	struct rte_flow_item_tcp inner_tcp_spec, inner_tcp_mask;
	struct rte_flow_item pattern[9];
	struct rte_flow_action_queue q_conf;
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	struct rte_flow *flow;
	int ret;

	mock_flow_dev_setup();
	mock_prealloc_tcam_block();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_udp_any_item(&pattern[2]);
	make_vxlan_item(&vxlan_spec, &vxlan_mask, &pattern[3]);
	make_eth_item(&inner_eth_spec, &inner_eth_mask, 0, &pattern[4]);
	make_ipv4_item(&inner_ip_spec, &inner_ip_mask, 0x0A000001, 0x0A000002,
		       &pattern[5]);
	make_tcp_item(&inner_tcp_spec, &inner_tcp_mask, 100, 200, &pattern[6]);
	make_end_item(&pattern[7]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	flow = hinic3_flow_ops.create(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_NOT_NULL(flow, "create VXLAN+TCP fdir flow");

	ret = hinic3_flow_ops.destroy(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      flow, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "destroy VXLAN+TCP fdir flow");

	return TEST_SUCCESS;
}

/* ================================================================== */
/* E. RSS action tests                                                */
/* ================================================================== */

static int
test_flow_validate_ipv4_rss_normal(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_tcp tcp_spec, tcp_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action_rss rss_conf;
	uint16_t queues[2] = {0, 1};
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int ret;

	mock_flow_dev_setup();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_tcp_item(&tcp_spec, &tcp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);
	make_rss_action(&rss_conf, queues, 2, &actions[0]);
	make_end_action(&actions[1]);

	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				       &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "validate RSS normal should succeed");

	return TEST_SUCCESS;
}

static int
test_flow_validate_ipv4_rss_zero_queues(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_tcp tcp_spec, tcp_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action_rss rss_conf;
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int ret;

	mock_flow_dev_setup();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_tcp_item(&tcp_spec, &tcp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);
	make_rss_action(&rss_conf, NULL, 0, &actions[0]);
	make_end_action(&actions[1]);

	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				       &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT(ret < 0, "RSS with 0 queues should fail");

	return TEST_SUCCESS;
}

static int
test_flow_validate_ipv4_rss_invalid_queue_id(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_tcp tcp_spec, tcp_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action_rss rss_conf;
	uint16_t queues[2] = {0, 200};
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int ret;

	mock_flow_dev_setup();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_tcp_item(&tcp_spec, &tcp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);
	make_rss_action(&rss_conf, queues, 2, &actions[0]);
	make_end_action(&actions[1]);

	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				       &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT(ret < 0, "RSS with invalid queue id should fail");

	return TEST_SUCCESS;
}

static int
test_flow_create_ipv4_rss(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_tcp tcp_spec, tcp_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action_rss rss_conf;
	uint16_t queues[2] = {0, 1};
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	struct rte_flow *flow;
	struct hinic3_filter_t *rules;
	int ret;

	mock_flow_dev_setup();
	mock_prealloc_tcam_block();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_tcp_item(&tcp_spec, &tcp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);
	make_rss_action(&rss_conf, queues, 2, &actions[0]);
	make_end_action(&actions[1]);

	flow = hinic3_flow_ops.create(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_NOT_NULL(flow, "create RSS flow should succeed");

	rules = (struct hinic3_filter_t *)flow->rule;
	RTE_TEST_ASSERT_NOT_NULL(rules->template_entry,
				 "template_entry should be set");

	ret = hinic3_flow_ops.destroy(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      flow, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "destroy RSS flow");

	return TEST_SUCCESS;
}

static int
test_flow_rss_template_refcount(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_tcp tcp_spec, tcp_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action_rss rss_conf;
	uint16_t queues[2] = {0, 1};
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	struct rte_flow *flow1, *flow2;
	struct hinic3_filter_t *rules1, *rules2;
	int ret;

	mock_flow_dev_setup();
	mock_prealloc_tcam_block();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_tcp_item(&tcp_spec, &tcp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);
	make_rss_action(&rss_conf, queues, 2, &actions[0]);
	make_end_action(&actions[1]);

	flow1 = hinic3_flow_ops.create(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				       &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_NOT_NULL(flow1, "create RSS flow 1");

	rules1 = (struct hinic3_filter_t *)flow1->rule;
	RTE_TEST_ASSERT_EQUAL(rules1->template_entry->ref_count, 1,
			      "ref_count should be 1 after first create");

	/* Use different fdir pattern to avoid -EEXIST from duplicate TCAM key */
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80201, 0xC0A80202, &pattern[1]);
	flow2 = hinic3_flow_ops.create(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				       &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_NOT_NULL(flow2, "create RSS flow 2");

	rules2 = (struct hinic3_filter_t *)flow2->rule;
	RTE_TEST_ASSERT_EQUAL(rules2->template_entry, rules1->template_entry,
			      "should reuse same template_entry");
	RTE_TEST_ASSERT_EQUAL(rules2->template_entry->ref_count, 2,
			      "ref_count should be 2 after second create");

	ret = hinic3_flow_ops.destroy(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      flow1, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "destroy flow 1");

	ret = hinic3_flow_ops.destroy(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      flow2, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "destroy flow 2");

	return TEST_SUCCESS;
}

static int
test_flow_rss_key_exceed_limit(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item pattern[4];
	struct rte_flow_action_rss rss_conf;
	uint16_t queues[2] = {0, 1};
	uint8_t key[41] = {0};
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int ret;

	mock_flow_dev_setup();
	mock_setup_bifur();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_end_item(&pattern[2]);
	memset(&rss_conf, 0, sizeof(rss_conf));
	rss_conf.queue = queues;
	rss_conf.queue_num = 2;
	rss_conf.key = key;
	rss_conf.key_len = 41; /* exceeds HINIC3_RSS_KEY_SIZE (40) */
	rss_conf.types = RTE_ETH_RSS_IP;
	actions[0].type = RTE_FLOW_ACTION_TYPE_RSS;
	actions[0].conf = &rss_conf;
	make_end_action(&actions[1]);

	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				       &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT(ret < 0, "RSS key > 40 bytes should fail");

	return TEST_SUCCESS;
}

static int
test_flow_validate_ipv4_rss_qpool(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_tcp tcp_spec, tcp_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action_rss rss_conf;
	uint16_t queues[2] = {0, 1};
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int ret;

	mock_flow_dev_setup();
	mock_setup_qpool();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_tcp_item(&tcp_spec, &tcp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);
	make_rss_action(&rss_conf, queues, 2, &actions[0]);
	make_end_action(&actions[1]);

	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				       &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "validate RSS qpool should succeed");

	return TEST_SUCCESS;
}

static int
test_flow_create_ipv4_rss_qpool(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_tcp tcp_spec, tcp_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action_rss rss_conf;
	uint16_t queues[2] = {0, 1};
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	struct rte_flow *flow;
	int ret;

	mock_flow_dev_setup();
	mock_setup_qpool();
	mock_prealloc_tcam_block();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_tcp_item(&tcp_spec, &tcp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);
	make_rss_action(&rss_conf, queues, 2, &actions[0]);
	make_end_action(&actions[1]);

	flow = hinic3_flow_ops.create(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_NOT_NULL(flow, "create RSS qpool flow should succeed");

	ret = hinic3_flow_ops.destroy(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      flow, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "destroy RSS qpool flow");

	return TEST_SUCCESS;
}

static int
test_flow_create_ipv4_rss_qpool_zero_types(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_tcp tcp_spec, tcp_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action_rss rss_conf;
	uint16_t queues[2] = {0, 1};
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	struct rte_flow *flow;
	int ret;

	mock_flow_dev_setup();
	mock_setup_qpool();
	mock_prealloc_tcam_block();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_tcp_item(&tcp_spec, &tcp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);

	memset(&rss_conf, 0, sizeof(rss_conf));
	rss_conf.queue = queues;
	rss_conf.queue_num = 2;
	rss_conf.types = 0; /* zero rss types */
	actions[0].type = RTE_FLOW_ACTION_TYPE_RSS;
	actions[0].conf = &rss_conf;
	make_end_action(&actions[1]);

	flow = hinic3_flow_ops.create(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_NOT_NULL(flow, "create RSS qpool with 0 types should succeed");

	ret = hinic3_flow_ops.destroy(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      flow, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "destroy flow");

	return TEST_SUCCESS;
}

static int
test_flow_flush_qpool(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_tcp tcp_spec, tcp_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action_queue q_conf;
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	struct rte_flow *flow;
	int ret;

	mock_flow_dev_setup();
	mock_setup_qpool();
	mock_prealloc_tcam_block();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_tcp_item(&tcp_spec, &tcp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	flow = hinic3_flow_ops.create(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_NOT_NULL(flow, "create fdir flow for qpool flush");

	ret = hinic3_flow_ops.flush(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID], &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "flush in qpool mode should succeed");

	RTE_TEST_ASSERT(TAILQ_EMPTY(&g_mock_nic_dev.filter_fdir_rule_list),
		       "fdir list should be empty after flush");

	return TEST_SUCCESS;
}

static int
test_flow_validate_ipv4_rss_sp230(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_tcp tcp_spec, tcp_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action_rss rss_conf;
	uint16_t queues[2] = {0, 1};
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int ret;

	mock_flow_dev_setup();
	mock_setup_sp230();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_tcp_item(&tcp_spec, &tcp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);
	make_rss_action(&rss_conf, queues, 2, &actions[0]);
	make_end_action(&actions[1]);

	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				       &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "validate RSS SP230 should succeed");

	return TEST_SUCCESS;
}

static int
test_flow_validate_ipv4_rss_sp230_level_reject(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_tcp tcp_spec, tcp_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action_rss rss_conf;
	uint16_t queues[2] = {0, 1};
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int ret;

	mock_flow_dev_setup();
	mock_setup_sp230();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_tcp_item(&tcp_spec, &tcp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);

	memset(&rss_conf, 0, sizeof(rss_conf));
	rss_conf.queue = queues;
	rss_conf.queue_num = 2;
	rss_conf.level = 1; /* SP230 rejects level != 0 */
	rss_conf.types = RTE_ETH_RSS_IP;
	actions[0].type = RTE_FLOW_ACTION_TYPE_RSS;
	actions[0].conf = &rss_conf;
	make_end_action(&actions[1]);

	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				       &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT(ret < 0, "SP230 RSS with level!=0 should fail");

	return TEST_SUCCESS;
}

static int
test_flow_create_ipv4_rss_sp230_group(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_tcp tcp_spec, tcp_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action_rss rss_conf;
	uint16_t queues[2] = {0, 1};
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	struct rte_flow *flow;
	int ret;

	mock_flow_dev_setup();
	mock_setup_sp230();
	mock_prealloc_tcam_block();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_tcp_item(&tcp_spec, &tcp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);
	make_rss_action(&rss_conf, queues, 2, &actions[0]);
	make_end_action(&actions[1]);

	flow = hinic3_flow_ops.create(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_NOT_NULL(flow, "create SP230 RSS group flow");

	ret = hinic3_flow_ops.destroy(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      flow, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "destroy SP230 RSS group flow");

	return TEST_SUCCESS;
}

static int
test_flow_create_ipv4_rss_sp230_excessive_queues(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_tcp tcp_spec, tcp_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action_rss rss_conf;
	uint16_t queues[HINIC3_RSS_INDIR_GROUP_SIZE_PF + 1];
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int ret;
	uint16_t i;

	mock_flow_dev_setup();
	mock_setup_sp230();
	mock_prealloc_tcam_block();

	/* SP230 group RSS rejects when num_rqs exceeds HINIC3_RSS_INDIR_GROUP_SIZE_PF */
	g_mock_nic_dev.num_rqs = HINIC3_RSS_INDIR_GROUP_SIZE_PF + 1;

	for (i = 0; i < RTE_DIM(queues); i++)
		queues[i] = i;

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_tcp_item(&tcp_spec, &tcp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);

	memset(&rss_conf, 0, sizeof(rss_conf));
	rss_conf.queue = queues;
	rss_conf.queue_num = RTE_DIM(queues);
	rss_conf.types = RTE_ETH_RSS_IP;
	actions[0].type = RTE_FLOW_ACTION_TYPE_RSS;
	actions[0].conf = &rss_conf;
	make_end_action(&actions[1]);

	/* SP230 with excessive queues should fail */
	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				       &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT(ret < 0, "SP230 RSS with excessive queues should fail");

	return TEST_SUCCESS;
}

static int
test_flow_create_destroy_rss_sp230_group_cleanup(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_tcp tcp_spec, tcp_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action_rss rss_conf;
	uint16_t queues[2] = {0, 1};
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	struct rte_flow *flow;
	int ret;

	mock_flow_dev_setup();
	mock_setup_sp230();
	mock_prealloc_tcam_block();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_tcp_item(&tcp_spec, &tcp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);
	make_rss_action(&rss_conf, queues, 2, &actions[0]);
	make_end_action(&actions[1]);

	flow = hinic3_flow_ops.create(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_NOT_NULL(flow, "create SP230 RSS flow");

	ret = hinic3_flow_ops.destroy(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      flow, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "destroy SP230 RSS flow");

	RTE_TEST_ASSERT(TAILQ_EMPTY(&g_mock_nic_dev.rss_template_list),
		       "rss_template_list should be empty after cleanup");

	return TEST_SUCCESS;
}

/* ================================================================== */
/* F. Drop action tests                                               */
/* ================================================================== */

static int
test_flow_drop_normal(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_tcp tcp_spec, tcp_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	struct rte_flow *flow;
	int ret;

	mock_flow_dev_setup();
	mock_prealloc_tcam_block();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_tcp_item(&tcp_spec, &tcp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);
	make_drop_action(&actions[0]);
	make_end_action(&actions[1]);

	flow = hinic3_flow_ops.create(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_NOT_NULL(flow, "DROP on SP620 should succeed");

	ret = hinic3_flow_ops.destroy(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      flow, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "destroy drop flow");

	return TEST_SUCCESS;
}

static int
test_flow_drop_sp230_rejected(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_tcp tcp_spec, tcp_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int ret;

	mock_flow_dev_setup();
	mock_setup_sp230();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_tcp_item(&tcp_spec, &tcp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);
	make_drop_action(&actions[0]);
	make_end_action(&actions[1]);

	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				       &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT(ret < 0, "DROP on SP230 should fail");

	return TEST_SUCCESS;
}

/* ================================================================== */
/* G. Flow destroy tests                                              */
/* ================================================================== */

static int
test_flow_destroy_null(void)
{
	struct rte_flow_error error;
	int ret;

	mock_flow_dev_setup();

	ret = hinic3_flow_ops.destroy(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      NULL, &error);
	RTE_TEST_ASSERT_EQUAL(ret, -EPERM, "NULL flow should return -EPERM");

	return TEST_SUCCESS;
}

static int
test_flow_destroy_fdir(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_tcp tcp_spec, tcp_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action_queue q_conf;
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	struct rte_flow *flow;
	int ret;

	mock_flow_dev_setup();
	mock_prealloc_tcam_block();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_tcp_item(&tcp_spec, &tcp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	flow = hinic3_flow_ops.create(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_NOT_NULL(flow, "create fdir flow");

	ret = hinic3_flow_ops.destroy(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      flow, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "destroy fdir flow without template");

	RTE_TEST_ASSERT(TAILQ_EMPTY(&g_mock_nic_dev.filter_fdir_rule_list),
		       "fdir list should be empty");

	return TEST_SUCCESS;
}

static int
test_flow_destroy_rss_template(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_tcp tcp_spec, tcp_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action_rss rss_conf;
	uint16_t queues[2] = {0, 1};
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	struct rte_flow *flow;
	struct hinic3_filter_t *rules;
	int ret;

	mock_flow_dev_setup();
	mock_prealloc_tcam_block();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_tcp_item(&tcp_spec, &tcp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);
	make_rss_action(&rss_conf, queues, 2, &actions[0]);
	make_end_action(&actions[1]);

	flow = hinic3_flow_ops.create(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_NOT_NULL(flow, "create RSS flow");

	rules = (struct hinic3_filter_t *)flow->rule;
	RTE_TEST_ASSERT_EQUAL(rules->template_entry->ref_count, 1,
			      "ref_count should be 1");

	ret = hinic3_flow_ops.destroy(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      flow, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "destroy RSS template flow");

	RTE_TEST_ASSERT(TAILQ_EMPTY(&g_mock_nic_dev.rss_template_list),
		       "rss_template_list should be empty after destroy");

	return TEST_SUCCESS;
}

static int
test_flow_destroy_sp230_rss(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_tcp tcp_spec, tcp_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action_rss rss_conf;
	uint16_t queues[2] = {0, 1};
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	struct rte_flow *flow;
	int ret;

	mock_flow_dev_setup();
	mock_setup_sp230();
	mock_prealloc_tcam_block();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_tcp_item(&tcp_spec, &tcp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);
	make_rss_action(&rss_conf, queues, 2, &actions[0]);
	make_end_action(&actions[1]);

	flow = hinic3_flow_ops.create(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_NOT_NULL(flow, "create SP230 RSS flow");

	ret = hinic3_flow_ops.destroy(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      flow, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "destroy SP230 RSS flow");

	RTE_TEST_ASSERT(TAILQ_EMPTY(&g_mock_nic_dev.rss_template_list),
		       "rss_template_list should be empty");

	return TEST_SUCCESS;
}

static int
test_flow_destroy_unsupported(void)
{
	struct rte_flow_error error;
	struct rte_flow flow;
	int ret;

	mock_flow_dev_setup();

	/* Create a flow with an unsupported filter type */
	memset(&flow, 0, sizeof(flow));
	flow.filter_type = (enum rte_filter_type)999;
	flow.rule = NULL;

	ret = hinic3_flow_ops.destroy(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      &flow, &error);
	RTE_TEST_ASSERT_EQUAL(ret, -EINVAL, "unsupported filter type should fail");

	return TEST_SUCCESS;
}

/* ================================================================== */
/* H. Flow flush tests                                                */
/* ================================================================== */

static int
test_flow_flush_empty(void)
{
	struct rte_flow_error error;
	int ret;

	mock_flow_dev_setup();

	ret = hinic3_flow_ops.flush(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID], &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "flush with no flows should succeed");

	return TEST_SUCCESS;
}

static int
test_flow_flush_mixed(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item pattern_eth[3];
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_tcp tcp_spec, tcp_mask;
	struct rte_flow_item pattern_fdir[5];
	struct rte_flow_action_queue q_conf;
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	struct rte_flow *flow1, *flow2;
	int ret;

	mock_flow_dev_setup();
	mock_prealloc_tcam_block();

	/* Create ethertype flow */
	make_eth_item(&eth_spec, &eth_mask, RTE_ETHER_TYPE_ARP, &pattern_eth[0]);
	make_end_item(&pattern_eth[1]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	flow1 = hinic3_flow_ops.create(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				       &g_default_attr, pattern_eth, actions, &error);
	RTE_TEST_ASSERT_NOT_NULL(flow1, "create ethertype flow");

	/* Create fdir flow */
	make_eth_item(&eth_spec, &eth_mask, 0, &pattern_fdir[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern_fdir[1]);
	make_tcp_item(&tcp_spec, &tcp_mask, 12345, 80, &pattern_fdir[2]);
	make_end_item(&pattern_fdir[3]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	flow2 = hinic3_flow_ops.create(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				       &g_default_attr, pattern_fdir, actions, &error);
	RTE_TEST_ASSERT_NOT_NULL(flow2, "create fdir flow");

	ret = hinic3_flow_ops.flush(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID], &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "flush mixed should succeed");

	RTE_TEST_ASSERT(TAILQ_EMPTY(&g_mock_nic_dev.filter_ethertype_list),
		       "ethertype list should be empty");
	RTE_TEST_ASSERT(TAILQ_EMPTY(&g_mock_nic_dev.filter_fdir_rule_list),
		       "fdir list should be empty");

	return TEST_SUCCESS;
}

static int
test_flow_flush_fdir_filter(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_tcp tcp_spec, tcp_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action_queue q_conf;
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	struct rte_flow *flow1, *flow2;
	int ret;

	mock_flow_dev_setup();
	mock_prealloc_tcam_block();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_tcp_item(&tcp_spec, &tcp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	flow1 = hinic3_flow_ops.create(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				       &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_NOT_NULL(flow1, "create fdir flow 1");

	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80201, 0xC0A80202, &pattern[1]);
	flow2 = hinic3_flow_ops.create(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				       &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_NOT_NULL(flow2, "create fdir flow 2");

	ret = hinic3_flow_ops.flush(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID], &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "flush fdir should succeed");

	RTE_TEST_ASSERT(TAILQ_EMPTY(&g_mock_nic_dev.filter_fdir_rule_list),
		       "fdir list should be empty");

	return TEST_SUCCESS;
}

/* ================================================================== */
/* I. Flow query tests                                                */
/* ================================================================== */

static int
test_flow_query_null_flow(void)
{
	struct rte_flow_query_count data;
	struct rte_flow_error error;
	int ret;

	mock_flow_dev_setup();

	memset(&data, 0, sizeof(data));
	ret = hinic3_flow_ops.query(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				    NULL, NULL, &data, &error);
	RTE_TEST_ASSERT_EQUAL(ret, -EPERM, "NULL flow should return -EPERM");

	return TEST_SUCCESS;
}

static int
test_flow_query_null_data(void)
{
	struct rte_flow_error error;
	struct rte_flow flow;
	int ret;

	mock_flow_dev_setup();

	memset(&flow, 0, sizeof(flow));
	ret = hinic3_flow_ops.query(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				    &flow, NULL, NULL, &error);
	RTE_TEST_ASSERT_EQUAL(ret, -EPERM, "NULL data should return -EPERM");

	return TEST_SUCCESS;
}

static int
test_flow_query_fdir_filter(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_tcp tcp_spec, tcp_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action_queue q_conf;
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	struct rte_flow_query_count data;
	struct rte_flow *flow;
	int ret;

	mock_flow_dev_setup();
	mock_prealloc_tcam_block();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_tcp_item(&tcp_spec, &tcp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	flow = hinic3_flow_ops.create(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_NOT_NULL(flow, "create fdir flow for query");

	memset(&data, 0, sizeof(data));
	ret = hinic3_flow_ops.query(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				    flow, NULL, &data, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "query fdir flow should succeed");

	ret = hinic3_flow_ops.destroy(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      flow, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "destroy queried flow");

	return TEST_SUCCESS;
}

static int
test_flow_query_fdir_filter_notexist(void)
{
	struct rte_flow_error error;
	struct rte_flow_query_count data;
	struct rte_flow flow;
	struct hinic3_filter_t rules;
	int ret;

	mock_flow_dev_setup();
	g_mock_flow_query_fdir_ret = -EINVAL;

	memset(&flow, 0, sizeof(flow));
	memset(&rules, 0, sizeof(rules));
	flow.filter_type = RTE_ETH_FILTER_FDIR;
	flow.rule = &rules;

	memset(&data, 0, sizeof(data));
	ret = hinic3_flow_ops.query(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				    &flow, NULL, &data, &error);
	RTE_TEST_ASSERT_EQUAL(ret, -EINVAL,
			      "query non-existent filter should return -EINVAL");

	return TEST_SUCCESS;
}

/* ================================================================== */
/* J. Pattern matching tests                                          */
/* ================================================================== */

static int
test_flow_pattern_ipv4_tcp(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_tcp tcp_spec, tcp_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action_queue q_conf;
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int ret;

	mock_flow_dev_setup();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_tcp_item(&tcp_spec, &tcp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				       &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "ETH+IPV4+TCP pattern should succeed");

	return TEST_SUCCESS;
}

static int
test_flow_pattern_ipv4_udp(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_udp udp_spec, udp_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action_queue q_conf;
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int ret;

	mock_flow_dev_setup();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_udp_item(&udp_spec, &udp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				       &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "ETH+IPV4+UDP pattern should succeed");

	return TEST_SUCCESS;
}

static int
test_flow_pattern_ipv6_tcp(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv6 ip_spec, ip_mask;
	struct rte_flow_item_tcp tcp_spec, tcp_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action_queue q_conf;
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int ret;

	mock_flow_dev_setup();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv6_item(&ip_spec, &ip_mask, &pattern[1]);
	make_tcp_item(&tcp_spec, &tcp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				       &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "ETH+IPV6+TCP pattern should succeed");

	return TEST_SUCCESS;
}

static int
test_flow_pattern_ipv4_vxlan_ipv4(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_vxlan vxlan_spec, vxlan_mask;
	struct rte_flow_item_eth inner_eth_spec, inner_eth_mask;
	struct rte_flow_item_ipv4 inner_ip_spec, inner_ip_mask;
	struct rte_flow_item pattern[8];
	struct rte_flow_action_queue q_conf;
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int ret;

	mock_flow_dev_setup();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_udp_any_item(&pattern[2]);
	make_vxlan_item(&vxlan_spec, &vxlan_mask, &pattern[3]);
	make_eth_item(&inner_eth_spec, &inner_eth_mask, 0, &pattern[4]);
	make_ipv4_item(&inner_ip_spec, &inner_ip_mask, 0x0A000001, 0x0A000002,
		       &pattern[5]);
	make_end_item(&pattern[6]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				       &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "VXLAN tunnel pattern should succeed");

	return TEST_SUCCESS;
}

static int
test_flow_pattern_ipv4_geneve(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_geneve geneve_spec, geneve_mask;
	struct rte_flow_item pattern[6];
	struct rte_flow_action_queue q_conf;
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int ret;

	mock_flow_dev_setup();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_udp_any_item(&pattern[2]);
	make_geneve_item(&geneve_spec, &geneve_mask, &pattern[3]);
	make_end_item(&pattern[4]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				       &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "GENEVE pattern should succeed");

	return TEST_SUCCESS;
}

static int
test_flow_pattern_unsupported(void)
{
	struct rte_flow_item pattern[3];
	struct rte_flow_action_queue q_conf;
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int ret;

	mock_flow_dev_setup();

	/* GRE is not supported */
	pattern[0].type = RTE_FLOW_ITEM_TYPE_GRE;
	pattern[0].spec = NULL;
	pattern[0].mask = NULL;
	pattern[0].last = NULL;
	make_end_item(&pattern[1]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				       &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT(ret < 0, "unsupported pattern should fail");

	return TEST_SUCCESS;
}

static int
test_flow_pattern_too_many(void)
{
	struct rte_flow_item pattern[HINIC3_FLOW_MAX_PATTERN_NUM + 2];
	struct rte_flow_action_queue q_conf;
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int i;
	int ret;

	mock_flow_dev_setup();

	for (i = 0; i < HINIC3_FLOW_MAX_PATTERN_NUM + 1; i++) {
		pattern[i].type = RTE_FLOW_ITEM_TYPE_VOID;
		pattern[i].spec = NULL;
		pattern[i].mask = NULL;
		pattern[i].last = NULL;
	}
	make_end_item(&pattern[HINIC3_FLOW_MAX_PATTERN_NUM + 1]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				       &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT(ret < 0, "too many patterns should fail");

	return TEST_SUCCESS;
}

static int
test_flow_pattern_eth_arp(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item pattern[3];
	struct rte_flow_action_queue q_conf;
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int ret;

	mock_flow_dev_setup();

	make_eth_item(&eth_spec, &eth_mask, RTE_ETHER_TYPE_ARP, &pattern[0]);
	make_end_item(&pattern[1]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				       &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "ETH+ARP ethertype pattern should succeed");

	return TEST_SUCCESS;
}

static int
test_flow_pattern_ipv4_sctp(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item pattern[4];
	struct rte_flow_action_queue q_conf;
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int ret;

	mock_flow_dev_setup();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	pattern[2].type = RTE_FLOW_ITEM_TYPE_SCTP;
	pattern[2].spec = NULL;
	pattern[2].mask = NULL;
	pattern[2].last = NULL;
	make_end_item(&pattern[3]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	/* SCTP is not in supported patterns, should fail */
	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				       &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT(ret < 0, "ETH+IPV4+SCTP pattern should fail (unsupported)");

	return TEST_SUCCESS;
}

static int
test_flow_pattern_ipv6_udp(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv6 ip_spec, ip_mask;
	struct rte_flow_item_udp udp_spec, udp_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action_queue q_conf;
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int ret;

	mock_flow_dev_setup();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv6_item(&ip_spec, &ip_mask, &pattern[1]);
	make_udp_item(&udp_spec, &udp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				       &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "ETH+IPV6+UDP pattern should succeed");

	return TEST_SUCCESS;
}

static int
test_flow_pattern_ipv4_ipinip(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_ipv4 inner_ip_spec, inner_ip_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action_queue q_conf;
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int ret;

	mock_flow_dev_setup();

	/* IPinIP: ETH + IPV4 + IPV4 + END (SP620 only) */
	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_ipv4_item(&inner_ip_spec, &inner_ip_mask, 0x0A000001, 0x0A000002,
		       &pattern[2]);
	make_end_item(&pattern[3]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				       &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "IPinIP pattern on SP620 should succeed");

	return TEST_SUCCESS;
}

static int
test_flow_pattern_ipv4vxlan_ipv6(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_vxlan vxlan_spec, vxlan_mask;
	struct rte_flow_item_eth inner_eth_spec, inner_eth_mask;
	struct rte_flow_item_ipv6 inner_ip_spec, inner_ip_mask;
	struct rte_flow_item pattern[8];
	struct rte_flow_action_queue q_conf;
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int ret;

	mock_flow_dev_setup();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_udp_any_item(&pattern[2]);
	make_vxlan_item(&vxlan_spec, &vxlan_mask, &pattern[3]);
	make_eth_item(&inner_eth_spec, &inner_eth_mask, 0, &pattern[4]);
	make_ipv6_item(&inner_ip_spec, &inner_ip_mask, &pattern[5]);
	make_end_item(&pattern[6]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				       &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "VXLAN with IPv6 inner should succeed");

	return TEST_SUCCESS;
}

static int
test_flow_pattern_ipv4vxlan_ipv4_udp(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_vxlan vxlan_spec, vxlan_mask;
	struct rte_flow_item_eth inner_eth_spec, inner_eth_mask;
	struct rte_flow_item_ipv4 inner_ip_spec, inner_ip_mask;
	struct rte_flow_item_udp inner_udp_spec, inner_udp_mask;
	struct rte_flow_item pattern[9];
	struct rte_flow_action_queue q_conf;
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int ret;

	mock_flow_dev_setup();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_udp_any_item(&pattern[2]);
	make_vxlan_item(&vxlan_spec, &vxlan_mask, &pattern[3]);
	make_eth_item(&inner_eth_spec, &inner_eth_mask, 0, &pattern[4]);
	make_ipv4_item(&inner_ip_spec, &inner_ip_mask, 0x0A000001, 0x0A000002,
		       &pattern[5]);
	make_udp_item(&inner_udp_spec, &inner_udp_mask, 100, 200, &pattern[6]);
	make_end_item(&pattern[7]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				       &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "VXLAN with UDP should succeed");

	return TEST_SUCCESS;
}

static int
test_flow_pattern_geneve_ipv4_tcp(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_geneve geneve_spec, geneve_mask;
	struct rte_flow_item_eth inner_eth_spec, inner_eth_mask;
	struct rte_flow_item_ipv4 inner_ip_spec, inner_ip_mask;
	struct rte_flow_item_tcp inner_tcp_spec, inner_tcp_mask;
	struct rte_flow_item pattern[9];
	struct rte_flow_action_queue q_conf;
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int ret;

	mock_flow_dev_setup();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_udp_any_item(&pattern[2]);
	make_geneve_item(&geneve_spec, &geneve_mask, &pattern[3]);
	make_eth_item(&inner_eth_spec, &inner_eth_mask, 0, &pattern[4]);
	make_ipv4_item(&inner_ip_spec, &inner_ip_mask, 0x0A000001, 0x0A000002,
		       &pattern[5]);
	make_tcp_item(&inner_tcp_spec, &inner_tcp_mask, 100, 200, &pattern[6]);
	make_end_item(&pattern[7]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				       &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "GENEVE with TCP should succeed");

	return TEST_SUCCESS;
}

static int
test_flow_pattern_vlan_ipv4(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action_queue q_conf;
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int ret;

	mock_flow_dev_setup();

	/* ETH + VLAN + IPV4 is not in supported patterns (no VLAN pattern),
	 * should fail with unsupported pattern */
	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	pattern[1].type = RTE_FLOW_ITEM_TYPE_VLAN;
	pattern[1].spec = NULL;
	pattern[1].mask = NULL;
	pattern[1].last = NULL;
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[2]);
	make_end_item(&pattern[3]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				       &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT(ret < 0, "VLAN+IPV4 pattern should fail (unsupported)");

	return TEST_SUCCESS;
}

/* ================================================================== */
/* K. RSS template management tests                                    */
/* ================================================================== */

static int
test_rss_template_alloc_fail(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_tcp tcp_spec, tcp_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action_rss rss_conf;
	uint16_t queues[2] = {0, 1};
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int ret;

	mock_flow_dev_setup();
	mock_prealloc_tcam_block();
	g_mock_rss_template_alloc_ret = -1;

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_tcp_item(&tcp_spec, &tcp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);
	make_rss_action(&rss_conf, queues, 2, &actions[0]);
	make_end_action(&actions[1]);

	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				       &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT(ret < 0, "RSS template alloc failure should fail validate");

	return TEST_SUCCESS;
}

static int
test_rss_template_qgrp_id_fail(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_tcp tcp_spec, tcp_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action_rss rss_conf;
	uint16_t queues[2] = {0, 1};
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int ret;

	mock_flow_dev_setup();
	mock_prealloc_tcam_block();
	g_mock_mgmt_cfg_qgrp_id_ret = -1;

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_tcp_item(&tcp_spec, &tcp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);
	make_rss_action(&rss_conf, queues, 2, &actions[0]);
	make_end_action(&actions[1]);

	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				       &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT(ret < 0, "qgrp_id alloc failure should fail validate");

	return TEST_SUCCESS;
}

static int
test_rss_template_queue_type_fail(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_tcp tcp_spec, tcp_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action_rss rss_conf;
	uint16_t queues[2] = {0, 1};
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int ret;

	mock_flow_dev_setup();
	mock_prealloc_tcam_block();
	g_mock_cmdq_set_rss_queue_type_ret = -1;

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_tcp_item(&tcp_spec, &tcp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);
	make_rss_action(&rss_conf, queues, 2, &actions[0]);
	make_end_action(&actions[1]);

	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				       &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT(ret < 0, "set queue type failure should fail validate");

	return TEST_SUCCESS;
}

static int
test_rss_template_indir_tbl_fail(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_tcp tcp_spec, tcp_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action_rss rss_conf;
	uint16_t queues[2] = {0, 1};
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	struct rte_flow *flow;

	mock_flow_dev_setup();
	mock_prealloc_tcam_block();
	g_mock_rss_queue_set_indir_tbl_ret = -1;

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_tcp_item(&tcp_spec, &tcp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);
	make_rss_action(&rss_conf, queues, 2, &actions[0]);
	make_end_action(&actions[1]);

	flow = hinic3_flow_ops.create(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_NULL(flow, "RSS flow with indir tbl failure should fail to create");

	return TEST_SUCCESS;
}

static int
test_rss_template_free_on_destroy(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_tcp tcp_spec, tcp_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action_rss rss_conf;
	uint16_t queues[2] = {0, 1};
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	struct rte_flow *flow;
	int ret;

	mock_flow_dev_setup();
	mock_prealloc_tcam_block();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_tcp_item(&tcp_spec, &tcp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);
	make_rss_action(&rss_conf, queues, 2, &actions[0]);
	make_end_action(&actions[1]);

	flow = hinic3_flow_ops.create(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_NOT_NULL(flow, "create RSS flow");

	ret = hinic3_flow_ops.destroy(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      flow, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "destroy RSS flow");

	RTE_TEST_ASSERT(TAILQ_EMPTY(&g_mock_nic_dev.rss_template_list),
		       "template should be freed when ref_count reaches 0");

	return TEST_SUCCESS;
}

static int
test_rss_template_reuse_different_queues(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_tcp tcp_spec, tcp_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action_rss rss_conf;
	uint16_t queues1[2] = {0, 1};
	uint16_t queues2[2] = {2, 3};
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	struct rte_flow *flow1, *flow2;
	struct hinic3_filter_t *rules1, *rules2;
	int ret;

	mock_flow_dev_setup();
	mock_prealloc_tcam_block();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_tcp_item(&tcp_spec, &tcp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);

	/* First flow with queues {0, 1} */
	make_rss_action(&rss_conf, queues1, 2, &actions[0]);
	make_end_action(&actions[1]);
	flow1 = hinic3_flow_ops.create(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				       &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_NOT_NULL(flow1, "create RSS flow 1");
	rules1 = (struct hinic3_filter_t *)flow1->rule;

	/* Second flow with different queues {2, 3} */
	make_rss_action(&rss_conf, queues2, 2, &actions[0]);
	make_end_action(&actions[1]);
	/* Use different fdir pattern to avoid -EEXIST from duplicate TCAM key */
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80201, 0xC0A80202, &pattern[1]);
	flow2 = hinic3_flow_ops.create(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				       &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_NOT_NULL(flow2, "create RSS flow 2");
	rules2 = (struct hinic3_filter_t *)flow2->rule;

	RTE_TEST_ASSERT_NOT_EQUAL(rules2->template_entry, rules1->template_entry,
				 "different queues should create new template");

	ret = hinic3_flow_ops.destroy(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      flow1, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "destroy flow 1");

	ret = hinic3_flow_ops.destroy(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      flow2, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "destroy flow 2");

	return TEST_SUCCESS;
}

static int
test_rss_template_max_exceeded(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_tcp tcp_spec, tcp_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action_rss rss_conf;
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	struct rte_flow *flows[FUNC_MAX_DPDK_NUM + 1];
	uint16_t queues[2];
	int i;
	int ret;

	mock_flow_dev_setup();
	mock_prealloc_tcam_block();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_end_item(&pattern[3]);

	/* Create FUNC_MAX_DPDK_NUM flows with different queue configs */
	for (i = 0; i < FUNC_MAX_DPDK_NUM; i++) {
		queues[0] = (uint16_t)(i * 2);
		queues[1] = (uint16_t)(i * 2 + 1);
		make_ipv4_item(&ip_spec, &ip_mask,
			       0xC0A80101 + i, 0xC0A80102, &pattern[1]);
		make_tcp_item(&tcp_spec, &tcp_mask, 12345 + i, 80, &pattern[2]);
		make_rss_action(&rss_conf, queues, 2, &actions[0]);
		make_end_action(&actions[1]);
		flows[i] = hinic3_flow_ops.create(
			&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
			&g_default_attr, pattern, actions, &error);
		if (flows[i] == NULL)
			break;
	}
	RTE_TEST_ASSERT_EQUAL(i, FUNC_MAX_DPDK_NUM,
			      "should create exactly FUNC_MAX_DPDK_NUM templates");

	/* The next one should fail */
	queues[0] = (uint16_t)(FUNC_MAX_DPDK_NUM * 2);
	queues[1] = (uint16_t)(FUNC_MAX_DPDK_NUM * 2 + 1);
	make_ipv4_item(&ip_spec, &ip_mask,
		       0xC0A80101 + FUNC_MAX_DPDK_NUM, 0xC0A80102, &pattern[1]);
	make_tcp_item(&tcp_spec, &tcp_mask,
		      12345 + FUNC_MAX_DPDK_NUM, 80, &pattern[2]);
	make_rss_action(&rss_conf, queues, 2, &actions[0]);
	make_end_action(&actions[1]);
	flows[FUNC_MAX_DPDK_NUM] = hinic3_flow_ops.create(
		&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
		&g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_NULL(flows[FUNC_MAX_DPDK_NUM],
			     "exceeding max templates should fail");

	/* Cleanup */
	for (i = 0; i < FUNC_MAX_DPDK_NUM; i++) {
		if (flows[i]) {
			ret = hinic3_flow_ops.destroy(
				&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				flows[i], &error);
			RTE_TEST_ASSERT_EQUAL(ret, 0, "cleanup destroy");
		}
	}

	return TEST_SUCCESS;
}

static int
test_rss_template_bifur_mode(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_tcp tcp_spec, tcp_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action_rss rss_conf;
	uint16_t queues[2] = {0, 1};
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int ret;

	mock_flow_dev_setup();
	mock_setup_bifur();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_tcp_item(&tcp_spec, &tcp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);
	make_rss_action(&rss_conf, queues, 2, &actions[0]);
	make_end_action(&actions[1]);

	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				       &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "RSS in BIFUR mode should succeed");

	return TEST_SUCCESS;
}

static int
test_rss_template_normal_with_bifur_en(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_tcp tcp_spec, tcp_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action_rss rss_conf;
	uint16_t queues[2] = {0, 1};
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int ret;

	mock_flow_dev_setup();
	g_mock_bifur_en = 1;
	g_mock_bifur_type = 2;

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_tcp_item(&tcp_spec, &tcp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);
	make_rss_action(&rss_conf, queues, 2, &actions[0]);
	make_end_action(&actions[1]);

	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				       &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "RSS normal with bifur_en should succeed");

	return TEST_SUCCESS;
}

static int
test_rss_template_qpool_sp230(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_tcp tcp_spec, tcp_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action_rss rss_conf;
	uint16_t queues[2] = {0, 1};
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int ret;

	mock_flow_dev_setup();
	mock_setup_sp230();
	mock_setup_qpool();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_tcp_item(&tcp_spec, &tcp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);
	make_rss_action(&rss_conf, queues, 2, &actions[0]);
	make_end_action(&actions[1]);

	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				       &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "RSS QPOOL+SP230 should succeed");

	return TEST_SUCCESS;
}

static int
test_rss_template_destroy_sp230(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_tcp tcp_spec, tcp_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action_rss rss_conf;
	uint16_t queues[2] = {0, 1};
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	struct rte_flow *flow;
	int ret;

	mock_flow_dev_setup();
	mock_setup_sp230();
	mock_prealloc_tcam_block();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_tcp_item(&tcp_spec, &tcp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);
	make_rss_action(&rss_conf, queues, 2, &actions[0]);
	make_end_action(&actions[1]);

	flow = hinic3_flow_ops.create(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_NOT_NULL(flow, "create SP230 RSS template flow");

	ret = hinic3_flow_ops.destroy(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      flow, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "destroy SP230 RSS template");

	RTE_TEST_ASSERT(TAILQ_EMPTY(&g_mock_nic_dev.rss_template_list),
		       "rss_template_list should be empty");

	return TEST_SUCCESS;
}

static int
test_rss_template_destroy_qpool(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_tcp tcp_spec, tcp_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action_rss rss_conf;
	uint16_t queues[2] = {0, 1};
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	struct rte_flow *flow;
	int ret;

	mock_flow_dev_setup();
	mock_setup_qpool();
	mock_prealloc_tcam_block();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_tcp_item(&tcp_spec, &tcp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);
	make_rss_action(&rss_conf, queues, 2, &actions[0]);
	make_end_action(&actions[1]);

	flow = hinic3_flow_ops.create(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_NOT_NULL(flow, "create QPOOL RSS template flow");

	ret = hinic3_flow_ops.destroy(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      flow, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "destroy QPOOL RSS template");

	return TEST_SUCCESS;
}

static int
test_rss_template_rss_key_valid(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_tcp tcp_spec, tcp_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action_rss rss_conf;
	uint16_t queues[2] = {0, 1};
	uint8_t key[HINIC3_RSS_KEY_SIZE] = {0};
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int ret;

	mock_flow_dev_setup();
	mock_setup_bifur();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_tcp_item(&tcp_spec, &tcp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);

	memset(&rss_conf, 0, sizeof(rss_conf));
	rss_conf.queue = queues;
	rss_conf.queue_num = 2;
	rss_conf.key = key;
	rss_conf.key_len = HINIC3_RSS_KEY_SIZE; /* 40 bytes, valid */
	rss_conf.types = RTE_ETH_RSS_IP;
	actions[0].type = RTE_FLOW_ACTION_TYPE_RSS;
	actions[0].conf = &rss_conf;
	make_end_action(&actions[1]);

	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				       &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "RSS with valid 40-byte key should succeed");

	return TEST_SUCCESS;
}

static int
test_rss_template_rss_key_short(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_tcp tcp_spec, tcp_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action_rss rss_conf;
	uint16_t queues[2] = {0, 1};
	uint8_t key[20] = {0};
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int ret;

	mock_flow_dev_setup();
	mock_setup_bifur();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_tcp_item(&tcp_spec, &tcp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);

	memset(&rss_conf, 0, sizeof(rss_conf));
	rss_conf.queue = queues;
	rss_conf.queue_num = 2;
	rss_conf.key = key;
	rss_conf.key_len = 20; /* short key, should still succeed */
	rss_conf.types = RTE_ETH_RSS_IP;
	actions[0].type = RTE_FLOW_ACTION_TYPE_RSS;
	actions[0].conf = &rss_conf;
	make_end_action(&actions[1]);

	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				       &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "RSS with short key should succeed");

	return TEST_SUCCESS;
}

static int
test_rss_template_create_destroy_multiple(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_tcp tcp_spec, tcp_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action_rss rss_conf;
	uint16_t queues[2];
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	struct rte_flow *flows[3];
	int i;
	int ret;

	mock_flow_dev_setup();
	mock_prealloc_tcam_block();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_end_item(&pattern[3]);

	for (i = 0; i < 3; i++) {
		queues[0] = (uint16_t)(i * 2);
		queues[1] = (uint16_t)(i * 2 + 1);
		make_ipv4_item(&ip_spec, &ip_mask,
			       0xC0A80101 + i, 0xC0A80102, &pattern[1]);
		make_tcp_item(&tcp_spec, &tcp_mask, 12345 + i, 80, &pattern[2]);
		make_rss_action(&rss_conf, queues, 2, &actions[0]);
		make_end_action(&actions[1]);
		flows[i] = hinic3_flow_ops.create(
			&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
			&g_default_attr, pattern, actions, &error);
		RTE_TEST_ASSERT_NOT_NULL(flows[i], "create RSS flow");
	}

	for (i = 0; i < 3; i++) {
		ret = hinic3_flow_ops.destroy(
			&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
			flows[i], &error);
		RTE_TEST_ASSERT_EQUAL(ret, 0, "destroy RSS flow");
	}

	RTE_TEST_ASSERT(TAILQ_EMPTY(&g_mock_nic_dev.rss_template_list),
		       "rss_template_list should be empty");

	return TEST_SUCCESS;
}

static int
test_rss_template_refcount_decrement(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_tcp tcp_spec, tcp_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action_rss rss_conf;
	uint16_t queues[2] = {0, 1};
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	struct rte_flow *flow1, *flow2;
	struct hinic3_filter_t *rules1, *rules2;
	int ret;

	mock_flow_dev_setup();
	mock_prealloc_tcam_block();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_tcp_item(&tcp_spec, &tcp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);
	make_rss_action(&rss_conf, queues, 2, &actions[0]);
	make_end_action(&actions[1]);

	flow1 = hinic3_flow_ops.create(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				       &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_NOT_NULL(flow1, "create RSS flow 1");
	rules1 = (struct hinic3_filter_t *)flow1->rule;

	/* Use different fdir pattern to avoid -EEXIST from duplicate TCAM key */
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80201, 0xC0A80202, &pattern[1]);
	flow2 = hinic3_flow_ops.create(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				       &g_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_NOT_NULL(flow2, "create RSS flow 2");
	rules2 = (struct hinic3_filter_t *)flow2->rule;

	RTE_TEST_ASSERT_EQUAL(rules2->template_entry, rules1->template_entry,
			      "should share template");
	RTE_TEST_ASSERT_EQUAL(rules1->template_entry->ref_count, 2,
			      "ref_count should be 2");

	/* Destroy first flow: ref_count should decrement */
	ret = hinic3_flow_ops.destroy(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      flow1, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "destroy flow 1");
	RTE_TEST_ASSERT_EQUAL(rules2->template_entry->ref_count, 1,
			      "ref_count should be 1 after destroy");

	/* Template should still be in list */
	RTE_TEST_ASSERT(!TAILQ_EMPTY(&g_mock_nic_dev.rss_template_list),
		       "template should still exist");

	/* Destroy second flow: template should be freed */
	ret = hinic3_flow_ops.destroy(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      flow2, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "destroy flow 2");
	RTE_TEST_ASSERT(TAILQ_EMPTY(&g_mock_nic_dev.rss_template_list),
		       "template should be freed after ref_count reaches 0");

	return TEST_SUCCESS;
}

/* ================================================================== */
/* Test suite definition                                              */
/* ================================================================== */

static struct unit_test_suite hinic3_flow_test_suite = {
	.suite_name = "HINIC3 Flow API Unit Tests",
	.setup = flow_test_setup,
	.teardown = flow_test_teardown,
	.unit_test_cases = {
		/* A. Flow attr validation tests */
		TEST_CASE(test_flow_attr_egress),
		TEST_CASE(test_flow_attr_priority),
		TEST_CASE(test_flow_attr_group),
		TEST_CASE(test_flow_attr_no_ingress),

		/* B. Flow validate null parameter tests */
		TEST_CASE(test_flow_validate_null_pattern),
		TEST_CASE(test_flow_validate_null_actions),
		TEST_CASE(test_flow_validate_null_attr),

		/* C. Ethertype flow tests */
		TEST_CASE(test_flow_validate_ethertype),
		TEST_CASE(test_flow_create_ethertype),
		TEST_CASE(test_flow_destroy_ethertype),
		TEST_CASE(test_flow_flush_ethertype),

		/* D. IPv4 flow tests */
		TEST_CASE(test_flow_validate_ipv4_vlan),
		TEST_CASE(test_flow_create_ipv4_vlan),
		TEST_CASE(test_flow_validate_ipv4vxlan_ipv4),
		TEST_CASE(test_flow_create_ipv4vxlan_ipv4),
		TEST_CASE(test_flow_validate_ipv4vxlan_ipv4_tcp),
		TEST_CASE(test_flow_create_ipv4vxlan_ipv4_tcp),

		/* E. RSS action tests */
		TEST_CASE(test_flow_validate_ipv4_rss_normal),
		TEST_CASE(test_flow_validate_ipv4_rss_zero_queues),
		TEST_CASE(test_flow_validate_ipv4_rss_invalid_queue_id),
		TEST_CASE(test_flow_create_ipv4_rss),
		TEST_CASE(test_flow_rss_template_refcount),
		TEST_CASE(test_flow_rss_key_exceed_limit),
		TEST_CASE(test_flow_validate_ipv4_rss_qpool),
		TEST_CASE(test_flow_create_ipv4_rss_qpool),
		TEST_CASE(test_flow_create_ipv4_rss_qpool_zero_types),
		TEST_CASE(test_flow_flush_qpool),
		TEST_CASE(test_flow_validate_ipv4_rss_sp230),
		TEST_CASE(test_flow_validate_ipv4_rss_sp230_level_reject),
		TEST_CASE(test_flow_create_ipv4_rss_sp230_group),
		TEST_CASE(test_flow_create_ipv4_rss_sp230_excessive_queues),
		TEST_CASE(test_flow_create_destroy_rss_sp230_group_cleanup),

		/* F. Drop action tests */
		TEST_CASE(test_flow_drop_normal),
		TEST_CASE(test_flow_drop_sp230_rejected),

		/* G. Flow destroy tests */
		TEST_CASE(test_flow_destroy_null),
		TEST_CASE(test_flow_destroy_fdir),
		TEST_CASE(test_flow_destroy_rss_template),
		TEST_CASE(test_flow_destroy_sp230_rss),
		TEST_CASE(test_flow_destroy_unsupported),

		/* H. Flow flush tests */
		TEST_CASE(test_flow_flush_empty),
		TEST_CASE(test_flow_flush_mixed),
		TEST_CASE(test_flow_flush_fdir_filter),

		/* I. Flow query tests */
		TEST_CASE(test_flow_query_null_flow),
		TEST_CASE(test_flow_query_null_data),
		TEST_CASE(test_flow_query_fdir_filter),
		TEST_CASE(test_flow_query_fdir_filter_notexist),

		/* J. Pattern matching tests */
		TEST_CASE(test_flow_pattern_ipv4_tcp),
		TEST_CASE(test_flow_pattern_ipv4_udp),
		TEST_CASE(test_flow_pattern_ipv6_tcp),
		TEST_CASE(test_flow_pattern_ipv4_vxlan_ipv4),
		TEST_CASE(test_flow_pattern_ipv4_geneve),
		TEST_CASE(test_flow_pattern_unsupported),
		TEST_CASE(test_flow_pattern_too_many),
		TEST_CASE(test_flow_pattern_eth_arp),
		TEST_CASE(test_flow_pattern_ipv4_sctp),
		TEST_CASE(test_flow_pattern_ipv6_udp),
		TEST_CASE(test_flow_pattern_ipv4_ipinip),
		TEST_CASE(test_flow_pattern_ipv4vxlan_ipv6),
		TEST_CASE(test_flow_pattern_ipv4vxlan_ipv4_udp),
		TEST_CASE(test_flow_pattern_geneve_ipv4_tcp),
		TEST_CASE(test_flow_pattern_vlan_ipv4),

		/* K. RSS template management tests */
		TEST_CASE(test_rss_template_alloc_fail),
		TEST_CASE(test_rss_template_qgrp_id_fail),
		TEST_CASE(test_rss_template_queue_type_fail),
		TEST_CASE(test_rss_template_indir_tbl_fail),
		TEST_CASE(test_rss_template_free_on_destroy),
		TEST_CASE(test_rss_template_reuse_different_queues),
		TEST_CASE(test_rss_template_max_exceeded),
		TEST_CASE(test_rss_template_bifur_mode),
		TEST_CASE(test_rss_template_normal_with_bifur_en),
		TEST_CASE(test_rss_template_qpool_sp230),
		TEST_CASE(test_rss_template_destroy_sp230),
		TEST_CASE(test_rss_template_destroy_qpool),
		TEST_CASE(test_rss_template_rss_key_valid),
		TEST_CASE(test_rss_template_rss_key_short),
		TEST_CASE(test_rss_template_create_destroy_multiple),
		TEST_CASE(test_rss_template_refcount_decrement),

		TEST_CASES_END()
	}
};

static int
test_hinic3_flow(void)
{
	return unit_test_suite_runner(&hinic3_flow_test_suite);
}

REGISTER_TEST_COMMAND(hinic3_flow_autotest, test_hinic3_flow);
