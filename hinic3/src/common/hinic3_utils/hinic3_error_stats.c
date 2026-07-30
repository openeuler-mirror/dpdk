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
    [HINIC3_COMMON_ERROR_ERRSTAT_TYPE_OUT_OF_BOUND] = {HINIC3_COMMON_RESOURCE, "error_errstat_type_out_of_bound"},
    [HINIC3_COMMON_ERROR_ERRSTAT_TYPE_NO_INFO] = {HINIC3_COMMON_RESOURCE, "error_errstat_type_no_info"},
    [HINIC3_COMMON_ERROR_ERRSTAT_INPUT_NULL] = {HINIC3_COMMON_RESOURCE, "error_errstat_input_null"},
    // CT 卸载模块
    [HINIC3_CT_OFFLOAD_ERROR_CT_NOT_READY_TO_OFFLOAD] = {HINIC3_CT_OFFLOAD, "error_ct_not_ready_to_offload"},
    [HINIC3_CT_OFFLOAD_ERROR_CT_CHECK_FAIL] = {HINIC3_CT_OFFLOAD, "error_ct_check_fail"},
    [HINIC3_CT_OFFLOAD_WARNING_CT_NOT_READY] = {HINIC3_CT_OFFLOAD, "warning_ct_not_ready"},
    [HINIC3_CT_OFFLOAD_ERROR_CLEAN_ID_NOT_FOUND] = {HINIC3_CT_OFFLOAD, "error_clean_id_not_found"},
    [HINIC3_CT_OFFLOAD_ERROR_CT_CHECK_CONN_FAILED] = {HINIC3_CT_OFFLOAD, "error_ct_check_conn_failed"},
    [HINIC3_CT_OFFLOAD_ERROR_GET_INFO_ID_NOT_FOUND] = {HINIC3_CT_OFFLOAD, "error_get_info_id_not_found"},
    [HINIC3_CT_OFFLOAD_ERROR_CHECK_ALG_CTRL] = {HINIC3_CT_OFFLOAD, "error_check_alg_ctrl"},
    [HINIC3_CT_OFFLOAD_ERROR_RECIRCLED_NON_TCP] = {HINIC3_CT_OFFLOAD, "error_recircled_non_tcp"},
    [HINIC3_CT_OFFLOAD_ERROR_GET_EXIST_CONN_NOT_FOUND] = {HINIC3_CT_OFFLOAD, "error_get_exist_conn_not_found"},
    [HINIC3_CT_OFFLOAD_WARNING_CT_NOT_CONNECT] = {HINIC3_CT_OFFLOAD, "warning_ct_not_connect"},
    [HINIC3_CT_OFFLOAD_ERROR_ADD_CT_NAT_ACTION_SEARCH_FAIL] = {HINIC3_CT_OFFLOAD, "error_add_ct_nat_action_search_fail"},
    [HINIC3_CT_OFFLOAD_ERROR_CT_SEARCH_FAIL] = {HINIC3_CT_OFFLOAD, "error_ct_search_fail"},
    [HINIC3_CT_OFFLOAD_WARNING_SET_CT_NOT_NAT] = {HINIC3_CT_OFFLOAD, "warning_set_ct_not_nat"},
    [HINIC3_CT_OFFLOAD_WARNING_ADD_CT_NOT_NAT] = {HINIC3_CT_OFFLOAD, "warning_add_ct_not_nat"},
    [HINIC3_CT_OFFLOAD_ERROR_CT_REVERT_CONN_IS_NULL] = {HINIC3_CT_OFFLOAD, "error_ct_revert_conn_is_null"},
    [HINIC3_CT_OFFLOAD_ERROR_CT_REVERT_CONN_IS_NOT_NAT] = {HINIC3_CT_OFFLOAD, "error_ct_revert_conn_is_not_nat"},
    [HINIC3_CT_OFFLOAD_ERROR_INSERT_CT_ACTION_FAIL] = {HINIC3_CT_OFFLOAD, "error_insert_ct_action_fail"},
    [HINIC3_CT_OFFLOAD_ERROR_IGNORE_CT_FOR_PUSH] = {HINIC3_CT_OFFLOAD, "error_ignore_ct_for_push"},
    [HINIC3_CT_OFFLOAD_ERROR_CALLBACK_GET_CONN_BY_KEY_FAIL] = {HINIC3_CT_OFFLOAD, "error_callback_get_conn_by_key_fail"},
    [HINIC3_CT_OFFLOAD_ERROR_CALLBACK_OFFLOAD_FAIL] = {HINIC3_CT_OFFLOAD, "error_callback_offload_fail"},
    [HINIC3_CT_OFFLOAD_ERROR_CALLBACK_SEARCH_CONN_FAIL] = {HINIC3_CT_OFFLOAD, "error_callback_search_conn_fail"},
    // 流表模块 - EMC 基础操作
    [HINIC3_FLOWS_ERROR_GLOBAL_CFG_SET_EMC_SAMPLE] = {HINIC3_FLOWS, "error_global_cfg_set_emc_sample"},
    [HINIC3_FLOWS_ERROR_EMC_OFFLOAD_PROCESS_FLOW_KEY_FAIL] = {HINIC3_FLOWS, "error_emc_offload_process_flow_key_fail"},
    [HINIC3_FLOWS_ERROR_EMC_OFFLOAD_PROCESS_FLOW_MASK_FAIL] = {HINIC3_FLOWS, "error_emc_offload_process_flow_mask_fail"},
    [HINIC3_FLOWS_ERROR_EMC_OFFLOAD_MODIFY_PROCESS_FLOW_KEY_FAIL] =
    {HINIC3_FLOWS, "error_emc_offload_modify_process_flow_key_fail"},
    [HINIC3_FLOWS_ERROR_EMC_OFFLOAD_CHECK_OFFLOAD_REPEATED] =
    {HINIC3_FLOWS, "error_emc_offload_check_offload_repeated"},
    [HINIC3_FLOWS_WARNING_EMC_OFFLOAD_CHECK_OFFLOAD_EXIT_GAP] = {HINIC3_FLOWS, "warning_emc_offload_check_offload_exit_gap"},
    [HINIC3_FLOWS_ERROR_EMC_OFFLOAD_CHECK_OFFLOAD_ONGOING] =
    {HINIC3_FLOWS, "error_emc_offload_check_offload_ongoing"},
    [HINIC3_FLOWS_ERROR_EMC_OFFLOAD_INSERT_INTO_HMAP_FAIL] = {HINIC3_FLOWS, "error_emc_offload_insert_into_hmap_fail"},
    [HINIC3_FLOWS_ERROR_EMC_OFFLOAD_PARSE_FLOW_ACTION] = {HINIC3_FLOWS, "error_emc_offload_parse_flow_action"},
    [HINIC3_FLOWS_ERROR_EMC_OFFLOAD_MODIFY_PARSE_FLOW_ACTION] =
    {HINIC3_FLOWS, "error_emc_offload_modify_parse_flow_action"},
    [HINIC3_FLOWS_ERROR_EMC_OFFLOAD_CALL_HARDWARE_FUNC] = {HINIC3_FLOWS, "error_emc_offload_call_hardware_func"},
    [HINIC3_FLOWS_ERROR_EMC_OFFLOAD_MODIFY_CALL_HARDWARE_FUNC] =
    {HINIC3_FLOWS, "error_emc_offload_modify_call_hardware_func"},
    // 流表模块 - FLOW 删除
    [HINIC3_FLOWS_ERROR_AGE_EMC_DEL_FLOW_IN_HMAP] = {HINIC3_FLOWS, "error_age_emc_del_flow_in_hmap"},
    [HINIC3_FLOWS_ERROR_EMC_DEL_FLOW_IN_HMAP] = {HINIC3_FLOWS, "error_emc_del_flow_in_hmap"},
    [HINIC3_FLOWS_ERROR_EMC_DEL_FLOW_QOS] = {HINIC3_FLOWS, "error_emc_del_flow_qos"},
    [HINIC3_FLOWS_ERROR_EMC_DEL_HARD_FLOW] = {HINIC3_FLOWS, "error_emc_del_hard_flow"},
    // 流表模块 - EMC OFFLOAD
    [HINIC3_FLOWS_ERROR_EMC_OFFLOAD] = {HINIC3_FLOWS, "error_emc_offload"},
    [HINIC3_FLOWS_ERROR_EMC_OFFLOAD_FLOW_MEGA_FLOW_ALLOC] = {HINIC3_FLOWS, "error_emc_offload_flow_mega_flow_alloc"},
    [HINIC3_FLOWS_ERROR_EMC_OFFLOAD_FLOW_CREATE_ECOLOGY_ALLOC] =
    {HINIC3_FLOWS, "error_emc_offload_flow_create_ecology_alloc"},
    [HINIC3_FLOWS_ERROR_EMC_OFFLOAD_FLOW_ALLOC] = {HINIC3_FLOWS, "error_emc_offload_flow_alloc"},
    [HINIC3_FLOWS_ERROR_EMC_OFFLOAD_AGENT_NO_INIT] = {HINIC3_FLOWS, "error_emc_offload_agent_no_init"},
    [HINIC3_FLOWS_ERROR_EMC_ETH_FLOW_CREATE_ECOLOGY_NO_INIT] =
    {HINIC3_FLOWS, "error_emc_eth_flow_create_ecology_no_init"},
    [HINIC3_FLOWS_ERROR_EMC_FLOW_NOT_OFFLOAD] = {HINIC3_FLOWS, "error_emc_flow_not_offload"},
    // 流表模块 - SESSION 删除
    [HINIC3_FLOWS_ERROR_EMC_ACL_DEL_FLOW_IN_SESSION] = {HINIC3_FLOWS, "error_emc_acl_del_flow_in_session"},
    [HINIC3_FLOWS_ERROR_EMC_AGE_DEL_FLOW_IN_SESSION] = {HINIC3_FLOWS, "error_emc_age_del_flow_in_session"},
    [HINIC3_FLOWS_ERROR_EMC_CMCC_DEL_FLOW_IN_SESSION] = {HINIC3_FLOWS, "error_emc_cmcc_del_flow_in_session"},
    [HINIC3_FLOWS_ERROR_EMC_MODIFY_DEL_FLOW_IN_SESSION] =
    {HINIC3_FLOWS, "error_emc_modify_del_flow_in_session"},
    [HINIC3_FLOWS_ERROR_EMC_MEGA_DEL_FLOW_IN_SESSION] = {HINIC3_FLOWS, "error_emc_mega_del_flow_in_session"},
    // 流表模块 - FLUSH
    [HINIC3_FLOWS_ERROR_EMC_FLUSH_HARD_FLOW] = {HINIC3_FLOWS, "error_emc_flush_hard_flow"},
    [HINIC3_FLOWS_ERROR_EMC_FLUSH_MPOOL] = {HINIC3_FLOWS, "error_emc_flush_mpool"},
    [HINIC3_FLOWS_ERROR_EMC_FLUSH_MIRROR_SESSION] = {HINIC3_FLOWS, "error_emc_flush_mirror_session"},
    [HINIC3_FLOWS_ERROR_EMC_FLUSH_MEGA_MIRROR_SESSION] =
    {HINIC3_FLOWS, "error_emc_flush_mega_mirror_session"},
    // 流表模块 - EMC DUMP
    [HINIC3_FLOWS_ERROR_EMC_DUMP_START] = {HINIC3_FLOWS, "error_emc_dump_start"},
    [HINIC3_FLOWS_ERROR_EMC_DUMP_START_HARD] = {HINIC3_FLOWS, "error_emc_dump_start_hard"},
    [HINIC3_FLOWS_ERROR_EMC_DUMP_DONE] = {HINIC3_FLOWS, "error_emc_dump_done"},
    [HINIC3_FLOWS_ERROR_EMC_DUMP_DONE_HARD] = {HINIC3_FLOWS, "error_emc_dump_done_hard"},
    [HINIC3_FLOWS_ERROR_EMC_DUMP_NEXT] = {HINIC3_FLOWS, "error_emc_dump_next"},
    [HINIC3_FLOWS_ERROR_EMC_DUMP_NEXT_MEM] = {HINIC3_FLOWS, "error_emc_dump_next_mem"},
    // 流表模块 - ACL
    [HINIC3_FLOWS_ERROR_ACL_FLOW_HARDWARE_DEL] = {HINIC3_FLOWS, "error_acl_flow_hardware_del"},
    [HINIC3_FLOWS_ERROR_ACL_FLOW_TABLE_DEL] = {HINIC3_FLOWS, "error_acl_flow_table_del"},
    [HINIC3_FLOWS_ERROR_ACL_FLOW_OFFLOAD] = {HINIC3_FLOWS, "error_acl_flow_offload"},
    [HINIC3_FLOWS_ERROR_ACL_DUMP_START] = {HINIC3_FLOWS, "error_acl_dump_start"},
    [HINIC3_FLOWS_ERROR_ACL_DUMP_START_HARD] = {HINIC3_FLOWS, "error_acl_dump_start_hard"},
    [HINIC3_FLOWS_ERROR_ACL_DUMP_DONE] = {HINIC3_FLOWS, "error_acl_dump_done"},
    [HINIC3_FLOWS_ERROR_ACL_DUMP_NEXT] = {HINIC3_FLOWS, "error_acl_dump_next"},
    [HINIC3_FLOWS_ERROR_ACL_DUMP_NEXT_HARD] = {HINIC3_FLOWS, "error_acl_dump_next_hard"},
    [HINIC3_FLOWS_ERROR_ACL_FLOW_QUERY_INPUT_NULL] = {HINIC3_FLOWS, "error_acl_flow_query_input_null"},
    [HINIC3_FLOWS_ERROR_ACL_FLOW_QUERY_DUPLICATE_MEM] = {HINIC3_FLOWS, "error_acl_flow_query_duplicate_mem"},
    [HINIC3_FLOWS_ERROR_ACL_FLOW_DELETE_DUPLICATE_MEM] = {HINIC3_FLOWS, "error_acl_flow_delete_duplicate_mem"},
    // 流表模块 - DP DUMP
    [HINIC3_FLOWS_ERROR_DP_DUMP_START] = {HINIC3_FLOWS, "error_dp_dump_start"},
    [HINIC3_FLOWS_ERROR_DP_DUMP_DONE] = {HINIC3_FLOWS, "error_dp_dump_done"},
    [HINIC3_FLOWS_ERROR_DP_DUMP_NEXT] = {HINIC3_FLOWS, "error_dp_dump_next"},
    [HINIC3_FLOWS_ERROR_DP_DUMP_FORMAT] = {HINIC3_FLOWS, "error_dp_dump_format"},
    [HINIC3_FLOWS_ERROR_DP_DUMP_ITEM_BUILD_FAIL] = {HINIC3_FLOWS, "error_dp_dump_item_build_fail"},
    [HINIC3_FLOWS_ERROR_DP_DUMP_ACTION_BUILD_FAIL] = {HINIC3_FLOWS, "error_dp_dump_action_build_fail"},
    [HINIC3_FLOWS_ERROR_DP_DUMP_PORT_SESSION_ID_ERR] = {HINIC3_FLOWS, "error_dp_dump_port_session_id_err"},
    // 流表模块 - MEGA DUMP
    [HINIC3_FLOWS_ERROR_MEGA_DUMP_START] = {HINIC3_FLOWS, "error_mega_dump_start"},
    [HINIC3_FLOWS_ERROR_MEGA_DUMP_START_HARD] = {HINIC3_FLOWS, "error_mega_dump_start_hard"},
    [HINIC3_FLOWS_ERROR_MEGA_DUMP_DONE] = {HINIC3_FLOWS, "error_mega_dump_done"},
    [HINIC3_FLOWS_ERROR_MEGA_DUMP_DONE_HARD] = {HINIC3_FLOWS, "error_mega_dump_done_hard"},
    [HINIC3_FLOWS_ERROR_MEGA_DUMP_NEXT] = {HINIC3_FLOWS, "error_mega_dump_next"},
    [HINIC3_FLOWS_ERROR_MEGA_DUMP_NEXT_HARD] = {HINIC3_FLOWS, "error_mega_dump_next_hard"},
    [HINIC3_FLOWS_ERROR_MEGA_DUMP_ACTION_TYPE] = {HINIC3_FLOWS, "error_mega_dump_action_type"},
    [HINIC3_FLOWS_ERROR_MEGA_DUMP_ITEM_TYPE] = {HINIC3_FLOWS, "error_mega_dump_item_type"},
    [HINIC3_FLOWS_ERROR_MEGA_DUMP_INPORT_ITEM] = {HINIC3_FLOWS, "error_mega_dump_inport_item"},
    [HINIC3_FLOWS_ERROR_MEGA_DUMP_OUTPORT_ITEM] = {HINIC3_FLOWS, "error_mega_dump_outport_item"},
    [HINIC3_FLOWS_ERROR_MEGA_DUMP_FORMAT_INPORT] = {HINIC3_FLOWS, "error_mega_dump_format_inport"},
    [HINIC3_FLOWS_ERROR_MEGA_DUMP_FORMAT_VLAN] = {HINIC3_FLOWS, "error_mega_dump_format_vlan"},
    [HINIC3_FLOWS_ERROR_MEGA_DUMP_FORMAT_VXLAN] = {HINIC3_FLOWS, "error_mega_dump_format_vxlan"},
    [HINIC3_FLOWS_ERROR_MEGA_DUMP_FORMAT_ETH] = {HINIC3_FLOWS, "error_mega_dump_format_eth"},
    [HINIC3_FLOWS_ERROR_MEGA_DUMP_FORMAT_ITEM_TYPE] = {HINIC3_FLOWS, "error_mega_dump_format_item_type"},
    [HINIC3_FLOWS_ERROR_MEGA_DUMP_FORMAT_ACTION_TYPE] = {HINIC3_FLOWS, "error_mega_dump_format_action_type"},
    [HINIC3_FLOWS_ERROR_MEGA_DUMP_FORMAT_SET_SMAC] = {HINIC3_FLOWS, "error_mega_dump_format_set_smac"},
    [HINIC3_FLOWS_ERROR_MEGA_DUMP_FORMAT_SET_DMAC] = {HINIC3_FLOWS, "error_mega_dump_format_set_dmac"},
    [HINIC3_FLOWS_ERROR_MEGA_DUMP_FORMAT_OUTPORT] = {HINIC3_FLOWS, "error_mega_dump_format_outport"},
    // 流表模块 - MEGA OFFLOAD
    [HINIC3_FLOWS_ERROR_MEGA_FLOW_OFFLOAD] = {HINIC3_FLOWS, "error_mega_flow_offload"},
    [HINIC3_FLOWS_ERROR_MEGA_FLOW_OFFLOAD_REPEATED] = {HINIC3_FLOWS, "error_mega_flow_offload_repeated"},
    [HINIC3_FLOWS_ERROR_MEGA_FLOW_OFFLOAD_DRV_CALL] = {HINIC3_FLOWS, "error_mega_flow_offload_drv_call"},
    [HINIC3_FLOWS_ERROR_MEGA_OFFLOAD_ETH_ITEM] = {HINIC3_FLOWS, "error_mega_offload_eth_item"},
    [HINIC3_FLOWS_ERROR_MEGA_OFFLOAD_VLAN_ITEM] = {HINIC3_FLOWS, "error_mega_offload_vlan_item"},
    [HINIC3_FLOWS_ERROR_MEGA_OFFLOAD_INPORT_ITEM] = {HINIC3_FLOWS, "error_mega_offload_inport_item"},
    [HINIC3_FLOWS_ERROR_MEGA_OFFLOAD_VXLAN_ITEM] = {HINIC3_FLOWS, "error_mega_offload_vxlan_item"},
    [HINIC3_FLOWS_ERROR_MEGA_OFFLOAD_OUTPORT_ACTION] = {HINIC3_FLOWS, "error_mega_offload_outport_action"},
    [HINIC3_FLOWS_ERROR_MEGA_OFFLOAD_SET_SMAC_ACTION] = {HINIC3_FLOWS, "error_mega_offload_set_smac_action"},
    [HINIC3_FLOWS_ERROR_MEGA_OFFLOAD_SET_DMAC_ACTION] = {HINIC3_FLOWS, "error_mega_offload_set_dmac_action"},
    [HINIC3_FLOWS_ERROR_MEGA_OFFLOAD_VXLAN_ENCAP_ACTION] = {HINIC3_FLOWS, "error_mega_offload_vxlan_encap_action"},
    [HINIC3_FLOWS_ERROR_MEGA_OFFLOAD_OUTPORT_WRONG] = {HINIC3_FLOWS, "error_mega_offload_outport_wrong"},
    // 流表模块 - MEGA FLOW 操作
    [HINIC3_FLOWS_ERROR_MEGA_MPOOL_FLUSH] = {HINIC3_FLOWS, "error_mega_mpool_flush"},
    [HINIC3_FLOWS_ERROR_MEGA_FLOW_FLUSH] = {HINIC3_FLOWS, "error_mega_flow_flush"},
    [HINIC3_FLOWS_ERROR_MEGA_FLOW_QUERY] = {HINIC3_FLOWS, "error_mega_flow_query"},
    [HINIC3_FLOWS_ERROR_MEGA_FLOW_QUERY_INPUT_NULL] = {HINIC3_FLOWS, "error_mega_flow_query_input_null"},
    [HINIC3_FLOWS_ERROR_MEGA_FLOW_DELETE] = {HINIC3_FLOWS, "error_mega_flow_delete"},
    [HINIC3_FLOWS_ERROR_MEGA_FLOW_KEY_PARSE] = {HINIC3_FLOWS, "error_mega_flow_key_parse"},
    [HINIC3_FLOWS_ERROR_MEGA_FLOW_ACTION_PARSE] = {HINIC3_FLOWS, "error_mega_flow_action_parse"},
    // 流表模块 - DPHASH
    [HINIC3_FLOWS_ERROR_DPHASH_FLOW_OFFLOAD] = {HINIC3_FLOWS, "error_dphash_flow_offload"},
    [HINIC3_FLOWS_ERROR_DPHASH_FLOW_DELETE_FROM_SW_FAIL] = {HINIC3_FLOWS, "error_dphash_flow_delete_from_sw_fail"},
    [HINIC3_FLOWS_ERROR_DPHASH_FLOW_DELETE_FROM_HW_FAIL] = {HINIC3_FLOWS, "error_dphash_flow_delete_from_hw_fail"},
    [HINIC3_FLOWS_ERROR_DPHASH_FLOW_GET_BY_KEY_FAIL] = {HINIC3_FLOWS, "error_dphash_flow_get_by_key_fail"},
    [HINIC3_FLOWS_ERROR_DPHASH_FLOW_EXIST] = {HINIC3_FLOWS, "error_dphash_flow_exist"},
    [HINIC3_FLOWS_ERROR_DPHASH_FLOW_INSERT_HMAP_FAIL] = {HINIC3_FLOWS, "error_dphash_flow_insert_hmap_fail"},
    [HINIC3_FLOWS_ERROR_DPHASH_FLOW_EXIST_WHILE_HW_NOT] = {HINIC3_FLOWS, "error_dphash_flow_exist_while_hw_not"},
    [HINIC3_FLOWS_ERROR_DPHASH_FLOW_PARSE_KEY_FAIL] = {HINIC3_FLOWS, "error_dphash_flow_parse_key_fail"},
    [HINIC3_FLOWS_ERROR_DPHASH_FLOW_PARSE_ACTION_FAIL] = {HINIC3_FLOWS, "error_dphash_flow_parse_action_fail"},
    [HINIC3_FLOWS_ERROR_DPHASH_FLOW_MGMT_PUT_FAIL] = {HINIC3_FLOWS, "error_dphash_flow_mgmt_put_fail"},
    [HINIC3_FLOWS_ERROR_DPHASH_FLOW_DELETE_SW_BY_KEY_GET_FAIL] = {HINIC3_FLOWS, "error_dphash_flow_delete_sw_by_key_get_fail"},
    [HINIC3_FLOWS_ERROR_CALLBACK_DPHASH_FLOW_PUT] = {HINIC3_FLOWS, "error_callback_dphash_flow_put"},
    [HINIC3_FLOWS_ERROR_CALLBACK_DP_HASH_SET_UFID_FLOW] =
    {HINIC3_FLOWS, "error_callback_dp_hash_set_ufid_flow"},
    // 流表模块 - CALLBACK
    [HINIC3_FLOWS_ERROR_CALLBACK_FLOW_PUT_INFO] = {HINIC3_FLOWS,
    "error_callback_flow_put_info"},
    [HINIC3_FLOWS_ERROR_CALLBACK_MODIFY_FLOW_PUT] = {HINIC3_FLOWS,
    "error_callback_modify_flow_put"},
    [HINIC3_FLOWS_ERROR_CALLBACK_SET_UFID_IN_RTE_FLOW] = {HINIC3_FLOWS,
    "error_callback_set_ufid_in_rte_flow"},
    // 流表模块 - METER
    [HINIC3_FLOWS_ERROR_METER_TYPE] = {HINIC3_FLOWS, "error_meter_type"},
    [HINIC3_FLOWS_ERROR_METER_ACTION] = {HINIC3_FLOWS, "error_meter_action"},
    [HINIC3_FLOWS_ERROR_METER_NOT_FOUND] = {HINIC3_FLOWS, "error_meter_not_found"},
    [HINIC3_FLOWS_ERROR_NEXT_METER_NOT_FOUND] = {HINIC3_FLOWS, "error_next_meter_not_found"},
    [HINIC3_FLOWS_ERROR_SET_FLOW_METER_NOT_FOUND] = {HINIC3_FLOWS, "error_set_flow_meter_not_found"},
    [HINIC3_FLOWS_ERROR_SET_FLOW_NEXT_METER_NOT_FOUND] =
    {HINIC3_FLOWS, "error_set_flow_next_meter_not_found"},
    // 流表模块 - QOS
    [HINIC3_FLOWS_ERROR_FLOW_QOS_FULL] = {HINIC3_FLOWS, "error_flow_qos_full"},
    [HINIC3_FLOWS_ERROR_FLOW_SET_QOS_HOVS] = {HINIC3_FLOWS, "error_flow_set_qos_hovs"},
    [HINIC3_FLOWS_ERROR_FLOW_DEL_QOS_HOVS] = {HINIC3_FLOWS, "error_flow_del_qos_hovs"},
    [HINIC3_FLOWS_ERROR_FLOW_DEL_NEXT_QOS_HOVS] = {HINIC3_FLOWS, "error_flow_del_next_qos_hovs"},
    [HINIC3_FLOWS_ERROR_FLOW_QOS_PACKET_MODE] = {HINIC3_FLOWS, "error_flow_qos_packet_mode"},
    // 流表模块 - SAMPLE
    [HINIC3_FLOWS_ERROR_SAMPLE_ACTION] = {HINIC3_FLOWS, "error_sample_action"},
    [HINIC3_FLOWS_ERROR_SAMPLE_ACTION_NULL] = {HINIC3_FLOWS, "error_sample_action_null"},
    [HINIC3_FLOWS_ERROR_MEGA_SAMPLE_ACTION_NULL] = {HINIC3_FLOWS, "error_mega_sample_action_null"},
    [HINIC3_FLOWS_ERROR_MEGA_SAMPLE_ACTION] = {HINIC3_FLOWS, "error_mega_sample_action"},
    [HINIC3_FLOWS_ERROR_SAMPLE_PORT_ID_NULL] = {HINIC3_FLOWS, "error_sample_port_id_null"},
    [HINIC3_FLOWS_ERROR_SAMPLE_PORT_ID] = {HINIC3_FLOWS, "error_sample_port_id"},
    [HINIC3_FLOWS_ERROR_SAMPLE_ACTION_SET] = {HINIC3_FLOWS, "error_sample_action_set"},
    [HINIC3_FLOWS_ERROR_SAMPLE_VXLAN_ENCAP] = {HINIC3_FLOWS, "error_sample_vxlan_encap"},
    [HINIC3_FLOWS_ERROR_MEGA_SAMPLE_VXLAN_ENCAP] = {HINIC3_FLOWS, "error_mega_sample_vxlan_encap"},
    [HINIC3_FLOWS_ERROR_SAMPLE_UDP_ITEM_NULL] = {HINIC3_FLOWS, "error_sample_udp_item_null"},
    [HINIC3_FLOWS_ERROR_MEGA_SAMPLE_UDP_ITEM_NULL] = {HINIC3_FLOWS, "error_mega_sample_udp_item_null"},
    [HINIC3_FLOWS_ERROR_SAMPLE_VXLAN_ITEM_NULL] = {HINIC3_FLOWS, "error_sample_vxlan_item_null"},
    [HINIC3_FLOWS_ERROR_MEGA_SAMPLE_VXLAN_ITEM_NULL] = {HINIC3_FLOWS, "error_mega_sample_vxlan_item_null"},
    [HINIC3_FLOWS_ERROR_SAMPLE_IPV6_ITEM_NULL] = {HINIC3_FLOWS, "error_sample_ipv6_item_null"},
    [HINIC3_FLOWS_ERROR_MEGA_SAMPLE_IPV6_ITEM_NULL] = {HINIC3_FLOWS, "error_mega_sample_ipv6_item_null"},
    [HINIC3_FLOWS_ERROR_SAMPLE_ETH_ITEM_NULL] = {HINIC3_FLOWS, "error_sample_eth_item_null"},
    [HINIC3_FLOWS_ERROR_MEGA_SAMPLE_ETH_ITEM_NULL] = {HINIC3_FLOWS, "error_mega_sample_eth_item_null"},
    [HINIC3_FLOWS_ERROR_SAMPLE_VLAN_ITEM_NULL] = {HINIC3_FLOWS, "error_sample_vlan_item_null"},
    [HINIC3_FLOWS_ERROR_MEGA_SAMPLE_VLAN_ITEM_NULL] = {HINIC3_FLOWS, "error_mega_sample_vlan_item_null"},
    [HINIC3_FLOWS_ERROR_SAMPLE_IPV4_ITEM_NULL] = {HINIC3_FLOWS, "error_sample_ipv4_item_null"},
    [HINIC3_FLOWS_ERROR_MEGA_SAMPLE_IPV4_ITEM_NULL] = {HINIC3_FLOWS, "error_mega_sample_ipv4_item_null"},
    [HINIC3_FLOWS_ERROR_SAMPLE_NVGRE_ITEM_NULL] = {HINIC3_FLOWS, "error_sample_nvgre_item_null"},
    [HINIC3_FLOWS_ERROR_MEGA_SAMPLE_NVGRE_ITEM_NULL] = {HINIC3_FLOWS, "error_mega_sample_nvgre_item_null"},
    [HINIC3_FLOWS_ERROR_SAMPLE_SHIM_ITEM] = {HINIC3_FLOWS, "error_sample_shim_item"},
    // 流表模块 - SESSION
    [HINIC3_FLOWS_ERROR_SESSION_FULLED] = {HINIC3_FLOWS, "error_session_fulled"},
    [HINIC3_FLOWS_ERROR_SESSION_UNUSED] = {HINIC3_FLOWS, "error_session_unused"},
    [HINIC3_FLOWS_ERROR_SESSION_RATIO] = {HINIC3_FLOWS, "error_session_ratio"},
    // 流表模块 - HW_STATS
    [HINIC3_FLOWS_ERROR_GET_HW_STATS_BY_UFID] = {HINIC3_FLOWS, "error_get_hw_stats_by_ufid"},
    // 流表模块 - MANY_OUTPUT
    [HINIC3_FLOWS_WARNING_OFFLOAD_CHECK_ACTIONS_MANY_OUTPUT] = {HINIC3_FLOWS, "warning_offload_check_actions_many_output"},
    [HINIC3_FLOWS_WARNING_MANY_OUTPUT] = {HINIC3_FLOWS, "warning_many_output"},
    // 流表模块 - VXLAN_OFFSET
    [HINIC3_FLOWS_ERROR_DP_FLOW_NO_VXLAN_ACTION_OFFSET] = {HINIC3_FLOWS, "error_dp_flow_no_vxlan_action_offset"},
    [HINIC3_FLOWS_ERROR_MEGA_FLOW_NO_VXLAN_ACTION_OFFSET] = {HINIC3_FLOWS, "error_mega_flow_no_vxlan_action_offset"},
    [HINIC3_FLOWS_ERROR_FLOW_NO_VXLAN_ACTION_OFFSET] = {HINIC3_FLOWS, "error_flow_no_vxlan_action_offset"},
    // 流表模块 - FLOW 类型错误
    [HINIC3_FLOWS_ERROR_WRONG_CREATE_FLOW_TYPE] = {HINIC3_FLOWS, "error_wrong_create_flow_type"},
    [HINIC3_FLOWS_ERROR_WRONG_FLUSH_FLOW_TYPE] = {HINIC3_FLOWS, "error_wrong_flush_flow_type"},
    [HINIC3_FLOWS_ERROR_INPUT_NOT_HIOVS] = {HINIC3_FLOWS, "error_input_not_hiovs"},
    // 流表模块 - UFID/TIME/FLUSH
    [HINIC3_FLOWS_ERROR_UFID_MAP_MPOOL_FLUSH] = {HINIC3_FLOWS, "error_ufid_map_mpool_flush"},
    [HINIC3_FLOWS_ERROR_GET_TIME_STAMP] = {HINIC3_FLOWS, "error_get_time_stamp"},
    [HINIC3_FLOWS_ERROR_FLOW_FLUSH_ALL_INPUT_NULL] = {HINIC3_FLOWS, "error_flow_flush_all_input_null"},
    // 流表模块 - DUMP_INPUT
    [HINIC3_FLOWS_ERROR_DUMP_INPUT_CHECK_PTHREAD] = {HINIC3_FLOWS, "error_dump_input_check_pthread"},
    [HINIC3_FLOWS_ERROR_DUMP_INPUT_CHECK_TYPE] = {HINIC3_FLOWS, "error_dump_input_check_type"},
    [HINIC3_FLOWS_ERROR_DEV_DUMP_NULL] = {HINIC3_FLOWS, "error_dev_dump_null"},
    // 流表模块 - FLOW 基础错误
    [HINIC3_FLOWS_ERROR_NO_EXIST_FLOW] = {HINIC3_FLOWS, "error_no_exist_flow"},
    [HINIC3_FLOWS_ERROR_FLOW_NOT_READY] = {HINIC3_FLOWS, "error_flow_not_ready"},
    [HINIC3_FLOWS_ERROR_FLOW_BLOCK_ID_ERR] = {HINIC3_FLOWS, "error_flow_block_id_err"},
    [HINIC3_FLOWS_ERROR_RX_HW_AGE_ERR] = {HINIC3_FLOWS, "error_rx_hw_age_err"},
    [HINIC3_FLOWS_ERROR_HIGH_PRIORITY_ETH_NULL] = {HINIC3_FLOWS, "error_high_priority_eth_null"},
    [HINIC3_FLOWS_ERROR_HIGH_PRIORITY_IPV6_NULL] = {HINIC3_FLOWS, "error_high_priority_ipv6_null"},
    [HINIC3_FLOWS_ERROR_HIGH_PRIORITY_WRONG_ITEM] = {HINIC3_FLOWS, "error_high_priority_wrong_item"},
    [HINIC3_FLOWS_ERROR_HIGH_PRIORITY_UPCALL_SET_FAIL] = {HINIC3_FLOWS, "error_high_priority_upcall_set_fail"},
    [HINIC3_FLOWS_ERROR_HIGH_PRIORITY_BOND_DEV_NULL] = {HINIC3_FLOWS, "error_high_priority_bond_dev_null"},
    [HINIC3_FLOWS_ERROR_FLOW_FLUSH_DEV_NULL] = {HINIC3_FLOWS, "error_flow_flush_dev_null"},
    [HINIC3_FLOWS_ERROR_FLOW_FLUSH_GET_PORT_ID_FAIL] = {HINIC3_FLOWS, "error_flow_flush_get_port_id_fail"},
    [HINIC3_FLOWS_ERROR_FLOW_FLUSH_BY_PORT_FAIL] = {HINIC3_FLOWS, "error_flow_flush_by_port_fail"},
    // 流表模块 - GET_PORT_ID 错误
    [HINIC3_FLOWS_ERROR_GET_PORT_ID_INPUT_NULL] = {HINIC3_FLOWS, "error_get_port_id_input_null"},
    [HINIC3_FLOWS_ERROR_GET_PORT_ID_NOMEM] = {HINIC3_FLOWS, "error_get_port_id_nomem"},
    [HINIC3_FLOWS_ERROR_GET_PORT_ID_INFO_GET_FAILED] = {HINIC3_FLOWS, "error_get_port_id_info_get_failed"},
    [HINIC3_FLOWS_ERROR_GET_PORT_ID_NETDEV_CLASS_NULL] = {HINIC3_FLOWS, "error_get_port_id_netdev_class_null"},
    [HINIC3_FLOWS_ERROR_GET_PORT_ID_PORT_ID_INVALID] = {HINIC3_FLOWS, "error_get_port_id_port_id_invalid"},
    // 流表模块 - IS_ETHDEV 错误
    [HINIC3_FLOWS_ERROR_IS_ETHDEV_NETDEV_CLASS_NULL] = {HINIC3_FLOWS, "error_is_ethdev_netdev_class_null"},
    [HINIC3_FLOWS_ERROR_IS_ETHDEV_DEV_INFO_ALLOC_FAIL] = {HINIC3_FLOWS, "error_is_ethdev_dev_info_alloc_fail"},
    [HINIC3_FLOWS_ERROR_IS_ETHDEV_DEV_INFO_GET_FAIL] = {HINIC3_FLOWS, "error_is_ethdev_dev_info_get_fail"},
    [HINIC3_FLOWS_ERROR_IS_ETHDEV_TYPE_NOT_DPDK] = {HINIC3_FLOWS, "error_is_ethdev_type_not_dpdk"},
    [HINIC3_FLOWS_ERROR_IS_ETHDEV_PORT_ID_INVALID] = {HINIC3_FLOWS, "error_is_ethdev_port_id_invalid"},
    // 流表模块 - IS_SUPPORT_OFFLOAD 错误
    [HINIC3_FLOWS_ERROR_IS_SUPPORT_OFFLOAD_CLASS_NULL] = {HINIC3_FLOWS, "error_is_support_offload_class_null"},
    [HINIC3_FLOWS_ERROR_IS_SUPPORT_OFFLOAD_TYPE_NULL] = {HINIC3_FLOWS, "error_is_support_offload_type_null"},
    [HINIC3_FLOWS_ERROR_FLEXDA_FUZZY_FLOW_COPY_RAW_KEY_ITEM_FAIL] = {HINIC3_FLOWS, "error_flexda_fuzzy_flow_copy_raw_key_item_fail"},
    [HINIC3_FLOWS_ERROR_IS_SUPPORT_OFFLOAD_NETDEV_NULL] = {HINIC3_FLOWS, "error_is_support_offload_netdev_null"},
    [HINIC3_FLOWS_ERROR_IS_SUPPORT_OFFLOAD_UNSUPPORTED] = {HINIC3_FLOWS, "error_is_support_offload_unsupported"},
    // OVSOFF 模块
    [HINIC3_OVS_FLOW_ERROR_FLOW_RECIRC_BY_PACKET] = {HINIC3_OVS_FLOW, "error_flow_recirc_by_packet"},
    [HINIC3_OVS_FLOW_ERROR_FLOW_GENERATE_BY_PACKET] = {HINIC3_OVS_FLOW, "error_flow_generate_by_packet"},
    [HINIC3_OVS_FLOW_ERROR_OFFLOAD_BY_PACKET] = {HINIC3_OVS_FLOW, "error_offload_by_packet"},
    [HINIC3_OVS_FLOW_ERROR_CREATE_INPUT_NULL] = {HINIC3_OVS_FLOW, "error_create_input_null"},
    [HINIC3_OVS_FLOW_ERROR_PARSE_INFO_FAIL] = {HINIC3_OVS_FLOW, "error_parse_info_fail"},
    [HINIC3_OVS_FLOW_ERROR_INSERT_POLICY_ID_FAIL] = {HINIC3_OVS_FLOW, "error_insert_policy_id_fail"},
    [HINIC3_OVS_FLOW_ERROR_INSERT_MEGA_UFID_FAIL] = {HINIC3_OVS_FLOW, "error_insert_mega_ufid_fail"},
    [HINIC3_OVS_FLOW_ERROR_DESTROY_PARAM_INVALID] = {HINIC3_OVS_FLOW, "error_destroy_param_invalid"},
    [HINIC3_OVS_FLOW_ERROR_DESTROY_FAILED] = {HINIC3_OVS_FLOW, "error_destroy_failed"},
    [HINIC3_OVS_FLOW_ERROR_CONSTRUCT_ETH_FAIL] = {HINIC3_OVS_FLOW, "error_construct_eth_fail"},
    [HINIC3_OVS_FLOW_ERROR_CONSTRUCT_IP_FAIL] = {HINIC3_OVS_FLOW, "error_construct_ip_fail"},
    [HINIC3_OVS_FLOW_ERROR_ALLOC_TCP_ITEM_FAIL] = {HINIC3_OVS_FLOW, "error_alloc_tcp_item_fail"},
    [HINIC3_OVS_FLOW_ERROR_ALLOC_UDP_ITEM_FAIL] = {HINIC3_OVS_FLOW, "error_alloc_udp_item_fail"},
    [HINIC3_OVS_FLOW_ERROR_ALLOC_ICMP_ITEM_FAIL] = {HINIC3_OVS_FLOW, "error_alloc_icmp_item_fail"},
    [HINIC3_OVS_FLOW_ERROR_NOT_SUPPORT_L4_PROTOCOL] = {HINIC3_OVS_FLOW, "error_not_support_l4_protocol"},
    [HINIC3_OVS_FLOW_ERROR_CONSTRUCT_5_TUPLES_FAIL] = {HINIC3_OVS_FLOW, "error_construct_5_tuples_fail"},
    [HINIC3_OVS_FLOW_ERROR_PARSE_RAW_IP_FAIL] = {HINIC3_OVS_FLOW, "error_parse_raw_ip_fail"},
    [HINIC3_OVS_FLOW_ERROR_CONSTRUCT_TUPLES_FAIL] = {HINIC3_OVS_FLOW, "error_construct_tuples_fail"},
    [HINIC3_OVS_FLOW_ERROR_INSERT_ACTION_FAIL] = {HINIC3_OVS_FLOW, "error_insert_action_fail"},
    [HINIC3_OVS_FLOW_ERROR_PACKET_PARSE_INPUT_NULL] = {HINIC3_OVS_FLOW, "error_packet_parse_input_null"},
    // 初始化模块
    [HINIC3_FLOWS_ERROR_FORWARD_ENGINE_NOT_READY] = {HINIC3_FLOWS, "error_forward_engine_not_ready"},
    [HINIC3_FLOWS_ERROR_SET_FORWARD_MODE_NOT_READY] = {HINIC3_FLOWS, "error_set_forward_mode_not_ready"},
    // 卸载策略模块
    [HINIC3_POLICY_WARNING_OFFLOAD_DELAY] = {HINIC3_POLICY, "warning_offload_delay"},
    [HINIC3_POLICY_ERROR_OFFLOAD_LIMITS] = {HINIC3_POLICY, "error_offload_limits"},
    [HINIC3_POLICY_ERROR_NO_CONN_TABLE] = {HINIC3_POLICY, "error_no_conn_table"},
    [HINIC3_POLICY_ERROR_NOT_USER_PERMISSIONS_0] = {HINIC3_POLICY, "error_not_user_permissions_0"},
    [HINIC3_POLICY_ERROR_USER_TABLE_NUM_EXCEED_PERMISSIONS] = {HINIC3_POLICY, "error_user_table_num_exceed_permissions"},
    [HINIC3_POLICY_ERROR_NOT_USER_PERMISSION] = {HINIC3_POLICY, "error_not_user_permission"},
    [HINIC3_POLICY_ERROR_DUPLICATE_OFFLOAD] = {HINIC3_POLICY, "error_duplicate_offload"},
    [HINIC3_POLICY_ERROR_ILLEGAL_INPUT] = {HINIC3_POLICY, "error_illegal_input"},
    [HINIC3_POLICY_ERROR_ILLEGAL_OFFLOAD_TIME] = {HINIC3_POLICY, "error_illegal_offload_time"},
    [HINIC3_POLICY_WARNING_CHECK_NO_OFFLOAD] = {HINIC3_POLICY, "warning_check_no_offload"},
    [HINIC3_POLICY_ERROR_REACH_USER_LIMITS] = {HINIC3_POLICY, "error_reach_user_limits"},
    [HINIC3_POLICY_WARNING_INTERNAL_ERROR] = {HINIC3_POLICY, "warning_internal_error"},
    [HINIC3_POLICY_WARNING_LOW_SPEED_FLOW_DEL] = {HINIC3_POLICY, "warning_low_speed_flow_del"},
    [HINIC3_POLICY_ERROR_LOW_SPEED] = {HINIC3_POLICY, "error_low_speed"},
    [HINIC3_POLICY_WARNING_HASH_TABLE_DEL_FAIL] = {HINIC3_POLICY, "warning_hash_table_del_fail"},
    [HINIC3_POLICY_ERROR_CALLBACK_ILLEGAL_INPUT] = {HINIC3_POLICY, "error_callback_illegal_input"},
    [HINIC3_POLICY_WARNING_DUPLICATE_CHECK] = {HINIC3_POLICY, "warning_duplicate_check"},
    [HINIC3_POLICY_WARNING_TIME_GET_FAILED] = {HINIC3_POLICY, "warning_time_get_failed"},
    [HINIC3_POLICY_WARNING_TIME_BACKWARD] = {HINIC3_POLICY, "warning_time_backward"},
    [HINIC3_POLICY_ERROR_DEL_HW_FLOW_FAIL] = {HINIC3_POLICY, "error_del_hw_flow_fail"},
    [HINIC3_POLICY_ERROR_ILLEGAL_DELAY_TIME] = {HINIC3_POLICY, "error_illegal_delay_time"},
    [HINIC3_POLICY_ERROR_ILLEGAL_TIME] = {HINIC3_POLICY, "error_illegal_time"},
    [HINIC3_POLICY_WARNING_LOW_PPS_DELAY] = {HINIC3_POLICY, "warning_low_pps_delay"},
    // 端口模块
    [HINIC3_PORTS_ERROR_HOVS_RTE_RX_BURST_NULL] = {HINIC3_PORTS, "error_hovs_rte_rx_burst_null"},
    [HINIC3_PORTS_ERROR_HOVS_RTE_TX_BURST_NULL] = {HINIC3_PORTS, "error_hovs_rte_tx_burst_null"},
    [HINIC3_PORTS_ERROR_HOVS_RTE_TX_BURST_QUEUE_ID_OUT_OF_RANGE] =
    {HINIC3_PORTS, "error_hovs_rte_tx_burst_queue_id_out_of_range"},
    [HINIC3_FLOWS_WARNING_OFFLOAD_DISABLE] = {HINIC3_FLOWS, "warning_offload_disable"},
    [HINIC3_PORTS_ERROR_VIRTUAL_QUEUE_RECV_PKTS_DISTRIBUTE_DROP] =
    {HINIC3_PORTS, "error_virtual_queue_recv_pkts_distribute_drop"},
    [HINIC3_PORTS_ERROR_VF_RX_QUEUE_INVALID] = {HINIC3_PORTS, "error_vf_rx_queue_invalid"},
    [HINIC3_PORTS_ERROR_BOND_RX_QUEUE_INVALID] = {HINIC3_PORTS, "error_bond_rx_queue_invalid"},
    [HINIC3_PORTS_ERROR_VIRTUAL_RX_QUEUE_INVALID] = {HINIC3_PORTS, "error_virtual_rx_queue_invalid"},
    [HINIC3_PORTS_ERROR_VF_TX_QUEUE_INVALID] = {HINIC3_PORTS, "error_vf_tx_queue_invalid"},
    [HINIC3_PORTS_ERROR_BOND_TX_QUEUE_INVALID] = {HINIC3_PORTS, "error_bond_tx_queue_invalid"},
    [HINIC3_PORTS_ERROR_DEV_SHARE_UPCALL_PRIVATE_DATA_NULL] = {HINIC3_PORTS, "error_dev_share_upcall_private_data_null"},
    [HINIC3_PORTS_ERROR_DEV_COMMOM_PORT_PRIVATE_DATA_NULL] = {HINIC3_PORTS, "error_dev_commom_port_private_data_null"},
    [HINIC3_PORTS_ERROR_DEV_VF_XMIT_PRIVATE_DATA_NULL] = {HINIC3_PORTS, "error_dev_vf_xmit_private_data_null"},
    [HINIC3_PORTS_ERROR_DEV_BOND_RECV_PRIVATE_DATA_NULL] = {HINIC3_PORTS, "error_dev_bond_recv_private_data_null"},
    [HINIC3_PORTS_ERROR_DEV_BOND_XMIT_PRIVATE_DATA_NULL] = {HINIC3_PORTS, "error_dev_bond_xmit_private_data_null"},
    [HINIC3_FLOWS_ERROR_FLOW_QUERY_CMCC_INPUT_NULL] = {HINIC3_FLOWS, "error_flow_query_cmcc_input_null"},
    [HINIC3_FLOWS_ERROR_FLOW_QUERY_INPUT_NULL] = {HINIC3_FLOWS, "error_flow_query_input_null"},
    [HINIC3_FLOWS_ERROR_FLOW_CREATE_DEV_DEV_INPUT_NULL] = {HINIC3_FLOWS, "error_flow_create_dev_input_null"},
    [HINIC3_FLOWS_ERROR_FLOW_CREATE_ATTR_INPUT_NULL] = {HINIC3_FLOWS, "error_flow_create_attr_input_null"},
    [HINIC3_FLOWS_ERROR_FLOW_CREATE_ERROR_INPUT_NULL] = {HINIC3_FLOWS, "error_flow_create_error_input_null"},
    [HINIC3_FLOWS_ERROR_FLOW_CREATE_INPUT_NULL] = {HINIC3_FLOWS, "error_flow_create_input_null"},
    [HINIC3_FLOWS_ERROR_FLOW_DEL_INPUT_NULL] = {HINIC3_FLOWS, "error_flow_del_input_null"},
    [HINIC3_FLOWS_ERROR_EMC_FLOW_GET_PORT_ID] = {HINIC3_FLOWS, "error_emc_flow_get_port_id"},
    [HINIC3_FLOWS_ERROR_ETH_FLOW_GET_PORT_ID] = {HINIC3_FLOWS, "error_eth_flow_get_port_id"},
    [HINIC3_FLOWS_ERROR_UPCALL_PRIORITY] = {HINIC3_FLOWS, "error_upcall_priority"},
    [HINIC3_FLOWS_WARNING_BOND_SLAVE_DETECT_PKT] = {HINIC3_FLOWS, "warning_bond_slave_detect_pkt"},
    [HINIC3_FLOWS_ERROR_BOND_SLAVE_DETECT_NO_MATCH] = {HINIC3_FLOWS, "error_bond_slave_detect_no_match"},
    // qos模块
    [HINIC3_FLOWS_ERROR_SET_MULTI_QOS] = {HINIC3_FLOWS, "error_set_multi_qos"},
    // ovs 解包模块
    [HINIC3_OVS_FLOW_ERROR_FLOW_NO_OUTPUT_PORT] = {HINIC3_OVS_FLOW, "error_flow_no_output_port"},
    [HINIC3_OVS_FLOW_ERROR_FLOW_MUTIL_OUTPUT_PORT] = {HINIC3_OVS_FLOW, "error_flow_mutil_output_port"},
    [HINIC3_OVS_FLOW_ERROR_OFFLOAD_FULLKEY_ADD_RECIRCLE_VPORT_FAIL] = {HINIC3_OVS_FLOW, "error_offload_fullkey_add_recircle_vport_fail"},
    [HINIC3_OVS_FLOW_ERROR_OFFLOAD_FULLKEY_ADD_VPORT_FAIL] = {HINIC3_OVS_FLOW, "error_offload_fullkey_add_vport_fail"},
    [HINIC3_OVS_FLOW_ERROR_OFFLOAD_PARSE_HDR_FAIL] = {HINIC3_OVS_FLOW, "error_offload_parse_hdr_fail"},
    [HINIC3_OVS_FLOW_ERROR_MBUF_GET_FAIL] = {HINIC3_OVS_FLOW, "error_mbuf_get_fail"},
    [HINIC3_OVS_FLOW_ERROR_PARSE_ACTION] = {HINIC3_OVS_FLOW, "error_parse_action"},
    [HINIC3_OVS_FLOW_ERROR_PARSE_PKT_EXTRACT] = {HINIC3_OVS_FLOW, "error_parse_pkt_extract"},
    [HINIC3_OVS_FLOW_ERROR_PARSE_PKT_INFO] = {HINIC3_OVS_FLOW, "error_parse_pkt_info"},
    [HINIC3_OVS_FLOW_ERROR_PARSE_HDR_PARSE] = {HINIC3_OVS_FLOW, "error_parse_hdr_parse"},
    [HINIC3_OVS_FLOW_ERROR_IP_FRAGMENT] = {HINIC3_OVS_FLOW, "error_ip_fragment"},
    [HINIC3_OVS_FLOW_ERROR_IP_HEADER_LEN_CHECK] = {HINIC3_OVS_FLOW, "error_ip_header_len_check"},
    [HINIC3_OVS_FLOW_ERROR_IPV6_FRAGMENT] = {HINIC3_OVS_FLOW, "error_ipv6_fragment"},
    [HINIC3_OVS_FLOW_ERROR_IPV6_ICMP_NOT_ECHO] = {HINIC3_OVS_FLOW, "error_ipv6_icmp_not_echo"},
    [HINIC3_OVS_FLOW_ERROR_BROADCAST_PKT] = {HINIC3_OVS_FLOW, "error_broadcast_pkt"},
    [HINIC3_OVS_FLOW_ERROR_NO_PKT_INFO] = {HINIC3_OVS_FLOW, "error_no_pkt_info"},
    [HINIC3_OVS_FLOW_ERROR_EXTRACT_PKT] = {HINIC3_OVS_FLOW, "error_extract_pkt"},
    [HINIC3_OVS_FLOW_ERROR_LCORE_IDX_OUT_OF_RANGE] = {HINIC3_OVS_FLOW, "error_lcore_idx_out_of_range"},
    [HINIC3_OVS_FLOW_ERROR_TRANS_KEY] = {HINIC3_OVS_FLOW, "error_trans_key"},
    [HINIC3_OVS_FLOW_ERROR_PROC_OFFLOAD_PKTS_FAILED] = {HINIC3_OVS_FLOW, "error_proc_offload_pkts_failed"},
    [HINIC3_OVS_FLOW_ERROR_OUTPUT_ACTION_NULL] = {HINIC3_OVS_FLOW, "error_output_action_null"},
    [HINIC3_OVS_FLOW_ERROR_VLAN_PCP_NULL] = {HINIC3_OVS_FLOW, "error_vlan_pcp_null"},
    [HINIC3_OVS_FLOW_ERROR_VLAN_VID_NULL] = {HINIC3_OVS_FLOW, "error_vlan_vid_null"},
    [HINIC3_OVS_FLOW_ERROR_COPY_RECIRC_ACTIONS_FAIL] = {HINIC3_OVS_FLOW, "error_copy_recirc_actions_fail"},
    [HINIC3_OVS_FLOW_ERROR_FLOW_ALLOC_FAIL] = {HINIC3_OVS_FLOW, "error_flow_alloc_fail"},
    [HINIC3_OVS_FLOW_ERROR_MEGA_UFID_INSERT_FAIL] = {HINIC3_OVS_FLOW, "error_mega_ufid_insert_fail"},
    [HINIC3_OVS_FLOW_ERROR_POLICY_ID_INSERT_FAIL] = {HINIC3_OVS_FLOW, "error_policy_id_insert_fail"},
    [HINIC3_OVS_FLOW_ERROR_RTE_FLOW_CREATE_FAIL] = {HINIC3_OVS_FLOW, "error_rte_flow_create_fail"},
    [HINIC3_OVS_FLOW_ERROR_FLOW_DESTROY_INPUT_NULL] = {HINIC3_OVS_FLOW, "error_flow_destroy_input_null"},
    [HINIC3_OVS_FLOW_ERROR_DEL_BATCH_CONTEXT_ALLOC_FAIL] = {HINIC3_OVS_FLOW, "error_del_batch_context_alloc_fail"},
    [HINIC3_OVS_FLOW_ERROR_DEL_BATCH_PER_TABLE_ALLOC_FAIL] = {HINIC3_OVS_FLOW, "error_del_batch_per_table_alloc_fail"},
    [HINIC3_OVS_FLOW_ERROR_CALLBACK_EXTRA_INFO_MEMCPY_FAIL] = {HINIC3_OVS_FLOW, "error_callback_extra_info_memcpy_fail"},
    [HINIC3_OVS_FLOW_ERROR_CALLBACK_INFO_NULL] = {HINIC3_OVS_FLOW, "error_callback_info_null"},
    [HINIC3_OVS_FLOW_ERROR_UFID_MAP_ADD_FLOW_SMAC_NULL] = {HINIC3_OVS_FLOW, "error_ufid_map_add_flow_smac_null"},
    [HINIC3_OVS_FLOW_ERROR_UFID_MAP_GET_TIME_FAIL] = {HINIC3_OVS_FLOW, "error_ufid_map_get_time_fail"},
    [HINIC3_OVS_FLOW_ERROR_UFID_MAP_ALLOC_HW_ELE_FAIL] = {HINIC3_OVS_FLOW, "error_ufid_map_alloc_hw_ele_fail"},
    [HINIC3_OVS_FLOW_ERROR_UFID_MAP_ADD_RTE_FLOW_HW_ELE_NULL] = {HINIC3_OVS_FLOW, "error_ufid_map_add_rte_flow_hw_ele_null"},
    [HINIC3_OVS_FLOW_ERROR_UFID_MAP_ALLOC_HW_BRIEF_FAIL] = {HINIC3_OVS_FLOW, "error_ufid_map_alloc_hw_brief_fail"},
    [HINIC3_OVS_FLOW_ERROR_UFID_MAP_ALLOC_SW_BRIEF_FAIL] = {HINIC3_OVS_FLOW, "error_ufid_map_alloc_sw_brief_fail"},
    // 命令行模块
    [HINIC3_CMD_ERROR_SW_UFID_FORMAT_ERROR] = {HINIC3_CMD, "error_sw_ufid_format_error"},
    [HINIC3_CMD_ERROR_EXCESSIVE_COMMAND] = {HINIC3_CMD, "error_excessive_command"},
    [HINIC3_CMD_ERROR_INCOMPLETE_COMMAND] = {HINIC3_CMD, "error_incomplete_command"},
    //UFID_MAP模块
    [HINIC3_UFID_MAP_ERROR_HMAP_NODE_FAIL] = {HINIC3_UFID_MAP, "error_hmap_node_fail"},
    [HINIC3_UFID_MAP_ERROR_GET_HW_ELEMENT_FAIL] = {HINIC3_UFID_MAP, "error_get_hw_element_fail"},
    [HINIC3_UFID_MAP_ERROR_FLOW_GET_BY_KEY_FAIL] = {HINIC3_UFID_MAP, "error_ufid_map_flow_get_by_key_fail"},
};

void
hinic3_add_error_stats(enum hinic3_errstat_type index, uint32_t count)
{
    long long int error_call_time = hinic3_time_sec();
    long long int error_latest_time = -1;
    long long int time_diff = 0;
    if (index >= HINIC3_ERRSTAT_END || index <= HINIC3_ERRSTAT_START) {
        g_hinic3_agent_error_stats[HINIC3_COMMON_ERROR_ERRSTAT_TYPE_OUT_OF_BOUND]++;
        error_latest_time = g_error_stats_info[HINIC3_COMMON_ERROR_ERRSTAT_TYPE_OUT_OF_BOUND].error_latest_log_time;
        time_diff = error_call_time - error_latest_time;
        if (time_diff > HINIC3_ERRSTAT_LOG_MAX_DIFF || error_latest_time == 0) {
            HINIC3_LOG(INFO, AGENT, "error stats: name=%s, count=%llu.",
                g_error_stats_info[HINIC3_COMMON_ERROR_ERRSTAT_TYPE_OUT_OF_BOUND].string,
                g_hinic3_agent_error_stats[HINIC3_COMMON_ERROR_ERRSTAT_TYPE_OUT_OF_BOUND]);
            g_error_stats_info[HINIC3_COMMON_ERROR_ERRSTAT_TYPE_OUT_OF_BOUND].error_latest_log_time = hinic3_time_sec();
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
        g_hinic3_agent_error_stats[HINIC3_COMMON_ERROR_ERRSTAT_INPUT_NULL]++;
        return -1;
    }
    if (index >= HINIC3_ERRSTAT_END || index <= HINIC3_ERRSTAT_START) {
        g_hinic3_agent_error_stats[HINIC3_COMMON_ERROR_ERRSTAT_TYPE_OUT_OF_BOUND]++;
        return -1;
    }

    stats->string = g_error_stats_info[index].string;
    stats->module = g_error_stats_info[index].module;
    if (stats->string == NULL || stats->module >= HINIC3_MODULE_MAX || stats->module < 0) {
        g_hinic3_agent_error_stats[HINIC3_COMMON_ERROR_ERRSTAT_TYPE_NO_INFO]++;
        return -1;
    }
    stats->level=  hinic3_errstat_get_level_by_string(stats->string);
    stats->count = g_hinic3_agent_error_stats[index];
    return 0;
}