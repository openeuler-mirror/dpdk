/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2025-2026 Huawei Technologies Co., Ltd
 * Shared mock implementations for hinic3 unit tests.
 *
 * All mock functions and control variables are defined here
 * to avoid duplicate symbol errors when multiple test files are linked.
 */

#include <rte_common.h>
#include <rte_ethdev.h>
#include <rte_pci.h>
#include <rte_malloc.h>

#include "base/hinic3_compat.h"
#include "base/hinic3_pmd_hwif.h"
#include "base/hinic3_pmd_nic_cfg.h"
#include "base/hinic3_pmd_hwdev.h"
#include "base/hinic3_pmd_mgmt.h"
#include "base/hinic3_pmd_mbox.h"
#include "hinic3_pmd_ethdev.h"
#include "hinic3_pmd_fdir.h"
#include "hinic3_pmd_flow_sec.h"
#include "hinic3_pmd_rx.h"
#include "test_hinic3_mock.h"

/* ====== Mock control variable definitions ====== */

u8 g_mock_bifur_en = 0;
u8 g_mock_iso_en = 0;
u8 g_mock_bifur_type = 0;
bool g_mock_is_shared_dev = false;
int g_mock_mgmt_sync_ret = 0;
int g_mock_add_tcam_rule_ret = 0;
int g_mock_del_tcam_rule_ret = 0;
int g_mock_alloc_tcam_block_ret = 0;
u16 g_mock_alloc_tcam_block_id = 0;

u8 g_mock_sec_tcam_en = 0;
int g_mock_sec_tcam_ret = 0;
int g_mock_update_rss_config_ret = 0;
int g_mock_cmdq_set_rss_queue_type_ret = 0;
u16 g_mock_global_func_id = 0;
int g_mock_mgmt_cfg_qgrp_id_ret = 0;
u16 g_mock_mgmt_cfg_qgrp_id_val = 1;
int g_mock_rss_template_alloc_ret = 0;
int g_mock_rss_template_free_ret = 0;
int g_mock_rss_get_indir_tbl_ret = 0;
int g_mock_rss_set_indir_tbl_ret = 0;
int g_mock_rss_set_indir_tbl_qpool_ret = 0;
int g_mock_rss_queue_set_indir_tbl_ret = 0;
int g_mock_rss_cfg_ret = 0;
int g_mock_flow_query_fdir_ret = 0;

/* flow_sec TCAM hardware function mocks */
int g_mock_fdir_alloc_sec_tcam_block_ret = 0;
u16 g_mock_fdir_alloc_sec_tcam_block_id = 0;
int g_mock_fdir_add_sec_tcam_rule_ret = 0;
int g_mock_fdir_del_sec_tcam_rule_ret = 0;
int g_mock_fdir_set_fdir_sec_tcam_rule_filter_ret = 0;

/* Ethdev mock control variables */
int g_mock_add_vlan_ret = 0;
int g_mock_del_vlan_ret = 0;
int g_mock_set_port_mtu_ret = 0;
int g_mock_set_rx_mode_ret = 0;

/* TX compact mock control variables */
bool g_mock_is_sp620_nic = false;
bool g_mock_is_sp230_nic = false;

/* NIC cfg mock control variables */
enum func_type g_mock_func_type = TYPE_PF;
u8 g_mock_physical_port_id = 0;
int g_mock_msg_to_mgmt_sync_ret = 0;
int g_mock_mbox_to_pf_ret = 0;
int g_mock_bifur_get_default_mac_ret = 0;
u8 g_mock_bifur_get_default_mac_addr[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
int g_mock_set_link_status_follow_ret = 0;

/* TM hardware function mock control variables */
int g_mock_set_tm_hierarchy_do_commit_ret = 0;
int g_mock_set_tm_config_tc_rate_ret = 0;
int g_mock_set_qos_port_trust_ret = 0;

/* MAC/VLAN hardware function mock control variables */
int g_mock_update_mac_ret = 0;
int g_mock_set_mac_ret = 0;
int g_mock_del_mac_ret = 0;
int g_mock_set_vlan_fliter_ret = 0;
int g_mock_set_rx_vlan_offload_ret = 0;

/* g_qinfo_type: driver 不再定义此全局变量（已改为 hwdev->qinfo_type 字段），
 * 测试中定义此符号供 weaken-symbol 机制引用。 */
int g_qinfo_type = HINIC3_QINFO_TYPE_NORMAL;

/* hinic3_qinfo_type_init: driver 已移除此函数，提供桩实现供测试链接 */
int hinic3_qinfo_type_init(const char *dev_file)
{
	RTE_SET_USED(dev_file);
	g_qinfo_type = HINIC3_QINFO_TYPE_NORMAL;
	return 0;
}

/* ====== Shared mock implementations (from fdir tests) ====== */

int __wrap_hinic3_get_bifur_enable(void *hwdev, u8 *bifur_enable,
			    u8 *iso_enable, u8 *bifur_type)
{
	RTE_SET_USED(hwdev);
	if (bifur_enable)
		*bifur_enable = g_mock_bifur_en;
	if (iso_enable)
		*iso_enable = g_mock_iso_en;
	if (bifur_type)
		*bifur_type = g_mock_bifur_type;
	return 0;
}

bool __wrap_hinic3_bifur_is_shared_dev(struct rte_pci_device *pci_dev)
{
	RTE_SET_USED(pci_dev);
	return g_mock_is_shared_dev;
}

int __wrap_l2nic_msg_to_mgmt_sync(void *hwdev, u16 cmd, void *buf_in,
			   u16 in_size, void *buf_out, u16 *out_size)
{
	RTE_SET_USED(hwdev);
	RTE_SET_USED(cmd);
	RTE_SET_USED(buf_in);
	RTE_SET_USED(buf_out);
	/* Set *out_size to in_size on success so callers' !out_size checks pass */
	if (out_size && g_mock_mgmt_sync_ret == 0)
		*out_size = in_size;
	else if (out_size)
		*out_size = 0;
	return g_mock_mgmt_sync_ret;
}

int __wrap_hinic3_add_tcam_rule(void *hwdev,
			 struct hinic3_tcam_cfg_rule *tcam_rule,
			 u8 tcam_rule_type)
{
	RTE_SET_USED(tcam_rule_type);

	if (!hwdev || !tcam_rule)
		return -EINVAL;

	if (tcam_rule->index >= HINIC3_MAX_TCAM_RULES_NUM)
		return -EINVAL;

	return g_mock_add_tcam_rule_ret;
}

int __wrap_hinic3_del_tcam_rule(void *hwdev, u32 index, u8 tcam_rule_type)
{
	RTE_SET_USED(tcam_rule_type);

	if (!hwdev)
		return -EINVAL;

	if (index >= HINIC3_MAX_TCAM_RULES_NUM)
		return -EINVAL;

	return g_mock_del_tcam_rule_ret;
}

int __wrap_hinic3_alloc_tcam_block(void *hwdev, u16 *index)
{
	RTE_SET_USED(hwdev);
	if (index)
		*index = g_mock_alloc_tcam_block_id++;
	return g_mock_alloc_tcam_block_ret;
}

int __wrap_hinic3_free_tcam_block(void *hwdev, u16 *index)
{
	RTE_SET_USED(hwdev);
	RTE_SET_USED(index);
	return 0;
}

int __wrap_hinic3_set_fdir_tcam_rule_filter(void *hwdev, bool enable)
{
	RTE_SET_USED(enable);

	if (!hwdev)
		return -EINVAL;

	return 0;
}

int __wrap_hinic3_flush_tcam_rule(void *hwdev)
{
	if (!hwdev)
		return -EINVAL;

	return 0;
}

void __wrap_hinic3_mgmt_get_rss_id(void *hwdev, u16 func_id,
			    u16 *rss_temp_id, u16 *rss_node_id,
			    u16 *rss_inst_id)
{
	RTE_SET_USED(hwdev);
	RTE_SET_USED(func_id);
	if (rss_temp_id)
		*rss_temp_id = 0;
	if (rss_node_id)
		*rss_node_id = 0;
	if (rss_inst_id)
		*rss_inst_id = 0;
}

/* ====== Flow-specific mock implementations ====== */

int __wrap_hinic3_fdir_cfg_sec_tcam(void *hwdev, u8 *en)
{
	struct hinic3_nic_dev *nic_dev = NULL;

	if (!hwdev)
		return -EINVAL;

	nic_dev = ((struct hinic3_hwdev *)hwdev)->dev_handle;
	if (!is_sp620_nic(nic_dev))
		return 0;

	if (en)
		*en = g_mock_sec_tcam_en;
	return g_mock_sec_tcam_ret;
}

int __wrap_hinic3_fdir_alloc_sec_tcam_block(void *hwdev, u8 key_width,
				     u16 *index)
{
	RTE_SET_USED(key_width);

	if (!hwdev)
		return -EINVAL;

	if (g_mock_fdir_alloc_sec_tcam_block_ret != 0)
		return g_mock_fdir_alloc_sec_tcam_block_ret;

	if (index)
		*index = g_mock_fdir_alloc_sec_tcam_block_id++;
	return 0;
}

int __wrap_hinic3_fdir_sec_tcam_block_free(void *hwdev, u8 key_width,
				    u16 *index)
{
	RTE_SET_USED(key_width);
	RTE_SET_USED(index);

	if (!hwdev)
		return -EINVAL;

	return 0;
}

int __wrap_hinic3_fdir_add_sec_tcam_rule(void *hwdev,
				  struct hinic3_ext_tcam_cfg_rule *tcam_rule,
				  u8 tcam_rule_type, u8 key_width)
{
	RTE_SET_USED(tcam_rule_type);
	RTE_SET_USED(key_width);

	if (!hwdev || !tcam_rule)
		return -EINVAL;

	if (tcam_rule->index >= HINIC3_MAX_TCAM_RULES_NUM)
		return -EINVAL;

	return g_mock_fdir_add_sec_tcam_rule_ret;
}

int __wrap_hinic3_fdir_del_sec_tcam_rule(void *hwdev, u32 index,
				  u8 tcam_rule_type, u8 key_width)
{
	RTE_SET_USED(tcam_rule_type);
	RTE_SET_USED(key_width);

	if (!hwdev)
		return -EINVAL;

	if (index >= HINIC3_MAX_TCAM_RULES_NUM)
		return -EINVAL;

	return g_mock_fdir_del_sec_tcam_rule_ret;
}

int __wrap_hinic3_fdir_set_fdir_sec_tcam_rule_filter(void *hwdev, bool enable)
{
	RTE_SET_USED(enable);

	if (!hwdev)
		return -EINVAL;

	return g_mock_fdir_set_fdir_sec_tcam_rule_filter_ret;
}

int __wrap_hinic3_update_rss_config(struct rte_eth_dev *dev,
			     struct rte_eth_rss_conf *rss_conf)
{
	RTE_SET_USED(dev);
	RTE_SET_USED(rss_conf);
	return g_mock_update_rss_config_ret;
}

int __wrap_hinic3_cmdq_set_rss_queue_type(void *hwdev,
				   struct hinic3_rss_type rss_type,
				   u16 q_grp_id, u16 cmd_type)
{
	RTE_SET_USED(hwdev);
	RTE_SET_USED(rss_type);
	RTE_SET_USED(q_grp_id);
	RTE_SET_USED(cmd_type);
	return g_mock_cmdq_set_rss_queue_type_ret;
}

u16 __wrap_hinic3_global_func_id(void *hwdev)
{
	RTE_SET_USED(hwdev);
	return g_mock_global_func_id;
}

int __wrap_hinic3_mgmt_cfg_qgrp_id(void *hwdev, u8 opcode, u16 *q_grp_id)
{
	RTE_SET_USED(hwdev);
	RTE_SET_USED(opcode);
	if (q_grp_id && opcode == HINIC3_QUEUE_GROUP_ID_ALLOC)
		*q_grp_id = g_mock_mgmt_cfg_qgrp_id_val;
	return g_mock_mgmt_cfg_qgrp_id_ret;
}

int __wrap_hinic3_rss_template_alloc(void *hwdev, u16 q_grp_id)
{
	RTE_SET_USED(q_grp_id);

	if (!hwdev)
		return -EINVAL;


	if (g_mock_msg_to_mgmt_sync_ret != 0)
		return g_mock_msg_to_mgmt_sync_ret;

	return g_mock_rss_template_alloc_ret;
}

int __wrap_hinic3_rss_template_free(void *hwdev, u16 q_grp_id)
{
	RTE_SET_USED(q_grp_id);

	if (!hwdev)
		return -EINVAL;


	return g_mock_rss_template_free_ret;
}

int __wrap_hinic3_rss_get_indir_tbl(void *hwdev, u32 *indir_table,
			     u32 indir_table_size)
{
	RTE_SET_USED(hwdev);
	RTE_SET_USED(indir_table_size);
	if (indir_table && indir_table_size > 0) {
		u32 i;
		for (i = 0; i < indir_table_size; i++)
			indir_table[i] = i % 4;
	}
	return g_mock_rss_get_indir_tbl_ret;
}

int __wrap_hinic3_rss_set_indir_tbl(void *hwdev, const u32 *indir_table,
			     u32 indir_table_size)
{
	RTE_SET_USED(hwdev);
	RTE_SET_USED(indir_table);
	RTE_SET_USED(indir_table_size);
	return g_mock_rss_set_indir_tbl_ret;
}

int __wrap_hinic3_rss_set_indir_tbl_qpool(void *hwdev,
				   const u32 *indir_table,
				   u32 indir_table_size)
{
	RTE_SET_USED(hwdev);
	RTE_SET_USED(indir_table);
	RTE_SET_USED(indir_table_size);
	return g_mock_rss_set_indir_tbl_qpool_ret;
}

int __wrap_hinic3_rss_queue_set_indir_tbl(void *hwdev,
				   const u32 *indir_table,
				   u32 indir_table_size, u16 q_grp_id)
{
	RTE_SET_USED(hwdev);
	RTE_SET_USED(indir_table);
	RTE_SET_USED(indir_table_size);
	RTE_SET_USED(q_grp_id);
	return g_mock_rss_queue_set_indir_tbl_ret;
}

int __wrap_hinic3_rss_cfg(void *hwdev, u8 rss_en, u8 tc_num, u8 *prio_tc)
{
	RTE_SET_USED(rss_en);

	if (!hwdev || !prio_tc || (tc_num & (tc_num - 1)))
		return -EINVAL;


	return g_mock_rss_cfg_ret;
}

int __wrap_hinic3_flow_query_fdir_filter(struct rte_eth_dev *dev,
				 struct hinic3_fdir_filter *fdir_filter,
				 u64 *hits, u64 *bytes)
{
	RTE_SET_USED(dev);
	RTE_SET_USED(fdir_filter);
	if (hits)
		*hits = 100;
	if (bytes)
		*bytes = 1024;
	return g_mock_flow_query_fdir_ret;
}

/* ====== TCAM key calculation helpers and mock ====== */

void
__wrap_tcam_translate_key_y_local(u8 *key_y, u8 *src_input, u8 *mask, u8 len)
{
	u8 idx;
	for (idx = 0; idx < len; idx++)
		key_y[idx] = src_input[idx] & mask[idx];
}

void
__wrap_tcam_translate_key_x_local(u8 *key_x, u8 *key_y, u8 *mask, u8 len)
{
	u8 idx;
	for (idx = 0; idx < len; idx++)
		key_x[idx] = key_y[idx] ^ mask[idx];
}

void
__wrap_tcam_key_calculate_local(struct hinic3_tcam_key *tcam_key, void *fdir_tcam_rule, u8 key_len)
{
	if (key_len == HINIC3_TCAM_FLOW_KEY_SIZE) {
		struct hinic3_tcam_cfg_rule *tcam_rule =
			(struct hinic3_tcam_cfg_rule *)fdir_tcam_rule;

		tcam_translate_key_y_local(tcam_rule->key.y,
			(u8 *)(&tcam_key->key_info),
			(u8 *)(&tcam_key->key_mask),
			key_len);
		tcam_translate_key_x_local(tcam_rule->key.x,
			tcam_rule->key.y,
			(u8 *)(&tcam_key->key_mask),
			key_len);
	} else {
		struct hinic3_ext_tcam_cfg_rule *tcam_rule =
			(struct hinic3_ext_tcam_cfg_rule *)fdir_tcam_rule;

		tcam_translate_key_y_local(tcam_rule->key.y,
			(u8 *)(&tcam_key->key_info),
			(u8 *)(&tcam_key->key_mask),
			key_len);
		tcam_translate_key_x_local(tcam_rule->key.x,
			tcam_rule->key.y,
			(u8 *)(&tcam_key->key_mask),
			key_len);
	}
}

void __wrap_tcam_key_calculate(struct hinic3_tcam_key *tcam_key,
			void *fdir_tcam_rule, u8 key_len)
{
	tcam_key_calculate_local(tcam_key, fdir_tcam_rule, key_len);
}

/* ====== Ethdev mock implementations ====== */

int __wrap_hinic3_add_vlan(void *hwdev, u16 vlan_id, u16 func_id)
{
	RTE_SET_USED(hwdev);
	RTE_SET_USED(vlan_id);
	RTE_SET_USED(func_id);
	return g_mock_add_vlan_ret;
}

int __wrap_hinic3_del_vlan(void *hwdev, u16 vlan_id, u16 func_id)
{
	RTE_SET_USED(hwdev);
	RTE_SET_USED(vlan_id);
	RTE_SET_USED(func_id);
	return g_mock_del_vlan_ret;
}

int __wrap_hinic3_set_port_mtu(void *hwdev, u16 new_mtu)
{
	/* 参数校验与真实函数保持一致，但不调用真实函数以避免硬件访问 */
	if (!hwdev)
		return -EINVAL;
	if (new_mtu < HINIC3_MIN_MTU_SIZE || new_mtu > HINIC3_MAX_JUMBO_FRAME_SIZE)
		return -EINVAL;
	RTE_SET_USED(hwdev);
	return g_mock_set_port_mtu_ret;
}

int __wrap_hinic3_set_rx_mode(void *hwdev, u32 enable)
{
	if (!hwdev)
		return -EINVAL;
	RTE_SET_USED(hwdev);
	RTE_SET_USED(enable);
	return g_mock_set_rx_mode_ret;
}

/* TX compact: is_sp620_nic mock - avoids accessing nic_dev->hwdev */
bool __wrap_is_sp620_nic(struct hinic3_nic_dev *nic_dev)
{
	RTE_SET_USED(nic_dev);
	return g_mock_is_sp620_nic;
}

/* TX compact: is_sp230_nic mock - avoids accessing nic_dev->hwdev */
bool __wrap_is_sp230_nic(struct hinic3_nic_dev *nic_dev)
{
	RTE_SET_USED(nic_dev);
	return g_mock_is_sp230_nic;
}

/* RX: is_sp560_nic mock - avoids accessing nic_dev->hwdev->port_id */
bool __wrap_is_sp560_nic(struct hinic3_nic_dev *nic_dev)
{
	RTE_SET_USED(nic_dev);
	return false;
}

/* ====== NIC cfg mock implementations ====== */

enum func_type __wrap_hinic3_func_type(void *hwdev)
{
	RTE_SET_USED(hwdev);
	return g_mock_func_type;
}

u8 __wrap_hinic3_physical_port_id(void *hwdev)
{
	RTE_SET_USED(hwdev);
	return g_mock_physical_port_id;
}

int __wrap_hinic3_msg_to_mgmt_sync(void *hwdev, enum hinic3_mod_type mod,
			    u16 cmd, void *buf_in, u16 in_size,
			    void *buf_out, u16 *out_size, u32 timeout)
{
	RTE_SET_USED(hwdev);
	RTE_SET_USED(mod);
	RTE_SET_USED(cmd);
	RTE_SET_USED(buf_in);
	RTE_SET_USED(timeout);
	RTE_SET_USED(buf_out);
	if (out_size && g_mock_msg_to_mgmt_sync_ret == 0)
		*out_size = in_size;
	else if (out_size)
		*out_size = 0;
	return g_mock_msg_to_mgmt_sync_ret;
}

int __wrap_hinic3_mbox_to_pf(struct hinic3_hwdev *hwdev,
		      enum hinic3_mod_type mod, u16 cmd,
		      void *buf_in, u16 in_size, void *buf_out,
		      u16 *out_size, u32 timeout)
{
	RTE_SET_USED(hwdev);
	RTE_SET_USED(mod);
	RTE_SET_USED(cmd);
	RTE_SET_USED(buf_in);
	RTE_SET_USED(in_size);
	RTE_SET_USED(buf_out);
	RTE_SET_USED(timeout);
	if (out_size && g_mock_mbox_to_pf_ret == 0)
		*out_size = in_size;
	else if (out_size)
		*out_size = 0;
	return g_mock_mbox_to_pf_ret;
}

int __wrap_hinic3_bifur_get_default_mac(struct rte_pci_device *pci_dev,
				 u8 *mac_addr)
{
	RTE_SET_USED(pci_dev);
	if (mac_addr && g_mock_bifur_get_default_mac_ret == 0)
		memcpy(mac_addr, g_mock_bifur_get_default_mac_addr, 6);
	return g_mock_bifur_get_default_mac_ret;
}

int __wrap_hinic3_set_link_status_follow(void *hwdev,
				 enum hinic3_link_follow_status status)
{
	if (!hwdev)
		return -EINVAL;


	if (status >= HINIC3_LINK_FOLLOW_STATUS_MAX)
		return -EINVAL;

	return g_mock_set_link_status_follow_ret;
}

/* ====== TM hardware function mock implementations ====== */

int __wrap_hinic3_set_tm_hierarchy_do_commit(void *hwdev, u8 *cos_tc,
				      u8 *tc_bw, u8 *rate_limit)
{
	RTE_SET_USED(cos_tc);
	RTE_SET_USED(tc_bw);
	RTE_SET_USED(rate_limit);

	if (!hwdev)
		return -EINVAL;

	return g_mock_set_tm_hierarchy_do_commit_ret;
}

int __wrap_hinic3_set_tm_config_tc_rate(void *hwdev, u8 tc_no, u8 rate)
{
	RTE_SET_USED(tc_no);
	RTE_SET_USED(rate);

	if (!hwdev)
		return -EINVAL;

	return g_mock_set_tm_config_tc_rate_ret;
}

int __wrap_hinic3_set_qos_port_trust(void *hwdev, u8 trust)
{
	RTE_SET_USED(trust);

	if (!hwdev)
		return -EINVAL;

	return g_mock_set_qos_port_trust_ret;
}

/* ====== MAC/VLAN hardware function mocks ====== */

int __wrap_hinic3_update_mac(void *hwdev, u8 *old_mac, u8 *new_mac,
			     u16 vlan_id, u16 func_id)
{
	RTE_SET_USED(func_id);
	if (!hwdev || !old_mac || !new_mac)
		return -EINVAL;
	if (vlan_id >= 4096)
		return -EINVAL;
	if (g_mock_msg_to_mgmt_sync_ret)
		return -EIO;
	return g_mock_update_mac_ret;
}

int __wrap_hinic3_set_mac(void *hwdev, const u8 *mac_addr, u16 vlan_id,
			  u16 func_id)
{
	RTE_SET_USED(func_id);
	if (!hwdev || !mac_addr)
		return -EINVAL;
	if (vlan_id >= 4096)
		return -EINVAL;
	if (g_mock_msg_to_mgmt_sync_ret)
		return -EIO;
	return g_mock_set_mac_ret;
}

int __wrap_hinic3_del_mac(void *hwdev, const u8 *mac_addr, u16 vlan_id,
			  u16 func_id)
{
	RTE_SET_USED(func_id);
	if (!hwdev || !mac_addr)
		return -EINVAL;
	if (vlan_id >= 4096)
		return -EINVAL;
	if (g_mock_msg_to_mgmt_sync_ret)
		return -EIO;
	return g_mock_del_mac_ret;
}

int __wrap_hinic3_set_vlan_fliter(void *hwdev, u32 vlan_filter_ctrl)
{
	RTE_SET_USED(vlan_filter_ctrl);
	if (!hwdev)
		return -EINVAL;
	return g_mock_set_vlan_fliter_ret;
}

int __wrap_hinic3_set_rx_vlan_offload(void *hwdev, u8 en)
{
	RTE_SET_USED(en);
	if (!hwdev)
		return -EINVAL;
	return g_mock_set_rx_vlan_offload_ret;
}
