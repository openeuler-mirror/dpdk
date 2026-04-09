/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2024 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_PARSE_AGENT_CONFIG_H
#define HINIC3_PARSE_AGENT_CONFIG_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "hinic3_util.h"
#include "rte_log.h"
#include "rte_cfgfile.h"
#include "hinic3_agent.h"
#include "hinic3_ds.h"
#include "hinic3_init_arg.h"

#define HINIC3_ARGS_NAME_MAX_LEN 50
#define HINIC3_VIRTUAL_QUEUE_MULTIPLEX_MIN 0
#define HINIC3_VIRTUAL_QUEUE_MULTIPLEX_MAX 16
#define HINIC3_PORT_QUEUE_DEPTH_SIZE 1024
#define HINIC3_PORT_MIN_QUEUE_DEPTH  (1 << 7)
#define HINIC3_PORT_MAX_QUEUE_DEPTH  (1 << 15)

struct hinic3_parse_num_type {
    char section_name[HINIC3_ARGS_NAME_MAX_LEN];
    char name[HINIC3_ARGS_NAME_MAX_LEN];
    uint32_t min_num;
    uint32_t max_num;
    uint32_t *num;
};

struct hinic3_init_arg *hinic3_get_init_arg(void);
int hinic3_get_agent_value_from_config(void);
bool hinic3_get_enable_hugepage_meminfo_statistic(void);
bool hinic3_get_enable_flexda_ovs_adapter(void);

static inline char *hinic3_get_multi_qos_level(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->qos_level;
}

static inline char *hinic3_pf_pci_addr_get(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->pf_pci_addr;
}

static inline struct hinic3_cpu_mask hinic3_forward_cpu_mask_get(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->forward_cpu_mask;
}

static inline struct hinic3_cpu_mask hinic3_control_cpu_mask_get(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->control_cpu_mask;
}

static inline uint32_t hinic3_disk_usage_get(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->disk_usage;
}

static inline bool hinic3_check_fuzzy_flow_switch(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->fuzzy_flow;
}

static inline bool hinic3_check_fuzzy_flow_l3_forward_switch(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->fuzzy_flow_l3_forward;
}

static inline bool hinic3_check_fuzzy_flow_flexda_switch(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->fuzzy_flow_flexda;
}

static inline bool hinic3_check_masked_to_exact_switch(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->masked_to_exact;
};

static inline uint16_t hinic3_virtio_queue_depth(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->virtio_queue_depth;
};

static inline bool hinic3_check_virtio_queue_depth_set(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->is_virtio_queue_depth_set;
};

static inline bool hinic3_support_hardware_flow_age_set(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->support_hardware_flow_age;
};

static inline bool hinic3_check_hardware_flow_age_switch(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->hardware_flow_age;
};

static inline int hinic3_device_mode_get(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->is_dpu ? DPU_MODE : SMART_NIC_MODE;
}

static inline int hinic3_forward_mode_get(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->forward_mode;
}

static inline int hinic3_hiovs_mode_get(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->hiovs_mode;
}

static inline uint16_t hinic3_bond_rx_depth_get(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->bond_rx_depth;
}

static inline uint16_t hinic3_bond_tx_depth_get(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->bond_tx_depth;
}

static inline uint16_t hinic3_vport_rx_depth(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->vport_rx_depth;
}

static inline uint16_t hinic3_vport_tx_depth(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->vport_tx_depth;
}

static inline uint8_t hinic3_virtual_queue_multiplex_get(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->virtual_queue_multiplex;
}

static inline uint32_t hinic3_offload_thread_num_get(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->offload_thread_num;
}

static inline uint32_t hinic3_max_flow_num_get(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->max_flow_num;
}

static inline uint32_t hinic3_max_queue_num_get(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->max_queue_num;
}

static inline uint32_t hinic3_upcall_queue_num_get(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->upcall_queue_num;
}

static inline uint32_t hinic3_mbuf_size_get(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->mbuf_size;
}

static inline uint32_t hinic3_flow_max_idle_get(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->flow_max_idle;
}

static inline uint64_t hinic3_vdpa_feature_get(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->vdpa_virtio_feature;
}

static inline enum tcp_ct_action_switch hinic3_status_packet_upcall_get(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->status_packet_upcall;
}

static inline uint32_t hinic3_support_virtio_queue_get(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->support_virtio_queue;
}

static inline uint32_t hinic3_packet_forward_mod_get(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->packet_forward_mode;
}

static inline uint32_t hinic3_packet_bond_hash_policy_get(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->bond_hash_policy;
}

static inline uint32_t hinic3_max_flow_num_limit_get(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->max_flow_num_limit;
}

static inline uint32_t hinic3_max_queue_num_limit_get(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->max_queue_num_limit;
}

static inline enum hinic3_query_bdf_type hinic3_query_bdf_type_get(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->query_bdf_type;
}

static inline bool hinic3_support_port_hot_plug(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->support_port_hot_plug;
}

static inline bool hinic3_is_support_vf_port(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->support_vf_port;
}

static inline bool hinic3_is_support_pf_port(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->support_pf_port;
}

static inline enum hinic3_card_mode hinic3_card_mod_get(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->card_mode;
}

static inline enum hinic3_user_scenario hinic3_user_scenario_get(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->user_scenario;
}

static inline bool hinic3_support_flow_qos_get(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->support_flow_qos;
}

static inline bool hinic3_support_multi_qos_get(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->support_multi_qos;
}

static inline bool hinic3_hot_migration_get(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->hot_migration;
}

static inline bool hinic3_masked_to_exact_get(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->masked_to_exact;
}

static inline bool hinic3_con_track_get(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->con_track;
}

static inline bool hinic3_dp_hash_flow_get(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->dp_hash_flow;
}

static inline bool hinic3_acl_flow_get(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->acl_flow;
}

static inline bool hinic3_offload_policy_get(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->offload_policy;
}

static inline bool hinic3_security_filter_get(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->security_filter;
}

static inline bool hinic3_support_sample_get(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->support_sample;
}

static inline bool hinic3_support_sample_pkt_cutoff_get(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->support_sample_pkt_cutoff;
}

static inline bool hinic3_support_sample_ratio_get(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->support_sample_ratio;
}

static inline bool hinic3_is_preload_port(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->is_preload_pf_port || conf->is_preload_vf_port;
}

static inline bool hinic3_is_preload_pf_port(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->is_preload_pf_port;
}

static inline bool hinic3_is_preload_vf_port(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->is_preload_vf_port;
}

static inline bool hinic3_vf_del_no_driver_check_get(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->vf_del_no_driver_check;
}

static inline bool hinic3_support_bond_detect_get(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->support_bond_detect;
}

static inline bool hinic3_support_vlan_tci_get(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->support_vlan_tci;
}

static inline bool hinic3_support_payload_capture_get(void)
{
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    return conf->support_payload_capture;
}

#endif
