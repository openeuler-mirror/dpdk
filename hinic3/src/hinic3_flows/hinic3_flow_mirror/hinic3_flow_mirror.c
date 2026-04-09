 /* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Huawei Technologies Co., Ltd
 */

#include "hinic3_flow_mirror.h"
#include "rte_flow.h"
#include "hiovs_api.h"
#include "hinic3_flow_agent.h"
#include "hinic3_parse_agent_config.h"
#include "hinic3_offload_flow_public.h"
#include "hinic3_offload_action.h"
#include "hinic3_meminfo.h"

#define HINIC3_SESSION_CUTOFF_OFFSET 24
#define HINIC3_IPV4_HEADER_LEN sizeof(struct hinic3_mirror_pkt_l3)
#define HINIC3_VXLAN_GPE_HEADER_LEN  8
#define HINIC3_SHIM_HEADER_LEN 12

static int hinic3_parse_mirror_IPv4_item(const struct rte_flow_item *item,
    struct hinic3_mirror_session_info *session_info, const uint8_t flow_type HINIC3_UNUSED)
{
    const struct rte_flow_item_ipv4 *ip = (const struct rte_flow_item_ipv4 *)item->spec;
    if (ip == NULL) {
        hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_SAMPLE_IPV4_ITEM_NULL, 1);
        return -1;
    }
    session_info->key.sip[0] = ip->hdr.src_addr;
    session_info->key.dip[0] = ip->hdr.dst_addr;
    session_info->key.proto = ip->hdr.next_proto_id;
    session_info->ip_type_flag |= HINIC3_FLG_MIRROR_IPV4;
    return 0;
}

static int hinic3_parse_mirror_vlan_item(const struct rte_flow_item *item,
    struct hinic3_mirror_session_info *session_info, const uint8_t flow_type HINIC3_UNUSED)
{
    const struct rte_flow_item_vlan *vlan = (const struct rte_flow_item_vlan *)item->spec;
    if (vlan == NULL) {
        hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_SAMPLE_VLAN_ITEM_NULL, 1);
        return -1;
    }
    session_info->key.tci = vlan->tci;
    session_info->key.inner_type = vlan->inner_type;
    session_info->ip_type_flag |= HINIC3_FLG_MIRROR_VLAN;
    return 0;
}

static int hinic3_parse_mirror_eth_item(const struct rte_flow_item *item,
    struct hinic3_mirror_session_info *session_info, const uint8_t flow_type HINIC3_UNUSED)
{
    const struct rte_flow_item_eth *eth = (const struct rte_flow_item_eth *)item->spec;
    if (eth == NULL) {
        hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_SAMPLE_ETH_ITEM_NULL, 1);
        return -1;
    }
    memcpy(session_info->key.dmac, &eth->dst, ETH_ALEN);
    memcpy(session_info->key.smac, &eth->src, ETH_ALEN);
    session_info->key.type = eth->type;
    session_info->ip_type_flag |= HINIC3_FLG_MIRROR_ETH;
    return 0;
}

static int hinic3_parse_mirror_IPv6_item(const struct rte_flow_item *item,
    struct hinic3_mirror_session_info *session_info, const uint8_t flow_type HINIC3_UNUSED)
{
    const struct rte_flow_item_ipv6 *ip = (const struct rte_flow_item_ipv6 *)item->spec;
    if (ip == NULL) {
        hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_SAMPLE_IPV6_ITEM_NULL, 1);
        return -1;
    }

    for (int i = 0; i < HINIC3_IPV6_ADDR_UINT32_LEN; i++) {
        session_info->key.sip[i] = hinic3_convert_u8_to_u32(&ip->hdr.src_addr[i * HINIC3_IPV6_ADDR_UINT32_LEN],
            HINIC3_IPV6_ADDR_UINT32_LEN);
        session_info->key.dip[i] = hinic3_convert_u8_to_u32(&ip->hdr.dst_addr[i * HINIC3_IPV6_ADDR_UINT32_LEN],
            HINIC3_IPV6_ADDR_UINT32_LEN);
    }

    session_info->key.proto = ip->hdr.proto;
    session_info->ip_type_flag |= HINIC3_FLG_MIRROR_IPV6;
    return 0;
}

static int hinic3_parse_mirror_vxlan_item(const struct rte_flow_item *item,
    struct hinic3_mirror_session_info *session_info, const uint8_t flow_type HINIC3_UNUSED)
{
    const struct rte_flow_item_vxlan *vxlan = (const struct rte_flow_item_vxlan *)item->spec;
    if (vxlan == NULL) {
        hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_SAMPLE_VXLAN_ITEM_NULL, 1);
        return -1;
    }
    session_info->key.tunnel.vxlan.flags = 0;
    session_info->key.tunnel.vxlan.rsvd1 = 0;
    memcpy(session_info->key.tunnel.vxlan.vxlan_rsvd0, vxlan->rsvd0, sizeof(vxlan->rsvd0));
    memcpy(session_info->key.tunnel.vxlan.vni, vxlan->vni, sizeof(vxlan->vni));
    session_info->ip_type_flag |= HINIC3_FLG_MIRROR_VXLAN;

    session_info->type = HINIC3_EMC_VXLAN_SESSION;
    return 0;
}

static int hinic3_parse_mirror_udp_item(const struct rte_flow_item *item,
    struct hinic3_mirror_session_info *session_info, const uint8_t flow_type HINIC3_UNUSED)
{
    const struct rte_flow_item_udp *udp = (const struct rte_flow_item_udp *)item->spec;
    if (udp == NULL) {
        hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_SAMPLE_UDP_ITEM_NULL, 1);
        return -1;
    }

    session_info->key.tunnel.vxlan.dst_port = udp->hdr.dst_port;
    session_info->key.tunnel.vxlan.src_port = udp->hdr.src_port;
    session_info->ip_type_flag |= HINIC3_FLG_MIRROR_UDP;
    return 0;
}

static int hinic3_parse_mirror_gre_item(const struct rte_flow_item *item,
    struct hinic3_mirror_session_info *session_info, const uint8_t flow_type HINIC3_UNUSED)
{
    const struct rte_flow_item_gre *gre = (const struct rte_flow_item_gre *)item->spec;
    if (gre == NULL) {
        hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_SAMPLE_NVGRE_ITEM_NULL, 1);
        return -1;
    }

    session_info->key.tunnel.gre.c_rsvd0_ver = gre->c_rsvd0_ver;
    session_info->key.tunnel.gre.protocol = gre->protocol;
    session_info->ip_type_flag |= HINIC3_FLG_MIRROR_GRE;

    if (flow_type == HINIC3_FLOW_EMC_MIRROR) {
        session_info->type = HINIC3_EMC_GRE_SESSION;
    } else if (flow_type == HINIC3_FLOW_ACL_MIRROR) {
        session_info->type = HINIC3_ACL_GRE_SESSION;
    } else {
        session_info->type = HINIC3_MEGA_GRE_SESSION;
    }
    return 0;
}

static struct hinic3_mirror_vxlan_item_func_map hinic3_mirror_vxlan_item_func_array[] = {
    {RTE_FLOW_ITEM_TYPE_ETH,    hinic3_parse_mirror_eth_item},
    {RTE_FLOW_ITEM_TYPE_VLAN,   hinic3_parse_mirror_vlan_item},
    {RTE_FLOW_ITEM_TYPE_IPV4,   hinic3_parse_mirror_IPv4_item},
    {RTE_FLOW_ITEM_TYPE_IPV6,   hinic3_parse_mirror_IPv6_item},
    {RTE_FLOW_ITEM_TYPE_VXLAN,  hinic3_parse_mirror_vxlan_item},
    {RTE_FLOW_ITEM_TYPE_UDP,    hinic3_parse_mirror_udp_item},
    {RTE_FLOW_ITEM_TYPE_GRE,    hinic3_parse_mirror_gre_item},
};

static bool hinic3_check_session_ip_type_flag(uint32_t ip_type_flag)
{
    switch (ip_type_flag) {
        case HINIC3_FLG_MIRROR_IPV4_VLAN:
        case HINIC3_FLG_MIRROR_IPV6_VLAN:
        case HINIC3_FLG_MIRROR_IPV4_VXLAN:
        case HINIC3_FLG_MIRROR_IPV6_VXLAN:
        case HINIC3_FLG_MIRROR_IPV4_GRE:
        case HINIC3_FLG_MIRROR_IPV6_GRE:
        case HINIC3_FLG_MIRROR_IPV4_VLAN_GRE:
        case HINIC3_FLG_MIRROR_IPV6_VLAN_GRE:
        case HINIC3_FLG_MIRROR_IPV4_VXLAN_GPE_SHIM:
            return true;
        default:
            return false;
    }
}

static int hinic3_parse_mirror_vxlan_nvgre_act(const struct rte_flow_action *act,
    struct hinic3_mirror_session_info *session_info, const uint8_t flow_type)
{
    int ret = 0;
    const struct rte_flow_action_vxlan_encap *encap_info = (const struct rte_flow_action_vxlan_encap *)act->conf;
    if (encap_info == NULL || encap_info->definition == NULL) {
        return -1;
    }
    const struct rte_flow_item *item = NULL;

    item = next_no_end_pattern(encap_info->definition, NULL);
    while (item) {
        for (size_t i = 0; i < HINIC3_MIRROR_VXLAN_ITEM_LEN; i++) {
            if (item->type == hinic3_mirror_vxlan_item_func_array[i].item) {
                ret = hinic3_mirror_vxlan_item_func_array[i].func(item, session_info, flow_type);
                break;
            }
        }
        if (ret != 0) {
            hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_SAMPLE_VXLAN_ENCAP, 1);
            return -1;
        }
        item = next_no_end_pattern(encap_info->definition, item);
    }

    if (hinic3_check_session_ip_type_flag(session_info->ip_type_flag)) {
        return 0;
    }
    return -1;
}

// 解析eth_ip_udp
static int hinic3_parse_mirror_vxlan_gpe_eth_ip_udp_item(struct hinic3_mirror_session_info *session_info, uint8_t *pkt_hdr, uint8_t **end_itr)
{
    int ret = 0;
    uint8_t *itr = pkt_hdr;
    struct hinic3_mirror_pkt_l2 *eth = (struct hinic3_mirror_pkt_l2 *)
        hinic3_malloc(sizeof(struct hinic3_mirror_pkt_l2), HINIC3_FLOWS);
    struct hinic3_mirror_pkt_l3 *ip = (struct hinic3_mirror_pkt_l3 *)
        hinic3_malloc(sizeof(struct hinic3_mirror_pkt_l3), HINIC3_FLOWS);
    struct rte_udp_hdr *udp = (struct rte_udp_hdr *)hinic3_malloc(sizeof(struct rte_udp_hdr), HINIC3_FLOWS);
    if (eth == NULL || ip == NULL || udp == NULL) {
        ret = -1;
        goto end;
    }    
    memcpy(eth, itr, HINIC3_ETH_HEADER_LEN);
    itr += HINIC3_ETH_HEADER_LEN;
    memcpy(ip, itr, HINIC3_IP_HEADER_LEN);
    itr += HINIC3_IP_HEADER_LEN;
    memcpy(udp, itr, HINIC3_UDP_HEADER_LEN);
    itr += HINIC3_UDP_HEADER_LEN;

    memcpy(session_info->key.dmac, &eth->dmac, ETH_ALEN);
    memcpy(session_info->key.smac, &eth->smac, ETH_ALEN); 
    session_info->key.type = eth->eth_type;
    session_info->ip_type_flag |= HINIC3_FLG_MIRROR_ETH;
 
    session_info->key.sip[0] = ip->src_addr;
    session_info->key.dip[0] = ip->dst_addr;
    session_info->key.proto = ip->next_proto_id;
    session_info->ip_type_flag |= HINIC3_FLG_MIRROR_IPV4;

    session_info->key.tunnel.vxlan_gpe_shim.dst_port = udp->src_port;
    session_info->key.tunnel.vxlan_gpe_shim.src_port = udp->dst_port;
    session_info->ip_type_flag |= HINIC3_FLG_MIRROR_UDP;

    *end_itr = itr;
    ret = 0;
end :
    hinic3_free(eth);
    hinic3_free(ip);
    hinic3_free(udp);
    return ret;
}

// 解析VXLAN_GPE
static int hinic3_parse_mirror_vxlan_gpe_shim_item(struct hinic3_mirror_session_info *session_info, uint8_t *pkt_hdr)
{
    int ret = 0;
    uint8_t *itr = pkt_hdr;
    size_t copy_len = 0;
    struct hinic3_mirror_vxlan_gpe *vxlan_gpe = (struct hinic3_mirror_vxlan_gpe *)
        hinic3_malloc(sizeof(struct hinic3_mirror_vxlan_gpe), HINIC3_FLOWS);
    struct hinic3_shim_info *shim = (struct hinic3_shim_info *)hinic3_malloc(sizeof(struct hinic3_shim_info),HINIC3_FLOWS);
    if (shim == NULL || vxlan_gpe == NULL) {
        ret = -1;
        goto end;
    }
    memcpy(vxlan_gpe, itr, HINIC3_VXLAN_GPE_HEADER_LEN);
    itr += HINIC3_VXLAN_GPE_HEADER_LEN;
    memcpy(shim, itr, HINIC3_SHIM_HEADER_LEN);

    session_info->key.tunnel.vxlan_gpe_shim.flags = vxlan_gpe->flags;
    session_info->key.tunnel.vxlan_gpe_shim.rsvd1 = 0;
    session_info->key.tunnel.vxlan_gpe_shim.next_protocol = vxlan_gpe->protocol;
    memcpy(session_info->key.tunnel.vxlan_gpe_shim.vxlan_rsvd0, vxlan_gpe->rsvd0,
        sizeof(vxlan_gpe->rsvd0));
    memcpy(session_info->key.tunnel.vxlan_gpe_shim.vni, vxlan_gpe->vni,
        sizeof(vxlan_gpe->vni));
    session_info->ip_type_flag |= HINIC3_FLG_MIRROR_VXLAN_GPE;
    session_info->key.tunnel.vxlan_gpe_shim.shim_type = shim->type;
    session_info->key.tunnel.vxlan_gpe_shim.shim_len = (shim->len + 1) * 4; // shim_header_total_len
    copy_len = (session_info->key.tunnel.vxlan_gpe_shim.shim_len < sizeof(session_info->key.tunnel.vxlan_gpe_shim.data)) ?
        session_info->key.tunnel.vxlan_gpe_shim.shim_len : sizeof(session_info->key.tunnel.vxlan_gpe_shim.data);
    memcpy(session_info->key.tunnel.vxlan_gpe_shim.data, shim, copy_len);
    session_info->ip_type_flag |= HINIC3_FLG_MIRROR_SHIM;

    session_info->type = HINIC3_EMC_VXLAN_GPE_SHIM;
    ret = 0;
end :
    hinic3_free(vxlan_gpe);
    hinic3_free(shim);
    return ret;
}

// xvlan_gpe_shim_header解析
static int hinic3_parse_mirror_vxlan_gpe_act(const struct rte_flow_action *act,
    struct hinic3_mirror_session_info *session_info, const uint8_t flow_type HINIC3_UNUSED)
{
    int ret = 0;
    const struct rte_flow_action_raw_encap *encap_info  = (const struct rte_flow_action_raw_encap *)act->conf;
    if (encap_info == NULL || encap_info -> data == NULL) {
        return -1;
    }
    uint8_t *itr = encap_info->data;
    uint8_t *end_ptr = NULL;

    session_info->type = HINIC3_EMC_VXLAN_GPE_SHIM;
    ret = hinic3_parse_mirror_vxlan_gpe_eth_ip_udp_item(session_info, itr, &end_ptr);
    if (ret != 0) {
        return -1;
    }
    itr = end_ptr;
    ret = hinic3_parse_mirror_vxlan_gpe_shim_item(session_info, itr);
    if (ret != 0) {
        return -1;
    }
    return 0;
}

static int hinic3_parse_mirror_port_id_act(const struct rte_flow_action *act,
    struct hinic3_mirror_session_info *session_info)
{
    const struct rte_flow_action_port_id *output_port = (const struct rte_flow_action_port_id *)act->conf;
    if (output_port == NULL) {
        hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_SAMPLE_PORT_ID, 1);
        return -1;
    }
    uint32_t output_port_id;
    output_port_id = hinic3_process_port_id(output_port->id);
    session_info->key.outer_port_id = output_port_id;
    return 0;
}

static int hinic3_set_mirror_act(struct hinic3_nlattr *act_nla, const struct hinic3_mirror_session_info *session_info)
{
    struct hiovs_mirror_info *hiovs_info = NULL;

    hiovs_info = hinic3_nlattr_put_unspec_uninit(act_nla, HINIC3_FLOW_ACT_MIRROR,
                                                sizeof(struct hiovs_mirror_info));
    if (hiovs_info == NULL) {
        return -1;
    }
    if (session_info->direction == HINIC3_SESSION_RX) {
        hiovs_info->sessions[0] = session_info->session_id;
        hiovs_info->ws.mirror_rx = 1;
        hiovs_info->ws.mirror_tx = 0;
    } else {
        hiovs_info->sessions[1] = session_info->session_id;
        hiovs_info->ws.mirror_rx = 0;
        hiovs_info->ws.mirror_tx = 1;
    }
    hiovs_info->ws.rsvd = 0;
    hiovs_info->ws.sport = 0;
    hiovs_info->ws.sampling_interval = session_info->sampling_interval;
    return 0;
}

static int hinic3_set_acl_mirror_act(struct hinic3_nlattr *act_nla, uint8_t session_id, uint8_t dir_flag)
{
    uint16_t set_session_id = (uint16_t)session_id;
    if (dir_flag == HINIC3_SESSION_TX) {
        return hinic3_nlattr_put_u16(act_nla, HINIC3_ACL_ACT_MIRROR_TX, set_session_id);
    } else {
        return hinic3_nlattr_put_u16(act_nla, HINIC3_ACL_ACT_MIRROR_RX, set_session_id);
    }
}

static int hinic3_set_mega_mirror_act(struct hinic3_nlattr *act_nla, uint8_t session_id)
{
    uint16_t set_session_id = (uint16_t)session_id;
    return hinic3_nlattr_put_u16(act_nla, HINIC3_FLOW_ACT_MIRROR, set_session_id);
}

static void hinic3_init_session_info(struct hinic3_mirror_session_info *session_info, uint32_t ratio)
{
    session_info->is_used = 1;

    session_info->sampling_interval = ratio;
}

int hinic3_offload_parse_sample_act(const struct rte_flow_action *act, struct hinic3_nlattr *act_nla,
    struct rte_flow *flow, uint8_t mirror_dir_flag)
{
    int ret = 0;
    const struct rte_flow_action_sample *sample_info = (const struct rte_flow_action_sample *)act->conf;
    if (sample_info == NULL || sample_info->actions == NULL) {
        hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_SAMPLE_ACTION, 1);
        return -1;
    }
    struct hinic3_mirror_session_info session_info = {0};
    hinic3_init_session_info(&session_info, sample_info->ratio);
    const struct rte_flow_action *action = next_action(sample_info->actions, NULL);
    while (action && (action->type != RTE_FLOW_ACTION_TYPE_END)) {
        ret = 0;
        switch (action->type) {
            case RTE_FLOW_ACTION_TYPE_PORT_ID:
                ret = hinic3_parse_mirror_port_id_act(action, &session_info);
                break;
            case RTE_FLOW_ACTION_TYPE_VXLAN_ENCAP:
            case RTE_FLOW_ACTION_TYPE_NVGRE_ENCAP:
                ret = hinic3_parse_mirror_vxlan_nvgre_act(action, &session_info, flow->flags.is_sample);
                break;
            case RTE_FLOW_ACTION_TYPE_RAW_ENCAP:
                ret = hinic3_parse_mirror_vxlan_gpe_act(action, &session_info, flow->flags.is_sample);
                break;
            default:
                break;
        }
        if (ret != 0) {
            hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_SAMPLE_ACTION, 1);
            return -1;
        }
        action = next_no_void_action(sample_info->actions, action);
    }

    (void)ovs_mutex_lock_session();
    ret = hinic3_deal_with_session_info(&session_info, flow, mirror_dir_flag);
    (void)ovs_mutex_unlock_session();
    if (ret != 0) {
        return -1;
    }
    if (flow->flags.is_sample == HINIC3_FLOW_EMC_MIRROR) {
        ret = hinic3_set_mirror_act(act_nla, &session_info);
    } else if (flow->flags.is_sample == HINIC3_FLOW_ACL_MIRROR) {
        ret = hinic3_set_acl_mirror_act(act_nla, session_info.session_id, mirror_dir_flag);
    } else {
        ret = hinic3_set_mega_mirror_act(act_nla, session_info.session_id);
    }
    if (ret != 0) {
        hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_SAMPLE_ACTION_SET, 1);
        return -1;
    }
    flow->session_id = session_info.session_id;
    return 0;
}
