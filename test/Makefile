# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2025-2026 Huawei Technologies Co., Ltd

# HINIC3 PMD tests
SRCS-y += test_hinic3_basic.c
SRCS-y += test_hinic3_hairpin.c

CFLAGS += -I$(RTE_SDK)/drivers/net/hinic3
CFLAGS += -I$(RTE_SDK)/drivers/net/hinic3/base

LDFLAGS += -Wl,--wrap=hinic3_get_bifur_enable -Wl,--wrap=hinic3_bifur_is_shared_dev -Wl,--wrap=l2nic_msg_to_mgmt_sync -Wl,--wrap=hinic3_add_tcam_rule -Wl,--wrap=hinic3_del_tcam_rule -Wl,--wrap=hinic3_alloc_tcam_block -Wl,--wrap=hinic3_free_tcam_block -Wl,--wrap=hinic3_set_fdir_tcam_rule_filter -Wl,--wrap=hinic3_flush_tcam_rule -Wl,--wrap=hinic3_mgmt_get_rss_id -Wl,--wrap=hinic3_fdir_cfg_sec_tcam -Wl,--wrap=hinic3_fdir_alloc_sec_tcam_block -Wl,--wrap=hinic3_fdir_sec_tcam_block_free -Wl,--wrap=hinic3_fdir_add_sec_tcam_rule -Wl,--wrap=hinic3_fdir_del_sec_tcam_rule -Wl,--wrap=hinic3_fdir_set_fdir_sec_tcam_rule_filter -Wl,--wrap=hinic3_update_rss_config -Wl,--wrap=hinic3_cmdq_set_rss_queue_type -Wl,--wrap=hinic3_global_func_id -Wl,--wrap=hinic3_mgmt_cfg_qgrp_id -Wl,--wrap=hinic3_rss_template_alloc -Wl,--wrap=hinic3_rss_template_free -Wl,--wrap=hinic3_rss_get_indir_tbl -Wl,--wrap=hinic3_rss_set_indir_tbl -Wl,--wrap=hinic3_rss_set_indir_tbl_qpool -Wl,--wrap=hinic3_rss_queue_set_indir_tbl -Wl,--wrap=hinic3_rss_cfg -Wl,--wrap=hinic3_flow_query_fdir_filter -Wl,--wrap=tcam_translate_key_y_local -Wl,--wrap=tcam_translate_key_x_local -Wl,--wrap=tcam_key_calculate_local -Wl,--wrap=tcam_key_calculate -Wl,--wrap=hinic3_add_vlan -Wl,--wrap=hinic3_del_vlan -Wl,--wrap=hinic3_set_port_mtu -Wl,--wrap=hinic3_set_rx_mode -Wl,--wrap=is_sp620_nic -Wl,--wrap=is_sp230_nic -Wl,--wrap=is_sp560_nic -Wl,--wrap=hinic3_func_type -Wl,--wrap=hinic3_physical_port_id -Wl,--wrap=hinic3_msg_to_mgmt_sync -Wl,--wrap=hinic3_mbox_to_pf -Wl,--wrap=hinic3_bifur_get_default_mac -Wl,--wrap=hinic3_set_link_status_follow -Wl,--wrap=hinic3_set_tm_hierarchy_do_commit -Wl,--wrap=hinic3_set_tm_config_tc_rate -Wl,--wrap=hinic3_set_qos_port_trust -Wl,--wrap=hinic3_update_mac -Wl,--wrap=hinic3_set_mac -Wl,--wrap=hinic3_del_mac -Wl,--wrap=hinic3_set_vlan_fliter -Wl,--wrap=hinic3_set_rx_vlan_offload
