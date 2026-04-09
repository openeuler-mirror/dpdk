/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */

#include "hinic3_agent_cmd_format.h"
#include "hinic3_util.h"
#include "hinic3_eth_packets.h"
#include "hinic3_ui_string.h"
#include "hinic3_log.h"
#include "hinic3_util.h"
#include "hinic3_packets_types.h"
#include "hinic3_ds.h"
#include "hinic3_packets.h"
#include "hinic3_string_util.h"
#include "hinic3_parse_agent_config.h"
#include "hinic3_offload_flow_port.h"
#include "hinic3_flow_agent_public.h"
#include "hinic3_agent_flow_cmd_dump.h"
#include "hinic3_flow_agent.h"
#include "hinic3_flexda_flow_public.h"
#include "hinic3_agent_cmd_format_flexda.h"

#define HINIC3_FLOW_QOS_BW 0b01
#define HINIC3_FLOW_QOS_PPS 0b10
#define HINIC3_FLOW_QOS_BW_PPS 0b11

static const char *g_ct_tcp_state_str[] = {
    [HINIC3_TCP_STATE_TYPE_CLOSED] = HINIC3_UI_TCP_STATE_TYPE_CLOSE_STR,
    [HINIC3_TCP_STATE_TYPE_LISTEN] = HINIC3_UI_TCP_STATE_TYPE_LISTEN_STR,
    [HINIC3_TCP_STATE_TYPE_SYN_SENT] = HINIC3_UI_TCP_STATE_TYPE_SYN_SENT_STR,
    [HINIC3_TCP_STATE_TYPE_SYN_RECV] = HINIC3_UI_TCP_STATE_TYPE_SYN_RECV_STR,
    [HINIC3_TCP_STATE_TYPE_ESTABLISHED] = HINIC3_UI_TCP_STATE_TYPE_ESTABLISHED_STR,
    [HINIC3_TCP_STATE_TYPE_CLOSE_WAIT] = HINIC3_UI_TCP_STATE_TYPE_CLOSE_WAIT_STR,
    [HINIC3_TCP_STATE_TYPE_FIN_WAIT_1] = HINIC3_UI_TCP_STATE_TYPE_FIN_WAIT_1_STR,
    [HINIC3_TCP_STATE_TYPE_CLOSING] = HINIC3_UI_TCP_STATE_TYPE_CLOSING_STR,
    [HINIC3_TCP_STATE_TYPE_LAST_ACK] = HINIC3_UI_TCP_STATE_TYPE_LAST_ACK_STR,
    [HINIC3_TCP_STATE_TYPE_FIN_WAIT_2] = HINIC3_UI_TCP_STATE_TYPE_FIN_WAIT_2_STR,
    [HINIC3_TCP_STATE_TYPE_TIME_WAIT] = HINIC3_UI_TCP_STATE_TYPE_TIME_WAIT_STR,
};

void
hinic3_hw_ufid_format_output(const uint64_t *hw_ufid, struct ds *ds)
{
    hinic3_ds_put_format(ds, HINIC3_HW_UFID_FMT, HINIC3_HW_UFID_ARGS(*hw_ufid));
}

static void
hinic3_flow_key_format_output_src_port(const hinic3_nlattr_itr nla, uint8_t nw_protocol, struct ds *ds)
{
    uint16_t u16_val = hinic3_nlattr_get_itr_u16(nla);
    uint8_t icmp_type =  (uint8_t)((u16_val & HINIC3_MID_EIGHT_BIT_MASK) >> HINIC3_BIT_MID_MOVE_INDEX);
    uint8_t icmp_code =  (uint8_t)(u16_val & HINIC3_LOW_EIGHT_BIT_MASK);
    if (nw_protocol == IPPROTO_ICMP) {
        hinic3_ds_put_format(ds, "%s(%hu), ", HINIC3_UI_KEY_ICMP_TYPE_STR, icmp_type);
        hinic3_ds_put_format(ds, "%s(%hu), ", HINIC3_UI_KEY_ICMP_CODE_STR, icmp_code);
    } else if (nw_protocol == IPPROTO_ICMPV6) {
        hinic3_ds_put_format(ds, "%s(%hu), ", HINIC3_UI_KEY_ICMP6_TYPE_STR, icmp_type);
        hinic3_ds_put_format(ds, "%s(%hu), ", HINIC3_UI_KEY_ICMP6_CODE_STR, icmp_code);
    } else {
        hinic3_ds_put_format(ds, "%s(%hu), ", HINIC3_UI_KEY_SRC_PORT_STR, ntohs(u16_val));
    }
}

static void
hinic3_flow_key_format_output_dst_port(const hinic3_nlattr_itr nla, uint8_t nw_protocol, struct ds *ds)
{
    uint16_t u16_val = hinic3_nlattr_get_itr_u16(nla);
    if (nw_protocol == IPPROTO_ICMP) {
        hinic3_ds_put_format(ds, "%s(%hu), ", HINIC3_UI_KEY_ICMP_ID_STR, ntohs(u16_val));
    } else if (nw_protocol == IPPROTO_ICMPV6) {
        hinic3_ds_put_format(ds, "%s(%hu), ", HINIC3_UI_KEY_ICMP6_ID_STR, ntohs(u16_val));
    } else {
        hinic3_ds_put_format(ds, "%s(%hu), ", HINIC3_UI_KEY_DST_PORT_STR, ntohs(u16_val));
    }
}

static void
hinic3_flow_process_vni(const hinic3_nlattr_itr nla, struct ds *ds)
{
    uint32_t u32_val = hinic3_nlattr_get_itr_u32(nla);
    hinic3_ds_put_format(ds, "%s(%u), ", HINIC3_UI_KEY_VNI_STR, ntohl(u32_val));
}

static void hinic3_flow_process_dl_type(const hinic3_nlattr_itr nla, struct ds *ds)
{
    uint16_t u16_val = hinic3_nlattr_get_itr_u16(nla);
    hinic3_ds_put_format(ds, "%s(%04x), ", HINIC3_UI_KEY_DL_TYPE_STR, ntohs(u16_val));
}

static void
hinic3_flow_process_ip(const hinic3_nlattr_itr nla, int type, struct ds *ds)
{
    const char *ip_string = (type == HINIC3_FLOW_KEY_SRC_IP) ? HINIC3_UI_KEY_SRC_IP_STR : HINIC3_UI_KEY_DST_IP_STR;
    uint32_t u32_val;
    u32_val = hinic3_nlattr_get_itr_u32(nla);
    hinic3_ds_put_format(ds, "%s(" IP_FMT "), ", ip_string, IP_ARGS(u32_val));
}

static void
hinic3_flow_process_ipv6(const hinic3_nlattr_itr nla, int type, struct ds *ds)
{
    const char *ip_string = (type == HINIC3_FLOW_KEY_SRC_IPV6) ? HINIC3_UI_KEY_SRC_IP_STR : HINIC3_UI_KEY_DST_IP_STR;
    const struct in6_addr *ipv6_addr = (const struct in6_addr *)hinic3_nlattr_get_itr_data(nla);
    hinic3_ds_put_format(ds, "%s(", ip_string);
    hinic3_ipv6_format_addr(ipv6_addr, ds);
    hinic3_ds_put_format(ds, "), ");
}

static void
hinic3_flow_process_vlan_vid(const hinic3_nlattr_itr nla, int type, struct ds *ds)
{
    const char *vid_string =
        (type == HINIC3_FLOW_KEY_INNER_VID) ? HINIC3_UI_KEY_INNER_VLAN_STR : HINIC3_UI_KEY_OUTER_VLAN_STR;
    uint16_t u16_val = hinic3_nlattr_get_itr_u16(nla);
    hinic3_ds_put_format(ds, "%s(%hu), ", vid_string, ntohs(u16_val));
}

static void
hinic3_flow_process_vport(const hinic3_nlattr_itr nla, struct ds *ds)
{
    if (hinic3_forward_mode_get() == OVS_KEY_EXTRACT_EXTEND_MODE_7TUPLE) {
        return;
    }
    uint16_t port_id = 0;
    uint16_t ifindex = hinic3_nlattr_get_itr_u16(nla);
    int ret = hinic3_get_port_id_by_ifindex(ntohs(ifindex), &port_id);
    if (ret != 0) {
        HINIC3_LOG(ERR, FLOW, "get port_id failed, ifindex(0x%x)", ifindex);
        hinic3_ds_put_format(ds, "%s(%s), ", HINIC3_UI_KEY_VPORT_STR, "error");
        return;
    }
    hinic3_ds_put_format(ds, "%s(%hu), %s(%hu), ", HINIC3_UI_KEY_VPORT_STR, port_id,
        HINIC3_UI_KEY_IFINDEX_STR, ntohs(ifindex));
}

static void
hinic3_flow_process_mac(const hinic3_nlattr_itr nla, int type, struct ds *ds)
{
    const char *mac_string = (type == HINIC3_FLOW_KEY_SRC_MAC) ? HINIC3_UI_KEY_SRC_MAC_STR : HINIC3_UI_KEY_DST_MAC_STR;
    const uint8_t *mac = (const uint8_t *)hinic3_nlattr_get_itr_unspec(nla, ETH_ALEN);
    hinic3_ds_put_format(ds, "%s(" HINIC3_MAC_FMT "), ", mac_string, HINIC3_OUTPUT_MAC(mac));
}

static uint8_t
hinic3_flow_process_nw_protocol(const hinic3_nlattr_itr nla, struct ds *ds)
{
    uint8_t nw_protocol = hinic3_nlattr_get_itr_u8(nla);
    hinic3_ds_put_format(ds, "%s(%hhu), ", HINIC3_UI_KEY_NW_PROTO_STR, nw_protocol);
    return nw_protocol;
}

void hinic3_flow_key_format_output(const struct hinic3_nlattr *key, struct ds *ds)
{
    hinic3_nlattr_itr nla = NULL;
    uint8_t nw_protocol = 0;
    HINIC3_NLATTR_FOR_EACH(nla, key)
    {
        int type = hinic3_nlattr_get_itr_type(nla);
        switch (type) {
            case HINIC3_FLOW_KEY_VNI:
                hinic3_flow_process_vni(nla, ds);
                break;
            case HINIC3_FLOW_KEY_DL_TYPE:
                hinic3_flow_process_dl_type(nla, ds);
                break;
            case HINIC3_FLOW_KEY_PROTOCOL:
                nw_protocol = hinic3_flow_process_nw_protocol(nla, ds);
                break;
            case HINIC3_FLOW_KEY_SRC_IP:
            case HINIC3_FLOW_KEY_DST_IP:
                hinic3_flow_process_ip(nla, type, ds);
                break;
            case HINIC3_FLOW_KEY_SRC_IPV6:
            case HINIC3_FLOW_KEY_DST_IPV6:
                hinic3_flow_process_ipv6(nla, type, ds);
                break;
            case HINIC3_FLOW_KEY_SRC_PORT:
                hinic3_flow_key_format_output_src_port(nla, nw_protocol, ds);
                break;
            case HINIC3_FLOW_KEY_DST_PORT:
                hinic3_flow_key_format_output_dst_port(nla, nw_protocol, ds);
                break;
            case HINIC3_FLOW_KEY_OUTER_VID:
            case HINIC3_FLOW_KEY_OUTER_TCI:
            case HINIC3_FLOW_KEY_INNER_VID:
            case HINIC3_FLOW_KEY_INNER_TCI:
                hinic3_flow_process_vlan_vid(nla, type, ds);
                break;
            case HINIC3_FLOW_KEY_IN_PORT:
                hinic3_flow_process_vport(nla, ds);
                break;
            case HINIC3_FLOW_KEY_SRC_MAC:
            case HINIC3_FLOW_KEY_DST_MAC:
                hinic3_flow_process_mac(nla, type, ds);
                break;
            default:
                break;
        }
    }
}

static void
hinic3_ct_tcp_state_format_output(struct ds *ds, uint8_t ct_tcp_state)
{
    const char *ct_tcp_state_name = NULL;
    if (ct_tcp_state <= HINIC3_TCP_STATE_TYPE_TIME_WAIT) {
        ct_tcp_state_name = g_ct_tcp_state_str[ct_tcp_state];
    } else {
        ct_tcp_state_name = HINIC3_UI_TCP_STATE_TYPE_UNKNOWN_STR;
    }

    hinic3_ds_put_cstr(ds, ct_tcp_state_name);
}

static void
hinic3_flow_process_ct_action(const hinic3_nlattr_itr nla, struct ds *ds)
{
    struct ds ds_state = DS_EMPTY_INITIALIZER;
    const struct hinic3_ct_tcp_state *ct_tcp_state =
        (const struct hinic3_ct_tcp_state *)hinic3_nlattr_get_itr_unspec(nla, sizeof(struct hinic3_ct_tcp_state));
    hinic3_ct_tcp_state_format_output(&ds_state, ct_tcp_state->state);

    hinic3_ds_put_format(ds, "%s(%s=%u, %s=%u, %s=%u, %s=%u, %s=%s), ", HINIC3_UI_KEY_CT_STR, HINIC3_UI_KEY_CT_SEQ_LOW_STR,
        ntohl(ct_tcp_state->seqlo), HINIC3_UI_KEY_CT_SEQ_HIGH_STR, ntohl(ct_tcp_state->seqhi),
        HINIC3_UI_KEY_CT_MAX_WIN_STR, ntohs(ct_tcp_state->max_win), HINIC3_UI_KEY_CT_SACLE_STR, ct_tcp_state->wscale,
        HINIC3_UI_KEY_CT_STATE_STR, hinic3_ds_cstr(&ds_state));
    hinic3_ds_destroy(&ds_state);
}

static void hinic3_flow_process_count_action(const hinic3_nlattr_itr nla HINIC3_UNUSED, struct ds *ds)
{
   hinic3_ds_put_format(ds, "%s, ", HINIC3_UI_COUNT_ACTION);
}

static void
hinic3_flow_process_vxlan_push_action(const hinic3_nlattr_itr nla, struct ds *ds)
{
    size_t size;
    uint32_t vni;
    const struct hinic3_flow_act_vxlan_gpe_header *vxlan_header = NULL;

    size = sizeof(struct hinic3_flow_act_vxlan_gpe_header);
    vxlan_header = (const struct hinic3_flow_act_vxlan_gpe_header *)hinic3_nlattr_get_itr_unspec(nla, size);
    vni = ntohl(vxlan_header->vxlan.vni) >> HINIC3_BYTE_BITS;

    if (vxlan_header->ip_version == HINIC3_IP_ADDR_V4) {
        hinic3_ds_put_format(ds,
            "%s(%s=" HINIC3_MAC_FMT ", %s=" HINIC3_MAC_FMT ", %s=" IP_FMT ", %s=" IP_FMT ", %s=%hu, %s=%hu, %s=%u, %s=%u), ",
            HINIC3_UI_KEY_VXLAN_PUSH_STR, HINIC3_UI_KEY_DST_MAC_STR, HINIC3_OUTPUT_MAC(vxlan_header->dmac),
            HINIC3_UI_KEY_SRC_MAC_STR, HINIC3_OUTPUT_MAC(vxlan_header->smac), HINIC3_UI_KEY_SRC_IP_STR,
            IP_ARGS(vxlan_header->sip[0]), HINIC3_UI_KEY_DST_IP_STR, IP_ARGS(vxlan_header->dip[0]),
            HINIC3_UI_KEY_SRC_PORT_STR, ntohs(vxlan_header->sport), HINIC3_UI_KEY_VID_STR,
            hinic3_vlan_tci_to_vid(vxlan_header->vlan_id), HINIC3_UI_KEY_VNI_STR, vni, HINIC3_UI_KEY_DSCP_STR,
            vxlan_header->dscp);
    } else if (vxlan_header->ip_version == HINIC3_IP_ADDR_V6) {
        hinic3_ds_put_format(ds,
            "%s(%s=" HINIC3_MAC_FMT ", %s=" HINIC3_MAC_FMT ", %s=" IPV6_FMT ", %s=" IPV6_FMT ", %s=%hu, %s=%hu, %s=%u), ",
            HINIC3_UI_KEY_VXLAN_PUSH_STR, HINIC3_UI_KEY_DST_MAC_STR, HINIC3_OUTPUT_MAC(vxlan_header->dmac),
            HINIC3_UI_KEY_SRC_MAC_STR, HINIC3_OUTPUT_MAC(vxlan_header->smac), HINIC3_UI_KEY_SRC_IP_STR,
            IPV6_ARGS(vxlan_header->sip), HINIC3_UI_KEY_DST_IP_STR, IPV6_ARGS(vxlan_header->dip),
            HINIC3_UI_KEY_SRC_PORT_STR, ntohs(vxlan_header->sport), HINIC3_UI_KEY_VID_STR,
            hinic3_vlan_tci_to_vid(vxlan_header->vlan_id), HINIC3_UI_KEY_VNI_STR, vni);
    }
}

static void
hinic3_flow_process_vxlan_pop_action(const hinic3_nlattr_itr nla, struct ds *ds)
{
    bool nal_flag = hinic3_nlattr_get_itr_flag(nla);
    hinic3_ds_put_format(ds, "%s(%s), ", HINIC3_UI_KEY_VXLAN_POP_STR,
        nal_flag ? HINIC3_UI_KEY_TURE_STR : HINIC3_UI_KEY_FALSE_STR);
}

static void
hinic3_flow_process_vlan_push_action(const hinic3_nlattr_itr nla, struct ds *ds)
{
    uint16_t u16_val = hinic3_nlattr_get_itr_u16(nla);
    uint16_t vlan = ntohs(u16_val);
    hinic3_ds_put_format(ds, "%s(%s=%hu,%s=%hu), ", HINIC3_UI_KEY_VLAN_PUSH_STR, HINIC3_UI_KEY_VID_STR,
        vlan & VLAN_VID_MASK, HINIC3_UI_KEY_VLAN_PCP_STR, (vlan & VLAN_PCP_MASK) >> VLAN_PCP_SHIFT);
}

static void
hinic3_flow_process_vlan_pop_action(const hinic3_nlattr_itr nla, struct ds *ds)
{
    bool nal_flag = hinic3_nlattr_get_itr_flag(nla);
    hinic3_ds_put_format(ds, "%s(%s), ", HINIC3_UI_KEY_VLAN_POP_STR,
        nal_flag ? HINIC3_UI_KEY_TURE_STR : HINIC3_UI_KEY_FALSE_STR);
}

static void
hinic3_flow_process_set_mac_action(const hinic3_nlattr_itr nla, struct ds *ds)
{
    enum hinic3_flow_action_type type = (enum hinic3_flow_action_type)hinic3_nlattr_get_itr_type(nla);
    const char *mac_string =
        (type == HINIC3_FLOW_ACT_SET_SMAC) ? HINIC3_UI_KEY_VLAN_SET_SMAC_STR : HINIC3_UI_KEY_VLAN_SET_DMAC_STR;
    const uint8_t *sd_mac = (const uint8_t *)hinic3_nlattr_get_itr_unspec(nla, ETH_ALEN);
    hinic3_ds_put_format(ds, "%s(" HINIC3_MAC_FMT "), ", mac_string, HINIC3_OUTPUT_MAC(sd_mac));
}

static void
hinic3_flow_process_set_ip_action(const hinic3_nlattr_itr nla, struct ds *ds)
{
    enum hinic3_flow_action_type type = (enum hinic3_flow_action_type)hinic3_nlattr_get_itr_type(nla);
    const char *ip_string =
        (type == HINIC3_FLOW_ACT_SET_SIP) ? HINIC3_UI_KEY_VLAN_SET_SIP_STR : HINIC3_UI_KEY_VLAN_SET_DIP_STR;
    uint32_t u32_val;
    u32_val = hinic3_nlattr_get_itr_u32(nla);
    hinic3_ds_put_format(ds, "%s(" IP_FMT "), ", ip_string, IP_ARGS(u32_val));
}

static void
hinic3_flow_process_set_ipv6_action(const hinic3_nlattr_itr nla, struct ds *ds)
{
    enum hinic3_flow_action_type type = (enum hinic3_flow_action_type)hinic3_nlattr_get_itr_type(nla);
    const char *ip_string =
        (type == HINIC3_FLOW_ACT_SET_SIPV6) ? HINIC3_UI_KEY_VLAN_SET_SIP_STR : HINIC3_UI_KEY_VLAN_SET_DIP_STR;
    const struct in6_addr *ipv6_addr = (const struct in6_addr *)hinic3_nlattr_get_itr_data(nla);
    hinic3_ds_put_format(ds, "%s(", ip_string);
    hinic3_ipv6_format_addr(ipv6_addr, ds);
    hinic3_ds_put_format(ds, "), ");
}

static void
hinic3_flow_process_set_u16_action(const hinic3_nlattr_itr nla, struct ds *ds)
{
    enum hinic3_flow_action_type type = (enum hinic3_flow_action_type)hinic3_nlattr_get_itr_type(nla);
    const char * print_string = NULL;
    uint16_t u16_val = hinic3_nlattr_get_itr_u16(nla);
    switch (type) {
        case HINIC3_FLOW_ACT_SET_SPORT:
            print_string = HINIC3_UI_KEY_VLAN_SET_SPORT_STR;
            break;
        case HINIC3_FLOW_ACT_SET_DPORT:
            print_string = HINIC3_UI_KEY_VLAN_SET_DPORT_STR;
            break;
        case HINIC3_FLOW_ACT_OUTPUT:
            print_string = HINIC3_UI_KEY_VLAN_OUTPUT_STR;
            break;
        default:
            return;
    }
    hinic3_ds_put_format(ds, "%s(%hu), ", print_string, ntohs(u16_val));
}

static void hinic3_flexda_flow_process_set_mac_action(const hinic3_nlattr_itr nla, struct ds *ds)
{
    enum hinic3_flow_action_type type = (enum hinic3_flow_action_type)hinic3_nlattr_get_itr_type(nla);
    const char *mac_string =
    (type == HINIC3_FLOW_ACT_DPDK_SET_MAC_SRC) ? HINIC3_UI_KEY_VLAN_SET_SMAC_STR : HINIC3_UI_KEY_VLAN_SET_DMAC_STR;
    const uint8_t *sd_mac = (const uint8_t *)hinic3_nlattr_get_itr_unspec(nla, ETH_ALEN);
    hinic3_ds_put_format(ds, "%s(" HINIC3_MAC_FMT "), ", mac_string, HINIC3_OUTPUT_MAC(sd_mac));
}
    
static void hinic3_flexda_flow_process_set_ip_action(const hinic3_nlattr_itr nla, struct ds *ds)
{
    enum hinic3_flow_action_type type = (enum hinic3_flow_action_type)hinic3_nlattr_get_itr_type(nla);
    const char *ip_string =
    (type == HINIC3_FLOW_ACT_DPDK_SET_IPV4_SRC) ? HINIC3_UI_KEY_VLAN_SET_SIP_STR : HINIC3_UI_KEY_VLAN_SET_DIP_STR;
    uint32_t u32_val;
    u32_val = hinic3_nlattr_get_itr_u32(nla);
    hinic3_ds_put_format(ds, "%s(" IP_FMT "), ", ip_string, IP_ARGS(u32_val));
}
        
static void hinic3_flexda_flow_process_set_ipv6_action(const hinic3_nlattr_itr nla, struct ds *ds)
{
    enum hinic3_flow_action_type type = (enum hinic3_flow_action_type)hinic3_nlattr_get_itr_type(nla);
    const char *ip_string =
    (type == HINIC3_FLOW_ACT_DPDK_SET_IPV6_SRC) ? HINIC3_UI_KEY_VLAN_SET_SIP_STR : HINIC3_UI_KEY_VLAN_SET_DIP_STR;
    const struct in6_addr *ipv6_addr = (const struct in6_addr *)hinic3_nlattr_get_itr_data(nla);
    hinic3_ds_put_format(ds, "%s(", ip_string);
    hinic3_ipv6_format_addr(ipv6_addr, ds);
    hinic3_ds_put_format(ds, "), ");
}

static void hinic3_flexda_flow_process_set_u16_action(const hinic3_nlattr_itr nla, struct ds *ds)
{
    enum hinic3_flow_action_type type = (enum hinic3_flow_action_type)hinic3_nlattr_get_itr_type(nla);
    const char *print_string = NULL;
    uint16_t u16_val = hinic3_nlattr_get_itr_u16(nla);
    switch (type) {
        case HINIC3_FLOW_ACT_DPDK_SET_TP_SRC:
            print_string = HINIC3_UI_KEY_VLAN_SET_SPORT_STR;
            break;
        case HINIC3_FLOW_ACT_DPDK_SET_TP_DST:
            print_string = HINIC3_UI_KEY_VLAN_SET_DPORT_STR;
            break;
        case HINIC3_FLOW_ACT_DPDK_PORT_ID:
            print_string = HINIC3_UI_KEY_VLAN_OUTPUT_STR;
            break;
        default:
            return;
    }
    hinic3_ds_put_format(ds, "%s(%hu), ", print_string, ntohs(u16_val));
}

static void
hinic3_flow_process_drop_action(const hinic3_nlattr_itr nla HINIC3_UNUSED, struct ds *ds)
{
    hinic3_ds_put_format(ds, "%s, ", HINIC3_UI_KEY_DROP);
}

static void
hinic3_flow_process_ttl_dec_action(const hinic3_nlattr_itr nla HINIC3_UNUSED, struct ds *ds)
{
    hinic3_ds_put_format(ds, "%s, ", HINIC3_UI_KET_TTL_DEC);
}

static void
hinic3_flow_process_show_set_priority_action(const hinic3_nlattr_itr nla, struct ds *ds)
{
    hinic3_ds_put_format(ds, "%s(%u), ", HINIC3_UI_KEY_VLAN_SET_PRIORITY_STR, hinic3_nlattr_get_itr_u32(nla));
}

static void
hinic3_flow_process_mirror_action(const hinic3_nlattr_itr nla, struct ds *ds)
{
    const struct hiovs_mirror_info *mirror_info = (const struct hiovs_mirror_info *)hinic3_nlattr_get_itr_data(nla);
    hinic3_ds_put_format(ds, "%s", HINIC3_UI_KEY_VLAN_MIRROR_STR);
    if (mirror_info->ws.mirror_rx == 1) {
        hinic3_ds_put_format(ds, "(session_id:%u, sport:%u, ratio:%u), ", mirror_info->sessions[0],
            mirror_info->ws.sport, mirror_info->ws.sampling_interval);
    } else {
        hinic3_ds_put_format(ds, "(session_id:%u, sport:%u, ratio:%u), ", mirror_info->sessions[1],
            mirror_info->ws.sport, mirror_info->ws.sampling_interval);
    }
}

static void
hinic3_flow_process_dp_hash_action(const hinic3_nlattr_itr nla, struct ds *ds)
{
    hinic3_ds_put_format(ds, "%s(%u), ", HINIC3_UI_KEY_DP_HASH_STR, hinic3_nlattr_get_itr_u32(nla));
}

static void
hinic3_flow_process_recirc_id_action(const hinic3_nlattr_itr nla, struct ds *ds)
{
    hinic3_ds_put_format(ds, "%s(%u), ", HINIC3_UI_KEY_RECIRC_ID_STR, hinic3_nlattr_get_itr_u32(nla));
}

static void hinic3_flow_process_block_version_action(const hinic3_nlattr_itr nla, struct ds *ds)
{
    const struct hinic3_flow_act_block_version *block = (const struct hinic3_flow_act_block_version *)hinic3_nlattr_get_itr_data(nla);
    hinic3_ds_put_format(ds, "%s(%u), ", HINIC3_UI_ACTION_BLOCK_ID_STR, block->block_id);
    hinic3_ds_put_format(ds, "%s(%u), ", HINIC3_UI_ACTION_BLOCK_VERSION_STR, block->block_version);
}

static void hinic3_flow_process_age_action(const hinic3_nlattr_itr nla, struct ds *ds)
{
    const struct rte_flow_action_age *age_info = (const struct rte_flow_action_age *)hinic3_nlattr_get_itr_data(nla);
    hinic3_ds_put_format(ds, HINIC3_UI_KEY_AGE_STR, age_info->timeout);
}

static void hinic3_flow_process_qos_id_action(const hinic3_nlattr_itr nla, struct ds *ds)
{
    const struct hovs_qos_action *qos_info =
        (const struct hovs_qos_action *)hinic3_nlattr_get_itr_data(nla);
    hinic3_ds_put_format(ds, "%s", HINIC3_UI_KEY_QOS_STR);

    HINIC3_LOG(ERR, FLOW, "qos_id: %u, qos_type: %u", qos_info->qos_id, qos_info->qos_type);
    switch (qos_info->qos_type)
    {
    case HINIC3_FLOW_QOS_BW:
        hinic3_ds_put_format(ds, MULTI_QOS_ACTION_OUTPUT, qos_info->qos_id, "bw");
        break;
    case HINIC3_FLOW_QOS_PPS:
        hinic3_ds_put_format(ds, MULTI_QOS_ACTION_OUTPUT, qos_info->qos_id, "pps");
        break;
    case HINIC3_FLOW_QOS_BW_PPS:
        hinic3_ds_put_format(ds, MULTI_QOS_ACTION_OUTPUT, qos_info->qos_id, "bw,pps");
        break;
    default:
        hinic3_ds_put_format(ds, MULTI_QOS_ACTION_OUTPUT, qos_info->qos_id, "error");
        return;
    }
}

struct hinic3_actions_format_output_func_map hinic3_actions_format_output_func_array[] = {
    {HINIC3_FLOW_ACT_CT, hinic3_flow_process_ct_action},
    {HINIC3_FLOW_ACT_VXL_PUSH, NULL},
    {HINIC3_FLOW_ACT_VXL_POP, hinic3_flow_process_vxlan_pop_action},
    {HINIC3_FLOW_ACT_VLAN_PUSH, hinic3_flow_process_vlan_push_action},
    {HINIC3_FLOW_ACT_VLAN_POP, hinic3_flow_process_vlan_pop_action},
    {HINIC3_FLOW_ACT_SET_SMAC, hinic3_flow_process_set_mac_action},
    {HINIC3_FLOW_ACT_SET_DMAC, hinic3_flow_process_set_mac_action},
    {HINIC3_FLOW_ACT_SET_SIP, hinic3_flow_process_set_ip_action},
    {HINIC3_FLOW_ACT_SET_DIP, hinic3_flow_process_set_ip_action},
    {HINIC3_FLOW_ACT_SET_SPORT, hinic3_flow_process_set_u16_action},
    {HINIC3_FLOW_ACT_SET_DPORT, hinic3_flow_process_set_u16_action},
    {HINIC3_FLOW_ACT_SET_PRIORITY, hinic3_flow_process_show_set_priority_action},
    {HINIC3_FLOW_ACT_OUTPUT, hinic3_flow_process_set_u16_action},
    {HINIC3_FLOW_ACT_VXL_GPE_PUSH, hinic3_flow_process_vxlan_push_action},
    {HINIC3_FLOW_ACT_SET_SIPV6, hinic3_flow_process_set_ipv6_action},
    {HINIC3_FLOW_ACT_SET_DIPV6, hinic3_flow_process_set_ipv6_action},
    {HINIC3_FLOW_ACT_SET_VXLAN_GROUP_ID, NULL},
    {HINIC3_FLOW_ACT_ECMP, NULL},
    {HINIC3_FLOW_ACT_DROP, hinic3_flow_process_drop_action},
    {HINIC3_FLOW_ACT_MIRROR, hinic3_flow_process_mirror_action},
    {HINIC3_FLOW_ACT_CRYPTO, NULL},
    {HINIC3_FLOW_ACT_COUNT, NULL},
    {HINIC3_FLOW_ACT_DEC_TTL, hinic3_flow_process_ttl_dec_action},
    {HINIC3_FLOW_ACT_UPCALL, NULL},
    {HINIC3_FLOW_ACT_DP_HASH, hinic3_flow_process_dp_hash_action},
    {HINIC3_FLOW_ACT_RECIRC_ID, hinic3_flow_process_recirc_id_action},
    {HINIC3_FLOW_ACT_BLOCK_VERSION, hinic3_flow_process_block_version_action},
    {HINIC3_FLOW_ACT_QOS, hinic3_flow_process_qos_id_action},
    {HINIC3_FLOW_ACT_AGE, hinic3_flow_process_age_action},
    {HINIC3_FLOW_ACT_TYPE_MAX, NULL},
};

struct hinic3_actions_format_output_func_map hinic3_flexda_actions_format_output_func_array[] = {
    [HINIC3_FLOW_ACT_CT] = {HINIC3_FLOW_ACT_CT, hinic3_flow_process_ct_action},
    [HINIC3_FLOW_ACT_DPDK_COUNT] = {HINIC3_FLOW_ACT_DPDK_COUNT, hinic3_flow_process_count_action},
    [HINIC3_FLOW_ACT_VXL_PUSH] = {HINIC3_FLOW_ACT_VXL_PUSH, NULL},
    [HINIC3_FLOW_ACT_VXL_POP] = {HINIC3_FLOW_ACT_VXL_POP, hinic3_flow_process_vxlan_pop_action},
    [HINIC3_FLOW_ACT_VLAN_PUSH] = {HINIC3_FLOW_ACT_VLAN_PUSH, hinic3_flow_process_vlan_push_action},
    [HINIC3_FLOW_ACT_VLAN_POP] = {HINIC3_FLOW_ACT_VLAN_POP, hinic3_flow_process_vlan_pop_action},
    [HINIC3_FLOW_ACT_DPDK_VLAN_POP] = {HINIC3_FLOW_ACT_DPDK_VLAN_POP, hinic3_flow_process_vlan_pop_action},
    [HINIC3_FLOW_ACT_DPDK_VLAN_PUSH] = {HINIC3_FLOW_ACT_DPDK_VLAN_PUSH, hinic3_flow_process_vlan_push_action},
    [HINIC3_FLOW_ACT_DPDK_SET_MAC_SRC] = {HINIC3_FLOW_ACT_DPDK_SET_MAC_SRC, hinic3_flexda_flow_process_set_mac_action},
    [HINIC3_FLOW_ACT_DPDK_SET_MAC_DST] = {HINIC3_FLOW_ACT_DPDK_SET_MAC_DST, hinic3_flexda_flow_process_set_mac_action},
    [HINIC3_FLOW_ACT_DPDK_SET_IPV4_SRC] = {HINIC3_FLOW_ACT_DPDK_SET_IPV4_SRC, hinic3_flexda_flow_process_set_ip_action},
    [HINIC3_FLOW_ACT_DPDK_SET_IPV4_DST] = {HINIC3_FLOW_ACT_DPDK_SET_IPV4_DST, hinic3_flexda_flow_process_set_ip_action},
    [HINIC3_FLOW_ACT_DPDK_SET_TP_SRC] = {HINIC3_FLOW_ACT_DPDK_SET_TP_SRC, hinic3_flexda_flow_process_set_u16_action},
    [HINIC3_FLOW_ACT_DPDK_SET_TP_DST] = {HINIC3_FLOW_ACT_DPDK_SET_TP_DST, hinic3_flexda_flow_process_set_u16_action},
    [HINIC3_FLOW_ACT_SET_SMAC] = {HINIC3_FLOW_ACT_SET_SMAC, hinic3_flow_process_set_mac_action},
    [HINIC3_FLOW_ACT_SET_DMAC] = {HINIC3_FLOW_ACT_SET_DMAC, hinic3_flow_process_set_mac_action},
    [HINIC3_FLOW_ACT_SET_SIP] = {HINIC3_FLOW_ACT_SET_SIP, hinic3_flow_process_set_ip_action},
    [HINIC3_FLOW_ACT_SET_DIP] = {HINIC3_FLOW_ACT_SET_DIP, hinic3_flow_process_set_ip_action},
    [HINIC3_FLOW_ACT_SET_SPORT] = {HINIC3_FLOW_ACT_SET_SPORT, hinic3_flow_process_set_u16_action},
    [HINIC3_FLOW_ACT_SET_DPORT] = {HINIC3_FLOW_ACT_SET_DPORT, hinic3_flow_process_set_u16_action},
    [HINIC3_FLOW_ACT_SET_PRIORITY] = {HINIC3_FLOW_ACT_SET_PRIORITY, hinic3_flow_process_show_set_priority_action},
    [HINIC3_FLOW_ACT_OUTPUT] = {HINIC3_FLOW_ACT_OUTPUT, hinic3_flow_process_set_u16_action},
    [HINIC3_FLOW_ACT_VXL_GPE_PUSH] = {HINIC3_FLOW_ACT_VXL_GPE_PUSH, hinic3_flow_process_vxlan_push_action},
    [HINIC3_FLOW_ACT_DPDK_PORT_ID] = {HINIC3_FLOW_ACT_DPDK_PORT_ID, hinic3_flexda_flow_process_set_u16_action},
    [HINIC3_FLOW_ACT_DPDK_SET_IPV6_SRC] = {HINIC3_FLOW_ACT_DPDK_SET_IPV6_SRC, hinic3_flexda_flow_process_set_ipv6_action},
    [HINIC3_FLOW_ACT_DPDK_SET_IPV6_DST] = {HINIC3_FLOW_ACT_DPDK_SET_IPV6_DST, hinic3_flexda_flow_process_set_ipv6_action},
    [HINIC3_FLOW_ACT_DPDK_VXL_PUSH] = {HINIC3_FLOW_ACT_DPDK_VXL_PUSH, hinic3_flow_process_vxlan_push_action},
    [HINIC3_FLOW_ACT_DPDK_VXL_POP] = {HINIC3_FLOW_ACT_DPDK_VXL_POP, hinic3_flow_process_vxlan_pop_action},
    [HINIC3_FLOW_ACT_SET_SIPV6] = {HINIC3_FLOW_ACT_SET_SIPV6, hinic3_flow_process_set_ipv6_action},
    [HINIC3_FLOW_ACT_SET_DIPV6] = {HINIC3_FLOW_ACT_SET_DIPV6, hinic3_flow_process_set_ipv6_action},
    [HINIC3_FLOW_ACT_SET_VXLAN_GROUP_ID] = {HINIC3_FLOW_ACT_SET_VXLAN_GROUP_ID, NULL},
    [HINIC3_FLOW_ACT_ECMP] = {HINIC3_FLOW_ACT_ECMP, NULL},
    [HINIC3_FLOW_ACT_DROP] = {HINIC3_FLOW_ACT_DROP, hinic3_flow_process_drop_action},
    [HINIC3_FLOW_ACT_MIRROR] = {HINIC3_FLOW_ACT_MIRROR, hinic3_flow_process_mirror_action},
    [HINIC3_FLOW_ACT_CRYPTO] = {HINIC3_FLOW_ACT_CRYPTO, NULL},
    [HINIC3_FLOW_ACT_COUNT] = {HINIC3_FLOW_ACT_COUNT, NULL},
    [HINIC3_FLOW_ACT_DEC_TTL] = {HINIC3_FLOW_ACT_DEC_TTL, hinic3_flow_process_ttl_dec_action},
    [HINIC3_FLOW_ACT_UPCALL] = {HINIC3_FLOW_ACT_UPCALL, NULL},
    [HINIC3_FLOW_ACT_DP_HASH] = {HINIC3_FLOW_ACT_DP_HASH, hinic3_flow_process_dp_hash_action},
    [HINIC3_FLOW_ACT_RECIRC_ID] = {HINIC3_FLOW_ACT_RECIRC_ID, hinic3_flow_process_recirc_id_action},
    [HINIC3_FLOW_ACT_BLOCK_VERSION] = {HINIC3_FLOW_ACT_BLOCK_VERSION, hinic3_flow_process_block_version_action},
    [HINIC3_FLOW_ACT_QOS] = {HINIC3_FLOW_ACT_QOS, hinic3_flow_process_qos_id_action},
    [HINIC3_FLOW_ACT_AGE] = {HINIC3_FLOW_ACT_AGE, hinic3_flow_process_age_action},
    [HINIC3_FLOW_ACT_TYPE_MAX] = {HINIC3_FLOW_ACT_TYPE_MAX, NULL},
};

void
hinic3_actions_format_output(struct ds *ds, const struct hinic3_nlattr *actions)
{
    hinic3_nlattr_itr nla = NULL;
    HINIC3_NLATTR_FOR_EACH(nla, actions)
    {
        enum hinic3_flow_action_type type = (enum hinic3_flow_action_type)hinic3_nlattr_get_itr_type(nla);
        if (type < HINIC3_FLOW_ACT_TYPE_MAX && hinic3_actions_format_output_func_array[type].func != NULL)
            hinic3_actions_format_output_func_array[type].func(nla, ds);
    }
}

void
hinic3_flow_stats_format_output(const struct hinic3_flow_stats *stats, struct ds *ds)
{
    hinic3_ds_put_format(ds, "%s(%" PRIu64 "), %s(%" PRIu64 "), ", HINIC3_UI_FLOW_PACKET_STRING, stats->packet_count,
        HINIC3_UI_FLOW_BYTES_STRING, stats->byte_count);

    if (hinic3_check_hardware_flow_age_switch() == true) {
        hinic3_ds_put_format(ds, "%s(%" PRIu32 "ms), ", HINIC3_UI_FLOW_LIVE_TIME_STRING, stats->live_time);
    } else {
        if (hinic3_support_hardware_flow_age_set()) {
            hinic3_ds_put_format(ds, "%s(%" PRIu32 "ms), %s(%" PRIu32 "ms), ", HINIC3_UI_FLOW_AGE_TIME_STRING,
                stats->age_time, HINIC3_UI_FLOW_LIVE_TIME_STRING, stats->live_time);
        } else {
            hinic3_ds_put_format(ds, "%s(%" PRIu32 "ms), ", HINIC3_UI_FLOW_LIVE_TIME_STRING, stats->live_time);
        }
    }

    hinic3_ds_put_cstr(ds, HINIC3_UI_FLOW_FLAG_STRING);
    hinic3_ds_put_format(ds, " %s(%" PRIu64 ")", HINIC3_UI_FLOW_CT_INFO_STRING, stats->ct_loss_pkts);
}

static void
hinic3_flexda_flow_stats_format_output(const struct hinic3_flow_stats *stats, struct ds *ds)
{
    hinic3_ds_put_format(ds, "%s(%" PRIu64 "), %s(%" PRIu64 "), ", HINIC3_UI_FLOW_PACKET_STRING, stats->packet_count,
    HINIC3_UI_FLOW_BYTES_STRING, stats->byte_count);

    /* 黑卡模式下通过hardware_flow_age来表示是否支持硬件流表老化
    灰卡模式下通过流表stats中的age_time是否为0表示是否支持硬件流表老化 */
    bool show_age_time = (hinic3_card_mod_get() == STANDARD_MODE && hinic3_check_hardware_flow_age_switch());
    show_age_time |= (hinic3_card_mod_get() == PROG_MODE && stats->age_time != 0);
    if (show_age_time) {
        hinic3_ds_put_format(ds, "%s(%" PRIu32 "ms), %s(%" PRIu32 "ms), ", HINIC3_UI_FLOW_AGE_TIME_STRING,
            stats->age_time, HINIC3_UI_FLOW_LIVE_TIME_STRING, stats->live_time);
    } else {
        hinic3_ds_put_format(ds, "%s(%" PRIu32 "ms), ", HINIC3_UI_FLOW_LIVE_TIME_STRING, stats->live_time);
    }
    hinic3_ds_put_cstr(ds, HINIC3_UI_FLOW_FLAG_STRING);
    hinic3_ds_put_format(ds, " %s(%" PRIu64 ")", HINIC3_UI_FLOW_CT_INFO_STRING, stats->ct_loss_pkts);
}

static void
hinic3_flexda_actions_format_output(struct ds *ds, const struct hinic3_nlattr *actions)
{
    hinic3_nlattr_itr nla = NULL;
    HINIC3_NLATTR_FOR_EACH(nla, actions)
    {
        enum hinic3_flow_action_type type = (enum hinic3_flow_action_type)hinic3_nlattr_get_itr_type(nla);
        if (type < HINIC3_FLOW_ACT_TYPE_MAX && hinic3_flexda_actions_format_output_func_array[type].func != NULL) {
            hinic3_flexda_actions_format_output_func_array[type].func(nla, ds);
        } else if ((type >= HINIC3_HYDRA_TYPE_ACTION_START && type <= HINIC3_HYDRA_TYPE_ACTION_END)
            || hinic3_flexda_flow_action_is_in_table(type)) {
            hinic3_flow_process_hydra_info(HIOVS_HYDRA_TYPE_ACTION, nla, ds);
        }
    }
}

void
hinic3_agent_flow_format_output(uint32_t table_id, struct hinic3_dpif_flow_for_get *f, struct ds *ds)
{
    struct hinic3_nlattr flow_key;
    struct hinic3_nlattr flow_actions;
    struct hinic3_nlattr flow_mask;
    if (hinic3_card_mod_get() == PROG_MODE) {
        hinic3_ds_put_format(ds, "%s%d, ", "  table_id: ", table_id);
    }
    hinic3_ds_put_format(ds, HINIC3_UI_FLOW_UFID_STRING);
    hinic3_hw_ufid_format_output(&f->ol_ufid, ds);
    if (f->related_hw_ufid != 0) {
        hinic3_ds_put_format(ds, ", %s", HINIC3_UI_FLOW_RELATED_UFID_STRING);
        hinic3_hw_ufid_format_output(&f->related_hw_ufid, ds);
    }

    hinic3_nlattr_init(&flow_key, f->key, f->key_len);
    hinic3_nlattr_reset_itr(&flow_key, f->key_len);
    hinic3_nlattr_init(&flow_actions, f->actions, f->action_len);
    hinic3_nlattr_reset_itr(&flow_actions, f->action_len);

    hinic3_ds_put_format(ds, ", %s", HINIC3_UI_FLOW_KEY_STRING);
    hinic3_cmd_flow_key_format_output(&flow_key, ds);
    // 打印模糊流表的mask域
    if (IS_FLEXDA_FUZZY_TABLE(table_id)) {
        hinic3_nlattr_init(&flow_mask, f->mask, f->mask_len);
        hinic3_nlattr_reset_itr(&flow_mask, f->mask_len);
        hinic3_ds_put_format(ds, "%s ", HINIC3_UI_ITEM_MASK_STRING);
        hinic3_cmd_flow_key_format_output(&flow_mask, ds);
    }

    hinic3_ds_put_format(ds, HINIC3_UI_FLOW_ACTION_STRING);
    if (hinic3_card_mod_get() == PROG_MODE) {
        hinic3_flexda_actions_format_output(ds, &flow_actions);
        hinic3_ds_put_format(ds, HINIC3_UI_FLOW_STATISTIC_STRING);
        hinic3_flexda_flow_stats_format_output(&(f->stats), ds);
    } else {
        hinic3_actions_format_output(ds, &flow_actions);
        hinic3_ds_put_format(ds, HINIC3_UI_FLOW_STATISTIC_STRING);
        hinic3_flow_stats_format_output(&(f->stats), ds);
    }

    hinic3_ds_put_format(ds, "\n");
}

void
hinic3_show_base_port_stats(struct ds *output_msg, const hinic3_port_stats *port_stats)
{
    hinic3_ds_put_format(output_msg, "\t%-30s : %lu\n", "rx-pkts", port_stats->ovs_port_stats.rx_packets);
    hinic3_ds_put_format(output_msg, "\t%-30s : %lu\n", "tx-pkts", port_stats->ovs_port_stats.tx_packets);
    hinic3_ds_put_format(output_msg, "\t%-30s : %lu\n", "rx-bytes", port_stats->ovs_port_stats.rx_bytes);
    hinic3_ds_put_format(output_msg, "\t%-30s : %lu\n", "tx-bytes", port_stats->ovs_port_stats.tx_bytes);
    hinic3_ds_put_format(output_msg, "\t%-30s : %lu\n", "hw-rx-errors", port_stats->ovs_port_stats.rx_errors);
    hinic3_ds_put_format(output_msg, "\t%-30s : %lu\n", "hw-tx-errors", port_stats->ovs_port_stats.tx_errors);
    hinic3_ds_put_format(output_msg, "\t%-30s : %lu\n", "hw-rx-drop", port_stats->ovs_port_stats.rx_dropped);
    hinic3_ds_put_format(output_msg, "\t%-30s : %lu\n", "hw-tx-drop", port_stats->ovs_port_stats.tx_dropped);
    hinic3_ds_put_format(output_msg, "\t%-30s : %lu\n", "hw-collisions", port_stats->ovs_port_stats.collisions);
    hinic3_ds_put_format(output_msg, "\t%-30s : %lu\n", "hw-tx-multicast-drop", port_stats->ovs_port_stats.multicast);
}

void
hinic3_show_check_port_stats(struct ds *output_msg, const hinic3_port_stats *port_stats)
{
    hinic3_ds_put_format(output_msg, "\t%-30s : %lu\n", "hw-rx-len-errors", port_stats->ovs_port_stats.rx_length_errors);
    hinic3_ds_put_format(output_msg, "\t%-30s : %lu\n", "hw-rx-over-errors", port_stats->ovs_port_stats.rx_over_errors);
    hinic3_ds_put_format(output_msg, "\t%-30s : %lu\n", "hw-rx-crc-errors", port_stats->ovs_port_stats.rx_crc_errors);
    hinic3_ds_put_format(output_msg, "\t%-30s : %lu\n", "hw-rx-frame-errors",
        port_stats->ovs_port_stats.rx_frame_errors);
    hinic3_ds_put_format(output_msg, "\t%-30s : %lu\n", "hw-rx-fifo-errors", port_stats->ovs_port_stats.rx_fifo_errors);
    hinic3_ds_put_format(output_msg, "\t%-30s : %lu\n", "hw-rx-miss-errors",
        port_stats->ovs_port_stats.rx_missed_errors);
    hinic3_ds_put_format(output_msg, "\t%-30s : %lu\n", "hw-tx-abort-errors",
        port_stats->ovs_port_stats.tx_aborted_errors);
    hinic3_ds_put_format(output_msg, "\t%-30s : %lu\n", "hw-tx-carrier-errors",
        port_stats->ovs_port_stats.tx_carrier_errors);
    hinic3_ds_put_format(output_msg, "\t%-30s : %lu\n", "hw-tx-fifo-errors", port_stats->ovs_port_stats.tx_fifo_errors);
    hinic3_ds_put_format(output_msg, "\t%-30s : %lu\n", "hw-tx-heartbeat-errors",
        port_stats->ovs_port_stats.tx_heartbeat_errors);
    hinic3_ds_put_format(output_msg, "\t%-30s : %lu\n", "hw-tx-window-errors",
        port_stats->ovs_port_stats.tx_window_errors);
}

void
hinic3_show_hiovs_port_stats(struct ds *output_msg, const hinic3_port_stats *port_stats)
{
    hinic3_ds_put_format(output_msg, "\t%-30s : %u\n", "hw-tx-bond-mode-err-drop", port_stats->tx_bond_mode_err_dropped);
    hinic3_ds_put_format(output_msg, "\t%-30s : %u\n", "hw-tx-bond-noport-drop", port_stats->tx_bond_noport_dropped);
    hinic3_ds_put_format(output_msg, "\t%-30s : %u\n", "hw-tx-bcmc-limit-drop", port_stats->tx_bcmc_limit_dropped);
    hinic3_ds_put_format(output_msg, "\t%-30s : %u\n", "hw-tx-bum-smac-drop", port_stats->tx_bum_smac_dropped);
    hinic3_ds_put_format(output_msg, "\t%-30s : %u\n", "hw-tx-bum-sipsmac-drop", port_stats->tx_bum_sipsmac_dropped);
    hinic3_ds_put_format(output_msg, "\t%-30s : %u\n", "hw-tx-bum-ethtype-drop", port_stats->tx_bum_ethtype_dropped);
    hinic3_ds_put_format(output_msg, "\t%-30s : %u\n", "hw-rx-wqe-fail-drop", port_stats->rx_wqe_fail_dropped);
    hinic3_ds_put_format(output_msg, "\t%-30s : %u\n", "hw-rx-qos-drop", port_stats->rx_qos_dropped);
    hinic3_ds_put_format(output_msg, "\t%-30s : %u\n", "hw-rx-mtu-drop", port_stats->rx_mtu_dropped);
    hinic3_ds_put_format(output_msg, "\t%-30s : %u\n", "hw-rx-vport-invalid-drop", port_stats->rx_vport_invalid_dropped);
    hinic3_ds_put_format(output_msg, "\t%-30s : %u\n", "hw-ct-drop", port_stats->ct_dropped);
}

void
hinic3_show_upcall_port_stats(struct ds *output_msg, const hinic3_port_stats *port_stats)
{
    hinic3_ds_put_format(output_msg, "\t%-30s : %u\n", "hw-upcall-wqe-fail-dropped",
        port_stats->upcall_wqe_fail_dropped);
    hinic3_ds_put_format(output_msg, "\t%-30s : %lu\n", "hw-upcall-pkt-num-vport", port_stats->upcall_pkt_num_vport);
    hinic3_ds_put_format(output_msg, "\t%-30s : %lu\n", "tx-uc-bytes", port_stats->tx_uc_bytes);
    hinic3_ds_put_format(output_msg, "\t%-30s : %lu\n", "tx-uc-pkts", port_stats->tx_uc_pkts);
    hinic3_ds_put_format(output_msg, "\t%-30s : %lu\n", "tx-bc-bytes", port_stats->tx_bc_bytes);
    hinic3_ds_put_format(output_msg, "\t%-30s : %lu\n", "tx-bc-pkts", port_stats->tx_bc_pkts);
    hinic3_ds_put_format(output_msg, "\t%-30s : %lu\n", "tx-mc-bytes", port_stats->tx_mc_bytes);
    hinic3_ds_put_format(output_msg, "\t%-30s : %lu\n", "tx-mc-pkts", port_stats->tx_mc_pkts);
    hinic3_ds_put_format(output_msg, "\t%-30s : %lu\n", "rx-uc-bytes", port_stats->rx_uc_bytes);
    hinic3_ds_put_format(output_msg, "\t%-30s : %lu\n", "rx-uc-pkts", port_stats->rx_uc_pkts);
    hinic3_ds_put_format(output_msg, "\t%-30s : %lu\n", "rx-bc-bytes", port_stats->rx_bc_bytes);
    hinic3_ds_put_format(output_msg, "\t%-30s : %lu\n", "rx-bc-pkts", port_stats->rx_bc_pkts);
    hinic3_ds_put_format(output_msg, "\t%-30s : %lu\n", "rx-mc-bytes", port_stats->rx_mc_bytes);
    hinic3_ds_put_format(output_msg, "\t%-30s : %lu\n", "rx-mc-pkts", port_stats->rx_mc_pkts);
}
