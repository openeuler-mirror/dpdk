/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2025-2026 Huawei Technologies Co., Ltd
 * Unit tests for hinic3 secondary TCAM flow API (hinic3_pmd_flow_sec.c)
 *
 * Tests cover:
 * - Pattern validation via hinic3_flow_ops.validate with sec_tcam enabled
 * - Flow create/destroy via hinic3_flow_ops.create/destroy
 * - Tunnel flow (VXLAN, GENEVE) create/validate
 * - Error paths (add fail, block alloc fail, set filter fail, duplicate)
 * - Flush and dynamic block management
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
#ifdef DPDK_20_11
#include <rte_geneve.h>
#endif
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
#include "hinic3_pmd_flow_sec.h"
#include "hinic3_pmd_rx.h"
#include "test_hinic3_mock.h"

#ifndef DPDK_21_11
extern struct rte_eth_dev rte_eth_devices[];
#endif

extern const struct rte_flow_ops hinic3_flow_ops;

#define MOCK_ETH_DEV_PORT_ID 0

/* ====== Mock device structures ====== */

static struct rte_pci_device g_sec_mock_pci_dev;
static struct hinic3_hwif g_sec_mock_hwif;
static struct cfg_mgmt_info g_sec_mock_cfg_mgmt;
static struct hinic3_hwdev g_sec_mock_hwdev;
static struct hinic3_nic_dev g_sec_mock_nic_dev;
static struct rte_eth_dev_data g_sec_mock_eth_dev_data;
static void *g_sec_mock_rx_queues[128];
static void *g_sec_mock_tx_queues[128];
static struct hinic3_rxq g_sec_mock_rxq;

/* ====== Mock device setup ====== */

static void
flow_sec_dev_setup(void)
{
	memset(&g_sec_mock_pci_dev, 0, sizeof(g_sec_mock_pci_dev));
	g_sec_mock_pci_dev.id.device_id = HINIC3_DEV_ID_SP620;

	memset(&g_sec_mock_hwif, 0, sizeof(g_sec_mock_hwif));
	g_sec_mock_hwif.attr.func_global_idx = 0x123;

	memset(&g_sec_mock_cfg_mgmt, 0, sizeof(g_sec_mock_cfg_mgmt));
	g_sec_mock_cfg_mgmt.svc_cap.er_id = 1;

	memset(&g_sec_mock_hwdev, 0, sizeof(g_sec_mock_hwdev));
	g_sec_mock_hwdev.hwif = &g_sec_mock_hwif;
	g_sec_mock_hwdev.cfg_mgmt = &g_sec_mock_cfg_mgmt;
	g_sec_mock_hwdev.dev_handle = &g_sec_mock_nic_dev;
	g_sec_mock_hwdev.pci_dev = &g_sec_mock_pci_dev;
	g_sec_mock_hwdev.port_id = MOCK_ETH_DEV_PORT_ID;

	memset(&g_sec_mock_nic_dev, 0, sizeof(g_sec_mock_nic_dev));
	g_sec_mock_nic_dev.hwdev = &g_sec_mock_hwdev;
	g_sec_mock_nic_dev.feature_cap = 0;
	g_sec_mock_nic_dev.rss_state = HINIC3_RSS_ENABLE;
	g_sec_mock_nic_dev.num_rqs = 4;
	TAILQ_INIT(&g_sec_mock_nic_dev.tcam.tcam_list);
	TAILQ_INIT(&g_sec_mock_nic_dev.tcam.tcam_dynamic_info.tcam_dynamic_list);
	TAILQ_INIT(&g_sec_mock_nic_dev.filter_ethertype_list);
	TAILQ_INIT(&g_sec_mock_nic_dev.filter_fdir_rule_list);
	TAILQ_INIT(&g_sec_mock_nic_dev.rss_template_list);

	memset(&g_sec_mock_eth_dev_data, 0, sizeof(g_sec_mock_eth_dev_data));
	g_sec_mock_eth_dev_data.dev_private = &g_sec_mock_nic_dev;
	memset(&g_sec_mock_rxq, 0, sizeof(g_sec_mock_rxq));
	memset(g_sec_mock_rx_queues, 0, sizeof(g_sec_mock_rx_queues));
	memset(g_sec_mock_tx_queues, 0, sizeof(g_sec_mock_tx_queues));
	for (int i = 0; i < 128; i++)
		g_sec_mock_rx_queues[i] = &g_sec_mock_rxq;
	g_sec_mock_eth_dev_data.rx_queues = g_sec_mock_rx_queues;
	g_sec_mock_eth_dev_data.tx_queues = g_sec_mock_tx_queues;
	g_sec_mock_eth_dev_data.nb_rx_queues = 128;
	g_sec_mock_eth_dev_data.nb_tx_queues = 128;
	rte_eth_devices[MOCK_ETH_DEV_PORT_ID].data = &g_sec_mock_eth_dev_data;
	rte_eth_devices[MOCK_ETH_DEV_PORT_ID].device = &g_sec_mock_pci_dev.device;

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
	g_mock_sec_tcam_en = 1;
	g_mock_sec_tcam_ret = 0;
	g_mock_update_rss_config_ret = 0;
	g_mock_cmdq_set_rss_queue_type_ret = 0;
	g_mock_global_func_id = g_sec_mock_hwif.attr.func_global_idx;
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

	/* Reset sec TCAM mock variables */
	g_mock_fdir_alloc_sec_tcam_block_ret = 0;
	g_mock_fdir_alloc_sec_tcam_block_id = 0;
	g_mock_fdir_add_sec_tcam_rule_ret = 0;
	g_mock_fdir_del_sec_tcam_rule_ret = 0;
	g_mock_fdir_set_fdir_sec_tcam_rule_filter_ret = 0;

	g_qinfo_type = HINIC3_QINFO_TYPE_NORMAL;
	g_sec_mock_hwdev.qinfo_type = HINIC3_QINFO_TYPE_NORMAL;
}

/* ====== Pattern/action helper functions ====== */

static const struct rte_flow_attr g_sec_default_attr = {
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
make_eth_mac_item(struct rte_flow_item_eth *spec,
		  struct rte_flow_item_eth *mask,
		  struct rte_flow_item *item)
{
	memset(spec, 0, sizeof(*spec));
	memset(mask, 0, sizeof(*mask));
	memset(&spec->dst, 0xAA, sizeof(spec->dst.addr_bytes));
	memset(&spec->src, 0xBB, sizeof(spec->src.addr_bytes));
	memset(&mask->dst, 0xFF, sizeof(mask->dst.addr_bytes));
	memset(&mask->src, 0xFF, sizeof(mask->src.addr_bytes));
	item->type = RTE_FLOW_ITEM_TYPE_ETH;
	item->spec = spec;
	item->mask = mask;
	item->last = NULL;
}

static void
make_vlan_item(struct rte_flow_item_vlan *spec,
	       struct rte_flow_item_vlan *mask,
	       uint16_t tci,
	       struct rte_flow_item *item)
{
	memset(spec, 0, sizeof(*spec));
	memset(mask, 0, sizeof(*mask));
	spec->tci = rte_cpu_to_be_16(tci);
	mask->tci = rte_cpu_to_be_16(0xFFFF);
	item->type = RTE_FLOW_ITEM_TYPE_VLAN;
	item->spec = spec;
	item->mask = mask;
	item->last = NULL;
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
make_tcp_flags_item(struct rte_flow_item_tcp *spec,
		    struct rte_flow_item_tcp *mask,
		    uint16_t src_port, uint16_t dst_port,
		    uint16_t tcp_flags,
		    struct rte_flow_item *item)
{
	memset(spec, 0, sizeof(*spec));
	memset(mask, 0, sizeof(*mask));
	spec->hdr.src_port = rte_cpu_to_be_16(src_port);
	spec->hdr.dst_port = rte_cpu_to_be_16(dst_port);
	mask->hdr.src_port = rte_cpu_to_be_16(0xFFFF);
	mask->hdr.dst_port = rte_cpu_to_be_16(0xFFFF);
	mask->hdr.tcp_flags = (uint8_t)tcp_flags;
	spec->hdr.tcp_flags = (uint8_t)tcp_flags;
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

static void
make_end_action(struct rte_flow_action *action)
{
	action->type = RTE_FLOW_ACTION_TYPE_END;
	action->conf = NULL;
}

/* ====== Setup/teardown for test suite ====== */

static int
flow_sec_test_setup(void)
{
	return TEST_SUCCESS;
}

static void
flow_sec_test_teardown(void)
{
}

/* ================================================================== */
/* Section 1: Pattern Validation (via hinic3_flow_ops.validate)       */
/* ================================================================== */

static int
test_sec_validate_ipv4_tcp(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_tcp tcp_spec, tcp_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action_queue q_conf;
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int ret;

	flow_sec_dev_setup();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_tcp_item(&tcp_spec, &tcp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      &g_sec_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "sec validate ETH+IPV4+TCP should succeed");

	return TEST_SUCCESS;
}

static int
test_sec_validate_ipv4_udp(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_udp udp_spec, udp_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action_queue q_conf;
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int ret;

	flow_sec_dev_setup();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_udp_item(&udp_spec, &udp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      &g_sec_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "sec validate ETH+IPV4+UDP should succeed");

	return TEST_SUCCESS;
}

static int
test_sec_validate_ipv6_tcp(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv6 ip_spec, ip_mask;
	struct rte_flow_item_tcp tcp_spec, tcp_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action_queue q_conf;
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int ret;

	flow_sec_dev_setup();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv6_item(&ip_spec, &ip_mask, &pattern[1]);
	make_tcp_item(&tcp_spec, &tcp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      &g_sec_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "sec validate ETH+IPV6+TCP should succeed");

	return TEST_SUCCESS;
}

static int
test_sec_validate_ipv6_udp(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv6 ip_spec, ip_mask;
	struct rte_flow_item_udp udp_spec, udp_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action_queue q_conf;
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int ret;

	flow_sec_dev_setup();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv6_item(&ip_spec, &ip_mask, &pattern[1]);
	make_udp_item(&udp_spec, &udp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      &g_sec_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "sec validate ETH+IPV6+UDP should succeed");

	return TEST_SUCCESS;
}

static int
test_sec_validate_vlan_ipv4_tcp(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_vlan vlan_spec, vlan_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_tcp tcp_spec, tcp_mask;
	struct rte_flow_item pattern[6];
	struct rte_flow_action_queue q_conf;
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int ret;

	flow_sec_dev_setup();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_vlan_item(&vlan_spec, &vlan_mask, 100, &pattern[1]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[2]);
	make_tcp_item(&tcp_spec, &tcp_mask, 12345, 80, &pattern[3]);
	make_end_item(&pattern[4]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      &g_sec_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "sec validate ETH+VLAN+IPV4+TCP should succeed");

	return TEST_SUCCESS;
}

static int
test_sec_validate_eth_mac(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_tcp tcp_spec, tcp_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action_queue q_conf;
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int ret;

	flow_sec_dev_setup();

	make_eth_mac_item(&eth_spec, &eth_mask, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_tcp_item(&tcp_spec, &tcp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      &g_sec_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "sec validate ETH with MAC spec/mask should succeed");

	return TEST_SUCCESS;
}

static int
test_sec_validate_tcp_flags(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_tcp tcp_spec, tcp_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action_queue q_conf;
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int ret;

	flow_sec_dev_setup();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_tcp_flags_item(&tcp_spec, &tcp_mask, 12345, 80, 0x10, &pattern[2]);
	make_end_item(&pattern[3]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      &g_sec_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "sec validate TCP with tcp_flags mask should succeed");

	return TEST_SUCCESS;
}

static int
test_sec_validate_vxlan_ipv4_tcp(void)
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

	flow_sec_dev_setup();

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
				      &g_sec_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "sec validate VXLAN+IPv4+TCP should succeed");

	return TEST_SUCCESS;
}

static int
test_sec_validate_vxlan_ipv6_tcp(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_vxlan vxlan_spec, vxlan_mask;
	struct rte_flow_item_eth inner_eth_spec, inner_eth_mask;
	struct rte_flow_item_ipv6 inner_ip_spec, inner_ip_mask;
	struct rte_flow_item_tcp inner_tcp_spec, inner_tcp_mask;
	struct rte_flow_item pattern[9];
	struct rte_flow_action_queue q_conf;
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int ret;

	flow_sec_dev_setup();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_udp_any_item(&pattern[2]);
	make_vxlan_item(&vxlan_spec, &vxlan_mask, &pattern[3]);
	make_eth_item(&inner_eth_spec, &inner_eth_mask, 0, &pattern[4]);
	make_ipv6_item(&inner_ip_spec, &inner_ip_mask, &pattern[5]);
	make_tcp_item(&inner_tcp_spec, &inner_tcp_mask, 100, 200, &pattern[6]);
	make_end_item(&pattern[7]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      &g_sec_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "sec validate VXLAN+IPv6+TCP should succeed");

	return TEST_SUCCESS;
}

static int
test_sec_validate_geneve_ipv4_tcp(void)
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

	flow_sec_dev_setup();

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
				      &g_sec_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "sec validate GENEVE+IPv4+TCP should succeed");

	return TEST_SUCCESS;
}

static int
test_sec_validate_geneve_ipv6_tcp(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_geneve geneve_spec, geneve_mask;
	struct rte_flow_item_eth inner_eth_spec, inner_eth_mask;
	struct rte_flow_item_ipv6 inner_ip_spec, inner_ip_mask;
	struct rte_flow_item_tcp inner_tcp_spec, inner_tcp_mask;
	struct rte_flow_item pattern[9];
	struct rte_flow_action_queue q_conf;
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int ret;

	flow_sec_dev_setup();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_udp_any_item(&pattern[2]);
	make_geneve_item(&geneve_spec, &geneve_mask, &pattern[3]);
	make_eth_item(&inner_eth_spec, &inner_eth_mask, 0, &pattern[4]);
	make_ipv6_item(&inner_ip_spec, &inner_ip_mask, &pattern[5]);
	make_tcp_item(&inner_tcp_spec, &inner_tcp_mask, 100, 200, &pattern[6]);
	make_end_item(&pattern[7]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      &g_sec_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "sec validate GENEVE+IPv6+TCP should succeed");

	return TEST_SUCCESS;
}

static int
test_sec_validate_null_pattern(void)
{
	struct rte_flow_action_queue q_conf;
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int ret;

	flow_sec_dev_setup();

	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      &g_sec_default_attr, NULL, actions, &error);
	RTE_TEST_ASSERT(ret < 0, "null pattern should fail");

	return TEST_SUCCESS;
}

static int
test_sec_validate_unsupported_item(void)
{
	struct rte_flow_item pattern[3];
	struct rte_flow_action_queue q_conf;
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int ret;

	flow_sec_dev_setup();

	pattern[0].type = RTE_FLOW_ITEM_TYPE_GRE;
	pattern[0].spec = NULL;
	pattern[0].mask = NULL;
	pattern[0].last = NULL;
	make_end_item(&pattern[1]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      &g_sec_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT(ret < 0, "unsupported item (GRE) should fail");

	return TEST_SUCCESS;
}

static int
test_sec_validate_ipv4_bad_mask(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_tcp tcp_spec, tcp_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action_queue q_conf;
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int ret;

	flow_sec_dev_setup();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	memset(&ip_spec, 0, sizeof(ip_spec));
	memset(&ip_mask, 0, sizeof(ip_mask));
	ip_spec.hdr.src_addr = rte_cpu_to_be_32(0xC0A80101);
	ip_spec.hdr.dst_addr = rte_cpu_to_be_32(0xC0A80102);
	ip_mask.hdr.src_addr = rte_cpu_to_be_32(0xFFFFFFFF);
	ip_mask.hdr.dst_addr = rte_cpu_to_be_32(0xFFFFFFFF);
	ip_mask.hdr.version_ihl = 0x0F; /* bad mask */
	pattern[1].type = RTE_FLOW_ITEM_TYPE_IPV4;
	pattern[1].spec = &ip_spec;
	pattern[1].mask = &ip_mask;
	pattern[1].last = NULL;
	make_tcp_item(&tcp_spec, &tcp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      &g_sec_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT(ret < 0, "IPv4 with version_ihl mask should fail");

	return TEST_SUCCESS;
}

static int
test_sec_validate_ipv6_bad_mask(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv6 ip_spec, ip_mask;
	struct rte_flow_item_tcp tcp_spec, tcp_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action_queue q_conf;
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int ret;

	flow_sec_dev_setup();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	memset(&ip_spec, 0, sizeof(ip_spec));
	memset(&ip_mask, 0, sizeof(ip_mask));
	memset(&ip_spec.hdr.src_addr, 0x11, 16);
	memset(&ip_mask.hdr.src_addr, 0xFF, 16);
	ip_mask.hdr.vtc_flow = 0xFFFFFFFF; /* bad mask */
	pattern[1].type = RTE_FLOW_ITEM_TYPE_IPV6;
	pattern[1].spec = &ip_spec;
	pattern[1].mask = &ip_mask;
	pattern[1].last = NULL;
	make_tcp_item(&tcp_spec, &tcp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      &g_sec_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT(ret < 0, "IPv6 with vtc_flow mask should fail");

	return TEST_SUCCESS;
}

static int
test_sec_validate_tcp_bad_mask(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_tcp tcp_spec, tcp_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action_queue q_conf;
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int ret;

	flow_sec_dev_setup();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	memset(&tcp_spec, 0, sizeof(tcp_spec));
	memset(&tcp_mask, 0, sizeof(tcp_mask));
	tcp_spec.hdr.src_port = rte_cpu_to_be_16(12345);
	tcp_spec.hdr.dst_port = rte_cpu_to_be_16(80);
	tcp_mask.hdr.src_port = rte_cpu_to_be_16(0xFFFF);
	tcp_mask.hdr.dst_port = rte_cpu_to_be_16(0xFFFF);
	tcp_mask.hdr.sent_seq = 0x1; /* bad mask */
	pattern[2].type = RTE_FLOW_ITEM_TYPE_TCP;
	pattern[2].spec = &tcp_spec;
	pattern[2].mask = &tcp_mask;
	pattern[2].last = NULL;
	make_end_item(&pattern[3]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      &g_sec_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT(ret < 0, "TCP with sent_seq mask should fail");

	return TEST_SUCCESS;
}

static int
test_sec_validate_vxlan_wrong_dport(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_udp udp_spec, udp_mask;
	struct rte_flow_item_vxlan vxlan_spec, vxlan_mask;
	struct rte_flow_item_eth inner_eth_spec, inner_eth_mask;
	struct rte_flow_item_ipv4 inner_ip_spec, inner_ip_mask;
	struct rte_flow_item pattern[9];
	struct rte_flow_action_queue q_conf;
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int ret;

	flow_sec_dev_setup();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	/* Outer UDP with wrong dport (1234 instead of 4789) */
	make_udp_item(&udp_spec, &udp_mask, 12345, 1234, &pattern[2]);
	make_vxlan_item(&vxlan_spec, &vxlan_mask, &pattern[3]);
	make_eth_item(&inner_eth_spec, &inner_eth_mask, 0, &pattern[4]);
	make_ipv4_item(&inner_ip_spec, &inner_ip_mask, 0x0A000001, 0x0A000002,
		       &pattern[5]);
	make_end_item(&pattern[6]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      &g_sec_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT(ret < 0, "VXLAN with wrong dport should fail");

	return TEST_SUCCESS;
}

static int
test_sec_validate_no_sp620(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_tcp tcp_spec, tcp_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action_queue q_conf;
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	int ret;

	flow_sec_dev_setup();
	/* Override: not SP620, sec_tcam should be skipped */
	g_mock_is_sp620_nic = false;

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_tcp_item(&tcp_spec, &tcp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	ret = hinic3_flow_ops.validate(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      &g_sec_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "validate should succeed via normal path (not sec)");

	return TEST_SUCCESS;
}

/* ================================================================== */
/* Section 2: Flow Create/Destroy                                     */
/* ================================================================== */

static int
test_sec_create_ipv4_tcp_queue(void)
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

	flow_sec_dev_setup();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_tcp_item(&tcp_spec, &tcp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	flow = hinic3_flow_ops.create(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      &g_sec_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_NOT_NULL(flow, "sec create ETH+IPV4+TCP+QUEUE should succeed");

	ret = hinic3_flow_ops.destroy(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      flow, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "destroy sec flow should succeed");

	return TEST_SUCCESS;
}

static int
test_sec_create_ipv6_udp_drop(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv6 ip_spec, ip_mask;
	struct rte_flow_item_udp udp_spec, udp_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	struct rte_flow *flow;
	int ret;

	flow_sec_dev_setup();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv6_item(&ip_spec, &ip_mask, &pattern[1]);
	make_udp_item(&udp_spec, &udp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);
	make_drop_action(&actions[0]);
	make_end_action(&actions[1]);

	flow = hinic3_flow_ops.create(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      &g_sec_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_NOT_NULL(flow, "sec create ETH+IPV6+UDP+DROP should succeed");

	ret = hinic3_flow_ops.destroy(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      flow, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "destroy sec drop flow should succeed");

	return TEST_SUCCESS;
}

static int
test_sec_create_vlan_ipv4_rss(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_vlan vlan_spec, vlan_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_tcp tcp_spec, tcp_mask;
	struct rte_flow_item pattern[6];
	struct rte_flow_action_rss rss_conf;
	uint16_t queues[2] = {0, 1};
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	struct rte_flow *flow;
	int ret;

	flow_sec_dev_setup();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_vlan_item(&vlan_spec, &vlan_mask, 100, &pattern[1]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[2]);
	make_tcp_item(&tcp_spec, &tcp_mask, 12345, 80, &pattern[3]);
	make_end_item(&pattern[4]);
	make_rss_action(&rss_conf, queues, 2, &actions[0]);
	make_end_action(&actions[1]);

	flow = hinic3_flow_ops.create(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      &g_sec_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_NOT_NULL(flow, "sec create ETH+VLAN+IPV4+TCP+RSS should succeed");

	ret = hinic3_flow_ops.destroy(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      flow, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "destroy sec RSS flow should succeed");

	return TEST_SUCCESS;
}

static int
test_sec_create_vxlan_ipv4_queue(void)
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

	flow_sec_dev_setup();

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
				      &g_sec_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_NOT_NULL(flow, "sec create VXLAN+IPv4+TCP+QUEUE should succeed");

	ret = hinic3_flow_ops.destroy(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      flow, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "destroy sec VXLAN flow should succeed");

	return TEST_SUCCESS;
}

static int
test_sec_create_geneve_ipv4_queue(void)
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
	struct rte_flow *flow;
	int ret;

	flow_sec_dev_setup();

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

	flow = hinic3_flow_ops.create(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      &g_sec_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_NOT_NULL(flow, "sec create GENEVE+IPv4+TCP+QUEUE should succeed");

	ret = hinic3_flow_ops.destroy(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      flow, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "destroy sec GENEVE flow should succeed");

	return TEST_SUCCESS;
}

static int
test_sec_destroy_flow(void)
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

	flow_sec_dev_setup();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_tcp_item(&tcp_spec, &tcp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	flow = hinic3_flow_ops.create(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      &g_sec_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_NOT_NULL(flow, "create sec flow for destroy test");

	ret = hinic3_flow_ops.destroy(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      flow, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "destroy sec flow should succeed");

	RTE_TEST_ASSERT(TAILQ_EMPTY(&g_sec_mock_nic_dev.filter_fdir_rule_list),
		       "fdir list should be empty after destroy");

	return TEST_SUCCESS;
}

static int
test_sec_create_add_fail(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_tcp tcp_spec, tcp_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action_queue q_conf;
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	struct rte_flow *flow;

	flow_sec_dev_setup();
	g_mock_fdir_add_sec_tcam_rule_ret = -1;

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_tcp_item(&tcp_spec, &tcp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	flow = hinic3_flow_ops.create(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      &g_sec_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_NULL(flow, "sec create with add_sec_tcam_rule fail should fail");

	return TEST_SUCCESS;
}

static int
test_sec_create_block_alloc_fail(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_tcp tcp_spec, tcp_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action_queue q_conf;
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	struct rte_flow *flow;

	flow_sec_dev_setup();
	g_mock_fdir_alloc_sec_tcam_block_ret = -1;

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_tcp_item(&tcp_spec, &tcp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	flow = hinic3_flow_ops.create(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      &g_sec_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_NULL(flow, "sec create with block alloc fail should fail");

	return TEST_SUCCESS;
}

static int
test_sec_create_set_filter_fail(void)
{
	struct rte_flow_item_eth eth_spec, eth_mask;
	struct rte_flow_item_ipv4 ip_spec, ip_mask;
	struct rte_flow_item_tcp tcp_spec, tcp_mask;
	struct rte_flow_item pattern[5];
	struct rte_flow_action_queue q_conf;
	struct rte_flow_action actions[3];
	struct rte_flow_error error;
	struct rte_flow *flow;

	flow_sec_dev_setup();
	g_mock_fdir_set_fdir_sec_tcam_rule_filter_ret = -1;

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_tcp_item(&tcp_spec, &tcp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	flow = hinic3_flow_ops.create(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      &g_sec_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_NULL(flow, "sec create with set_filter fail should fail");

	return TEST_SUCCESS;
}

static int
test_sec_flush(void)
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

	flow_sec_dev_setup();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_tcp_item(&tcp_spec, &tcp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	flow1 = hinic3_flow_ops.create(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      &g_sec_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_NOT_NULL(flow1, "create sec flow 1 for flush");

	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80201, 0xC0A80202, &pattern[1]);
	flow2 = hinic3_flow_ops.create(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      &g_sec_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_NOT_NULL(flow2, "create sec flow 2 for flush");

	ret = hinic3_flow_ops.flush(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID], &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "flush sec flows should succeed");

	RTE_TEST_ASSERT(TAILQ_EMPTY(&g_sec_mock_nic_dev.filter_fdir_rule_list),
		       "fdir list should be empty after flush");

	return TEST_SUCCESS;
}

/* ================================================================== */
/* Section 3: Error Paths                                             */
/* ================================================================== */

static int
test_sec_del_nonexistent(void)
{
	struct rte_flow_error error;
	struct rte_flow flow;
	struct hinic3_filter_t rules;
	int ret;

	flow_sec_dev_setup();

	memset(&flow, 0, sizeof(flow));
	memset(&rules, 0, sizeof(rules));
	flow.filter_type = RTE_ETH_FILTER_FDIR;
	flow.rule = &rules;
	rules.fdir_filter.tcam_index = 0xFFFF;

	ret = hinic3_flow_ops.destroy(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      &flow, &error);
	RTE_TEST_ASSERT(ret < 0, "destroy non-existent sec flow should fail");

	return TEST_SUCCESS;
}

static int
test_sec_create_duplicate(void)
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

	flow_sec_dev_setup();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_tcp_item(&tcp_spec, &tcp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	flow1 = hinic3_flow_ops.create(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      &g_sec_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_NOT_NULL(flow1, "first sec create should succeed");

	/* Same pattern -> duplicate key -> should fail */
	flow2 = hinic3_flow_ops.create(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      &g_sec_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_NULL(flow2, "duplicate sec create should fail");

	ret = hinic3_flow_ops.destroy(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      flow1, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "destroy first sec flow");

	return TEST_SUCCESS;
}

static int
test_sec_create_multiple_same_block(void)
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

	flow_sec_dev_setup();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_tcp_item(&tcp_spec, &tcp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	flow1 = hinic3_flow_ops.create(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      &g_sec_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_NOT_NULL(flow1, "create sec flow 1");

	RTE_TEST_ASSERT_EQUAL(g_sec_mock_nic_dev.tcam_rule_nums, 1,
			      "tcam_rule_nums should be 1 after first flow");

	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80201, 0xC0A80202, &pattern[1]);
	flow2 = hinic3_flow_ops.create(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      &g_sec_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_NOT_NULL(flow2, "create sec flow 2");

	RTE_TEST_ASSERT_EQUAL(g_sec_mock_nic_dev.tcam_rule_nums, 2,
			      "tcam_rule_nums should be 2 after second flow");

	/* Both flows should share the same dynamic block */
	RTE_TEST_ASSERT_EQUAL(
		g_sec_mock_nic_dev.tcam.tcam_dynamic_info.dynamic_block_cnt, 1,
		"should have only 1 dynamic block for both flows");

	ret = hinic3_flow_ops.destroy(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      flow1, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "destroy sec flow 1");

	ret = hinic3_flow_ops.destroy(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      flow2, &error);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "destroy sec flow 2");

	return TEST_SUCCESS;
}

static int
test_sec_del_rule_fail(void)
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

	flow_sec_dev_setup();

	make_eth_item(&eth_spec, &eth_mask, 0, &pattern[0]);
	make_ipv4_item(&ip_spec, &ip_mask, 0xC0A80101, 0xC0A80102, &pattern[1]);
	make_tcp_item(&tcp_spec, &tcp_mask, 12345, 80, &pattern[2]);
	make_end_item(&pattern[3]);
	make_queue_action(&q_conf, 0, &actions[0]);
	make_end_action(&actions[1]);

	flow = hinic3_flow_ops.create(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      &g_sec_default_attr, pattern, actions, &error);
	RTE_TEST_ASSERT_NOT_NULL(flow, "create sec flow for del_rule_fail test");

	/*
	 * hinic3_flow_destroy for sec flows calls hinic3_fdir_del_sec_tcam_rule
	 * (wrapped via g_mock_fdir_del_sec_tcam_rule_ret).
	 * Setting g_mock_fdir_del_sec_tcam_rule_ret = -1 makes the destroy fail.
	 */
	g_mock_fdir_del_sec_tcam_rule_ret = -1;

	ret = hinic3_flow_ops.destroy(&rte_eth_devices[MOCK_ETH_DEV_PORT_ID],
				      flow, &error);
	RTE_TEST_ASSERT(ret < 0, "destroy with del_sec_tcam_rule fail should fail");

	return TEST_SUCCESS;
}

/* ================================================================== */
/* Test suite definition                                              */
/* ================================================================== */

static struct unit_test_suite hinic3_flow_sec_test_suite = {
	.suite_name = "HINIC3 Flow Sec TCAM Unit Tests",
	.setup = flow_sec_test_setup,
	.teardown = flow_sec_test_teardown,
	.unit_test_cases = {
		/* Section 1: Pattern Validation */
		TEST_CASE(test_sec_validate_ipv4_tcp),
		TEST_CASE(test_sec_validate_ipv4_udp),
		TEST_CASE(test_sec_validate_ipv6_tcp),
		TEST_CASE(test_sec_validate_ipv6_udp),
		TEST_CASE(test_sec_validate_vlan_ipv4_tcp),
		TEST_CASE(test_sec_validate_eth_mac),
		TEST_CASE(test_sec_validate_tcp_flags),
		TEST_CASE(test_sec_validate_vxlan_ipv4_tcp),
		TEST_CASE(test_sec_validate_vxlan_ipv6_tcp),
		TEST_CASE(test_sec_validate_geneve_ipv4_tcp),
		TEST_CASE(test_sec_validate_geneve_ipv6_tcp),
		TEST_CASE(test_sec_validate_null_pattern),
		TEST_CASE(test_sec_validate_unsupported_item),
		TEST_CASE(test_sec_validate_ipv4_bad_mask),
		TEST_CASE(test_sec_validate_ipv6_bad_mask),
		TEST_CASE(test_sec_validate_tcp_bad_mask),
		TEST_CASE(test_sec_validate_vxlan_wrong_dport),
		TEST_CASE(test_sec_validate_no_sp620),

		/* Section 2: Flow Create/Destroy */
		TEST_CASE(test_sec_create_ipv4_tcp_queue),
		TEST_CASE(test_sec_create_ipv6_udp_drop),
		TEST_CASE(test_sec_create_vlan_ipv4_rss),
		TEST_CASE(test_sec_create_vxlan_ipv4_queue),
		TEST_CASE(test_sec_create_geneve_ipv4_queue),
		TEST_CASE(test_sec_destroy_flow),
		TEST_CASE(test_sec_create_add_fail),
		TEST_CASE(test_sec_create_block_alloc_fail),
		TEST_CASE(test_sec_create_set_filter_fail),
		TEST_CASE(test_sec_flush),

		/* Section 3: Error Paths */
		TEST_CASE(test_sec_del_nonexistent),
		TEST_CASE(test_sec_create_duplicate),
		TEST_CASE(test_sec_create_multiple_same_block),
		TEST_CASE(test_sec_del_rule_fail),

		TEST_CASES_END()
	}
};

static int
test_hinic3_flow_sec(void)
{
	return unit_test_suite_runner(&hinic3_flow_sec_test_suite);
}

REGISTER_TEST_COMMAND(hinic3_flow_sec_autotest, test_hinic3_flow_sec);
