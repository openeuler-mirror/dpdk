/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2022 Huawei Technologies Co., Ltd
 */
#ifndef DPAK_CFG_H
#define DPAK_CFG_H

#include <stdbool.h>
#include <stdint.h>
#include "hinic3_log.h"
#include "hiovs_api.h"
#define HINIC3_CPU_MASK_NUM 4
#define HINIC3_PCI_MAX_LEN 40
#define RUN_TIME_MODE_MAX_LENGTH 50
#define QOS_LEVEL_MAX_LENGTH 50
#define HINIC3_FLOW_MAX_IDLE_DEFAULT 200000
#define HINIC3_FLOW_AGE_TIME_DEFAULT "200000"

/* 三种状态 默认下发 默认不下发 选择性下发 */
enum tcp_ct_action_switch {
    DEFAULT_PUT,
    DEFAULT_NOT_PUT,
    SELECT_PUT,
};

enum hinic3_work_mode {
    SMART_NIC_MODE,
    DPU_MODE,
};

enum hinic3_card_mode {
    STANDARD_MODE,
    PROG_MODE,
};

enum hinic3_forward_mode {
    OVS_KEY_EXTRACT_MODE_9TUPLE    = 0, /**< 9 tuples: localtag, dmac, smac, ethtype, sip, dip, protol, sport, dport */
    OVS_KEY_EXTRACT_MODE_4TUPLE    = 1, /**< 4 tuples: localtag, dmac, smac, ethtype */
    OVS_KEY_EXTRACT_MODE_6_9TUPLE  = 2, /**< 6 tuples: localtag, sip, dip, protol, sport, dport */
    OVS_KEY_EXTRACT_MODE_MAX       = 3,
    OVS_KEY_EXTRACT_EXTEND_MODE_6TUPLE    = 4, /**< 6 tuples: localtag, dmac, dip, ethtype, vlan_id, input_port */
    OVS_KEY_EXTRACT_EXTEND_MODE_11TUPLE   = 5, /**< 11 tuples: 9 tuples, vlan_id, input_port */
    OVS_KEY_EXTRACT_EXTEND_MODE_7TUPLE = 6,  /** < 9 tuples vni, eth_type, src_ip, dst_ip, protocol, sport, dport */
    OVS_KEY_EXTRACT_EXTEND_MODE_11TUPLE_VLANTCI = 7,  /** < 9 tuples vni, eth_type, src_ip, dst_ip, protocol, sport, dport */
};

enum hinic3_bare_metal_type {
    HINIC3_SCENE_BARE_METAL = 0,
    HINIC3_SCENE_BARE_METAL_CONTAINER = 1,
};

enum hinic3_query_bdf_type {
    QUERY_BDF_TYPE_REAL,      /**< query real bdf type. */
    QUERY_BDF_TYPE_FAKE,      /**< query fake bdf type. */
};

struct hinic3_cpu_mask {
    uint32_t cpu_mask[HINIC3_CPU_MASK_NUM];
    uint32_t cpu_mask_invalid_num;
};

enum hinic3_bond_hash_policy {
    EFFICIENT_HASH,
    STANDARD_HASH,
};

enum hinic3_user_scenario {
    OPEN_OVS,
    COM_BD,
    COM_SN,
    CMN_NET,
    CMN_IT,
};

struct hinic3_init_arg {
    bool hot_migration;
    bool is_dpu;
    bool security_filter;
    bool offload_policy;
    bool masked_to_exact;
    bool acl_flow;
    bool dp_hash_flow;
    bool fuzzy_flow;
    bool fuzzy_flow_l3_forward;
    bool fuzzy_flow_flexda;
    bool hardware_flow_age;
    bool con_track;
    bool support_sample;
    bool support_sample_pkt_cutoff;
    bool support_sample_ratio;
    enum tcp_ct_action_switch status_packet_upcall;
    uint32_t support_virtio_queue;
    uint32_t bond_rx_depth;
    uint32_t bond_tx_depth;
    uint32_t vport_rx_depth;
    uint32_t vport_tx_depth;
    enum hinic3_forward_mode forward_mode;
    enum hiovs_work_mode hiovs_mode;
    enum hinic3_card_mode card_mode;
    enum hinic3_user_scenario user_scenario;
    uint32_t offload_thread_num;
    uint32_t max_flow_num;
    uint32_t max_flow_num_limit;
    struct hinic3_cpu_mask forward_cpu_mask;
    struct hinic3_cpu_mask control_cpu_mask;
    char pf_pci_addr[HINIC3_PCI_MAX_LEN];
    uint32_t max_queue_num;
    uint32_t max_queue_num_limit;
    uint32_t upcall_queue_num;
    uint32_t mbuf_size;
    uint32_t flow_max_idle;
    uint64_t vdpa_virtio_feature;
    uint32_t disk_usage;
    uint32_t packet_forward_mode;
    bool support_flow_qos;
    bool support_multi_qos;
    bool enable_hugepage_meminfo_statistic;
    bool enable_flexda_ovs_adapter;
    uint32_t virtual_queue_multiplex;
    char run_time_mode[RUN_TIME_MODE_MAX_LENGTH];
    enum hinic3_bond_hash_policy bond_hash_policy;
    uint32_t pcap_cpu;
    enum hinic3_query_bdf_type query_bdf_type;
    bool support_port_hot_plug;
    enum hinic3_bare_metal_type bare_metal_type;
    bool support_vf_port;
    bool support_pf_port;
    char qos_level[QOS_LEVEL_MAX_LENGTH];
    bool is_virtio_queue_depth_set;
    uint16_t virtio_queue_depth;
    // 是否预载所有PF端口
    bool is_preload_pf_port;
    // 是否预载所有VF端口
    bool is_preload_vf_port;
    // 创建VF端口是否下发‘不做驱动检查标记’到组件
    bool vf_del_no_driver_check;
    bool support_bond_detect;
    bool support_hardware_flow_age;
    // 是否支持匹配16bits VLAN TCI
    bool support_vlan_tci;
    bool support_payload_capture;
};

const char *hinic3_log_prefix_get(void);
int hinic3_get_fixed_config(struct hinic3_init_arg **arg, const char *run_time_mode);
void hinic3_init_log_prefix(const char *run_time_mode);
#endif
