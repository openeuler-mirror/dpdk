/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */


#ifndef HINIC3_MESSAGE_H
#define HINIC3_MESSAGE_H

#include "rte_pci.h"
#include "hinic3_packets_types.h"
#include "hinic3_init.h"
#include "hinic3_message_sub.h"

#ifdef  __cplusplus
extern "C" {
#endif

#define HINIC3_ETP_DISABLE (0)
#define HINIC3_ETP_ENABLE (1)
#define HINIC3_ETP_RESET (2)
#define HINIC3_TIME_STR_LEN (64)

#define HINIC3_ETP_ID_SIZE   (16)

enum hinic3_key_format {
    HINIC3_KEY_FMT_VNI_5TUPLE,
    HINIC3_KEY_FMT_VLAN_VXLAN_HASH_5TUPLE,
    HINIC3_KEY_FMT_PORT_VLAN_VXLAN_5TUPLE,
    HINIC3_KEY_FMT_MASKED_7TUPLE
};

enum hinic3_global_cfg_time_sync_type {
    HINIC3_PCAP_ETP_CFG_TIME_SYNC,
    HINIC3_GLOBAL_CFG_TIME_SYNC_MAX,
};

/* 当前仅在模糊流表场景下使用HINIC3_FLOW_KEY_INNER_TCI，精确流表使用HINIC3_FLOW_KEY_OUTER_VID或HINIC3_FLOW_KEY_OUTER_TCI */
enum hinic3_flow_key_type {
    HINIC3_FLOW_KEY_IN_PORT,
    HINIC3_FLOW_KEY_VNI,
    HINIC3_FLOW_KEY_OUTER_VID,
    HINIC3_FLOW_KEY_INNER_VID,
    HINIC3_FLOW_KEY_DL_TYPE,
    HINIC3_FLOW_KEY_SRC_IP,
    HINIC3_FLOW_KEY_DST_IP,
    HINIC3_FLOW_KEY_PROTOCOL,
    HINIC3_FLOW_KEY_SRC_PORT,
    HINIC3_FLOW_KEY_DST_PORT,
    HINIC3_FLOW_KEY_SRC_MAC,
    HINIC3_FLOW_KEY_DST_MAC,
    HINIC3_FLOW_KEY_SRC_IPV6,
    HINIC3_FLOW_KEY_DST_IPV6,
    HINIC3_FLOW_KEY_ETH_TYPE,
    HINIC3_FLOW_KEY_CONTROL_FLAG,
    HINIC3_FLOW_KEY_DP_HASH,
    HINIC3_FLOW_KEY_RECIRC_ID,
    HINIC3_FLOW_KEY_OUTER_TCI,
    HINIC3_FLOW_KEY_INNER_TCI,

    // 灰卡自定义key_type
    HINIC3_FLOW_KEY_DPDK_PORT_ID = 7 + 256,
    HINIC3_FLOW_KEY_DPDK_VXLAN = 17 + 256,

    HINIC3_FLOW_KEY_DEF_INVALID = 100 + 256,
    HINIC3_FLOW_KEY_DEF_IPV4_PROTOCOL,
    HINIC3_FLOW_KEY_DEF_IPV4_SIP,
    HINIC3_FLOW_KEY_DEF_IPV4_DIP,
    HINIC3_FLOW_KEY_DEF_IPV6_PROTOCOL,
    HINIC3_FLOW_KEY_DEF_IPV6_SIP,
    HINIC3_FLOW_KEY_DEF_IPV6_DIP,
    HINIC3_FLOW_KEY_DEF_TCP_SPORT,
    HINIC3_FLOW_KEY_DEF_TCP_DPORT,
    HINIC3_FLOW_KEY_DEF_UDP_SPORT,
    HINIC3_FLOW_KEY_DEF_UDP_DPORT,
    HINIC3_FLOW_KEY_DEF_ETH_TYPE,
    HINIC3_FLOW_KEY_DEF_ETH_SMAC,
    HINIC3_FLOW_KEY_DEF_ETH_DMAC,
    HINIC3_FLOW_KEY_DEF_VLAN_TCI,
    HINIC3_FLOW_KEY_DEF_ICMP_TYPE,
    HINIC3_FLOW_KEY_DEF_ICMP_CODE,
    HINIC3_FLOW_KEY_DEF_ICMP_IDENT,
    HINIC3_FLOW_KEY_DEF_ICMP6_TYPE,
    HINIC3_FLOW_KEY_DEF_ICMP6_CODE,

    HINIC3_FLOW_KEY_TYPE_MAX,
};

enum hinic3_ct_tcp_state_type {
    HINIC3_TCP_STATE_TYPE_CLOSED,
    HINIC3_TCP_STATE_TYPE_LISTEN,
    HINIC3_TCP_STATE_TYPE_SYN_SENT,
    HINIC3_TCP_STATE_TYPE_SYN_RECV,
    HINIC3_TCP_STATE_TYPE_ESTABLISHED,
    HINIC3_TCP_STATE_TYPE_CLOSE_WAIT,
    HINIC3_TCP_STATE_TYPE_FIN_WAIT_1,
    HINIC3_TCP_STATE_TYPE_CLOSING,
    HINIC3_TCP_STATE_TYPE_LAST_ACK,
    HINIC3_TCP_STATE_TYPE_FIN_WAIT_2,
    HINIC3_TCP_STATE_TYPE_TIME_WAIT
};

/* discard struct, use hinic3_flow_act_vxlan_gpe_header instead */
struct hinic3_flow_act_vxlan_header {
    uint8_t dmac[HINIC3_ETH_ADDR_LEN];    /* Destination MAC address */
    uint8_t smac[HINIC3_ETH_ADDR_LEN];    /* Source MAC address */
    uint32_t sip;        /* Source IP address, network order */
    uint32_t dip;        /* Destination IP address, network order */
    uint16_t sport;     /* Source port, network order */
    uint16_t vid;        /* Virtual lan identifier, network order */
    uint32_t vni;       /* Virtual network identifier, network order */
};

struct hinic3_flow_act_vxlan_gpe_header {
    uint8_t dmac[ETH_ALEN];     /**< Destination MAC address */
    uint8_t smac[ETH_ALEN];     /**< Source MAC address */

    uint16_t rsvd;              /**< rsvd */
    uint16_t vlan_id;           /**< Virtual lan identifier, network order */

    uint8_t ip_version;         /**< ip_version: 0-ipv4, 1-ipv6 */
    uint8_t dscp;
    uint16_t rsvd0;             /**< rsvd */

    uint32_t sip[4];            /**< Source IP address, network order */
    uint32_t dip[4];            /**< Destination IP address, network order */

    uint16_t sport;             /**< Source port, network order */
    uint16_t dport;             /**< Destination port, network order */

    struct {
        uint32_t flag   : 8;    /**< vxlan header flags */
        uint32_t rsvd1  : 24;   /**< vxlan header rsvd, network order */
        uint32_t vni    : 24;   /**< Virtual network identifier, network order */
        uint32_t rsvd2  : 8;    /**< vxlan header rsvd */
    } vxlan;

    uint32_t rsvd3[8];          /**< rsvd 32B */
};

struct hinic3_flow_act_block_version {
    uint16_t block_id;
    uint16_t block_version;
};

enum hinic3_flow_action_type {
    HINIC3_FLOW_ACT_CT,
    HINIC3_FLOW_ACT_VXL_PUSH,
    HINIC3_FLOW_ACT_VXL_POP,
    HINIC3_FLOW_ACT_VLAN_PUSH,
    HINIC3_FLOW_ACT_VLAN_POP,
    HINIC3_FLOW_ACT_SET_SMAC,
    HINIC3_FLOW_ACT_SET_DMAC,
    HINIC3_FLOW_ACT_SET_SIP,
    HINIC3_FLOW_ACT_SET_DIP,
    HINIC3_FLOW_ACT_SET_SPORT,
    HINIC3_FLOW_ACT_SET_DPORT,
    HINIC3_FLOW_ACT_SET_PRIORITY,
    HINIC3_FLOW_ACT_OUTPUT,
    HINIC3_FLOW_ACT_VXL_GPE_PUSH,
    HINIC3_FLOW_ACT_SET_SIPV6,
    HINIC3_FLOW_ACT_SET_DIPV6,
    HINIC3_FLOW_ACT_SET_VXLAN_GROUP_ID,
    HINIC3_FLOW_ACT_ECMP,
    HINIC3_FLOW_ACT_DROP,
    HINIC3_FLOW_ACT_MIRROR,
    HINIC3_FLOW_ACT_CRYPTO,
    HINIC3_FLOW_ACT_COUNT,
    HINIC3_FLOW_ACT_DEC_TTL,
    HINIC3_FLOW_ACT_UPCALL,
    HINIC3_FLOW_ACT_DP_HASH,
    HINIC3_FLOW_ACT_RECIRC_ID,
    HINIC3_FLOW_ACT_BLOCK_VERSION,
    HINIC3_FLOW_ACT_QOS,
    HINIC3_FLOW_ACT_AGE,

    // 灰卡
    /**
     *      * Example:
     *      * call function: nl_msg_put_flag
     *      * parameters: actions, HINIC3_FLOW_ACT_DPDK_COUNT, 1
     *      */
    HINIC3_FLOW_ACT_DPDK_COUNT = 8 + 0x100,

    /**
     *      * Example:
     *      * call function: nl_msg_put_u32
     *      * parameters: actions, HINIC3_FLOW_ACT_DPDK_PORT_ID, 1
     *      */
    HINIC3_FLOW_ACT_DPDK_PORT_ID = 13 + 0x100,

    /**
     *      * Example:
     *      * call function: nl_msg_put_flag
     *      * parameters: actions, HINIC3_FLOW_ACT_DPDK_VLAN_POP, 1
     *      */
    HINIC3_FLOW_ACT_DPDK_VLAN_POP = 22 + 0x100,

    /**
     *      * Example:
     *      * call function: nl_msg_put_u16
     *      * parameters: actions, HINIC3_FLOW_ACT_DPDK_VLAN_PUSH, htonl(0xc0a8)
     *      */
    HINIC3_FLOW_ACT_DPDK_VLAN_PUSH = 23 + 0x100,

    /**
     *      * Example:
     *      * call function: nl_msg_put_u8
     *      * parameters: actions, HINIC3_FLOW_ACT_DPDK_SET_VLAN_VID, htonl(0xc1)
     *      */
    HINIC3_FLOW_ACT_DPDK_SET_VLAN_VID = 24 + 0x100,

    /**
     *      * Example:
     *      * call function: nl_msg_put_u16
     *      * parameters: actions, HINIC3_FLOW_ACT_DPDK_SET_VLAN_PCP, htonl(0xc0a8)
     *      */
    HINIC3_FLOW_ACT_DPDK_SET_VLAN_PCP = 25 + 0x100,

    /**
     *      * Example:
     *      * call function: nl_msg_put_unspec
     *      * parameters: actions, HINIC3_FLOW_ACT_DPDK_VXL_PUSH, &vxlan, sizeof vxlan
     *      */
    HINIC3_FLOW_ACT_DPDK_VXL_PUSH = 28 + 0x100,

    /**
     *      * Example:
     *      * call function: nl_msg_put_flag
     *      * parameters: actions, HINIC3_FLOW_ACT_DPDK_VXL_POP, 1
     *      */
    HINIC3_FLOW_ACT_DPDK_VXL_POP = 29 + 0x100,

    /**
     *      * Example:
     *      * call function: nl_msg_put_u32
     *      * parameters: actions, HINIC3_FLOW_ACT_DPDK_SET_IPV4_SRC, htonl(0xc0a80101)
     *      */
    HINIC3_FLOW_ACT_DPDK_SET_IPV4_SRC = 34 + 0x100,

    /**
     *      * Example:
     *      * call function: nl_msg_put_u32
     *      * parameters: actions, HINIC3_FLOW_ACT_DPDK_SET_IPV4_DST, htonl(0xc0a80101)
     *      */
    HINIC3_FLOW_ACT_DPDK_SET_IPV4_DST = 35 + 0x100,

    /**
     *      * Example:
     *      * call function: nl_msg_put_unspec
     *      * parameters: actions, HINIC3_FLOW_ACT_DPDK_SET_IPV6_SRC, &in6_addr, sizeof in6_addr
     *      */
    HINIC3_FLOW_ACT_DPDK_SET_IPV6_SRC = 36 + 0x100,

    /**
     *      * Example:
     *      * call function: nl_msg_put_unspec
     *      * parameters: actions, HINIC3_FLOW_ACT_DPDK_SET_IPV6_DST, &in6_addr, sizeof in6_addr
     *      */
    HINIC3_FLOW_ACT_DPDK_SET_IPV6_DST = 37 + 0x100,

    /**
     *      * Example:
     *      * call function: nl_msg_put_u16
     *      * parameters: actions, HINIC3_FLOW_ACT_DPDK_SET_TP_SRC, htons(0x5678)
     *      */
    HINIC3_FLOW_ACT_DPDK_SET_TP_SRC = 38 + 0x100,

    /**
     *      * Example:
     *      * call function: nl_msg_put_u16
     *      * parameters: actions, HINIC3_FLOW_ACT_DPDK_SET_TP_DST, htons(0x5678)
     *      */
    HINIC3_FLOW_ACT_DPDK_SET_TP_DST = 39 + 0x100,

    HINIC3_FLOW_ACT_DPDK_DEC_TTL = 41 + 0x100,

    /**
     *      * Example:
     *      * call function: nl_msg_put_unspec
     *      * parameters: actions, HINIC3_FLOW_ACT_DPDK_SET_MAC_SRC, addr, sizeof(addr)
     *      */
    HINIC3_FLOW_ACT_DPDK_SET_MAC_SRC = 43 + 0x100,

    /**
    *      * Example:
    *      * call function: nl_msg_put_unspec
    *      * parameters: actions, HINIC3_FLOW_ACT_DPDK_SET_MAC_DST, addr, sizeof(addr)
    *      */
    HINIC3_FLOW_ACT_DPDK_SET_MAC_DST = 44 + 0x100,

    /**
    *      * Example:
    *      * call function: nl_msg_put_u16
    *      * parameters: actions, HINIC3_FLOW_ACT_DPDK_MIRROR, 0x1234
    *      */
    HINIC3_FLOW_ACT_DPDK_MIRROR = 54 + 0x100,

    HINIC3_FLOW_ACT_DEF_INVALID = 100 + 0x100,

    /**
    *      * Example:
    *      * call function: nl_msg_put_unspec
    *      * parameters: actions, HINIC3_FLOW_ACT_DEF_CT, don't care about the content
    *      */
    HINIC3_FLOW_ACT_DEF_CT,

    /**
    *      * Example:
    *      * call function: nl_msg_put_unspec
    *      * parameters: actions, HINIC3_FLOW_ACT_DEF_USER_COUNT, don't care about the content
    *      */
    HINIC3_FLOW_ACT_DEF_USER_COUNT,

    /**
    *      * Example:
    *      * call function: nl_msg_put_flag
    *      * parameters: actions, HINIC3_FLOW_ACT_DEF_UPCALL, 1
    *      */
    HINIC3_FLOW_ACT_DEF_UPCALL,

    /**
    *      * Example:
    *      * call function: nl_msg_put_u32
    *      * parameters: actions, HINIC3_FLOW_ACT_DEF_SET_MBUF, 0x12345678
    *      */
    HINIC3_FLOW_ACT_DEF_SET_MBUF,

    /**
    *      * Example:
    *      * call function: nl_msg_put_u32
    *      * parameters: actions, HINIC3_FLOW_ACT_DEF_SET_REG_0, 0x12345678
    *      */
    HINIC3_FLOW_ACT_DEF_SET_REG_0,

    /**
    *      * Example:
    *      * call function: nl_msg_put_u32
    *      * parameters: actions, HINIC3_FLOW_ACT_DEF_SET_REG_1, 0x12345678
    *      */
    HINIC3_FLOW_ACT_DEF_SET_REG_1,

    /**
    *      * Example:
    *      * call function: nl_msg_put_u32
    *      * parameters: actions, HINIC3_FLOW_ACT_DEF_COLOR, 0x12345678
    *      */
    HINIC3_FLOW_ACT_DEF_COLOR,
    
    HINIC3_FLOW_ACT_TYPE_MAX,
};

enum FLOW_PUT_RESULT {
    SUCCES = 0,
    FAIL,
    TIMEOUT,
};

enum hinic3_pkt_user_data_mac_type {
    HINIC3_USER_DATA_MAC_UNICAST,
    HINIC3_USER_DATA_MAC_BROADCAST,
    HINIC3_USER_DATA_MAC_MULTICAST
};

enum hinic3_pkt_user_data_l3_type {
    HINIC3_USER_DATA_L3_UNKNOWN,
    HINIC3_USER_DATA_L3_IPV4,
    HINIC3_USER_DATA_L3_IPV6,
    HINIC3_USER_DATA_L3_ARP
};

enum hinic3_pkt_user_data_l4_type {
    HINIC3_USER_DATA_L4_UNKNOWN,
    HINIC3_USER_DATA_L4_UDP = 4,
    HINIC3_USER_DATA_L4_TCP
};

enum hinic3_pkt_user_data_traffic_type {
    HINIC3_TRAFFIC_FROM_HOST_DEFAULT,   /* Traffic from host to NIC.  NIC should process it with its normal pipeline */
    HINIC3_TRAFFIC_FROM_HOST_FALLBACK, /* Traffic from host to NIC. NIC should  send packet to destination port */
    HINIC3_TRAFFIC_FROM_NIC_DEFAULT,     /* Traffic from NIC to host. Means NIC can not offload the packet type */
    HINIC3_TRAFFIC_FROM_NIC_MISS_UPCALL, /* Traffic from NIC to host. NIC forward cache missing, shoud be offload */
    HINIC3_TRAFFIC_COMMAND,             /* Command traffic */
    HINIC3_TRAFFIC_FROM_HOST_PACKET_OUT /* Traffic from host to NIC. For etp_packet_out_cmd_parse */
};

enum migrate_vtep_state {
    MIGRATE_JUMP_PRECONFIG_STATE = 0,
    MIGRATE_JUMP_ACTIVE_STATE = 1,
    MIGRATE_JUMP_FLIP_STATE = 2,
    MIGRATE_JUMP_CLEAR_STATE = 3,
};

enum migrate_tap_dir {
    MIGRATE_JUMP_TAP_SRC = 0, /* src port flag */
    MIGRATE_JUMP_TAP_DST = 1, /* dst port flag */
};

#define TAP_NAME_LEN 32
struct migrate_tunnel_info {
    uint8_t src_mac[HINIC3_ETH_ADDR_LEN];
    uint8_t dst_mac[HINIC3_ETH_ADDR_LEN];
    hinic3_be32 ip_src;
    hinic3_be32 ip_dst;
    hinic3_be16 src_port; /* udp sport id */
    hinic3_be32 vx_vni;
    hinic3_be16 vport_id; /* bond vport id */
    char tap_name[TAP_NAME_LEN];
    hinic3_be32 global_src_vni;
    hinic3_be32 global_dst_vni;
};

struct migrate_state_dir {
    enum migrate_vtep_state vtep_state;
    enum migrate_tap_dir dir;
};

struct hinic3_pcap_probe_ctl_t {
    uint8_t rule_idx;
    uint8_t enable            : 4;
    uint8_t first_last_rule   : 4;
    uint16_t hinic3_port_id;
};

struct hinic3_pcap_probe_cfg_mask_t {
    uint32_t all_pass          : 1;
    uint32_t vxlan_inner       : 1;
    uint32_t host              : 1;
    uint32_t direct_rx         : 1;
    uint32_t direct_tx         : 1;
    uint32_t proto_en          : 1;
    uint32_t cnt_en            : 1;
    uint32_t vni_en            : 1;
    uint32_t dport_en          : 1;
    uint32_t sport_en          : 1;
    uint32_t vlan_en           : 1;
    uint32_t eth_type_en       : 1;
    uint32_t dmac_en           : 1;
    uint32_t smac_en           : 1;
    uint32_t dip_en            : 1;
    uint32_t sip_en            : 1;
    uint32_t dscp_en           : 1;
    uint32_t ipv6_en           : 1; /* 0: ipv4/not care, 1: ipv6 */
    uint32_t resv              : 2;
    uint32_t hinic3_private_pfid : 4;
    uint32_t hinic3_private_qid : 8;
};

struct hinic3_pcap_probe_rule_cfg_info {
    uint32_t vport_id       : 16;
    uint32_t cap_id         : 8;
    uint32_t dscp           : 8;
    uint32_t sip;
    uint32_t dip;
    uint32_t host_ip;
    uint8_t smac[ETH_ADDR_LEN];
    uint8_t dmac[ETH_ADDR_LEN];
    uint32_t vlan_id        : 16;
    uint32_t eth_type       : 16;
    uint32_t vni            : 24;
    uint32_t ip_proto       : 8;
    uint32_t dport          : 16;
    uint32_t sport          : 16;
    uint32_t sip_mask;
    uint32_t dip_mask;
    uint32_t host_mask;
    uint32_t pcap_cfg_cnt_h;
    uint32_t pcap_cfg_cnt_l;
};

struct hinic3_pcap_probe_rule_ipv6_cfg_info {
    struct in6_addr sip6;
    struct in6_addr dip6;
    struct in6_addr hip6;
};

struct hinic3_pcap_probe_filter_t {
    struct hinic3_pcap_probe_ctl_t  pcap_ctl;
    struct hinic3_pcap_probe_cfg_mask_t  cap_mask;
    struct hinic3_pcap_probe_rule_cfg_info rule_cfg;
    struct hinic3_pcap_probe_rule_ipv6_cfg_info rule_ipv6_cfg;
};

#ifdef  __cplusplus
}
#endif

#endif /* HINIC3_MESSAGE_H */
