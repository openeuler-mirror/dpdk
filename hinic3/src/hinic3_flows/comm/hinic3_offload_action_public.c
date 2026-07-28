/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Huawei Technologies Co., Ltd
 */
#include "hinic3_offload_action_public.h"

#define VXLAN_HEADER_FLAG 0x08

static void hinic3_fill_vxlan_ip_header(struct hinic3_flow_act_vxlan_gpe_header *offload_vxlan_hdr,
                                       const struct rte_flow_item *item)
{
    const struct hinic3_ip_header *ipv4 = NULL;
    const struct hinic3_ip6_header *ipv6 = NULL;

    switch (item->type) {
        case RTE_FLOW_ITEM_TYPE_IPV4:
            ipv4 = item->spec;
            offload_vxlan_hdr->ip_version = HINIC3_IP_ADDR_V4;
            offload_vxlan_hdr->sip[0] = ipv4->ip_src;
            offload_vxlan_hdr->dip[0] = ipv4->ip_dst;
            offload_vxlan_hdr->dscp = ipv4->ip_tos;
            break;
        case RTE_FLOW_ITEM_TYPE_IPV6:
            ipv6 = item->spec;
            offload_vxlan_hdr->ip_version = HINIC3_IP_ADDR_V6;
            memcpy(offload_vxlan_hdr->sip, &ipv6->saddr, sizeof(ipv6->saddr));
            memcpy(offload_vxlan_hdr->dip, &ipv6->daddr, sizeof(ipv6->daddr));
            break;
        default:
            break;
    }
    return;
}

static void hinic3_offload_fill_vxlan_udp_header(struct hinic3_flow_act_vxlan_gpe_header *offload_vxlan_hdr,
    const struct rte_flow_item *item)
{
    const struct rte_flow_item_udp *udp = NULL;
    udp = item->spec;
    offload_vxlan_hdr->sport = udp->hdr.src_port;
    offload_vxlan_hdr->dport = udp->hdr.dst_port; // ovs统一使用上层传入的dst_port值

    return;
}

int hinic3_offload_fill_vxlan_header(struct hinic3_flow_act_vxlan_gpe_header *offload_vxlan_hdr,
    const struct rte_flow_action_vxlan_encap *vxlan_info)
{
    uint32_t flags = 0;
    const struct hinic3_eth_header *eth = NULL;
    const struct rte_flow_item_vxlan *vxlan = NULL;
    const struct rte_flow_item *item = NULL;

    item = next_no_end_pattern(vxlan_info->definition, NULL);
    while (item) {
        if (item->spec == NULL) {
            HINIC3_LOG(ERR, FLOW, "hinic3load vxlan action parse error. item spec is null. item type is %d", item->type);
            return -1;
        }
        switch (item->type) {
            case RTE_FLOW_ITEM_TYPE_ETH:
                eth = item->spec;
                memcpy(offload_vxlan_hdr->dmac, &eth->dst, ETH_ALEN);
                memcpy(offload_vxlan_hdr->smac, &eth->src, ETH_ALEN);
                flags |= HINIC3_FLG_VXLAN_ETH;
                break;
            case RTE_FLOW_ITEM_TYPE_VLAN:
                offload_vxlan_hdr->vlan_id = *((const uint16_t*)item->spec);
                flags |= HINIC3_FLG_VXLAN_VLAN;
                break;
            case RTE_FLOW_ITEM_TYPE_IPV4:
            case RTE_FLOW_ITEM_TYPE_IPV6:
                (void)hinic3_fill_vxlan_ip_header(offload_vxlan_hdr, item);
                flags |= HINIC3_FLG_VXLAN_IP;
                break;
            case RTE_FLOW_ITEM_TYPE_UDP:
                hinic3_offload_fill_vxlan_udp_header(offload_vxlan_hdr, item);
                flags |= HINIC3_FLG_VXLAN_UDP;
                break;
            case RTE_FLOW_ITEM_TYPE_VXLAN:
                vxlan = item->spec;
                /* ovs统一使用上层传入的vxlan.flag值 */
                memcpy(&offload_vxlan_hdr->vxlan, vxlan, sizeof(*vxlan));
                flags |= HINIC3_FLG_VXLAN_VNI;
                break;
            default:
                break;
        }
        item = next_no_end_pattern(vxlan_info->definition, item);
    }
    if (flags == HINIC3_FLG_VXLAN_INTEGRITY_COMM || flags == HINIC3_FLG_VXLAN_INTEGRITY_VLAN) {
        return 0;
    }
    return -1;
}

#define HINIC3_ACT_VXLAN_GPE_HEADER_SIZE (84)
int hinic3_offload_parse_vxlan_act(struct hinic3_offload_action *offload_action,
                                  const struct rte_flow_action_vxlan_encap *vxlan_info)
{
    int ret;
    struct hinic3_flow_act_vxlan_gpe_header *vxlan_hdr = NULL;
    struct hinic3_nlattr *hinic3_actions = &offload_action->act_nla;

    if (vxlan_info == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3offload vxlan action is null");
        return -1;
    }

    if (hinic3_card_mod_get() == PROG_MODE)
        vxlan_hdr = hinic3_nlattr_put_unspec_uninit(hinic3_actions, HINIC3_FLOW_ACT_DPDK_VXL_PUSH,
                                               HINIC3_ACT_VXLAN_GPE_HEADER_SIZE);
    else
        vxlan_hdr = hinic3_nlattr_put_unspec_uninit(hinic3_actions, HINIC3_FLOW_ACT_VXL_GPE_PUSH,
                                               sizeof(struct hinic3_flow_act_vxlan_gpe_header));
    if (vxlan_hdr == NULL) {
        hinic3_add_error_stats(HINIC3_FLOWS_ERROR_FLOW_NO_VXLAN_ACTION_OFFSET, 1);
        return -1;
    }
    offload_action->vxlan_hdr = vxlan_hdr;
    ret = hinic3_offload_fill_vxlan_header(vxlan_hdr, vxlan_info);
    if (ret != 0) {
        return -1;
    }
    offload_action->has_vxlan_push = true;
    return 0;
}

static int hinic3_offload_parse_set_act_standard(struct hinic3_nlattr *hinic3_actions, const struct rte_flow_action *act)
{
    int ret = 0;
    if (act->conf == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3load set action conf is null.");
        return -1;
    }

    switch (act->type) {
        case RTE_FLOW_ACTION_TYPE_SET_MAC_SRC:
            ret = hinic3_nlattr_put_unspec(hinic3_actions, HINIC3_FLOW_ACT_SET_SMAC,
                                          act->conf, sizeof(struct eth_address));
            break;
        case RTE_FLOW_ACTION_TYPE_SET_MAC_DST:
            ret = hinic3_nlattr_put_unspec(hinic3_actions, HINIC3_FLOW_ACT_SET_DMAC,
                                          act->conf, sizeof(struct eth_address));
            break;
        case RTE_FLOW_ACTION_TYPE_SET_IPV4_SRC:
            ret = hinic3_nlattr_put_unspec(hinic3_actions, HINIC3_FLOW_ACT_SET_SIP, act->conf, sizeof(in4_addr_t));
            break;
        case RTE_FLOW_ACTION_TYPE_SET_IPV4_DST:
            ret = hinic3_nlattr_put_unspec(hinic3_actions, HINIC3_FLOW_ACT_SET_DIP, act->conf, sizeof(in4_addr_t));
            break;
        case RTE_FLOW_ACTION_TYPE_SET_IPV6_SRC:
            ret = hinic3_nlattr_put_unspec(hinic3_actions, HINIC3_FLOW_ACT_SET_SIPV6, act->conf, sizeof(in6_addr_t));
            break;
        case RTE_FLOW_ACTION_TYPE_SET_IPV6_DST:
            ret = hinic3_nlattr_put_unspec(hinic3_actions, HINIC3_FLOW_ACT_SET_DIPV6, act->conf, sizeof(in6_addr_t));
            break;
        case RTE_FLOW_ACTION_TYPE_SET_TP_SRC:
            ret = hinic3_nlattr_put_unspec(hinic3_actions, HINIC3_FLOW_ACT_SET_SPORT, act->conf, sizeof(uint16_t));
            break;
        case RTE_FLOW_ACTION_TYPE_SET_TP_DST:
            ret = hinic3_nlattr_put_unspec(hinic3_actions, HINIC3_FLOW_ACT_SET_DPORT, act->conf, sizeof(uint16_t));
            break;
        default:
            break;
    }
    return ret;
}

static int hinic3_offload_parse_set_act_flexda(struct hinic3_nlattr *hinic3_actions, const struct rte_flow_action *act)
{
    int ret = 0;
    if (act->conf == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3load set action conf is null.");
        return -1;
    }

    switch (act->type) {
        case RTE_FLOW_ACTION_TYPE_SET_MAC_SRC:
            ret = hinic3_nlattr_put_unspec(hinic3_actions, HINIC3_FLOW_ACT_DPDK_SET_MAC_SRC,
                                          act->conf, sizeof(struct eth_address));
            break;
        case RTE_FLOW_ACTION_TYPE_SET_MAC_DST:
            ret = hinic3_nlattr_put_unspec(hinic3_actions, HINIC3_FLOW_ACT_DPDK_SET_MAC_DST,
                                          act->conf, sizeof(struct eth_address));
            break;
        case RTE_FLOW_ACTION_TYPE_SET_IPV4_SRC:
            ret = hinic3_nlattr_put_unspec(hinic3_actions, HINIC3_FLOW_ACT_DPDK_SET_IPV4_SRC, act->conf, sizeof(in4_addr_t));
            break;
        case RTE_FLOW_ACTION_TYPE_SET_IPV4_DST:
            ret = hinic3_nlattr_put_unspec(hinic3_actions, HINIC3_FLOW_ACT_DPDK_SET_IPV4_DST, act->conf, sizeof(in4_addr_t));
            break;
        case RTE_FLOW_ACTION_TYPE_SET_IPV6_SRC:
            ret = hinic3_nlattr_put_unspec(hinic3_actions, HINIC3_FLOW_ACT_DPDK_SET_IPV6_SRC, act->conf, sizeof(in6_addr_t));
            break;
        case RTE_FLOW_ACTION_TYPE_SET_IPV6_DST:
            ret = hinic3_nlattr_put_unspec(hinic3_actions, HINIC3_FLOW_ACT_DPDK_SET_IPV6_DST, act->conf, sizeof(in6_addr_t));
            break;
        case RTE_FLOW_ACTION_TYPE_SET_TP_SRC:
            ret = hinic3_nlattr_put_unspec(hinic3_actions, HINIC3_FLOW_ACT_DPDK_SET_TP_SRC, act->conf, sizeof(uint16_t));
            break;
        case RTE_FLOW_ACTION_TYPE_SET_TP_DST:
            ret = hinic3_nlattr_put_unspec(hinic3_actions, HINIC3_FLOW_ACT_DPDK_SET_TP_DST, act->conf, sizeof(uint16_t));
            break;
        default:
            break;
    }
    return ret;
}

static int hinic3_offload_set_tag_act(struct hinic3_nlattr *hinic3_actions, const struct rte_flow_action *act)
{
    struct hinic3_flow_act_block_version block;
    const struct rte_flow_action_set_tag *set_tag = act->conf;

    if (set_tag == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3load set tag action is null.");
        return -1;
    }

    struct hinic3_dp_extend_info *extend_info = hinic3_get_offload_extend_info();
    if (extend_info == NULL || extend_info->hw_offload == NULL) {
        return -1;
    }

    struct hinic3_flow_agent_db *hinic3_db = (struct hinic3_flow_agent_db *)extend_info->hw_offload;

    block.block_id = set_tag->mask;
    block.block_version = set_tag->data;

    if (block.block_id >= hinic3_db->max_block_num) {
        hinic3_add_error_stats(HINIC3_FLOWS_ERROR_FLOW_BLOCK_ID_ERR, 1);
        return -1;
    }

    return hinic3_nlattr_put_unspec(hinic3_actions, HINIC3_FLOW_ACT_BLOCK_VERSION,
                                   &block, sizeof(struct hinic3_flow_act_block_version));
}

static int hinic3_offload_parse_IP_action(const struct rte_flow_action *act, struct hinic3_offload_action * offload_action)
{
    struct hinic3_nlattr * hinic3_actions = &offload_action->act_nla;
    int ret = 0;
    switch (act->type) {
        case RTE_FLOW_ACTION_TYPE_SET_IPV4_SRC:
        case RTE_FLOW_ACTION_TYPE_SET_IPV4_DST:
        case RTE_FLOW_ACTION_TYPE_SET_MAC_SRC:
        case RTE_FLOW_ACTION_TYPE_SET_MAC_DST:
        case RTE_FLOW_ACTION_TYPE_SET_TP_SRC:
        case RTE_FLOW_ACTION_TYPE_SET_TP_DST:
        case RTE_FLOW_ACTION_TYPE_SET_IPV6_SRC:
        case RTE_FLOW_ACTION_TYPE_SET_IPV6_DST:
            if (hinic3_card_mod_get() == PROG_MODE) 
                ret = hinic3_offload_parse_set_act_flexda(hinic3_actions, act);
            else
                ret = hinic3_offload_parse_set_act_standard(hinic3_actions, act);
            break;
        default:
            return -1;
    }
    return ret;
}

static int hinic3_offload_construct_pop_vlan_action(struct hinic3_nlattr *hinic3_actions)
{
    if (hinic3_card_mod_get() == PROG_MODE)
        return hinic3_nlattr_put_flag(hinic3_actions, HINIC3_FLOW_ACT_DPDK_VLAN_POP);
    else
        return hinic3_nlattr_put_flag(hinic3_actions, HINIC3_FLOW_ACT_VLAN_POP);
}

static int hinic3_offload_construct_dec_ttl_action(struct hinic3_nlattr *hinic3_actions)
{
    if (hinic3_card_mod_get() == PROG_MODE)
        return hinic3_nlattr_put_flag(hinic3_actions, HINIC3_FLOW_ACT_DPDK_DEC_TTL);
    else
        return hinic3_nlattr_put_flag(hinic3_actions, HINIC3_FLOW_ACT_DEC_TTL);
}

int hinic3_offload_parse_action_sub(const struct rte_flow_action *act,
                                   struct hinic3_offload_action *offload_action, struct rte_flow* mega_flow)
{
    int ret = 0;
    struct hinic3_nlattr *hinic3_actions = &offload_action->act_nla;
    const struct rte_flow_action_age *flow_age = NULL;
    switch (act->type) {
        case RTE_FLOW_ACTION_TYPE_OF_PUSH_VLAN:
        case RTE_FLOW_ACTION_TYPE_RSS:
        case RTE_FLOW_ACTION_TYPE_JUMP:
            return 0;
        case RTE_FLOW_ACTION_TYPE_OF_POP_VLAN:
            ret = hinic3_offload_construct_pop_vlan_action(hinic3_actions);
            offload_action->has_vlan_pop = true;
            break;
        case RTE_FLOW_ACTION_TYPE_DROP:
            ret = hinic3_nlattr_put_flag(hinic3_actions, HINIC3_FLOW_ACT_DROP);
            break;
        case RTE_FLOW_ACTION_TYPE_DEC_TTL:
            ret = hinic3_offload_construct_dec_ttl_action(hinic3_actions);
            break;
        case RTE_FLOW_ACTION_TYPE_VXLAN_DECAP:
            if (hinic3_card_mod_get() == PROG_MODE)
                ret = hinic3_nlattr_put_flag(hinic3_actions, HINIC3_FLOW_ACT_DPDK_VXL_POP);
            else
                ret = hinic3_nlattr_put_flag(hinic3_actions, HINIC3_FLOW_ACT_VXL_POP);
            break;
        case RTE_FLOW_ACTION_TYPE_COUNT:
            if (hinic3_card_mod_get() == PROG_MODE)
                ret = hinic3_nlattr_put_flag(hinic3_actions, HINIC3_FLOW_ACT_DPDK_COUNT);
            else
                ret = hinic3_nlattr_put_flag(hinic3_actions, HINIC3_FLOW_ACT_COUNT);
            break;
        case RTE_FLOW_ACTION_TYPE_SET_TAG:
            ret = hinic3_offload_set_tag_act(hinic3_actions, act);
            break;
        case RTE_FLOW_ACTION_TYPE_VXLAN_ENCAP:
            ret = hinic3_offload_parse_vxlan_act(offload_action, act->conf);
            break;
        case RTE_FLOW_ACTION_TYPE_AGE:
            flow_age = (const struct rte_flow_action_age *)act->conf;
            if (flow_age == NULL || flow_age->timeout < HINIC3_AGED_TIME_MIN ||
                flow_age->timeout > HINIC3_AGED_TIME_MAX) {
                return -1;
            }
            mega_flow->context = flow_age->context;
            ret = hinic3_nlattr_put_u16(&offload_action->act_nla, HINIC3_FLOW_ACT_AGE, flow_age->timeout);
            break;
        default:
            return hinic3_offload_parse_IP_action(act, offload_action);
    };

    return ret;
}

int hinic3_offload_copy_current_action(const struct hinic3_nlattr *cur_action, struct hinic3_nlattr *final_action)
{
    uint8_t *dst_buf_ptr = NULL;

    if (cur_action->used_len > (final_action->total_len - final_action->used_len)) {
        return PROCESS_ACTION_FAIL;
    }
    dst_buf_ptr = (void *)((char *)final_action->data + final_action->used_len);
    memcpy(dst_buf_ptr, cur_action->data, cur_action->used_len);
    final_action->used_len += cur_action->used_len;
    final_action->nla_itr = (hinic3_nlattr_itr)((uint8_t *)final_action->data + final_action->used_len);
    return 0;
}

bool hinic3_offload_check_actions(struct hinic3_nlattr *hinic3_actions)
{
    int output_cnt = 0;
    int act_type;
    hinic3_nlattr_itr nla_itr = NULL;

    HINIC3_NLATTR_FOR_EACH(nla_itr, hinic3_actions) {
        act_type = hinic3_nlattr_get_itr_type(nla_itr);
        if (act_type == HINIC3_FLOW_ACT_OUTPUT) {
            output_cnt++;
        }
    }

    if (output_cnt > 1) {
        hinic3_add_error_stats(HINIC3_FLOWS_WARNING_MANY_OUTPUT, 1);
        HINIC3_LOG(ERR, FLOW, "Multi-port flow error!");
        return false;
    }
    return true;
}

bool hinic3_offload_flow_actions_check(struct rte_flow_action *action, size_t nums)
{
    int output_cnt = 0;

    for (size_t i = 0; i < nums && action[i].type != RTE_FLOW_ACTION_TYPE_END; ++i) {
        if (action[i].type == RTE_FLOW_ACTION_TYPE_PORT_ID) {
            output_cnt++;
        }
    }

    if (output_cnt > 1) {
        hinic3_add_error_stats(HINIC3_FLOWS_WARNING_OFFLOAD_CHECK_ACTIONS_MANY_OUTPUT, 1);
        HINIC3_LOG(ERR, FLOW, "Multi-port flow error!");
        return false;
    }
    return true;
}
