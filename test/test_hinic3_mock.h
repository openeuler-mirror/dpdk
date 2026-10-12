/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2025-2026 Huawei Technologies Co., Ltd
 * Shared mock infrastructure for hinic3 unit tests.
 *
 * This header declares mock control variables and function prototypes
 * used by multiple test files (fdir, flow, etc.). The mock implementations
 * are in test_hinic3_mock.c to avoid duplicate symbol errors.
 */

#ifndef _TEST_HINIC3_MOCK_H_
#define _TEST_HINIC3_MOCK_H_

#include <rte_ethdev.h>
#include <rte_pci.h>
#include <stdbool.h>
#include "base/hinic3_compat.h"
#include "base/hinic3_pmd_hwif.h"
#include "base/hinic3_pmd_nic_cfg.h"
#include "base/hinic3_pmd_mgmt.h"
#include "base/hinic3_pmd_mbox.h"
#include "hinic3_pmd_fdir.h"

/* DPDK < 21 compatibility: old enum names without RTE_ prefix */
#ifndef DPDK_21_11
#define RTE_ETH_VLAN_FILTER_MASK ETH_VLAN_FILTER_MASK
#define RTE_ETH_VLAN_STRIP_MASK ETH_VLAN_STRIP_MASK
#define RTE_ETH_RX_OFFLOAD_VLAN_STRIP DEV_RX_OFFLOAD_VLAN_STRIP
#define RTE_ETH_RX_OFFLOAD_IPV4_CKSUM DEV_RX_OFFLOAD_IPV4_CKSUM
#define RTE_ETH_RX_OFFLOAD_UDP_CKSUM DEV_RX_OFFLOAD_UDP_CKSUM
#define RTE_ETH_RX_OFFLOAD_TCP_CKSUM DEV_RX_OFFLOAD_TCP_CKSUM
#define RTE_ETH_RX_OFFLOAD_SCTP_CKSUM DEV_RX_OFFLOAD_SCTP_CKSUM
#define RTE_ETH_RX_OFFLOAD_VLAN_FILTER DEV_RX_OFFLOAD_VLAN_FILTER
#define RTE_ETH_RX_OFFLOAD_SCATTER DEV_RX_OFFLOAD_SCATTER
#define RTE_ETH_RX_OFFLOAD_RSS_HASH DEV_RX_OFFLOAD_RSS_HASH
#define RTE_ETH_TX_OFFLOAD_VLAN_INSERT DEV_TX_OFFLOAD_VLAN_INSERT
#define RTE_ETH_TX_OFFLOAD_IPV4_CKSUM DEV_TX_OFFLOAD_IPV4_CKSUM
#define RTE_ETH_TX_OFFLOAD_UDP_CKSUM DEV_TX_OFFLOAD_UDP_CKSUM
#define RTE_ETH_TX_OFFLOAD_TCP_CKSUM DEV_TX_OFFLOAD_TCP_CKSUM
#define RTE_ETH_TX_OFFLOAD_SCTP_CKSUM DEV_TX_OFFLOAD_SCTP_CKSUM
#define RTE_ETH_TX_OFFLOAD_TCP_TSO DEV_TX_OFFLOAD_TCP_TSO
#define RTE_ETH_TX_OFFLOAD_MULTI_SEGS DEV_TX_OFFLOAD_MULTI_SEGS
#define RTE_ETH_TX_OFFLOAD_GENEVE_TNL_TSO DEV_TX_OFFLOAD_GENEVE_TNL_TSO
#define RTE_ETH_TX_OFFLOAD_IPIP_TNL_TSO DEV_TX_OFFLOAD_IPIP_TNL_TSO
#define RTE_ETH_RSS_IPV4 ETH_RSS_IPV4
#define RTE_ETH_RSS_IP ETH_RSS_IP
#define RTE_ETH_RETA_GROUP_SIZE RTE_RETA_GROUP_SIZE
#define RTE_ETH_MQ_RX_DCB_FLAG ETH_MQ_RX_DCB_FLAG
#endif

/* ====== Mock control variables (shared across test files) ====== */

extern u8 g_mock_bifur_en;
extern u8 g_mock_iso_en;
extern u8 g_mock_bifur_type;
extern bool g_mock_is_shared_dev;
extern int g_mock_mgmt_sync_ret;
extern int g_mock_add_tcam_rule_ret;
extern int g_mock_del_tcam_rule_ret;
extern int g_mock_alloc_tcam_block_ret;
extern u16 g_mock_alloc_tcam_block_id;

/* Flow-specific mock variables */
extern u8 g_mock_sec_tcam_en;
extern int g_mock_sec_tcam_ret;
extern int g_mock_update_rss_config_ret;
extern int g_mock_cmdq_set_rss_queue_type_ret;
extern u16 g_mock_global_func_id;
extern int g_mock_mgmt_cfg_qgrp_id_ret;
extern u16 g_mock_mgmt_cfg_qgrp_id_val;
extern int g_mock_rss_template_alloc_ret;
extern int g_mock_rss_template_free_ret;
extern int g_mock_rss_get_indir_tbl_ret;
extern int g_mock_rss_set_indir_tbl_ret;
extern int g_mock_rss_set_indir_tbl_qpool_ret;
extern int g_mock_rss_queue_set_indir_tbl_ret;
extern int g_mock_rss_cfg_ret;
extern int g_mock_flow_query_fdir_ret;

/* flow_sec TCAM hardware function mocks */
extern int g_mock_fdir_alloc_sec_tcam_block_ret;
extern u16 g_mock_fdir_alloc_sec_tcam_block_id;
extern int g_mock_fdir_add_sec_tcam_rule_ret;
extern int g_mock_fdir_del_sec_tcam_rule_ret;
extern int g_mock_fdir_set_fdir_sec_tcam_rule_filter_ret;

/* g_qinfo_type is defined in test_hinic3_mock.c (driver 已改为 hwdev->qinfo_type 字段)。
 * 测试中定义此符号供 weaken-symbol 机制引用。 */
extern int g_qinfo_type;

/* hinic3_qinfo_type_init: driver 已移除此函数，测试中提供桩实现 */
int hinic3_qinfo_type_init(const char *dev_file);

/* ====== Ethdev ops callback test support ====== */
/*
 * hinic3_pmd_ops / hinic3_pmd_vf_ops are static const in hinic3_pmd_ethdev.c.
 * The build process uses objcopy --globalize-symbol to promote them to global
 * visibility so that unit tests can set rte_eth_devices[].dev_ops and call
 * static ops callbacks through the function pointer table.
 */
extern const struct eth_dev_ops hinic3_pmd_ops;
extern const struct eth_dev_ops hinic3_pmd_vf_ops;

/* Ethdev mock control variables */
extern int g_mock_add_vlan_ret;
extern int g_mock_del_vlan_ret;
extern int g_mock_set_port_mtu_ret;
extern int g_mock_set_rx_mode_ret;

/* TX compact mock control variables */
extern bool g_mock_is_sp620_nic;
extern bool g_mock_is_sp230_nic;

/* NIC cfg mock control variables */
extern enum func_type g_mock_func_type;
extern u8 g_mock_physical_port_id;
extern int g_mock_msg_to_mgmt_sync_ret;
extern int g_mock_mbox_to_pf_ret;
extern int g_mock_bifur_get_default_mac_ret;
extern u8 g_mock_bifur_get_default_mac_addr[6];
extern int g_mock_set_link_status_follow_ret;

/* TM hardware function mocks */
extern int g_mock_set_tm_hierarchy_do_commit_ret;
extern int g_mock_set_tm_config_tc_rate_ret;
extern int g_mock_set_qos_port_trust_ret;

/* MAC/VLAN hardware function mocks */
extern int g_mock_update_mac_ret;
extern int g_mock_set_mac_ret;
extern int g_mock_del_mac_ret;
extern int g_mock_set_vlan_fliter_ret;
extern int g_mock_set_rx_vlan_offload_ret;

/* Globalized static function declarations */
int hinic3_xstats_calc_num(struct hinic3_nic_dev *nic_dev);
bool hinic3_find_vlan_filter(struct hinic3_nic_dev *nic_dev, uint16_t vlan_id);
void hinic3_store_vlan_filter(struct hinic3_nic_dev *nic_dev, u16 vlan_id,
			      bool on);

#endif /* _TEST_HINIC3_MOCK_H_ */
