/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_PROVIDER_H
#define HINIC3_PROVIDER_H

#include <stdint.h>
#include "rte_config.h"
#include "rte_mbuf.h"
#include "rte_pci.h"
#include "hinic3_message.h"
#include "hinic3_driver_public.h"

#ifdef __cplusplus
extern "C" {
#endif
/*
 * PKT_RX_HW_OFFLOAD_INFO is used to indicate if mbuf->udata64
 * contains valid information.
 * Be careful: Flow agent and IFE used the same mbuf->udata64.
 */
#define MAX_NAME_LEN 32
#define HINIC3_RX_THREAD_MAX 16
#define HINIC3_FLOW_STATS_BLOCK_SIZE 128

struct hinic3_netdev_stats {
    uint64_t rx_packets;
    uint64_t tx_packets;
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    uint64_t rx_errors;
    uint64_t tx_errors;
    uint64_t rx_dropped;
    uint64_t tx_dropped;
    uint64_t multicast;
    uint64_t collisions;

    uint64_t rx_length_errors;
    uint64_t rx_over_errors;
    uint64_t rx_crc_errors;
    uint64_t rx_frame_errors;
    uint64_t rx_fifo_errors;
    uint64_t rx_missed_errors;

    uint64_t tx_aborted_errors;
    uint64_t tx_carrier_errors;
    uint64_t tx_fifo_errors;
    uint64_t tx_heartbeat_errors;
    uint64_t tx_window_errors;

    uint64_t rx_packets_split[0x7];
    uint64_t tx_packets_split[0x7];

    uint64_t tx_multicast_packets;

    uint64_t rx_broadcast_packets;
    uint64_t tx_broadcast_packets;

    uint64_t rx_errors_size[0x4];
};

struct hinic3_port_stats_ {
    struct hinic3_netdev_stats ovs_port_stats;
    uint64_t timestamp;

    /* Hinic ucode counter */
    uint32_t tx_bond_mode_err_dropped;
    uint32_t tx_bond_noport_dropped;
    uint32_t tx_bcmc_limit_dropped;
    uint32_t tx_bum_smac_dropped;
    uint32_t tx_bum_sipsmac_dropped;
    uint32_t tx_bum_ethtype_dropped;
    uint32_t rx_wqe_fail_dropped;
    uint32_t rx_qos_dropped;
    uint32_t rx_mtu_dropped;
    uint32_t rx_vport_invalid_dropped;
    uint32_t ct_dropped; /* tx_drop:vport type is function; else rx_drop */
    uint32_t upcall_wqe_fail_dropped;

    uint64_t upcall_pkt_num_vport;
    uint64_t tx_uc_bytes; /* only valid when vport type is function */
    uint64_t tx_uc_pkts;
    uint64_t tx_bc_bytes; /* only valid when vport type is function */
    uint64_t tx_bc_pkts;
    uint64_t tx_mc_bytes; /* only valid when vport type is function */
    uint64_t tx_mc_pkts;
    uint64_t rx_uc_bytes; /* only valid when vport type is function */
    uint64_t rx_uc_pkts;
    uint64_t rx_bc_bytes; /* only valid when vport type is function */
    uint64_t rx_bc_pkts;
    uint64_t rx_mc_bytes; /* only valid when vport type is function */
    uint64_t rx_mc_pkts;

    uint32_t upcall_no_sge_dropped; /* upcall pre-drop without sge */
    uint32_t rss_q_invalid_dropped; /* rss queue invalid drop, only valid for vport */
    uint32_t rq_bp_dropped;         /* only valid when vport type is function */
    uint32_t lacp_rx_nowqe_dropped; /* only valid when vport type is bond */
    uint32_t rsvd[6];
};
typedef struct hinic3_port_stats_ hinic3_port_stats;

enum hinic3_provider_type {
    HINIC3_PROVIDER_NONE = 0,
    HINIC3_PROVIDER_IFE,   /* Integrated Forwarding Engine */
    HINIC3_PROVIDER_HINIC, /* Hi1822 NIC */
};

typedef enum tag_hinic3_global_api {
    HINIC3_GLOBAL_CFG_SET = 0,
    HINIC3_GLOBAL_CFG_GET,
    HINIC3_GLOBAL_STATISTICS_GET,
    HINIC3_GLOBAL_STATISTICS_FLUSH,
    HINIC3_GLOBAL_OPEN_LOG,
    HINIC3_GLOBAL_SET_LOG_LEVEL,
    HINIC3_GLOBAL_CMD_EXEC,
    HINIC3_GLOBAL_PCIE_LIST_QUERY,
    HINIC3_GLOBAL_API_MAX
} hinic3_global_api;

typedef enum tag_hinic3_port_api {
    HINIC3_PORT_MGMT_ADD = 0,
    HINIC3_PORT_MGMT_DEL,
    HINIC3_PORT_MGMT_GET,
    HINIC3_PORT_MGMT_SET,
    HINIC3_PORT_QUEUE_SET,
    HINIC3_PORT_QUEUE_RELEASE,
    HINIC3_BOND_MGMT_CREATE,
    HINIC3_BOND_MGMT_DELETE,
    HINIC3_BOND_MGMT_GET,
    HINIC3_BOND_MGMT_SET,
    HINIC3_BOND_MGMT_CLEAR,
    HINIC3_BOND_SLAVE_INFO_GET,
    HINIC3_ETHERADDR_GET,
    HINIC3_MTU_SET,
    HINIC3_CARRIER_GET,
    HINIC3_CARRIER_RESETS_GET,
    HINIC3_PORT_STATISTICS_GET,
    HINIC3_PORT_STATISTICS_FLUSH,
    HINIC3_FEATURES_GET,
    HINIC3_SECURITY_GET,
    HINIC3_SECURITY_SET,
    HINIC3_SECURITY_REMOVE,
    HINIC3_SECURITY_CLEAR,
    HINIC3_QOS_INGRESS_LIMIT_SET,
    HINIC3_QOS_INGRESS_LIMIT_GET,
    HINIC3_QOS_EGRESS_LIMIT_SET,
    HINIC3_QOS_EGRESS_LIMIT_GET,
    HINIC3_QOS_DROP_THRESH_SET,
    HINIC3_QOS_DROP_THRESH_GET,
    HINIC3_PORT_MGMT_GET_CAPABILITY,
    HINIC3_PORT_MGMT_GET_UPCALL_INFO,
    HINIC3_QOS_STATISTICS_GET,
    HINIC3_QOS_STATISTICS_CLEAR_GET,
    HINIC3_QOS_STATISTICS_CLEAR_CLEAR,
    HINIC3_HQOS_STATISTICS_CLEAR_GET,
    HINIC3_HQOS_STATISTICS_CLEAR_CLEAR,
    HINIC3_PORT_MGMT_ADD_DYNAMIC,
    HINIC3_VM_QOS_LIMIT_SET,
    HINIC3_VM_QOS_LIMIT_GET,
    HINIC3_PORT_QOS_LIMIT_SET,
    HINIC3_PORT_QOS_LIMIT_GET,
    HINIC3_VM_QOS_SRTCM_LIMIT_SET,
    HINIC3_VM_QOS_SRTCM_LIMIT_GET,
    HINIC3_NET_QOS_LIMIT_SET,
    HINIC3_NET_QOS_LIMIT_GET,
    HINIC3_PORT_MGMT_GET_BOND_SALVE_INFO,
    HINIC3_PORT_MGMT_SET_USAGE_STATE,
    HINIC3_PORT_MGMT_GET_USAGE_STATE,
    HINIC3_UPCALL_MTU_SET,
    HINIC3_HOTPLUG_ADD,
    HINIC3_HOTPLUG_DEL,
    HINIC3_GET_TX_QUEUE_COUNT,
    HINIC3_GET_RX_QUEUE_COUNT,
    HINIC3_PORT_API_MAX
} hinic3_port_api;

enum {
    HINIC3_FLOW_CB_T_PUT_ACK,
    HINIC3_FLOW_CB_T_AGE,
    HINIC3_FLOW_CB_T_FLUSH_DONE,
    HINIC3_FLOW_CB_T_MAX
};


/* global statistics */
struct hinic3_global_stats {
    uint32_t offload_flow_num;
    uint32_t offload_qpc_num;
    uint32_t same_flow_with_multi_gpa;
    uint32_t vxlan_rx_vtep_miss;
    uint32_t upcall_pf0_invalid_count;
    uint32_t upcall_pf0_rx_wqe_fail_dropped;
    uint32_t upcall_pf1_invalid_count;
    uint32_t upcall_pf1_rx_wqe_fail_dropped;

    uint64_t upcall_limit_drop_total;
    uint64_t upcall_from_vf_total;
    uint64_t upcall_from_hwbond_total;
    uint64_t flow_default_upcall;
    uint64_t flow_miss_upcall;
    uint64_t flow_invalid_upcall;
    uint64_t qu_prefetch_statics_fail;
};

#ifdef __cplusplus
}
#endif

#endif /* HINIC3_PROVIDER_H */
