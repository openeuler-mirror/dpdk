/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2019 Huawei Technologies Co., Ltd
 */

#ifndef _HINIC3_PMD_FDIR_H_
#define _HINIC3_PMD_FDIR_H_

#define HINIC3_FLOW_MAX_PATTERN_NUM   16

#define HINIC3_TCAM_DYNAMIC_BLOCK_SIZE  16

#define HINIC3_TCAM_DYNAMIC_MAX_FILTERS 1024

#define HINIC3_PKT_TCAM_DYNAMIC_INDEX_START(block_index)  \
		(HINIC3_TCAM_DYNAMIC_BLOCK_SIZE * (block_index))

#ifdef HINIC3_TRAFFIC_BIFUR
#define HINIC3_RSS_QUEUE_BUF 128
#endif

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
	uint16_t ether_type;
};

struct hinic3_fdir_filter {
	int tcam_index;
	uint8_t ip_type; /* inner ip type */
	uint8_t outer_ip_type; /* outer ip type */
	uint8_t tunnel_type;
	struct hinic3_fdir_rule_key key_mask;
	struct hinic3_fdir_rule_key key_spec;
	uint32_t rq_index; /* queue assigned when matched */
#ifdef HINIC3_TRAFFIC_BIFUR
	uint32_t queue_num;
#endif
};

struct hinic3_filter_t {
	u16  filter_rule_nums;
	enum rte_filter_type filter_type;
	struct rte_eth_ethertype_filter ethertype_filter;
	struct hinic3_fdir_filter fdir_filter;

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
	u32 rsvd2 : 16;

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

	u32 rsvd2 : 16;
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
	u32 model : 1;
	u32 bifur_flag : 2;
#else
	u32 rsvd1 : 3;
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
	u32 model : 1;
#else
	u32 rsvd1 : 3;
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
	u32 rsvd2 : 16;

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

	u32 rsvd2 : 16;
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

struct hinic3_tcam_key {
	union {
		struct hinic3_tcam_key_mem key_info;
		struct hinic3_tcam_key_ipv6_mem key_info_ipv6;
		struct hinic3_tcam_key_vxlan_ipv6_mem key_info_vxlan_ipv6;
	};

	union {
		struct hinic3_tcam_key_mem key_mask;
		struct hinic3_tcam_key_ipv6_mem key_mask_ipv6;
		struct hinic3_tcam_key_vxlan_ipv6_mem key_mask_vxlan_ipv6;
	};
};

struct hinic3_tcam_filter {
	TAILQ_ENTRY(hinic3_tcam_filter) entries;
	uint16_t dynamic_block_id;
	uint16_t index; /* tcam index */
	struct hinic3_tcam_key tcam_key;
	uint16_t queue; /* rx queue assigned to */
};

TAILQ_HEAD(hinic3_tcam_filter_list, hinic3_tcam_filter);

struct hinic3_tcam_dynamic_block {
	TAILQ_ENTRY(hinic3_tcam_dynamic_block) entries;
	u16 dynamic_block_id;
	u16 dynamic_index_cnt;
	u8 dynamic_index[HINIC3_TCAM_DYNAMIC_BLOCK_SIZE];
};

TAILQ_HEAD(hinic3_tcam_dynamic_filter_list, hinic3_tcam_dynamic_block);

struct hinic3_tcam_dynamic_block_info {
	struct hinic3_tcam_dynamic_filter_list tcam_dynamic_list;
	u16 dynamic_block_cnt;
};

struct hinic3_tcam_info {
	struct hinic3_tcam_filter_list tcam_list;
	struct hinic3_tcam_dynamic_block_info tcam_dynamic_info;
};

#define HINIC3_32_UPPER_16_BITS(n)   ((((n) >> 16)) & 0xffff)
#define HINIC3_32_LOWER_16_BITS(n)   ((n) & 0xffff)

#define HINIC3_ARP_RULE_NUM 3
#define HINIC3_RARP_RULE_NUM 1
#define HINIC3_SLOW_RULE_NUM 2
#define HINIC3_LLDP_RULE_NUM 2
#define HINIC3_CNM_RULE_NUM 1
#define HINIC3_ECP_RULE_NUM 2

#define RTE_ETHER_TYPE_CNM  0x22e7
#define RTE_ETHER_TYPE_ECP 0x8940

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

	HINIC3_PKT_UNKNOWN = 31,
};

int hinic3_flow_add_del_fdir_filter(struct rte_eth_dev *dev,
				    struct hinic3_fdir_filter *fdir_filter,
				    bool add);
int hinic3_flow_add_del_ethertype_filter(struct rte_eth_dev *dev,
					 struct rte_eth_ethertype_filter *ethertype_filter,
					 bool add);

void hinic3_free_fdir_filter(struct rte_eth_dev *dev);
int hinic3_enable_rxq_fdir_filter(struct rte_eth_dev *dev, u32 queue_id, u32 able);
int hinic3_flow_parse_attr(const struct rte_flow_attr *attr, struct rte_flow_error *error);

int hinic3_flow_query_fdir_filter(struct rte_eth_dev *dev, struct hinic3_fdir_filter *fdir_filter,
					__rte_unused u64 *hits, __rte_unused u64 *bytes_count);

#endif
