/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2025 Huawei Technologies Co., Ltd
 */

#include <stddef.h>
#include <string.h>
#include "hinic3_util.h"
#include "hinic3_log.h"
#include "hinic3_error_stats.h"

#define HINIC3_ERRSTAT_ERROR_STRING "error"
#define HINIC3_ERRSTAT_WARNING_STRING "warning"
#define HINIC3_ERRSTAT_LOG_MAX_DIFF 10800
static uint64_t g_hinic3_agent_error_stats[HINIC3_ERRSTAT_END] = { 0 };

struct hinic3_error_stats_info {
    enum hinic3_module module;
    const char *string;
    long long int error_latest_log_time;
};



static struct hinic3_error_stats_info g_error_stats_info[] = {
    // 通用工具模块
    [HINIC3_ERRSTAT_COMMON_ERRSTAT_TYPE_OUTOFBOUND] = {HINIC3_COMMON_RESOURCE, "errstat_self_type_out_of_bound"},
    [HINIC3_ERRSTAT_COMMON_ERRSTAT_TYPE_NO_INFO] = {HINIC3_COMMON_RESOURCE, "errstat_self_type_no_info"},
    [HINIC3_ERRSTAT_COMMON_ERRSTAT_INPUT_NULL] = {HINIC3_COMMON_RESOURCE, "errstat_self_input_is_null"},
    // CT 卸载模块
    [HINIC3_FLOW_AGENT_ERROR_CT_NOT_READY_TO_OFFLOAD] = {HINIC3_CT_OFFLOAD, "error_ct_not_ready_to_offload"},
    [HINIC3_FLOW_AGENT_ERROR_CT_CHECK_FAIL] = {HINIC3_CT_OFFLOAD, "error_ct_check_fail"},
    [HINIC3_FLOW_AGENT_ERROR_CT_NOT_READY] = {HINIC3_CT_OFFLOAD, "warning_ct_not_ready"},
    [HINIC3_FLOW_AGENT_ERROR_CT_CLEAN_ID_NOT_FOUND] = {HINIC3_CT_OFFLOAD, "error_clean_id_not_found"},
    [HINIC3_FLOW_AGENT_ERROR_CT_CHECK_CONN_FAILED] = {HINIC3_CT_OFFLOAD, "error_ct_check_conn_failed"},
    [HINIC3_FLOW_AGENT_ERROR_CT_GET_INFO_ID_NOT_FOUND] = {HINIC3_CT_OFFLOAD, "error_get_info_id_not_found"},
    [HINIC3_FLOW_AGENT_ERROR_CT_CHECK_ALG_CTRL] = {HINIC3_CT_OFFLOAD, "error_check_alg_ctrl"},
    [HINIC3_FLOW_AGENT_ERROR_CT_RECIRCLED_NOT_TCP] = {HINIC3_CT_OFFLOAD, "error_recircled_non_tcp"},
    [HINIC3_FLOW_AGENT_ERROR_CT_GET_EXIST_CONN_NOT_FOUND] = {HINIC3_CT_OFFLOAD, "error_get_exist_conn_not_found"},
    [HINIC3_FLOW_AGENT_ERROR_CT_NOT_CONNECT] = {HINIC3_CT_OFFLOAD, "warning_ct_not_connect"},
    [HINIC3_FLOW_AGENT_ERROR_ADD_CT_SEARCH_ERR] = {HINIC3_CT_OFFLOAD, "error_add_ct_nat_action_search_fail"},
    [HINIC3_FLOW_AGENT_ERROR_SET_CT_SEARCH_ERR] = {HINIC3_CT_OFFLOAD, "error_ct_search_fail"},
    [HINIC3_FLOW_AGENT_ERROR_SET_CT_NOT_NAT] = {HINIC3_CT_OFFLOAD, "warning_set_ct_not_nat"},
    [HINIC3_FLOW_AGENT_ERROR_ADD_CT_NOT_NAT] = {HINIC3_CT_OFFLOAD, "warning_add_ct_not_nat"},
    [HINIC3_FLOW_AGENT_ERROR_CT_REVERT_CONN_NULL] = {HINIC3_CT_OFFLOAD, "error_ct_revert_conn_is_null"},
    [HINIC3_FLOW_AGENT_ERROR_CT_REVERT_CONN_NOT_NAT] = {HINIC3_CT_OFFLOAD, "error_ct_revert_conn_is_not_nat"},
    // driver adapter模块
    [HINIC3_FLOW_AGENT_ERROR_GLOBAL_CFG_SET] = {HINIC3_DRIVER_ADAPTER, "error_global_cfg_set_emc_sample__error"},
    [HINIC3_VPORT_HOVS_RTE_RX_BURST_NULL] = {HINIC3_DRIVER_ADAPTER, "error_vport_hovs_rte_rx_burst_null"},
    [HINIC3_VPORT_HOVS_RTE_TX_BURST_NULL] = {HINIC3_DRIVER_ADAPTER, "error_vport_hovs_rte_tx_burst_null"},
    [HINIC3_VPORT_HOVS_RTE_TX_BURST_QUEUE_ID_OUT_OF_RANGE] =
    {HINIC3_DRIVER_ADAPTER, "error_vport_hovs_rte_tx_burst_queue_id_out_of_range"},
    // 流表模块
    [HINIC3_FLOW_AGENT_ERROR_OFFLOAD_PROCESS_FLOW_KEY] = {HINIC3_FLOWS, "error_emc_offload_process_flow_key_fail"},
    [HINIC3_FLOW_AGENT_ERROR_OFFLOAD_PROCESS_FLOW_MASK] = {HINIC3_FLOWS, "error_emc_offload_process_flow_mask_fail"},
    [HINIC3_FLOW_AGENT_ERROR_MODIFY_PROCESS_FLOW_KEY] =
    {HINIC3_FLOWS, "error_emc_offload_modify_process_flow_key_fail"},
    [HINIC3_FLOW_AGENT_ERROR_OFFLOAD_CHECK_OFFLOADING_REPEATED] =
    {HINIC3_FLOWS, "error_emc_offload_check_offload_repeated"},
    [HINIC3_FLOW_AGENT_ERROR_OFFLOAD_CHECK_OFFLOADING_EXIST_GAP] =
    {HINIC3_FLOWS, "warning_emc_offload_check_offload_exit_gap"},
    [HINIC3_FLOW_AGENT_ERROR_OFFLOAD_CHECK_OFFLOADING_ONGOING] =
    {HINIC3_FLOWS, "error_emc_offload_check_offload_ongoing"},
    [HINIC3_FLOW_AGENT_ERROR_OFFLOAD_INSERT_RTE_FLOW_IN_HMAP] = {HINIC3_FLOWS, "error_emc_offload_insert_into_hmap_fail"},
    [HINIC3_FLOW_AGENT_ERROR_OFFLOAD_PARSE_FLOW_ACTION] = {HINIC3_FLOWS, "error_emc_offload_parse_flow_action_error"},
    [HINIC3_FLOW_AGENT_ERROR_MODIFY_PARSE_FLOW_ACTION] =
    {HINIC3_FLOWS, "error_emc_offload_modify_parse_flow_action_error"},
    [HINIC3_FLOW_AGENT_ERROR_OFFLOAD_CALL_HARDWARE_FUNC] = {HINIC3_FLOWS, "error_emc_offload_call_hardware_func_error"},
    [HINIC3_FLOW_AGENT_ERROR_MODIFY_CALL_HARDWARE_FUNC] =
    {HINIC3_FLOWS, "error_emc_offload_modify_call_hardware_func_error"},
    [HINIC3_FLOW_AGENT_ERROR_AGE_DEL_RTE_FLOW_IN_HMAP] = {HINIC3_FLOWS, "error_age_emc_destroy_flow_in_hmap_error"},
    [HINIC3_FLOW_AGENT_ERROR_DEL_RTE_FLOW_IN_HMAP] = {HINIC3_FLOWS, "error_emc_destroy_flow_in_hmap_error"},
    [HINIC3_FLOW_AGENT_ERROR_DEL_FLOW_QOS] = {HINIC3_FLOWS, "error_emc_destroy_flow_qos_error"},
    [HINIC3_FLOW_AGENT_ERROR_DEL_HARD_FLOW] = {HINIC3_FLOWS, "error_emc_destroy_hard_error"},
    [HINIC3_FLOW_AGENT_ERROR_EMC_FLOW_OFFLOAD] = {HINIC3_FLOWS, "error_emc_offload_error"},
    [HINIC3_FLOW_AGENT_ERROR_MEGA_FLOW_ALLOC] = {HINIC3_FLOWS, "error_emc_offload_flow_mega_flow_alloc_error"},
    [HINIC3_FLOW_AGENT_ERROR_EMC_FLOW_CREATE_ECOLOGY_ALLOC] =
    {HINIC3_FLOWS, "error_emc_offload_flow_create_ecology_alloc_error"},
    [HINIC3_FLOW_AGENT_ERROR_EMC_FLOW_ALLOC] = {HINIC3_FLOWS, "error_emc_offload_flow_alloc_error"},
    [HINIC3_FLOW_AGENT_ERROR_EMC_FLOW_OFFLOAD_NO_INIT] = {HINIC3_FLOWS, "error_emc_offload_agent_no_init_error"},
    [HINIC3_FLOW_AGENT_ERROR_ETH_FLOW_CREATE_ECOLOGY_NO_INIT] =
    {HINIC3_FLOWS, "error_emc_eth_flow_create_ecology_no_init_error"},
    [HINIC3_FLOW_AGENT_ERROR_ACL_DEL_FLOW_IN_SESSION] = {HINIC3_FLOWS, "error_emc_acl_destroy_mirror_session_error"},
    [HINIC3_FLOW_AGENT_ERROR_AGE_DEL_FLOW_IN_SESSION] = {HINIC3_FLOWS, "error_emc_age_destroy_mirror_session_error"},
    [HINIC3_FLOW_AGENT_ERROR_CMCC_DEL_FLOW_IN_SESSION] = {HINIC3_FLOWS, "error_emc_cmcc_destroy_mirror_session_error"},
    [HINIC3_FLOW_AGENT_ERROR_MODIFY_DEL_FLOW_IN_SESSION] =
    {HINIC3_FLOWS, "error_emc_modify_destroy_mirror_session_error"},
    [HINIC3_FLOW_AGENT_ERROR_MEGA_DEL_FLOW_IN_SESSION] = {HINIC3_FLOWS, "error_emc_mega_destroy_mirror_session_error"},
    [HINIC3_FLOW_AGENT_ERROR_EMC_HARD_FLOW_FLUSH] = {HINIC3_FLOWS, "error_emc_flush_hard_flow_error"},
    [HINIC3_FLOW_AGENT_ERROR_EMC_MPOOL_FLUSH] = {HINIC3_FLOWS, "error_emc_flush_mpool_error"},
    [HINIC3_FLOW_AGENT_ERROR_MIRROR_SESSION_FLUSH] = {HINIC3_FLOWS, "error_emc_flush_mirror_session_error"},
    [HINIC3_FLOW_AGENT_ERROR_MEGA_MIRROR_SESSION_FLUSH] =
    {HINIC3_FLOWS, "error_emc_flush_mega_data_mirror_session_error"},
    [HINIC3_FLOW_AGENT_ERROR_FLOW_NOT_OFFLOADED] = {HINIC3_FLOWS, "error_emc_flow_not_offload"},
    [HINIC3_FLOW_AGENT_ERROR_DUMP_START] = {HINIC3_FLOWS, "error_emc_dump_start_error"},
    [HINIC3_FLOW_AGENT_ERROR_MEGA_DUMP_START] = {HINIC3_FLOWS, "error_mega_dump_start_error"},
    [HINIC3_FLOW_AGENT_ERROR_DUMP_START_HARD] = {HINIC3_FLOWS, "error_emc_dump_start_hard_error"},
    [HINIC3_FLOW_AGENT_ERROR_DUMP_DONE] = {HINIC3_FLOWS, "error_emc_dump_done_error"},
    [HINIC3_FLOW_AGENT_ERROR_DUMP_DONE_HARD] = {HINIC3_FLOWS, "error_emc_dump_done_hard_error"},
    [HINIC3_FLOW_AGENT_ERROR_DUMP_NEXT] = {HINIC3_FLOWS, "error_dump_next_error"},
    [HINIC3_FLOW_AGENT_ERROR_DUMP_EMC_NEXT] = {HINIC3_FLOWS, "error_emc_dump_next_error"},
    [HINIC3_FLOW_AGENT_ERROR_DUMP_DP_NEXT] = {HINIC3_FLOWS, "error_dp_dump_next_error"},
    [HINIC3_FLOW_AGENT_ERROR_DUMP_DP_FORMAT] = {HINIC3_FLOWS, "error_dp_dump_format_error"},
    [HINIC3_FLOW_AGENT_ERROR_DUMP_MEGA_NEXT] = {HINIC3_FLOWS, "error_mega_dump_next_error"},
    [HINIC3_FLOW_AGENT_ERROR_DUMP_NEXT_MEM] = {HINIC3_FLOWS, "error_emc_dump_next_mem_error"},
    [HINIC3_FLOW_AGENT_ERROR_ACL_FLOW_HARDWARE_DEL] = {HINIC3_FLOWS, "error_acl_destroy_hardware_error"},
    [HINIC3_FLOW_AGENT_ERROR_ACL_FLOW_TABLE_DEL] = {HINIC3_FLOWS, "error_acl_destroy_sw_table_error"},
    [HINIC3_FLOW_AGENT_ERROR_ACL_FLOW_OFFLOAD] = {HINIC3_FLOWS, "error_acl_offload_error"},
    [HINIC3_FLOW_AGENT_ERROR_DUMP_ACL_START] = {HINIC3_FLOWS, "error_acl_dump_start_error"},
    [HINIC3_FLOW_AGENT_ERROR_DUMP_ACL_START_HARD] = {HINIC3_FLOWS, "error_acl_dump_start_hard_error"},
    [HINIC3_FLOW_AGENT_ERROR_DUMP_ACL_DONE] = {HINIC3_FLOWS, "error_acl_dump_done_error"},
    [HINIC3_FLOW_AGENT_ERROR_DUMP_ACL_NEXT] = {HINIC3_FLOWS, "error_acl_dump_next_error"},
    [HINIC3_FLOW_AGENT_ERROR_DUMP_ACL_NEXT_HARD] = {HINIC3_FLOWS, "error_acl_dump_next_hard_error"},
    [HINIC3_FLOW_AGENT_ERROR_MEGA_FLOW_OFFLOAD] = {HINIC3_FLOWS, "error_mega_offload_fail"},
    [HINIC3_FLOW_AGENT_ERROR_MEGA_FLOW_OFFLOAD_REPEATED] = {HINIC3_FLOWS, "error_mega_offload_repeated"},
    [HINIC3_FLOW_AGENT_ERROR_MEGA_FLOW_OFFLOAD_DRV_CALL] = {HINIC3_FLOWS, "error_mega_offload_drv_call_error"},
    [HINIC3_FLOW_AGENT_ERROR_MEGA_OFFLOAD_ETH_ITEM] = {HINIC3_FLOWS, "error_mega_offload_eth_item_error"},
    [HINIC3_FLOW_AGENT_ERROR_MEGA_OFFLOAD_VLAN_ITEM] = {HINIC3_FLOWS, "error_mega_offload_vlan_item_error"},
    [HINIC3_FLOW_AGENT_ERROR_MEGA_OFFLOAD_INPORT_ITEM] = {HINIC3_FLOWS, "error_mega_offload_inport_item_error"},
    [HINIC3_FLOW_AGENT_ERROR_MEGA_OFFLOAD_VXLAN_ITEM] = {HINIC3_FLOWS, "error_mega_offload_vxlan_item_error"},
    [HINIC3_FLOW_AGENT_ERROR_MEGA_OFFLOAD_OUTPORT_ACTION] = {HINIC3_FLOWS, "error_mega_offload_outport_action_error"},
    [HINIC3_FLOW_AGENT_ERROR_MEGA_OFFLOAD_SET_SMAC_ACTION] = {HINIC3_FLOWS, "error_mega_offload_set_smac_action_error"},
    [HINIC3_FLOW_AGENT_ERROR_MEGA_OFFLOAD_SET_DMAC_ACTION] = {HINIC3_FLOWS, "error_mega_offload_set_dmac_action_error"},
    [HINIC3_FLOW_AGENT_ERROR_MEGA_OFFLOAD_VXLAN_ENCAP_ACTION] = {HINIC3_FLOWS, "error_mega_offload_vxlan_encap_action"},
    [HINIC3_FLOW_AGENT_ERROR_MEGA_OFFLOAD_OUTPORT_WRONG] = {HINIC3_FLOWS, "error_mega_offload_outport_error"},
    [HINIC3_FLOW_AGENT_ERROR_MEGA_MPOOL_FLUSH] = {HINIC3_FLOWS, "error_mega_flush_mpool_error"},
    [HINIC3_FLOW_AGENT_ERROR_MEGA_FLOW_FLUSH] = {HINIC3_FLOWS, "error_mega_flush_flow_error"},
    [HINIC3_FLOW_AGENT_ERROR_MEGA_FLOW_QUERY] = {HINIC3_FLOWS, "error_mega_query_error"},
    [HINIC3_FLOW_AGENT_ERROR_MEGA_FLOW_DELETE] = {HINIC3_FLOWS, "error_mega_delete_error"},
    [HINIC3_FLOW_AGENT_ERROR_MEGA_FLOW_KEY_PARSE] = {HINIC3_FLOWS, "error_mega_parse_key_fail"},
    [HINIC3_FLOW_AGENT_ERROR_MEGA_FLOW_ACTION_PARSE] = {HINIC3_FLOWS, "error_mega_parse_action_fail"},
    [HINIC3_FLOW_AGENT_ERROR_MEGA_DUMP_DONE_HARD] = {HINIC3_FLOWS, "error_mega_dump_done_hard_error"},
    [HINIC3_FLOW_AGENT_ERROR_MEGA_DUMP_START_HARD] = {HINIC3_FLOWS, "error_mega_dump_start_hard_error"},
    [HINIC3_FLOW_AGENT_ERROR_MEGA_DUMP_DONE] = {HINIC3_FLOWS, "error_mega_dump_done_error"},
    [HINIC3_FLOW_AGENT_ERROR_MEGA_FLOW_DUMP_ACTION_TYPE] = {HINIC3_FLOWS, "error_mega_dump_action_type"},
    [HINIC3_FLOW_AGENT_ERROR_MEGA_FLOW_DUMP_ITEM_TYPE] = {HINIC3_FLOWS, "error_mega_dump_item_type"},
    [HINIC3_FLOW_AGENT_ERROR_MEGA_DUMP_NEXT_HARD] = {HINIC3_FLOWS, "error_mega_dump_next_hard_error"},
    [HINIC3_FLOW_AGENT_ERROR_MEGA_DUMP_INPORT_ITEM] = {HINIC3_FLOWS, "error_mega_dump_inport_item_error"},
    [HINIC3_FLOW_AGENT_ERROR_MEGA_DUMP_OUTPORT_ITEM] = {HINIC3_FLOWS, "error_mega_dump_outport_item_error"},
    [HINIC3_FLOW_AGENT_ERROR_MEGA_DUMP_FORMAT_INPORT] = {HINIC3_FLOWS, "error_mega_dump_format_inport_error"},
    [HINIC3_FLOW_AGENT_ERROR_MEGA_DUMP_FORMAT_VLAN] = {HINIC3_FLOWS, "error_mega_dump_format_vlan_error"},
    [HINIC3_FLOW_AGENT_ERROR_MEGA_DUMP_FORMAT_VXLAN] = {HINIC3_FLOWS, "error_mega_dump_format_vxlan_error"},
    [HINIC3_FLOW_AGENT_ERROR_MEGA_DUMP_FORMAT_ETH] = {HINIC3_FLOWS, "error_mega_dump_format_eth_error"},
    [HINIC3_FLOW_AGENT_ERROR_MEGA_DUMP_FORMAT_ITEM_TYPE] = {HINIC3_FLOWS, "error_mega_dump_format_item_type_error"},
    [HINIC3_FLOW_AGENT_ERROR_MEGA_DUMP_FORMAT_ACTION_TYPE] = {HINIC3_FLOWS, "error_mega_dump_format_action_type_error"},
    [HINIC3_FLOW_AGENT_ERROR_MEGA_DUMP_FORMAT_SET_SMAC] = {HINIC3_FLOWS, "error_mega_dump_format_set_smac_error"},
    [HINIC3_FLOW_AGENT_ERROR_MEGA_DUMP_FORMAT_SET_DMAC] = {HINIC3_FLOWS, "error_mega_dump_format_set_dmac_error"},
    [HINIC3_FLOW_AGENT_ERROR_MEGA_DUMP_FORMAT_OUTPORT] = {HINIC3_FLOWS, "error_mega_dump_format_outport_error"},
    [HINIC3_FLOW_AGENT_ERROR_DPHASH_FLOW_OFFLOAD] = {HINIC3_FLOWS, "error_dphash_flow_offload"},
    [HINIC3_FLOW_AGENT_ERROR_CALLBACK_DPHASH_FLOW_PUT] = {HINIC3_FLOWS, "error_callback_dphash_process_flow_put_error"},
    [HINIC3_FLOW_AGENT_ERROR_CALLBACK_FLOW_PUT_INFO] = {HINIC3_FLOWS,
    "error_callback_emc_offload_process_flow_put_error"},
    [HINIC3_FLOW_AGENT_ERROR_CALLBACK_MODIFY_FLOW_PUT] = {HINIC3_FLOWS,
    "error_callback_emc_modify_process_flow_put_error"},
    [HINIC3_FLOW_AGENT_ERROR_CALLBACK_SET_UFID_IN_RTE_FLOW] = {HINIC3_FLOWS,
    "error_callback_emc_offload__set_ufid_error"},
    [HINIC3_FLOW_AGENT_ERROR_CALLBACK_DP_HASH_SET_UFID_FLOW] =
    {HINIC3_FLOWS, "error_callback_dp_hash_set_ufid_in_ret_flow"},
    [HINIC3_FLOW_AGENT_ERROR_METER_TYPE] = {HINIC3_FLOWS, "error_meter_type_error"},
    [HINIC3_FLOW_AGENT_ERROR_METER_ACTION] = {HINIC3_FLOWS, "error_meter_action_error"},
    [HINIC3_FLOW_AGENT_ERROR_METER_NOT_FOUND] = {HINIC3_FLOWS, "error_meter_id_not_found_error"},
    [HINIC3_FLOW_AGENT_ERROR_NEXT_METER_NOT_FOUND] = {HINIC3_FLOWS, "error_next_meter_id_not_found"},
    [HINIC3_FLOW_AGENT_ERROR_SET_FLOW_METER_NOT_FOUND] = {HINIC3_FLOWS, "error_set_flow_meter_id_not_found"},
    [HINIC3_FLOW_AGENT_ERROR_SET_FLOW_NEXT_METER_NOT_FOUND] =
    {HINIC3_FLOWS, "error_set_flow_next_meter_id_not_found"},
    [HINIC3_FLOW_AGENT_ERROR_FLOW_QOS_FULL] = {HINIC3_FLOWS, "error_flow_qos_full"},
    [HINIC3_FLOW_AGENT_ERROR_FLOW_SET_QOS_HOVS] = {HINIC3_FLOWS, "error_flow_qos_set_hovs_error"},
    [HINIC3_FLOW_AGENT_ERROR_FLOW_DEL_QOS_HOVS] = {HINIC3_FLOWS, "error_flow_qos_del_hovs_error"},
    [HINIC3_FLOW_AGENT_ERROR_FLOW_DEL_NEXT_QOS_HOVS] = {HINIC3_FLOWS, "error_next_flow_qos_del_hovs_error"},
    [HINIC3_FLOW_AGENT_ERROR_FLOW_QOS_PACKET_MODE] = {HINIC3_FLOWS, "error_flow_qos_packet_mode_error"},
    [HINIC3_FLOW_AGENT_ERROR_SAMPLE_ACTION] = {HINIC3_FLOWS, "error_sample_action_error"},
    [HINIC3_FLOW_AGENT_ERROR_SAMPLE_ACTION_NULL] = {HINIC3_FLOWS, "error_sample_action_null"},
    [HINIC3_FLOW_AGENT_ERROR_MEGA_SAMPLE_ACTION_NULL] = {HINIC3_FLOWS, "error_mega_sample_action_null"},
    [HINIC3_FLOW_AGENT_ERROR_MEGA_SAMPLE_ACTION] = {HINIC3_FLOWS, "error_mega_sample_action_error"},
    [HINIC3_FLOW_AGENT_ERROR_SAMPLE_PORT_ID_NULL] = {HINIC3_FLOWS, "error_sample_port_id_null"},
    [HINIC3_FLOW_AGENT_ERROR_SAMPLE_PORT_ID] = {HINIC3_FLOWS, "error_sample_port_id_error"},
    [HINIC3_FLOW_AGENT_ERROR_SAMPLE_ACTION_SET] = {HINIC3_FLOWS, "error_sample_set_action_error"},
    [HINIC3_FLOW_AGENT_ERROR_SAMPLE_VXLAN_ENCAP] = {HINIC3_FLOWS, "error_sample_vxlan_encap_error"},
    [HINIC3_FLOW_AGENT_ERROR_MEGA_SAMPLE_VXLAN_ENCAP] = {HINIC3_FLOWS, "error_mega_sample_vxlan_encap_error"},
    [HINIC3_FLOW_AGENT_ERROR_SAMPLE_UDP_ITEM_NULL] = {HINIC3_FLOWS, "error_sample_udp_item_null"},
    [HINIC3_FLOW_AGENT_ERROR_MEGA_SAMPLE_UDP_ITEM_NULL] = {HINIC3_FLOWS, "error_mega_sample_udp_item_null"},
    [HINIC3_FLOW_AGENT_ERROR_SAMPLE_VXLAN_ITEM_NULL] = {HINIC3_FLOWS, "error_sample_vxlan_item_null"},
    [HINIC3_FLOW_AGENT_ERROR_MEGA_SAMPLE_VXLAN_ITEM_NULL] = {HINIC3_FLOWS, "error_mega_sample_vxlan_item_null"},
    [HINIC3_FLOW_AGENT_ERROR_SAMPLE_IPV6_ITEM_NULL] = {HINIC3_FLOWS, "error_sample_ipv6_item_null"},
    [HINIC3_FLOW_AGENT_ERROR_MEGA_SAMPLE_IPV6_ITEM_NULL] = {HINIC3_FLOWS, "error_sample_ipv6_item_null"},
    [HINIC3_FLOW_AGENT_ERROR_SAMPLE_ETH_ITEM_NULL] = {HINIC3_FLOWS, "error_sample_eth_item_null"},
    [HINIC3_FLOW_AGENT_ERROR_MEGA_SAMPLE_ETH_ITEM_NULL] = {HINIC3_FLOWS, "error_sample_eth_item_null"},
    [HINIC3_FLOW_AGENT_ERROR_SAMPLE_VLAN_ITEM_NULL] = {HINIC3_FLOWS, "error_sample_vlan_item_null"},
    [HINIC3_FLOW_AGENT_ERROR_MEGA_SAMPLE_VLAN_ITEM_NULL] = {HINIC3_FLOWS, "error_mega_sample_vlan_item_null"},
    [HINIC3_FLOW_AGENT_ERROR_SAMPLE_IPV4_ITEM_NULL] = {HINIC3_FLOWS, "error_sample_ipv4_item_null"},
    [HINIC3_FLOW_AGENT_ERROR_MEGA_SAMPLE_IPV4_ITEM_NULL] = {HINIC3_FLOWS, "error_mega_sample_ipv4_item_null"},
    [HINIC3_FLOW_AGENT_ERROR_SAMPLE_NVGRE_ITEM_NULL] = {HINIC3_FLOWS, "error_sample_gre_item_null"},
    [HINIC3_FLOW_AGENT_ERROR_MEGA_SAMPLE_NVGRE_ITEM_NULL] = {HINIC3_FLOWS, "error_mega_sample_gre_item_null"},
    [HINIC3_FLOW_AGENT_ERROR_SAMPLE_SHIM_ITEM] = {HINIC3_FLOWS, "error_sample_shim_item_error"},
    [HINIC3_FLOW_AGENT_ERROR_SESSION_FULLED] = {HINIC3_FLOWS, "error_session_fulled"},
    [HINIC3_FLOW_AGENT_ERROR_SESSION_UNUSED] = {HINIC3_FLOWS, "error_session_unused"},
    [HINIC3_FLOW_AGENT_ERROR_SESSION_RATIO] = {HINIC3_FLOWS, "error_session_ratio_out_of_range"},
    [HINIC3_FLOW_AGENT_ERROR_GET_HW_STATS_BY_UFID] = {HINIC3_FLOWS, "error_get_hw_stats_by_ufid"},
    [HINIC3_FLOW_AGENT_ERROR_GET_HW_STATISTICS_BY_UFID] = {HINIC3_FLOWS, "error_get_hw_statistics_by_ufid"},
    [HINIC3_FLOW_AGENT_ERROR_OFFLOAD_CHECK_ACTIONS_MANY_OUTPUT] = {HINIC3_FLOWS, "warning_many_output"},
    [HINIC3_FLOW_AGENT_ERROR_MANY_OUTPUT] = {HINIC3_FLOWS, "warning_many_output"},
    [HINIC3_FLOW_AGENT_ERROR_INPUT_NOT_HIOVS] = {HINIC3_FLOWS, "error_input_port_not_hiovs"},
    [HINIC3_FLOW_AGENT_ERROR_DP_FLOW_NO_VXLAN_ACTION_OFFSET] = {HINIC3_FLOWS, "error_dp_no_vxlan_action_offset"},
    [HINIC3_FLOW_AGENT_ERROR_MEGA_FLOW_NO_VXLAN_ACTION_OFFSET] = {HINIC3_FLOWS, "error_mega_no_vxlan_action_offset"},
    [HINIC3_FLOW_AGENT_ERROR_FLOW_NO_VXLAN_ACTION_OFFSET] = {HINIC3_FLOWS, "error_no_vxlan_action_offset"},
    [HINIC3_FLOW_AGENT_ERROR_WRONG_CREATE_FLOW_TYPE] = {HINIC3_FLOWS, "error_flow_offload_type_error"},
    [HINIC3_FLOW_AGENT_ERROR_WRONG_FLUSH_FLOW_TYPE] = {HINIC3_FLOWS, "error_flow_flush_type_error"},
    [HINIC3_FLOW_AGENT_ERROR_UFID_MAP_MPOOL_FLUSH] = {HINIC3_FLOWS, "error_ufid_map_flush_mpool_error"},
    [HINIC3_FLOW_AGENT_ERROR_GET_TIME_STAMP] = {HINIC3_FLOWS, "error_get_time_stamp_error"},
    [HINIC3_FLOW_AGENT_ERROR_FLOW_FLUSH_ALL_INPUT_NULL] = {HINIC3_FLOWS, "error_flow_flush_input_error"},
    [HINIC3_FLOW_AGENT_ERROR_DUMP_INPUT_CHECK_PTHREAD] = {HINIC3_FLOWS, "error_flow_dump_input_thread_error"},
    [HINIC3_FLOW_AGENT_ERROR_DUMP_INPUT_CHECK_TYPE] = {HINIC3_FLOWS, "error_flow_dump_input_type_error"},
    [HINIC3_FLOW_AGENT_ERROR_DEV_DUMP_NULL] = {HINIC3_FLOWS, "error_eth_flow_dev_dump_error"},
    [HINIC3_FLOW_ERROR_NO_EXIST_FLOW] = {HINIC3_FLOWS, "error_modify_no_exist_flow"},
    [HINIC3_FLOW_ERROR_FLOW_NOT_READY] = {HINIC3_FLOWS, "error_modify_flow_not_ready"},
    [HINIC3_FLOW_ERROR_FLOW_BLOCK_ID_ERR] = {HINIC3_FLOWS, "error_flow_block_id_out_of_range"},
    [HINIC3_FLOW_ERROR_FLOW_BLOCK_VERSION_ERR] = {HINIC3_FLOWS, "error_flow_block_version_out_of_range"},
    [HINIC3_FLOW_ERROR_RX_HW_AGE_ERR] = {HINIC3_FLOWS, "error_flow_dp_info_is_null"},
    [HINIC3_FLOW_ERROR_GET_PORT_ID_ERR_INPUT_NULL] = {HINIC3_FLOWS, "error_get_vport_id_netdev_is_null"},
    [HINIC3_FLOW_ERROR_GET_PORT_ID_ERR_NOMEM] = {HINIC3_FLOWS, "error_get_vport_id_info_is_null"},
    [HINIC3_FLOW_ERROR_GET_PORT_ID_ERR_ID_INVAL] = {HINIC3_FLOWS, "error_get_vport_id_port_id_is_null"},
    [HINIC3_FLOW_ERROR_GET_PORT_ID_ERR_INFO_GET_FAILED] = {HINIC3_FLOWS, "error_get_vport_id_get_port_id_err"},
    [HINIC3_FLOW_ERROR_IS_ETHDEV_ERR_INPUT_NULL] = {HINIC3_FLOWS, "error_is_ethdev_netdev_is_null"},
    [HINIC3_FLOW_ERROR_IS_ETHDEV_ERR_UNKNOWN_TYPE] = {HINIC3_FLOWS, "error_is_ethdev_netdev_type_is_null"},
    [HINIC3_FLOW_ERROR_IS_ETHDEV_ERR_ID_INVAL] = {HINIC3_FLOWS, "error_is_ethdev_port_id_is_null"},
    [HINIC3_FLOW_ERROR_IS_ETHDEV_ERR_NOMEM] = {HINIC3_FLOWS, "error_is_ethdev_info_is_null"},
    [HINIC3_FLOW_ERROR_IS_ETHDEV_ERR_INFO_GET_FAILED] = {HINIC3_FLOWS, "error_is_ethdev_get_port_id_err"},
    [HINIC3_FLOW_ERROR_IS_SUPPORT_OFFLOAD_ERR_INPUT_NULL] = {HINIC3_FLOWS, "error_is_support_offload_netdev_is_null"},
    [HINIC3_FLOW_ERROR_IS_SUPPORT_OFFLOAD_ERR_CLASS_NULL] = {HINIC3_FLOWS, "error_is_support_offload_netdev_type_is_null"},
    [HINIC3_FLOW_ERROR_IS_SUPPORT_OFFLOAD_ERR_TYPE_NULL] = {HINIC3_FLOWS, "error_is_support_offload_port_id_is_null"},
    [HINIC3_FLOW_ERROR_IS_SUPPORT_OFFLOAD_ERR_UNKNOWN_NETDEV] = {HINIC3_FLOWS, "error_is_support_offload_info_is_null"},
    [HINIC3_FLOW_ERROR_FLEXDA_FUZZY_FLOW_COPY_RAW_KEY_ITEM_FAIL] = {HINIC3_FLOWS, "error_flexda_fuzzy_flow_copy_key_failed"},
    [HINIC3_FLOW_ERROR_IS_SUPPORT_OFFLOAD_ERR_ONE] = {HINIC3_FLOWS, "error_is_ethdev_netdev_is_null"},
    [HINIC3_FLOW_ERROR_IS_SUPPORT_OFFLOAD_ERR_TWO] = {HINIC3_FLOWS, "error_is_ethdev_netdev_type_is_null"},
    [HINIC3_FLOW_ERROR_IS_SUPPORT_OFFLOAD_ERR_THREE] = {HINIC3_FLOWS, "error_is_ethdev_port_id_is_null"},
    [HINIC3_FLOW_ERROR_IS_SUPPORT_OFFLOAD_ERR_FOUR] = {HINIC3_FLOWS, "error_is_ethdev_info_is_null"},
    [HINIC3_FLOW_ERROR_GET_PORT_ID_ERR_ONE] = {HINIC3_FLOWS, "error_get_vport_id_netdev_is_null"},
    [HINIC3_FLOW_ERROR_GET_PORT_ID_ERR_TWO] = {HINIC3_FLOWS, "error_get_vport_id_info_is_null"},
    [HINIC3_FLOW_ERROR_GET_PORT_ID_ERR_THREE] = {HINIC3_FLOWS, "error_get_vport_id_port_id_is_null"},
    [HINIC3_FLOW_ERROR_GET_PORT_ID_ERR_FOUR] = {HINIC3_FLOWS, "error_get_vport_id_get_port_id_err"},
    [HINIC3_FLOW_ERROR_IS_ETHDEV_ERR_ONE] = {HINIC3_FLOWS, "error_is_ethdev_netdev_is_null"},
    [HINIC3_FLOW_ERROR_IS_ETHDEV_ERR_TWO] = {HINIC3_FLOWS, "error_is_ethdev_netdev_type_is_null"},
    [HINIC3_FLOW_ERROR_IS_ETHDEV_ERR_THREE] = {HINIC3_FLOWS, "error_is_ethdev_port_id_is_null"},
    [HINIC3_FLOW_ERROR_IS_ETHDEV_ERR_FOUR] = {HINIC3_FLOWS, "error_is_ethdev_info_is_null"},
    [HINIC3_FLOW_ERROR_IS_ETHDEV_ERR_FIVE] = {HINIC3_FLOWS, "error_is_ethdev_get_port_id_err"},
    // OVSOFF
    [HINIC3_FLOW_AGENT_ERROR_IGNORE_CT_FOR_PUSH] = {HINIC3_OVS_FLOW, "warning_ignore_ct_for_push"},
    [HINIC3_FLOW_OVS_ERROR_FLOW_RECIRC_BY_PACKET] = {HINIC3_OVS_FLOW, "error_flow_recirc_by_packet_error"},
    [HINIC3_FLOW_OVS_ERROR_FLOW_GENERATE_BY_PACKET] = {HINIC3_OVS_FLOW, "error_flow_generate_by_packet_error"},
    [HINIC3_FLOW_OVS_ERROR_OFFLOAD_BY_PACKET] = {HINIC3_OVS_FLOW, "error_offload_by_packet_error"},
    // 初始化模块
    [HINIC3_FLOW_AGENT_ERROR_FORWARD_ENGINE_NOT_READY] = {HINIC3_INIT, "error_forward_engine_not_ready"},
    [HINIC3_FLOW_AGENT_ERROR_SET_SET_FORWARD_MODE_NOT_READY] = {HINIC3_INIT, "error_set_forward_mode_not_ready"},
    // 卸载策略模块
    [HINIC3_FLOW_AGENT_STATS_OFFLOAD_DELAY] = {HINIC3_POLICY, "warning_check_offload_delay"},
    [HINIC3_FLOW_AGENT_ERROR_OFFLOAD_LIMITS_TABLE_NUM_EXCEED] =
    {HINIC3_POLICY, "warning_check_reach_hw_limits_table_num_exceed"},
    [HINIC3_FLOW_AGENT_ERROR_OFFLOAD_LIMITS] = {HINIC3_POLICY, "warning_check_reach_hw_limits"},
    [HINIC3_FLOW_AGENT_ERROR_NO_CONN_TABLE] = {HINIC3_POLICY, "warning_check_no_conn_table"},
    [HINIC3_FLOW_AGENT_ERROR_NOT_USER_PERMISSIONS_0] = {HINIC3_POLICY, "warning_check_user_permission_fail"},
    [HINIC3_FLOW_AGENT_ERROR_USER_TABLE_NUM_EXCEED_PERMISSIONS] = {HINIC3_POLICY, "warning_check_user_permission_fail"},
    [HINIC3_FLOW_AGENT_ERROR_NOT_USER_PERMISSION] = {HINIC3_POLICY, "warning_check_user_permission_fail"},
    [HINIC3_FLOW_AGENT_ERROR_DUPLICATE_OFFLOAD] = {HINIC3_POLICY, "warning_check_duplicated_policy_table"},
    [HINIC3_FLOW_AGENT_ERROR_POLICY_ILLEGAL_INPUT] = {HINIC3_POLICY, "error_check_illegal_input"},
    [HINIC3_FLOW_AGENT_ERROR_POLICY_ILLEGAL_OFFLOAD_TIME] = {HINIC3_POLICY, "error_check_illegal_offload_time"},
    [HINIC3_FLOW_OVS_POLICY_CHECK_NO_OFFLOAD] = {HINIC3_POLICY, "warning_check_no_offload"},
    [HINIC3_FLOW_OVS_ERROR_POLICY_REACH_USER_LIMITS] = {HINIC3_POLICY, "warning_check_reach_user_limit"},
    [HINIC3_FLOW_OVS_POLICY_INTERNAL_ERROR] = {HINIC3_POLICY, "error_check_internal_error"},
    [HINIC3_FLOW_AGENT_POLICY_LOW_SPEED_FLOW_DEL] = {HINIC3_POLICY, "warning_low_speed_flow_delete"},
    [HINIC3_FLOW_AGENT_POLICY_LOW_SPEED_ERROR] = {HINIC3_POLICY, "warning_low_speed_flow_block"},
    [HINIC3_FLOW_OVS_POLICY_HASH_TABLE_DEL_FAIL] = {HINIC3_POLICY, "error_del_hash_table_fail"},
    [HINIC3_FLOW_AGENT_ERROR_POLICY_CALLBACK_ILLEGAL_INPUT] = {HINIC3_POLICY, "error_callback_illegal_input"},
    [HINIC3_ERRSTAT_WARNING_POLICY_DUPLICATE_CHECK] = {HINIC3_POLICY, "warning_policy_duplicate_check"},
    [HINIC3_ERRSTAT_WARNING_POLICY_DUPLICATE_TIME1] = {HINIC3_POLICY, "warning_policy_duplicate_time1"},
    [HINIC3_ERRSTAT_WARNING_POLICY_DUPLICATE_TIME2] = {HINIC3_POLICY, "warning_policy_duplicate_time2"},
    [HINIC3_ERRSTAT_ERROR_POLICY_DEL_HW_FLOW_FAIL] = {HINIC3_POLICY, "error_policy_del_hw_flow_fail"},
    [HINIC3_ERRSTAT_ERROR_POLICY_ILLEGAL_DELAY_TIME] = {HINIC3_POLICY, "error_policy_illegal_delay_time"},
    [HINIC3_ERRSTAT_ERROR_POLICY_ILLEGAL_TIME] = {HINIC3_POLICY, "error_policy_illegal_time"},
    [HINIC3_ERRSTAT_WARNING_POLICY_LOW_PPS_DELAY] = {HINIC3_POLICY, "warning_policy_low_pps_delay"},
    // 端口模块
    [HINIC3_FLOW_AGENT_ERROR_OFFLOAD_DISABLE] = {HINIC3_PORTS, "warning_offload_disable"},
    [HINIC3_VPORT_VIRTUAL_QUEUE_RECV_PKTS_DISTRIBUTE_DROP] =
    {HINIC3_PORTS, "error_virtual_queue_recv_pkts_distribute_drop"},
    [HINIC3_VPORT_VF_RX_QUEUE_INVALID] = {HINIC3_PORTS, "error_vf_standard_queue_valid_rxq_invalid"},
    [HINIC3_VPORT_BOND_RX_QUEUE_INVALID] = {HINIC3_PORTS, "error_bond_standard_queue_valid_rxq_invalid"},
    [HINIC3_VPORT_VIRTUAL_RX_QUEUE_INVALID] = {HINIC3_PORTS, "error_vf_virtual_queue_valid_rxq_invalid"},
    [HINIC3_VPORT_VF_TX_QUEUE_INVALID] = {HINIC3_PORTS, "error_vf_standard_queue_valid_txq_invalid"},
    [HINIC3_VPORT_BOND_TX_QUEUE_INVALID] = {HINIC3_PORTS, "error_bond_standard_queue_valid_txq_invalid"},
    [HINIC3_VPORT_DEV_SHARE_UPCALL_PRIVATE_DATA_NULL] = {HINIC3_PORTS, "error_vf_rx_shared_dev_private_data_null"},
    [HINIC3_VPORT_DEV_COMMOM_PORT_PRIVATE_DATA_NULL] = {HINIC3_PORTS, "error_vf_rx_standard_dev_private_data_null"},
    [HINIC3_VPORT_DEV_VF_XMIT_PRIVATE_DATA_NULL] = {HINIC3_PORTS, "error_vf_tx_dev_private_data_null"},
    [HINIC3_VPORT_DEV_BOND_RECV_PRIVATE_DATA_NULL] = {HINIC3_PORTS, "error_bond_rx_dev_private_data_null"},
    [HINIC3_VPORT_DEV_BOND_XMIT_PRIVATE_DATA_NULL] = {HINIC3_PORTS, "error_bond_tx_dev_private_data_null"},
    [HINIC3_FLOW_AGENT_ERROR_FLOW_QUERY_CMCC_INPUT_NULL] = {HINIC3_PORTS, "error_flow_query_input_null"},
    [HINIC3_FLOW_AGENT_ERROR_FLOW_QUERY_INPUT_NULL] = {HINIC3_PORTS, "error_flow_query_input_error"},
    [HINIC3_FLOW_AGENT_ERROR_FLOW_CREATE_DEV_DEV_INPUT_NULL] = {HINIC3_PORTS, "error_flow_offload_input_null"},
    [HINIC3_FLOW_AGENT_ERROR_FLOW_CREATE_ATTR_INPUT_NULL] = {HINIC3_PORTS, "error_flow_offload_input_null"},
    [HINIC3_FLOW_AGENT_ERROR_FLOW_CREATE_RRROR_INPUT_NULL] = {HINIC3_PORTS, "error_flow_offload_input_null"},
    [HINIC3_FLOW_AGENT_ERROR_FLOW_CREATE_INPUT_NULL] = {HINIC3_PORTS, "error_flow_offload_ecology_input_null"},
    [HINIC3_FLOW_AGENT_ERROR_FLOW_DEL_INPUT_NULL] = {HINIC3_PORTS, "error_flow_destroy_input_error"},
    [HINIC3_FLOW_AGENT_ERROR_EMC_FLOW_GET_PORT_ID] = {HINIC3_PORTS, "error_emc_flow_offload_get_port_id_error"},
    [HINIC3_FLOW_AGENT_ERROR_ETH_FLOW_GET_PORT_ID] = {HINIC3_PORTS, "error_flow_create_ecology_get_port_id_error"},
    [HINIC3_FLOW_AGENT_ERROR_UPCALL_PRIORITY] = {HINIC3_PORTS, "error_upcall_priority_error"},
    [HINIC3_FLOW_AGENT_WARNING_BOND_SLAVE_DETECT_PKT] = {HINIC3_PORTS, "warning_bond_slave_detect_pkt"},
    [HINIC3_FLOW_AGENT_ERROR_BOND_SLAVE_DETECT_NO_MATCH] = {HINIC3_PORTS, "error_bond_slave_detect_no_match"},
    // qos模块
    [HINIC3_FLOW_AGENT_ERROR_SET_MULTI_QOS] = {HINIC3_QOS, "error_set_multi_qos_error"},
    // ovs 解包模块
    [HINIC3_FLOW_OVS_ERROR_FLOW_NO_OUTPUT_PORT] = {HINIC3_PACKET_PARSE, "error_ovs_flow_no_output_port"},
    [HINIC3_FLOW_OVS_ERROR_FLOW_MUTIL_OUTPUT_PORT] = {HINIC3_PACKET_PARSE, "error_ovs_flow_mutil_output_port"},
    [HINIC3_FLOW_OVS_ERROR_OFFLOAD_FULLKEY_VPORT_FAIL_1] = {HINIC3_PACKET_PARSE, "error_ovs_offload_fullkey_add_recircle_vport_fail"},
    [HINIC3_FLOW_OVS_ERROR_OFFLOAD_FULLKEY_VPORT_FAIL_2] = {HINIC3_PACKET_PARSE, "error_ovs_offload_fullkey_add_vport_fail"},
    [HINIC3_FLOW_OVS_ERROR_OFFLOAD_PARSE_HDR_FAIL] = {HINIC3_PACKET_PARSE, "error_ovs_offload_parse_hdr_fail"},
    [HINIC3_PACKET_ERROR_MBUF_GET_FIAL] = {HINIC3_PACKET_PARSE, "error_mbuf_get_failed"},
    [HINIC3_PACKET_ERROR_PARSE_ACTION] = {HINIC3_PACKET_PARSE, "error_action_parse_failed"},
    [HINIC3_PACKET_ERROR_PARSE_PKT_EXTRACT] = {HINIC3_PACKET_PARSE, "error_extract_failed"},
    [HINIC3_PACKET_ERROR_PARSE_PKT_INFO] = {HINIC3_PACKET_PARSE, "error_info_get_failed"},
    [HINIC3_PACKET_ERROR_PARSE_HDR_PARSE] = {HINIC3_PACKET_PARSE, "error_hdr_parse_failed"},
    [HINIC3_FLOW_AGENT_ERROR_IP_FRAGMENT] = {HINIC3_PACKET_PARSE, "error_ip_fragment"},
    [HINIC3_FLOW_AGENT_ERROR_IPV6_FRAGMENT] = {HINIC3_PACKET_PARSE, "error_ipv6_fragment"},
    [HINIC3_FLOW_AGENT_ERROR_IPV6_ICMP_NOT_ECHO] = {HINIC3_PACKET_PARSE, "error_ipv6_not_echo"},
    [HINIC3_FLOW_AGENT_ERROR_BROADCAST_PKT] = {HINIC3_PACKET_PARSE, "warning_broadcast_pkt"},
    [HINIC3_FLOW_AGENT_ERROR_NO_PKT_INFO] = {HINIC3_PACKET_PARSE, "error_no_pkt_info"},
    [HINIC3_FLOW_AGENT_ERROR_EXTRACT_PKT] = {HINIC3_PACKET_PARSE, "warning_extract_pkt"},
    [HINIC3_FLOW_AGENT_ERROR_TRANS_KEY] = {HINIC3_PACKET_PARSE, "error_transfer_key"},
    [HINIC3_FLOW_AGENT_ERROR_PROC_OFFLOAD_PKTS_FAILED] = {HINIC3_PACKET_PARSE, "error_proc_offload_pkts_failed"},
    // 命令行模块
    [HINIC3_UFID_MAP_SW_UFID_FORMAT_ERROR] = {HINIC3_CMD, "error_command_parameter"},
    [HINIC3_INCOMPLETE_COMMAND] = {HINIC3_CMD, "warning_incomplete_command"},
    [HINIC3_EXCESSIVE_COMMAND] = {HINIC3_CMD, "warning_excessive_command"},
    //UFID_MAP模块
    [HINIC3_ERRSTAT_ERROR_UFID_HMAP_NODE_FAIL] = {HINIC3_UFID_MAP, "error_ufid_hamp_node_fail"},
    [HINIC3_ERRSTAT_ERROR_GET_HW_ELEMENT_FAIL] = {HINIC3_UFID_MAP, "error_get_hw_element_fail"},
};

void
hinic3_add_error_stats(enum hinic3_errstat_type index, uint32_t count)
{
    long long int error_call_time = hinic3_time_sec();
    long long int error_latest_time = -1;
    long long int time_diff = 0;
    if (index >= HINIC3_ERRSTAT_END || index <= HINIC3_ERRSTAT_START) {
        g_hinic3_agent_error_stats[HINIC3_ERRSTAT_COMMON_ERRSTAT_TYPE_OUTOFBOUND]++;
        error_latest_time = g_error_stats_info[HINIC3_ERRSTAT_COMMON_ERRSTAT_TYPE_OUTOFBOUND].error_latest_log_time;
        time_diff = error_call_time - error_latest_time;
        if (time_diff > HINIC3_ERRSTAT_LOG_MAX_DIFF || error_latest_time == 0) {
            HINIC3_LOG(INFO, AGENT, "error stats: name=%s, count=%llu.",
                g_error_stats_info[HINIC3_ERRSTAT_COMMON_ERRSTAT_TYPE_OUTOFBOUND].string,
                g_hinic3_agent_error_stats[HINIC3_ERRSTAT_COMMON_ERRSTAT_TYPE_OUTOFBOUND]);
            g_error_stats_info[HINIC3_ERRSTAT_COMMON_ERRSTAT_TYPE_OUTOFBOUND].error_latest_log_time = hinic3_time_sec();
        }
        return;
    }

    g_hinic3_agent_error_stats[index] += count;
    error_latest_time = g_error_stats_info[index].error_latest_log_time;
    time_diff = error_call_time - error_latest_time;
    if (time_diff > HINIC3_ERRSTAT_LOG_MAX_DIFF || error_latest_time == 0) {
        HINIC3_LOG(INFO, AGENT, "error stats: name=%s, count=%llu.",
            g_error_stats_info[index].string, g_hinic3_agent_error_stats[index]);
        g_error_stats_info[index].error_latest_log_time = hinic3_time_sec();
    }
}

static inline enum hinic3_errstat_level
hinic3_errstat_get_level_by_string(const char* string)
{
    if (strncmp(HINIC3_ERRSTAT_ERROR_STRING, string, strlen(HINIC3_ERRSTAT_ERROR_STRING)) == 0)
        return HINIC3_ERRSTAT_LEVEL_ERROR;
    else
        return HINIC3_ERRSTAT_LEVEL_WARNING;
}

int
hinic3_get_error_stats(enum hinic3_errstat_type index, struct hinic3_error_stats *stats)
{
    if (stats == NULL) {
        g_hinic3_agent_error_stats[HINIC3_ERRSTAT_COMMON_ERRSTAT_INPUT_NULL]++;
        return -1;
    }
    if (index >= HINIC3_ERRSTAT_END || index <= HINIC3_ERRSTAT_START) {
        g_hinic3_agent_error_stats[HINIC3_ERRSTAT_COMMON_ERRSTAT_TYPE_OUTOFBOUND]++;
        return -1;
    }

    stats->string = g_error_stats_info[index].string;
    stats->module = g_error_stats_info[index].module;
    if (stats->string == NULL || stats->module >= HINIC3_MODULE_MAX || stats->module < 0) {
        g_hinic3_agent_error_stats[HINIC3_ERRSTAT_COMMON_ERRSTAT_TYPE_NO_INFO]++;
        return -1;
    }
    stats->level = hinic3_errstat_get_level_by_string(stats->string);
    stats->count = g_hinic3_agent_error_stats[index];
    return 0;
}
