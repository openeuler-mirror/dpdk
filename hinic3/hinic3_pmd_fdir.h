/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2019 Huawei Technologies Co., Ltd
 */

#ifndef _HINIC3_PMD_FDIR_H_
#define _HINIC3_PMD_FDIR_H_

#include "base/hinic3_compat.h"
#include "base/hinic3_pmd_nic_cfg.h"

#define HINIC3_FLOW_MAX_PATTERN_NUM   16

#define HINIC3_TCAM_DYNAMIC_BLOCK_SIZE  16

#define HINIC3_TCAM_DYNAMIC_MAX_FILTERS 1024

#define HINIC3_PKT_TCAM_DYNAMIC_INDEX_START(block_index)  \
	(HINIC3_TCAM_DYNAMIC_BLOCK_SIZE * (block_index))

#define HINIC3_TCAM_GET_DYNAMIC_BLOCK_INDEX(index) \
		((index) / HINIC3_TCAM_DYNAMIC_BLOCK_SIZE)

#define HINIC3_TCAM_GET_INDEX_IN_BLOCK(index) \
		((index) % HINIC3_TCAM_DYNAMIC_BLOCK_SIZE)

#define HINIC3_TCAM_INVALID_INDEX 0xFFFF

#ifdef HINIC3_TRAFFIC_BIFUR
#define HINIC3_RSS_QUEUE_BUF 128
#endif

enum hinic3_ether_type {
	HINIC3_PKT_TYPE_ARP = 1,
	HINIC3_PKT_TYPE_ARP_REQ,
	HINIC3_PKT_TYPE_ARP_REP,
	HINIC3_PKT_TYPE_RARP,
	HINIC3_PKT_TYPE_LACP,
	HINIC3_PKT_TYPE_LLDP,
	HINIC3_PKT_TYPE_OAM,
	HINIC3_PKT_TYPE_CDCP,
	HINIC3_PKT_TYPE_CNM,
	HINIC3_PKT_TYPE_ECP = 10,
	HINIC3_PKT_TYPE_BUTT,

	HINIC3_PKT_UNKNOWN = 31,
};

struct rte_flow {
	TAILQ_ENTRY(rte_flow) node;
	enum rte_filter_type filter_type;
	void *rule;
};

struct hinic3_fdir_rule_key {
	struct rte_eth_ipv4_flow ipv4;
	struct rte_eth_ipv6_flow ipv6;
	struct rte_eth_ipv4_flow inner_ipv4;
	struct rte_eth_ipv6_flow inner_ipv6;
	struct rte_eth_tunnel_flow tunnel;
	uint16_t src_port;
	uint16_t dst_port;
	uint8_t proto;
	uint8_t vlan_flag;
	uint16_t ether_type;
};

struct hinic3_fdir_filter {
	int tcam_index;
	uint8_t ip_type; /**< Inner ip type. */
	uint8_t outer_ip_type;
	uint8_t tunnel_type;
 	uint8_t action;
	uint16_t rss_group_id;
	struct hinic3_fdir_rule_key key_mask;
	struct hinic3_fdir_rule_key key_spec;
	uint32_t rq_index; /**< Queue assigned when matched. */
	bool is_hairpin;
	uint32_t queue_num;
};

struct hinic3_ethertype_filter {
	int tcam_index[HINIC3_PKT_TYPE_BUTT];
	uint16_t ether_type;	/**< Ether type to match */
	uint16_t flags;
	uint16_t queue;		/**< Queue assigned to when match*/
};

/* This structure is used to describe a basic filter type. */
struct hinic3_filter_t {
	u16  filter_rule_nums;
	enum rte_filter_type filter_type;
	struct hinic3_ethertype_filter ethertype_filter;
	struct hinic3_fdir_filter fdir_filter;
	struct hinic3_rss_template_entry *template_entry;

};

enum hinic3_action_type {
	HINIC3_ACTION_ADD,
	HINIC3_ACTION_NOT_ADD,
};

enum hinic3_fdir_tunnel_mode {
	HINIC3_FDIR_TUNNEL_MODE_NORMAL = 0,
	HINIC3_FDIR_TUNNEL_MODE_VXLAN  = 1,
	HINIC3_FDIR_TUNNEL_MODE_NVGRE  = 2,
	HINIC3_FDIR_TUNNEL_MODE_FC     = 3,
	HINIC3_FDIR_TUNNEL_MODE_GPE    = 4,
	HINIC3_FDIR_TUNNEL_MODE_GENEVE = 5,
	HINIC3_FDIR_TUNNEL_MODE_NSH    = 6,
	HINIC3_FDIR_TUNNEL_MODE_IPIP   = 7,
	HINIC3_FDIR_TUNNEL_MODE_MAX    = 8,
};

enum hinic3_fdir_ip_type {
	HINIC3_FDIR_IP_TYPE_IPV4 = 0,
	HINIC3_FDIR_IP_TYPE_IPV6 = 1,
	HINIC3_FDIR_IP_TYPE_ANY	 = 2,
};

struct hinic3_tcam_key_mem {
#if (RTE_BYTE_ORDER == RTE_BIG_ENDIAN)
	u32 rsvd0 : 16;
	u32 ip_proto : 8;
	u32 tunnel_type : 4;
#ifdef HINIC3_TRAFFIC_BIFUR
	u32 model : 1;
	u32 bifur_flag : 2;
	u32 rsvd1 : 1;
#else
	u32 rsvd1 : 4;
#endif

	u32 function_id : 15;
	u32 ip_type : 1;
	u32 sipv4_h : 16;

	u32 sipv4_l : 16;
	u32 dipv4_h : 16;

	u32 dipv4_l : 16;
	u32 vlan_flag : 1;
	u32 rsvd2 : 15;

	u32 rsvd3;

	u32 ether_type : 16;
	u32 dport : 16;

	u32 sport : 16;
	u32 rsvd5 : 16;
#ifdef HINIC3_TRAFFIC_BIFUR
	u32 rsvd6 : 12;
	u32 er_id : 4;
#else
	u32 rsvd6 : 16;
#endif
	u32 outer_sipv4_h : 16;

	u32 outer_sipv4_l : 16;
	u32 outer_dipv4_h : 16;

	u32 outer_dipv4_l : 16;
	u32 vni_h : 16;

	u32 vni_l : 16;
	u32 rsvd7 : 16;
#else
#ifdef HINIC3_TRAFFIC_BIFUR
	u32 rsvd1 : 1;
	u32 bifur_flag : 2;
	u32 model : 1;
#else
	u32 rsvd1 : 4;
#endif
	u32 tunnel_type : 4;
	u32 ip_proto : 8;
	u32 rsvd0 : 16;

	u32 sipv4_h : 16;
	u32 ip_type : 1;
	u32 function_id : 15;

	u32 dipv4_h : 16;
	u32 sipv4_l : 16;

	u32 rsvd2 : 15;
	u32 vlan_flag : 1;
	u32 dipv4_l : 16;

	u32 rsvd3;

	u32 dport : 16;
	u32 ether_type : 16;

	u32 rsvd5 : 16;
	u32 sport : 16;

	u32 outer_sipv4_h : 16;
#ifdef HINIC3_TRAFFIC_BIFUR
	u32 er_id : 4;
	u32 rsvd6 : 12;
#else
	u32 rsvd6 : 16;
#endif

	u32 outer_dipv4_h : 16;
	u32 outer_sipv4_l : 16;

	u32 vni_h : 16;
	u32 outer_dipv4_l : 16;

	u32 rsvd7 : 16;
	u32 vni_l : 16;
#endif
};

struct hinic3_tcam_key_mem_htn {
#if (RTE_BYTE_ORDER == RTE_BIG_ENDIAN)
	u32 rsvd0 : 16;
	u32 ip_proto : 8;
	u32 tunnel_type : 3;
	u32 function_id_h: 5;

	u32 function_id_l : 5;
	u32 ip_type : 2;
	u32 outer_ip_type : 1;
	u32 rsvd1 : 8;
	u32 outer_sipv4_h : 16;

	u32 outer_sipv4_l : 16;
	u32 outer_dipv4_h : 16;

	u32 outer_dipv4_l : 16;
	u32 rsvd2 : 8;
	u32 vni_h : 8;

	u32 vni_l : 16;
	u32 sipv4_h : 16;

	u32 sipv4_l : 16;
	u32 rsvd5 : 16;

	u32 rsvd6;
	u32 rsvd7;

	u32 rsvd8 : 16;
	u32 dipv4_h : 16;

	u32 dipv4_l : 16;
	u32 sport : 16;

	u32 dport : 16;
	u32 rsvd5 : 16;
#else
	u32 function_id_h : 5;
	u32 tunnel_type : 3;
	u32 ip_proto : 8;
	u32 rsvd0 : 16;

	u32 outer_sipv4_h : 16;
	u32 rsvd1 : 8;
	u32 outer_ip_type : 1;
	u32 ip_type : 2;
	u32 function_id_l : 5;

	u32 outer_dipv4_h : 16;
	u32 outer_sipv4_l : 16;

	u32 vni_h : 8;
	u32 rsvd2 : 8;
	u32 outer_dipv4_l : 16;

	u32 sipv4_h : 16;
	u32 vni_l : 16;

	u32 rsvd5 : 16;
	u32 sipv4_l : 16;

	u32 rsvd6;
	u32 rsvd7;

	u32 dipv4_h : 16;
	u32 rsvd8 : 16;

	u32 sport : 16;
	u32 dipv4_l :16;

	u32 rsvd9 : 16;
	u32 dport : 16;
#endif
};

/*
 * This structure is used in normal IPv6 or IPv6 tunnel scenarios.
 *
 * @ip_proto
 *   indicates the normal IPv6 nextHdr or inner IPv4/IPv6 next proto.
 *
 * @outer_ip_type
 *   only used in IPv6 tunnel scenarios and indicates the outer ip version.
 *
 * @ip_type
 *   indicates the normal IPv6 version or the inner IP of the IPv6 tunnel.
 *
 * @sipv6_key
 * @sipv6_key
 *   normal IPv6 address or outer IPv6 address of the IPv6 tunnel.
 *
 * @dport
 * @sport
 *   NOTE: different from ipv6 addr key above, it indicates TCP/UDP dst and src
 *   ports in a normal IPv6 rule, or in the inner layer of an IPv6 tunnel rule
 *   when the tunnel_type is VxLAN.
 */
struct hinic3_tcam_key_ipv6_mem {
#if (RTE_BYTE_ORDER == RTE_BIG_ENDIAN)
	u32 rsvd0 : 16;
	u32 ip_proto : 8;
	u32 tunnel_type : 4;
	u32 outer_ip_type : 1;
#ifdef HINIC3_TRAFFIC_BIFUR
	u32 vlan_flag : 1;
	u32 bifur_flag : 2;
#else
	u32 vlan_flag : 1;
	u32 rsvd1 : 2;
#endif

	u32 function_id : 15;
	u32 ip_type : 1;
	u32 sipv6_key0 : 16;

	u32 sipv6_key1 : 16;
	u32 sipv6_key2 : 16;

	u32 sipv6_key3 : 16;
	u32 sipv6_key4 : 16;

	u32 sipv6_key5 : 16;
	u32 sipv6_key6 : 16;

	u32 sipv6_key7 : 16;
	u32 dport : 16;

	u32 sport : 16;
	u32 dipv6_key0 : 16;

	u32 dipv6_key1 : 16;
	u32 dipv6_key2 : 16;

	u32 dipv6_key3 : 16;
	u32 dipv6_key4 : 16;

	u32 dipv6_key5 : 16;
	u32 dipv6_key6 : 16;

	u32 dipv6_key7 : 16;
	u32 rsvd2 : 16;
#else
#ifdef HINIC3_TRAFFIC_BIFUR
	u32 bifur_flag : 2;
	u32 vlan_flag : 1;
#else
	u32 rsvd1 : 2;
	u32 vlan_flag : 1;
#endif
	u32 outer_ip_type : 1;
	u32 tunnel_type : 4;
	u32 ip_proto : 8;
	u32 rsvd0 : 16;

	u32 sipv6_key0 : 16;
	u32 ip_type : 1;
	u32 function_id : 15;

	u32 sipv6_key2 : 16;
	u32 sipv6_key1 : 16;

	u32 sipv6_key4 : 16;
	u32 sipv6_key3 : 16;

	u32 sipv6_key6 : 16;
	u32 sipv6_key5 : 16;

	u32 dport : 16;
	u32 sipv6_key7 : 16;

	u32 dipv6_key0 : 16;
	u32 sport : 16;

	u32 dipv6_key2 : 16;
	u32 dipv6_key1 : 16;

	u32 dipv6_key4 : 16;
	u32 dipv6_key3 : 16;

	u32 dipv6_key6 : 16;
	u32 dipv6_key5 : 16;

	u32 rsvd2 : 16;
	u32 dipv6_key7 : 16;
#endif
};

struct hinic3_tcam_key_ipv6_mem_htn {
#if (RTE_BYTE_ORDER == RTE_BIG_ENDIAN)
	u32 rsvd0 : 16;
	u32 ip_proto : 8;
	u32 tunnel_type : 3;
	u32 function_id_h : 5;

	u32 function_id_l : 5;
	u32 ip_type : 2;
	u32 outer_ip_type : 1;
	u32 rsvd1 : 8;
	u32 sipv6_key0 : 16;

	u32 sipv6_key1 : 16;
	u32 sipv6_key2 : 16;

	u32 sipv6_key3 : 16;
	u32 sipv6_key4 : 16;

	u32 sipv6_key5 : 16;
	u32 sipv6_key6 : 16;

	u32 sipv6_key7 : 16;
	u32 dipv6_key0 : 16;

	u32 dipv6_key1 : 16;
	u32 dipv6_key2 : 16;

	u32 dipv6_key3 : 16;
	u32 dipv6_key4 : 16;

	u32 dipv6_key5 : 16;
	u32 dipv6_key6 : 16;

	u32 dipv6_key7 : 16;
	u32 sport : 16;

	u32 dport : 16;
	u32 rsvd2 : 16;
#else
	u32 function_id_h : 5;
	u32 tunnel_type : 3;
	u32 ip_proto : 8;
	u32 rsvd0 : 16;

	u32 sipv6_key0 : 16;
	u32 rsvd1 : 8;
	u32 outer_ip_type : 1;
	u32 ip_type : 2;
	u32 function_id_l : 5;

	u32 sipv6_key2 : 16;
	u32 sipv6_key1 : 16;

	u32 sipv6_key4 : 16;
	u32 sipv6_key3 : 16;

	u32 sipv6_key6 : 16;
	u32 sipv6_key5 : 16;

	u32 dipv6_key0 : 16;
	u32 sipv6_key7 : 16;

	u32 dipv6_key2 : 16;
	u32 dipv6_key1 : 16;

	u32 dipv6_key4 : 16;
	u32 dipv6_key3 : 16;

	u32 dipv6_key6 : 16;
	u32 dipv6_key5 : 16;

	u32 sport : 16;
	u32 dipv6_key7 : 16;

	u32 rsvd2 : 16;
	u32 dport : 16;
#endif
};

struct hinic3_tcam_key_vxlan_ipv6_mem {
#if (RTE_BYTE_ORDER == RTE_BIG_ENDIAN)
	u32 rsvd0 : 16;
	u32 ip_proto : 8;
	u32 tunnel_type : 4;
	u32 rsvd1 : 4;

	u32 function_id : 15;
	u32 ip_type : 1;
	u32 dipv6_key0 : 16;

	u32 dipv6_key1 : 16;
	u32 dipv6_key2 : 16;

	u32 dipv6_key3 : 16;
	u32 dipv6_key4 : 16;

	u32 dipv6_key5 : 16;
	u32 dipv6_key6 : 16;

	u32 dipv6_key7 : 16;
	u32 dport : 16;

	u32 sport : 16;
	u32 vlan_flag : 1;
	u32 rsvd2 : 15;

	u32 rsvd3 : 16;
	u32 outer_sipv4_h : 16;

	u32 outer_sipv4_l : 16;
	u32 outer_dipv4_h : 16;

	u32 outer_dipv4_l : 16;
	u32 vni_h : 16;

	u32 vni_l : 16;
	u32 rsvd4 : 16;
#else
	u32 rsvd1 : 4;
	u32 tunnel_type : 4;
	u32 ip_proto : 8;
	u32 rsvd0 : 16;

	u32 dipv6_key0 : 16;
	u32 ip_type : 1;
	u32 function_id : 15;

	u32 dipv6_key2 : 16;
	u32 dipv6_key1 : 16;

	u32 dipv6_key4 : 16;
	u32 dipv6_key3 : 16;

	u32 dipv6_key6 : 16;
	u32 dipv6_key5 : 16;

	u32 dport : 16;
	u32 dipv6_key7 : 16;

	u32 rsvd2 : 15;
	u32 vlan_flag : 1;
	u32 sport : 16;

	u32 outer_sipv4_h : 16;
	u32 rsvd3 : 16;

	u32 outer_dipv4_h : 16;
	u32 outer_sipv4_l : 16;

	u32 vni_h : 16;
	u32 outer_dipv4_l : 16;

	u32 rsvd4 : 16;
	u32 vni_l : 16;
#endif
};

struct hinic3_tcam_key_vxlan_ipv6_mem_htn {
#if (RTE_BYTE_ORDER == RTE_BIG_ENDIAN)
	u32 rsvd0 : 16;
	u32 ip_proto : 8;
	u32 tunnel_type : 3;
	u32 function_id_h : 5;

	u32 function_id_l : 5;
	u32 ip_type : 2;
	u32 outer_ip_type : 1;
	u32 rsvd1 : 8;
	u32 outer_sipv4_h : 16;

	u32 outer_sipv4_l : 16;
	u32 outer_dipv4_h : 16;

	u32 outer_dipv4_l : 16;
	u32 rsvd2 : 8;
	u32 vni_h : 8;

	u32 vni_l : 16;
	u32 rsvd3 : 16;

	u32 rsvd4 : 16;
	u32 dipv6_key0 : 16;

	u32 dipv6_key1 : 16;
	u32 dipv6_key2 : 16;

	u32 dipv6_key3 : 16;
	u32 dipv6_key4 : 16;

	u32 dipv6_key5 : 16;
	u32 dipv6_key6 : 16;

	u32 dipv6_key7 : 16;
	u32 sport : 16;

	u32 dport : 16;
	u32 rsvd2 : 16;
#else
	u32 function_id_h : 5;
	u32 tunnel_type : 3;
	u32 ip_proto : 8;
	u32 rsvd0 : 16;

	u32 outer_sipv4_h : 16;
	u32 rsvd1 : 8;
	u32 outer_ip_type : 1;
	u32 ip_type : 2;
	u32 function_id_l : 5;

	u32 outer_dipv4_h : 16;
	u32 outer_sipv4_l : 16;

	u32 vni_h : 8;
	u32 rsvd2 : 8;
	u32 outer_dipv4_l : 16;

	u32 rsvd3 : 16;
	u32 vni_l : 16;

	u32 dipv6_key0 : 16;
	u32 rsvd4 : 16;

	u32 dipv6_key2 : 16;
	u32 dipv6_key1 : 16;

	u32 dipv6_key4 : 16;
	u32 dipv6_key3 : 16;

	u32 dipv6_key6 : 16;
	u32 dipv6_key5 : 16;

	u32 sport : 16;
	u32 dipv6_key7 : 16;

	u32 rsvd5 : 16;
	u32 dport : 16;
#endif
};

/*
 * TCAM key structure. The two unions indicate the key and mask respectively.
 * The TCAM key is consistent with the TCAM entry.
 */
struct hinic3_tcam_key {
	union {
		struct hinic3_tcam_key_mem key_info;
		struct hinic3_tcam_key_ipv6_mem key_info_ipv6;
		struct hinic3_tcam_key_vxlan_ipv6_mem key_info_vxlan_ipv6;

		struct hinic3_tcam_key_mem_htn key_info_htn;
		struct hinic3_tcam_key_ipv6_mem_htn key_info_ipv6_htn;
		struct hinic3_tcam_key_vxlan_ipv6_mem_htn key_info_vxlan_ipv6_htn;
	};
	union {
		struct hinic3_tcam_key_mem key_mask;
		struct hinic3_tcam_key_ipv6_mem key_mask_ipv6;
		struct hinic3_tcam_key_vxlan_ipv6_mem key_mask_vxlan_ipv6;

		struct hinic3_tcam_key_mem_htn key_mask_htn;
		struct hinic3_tcam_key_ipv6_mem_htn key_mask_ipv6_htn;
		struct hinic3_tcam_key_vxlan_ipv6_mem_htn key_mask_vxlan_ipv6_htn;
	};
};

/* Structure indicates the TCAM filter. */
struct hinic3_tcam_filter {
	TAILQ_ENTRY(hinic3_tcam_filter) entries; /**< Filter entry, used for linked list operations. */
	uint16_t dynamic_block_id;	 /**< Dynamic block ID. */
	uint16_t index;			 /**< TCAM index. */
	struct hinic3_tcam_key tcam_key; /**< Indicate TCAM key. */
	uint16_t queue;			 /**< Allocated RX queue. */
};

/* Define a linked list header for storing hinic3_tcam_filter data. */
TAILQ_HEAD(hinic3_tcam_filter_list, hinic3_tcam_filter);

struct hinic3_tcam_dynamic_block {
	TAILQ_ENTRY(hinic3_tcam_dynamic_block) entries;
	u16 dynamic_block_id;
	u16 dynamic_index_cnt;
	u8 dynamic_index[HINIC3_TCAM_DYNAMIC_BLOCK_SIZE];
};

/* Define a linked list header for storing hinic3_tcam_dynamic_block data. */
TAILQ_HEAD(hinic3_tcam_dynamic_filter_list, hinic3_tcam_dynamic_block);

/* Indicate TCAM dynamic block info. */
struct hinic3_tcam_dynamic_block_info {
	struct hinic3_tcam_dynamic_filter_list tcam_dynamic_list;
	u16 dynamic_block_cnt;
};

/* Structure is used to store TCAM information. */
struct hinic3_tcam_info {
	struct hinic3_tcam_filter_list tcam_list;
	struct hinic3_tcam_dynamic_block_info tcam_dynamic_info;
};

/* Obtain the upper and lower 16 bits. */
#define HINIC3_32_UPPER_16_BITS(n) ((((n) >> 16)) & 0xffff)
#define HINIC3_32_LOWER_16_BITS(n) ((n) & 0xffff)

/* Number of protocol rules */
#define HINIC3_ARP_RULE_NUM  3
#define HINIC3_RARP_RULE_NUM 1
#define HINIC3_SLOW_RULE_NUM 2
#define HINIC3_LLDP_RULE_NUM 2
#define HINIC3_CNM_RULE_NUM  1
#define HINIC3_ECP_RULE_NUM  2

#define HINIC3_UINT1_MAX	0x1
#define HINIC3_UINT2_MAX	0x3
#define HINIC3_UINT3_MAX	0x7
#define HINIC3_UINT4_MAX	0xf
#define HINIC3_UINT5_WIDTH	0x5
#define HINIC3_UINT5_MAX	0x1f
#define HINIC3_UINT15_MAX	0x7fff
#define BIFUR_EN              0x2

/* Define Ethernet type. */
#define RTE_ETHER_TYPE_CNM 0x22e7
#define RTE_ETHER_TYPE_ECP 0x8940


#ifndef HINIC3_QUEUE_MAX
#define HINIC3_QUEUE_MAX          16
#endif

/* 256-entry RSS indir table split into 8 groups; group0 for func RSS */
#define HINIC3_RSS_INDIR_GROUP_NUM	8
#define HINIC3_RSS_INDIR_GROUP_SIZE	(HINIC3_RSS_INDIR_SIZE / HINIC3_RSS_INDIR_GROUP_NUM)
#define HINIC3_RSS_FUNC_GROUP_ID	0
#define HINIC3_FLOW_RSS_GROUP_MAX	(HINIC3_RSS_INDIR_GROUP_NUM - 1)

/* RSS template entry structure for managing RSS templates */
struct hinic3_rss_template_entry {
	TAILQ_ENTRY(hinic3_rss_template_entry) node;
	u16 queue_num;			/* Number of queues */
	u16 rss_group_id;		/* Indir group id: 1-7 for flow rss */
	u16 ref_count;
	u16 rsvd;
	u16 queues[HINIC3_QUEUE_MAX];	/* Queue list */
};

/* RSS template list head */
TAILQ_HEAD(hinic3_rss_template_list, hinic3_rss_template_entry);

/* Maximum number of RSS templates per function */
#ifndef FUNC_MAX_DPDK_NUM
#define FUNC_MAX_DPDK_NUM 32
#endif

int hinic3_flow_add_del_fdir_filter(struct rte_eth_dev *dev,
				    struct hinic3_fdir_filter *fdir_filter,
				    bool add);
int hinic3_flow_add_del_ethertype_filter(struct rte_eth_dev *dev,
					 struct hinic3_ethertype_filter *ethertype_filter,
					 bool add);

void hinic3_free_fdir_filter(struct rte_eth_dev *dev);
int hinic3_enable_rxq_fdir_filter(struct rte_eth_dev *dev, u32 queue_id, u32 able);
int hinic3_flow_parse_attr(const struct rte_flow_attr *attr, struct rte_flow_error *error);
void hinic3_fdir_tcam_info_htn_init(struct rte_eth_dev *dev, struct hinic3_fdir_filter *rule,
				    struct hinic3_tcam_key *tcam_key,
				    struct hinic3_tcam_cfg_rule *fdir_tcam_rule);
int hinic3_set_fdir_ethertype_filter(void *hwdev, u8 pkt_type, void *filter, u8 en);
u16 hinic3_tcam_alloc_index(void *dev, u16 *block_id);
void hinic3_tcam_index_free(void *dev, u16 index, u16 block_id);
int hinic3_flow_query_fdir_filter(struct rte_eth_dev *dev, struct hinic3_fdir_filter *fdir_filter,
					__rte_unused u64 *hits, __rte_unused u64 *bytes_count);

static inline void tcam_translate_key_y(u8 *key_y, u8 *src_input, u8 *mask, u8 len)
{
	u8 idx;

	for (idx = 0; idx < len; idx++)
		key_y[idx] = src_input[idx] & mask[idx];
}

static inline void tcam_translate_key_x(u8 *key_x, u8 *key_y, u8 *mask, u8 len)
{
	u8 idx;

	for (idx = 0; idx < len; idx++)
		key_x[idx] = key_y[idx] ^ mask[idx];
}

static inline void tcam_key_calculate(struct hinic3_tcam_key *tcam_key,
				      struct hinic3_tcam_cfg_rule *fdir_tcam_rule)
{
	tcam_translate_key_y(fdir_tcam_rule->key.y,
		(u8 *)(&tcam_key->key_info),
		(u8 *)(&tcam_key->key_mask),
		HINIC3_TCAM_FLOW_KEY_SIZE);
	tcam_translate_key_x(fdir_tcam_rule->key.x,
		fdir_tcam_rule->key.y,
		(u8 *)(&tcam_key->key_mask),
		HINIC3_TCAM_FLOW_KEY_SIZE);
}

#endif /**< _HINIC3_FDIR_H_ */
