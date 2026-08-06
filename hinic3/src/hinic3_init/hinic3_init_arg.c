/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2024 Huawei Technologies Co., Ltd
 */

#include <stdbool.h>
#include <stdint.h>
#include "hinic3_init_arg.h"
#include "hinic3_util.h"
#define LOG_PREFIX_LENGTH 32

static char g_log_prefix[LOG_PREFIX_LENGTH] = "dpak";
const char g_log_prefix_comnet_smartnic[LOG_PREFIX_LENGTH] = "offload_smartnic";
const char g_log_prefix_comnet_dpu[LOG_PREFIX_LENGTH] = "offload_dpu";
const char g_log_prefix_comit_dpu[LOG_PREFIX_LENGTH] = "OFFLOAD_SNIC";

struct hinic3_init_arg init_arg_openovs_standard_dpu_virt = {
    .hot_migration = true,
    .is_dpu = true,
    .security_filter = true,
    .offload_policy = true,
    .masked_to_exact = true,
    .acl_flow = false,
    .dp_hash_flow = false,
    .fuzzy_flow = false,
    .hardware_flow_age = true,
    .con_track = true,
    .support_sample = false,
    .support_sample_pkt_cutoff = false,
    .support_sample_ratio = false,
    .forward_mode = OVS_KEY_EXTRACT_EXTEND_MODE_11TUPLE,
    .hiovs_mode = HIOVS_WORK_MODE_ZERO_VM_PT,
    .vdpa_virtio_feature = 0,
    .status_packet_upcall = DEFAULT_NOT_PUT,
    .support_virtio_queue = 2061,
    .max_flow_num_limit = 2,
    .max_queue_num_limit = 32,
    .support_flow_qos = true,
    .support_multi_qos = false,
    .query_bdf_type = QUERY_BDF_TYPE_REAL,
    .support_port_hot_plug = false,
    .support_vf_port = true,
    .support_pf_port = false,
    .is_virtio_queue_depth_set = true,
    .is_preload_pf_port = false,
    .is_preload_vf_port = false,
    .vf_del_no_driver_check = false,
    .support_hardware_flow_age = false,
    .virtio_queue_depth = 1024,
    .card_mode = STANDARD_MODE,
    .user_scenario = OPEN_OVS,
    .support_vlan_tci = false,
    .support_payload_capture = true,
};

static struct hinic3_init_arg init_arg_openovs_standard_dpu_metal = {
    .hot_migration = false,
    .is_dpu = true,
    .security_filter = true,
    .offload_policy = true,
    .masked_to_exact = true,
    .acl_flow = false,
    .dp_hash_flow = false,
    .fuzzy_flow = false,
    .hardware_flow_age = true,
    .con_track = true,
    .support_sample = false,
    .support_sample_pkt_cutoff = false,
    .support_sample_ratio = false,
    .forward_mode = OVS_KEY_EXTRACT_EXTEND_MODE_11TUPLE,
    .hiovs_mode = HIOVS_WORK_MODE_BMGW,
    .vdpa_virtio_feature = 0,
    .status_packet_upcall = DEFAULT_NOT_PUT,
    .support_virtio_queue = 1024,
    .max_flow_num_limit = 2,
    .max_queue_num_limit = 32,
    .support_flow_qos = true,
    .support_multi_qos = false,
    .query_bdf_type = QUERY_BDF_TYPE_FAKE,
    .support_port_hot_plug = false,
    .support_vf_port = false,
    .support_pf_port = true,
    .is_virtio_queue_depth_set = false,
    .is_preload_pf_port = true,
    .is_preload_vf_port = true,
    .vf_del_no_driver_check = false,
    .support_hardware_flow_age = false,
    .virtio_queue_depth = 1024,
    .card_mode = STANDARD_MODE,
    .user_scenario = OPEN_OVS,
    .support_vlan_tci = false,
    .support_payload_capture = true,
};

struct hinic3_init_arg init_arg_openovs_standard_smartnic_virt = {
    .hot_migration = true,
    .is_dpu = false,
    .security_filter = true,
    .offload_policy = true,
    .masked_to_exact = true,
    .acl_flow = false,
    .dp_hash_flow = false,
    .fuzzy_flow = false,
    .hardware_flow_age = true,
    .con_track = true,
    .support_sample = false,
    .support_sample_pkt_cutoff = false,
    .support_sample_ratio = false,
    .forward_mode = OVS_KEY_EXTRACT_EXTEND_MODE_11TUPLE,
    .hiovs_mode = HIOVS_WORK_MODE_PT_CONTAINER,
    .vdpa_virtio_feature = 0,
    .status_packet_upcall = DEFAULT_NOT_PUT,
    .support_virtio_queue = 1045,
    .max_flow_num_limit = 2,
    .max_queue_num_limit = 32,
    .support_flow_qos = true,
    .support_multi_qos = false,
    .query_bdf_type = QUERY_BDF_TYPE_REAL,
    .support_port_hot_plug = false,
    .support_vf_port = true,
    .support_pf_port = false,
    .is_virtio_queue_depth_set = true,
    .is_preload_pf_port = false,
    .is_preload_vf_port = false,
    .vf_del_no_driver_check = false,
    .support_hardware_flow_age = false,
    .virtio_queue_depth = 1024,
    .card_mode = STANDARD_MODE,
    .user_scenario = OPEN_OVS,
    .support_vlan_tci = false,
    .support_payload_capture = true,
};

struct hinic3_init_arg init_arg_cmnnet_standard_dpu_metal = {
    .hot_migration = false,
    .is_dpu = true,
    .security_filter = false,
    .offload_policy = false,
    .masked_to_exact = false,
    .acl_flow = true,
    .dp_hash_flow = false,
    .fuzzy_flow = false,
    .hardware_flow_age = false,
    .con_track = false,
    .support_sample = true,
    .support_sample_pkt_cutoff = true,
    .support_sample_ratio = false,
    .forward_mode = OVS_KEY_EXTRACT_EXTEND_MODE_11TUPLE_VLANTCI,
    .hiovs_mode = HIOVS_WORK_MODE_BMGW,
    .vdpa_virtio_feature = 0,
    .status_packet_upcall = DEFAULT_NOT_PUT,
    .support_virtio_queue = 1024,
    .max_flow_num_limit = 2,
    .max_queue_num_limit = 32,
    .support_flow_qos = false,
    .support_multi_qos = false,
    .query_bdf_type = QUERY_BDF_TYPE_FAKE,
    .support_port_hot_plug = false,
    .support_vf_port = false,
    .support_pf_port = true,
    .is_virtio_queue_depth_set = false,
    .is_preload_pf_port = true,
    .is_preload_vf_port = true,
    .vf_del_no_driver_check = false,
    .support_bond_detect = false,
    .support_hardware_flow_age = false,
    .virtio_queue_depth = 4096,
    .card_mode = STANDARD_MODE,
    .user_scenario = CMN_NET,
    .support_vlan_tci = true,
    .support_payload_capture = true,
};

struct hinic3_init_arg init_arg_cmnnet_standard_dpu_container = {
    .hot_migration = false,
    .is_dpu = true,
    .security_filter = false,
    .offload_policy = false,
    .masked_to_exact = false,
    .acl_flow = true,
    .dp_hash_flow = true,
    .fuzzy_flow = true,
    .fuzzy_flow_l3_forward = false,
    .hardware_flow_age = false,
    .con_track = false,
    .support_sample = true,
    .support_sample_pkt_cutoff = true,
    .support_sample_ratio = false,
    .forward_mode = OVS_KEY_EXTRACT_EXTEND_MODE_6TUPLE,
    .hiovs_mode = HIOVS_WORK_MODE_BMGW,
    .vdpa_virtio_feature = 0,
    .status_packet_upcall = DEFAULT_NOT_PUT,
    .support_virtio_queue = 1024,
    .max_flow_num_limit = 2,
    .max_queue_num_limit = 32,
    .support_flow_qos = false,
    .support_multi_qos = false,
    .query_bdf_type = QUERY_BDF_TYPE_FAKE,
    .support_port_hot_plug = false,
    .support_vf_port = true,
    .support_pf_port = true,
    .is_preload_pf_port = true,
    .is_preload_vf_port = false,
    .vf_del_no_driver_check = true,
    .support_bond_detect = false,
    .support_hardware_flow_age = false,
    .virtio_queue_depth = 4096,
    .card_mode = STANDARD_MODE,
    .user_scenario = CMN_NET,
    .support_vlan_tci = true,
    .support_payload_capture = true,
};

struct hinic3_init_arg init_arg_cmnnet_standard_smartnic_virt = {
    .hot_migration = false,
    .is_dpu = false,
    .security_filter = false,
    .offload_policy = false,
    .masked_to_exact = false,
    .acl_flow = true,
    .dp_hash_flow = true,
    .fuzzy_flow = true,
    .fuzzy_flow_l3_forward = false,
    .hardware_flow_age = false,
    .con_track = false,
    .support_sample = true,
    .support_sample_pkt_cutoff = true,
    .support_sample_ratio = false,
    .forward_mode = OVS_KEY_EXTRACT_EXTEND_MODE_6TUPLE,
    .hiovs_mode = HIOVS_WORK_MODE_PT_CONTAINER,
    .vdpa_virtio_feature = 0,
    .status_packet_upcall = DEFAULT_NOT_PUT,
    .support_virtio_queue = 1045,
    .max_flow_num_limit = 2,
    .max_queue_num_limit = 32,
    .support_flow_qos = false,
    .support_multi_qos = false,
    .query_bdf_type = QUERY_BDF_TYPE_REAL,
    .support_port_hot_plug = false,
    .support_vf_port = true,
    .support_pf_port = false,
    .is_virtio_queue_depth_set = false,
    .is_preload_pf_port = false,
    .is_preload_vf_port = false,
    .vf_del_no_driver_check = false,
    .support_bond_detect = true,
    .support_hardware_flow_age = false,
    .virtio_queue_depth = 4096,
    .card_mode = STANDARD_MODE,
    .user_scenario = CMN_NET,
    .support_vlan_tci = true,
    .support_payload_capture = true,
};

struct hinic3_init_arg init_arg_cmnit_standard_dpu_metal = {
    .hot_migration = false,
    .is_dpu = true,
    .security_filter = false,
    .offload_policy = false,
    .masked_to_exact = false,
    .acl_flow = true,
    .dp_hash_flow = false,
    .fuzzy_flow = false,
    .hardware_flow_age = false,
    .con_track = false,
    .support_sample = true,
    .support_sample_pkt_cutoff = true,
    .support_sample_ratio = false,
    .forward_mode = OVS_KEY_EXTRACT_EXTEND_MODE_11TUPLE_VLANTCI,
    .hiovs_mode = HIOVS_WORK_MODE_BMGW,
    .vdpa_virtio_feature = 0,
    .status_packet_upcall = DEFAULT_PUT,
    .support_virtio_queue = 1024,
    .max_flow_num_limit = 2,
    .max_queue_num_limit = 32,
    .support_flow_qos = false,
    .support_multi_qos = false,
    .query_bdf_type = QUERY_BDF_TYPE_FAKE,
    .support_port_hot_plug = false,
    .support_vf_port = false,
    .support_pf_port = true,
    .is_virtio_queue_depth_set = false,
    .is_preload_pf_port = true,
    .is_preload_vf_port = true,
    .vf_del_no_driver_check = false,
    .support_bond_detect = false,
    .support_hardware_flow_age = false,
    .virtio_queue_depth = 4096,
    .card_mode = STANDARD_MODE,
    .user_scenario = CMN_IT,
    .support_vlan_tci = true,
    .support_payload_capture = true,
};

struct hinic3_init_arg init_arg_combd_standard_dpu_virt = {
    .hot_migration = false,
    .is_dpu = true,
    .security_filter = false,
    .offload_policy = false,
    .masked_to_exact = true,
    .acl_flow = false,
    .dp_hash_flow = false,
    .fuzzy_flow = true,
    .fuzzy_flow_l3_forward = true,
    .fuzzy_flow_flexda =false,
    .hardware_flow_age = false,
    .con_track = false,
    .support_sample = true,
    .support_sample_pkt_cutoff = false,
    .support_sample_ratio = true,
    .forward_mode = OVS_KEY_EXTRACT_EXTEND_MODE_7TUPLE,
    .hiovs_mode = HIOVS_WORK_MODE_ZERO_VM_PT,
    .vdpa_virtio_feature = 0,
    .status_packet_upcall = SELECT_PUT,
    .support_virtio_queue = 4096,
    .max_flow_num_limit = 4,
    .max_queue_num_limit = 64,
    .support_flow_qos = true,
    .support_multi_qos = true,
    .query_bdf_type = QUERY_BDF_TYPE_REAL,
    .support_port_hot_plug = false,
    .support_vf_port = true,
    .support_pf_port = true,
    .is_preload_pf_port = false,
    .is_preload_vf_port = false,
    .vf_del_no_driver_check = false,
    .is_virtio_queue_depth_set = true,
    .card_mode = STANDARD_MODE,
    .user_scenario = COM_BD,
    .support_hardware_flow_age = true,
    .virtio_queue_depth = 1024,
    .support_vlan_tci = true,
    .support_payload_capture = true,
};

struct hinic3_init_arg init_arg_combd_standard_dpu_metal = {
    .hot_migration = false,
    .is_dpu = true,
    .security_filter = false,
    .offload_policy = false,
    .masked_to_exact = true,
    .acl_flow = false,
    .dp_hash_flow = false,
    .fuzzy_flow = true,
    .fuzzy_flow_l3_forward = true,
    .fuzzy_flow_flexda =false,
    .hardware_flow_age = false,
    .con_track = false,
    .support_sample = true,
    .support_sample_pkt_cutoff = false,
    .support_sample_ratio = true,
    .forward_mode = OVS_KEY_EXTRACT_EXTEND_MODE_7TUPLE,
    .hiovs_mode = HIOVS_WORK_MODE_BMGW,
    .vdpa_virtio_feature = 0,
    .status_packet_upcall = SELECT_PUT,
    .support_virtio_queue = 4096,
    .max_flow_num_limit = 4,
    .max_queue_num_limit = 64,
    .query_bdf_type = QUERY_BDF_TYPE_FAKE,
    .support_port_hot_plug = true,
    .support_vf_port = true,
    .support_pf_port = true,
    .is_preload_pf_port = false,
    .is_preload_vf_port = false,
    .vf_del_no_driver_check = false,
    .is_virtio_queue_depth_set = true,
    .card_mode = STANDARD_MODE,
    .user_scenario = COM_BD,
    .support_flow_qos = true,
    .support_multi_qos = true,
    .support_hardware_flow_age = true,
    .virtio_queue_depth = 1024,
    .support_vlan_tci = true,
    .support_payload_capture = true,
};

 
struct hinic3_init_arg init_arg_openovs_prog_dpu_virt = {
    .hot_migration = true,
    .is_dpu = true,
    .security_filter = false,
    .offload_policy = false,
    .masked_to_exact = true,
    .acl_flow = false,
    .dp_hash_flow = false,
    .fuzzy_flow = true,
    .fuzzy_flow_flexda = true,
    .fuzzy_flow_l3_forward = false,
    .hardware_flow_age = true,
    .con_track = false,
    .support_sample = false,
    .support_sample_pkt_cutoff = false,
    .support_sample_ratio = false,
    .forward_mode = OVS_KEY_EXTRACT_EXTEND_MODE_11TUPLE,
    .hiovs_mode = HIOVS_WORK_MODE_ZERO_VM_PT,
    .vdpa_virtio_feature = 0,
    .status_packet_upcall = DEFAULT_NOT_PUT,
    .support_virtio_queue = 2061,
    .max_flow_num_limit = 32,
    .max_queue_num_limit = 32,
    .support_flow_qos = false,
    .support_multi_qos = false,
    .query_bdf_type = QUERY_BDF_TYPE_REAL,
    .support_port_hot_plug = false,
    .support_vf_port = true,
    .support_pf_port = false,
    .card_mode = PROG_MODE,
    .user_scenario = OPEN_OVS,
    .is_virtio_queue_depth_set = false,
    .support_payload_capture = false,
};

struct hinic3_init_arg init_arg_openovs_prog_dpu_metal = {
    .hot_migration = false,
    .is_dpu = true,
    .security_filter = false,
    .offload_policy = false,
    .masked_to_exact = true,
    .acl_flow = false,
    .dp_hash_flow = false,
    .fuzzy_flow = true,
    .fuzzy_flow_l3_forward = false,
    .fuzzy_flow_flexda =true,
    .hardware_flow_age = true,
    .con_track = false,
    .support_sample = true,
    .support_sample_pkt_cutoff = false,
    .support_sample_ratio = false,
    .forward_mode = OVS_KEY_EXTRACT_EXTEND_MODE_11TUPLE,
    .hiovs_mode = HIOVS_WORK_MODE_BMGW,
    .vdpa_virtio_feature = 0,
    .status_packet_upcall = SELECT_PUT,
    .support_virtio_queue = 4096,
    .max_flow_num_limit = 4,
    .max_queue_num_limit = 32,
    .query_bdf_type = QUERY_BDF_TYPE_FAKE,
    .support_port_hot_plug = false,
    .support_vf_port = false,
    .support_pf_port = true,
    .is_preload_pf_port = true,
    .is_preload_vf_port = true,
    .vf_del_no_driver_check = false,
    .is_virtio_queue_depth_set = false,
    .card_mode = PROG_MODE,
    .user_scenario = OPEN_OVS,
    .support_flow_qos = true,
    .support_multi_qos = true,
    .support_hardware_flow_age = true,
    .virtio_queue_depth = 1024,
    .support_vlan_tci = false,
    .support_payload_capture = false,
};

struct hinic3_init_arg init_arg_comsn_prog_dpu_virt = {
    .hot_migration = true,
    .is_dpu = true,
    .security_filter = false,
    .offload_policy = false,
    .masked_to_exact = true,
    .acl_flow = false,
    .dp_hash_flow = false,
    .fuzzy_flow = false,
    .hardware_flow_age = false,
    .con_track = false,
    .support_sample = false,
    .support_sample_pkt_cutoff = false,
    .support_sample_ratio = false,
    .forward_mode = OVS_KEY_EXTRACT_EXTEND_MODE_11TUPLE,
    .hiovs_mode = HIOVS_WORK_MODE_ZERO_VM_PT,
    .vdpa_virtio_feature = 0,
    .status_packet_upcall = DEFAULT_NOT_PUT,
    .support_virtio_queue = 2061,
    .max_flow_num_limit = 2,
    .max_queue_num_limit = 64,
    .support_flow_qos = false,
    .support_multi_qos = false,
    .query_bdf_type = QUERY_BDF_TYPE_REAL,
    .support_port_hot_plug = false,
    .support_vf_port = true,
    .support_pf_port = false,
    .card_mode = PROG_MODE,
    .user_scenario = COM_SN,
    .is_virtio_queue_depth_set = false,
    .support_payload_capture = false,
};

// 初始化函数
void hinic3_init_log_prefix(const char *run_time_mode)
{
    if (run_time_mode == NULL) {
        strncpy(g_log_prefix, "dpak", LOG_PREFIX_LENGTH - 1);
        return;
    }
        
    if (strcmp(run_time_mode, "0x0300") == 0 ||  strcmp(run_time_mode, "0x0302") == 0) {
        strncpy(g_log_prefix, g_log_prefix_comnet_dpu, LOG_PREFIX_LENGTH - 1);
    } else if (strcmp(run_time_mode, "0x0305") == 0) {
        strncpy(g_log_prefix, g_log_prefix_comnet_smartnic, LOG_PREFIX_LENGTH - 1);
    } else if (strcmp(run_time_mode, "0x0400") == 0) {
        strncpy(g_log_prefix, g_log_prefix_comit_dpu, LOG_PREFIX_LENGTH - 1);
    } else {
        strncpy(g_log_prefix, "dpak", LOG_PREFIX_LENGTH - 1);
    }
    g_log_prefix[LOG_PREFIX_LENGTH - 1] = '\0';
}

const char *hinic3_log_prefix_get(void)
{
    return g_log_prefix;
}

struct hinic3_run_time_mode {
    const char *mode_hex_code;
    struct hinic3_init_arg *mode_arg;
};

static struct hinic3_run_time_mode g_hinic3_run_time_mode_map[] = {
    {"0x0000", &init_arg_openovs_standard_dpu_metal},
    {"0x0001", &init_arg_openovs_standard_dpu_virt},
    {"0x0005", &init_arg_openovs_standard_smartnic_virt},
    {"0x0008", &init_arg_openovs_prog_dpu_metal},
    {"0x0009", &init_arg_openovs_prog_dpu_virt},
    {"0x0100", &init_arg_combd_standard_dpu_metal},
    {"0x0101", &init_arg_combd_standard_dpu_virt},
    {"0x0209", &init_arg_comsn_prog_dpu_virt},
    {"0x0300", &init_arg_cmnnet_standard_dpu_metal},
    {"0x0302", &init_arg_cmnnet_standard_dpu_container},
    {"0x0305", &init_arg_cmnnet_standard_smartnic_virt},
    {"0x0400", &init_arg_cmnit_standard_dpu_metal},
};

static struct hinic3_init_arg *hinic3_get_run_time_mode_arg(const char *run_time_mode)
{
    if (run_time_mode == NULL){
        return NULL;
    }
    for (size_t i = 0; i < ARRAY_SIZE(g_hinic3_run_time_mode_map); i++) {
        if (strcmp(run_time_mode, g_hinic3_run_time_mode_map[i].mode_hex_code) == 0) {
            HINIC3_LOG(INFO, AGENT, "hinic3 get run time mode '%s'.", run_time_mode);
            return g_hinic3_run_time_mode_map[i].mode_arg;
        }
    }
    return NULL;
}

int hinic3_get_fixed_config(struct hinic3_init_arg **arg, const char * run_time_mode)
{
    struct hinic3_init_arg *temp_arg = NULL;

    temp_arg = hinic3_get_run_time_mode_arg(run_time_mode);
    if (temp_arg == NULL) {
        HINIC3_LOG(ERR, AGENT, "run time mode '%s' do not exit!", run_time_mode);
        return -1;
    }
    *arg = temp_arg;
    return 0;
}
