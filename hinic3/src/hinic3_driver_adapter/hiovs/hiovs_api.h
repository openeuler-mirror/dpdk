 /* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2022 Huawei Technologies Co., Ltd
 */

#ifndef HIOVS_API_H
#define HIOVS_API_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "rte_mtr.h"
#include "hinic3_packets_types.h"
#include "hinic3_flow_agent_enum.h"
#include "hiovs_flexda_api_extra.h"
#ifdef __cplusplus
extern "C" {
#endif
struct eth_addr;
struct dp_packet;

struct hovs_port_capability {
    uint32_t supported_upcall_qnum; /* *< Max number of upcall queues for one port(vnic/hwbond). */
    uint8_t upcall_reuse;           /* *< Indicate whether upcall reuse is supported: 1-support, 0-not support */
    uint8_t rsvd[3];
};

struct hovs_port_upcall_info {
    uint32_t total_upcall_qnum; /* *< Number of total upcall queues on the vf(vport) */
    uint32_t left_upcall_qnum;  /* *< Number of idle queue on the vf(vport) */
    uint32_t rsvd[2];
};

enum hovs_fm_rule_event_type {
    HOVS_RULE_EVENT_BIT_RETRANSMISSION,
    HOVS_RULE_EVENT_BIT_TIMEOUT_RETRANSMISSION,
    HOVS_RULE_EVENT_BIT_DOWNTIME,
    HOVS_RULE_EVENT_MAX = 12
};

struct hovs_fm_rule_event {
    uint32_t event; /* *< rule event bit, see hovs_fm_rule_event_type */
    uint16_t rec[HOVS_RULE_EVENT_MAX];
    uint8_t ip_type; /* *< 0: ipv4, 1: ipv6 */
    uint8_t rule_id;
    uint16_t vport_id;
    uint32_t sip[4]; /* *< when ipv4, sip[0] is valid */
    uint32_t dip[4]; /* *< when ipv4, dip[0] is valid */
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t max_tcp_latency;  /* *< fix to zero */
    uint32_t max_icmp_latency; /* *< fix to zero */
};

struct hovs_fm_statistics_info {
    uint32_t max_tcp_retrans_count;       /* *< max tcp retransmission count */
    uint32_t max_tcp_downtime;            /* *< max tcp network outage */
    uint32_t max_tcp_timeout_retrans_cnt; /* *< max tcp timeout-retransmission count */
    uint32_t max_tcp_jitter;              /* *< fix to zero */
    uint32_t avg_tcp_time;                /* *< fix to zero */
    uint32_t max_icmp_jitter;             /* *< fix to zero */
    uint32_t avg_icmp_time;               /* *< fix to zero */
    uint32_t icmp_packet_lost;            /* *< fix to zero */
};

struct hovs_fm_vport_stats {
    uint32_t vport_id;
    struct hovs_fm_statistics_info stats;
};

enum hovs_fm_event_type {
    HOVS_FM_EVENT_PERIODICAL_VPORT_REPORT,
    HOVS_FM_EVENT_PERIODICAL_RULE_REPORT,
    HOVS_FM_EVENT_THRESHOLD_RULE_REPORT,
    HOVS_FM_EVENT_MAX
};

struct hovs_fm_report {
    uint16_t report_type; /* *< see hovs_fm_event_type */
    union {
        struct hovs_fm_vport_stats stats_report;
        struct hovs_fm_rule_event rule_report;
    };
};

struct hovs_flow_capability {
    uint32_t vendor_id;           /* *< Unique identifier */
    uint32_t supported_dev_types; /* *< Bitmap of ports in "enum hovs_port_type" */
    uint32_t supported_actions;   /* *< Bitmap of actions in "enum hinic3_flow_action_type" */
    uint8_t key_format_type;      /* *< Bitmap of key format in "enum hinic3_key_format" */
    uint8_t cleanup_max_level;    /* *< 0 : don't cleanup hardware flow even hardware table is full */
    uint16_t flags;               /* *< Bitmap of cap in HOVS_ENG_CAP_F_xxx */
    uint32_t max_flow_size;       /* *< Max flow context num */
    uint32_t rx_thread_num;       /* *< Max rx thread num for flow response pull */
};

/**
 * Reinject packet to NIC from OVS, send the packet to vf or bond port.
 *
 * @param dpdk_port_id  unused.
 * @param dpdk_queue_id reinject queue id.
 * @param tx_pkts       The address of an array of *nb_pkts* pointers to *udk_mbuf* structures
 * which contain the output packets.
 * @param nb_pkts       The maximum number of packets to transmit.
 * @return
 * - The number of transmitted packets
 */
uint16_t hovs_rte_tx_burst(uint16_t dpdk_port_id, uint16_t dpdk_queue_id, void **tx_pkts, uint16_t nb_pkts);

/**
 * Upcall packet to OVS from NIC, receive packet to ovs.
 *
 * @param      dpdk_port_id  sub table for flow flush, others unused
 * @param      dpdk_queue_id queue id for upcall packet, HINIC3_PTHREAD_ACK/HINIC3_PTHREAD_AGING for flow
 * @param[out] rx_pkts       The address of an array of pointers to *udk_mbuf* structures that
 * must be large enough to store *nb_pkts* pointers in it.
 * @param[out] nb_pkts       The maximum number of packets to retrieve.
 * @return
 * - The number of received packets
 */
uint16_t hovs_rte_rx_burst(uint16_t dpdk_port_id, uint16_t dpdk_queue_id, void **rx_pkts, uint16_t nb_pkts);

int16_t hovs_rte_get_txq_backlog(uint16_t vport_id, uint16_t hiovs_queue_id);
int16_t hovs_rte_get_rxq_backlog(uint16_t vport_id, uint16_t hiovs_queue_id);

/**
 * vPort API Class
 */
struct hovs_port_stats {
    uint64_t rx_pkts;              /* *< Total packets received. */
    uint64_t tx_pkts;              /* *< Total packets transmitted. */
    uint64_t rx_bytes;             /* *< Total bytes received. */
    uint64_t tx_bytes;             /* *< Total bytes transmitted. */
    uint64_t rx_errors;            /* *< Bad packets received. */
    uint64_t tx_errors;            /* *< Packet transmit problems. */
    uint64_t rx_dropped;           /* *< No buffer space. */
    uint64_t tx_dropped;           /* *< No buffer space. */
    uint64_t tx_multicast_dropped; /* *< No buffer space. */
    uint64_t collisions;

    /* Detailed receive errors. */
    uint64_t rx_length_errors;
    uint64_t rx_over_errors;   /* *< Receiver ring buff overflow. */
    uint64_t rx_crc_errors;    /* *< Recved pkt with crc error. */
    uint64_t rx_frame_errors;  /* *< Recv'd frame alignment error. */
    uint64_t rx_fifo_errors;   /* *< Recv'r fifo overrun . */
    uint64_t rx_missed_errors; /* *< Receiver missed packet. */

    /* Detailed transmit errors. */
    uint64_t tx_aborted_errors;
    uint64_t tx_carrier_errors;
    uint64_t tx_fifo_errors;
    uint64_t tx_heartbeat_errors;
    uint64_t tx_window_errors;

    uint64_t timestamp;

    /* ucode counter */
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
    uint32_t ct_dropped; /* *< tx_drop:vport type is function; else rx_drop */
    uint32_t upcall_wqe_fail_dropped;

    uint64_t upcall_pkt_num_vport;
    uint64_t tx_uc_bytes; /* *< only valid when vport type is function */
    uint64_t tx_uc_pkts;
    uint64_t tx_bc_bytes; /* *< only valid when vport type is function */
    uint64_t tx_bc_pkts;
    uint64_t tx_mc_bytes; /* *< only valid when vport type is function */
    uint64_t tx_mc_pkts;
    uint64_t rx_uc_bytes; /* *< only valid when vport type is function */
    uint64_t rx_uc_pkts;
    uint64_t rx_bc_bytes; /* *< only valid when vport type is function */
    uint64_t rx_bc_pkts;
    uint64_t rx_mc_bytes; /* *< only valid when vport type is function */
    uint64_t rx_mc_pkts;

    uint32_t upcall_no_sge_dropped; /* upcall pre-drop without sge */
    uint32_t rss_q_invalid_dropped; /* rss queue invalid drop, only valid for vport */
    uint32_t rq_bp_dropped;         /* only valid when vport type is function */
    uint32_t lacp_rx_nowqe_dropped; /* only valid when vport type is bond */
    uint16_t ipfrag_upcall_limit_dropped;
    uint16_t rsvd0;
    uint32_t rsvd[5];
};

/**
 * global statistics
 */
struct hovs_global_stats {
    uint32_t offload_flow_num;
    uint32_t offload_qpc_num;
    uint32_t same_flow_with_multi_gpa;
    uint32_t vxlan_rx_vtep_miss;
    uint32_t upcall_pf_invalid_dropped[6];
    uint32_t upcall_pf_rx_wqe_fail_dropped[6];

    uint64_t upcall_limit_drop_total;
    uint64_t upcall_from_vf_total;
    uint64_t upcall_from_hwbond_total;
    uint64_t flow_default_upcall;
    uint64_t flow_miss_upcall;
    uint64_t flow_invalid_upcall;
    uint64_t qu_prefetch_statics_fail;

    uint32_t tso_space_invalid_dropped;
    uint32_t output_type_invalid_dropped;
    uint32_t lacp_tx_dst_err_dropped;
    uint32_t lldp_tx_dst_err_dropped;
    uint32_t arp_tx_dst_err_dropped;
    uint32_t p0_rx_bp_drop;
    uint32_t p1_rx_bp_drop;
    uint32_t p2_rx_bp_drop;
    uint32_t p3_rx_bp_drop;
    uint32_t rsvd[5];
};

/* qos_info */
struct hovs_mtr_user_data
{
    uint32_t acl_index : 5;
    uint32_t acl_tag_en : 1;
    uint32_t flow_qos_id : 10;
    uint32_t flow_qos_type : 2;
    uint32_t flow_qos_en : 1;
    uint32_t rsvd : 13;
};

struct hovs_flow_stats {
    uint64_t packet_count; /* *< Number of packets matched. */
    uint64_t byte_count;   /* *< Number of bytes matched. */
    uint64_t ct_loss_pkts; /* *< Number of packets dropped for CT failure. */
    uint32_t live_time;    /* *< Remain life time of flow, unit : ms */
    uint32_t age_time;     /* *< Total life time of flow, unit : ms */
    uint16_t tcp_flags;
    uint8_t block[128];
};

enum qos_type_limit {
    QOS_TYPE_FUNC_LIMIT = 0,
    QOS_TYPE_VM_LIMIT,
    QOS_TYPE_FLOW_LIMIT,
    QOS_TYPE_NET_LIMIT,
    QOS_TYPE_MAX,
};

struct hovs_qos_stats {
    uint32_t qos_type; /* *< 0:func limit  1:vm limit */
    uint32_t index;    /* *< when qos_type=0: index=func_id, qos_type=1:index=vm_id */
    uint32_t rx_drop_num;
    uint32_t tx_drop_num;
    uint32_t reserved0; /* *< reserved for later */
    uint32_t reserved1; /* *< reserved for later */
};

struct hovs_dpif_flow {
    const struct nlattr *key;     /* *< Flow to Put */
    size_t key_len;               /* *< length of key in bytes */
    const struct nlattr *mask;    /* *< mask to put */
    size_t mask_len;              /* *< length of mask in bytes */
    const struct nlattr *actions; /* *< actions to perform on flow */
    size_t action_len;
    uint8_t mask_present;
    uint64_t hw_ufid;
    struct hovs_flow_stats stats;
};

struct hovs_dpif_flow_for_get {
    struct nlattr *key;     /* *< Flow to get */
    size_t key_len;         /* *< length of key in bytes */
    struct nlattr *mask;    /* *< mask to get */
    size_t mask_len;        /* *< length of mask in bytes */
    struct nlattr *actions; /* *< actions to perform on flow */
    size_t action_len;
    uint8_t mask_present;
    uint64_t hw_ufid;
    uint64_t related_hw_ufid;
    struct hovs_flow_stats stats;
};

struct hovs_flexda_dpif_flow {
    const struct nlattr *key;     /* *< Flow to Put */
    size_t key_len;               /* *< length of key in bytes */
    const struct nlattr *mask;    /* *< mask to put */
    size_t mask_len;              /* *< length of mask in bytes */
    const struct nlattr *actions; /* *< actions to perform on flow */
    size_t action_len;
    uint8_t mask_present;
    uint64_t hw_ufid;
    struct hovs_flow_stats stats;
};

struct hovs_eth_addr {
    union {
        uint8_t ea[6];
        uint16_t be16[3]; /* *< network order */
    };
};

struct hovs_src_ipmac {
    uint32_t ip_addr;
    struct hovs_eth_addr mac_addr;
};

struct hovs_slave_info {
    struct nlattr *slave_info;
    uint32_t args_len;
};

struct hovs_pci_addr {
    uint32_t domain;  /* Device domain */
    uint8_t bus;      /* Device bus */
    uint8_t devid;    /* Device ID */
    uint8_t function; /* Device function */
};

struct hovs_pci_addr_info {
    struct hovs_pci_addr pci_addr;
    uint8_t type;
    union {
        uint16_t max_queue_num;
        uint16_t glb_func_inx;
    };
};

struct hovs_phy_dev_info {
    uint32_t phy_dev_num;
    struct hovs_pci_addr_info pci_info[MAX_PHY_DEV_NUM];
};

struct hovs_acl_indir_stats {
    uint64_t packet_count;
    uint64_t byte_count;
};

struct hovs_bond_slave_stats {
    uint64_t vld_slave;
    uint64_t rx_pkts[4];
    uint64_t tx_pkts[4];
    uint64_t rx_bytes[4];
    uint64_t tx_bytes[4];
};

struct hovs_acl_stats {
    uint64_t packet_count; /* *< Number of packets matched. */
    uint64_t byte_count;
};

struct hovs_dpif_acl {
    const struct nlattr *key;
    size_t key_len;
    const struct nlattr *mask;
    size_t mask_len;
    const struct nlattr *actions;
    size_t action_len;
    uint8_t group_id;
    uint8_t index;
};

struct hovs_dpif_acl_for_get {
    struct nlattr *key;     /* *< ACL to Put */
    size_t key_len;         /* *< length of key in bytes */
    struct nlattr *mask;    /* *< mask to put */
    size_t mask_len;        /* *< length of mask in bytes */
    struct nlattr *actions; /* *< actions to perform on acl */
    size_t action_len;
    uint8_t group_id;
    uint8_t index;
    struct hovs_acl_stats stats;
};

/**
 * Add a virtual NIC port and associate it with a VF.
 *
 * @param[out] port_id  Output the generated port ID after the vNIC is add successfully.
 * @param      pci_addr The PCI address of the port to be added.
 * @return
 * - HIOVS_OK:    if successful.
 * - HIOVS_EEXEC: if api execute fail.
 * - HIOVS_ERROR: if the queue for this port is not enough.
 */
int hovs_port_mgmt_add(uint16_t *port_id, void *pci_addr);

/**
 * Add a virtual NIC port that supports dynamic queues and associate it with a VF.
 *
 * @param[out] port_id  Output the generated port ID after the vNIC is add successfully.
 * @param      args     TLV format: configuration information about the port.
 * Including the PCIe address, number of upcall queues, and dynamic queue information.
 * @param      args_len The length of the parameter args.
 * @return
 * - HIOVS_OK:    if successful.
 * - HIOVS_EEXEC: if API execute fail.
 * - HIOVS_ERROR: if the parameter check fails.
 */
int hovs_port_mgmt_add_dynamic(uint16_t *port_id, const struct nlattr *args, uint32_t args_len);

/**
 * Delete a virtual NIC port
 *
 * @param port_id  The port_id of vNIC to be deleted.
 */
void hovs_port_mgmt_del(uint16_t port_id);

/**
 * Query a virtual NIC port base on args.
 *
 * @param[out] port_id  Output the generated port ID base on args.
 * @param      args     TLV format: configuration information about the port.
 * Including the HINIC3_PORT_ARG_PCI_ADDR, HINIC3_PORT_ARG_FAKE_BDF.
 * @param      args_len The length of the parameter args.
 * @return
 * - HIOVS_OK:    if successful.
 * - HIOVS_ERROR: if the parameter check fails or the execute fails.
 */
int hovs_port_mgmt_query_flexible(const struct nlattr *args, uint32_t args_len, uint16_t *port_id);

/* *
 * Query the configuration of a port. Supports query configurations in batches.
 *
 * @param      port_id  The port_id of vNIC to be queried.
 * @param[out] args     TLV format: List of attribute obtained by querying. @ref enum hinic3_port_arg_type
 * - HINIC3_PORT_ARG_DPDK_PORT_ID
 * - HINIC3_PORT_ARG_PORT_UPCALL_QUEUE_MAP
 * @param[out] args_len The length of the result args.
 * @return
 * - HIOVS_OK:    if successful.
 * - HIOVS_ERROR: if the parameter check fails or the execute fails.
 */
int hovs_port_mgmt_get(uint16_t port_id, struct nlattr *args, uint32_t *args_len);

/**
 * Set the configuration of a port. Supports set configurations in batches.
 *
 * @param port_id       The port_id of vNIC to be set.
 * @param args          TLV format: List of attributes to be set. @ref enum hinic3_port_arg_type
 * - HINIC3_PORT_ARG_VLAN_TAG
 * - HINIC3_PORT_ARG_VNI
 * - HINIC3_PORT_ARG_BUCKET_ID
 * @param args_len      The length of the parameter args.
 * @param unset_args    TLV format: unsupport.
 * @param unset_args_len    The length of the parameter unset_args.
 * @return
 * - HIOVS_OK:    if successful.
 * - HIOVS_ERROR: if the parameter check fails or the execute fails.
 */
int hovs_port_mgmt_set(uint16_t port_id, const struct nlattr *args, uint32_t args_len, struct nlattr *unset_args,
    uint32_t *unset_args_len);

/**
 * Get port capability.
 *
 * @param cap       Port capability.
 * @return
 * - HIOVS_OK:    if successful.
 * - HIOVS_ERROR: if the parameter check fails or the execute fails.
 */
int hovs_port_mgmt_get_capability(struct hovs_port_capability *cap);

/**
 * Get upcall info.
 *
 * @param info      Port upcall info. Include the number of total upcall queues and the left number of upcall queues.
 * @return
 * - HIOVS_OK:    if successful.
 * - HIOVS_ERROR: if the parameter check fails or the execute fails.
 */
int hovs_port_mgmt_get_upcall_info(struct hovs_port_upcall_info *info);


int hovs_port_mgmt_setup_upcall_queue(uint16_t port_id, uint16_t qid, unsigned int socket_id, void *mp);
/**
 * Create bond.
 * This interface is used to create a virtual bond port.
 *
 * @param      mode unused
 * @param      name The name of bond device.
 * @param[out] bond_port_id  Output the bond_port_id after the bond is created successfully.
 * @return
 * - HIOVS_OK:    if successful.
 * - HIOVS_ERROR: if the parameter check fails or the execute fails.
 */
int hovs_bond_mgmt_create(uint8_t mode, const char *name, uint16_t *bond_port_id);

/**
 * Delete bond.
 * This interface is used to delete the bond port and delete the synchronization between the bond port and
 * linux-kernel bonding.
 *
 * @param bond_port_id  The ID of bond_port to be deleted.
 */
void hovs_bond_mgmt_delete(uint16_t bond_port_id);

/**
 * Get bond configuration.
 * The ovs bond is synchronized from the linux-kernel bonding interface. This interface is used to get the linux-kernel
 * bonding status.
 *
 * @param      bond_port_id  The ID of bond_port.
 * @param[out] args     TLV format: List of attribute obtained by querying. @ref enum hinic3_bond_arg_type
 * - HINIC3_BOND_ARG_UPLINK_PCI_ID
 * - HINIC3_BOND_ARG_SLAVES
 * - HINIC3_BOND_ARG_SLAVE_NAME
 * @param[out] args_len The length of the parameter args.
 * @return
 * - HIOVS_OK:    if successful.
 * - HIOVS_ERROR: if the parameter check fails  or the execute fails.
 */
int hovs_bond_mgmt_get(uint16_t bond_port_id, struct nlattr *args, uint32_t *args_len);

/**
 * Get the mac address of vport.
 *
 * @param      port_id  The vport id, only support the mac address of bond port can be query. unsupport vf port now.
 * @param[out] mac      The mac address.
 * @return
 * - HIOVS_OK:    if successful.
 * - HIOVS_ERROR: if the parameter check fails or the execute fails.
 */
int hovs_etheraddr_get(uint16_t port_id, struct hovs_eth_addr *mac);

/**
 * Set the mac address of vport.
 *
 * @param port_id   The vport id, only support the mac address of bond port can be set. unsupport vf port now.
 * @param mac       The mac address.
 * @return
 * - HIOVS_OK:    if successful.
 * - HIOVS_ERROR: if the parameter check fails or the execute fails.
 */
int hovs_etheraddr_set(uint16_t port_id, const struct hovs_eth_addr mac);

/**
 * Set the mtu of vport.
 *
 * @param port_id   The vport id, only support the mtu of bond port can be set. unsupport vf port now.
 * @param mtu       The mtu [256, 9216].
 * @return
 * - HIOVS_OK:    if successful.
 * - HIOVS_ERROR: if the parameter check fails or the execute fails.
 */
int hovs_mtu_set(uint16_t port_id, int mtu);

/**
 * Get the statistics of vNIC.
 *
 * @param       port_id The port_id of vNIC.
 * @param[out]  stats   Output the statistics info of vNIC.
 * @return
 * - HIOVS_OK:    if successful.
 * - HIOVS_ERROR: if API execute fail.
 */
int hovs_port_statistics_get(uint16_t port_id, struct hovs_port_stats *stats);

/**
 * Clear the statistics of vNIC.
 *
 * @param       port_id The port_id of vNIC.
 * @return
 * - HIOVS_OK:    if successful.
 * - HIOVS_ERROR: if the parameter check fails or the execute fails.
 */
int hovs_port_statistics_flush(uint16_t port_id);

/**
 * Get BUM configuration.
 *
 * @param      port_id  The port_id of vNIC.
 * @param[out] args     TLV format: List of attribute obtained by querying. @ref enum hinic3_bum_arg_type
 * - HINIC3_BUM_ARG_SRC_MAC
 * - HINIC3_BUM_ARG_BRD_RATELIMIT
 * - HINIC3_BUM_ARG_ETHER_TYPE_CHECK
 * - HINIC3_BUM_ARG_EXTRA_ETH_TYPE
 * @param[out] args_len The length of the parameter args.
 * @return
 * - HIOVS_OK:    if successful.
 * - HIOVS_ERROR: if the parameter check fails or the execute fails.
 */
int hovs_bum_get(uint16_t port_id, struct nlattr *args, uint32_t *args_len);

int hovs_bum_set(uint16_t port_id, const struct nlattr *args, uint32_t args_len, struct nlattr *unset_args,
    uint32_t *unset_args_len);

int hovs_bum_remove(uint16_t port_id, const struct nlattr *args, uint32_t args_len);

/**
 * Clear BUM configuration.
 *
 * @param bond_port_id  The port_id of vNIC.
 * @return
 * - HIOVS_OK:    if successful.
 * - HIOVS_ERROR: if the parameter check fails or the execute fails.
 */
int hovs_bum_clear(uint16_t port_id);

/**
 * Get the forward mode of the hardware offload flow.
 *
 * @param[out] forward_mode The forward mode obtained from the NIC.
 * @return
 * - HIOVS_OK:    if successful.
 * - HIOVS_ERROR: if the parameter check fails or the execute fails.
 */
int hovs_flow_mgmt_get_forward_mode(uint8_t *forward_mode);

int hovs_flow_mgmt_set_forward_mode(uint8_t forward_mode);

int hovs_flow_mgmt_put(const struct hovs_dpif_flow *put, const struct nlattr *args, size_t args_len);

/**
 * Get a hardware offload flow table based on the key.
 *
 * @param key      The flow key.
 * @param key_len  The length of flow key.
 * @param[out] get  Obtained flow table result
 * @return
 *   - HIOVS_OK:     if successful.
 *   - HIOVS_ERROR:  if the parameter check fails or the execute fails.
 *   - HIOVS_EEMPTY: if flow is not existed.
 */
int hovs_flow_mgmt_get_by_key(const struct nlattr *key, size_t key_len,
    struct hovs_dpif_flow_for_get *get);

int hovs_flow_mgmt_get_by_ufid(uint64_t ufid, struct hovs_dpif_flow_for_get *get);

int hovs_flow_mgmt_del_by_key(const struct nlattr *key, size_t key_len);

int hovs_flow_mgmt_del_by_ufid(uint64_t ufid);

int hovs_flow_mgmt_del_by_batch(const uint64_t *ufids, struct hovs_dpif_flow_for_get **flows, const size_t cnt);


int hovs_flow_mgmt_flush(void);


int hovs_flow_mgmt_dump_start(void **state);

int hovs_flow_mgmt_dump_next(void *state, struct hovs_dpif_flow_for_get *dump);

int hovs_flow_mgmt_dump_done(void *state);

int hovs_flow_mgmt_get_maxflows(uint32_t *max_flows);

int hovs_flow_mgmt_get_capability(struct hovs_flow_capability *cap);

int hovs_statistics_flow_get_by_ufid(const uint64_t *ufid, const size_t cnt, struct hovs_flow_stats *stats);

/**
 * Flush the the flow statistics in batch.
 *
 * @param ufid  The ID of united flow.
 * @return
 * - HIOVS_OK:     if successful.
 * - HIOVS_ERROR:  if the parameter check fails or the execute fails.
 */
int hovs_statistics_flow_flush_by_ufid(uint64_t ufid);

typedef int (*hovs_flow_callback_t)(uint64_t ufid, const struct hovs_dpif_flow_for_get *flow, const struct nlattr *args,
    size_t args_len);


hovs_flow_callback_t hovs_flow_callback_register(hovs_flow_callback_t cb);

typedef int (*hovs_flexda_flow_callback_t)(uint32_t table_id, uint64_t ufid, const struct hovs_dpif_flow_for_get *flow, const struct nlattr *args,
size_t args_len);

hovs_flexda_flow_callback_t hovs_flexda_flow_callback_register(hovs_flexda_flow_callback_t cb);

int hovs_flexda_mml_lib(const char *buf_in, uint32_t in_size, char *buf_out, uint32_t *out_len, uint32_t max_buf_out_len);

int hovs_global_cfg_set(const struct nlattr *args, size_t args_len, struct nlattr *unset_args, size_t *unset_args_len);

int hovs_global_cfg_get(struct nlattr *args, size_t *args_len);

/**
 * Get the the device feature, include default feature, support feature.
 *
 * @param[out] stats Output default feature, support feature.
 * @return
 * - HIOVS_OK:     if successful.
 * - HIOVS_ERROR:  if the parameter check fails or the execute fails.
 */
int hovs_global_device_feature_get(uint64_t *default_device_feature, uint64_t *support_device_feature);

/**
 * Get the the global statistics, include offload stats, upcall stats, other drop stats.
 *
 * @param[out] stats Output the the global statistics.
 * @return
 * - HIOVS_OK:     if successful.
 * - HIOVS_ERROR:  if the parameter check fails or the execute fails.
 */
int hovs_global_statistics_get(struct hovs_global_stats *stats);

/**
 * Flush the the global statistics.
 *
 * @return
 * - HIOVS_OK:     if successful.
 * - HIOVS_ERROR:  if the parameter check fails or the execute fails.
 */
int hovs_global_statistics_flush(void);

int hovs_mml_lib(const char *buf_in, uint32_t in_size, char *buf_out, uint32_t *out_len, uint32_t max_buf_out_len);
/**
 * hiovs lib initialize.
 *
 * @param arg Input parameters: @ref struct hiovs_lib_arg
 * @return
 * - HIOVS_OK:     if successful.
 * - HIOVS_ERROR:  if the parameter check fails or the execute fails.
 */
int32_t hovs_lib_init(void *arg);

int hovs_lib_deinit(void *arg);

int hovs_open_log(uint32_t module_type, uint32_t enable);

int hovs_set_log_level(uint32_t module_type, uint32_t log_level);

struct hovs_qos_stats_batch {
    union {
        uint32_t rx_drop_num;
        uint32_t flow_qos_drop_num;
    };
    uint32_t tx_drop_num;
};

int hovs_qos_statistics_get_batch(uint16_t type, uint16_t *ids, struct hovs_qos_stats_batch *stats, size_t cnt);

struct hovs_qos_stats_batch_all {
    struct hovs_qos_stats_batch drop_pkg_num;
    union {
        uint64_t rx_drop_byte_num;
        uint64_t flow_qos_drop_byte_num;
    };
    uint64_t tx_drop_byte_num;
};

struct hovs_hqos_stats_batch_all {
    uint64_t tx_pkts[RTE_COLORS];   // 流表级限速不区分方向，只使用tx表示绿色报文
    uint64_t tx_bytes[RTE_COLORS];  // 流表级限速不区分方向，只使用tx表示绿色报文
    uint64_t tx_drop_pkts;  // 流表级限速的模式下，tx代表丢包
    uint64_t tx_drop_bytes; // 流表级限速的模式下，tx代表丢包
    uint64_t rx_pkts[RTE_COLORS];
    uint64_t rx_bytes[RTE_COLORS];
    uint64_t rx_drop_pkts;
    uint64_t rx_drop_bytes;
};

int hovs_qos_statistics_get_all_batch(uint16_t type, uint16_t *ids, struct hovs_qos_stats_batch_all *stats,
    size_t cnt);

/**
 * Get the QoS for stats.
 *
 * @param type            The type of QoS, [0:func limit, 1:vm limit, 3:flow limit, 4:net limit].
 * @param ids             The id of QoS, [func limit: vport_id, vm limit: vm_id, flow limit: ufid, net limit: net_id].
 * @param[out] stats      The QoS statistics.
 * @param[out] cnt        Array size of ids.
 * @return
 * - HIOVS_OK:    if successful.
 * - HIOVS_ERROR: if the parameter check fails or the execute fails.
 */
int hovs_hqos_statistics_get_all_batch(uint16_t type, uint16_t *ids, struct hovs_hqos_stats_batch_all *stats,
    size_t cnt);

/**
 * Clear the QoS for stats.
 *
 * @param type            The type of QoS, [0:func limit, 1:vm limit, 3:flow limit, 4:net limit].
 * @param ids             The id of QoS, [func limit: vport_id, vm limit: vm_id, flow limit: ufid, net limit: net_id].
 * @param[out] cnt        Array size of ids.
 * @return
 * - HIOVS_OK:    if successful.
 * - HIOVS_ERROR: if the parameter check fails or the execute fails.
 */
int hovs_hqos_statistics_clear_batch(uint16_t type, uint16_t dir, uint16_t *ids, size_t cnt);

/* *
 * Set the QoS limiting for a virtual machine.
 *
 * @attention  Port rate limiting and vm rate limiting cannot be used at the same time.
 * @param group_id  The ID of virtual machine, [1 ~ 255].
 * @param dir       0-ingress(vm -> NIC, vm tx), 1-egress(NIC -> vm, vm rx), 2-both ingress and egress
 * @param type      0-bandwidth, 1-pps
 * @param max_rate  PIR : min_rate ~ 400000000 kbps(400Gbps) for bandwidth.
 * min_rate ~ 128000 pps for pps.
 * If the rate is 0, configure the value as the maximum value.
 * @param max_burst PBS : min_burst ~ 2560000 kbits(2.56Gbits) for bandwidth.
 * min_burst ~ 1000 packets for pps
 * If the burst is 0, configure the value as the maximum value.
 * @param min_rate  CIR : 0 ~ 400000000 kbps(400Gbps) for bandwidth.
 * 0 ~ 128000 pps for pps.
 * If the rate is 0, configure the value as the maximum value.
 * @param min_burst CBS : 0 ~ 2560000 kbits(2.56Gbits) for bandwidth.
 * 0 ~ 1000 packets for pps.
 * If the burst is 0, configure the value as the maximum value.
 * @return
 * - HIOVS_OK:    if successful.
 * - HIOVS_ERROR: if the parameter check fails or the execute fails.
 */
int hovs_qos_vm_limit_set(uint16_t group_id, uint16_t dir, uint16_t type, uint64_t max_rate, uint64_t max_burst,
    uint64_t min_rate, uint64_t min_burst);

/**
 * Get the QoS limiting for a virtual machine.
 *
 * @attention  Port rate limiting and vm rate limiting cannot be used at the same time.
 * @param group_id        The ID of virtual machine.
 * @param dir             0-ingress(vm -> NIC, vm tx), 1-egress(NIC -> vm, vm rx), 2-both ingress and egress
 * @param type            0-bandwidth, 1-pps
 * @param[out] max_rate   VM PIR/kbps/pps      obtained from the NIC.
 * @param[out] max_burst  VM PBS/kbits/packets obtained from the NIC.
 * @param[out] min_rate   VM CIR/kbps/pps      obtained from the NIC.
 * @param[out] min_burst  VM CBS/kbits/packets obtained from the NIC.
 * @return
 * - HIOVS_OK:    if successful.
 * - HIOVS_ERROR: if the parameter check fails or the execute fails.
 */
int hovs_qos_vm_limit_get(uint16_t group_id, uint16_t dir, uint16_t type, uint64_t *max_rate, uint64_t *max_burst,
    uint64_t *min_rate, uint64_t *min_burst);

/**
 * Set the QoS limiting for a virtual machine using srTCM.
 *
 * @attention  1. Port rate limiting and vm rate limiting cannot be used at the same time.
 * 2. Call the trTCM hardware engine to simulate srTCM algorithm interface.
 * 3. If CIR is 0 or maximum, configure CIR and CBS as the maximum value, configure EBS as 0.
 * 4. If CBS is 0, configure CBS as the maximum value.
 * 5. If EBS is 0, the algorithm changes to single rate single bucket.
 * 6. CBS + EBS <= max and EBS < max, max is 2560000 kbits(2.56Gbits) for bandwidth or 1000 packets for pps.
 * @param group_id  The ID of virtual machine.
 * @param dir       0-ingress(vm -> NIC, vm tx), 1-egress(NIC -> vm, vm rx), 2-both ingress and egress
 * @param type      0-bandwidth, 1-pps
 * @param min_rate  CIR : 0 ~ 400000000 kbps(400Gbps) for bandwidth.
 * 0 ~ 128000 pps for pps.
 * @param min_burst CBS : 0 ~ (2560000 - EBS) kbits(2.56Gbits) for bandwidth.
 * 0 ~ (1000 - EBS) packets for pps.
 * @param exs_burst EBS : 0 ~ (2560000 - CBS) kbits(2.56Gbits) for bandwidth.
 * 0 ~ (1000 - CBS) packets for pps.
 * @return
 * - HIOVS_OK:    if successful.
 * - HIOVS_ERROR: if the parameter check fails or the execute fails.
 */
int hovs_qos_vm_srtcm_limit_set(uint16_t group_id, uint16_t dir, uint16_t type,
    uint64_t min_rate, uint64_t min_burst, uint64_t exs_burst);

/**
 * Get the QoS limiting for a virtual machine using srTCM.
 *
 * @attention  1. Port rate limiting and vm rate limiting cannot be used at the same time.
 * 2. Call the trTCM hardware engine to simulate srTCM algorithm interface.
 * @param group_id        The ID of virtual machine.
 * @param dir             0-ingress(vm -> NIC, vm tx), 1-egress(NIC -> vm, vm rx), 2-both ingress and egress
 * @param type            0-bandwidth, 1-pps
 * @param[out] min_rate   VM CIR/kbps/pps      obtained from the NIC.
 * @param[out] min_burst  VM CBS/kbits/packets obtained from the NIC.
 * @param[out] exs_burst  VM EBS/kbits/packets obtained from the NIC.
 * @return
 * - HIOVS_OK:    if successful.
 * - HIOVS_ERROR: if the parameter check fails or the execute fails.
 */
int hovs_qos_vm_srtcm_limit_get(uint16_t group_id, uint16_t dir, uint16_t type,
    uint64_t *min_rate, uint64_t *min_burst, uint64_t *exs_burst);

/* *
 * Set the QoS limiting for a port.
 *
 * @param port_id   The ID of port, [1 ~ 4095].
 * @param dir       0-ingress(vm -> NIC, vm tx), 1-egress(NIC -> vm, vm rx)
 * @param type      0-bandwidth, 1-pps
 * @param max_rate  PIR : min_rate ~ 400000000 kbps(400Gbps) for bandwidth.
 * min_rate ~ 128000 pps for pps.
 * If the rate is 0, configure the value as the maximum value.
 * @param max_burst PBS : min_burst ~ 2560000 kbits(2.56Gbits) for bandwidth.
 * min_burst ~ 1000 packets for pps
 * If the burst is 0, configure the value as the maximum value.
 * @param min_rate  CIR : 0 ~ 400000000 kbps(400Gbps) for bandwidth.
 * 0 ~ 128000 pps for pps.
 * If the rate is 0, configure the value as the maximum value.
 * @param min_burst CBS : 0 ~ 2560000 kbits(2.56Gbits) for bandwidth.
 * 0 ~ 1000 packets for pps.
 * If the burst is 0, configure the value as the maximum value.
 * @return
 * - HIOVS_OK:    if successful.
 * - HIOVS_ERROR: if the parameter check fails or the execute fails.
 */
int hovs_qos_vport_limit_set(uint16_t port_id, uint16_t dir, uint16_t type,
    uint64_t max_rate, uint64_t max_burst, uint64_t min_rate, uint64_t min_burst);

/**
 * Get the QoS limiting for a port.
 *
 * @param port_id        The ID of port.
 * @param dir             0-ingress(vm -> NIC, vm tx), 1-egress(NIC -> vm, vm rx)
 * @param type            0-bandwidth, 1-pps
 * @param[out] max_rate   VM PIR/kbps/pps      obtained from the NIC.
 * @param[out] max_burst  VM PBS/kbits/packets obtained from the NIC.
 * @param[out] min_rate   VM CIR/kbps/pps      obtained from the NIC.
 * @param[out] min_burst  VM CBS/kbits/packets obtained from the NIC.
 * @return
 * - HIOVS_OK:    if successful.
 * - HIOVS_ERROR: if the parameter check fails or the execute fails.
 */
int hovs_qos_vport_limit_get(uint16_t port_id, uint16_t dir, uint16_t type,
    uint64_t max_rate, uint64_t max_burst, uint64_t min_rate, uint64_t min_burst);

/**
 * Set the QoS limiting for a net.
 *
 * @param dir             0-ingress(net -> NIC, net tx), 1-egress(NIC -> net, net rx)
 * @param type            0-bandwidth, 1-pps
 * @param[out] max_rate   net PIR/kbps/pps      obtained from the NIC.
 * @param[out] max_burst  net PBS/kbits/packets obtained from the NIC.
 * @return
 * - HIOVS_OK:    if successful.
 * - HIOVS_ERROR: if the parameter check fails or the execute fails.
 */
int hovs_qos_net_limit_set(uint16_t host_id, uint16_t dir, uint16_t type, uint64_t max_rate, uint64_t max_burst);

/**
 * Set the QoS limiting for a net.
 *
 * @param dir             0-ingress(net -> NIC, net tx), 1-egress(NIC -> net, net rx)
 * @param type            0-bandwidth, 1-pps
 * @param[out] max_rate   net PIR/kbps/pps      obtained from the NIC.
 * @param[out] max_burst  net PBS/kbits/packets obtained from the NIC.
 * @return
 * - HIOVS_OK:    if successful.
 * - HIOVS_ERROR: if the parameter check fails or the execute fails.
 */
int hovs_qos_net_limit_get(uint16_t host_id, uint16_t dir, uint16_t type, uint64_t *max_rate, uint64_t *max_burst);

/**
 * Get the pcie list.
 * @attention  only support front_back is 1, which means get front-end pcie list.
 * @param front_back  1-front end, 0-back end.
 * @param bdf_type    see enum hiovs_bdf_type
 * @param[out] dev    pcie list
 * @return
 *   - HIOVS_OK:    if successful.
 *   - HIOVS_ERROR: if the parameter check fails or the execute fails.
 */
int hovs_global_pcie_list_query(uint8_t front_back, uint8_t bdf_type, struct hovs_phy_dev_info *dev);

/**
 * Set high priority upcall queue
 *
 * @param  port_id  The indirect id to be freed
 * @param  cfgs  The Array of constraints
 * @param  config_num  Length of the cfgs array
 * @param  upcall_queue_num Length of the upcall queue
 * @return
 *   - HIOVS_OK:     if successful.
 *   - HIOVS_ERROR:  if the parameter check fails or the execute fails.
 */
int hovs_port_mgmt_set_upcall_priority(uint16_t port_id,
                                       struct hovs_high_priority_protocol_cfg cfgs[],
                                       uint32_t config_num,
                                       uint8_t upcall_queue_num);


int hovs_bond_slave_statistics_get(uint16_t port_id, struct hovs_bond_slave_stats *stats);

int hovs_port_mgmt_set_usage_state(uint16_t port_id, uint16_t status);

int hovs_port_mgmt_get_usage_state(uint16_t port_id, uint32_t *device_status);

int hovs_upcall_mtu_set(uint8_t type, int mtu);

int hovs_mega_flow_mgmt_put(const struct hovs_dpif_flow *put, uint32_t index, uint64_t *ufid);

int hovs_mega_flow_mgmt_del_by_ufid(uint64_t ufid);

int hovs_mega_flow_mgmt_flush(void);

int hovs_mega_flow_mgmt_dump_start(void **state);

int hovs_mega_flow_mgmt_dump_next(void *state, struct hovs_dpif_flow_for_get *dump);

int hovs_mega_flow_mgmt_dump_done(void *state);

int hovs_statistics_mega_flow_get_by_ufid(const uint64_t ufid, struct hovs_flow_stats *stats);

int hovs_mega_flow_set_l3_forward(bool flag);

int hovs_flow_mgmt_get_block_table_size(uint32_t *block_num);

int hovs_flow_mgmt_update(const struct hovs_dpif_flow *modify, const struct nlattr *args, size_t args_len);

void hovs_flow_mgmt_set_block_version(uint32_t block_num, uint16_t block_id[], uint16_t block_version[], int result[]);

int hovs_flow_mgmt_get_block_version(uint32_t block_num, uint16_t block_id[], uint16_t block_version[]);

int hovs_qos_flow_limit_set(uint16_t qos_id, uint16_t type, uint64_t max_rate, uint64_t max_burst,
    uint64_t min_rate, uint64_t min_burst);

int hovs_qos_flow_limit_get(uint16_t qos_id, uint16_t type, uint64_t *max_rate, uint64_t *max_burst,
    uint64_t *min_rate, uint64_t *min_burst);

int hovs_hotplug_add(uint16_t port_id);

int hovs_hotplug_del(uint16_t port_id);    

//comnet
int hovs_acl_indir_counter_alloc(uint8_t *indir_id);
int hovs_acl_indir_counter_get(uint8_t indir_id, struct hovs_acl_indir_stats *indir_stats);
int hovs_acl_indir_counter_free(uint8_t indir_id);
int hovs_acl_indir_counter_reset(uint8_t indir_id);
int hovs_acl_mgmt_put(const struct hovs_dpif_acl *put);
int hovs_acl_mgmt_del(uint16_t group_id, uint16_t index);
int hovs_acl_get_stats(uint16_t group_id, uint16_t index, struct hovs_acl_stats *acl_stats);
int hovs_acl_mgmt_flush(uint16_t group_id);
int hovs_acl_mgmt_dump_start(uint16_t group_id, void **state);
int hovs_acl_mgmt_dump_next(void *state, struct hovs_dpif_acl_for_get *get);
int hovs_acl_mgmt_dump_done(void *state);
int hovs_dp_hash_mgmt_put(const struct hovs_dpif_flow *put, const struct nlattr *args, size_t args_len);
int hovs_dp_hash_mgmt_del_by_key(const struct nlattr *key, size_t key_len);
int hovs_dp_hash_mgmt_get_by_key(const struct nlattr *key, size_t key_len,
    struct hovs_dpif_flow_for_get *get);
int hovs_dp_hash_mgmt_dump_start(void **state);
int hovs_dp_hash_mgmt_dump_next(void *state, struct hovs_dpif_flow_for_get *get);
int hovs_dp_hash_mgmt_dump_done(void *state);
int hovs_dp_hash_mgmt_flush(void);

#ifdef __cplusplus
}
#endif

#endif
