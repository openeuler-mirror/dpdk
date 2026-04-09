/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2022 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_OFFLOAD_FLOW_H
#define HINIC3_OFFLOAD_FLOW_H

#include <stdint.h>
#include "hinic3_offload_flow_public.h"
#include "hinic3_ufid_map_rte_flow.h"

#define HINIC3_VXLAN_VNI_DATA_SIZE 3
#define HINIC3_BIT_MID_MOVE_INDEX  8
#define HINIC3_ICMP_ECHO_REQUEST_TYPE  8
#define HINIC3_ICMP_ECHO_REPLY_TYPE  0
#define HINIC3_ICMP_ECHO_NORMAL_CODE  0
#define HINIC3_ICMP_TYPE_MASK 0xFF00
#define HINIC3_ICMP_CODE_MASK 0X00FF

struct hash_table_node *hinic3_get_offload_flow_bucket(uint32_t flow_hash, uint32_t table_id);
struct rte_flow *hinic3_offload_flow(const struct rte_flow_item pattern[], const struct rte_flow_action actions[],
    struct hinic3_flow_agent_db *hw_offload, struct rte_flow_error *error, uint8_t table_id);
int hinic3_process_flow_key(const struct rte_flow_item pattern[], struct hinic3_flow_offload_param *param,
    struct rte_flow *mega_flow, struct hinic3_dpif_flow *flow, uint8_t *has_vxlan_item);
int hinic3_process_flow_mask(const struct rte_flow_item pattern[], struct hinic3_flow_offload_param *param,
    struct fuzzy_flow *mega_flow, struct hinic3_dpif_flow *flow, uint8_t *has_vxlan_item);
int hinic3_offload_parse_key(const struct rte_flow_item pattern[], struct hinic3_conntrack_full_key *key,
    uint8_t *has_vxlan_item);
int hinic3_offload_parse_mask(const struct rte_flow_item pattern[], struct hinic3_conntrack_full_key *mask, uint8_t *has_vxlan_item);
void hinic3_offload_flow_construct_key_entrance(const struct hinic3_conntrack_key *key, struct hinic3_nlattr *hinic3_key);
struct rte_flow *hinic3_modify_flow(const struct rte_flow_item pattern[], const struct rte_flow_action actions[],
                                        struct hinic3_flow_agent_db *hw_offload, struct rte_flow_error *error);
void hinic3_flow_log_original_entry(const struct rte_flow_item patterns[], const struct rte_flow_action actions[]);
void hinic3_init_hydra_key(struct hinic3_conntrack_full_key *key);
void hinic3_process_hydra_key(struct hinic3_conntrack_full_key *key);
int hinic3_parse_hydra_key(const struct rte_flow_item *item, struct hinic3_conntrack_full_key *key);
void hinic3_flow_agent_construct_hydra_key(struct hinic3_nlattr *hinic3_key,
    const struct hinic3_conntrack_key *key);
#endif
