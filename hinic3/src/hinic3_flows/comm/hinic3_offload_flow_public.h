/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_OFFLOAD_FLOW_PUBLIC_H
#define HINIC3_OFFLOAD_FLOW_PUBLIC_H

#include "rte_flow.h"
#include "rte_ethdev.h"
#include "hinic3_flow_agent_public.h"
#include "hinic3_driver_public.h"
#include "hinic3_message.h"
#include "hinic3_parse_agent_config.h"

#define PKT_RX_HW_OFFLOAD_INFO  (1ULL << 39)
#define HINIC3_OFFLOAD_DUPLICATE_TIME_CHECK 1000 /* ms */
#define HINIC3_IPV6_ADDR_UINT32_LEN 4
#define HINIC3_IPV6_ADDR_UINT8_LEN 16
#define HINIC3_UINT8_SHIFT 8u
#define HINIC3_VNI_LEN 3
#define HINIC3_VXLAN_RSVD0_LEN 3
#define HINIC3_SHIM_RSVD0_LEN 2
#define HINIC3_TNI_LEN 3
#define HINIC3_MIRROR_IP_LEN 16
#define HINIC3_FLOW_PORT_ID 0
/** dpdk 23.11 */
#define HINIC3_FLOW_ITEM_TYPE_SW_UFID (300)
#define HINIC3_FLOW_ITEM_TYPE_POLICY_ID 199
#define HINIC3_FLOW_ITEMS_NUM 15

#define HINIC3_MAC_LEN 3
#define HINIC3_IP_LEN  4

#define HINIC3_GENEVE_OFFSET_ONE 8
#define HINIC3_GENEVE_OFFSET_TWO 16
#define HINIC3_GENEVE_OFFSET_THREE 24

#define HINIC3_FLG_VXLAN_ETH               (1LLU << 0)
#define HINIC3_FLG_VXLAN_IP                (1LLU << 1)
#define HINIC3_FLG_VXLAN_UDP               (1LLU << 2)
#define HINIC3_FLG_VXLAN_VNI               (1LLU << 3)
#define HINIC3_FLG_VXLAN_VLAN              (1LLU << 4)

#define HINIC3_FLG_GENEVE_ETH              (1LLU << 0)
#define HINIC3_FLG_GENEVE_IP               (1LLU << 1)
#define HINIC3_FLG_GENEVE_UDP              (1LLU << 2)
#define HINIC3_FLG_GENEVE                  (1LLU << 3)
#define HINIC3_FLG_GENEVE_VLAN             (1LLU << 4)
#define HINIC3_FLG_GENEVE_OPT              (1LLU << 5)

#define HINIC3_FLG_VXLAN_INTEGRITY_COMM    (HINIC3_FLG_VXLAN_ETH |        \
                                           HINIC3_FLG_VXLAN_IP |         \
                                           HINIC3_FLG_VXLAN_UDP |        \
                                           HINIC3_FLG_VXLAN_VNI)

#define HINIC3_FLG_VXLAN_INTEGRITY_VLAN    (HINIC3_FLG_VXLAN_ETH |        \
                                           HINIC3_FLG_VXLAN_VLAN |       \
                                           HINIC3_FLG_VXLAN_IP |         \
                                           HINIC3_FLG_VXLAN_UDP |        \
                                           HINIC3_FLG_VXLAN_VNI)

#define HINIC3_FLG_GENEVE_INTEGRITY_COMM   (HINIC3_FLG_VXLAN_ETH |        \
                                           HINIC3_FLG_GENEVE_IP |        \
                                           HINIC3_FLG_GENEVE_UDP |        \
                                           HINIC3_FLG_GENEVE |        \
                                           HINIC3_FLG_GENEVE_OPT)

#define HINIC3_FLG_GENEVE_INTEGRITY_VLAN   (HINIC3_FLG_VXLAN_ETH |        \
                                           HINIC3_FLG_GENEVE_VLAN |      \
                                           HINIC3_FLG_GENEVE_IP |        \
                                           HINIC3_FLG_GENEVE_UDP |        \
                                           HINIC3_FLG_GENEVE |        \
                                           HINIC3_FLG_GENEVE_OPT)

#define MILLION_FLOWS           (1 << 20)
#define OFFLOAD_FLOW_BUCKETS_SHIFT 16
#define OFFLOAD_FLOW_BUCKETS (1 << OFFLOAD_FLOW_BUCKETS_SHIFT)

#define HINIC3_FLOW_TYPE_OFFSET 30
#define HINIC3_FLOW_ATTR_TYPE_OFFSET 27
#define HINIC3_FLOW_TYPE_MASK 0x3
#define HINIC3_EMC_FLOW_TYPE 0
#define HINIC3_MEGA_FLOW_TYPE 1
#define HINIC3_DPHASH_FLOW_TYPE 2
#define HINIC3_ACL_FLOW_TYPE 3
#define HINIC3_DEFAULT_WINDOW 0x1000
#define HINIC3_PUT_FLOW_SUCCESS 0

enum {
    HINIC3_DP_OPENFLOW,
    HINIC3_DP_ELB_TRANSLATE,
    HINIC3_DP_IFP,
    HINIC3_DP_L2FWD,
    HINIC3_DP_MAX
};

enum {
    PROCESS_KEY_ACT_SUCC,
    PROCESS_KEY_ACT_FAIL,
    PROCESS_KEY_ACT_SKIP,
    PROCESS_KEY_ACT_FRAG,
};

enum {
    PROCESS_KEY_SUCC,
    PROCESS_KEY_FAIL,
    PROCESS_KEY_SKIP,
};

enum {
    PROCESS_ACTION_SUCC = 0,
    PROCESS_ACTION_FAIL,
    PROCESS_ACTION_RECIRCLE,
};

enum hinic3_offload_policy_status {
    POLICY_STATUS_OFFLOAD,
    POLICY_STATUS_OFFLOAD_APPLIED,
    POLICY_STATUS_NO_OFFLOAD
};

struct hinic3_offload_action {
    struct hinic3_nlattr act_nla;
    bool has_output;
    bool has_vxlan_pop;
    bool has_vxlan_push;
    bool has_geneve_pop;
    bool has_geneve_push;
    bool has_ct;
    bool has_circle;
    bool has_vlan_push;
    bool has_vlan_pop;
    bool has_dp_hash;
    uint16_t vlan_id;
    struct hinic3_flow_act_vxlan_gpe_header *vxlan_hdr;
};

struct hinic3_flow_offload_info {
    unsigned pmd_core_id;
    int pkts_cnt;
    struct dp_packet **pkts;
    hinic3_u128 sw_ufid;
    struct rte_flow *mega_flow;
    uint16_t odp_inport_id;
    uint8_t in_port_type;
    bool is_ct;
    bool is_recirc;
    uint32_t table_id;
    enum hinic3_offload_policy_status policy_status;
};

struct hinic3_flow_offload_param {
    struct hinic3_flow_offload_info *offload_info;
    struct hinic3_flow_agent_db *hw_offload;
    struct hinic3_pkt_key_cache_s pkt_key_cache;
    struct dp_packet *one_pkt;
    struct hinic3_pkt_info *pkt_info;
    struct hinic3_pkt_header pkt_hdr;
    struct hinic3_conntrack_full_key full_key;
    struct hinic3_nlattr hinic3_key;
    struct hinic3_nlattr hinic3_mask;
    struct hinic3_offload_action cur_actions;
    struct hinic3_nlattr hinic3_actions;
    struct hinic3_nlattr hinic3_args;
    int lcore_idx;
    struct hinic3_offload_buf_s offload_buff;
};

struct hinic3_flow_callback_info {
    bool reply;
    bool is_ct;
    bool is_dp_hash;
    bool is_modify;
    uint8_t flow_put_result;
    uint8_t policy_flag;
    uint32_t mega_ufid_cnt;
    uint32_t core_id;
    uint32_t flow_hash;
    clock_t start_t;
    hinic3_u128 *mega_ufid_list;
    hinic3_u128 mega_ufid;
    hinic3_u128 policy_id;
    struct hinic3_conntrack_full_key ct_key;
    struct hinic3_conntrack_full_key no_ct_key;
    struct conn_key ovs_key;
};

struct hinic3_flow_act_geneve_encap_data {
    uint32_t dmac[HINIC3_MAC_LEN];
    uint32_t smac[HINIC3_MAC_LEN];
    uint32_t sip[HINIC3_IP_LEN];
    uint32_t dip[HINIC3_IP_LEN];
    uint32_t option_value;
    uint32_t geneve_vni;
    uint32_t type;
    uint32_t sport;
    uint32_t dport;
    uint32_t option_class;
    uint32_t geneve_flags;
    uint32_t opt_len;
    uint32_t length;
    uint32_t ip_version;
};

typedef struct {
    struct rte_flow *rte_flow;
    struct dp_packet *packet;
    struct hinic3_pkt_info *pkt_info;
    struct hinic3_flow_offload_info *offload_info;
    uint64_t reverse_ufid;
    struct hinic3_dpif_flow *flow;
    struct hinic3_nlattr *args;
    size_t args_len;
    struct hinic3_nlattr *hinic3_actions;
    struct hinic3_conntrack_full_key *full_key;
} hinic3_flow_info;

struct hinic3_inner_metadata {
    uint32_t rx_seq;
    struct {
        uint8_t offload : 2;
        /* Copied from struct hinic3_pkt_user_data */
        uint8_t traffic_type : 6;
    } status;
    /* index of array pmd_status[pmd_core_id]->pkt_info_bufs */
    uint8_t offset;
    uint16_t lcore_idx;
};

typedef struct {
    uint32_t live_time;
    uint32_t core_id;
    int sw_ufid_buffer_size;
    int sw_ufid_cnt;
    void *sw_ufid_buffer;
    uint64_t reverse_hw_ufid;
    struct hinic3_conntrack_full_key *ct_key;
    struct hinic3_conntrack_full_key *no_ct_key;
} hinic3_args_context;

typedef int (*ct_offload_action_fn)(struct hinic3_nlattr *hinic3_actions, const struct rte_flow_action *act,
                             struct hinic3_flow_offload_param *param, struct hinic3_conntrack_full_key *full_key);
struct hinic3_ct_offload_ops {
    ct_offload_action_fn ct_offload_action;
};


struct hinic3_ct_offload_ops* hinic3_get_ct_offload_ops(void);

static inline const struct rte_flow_action *next_flexda_action(const struct rte_flow_action actions[],
    const struct rte_flow_action *cur)
{
    const struct rte_flow_action *next = cur ? (cur + 1) : &actions[0];
    return next;
}

static inline const struct rte_flow_action *next_no_void_action(const struct rte_flow_action actions[],
    const struct rte_flow_action *cur)
{
    const struct rte_flow_action *next = cur ? (cur + 1) : &actions[0];
    while (1) {
        if (next->type != RTE_FLOW_ACTION_TYPE_VOID) {
            return next;
        }
        next++;
    }
}

static inline const struct rte_flow_action *next_action(const struct rte_flow_action actions[],
    const struct rte_flow_action *cur)
{
    if (hinic3_card_mod_get() == PROG_MODE)
        return next_flexda_action(actions, cur);
    else
        return next_no_void_action(actions, cur);
}

static inline const struct rte_flow_item *next_no_end_pattern(const struct rte_flow_item pattern[],
    const struct rte_flow_item *cur)
{
    const struct rte_flow_item *next = cur ? (cur + 1) : &pattern[0];

    if (next->type == RTE_FLOW_ITEM_TYPE_END) {
        next = NULL;
    }

    return next;
}

static inline uint32_t hinic3_eth_flow_type(const struct rte_flow_attr *attr)
{
    uint32_t flow_type = (attr->reserved >> HINIC3_FLOW_ATTR_TYPE_OFFSET) & HINIC3_FLOW_TYPE_MASK;
    return flow_type;
}

void hinic3_offload_thread_rx_put_ack(uint32_t thread_id, struct hinic3_dp_extend_info *dp_info);
uint32_t hinic3_convert_u8_to_u32(const uint8_t *src_array, uint8_t len);
void hinic3_free_pmd_pkt_info(const struct hinic3_inner_metadata *udata64);

#endif
