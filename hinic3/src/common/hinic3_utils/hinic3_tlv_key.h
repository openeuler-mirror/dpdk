/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_TLV_KEY_H
#define HINIC3_TLV_KEY_H                             1

#ifdef __cplusplus
extern "C" {
#endif

#define HINIC3_PORT_VLAN_OL                          "vlan_ol"
#define HINIC3_PORT_VLAN_TAG                         "vlan"
#define HINIC3_PORT_VLAN_MODE                        "vlan_mode"
#define HINIC3_PORT_VNI                              "vni"
#define HINIC3_DPDK_PORT_ID                          "dpdk_port_id"
#define HINIC3_PORT_MAX_QUEUE_NUM                    "max_queue_num" /* for virtio-vf flavor mgmt */
#define HINIC3_PORT_PCI_ADDR                         "pci_addr" /* for virtio-vf mgmt add dynamic */
#define HINIC3_PORT_BLOCK_START                      "block_start" /* for virtio-vf mgmt add dynamic */
#define HINIC3_PORT_BLOCK_SIZE                       "block_size" /* for virtio-vf mgmt add dynamic */
#define HINIC3_PORT_UPCALL_QUEUE_NUM                 "upcall_queue_num" /* for vport upcall queue */
#define HINIC3_PORT_UPCALL_REUSE                     "share_upcall" /* for vport share upcall flag */
#define HINIC3_PORT_VM_ID                            "vm_id" /* for virtio-vf QoS */
#define HINIC3_PORT_BUCKET_ID                        "bucket_id" /* for virtio-vf QoS */
#define HINIC3_PORT_BW_INGRESS                       "bw_ingress" /* for virtio-vf VM-QoS (unused) */
#define HINIC3_PORT_VM_BW_INGRESS                    "vm_bw_ingress" /* for virtio-vf VM-QoS */
#define HINIC3_PORT_VM_PPS_INGRESS                   "vm_pps_ingress" /* for virtio-vf VM-QoS */
#define HINIC3_PORT_VM_BW_EGRESS                     "vm_bw_egress" /* for virtio-vf VM-QoS */
#define HINIC3_PORT_VM_PPS_EGRESS                    "vm_pps_egress" /* for virtio-vf VM-QoS */
#define HINIC3_PORT_DROP_THRESH_LEVEL                "drop_thresh_level" /* for virtio-vf QoS */
#define HINIC3_PORT_QUEUE_MAP                        "vport_queue_map" /* for virtio-vf queue map */
#define HINIC3_PORT_UPCALL_QUEUE_MAP                 "vport_upcall_queue_map" /* for upcall queue map */
#define HINIC3_PORT_GRO_ENABLE                       "gro_enable" /* for virtio-vf gro */
#define HINIC3_PORT_IPV6_GRO_ENABLE                  "ipv6_gro_enable" /* for virtio-vf ipv6 gro */
#define HINIC3_PORT_MIGRATE_TUNNEL_INFO              "migrate_tunnel_info"
#define HINIC3_PORT_MIGRATE_STATE                    "migrate_state"
#define HINIC3_PORT_EP_PFID                          "ep_pfid" /* for zero-virtio-pt init */
#define HINIC3_PORT_EP_VFID                          "ep_vfid" /* for zero-virtio-pt init */
#define HINIC3_PORT_QUEUE_SIZE                       "queue_size" /* for zero-virtio-pt init */
#define HINIC3_PORT_HIGH_RATE                        "high_rate" /* for virtio-vf high_rate */
#define HINIC3_PORT_FDIR_POLICY                      "fdir_policy" /* for hinic3 fdir policy options */
#define HINIC3_PORT_XMIT_HASH                        "xmit_hash" /* for hinic3 port xmit_hash options */
#define HINIC3_PORT_VIRTIO_QUEUE_DEPTH               "virtio_queue_depth" /* set queue_depth */
#define HINIC3_PORT_FUNCTION_ID                      "function_id" /* for dynamic add port by function id */
#define HINIC3_PORT_NO_DRIVER_CHECK                  "port_no_driver_check"

#define HINIC3_BOND_NAME                             "bond_name"
#define HINIC3_PORT_PCI_ID                           "pci" /* format same as below */
#define HINIC3_BOND_UPLINK_PCI_ID                    "bond_uplink_pci_id" /* format: HH:HH.D, H means hex, D means dec */
#define HINIC3_BOND_MODE                             "mode"
#define HINIC3_BOND_XMIT_HASH_POLICY                 "policy"
#define HINIC3_BOND_SLAVES                           "slaves"
#define HINIC3_BOND_ACTIVE_SLAVE                     "active_slave"
#define HINIC3_BOND_LACP_RATE                        "lacp_rate"
#define HINIC3_BOND_LACP_DEACTIVE_SLAVES             "lacp_deactive_slaves"
#define HINIC3_PRIORITY_UPCALL                       "priority_upcall"
#define HINIC3_PORT_MAC                              "mac_addr" /* format:fa:16:3e:01:e7:42 */
#define HINIC3_MAX_QUEUE_NUM                         "max_queues"
#define HINIC3_REPRESENTOR_ID                        "representor"
#define HINIC3_SHARE_UPCALL                          "share-upcall-queues"
#define HINIC3_VF_MAC_ADDR_DEFAULT                   "00:00:00:00:00:00"

/* attribute for json_slaves_arrays---for get only */
#define HINIC3_BOND_ARG_UPDELAY_STR                  "updelay"
#define HINIC3_BOND_ARG_DOWNDELAY_STR                "downdelay"
#define HINIC3_BOND_ARG_LACP_STATUS_STR              "lacp_status"
#define HINIC3_BOND_ARG_ACTIVE_MAC_STR               "active_mac"
#define HINIC3_BOND_ARG_SLAVE_NAME_STR               "name"
#define HINIC3_BOND_ARG_SLAVE_PCI_STR                "pci"
#define HINIC3_BOND_ARG_SLAVE_STATUS_STR             "status"
#define HINIC3_BOND_ARG_SLAVE_SPEED_STR              "speed"
#define HINIC3_BOND_ARG_SLAVE_DUPLEX_STR             "duplex"
#define HINIC3_BOND_ARG_SLAVE_MAC_ADDRESS_STR        "mac_address"
#define HINIC3_BOND_ARG_SLAVE_MTU_STR                "mtu"
#define HINIC3_BOND_ARG_SLAVE_STATISTICS_STR         "statistics"
#define HINIC3_BOND_ARG_SLAVE_LACP_INFO_STR          "lacp_info"

/* attribute for slave_statistics---for get only */
#define STATISTICS_RX_PKTS_STR                      "rx_pkts"
#define STATISTICS_RX_BYTES_STR                     "rx_bytes"
#define STATISTICS_RX_DROPPED_STR                   "rx_dropped"
#define STATISTICS_RX_ERRORS_STR                    "rx_errors"
#define STATISTICS_TX_PKTS_STR                      "tx_pkts"
#define STATISTICS_TX_BYTES_STR                     "tx_bytes"
#define STATISTICS_TX_DROPPED_STR                   "tx_dropped"
#define STATISTICS_TX_ERRORS_STR                    "tx_errors"
#define SLAVE_STATISTICS_RX_PDUS_STR                "rx_pdus"
#define SLAVE_STATISTICS_TX_PDUS_STR                "tx_pdus"
#define STATISTICS_RX_8023AD_DROPPED_STR            "rx_8023ad_dropped"
#define STATISTICS_TX_8023AD_DROPPED_STR            "tx_8023ad_dropped"
#define STATISTICS_UNKNOWN_PKT_8023AD_DROPPED_STR   "unknown_drop_pkts"

/* attribute for slave_lacp_info---for get only */
#define LACP_INFO_AGG_PORD_ID_STR                   "agg_port_id"
#define LACP_INFO_SELECT_STR                        "select"
#define LACP_INFO_LACP_TIME_STR                     "lacp_time"

#define LACP_INFO_ACTOR_STR                         "actor"
#define LACP_INFO_PARTNER_STR                       "partner"
#define LACP_INFO_SYS_ID_STR                        "sys_id"
#define LACP_INFO_PORT_PRIORITY_STR                 "port_priority"
#define LACP_INFO_SYS_PRIORITY_STR                  "sys_priority"
#define LACP_INFO_PORT_NUM_STR                      "port_num"
#define LACP_INFO_KEY_STR                           "key"
#define LACP_INFO_STATE_STR                         "state"

/* define for smap or json as str-key */
#define HINIC3_GLOBAL_CFG_ARG_VLAN_ETHTYPE_STR       "vlan_ethtype"
#define HINIC3_GLOBAL_CFG_ARG_RSS_VF_NUM_STR         "rss_vf_num"
#define HINIC3_GLOBAL_CFG_ARG_VXLAN_LOCAL_IP_STR     "local_ip"
#define HINIC3_GLOBAL_CFG_ARG_PCI_VENDOR_ID_STR      "pci_vendor_id"
#define HINIC3_GLOBAL_CFG_ARG_PCI_DEVICE_ID_STR      "pci_device_id"
#define HINIC3_GLOBAL_CFG_ARG_PCI_SUB_VENDOR_ID_STR  "pci_sub_vendor_id"
#define HINIC3_GLOBAL_CFG_ARG_PCI_SUB_DEVICE_ID_STR  "pci_sub_device_id"
#define HINIC3_GLOBAL_CFG_ARG_INVALID_TCP_ACTION_STR "invalid_tcp_action"
#define HINIC3_GLOBAL_CFG_ARG_IP_FRAG_ACTION_STR     "ip_frag_action"
#define HINIC3_GLOBAL_CFG_ARG_THREAD_MODE_STR        "thread_mode"
#define HINIC3_GLOBAL_CFG_ARG_FLOW_AGE_TIME_STR      "flow_age_time"
#define HINIC3_GLOBAL_CFG_ARG_PTHREAD_FDS_STR        "pthread_fds"
#define GLOBAL_VLAN_ETHTYPE_8021Q_STR               "8021Q"
#define GLOBAL_CFG_ACTION_DROP_STR                  "drop"
#define GLOBAL_CFG_ACTION_UPCALL_STR                "upcall"
#define GLOBAL_CFG_THREAD_MODE_ROUND_ROBIN_STR      "round-robin"
#define GLOBAL_CFG_THREAD_MODE_INTERRUPT_STR        "interrupt"
#define HINIC3_GLOBAL_CFG_ARG_VF_INFO_STR            "vf_info"
#define HINIC3_GLOBAL_CFG_ARG_QUEUE_POOL_SIZE_STR    "queue_pool_size"
#define HINIC3_GLOBAL_CFG_ARG_PHY_DEV_INFO_STR       "phy_dev_info"
#define HINIC3_GLOBAL_CFG_ARG_VF_ENABLE_STR          "vf_enable" /* enable sriov function for offload nic */
#define HINIC3_GLOBAL_CFG_ARG_PMD_MODE_STR           "pmd_mode" /* star:1/stop:0 */
#define HINIC3_GLOBAL_CFG_ARG_PMD_MODE_START_STR     "start"
#define HINIC3_GLOBAL_CFG_ARG_PMD_MODE_STOP_STR      "stop"
#define HINIC3_GLOBAL_CFG_ARG_PCAP_PROBE_STR         "pcap_probe_cfg"
#define HINIC3_GLOBAL_GET_PCAP_PROBE_STATS_STR       "pcap_probe_stats"
#define HINIC3_GLOBAL_CFG_ARG_ETP_STR                "etp_cfg"
#define HINIC3_GLOBAL_GET_ETP_STATS_STR              "etp_stats"
#define HINIC3_GLOBAL_CFG_UPCALL_PUSH_VXLAN_STR      "upcall_push_vxlan"
#define HINIC3_GLOBAL_CFG_UPCALL_PUSH_VXLAN_ON_STR   "on"
#define HINIC3_GLOBAL_CFG_UPCALL_PUSH_VXLAN_OFF_STR  "off"
#define HINIC3_GLOBAL_CFG_UNAGE_FLAG_STR             "unage_flag"
#define HINIC3_GLOBAL_CFG_PKT_FORWARD_MOD_STR        "packet_forward_mode"
#define HINIC3_GLOBAL_CFG_PKT_LOW_LATENCY_MOD_STR    "low_latency"
#define HINIC3_GLOBAL_CFG_PKT_HIGH_THROUGH_STR       "high_throughput"

/* bum attribute key-define for smap or json as str-key */
#define HINIC3_BUM_ARG_SRC_MAC_STR                   "src-mac:"
#define HINIC3_BUM_ARG_BRD_RATELIMIT_STR             "brd-ratelimit:"
#define HINIC3_BUM_ARG_ETHER_TYPE_CHECK_STR          "ether-type-check:"
#define HINIC3_BUM_ARG_EXTRA_ETH_TYPE_STR            "extra-eth-type:"

#define HINIC3_QOS_QUEUE_ARG_MAX_RATE_STR            "max-rate"
#define HINIC3_QOS_QUEUE_ARG_MIN_RATE_STR            "min-rate"
#define HINIC3_QOS_QUEUE_ARG_BURST_STR               "burst"
#define HINIC3_QOS_QUEUE_ARG_WEIGHT_STR              "weight"
#define HINIC3_QOS_QUEUE_ARG_PRIORITY_STR            "priority"

#ifdef __cplusplus
}
#endif
#endif /* HINIC3_TLV_KEY_H */
