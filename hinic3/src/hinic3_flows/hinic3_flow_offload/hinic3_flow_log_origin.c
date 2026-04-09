/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2025 Huawei Technologies Co., Ltd
 */

#include "hinic3_util.h"
#include "hinic3_log.h"
#include "hinic3_packets.h"
#include "hinic3_vxlan.h"
#include "hinic3_agent_cmd_format.h"
#include "hinic3_dump_flow.h"
#include "hinic3_offload_flow.h"

typedef void (*hinic3_flow_item_log)(const struct rte_flow_item *, struct ds *);
typedef void (*hinic3_flow_action_log)(const struct rte_flow_action *, struct ds *);

struct hinic3_flow_item_info {
    enum rte_flow_item_type item_type;
    hinic3_flow_item_log log_callback;
};

struct hinic3_flow_action_info {
    enum rte_flow_action_type action_type;
    hinic3_flow_action_log log_callback;
};

static void hinic3_item_eth_log(const struct rte_flow_item *item, struct ds *ds)
{
    const struct rte_flow_item_eth *eth = (const struct rte_flow_item_eth *)item->spec;
    hinic3_ds_put_format(ds, "ETH:");
    if (eth == NULL) {
        hinic3_ds_put_format(ds, "item is null, ");
        return;
    }
    hinic3_ds_put_format(ds, "smac(" HINIC3_MAC_FMT "),", HINIC3_OUTPUT_MAC(eth->src.addr_bytes));
    hinic3_ds_put_format(ds, "dmac(" HINIC3_MAC_FMT "),", HINIC3_OUTPUT_MAC(eth->dst.addr_bytes));
    hinic3_ds_put_format(ds, "eth_type(%#hx),", ntohs(eth->type));
    hinic3_ds_put_format(ds, "has_vlan(%u),", (unsigned int)eth->has_vlan);
    hinic3_ds_put_format(ds, "reserved(%u),", (unsigned int)eth->reserved);
}

static void hinic3_item_input_port_log(const struct rte_flow_item *item, struct ds *ds)
{
    const struct rte_flow_item_port_id *port = (const struct rte_flow_item_port_id *)item->spec;
    hinic3_ds_put_format(ds, "INPORT:");
    if (port == NULL) {
        hinic3_ds_put_format(ds, "item is null, ");
        return;
    }
    hinic3_ds_put_format(ds, "vport(%u),", port->id);
}

static void hinic3_item_tcp_log(const struct rte_flow_item *item, struct ds *ds)
{
    const struct rte_flow_item_tcp *tcp = (const struct rte_flow_item_tcp *)item->spec;
    hinic3_ds_put_format(ds, "TCP:");
    if (tcp == NULL) {
        hinic3_ds_put_format(ds, "item is null, ");
        return;
    }
    hinic3_ds_put_format(ds, "src_port(%hu),", ntohs(tcp->hdr.src_port));
    hinic3_ds_put_format(ds, "dst_port(%hu),", ntohs(tcp->hdr.dst_port));
    hinic3_ds_put_format(ds, "sent_seq(%u),", ntohs(tcp->hdr.sent_seq));
    hinic3_ds_put_format(ds, "recv_ack(%u),", ntohs(tcp->hdr.recv_ack));
    hinic3_ds_put_format(ds, "data_off(%hhu),", tcp->hdr.data_off);
    hinic3_ds_put_format(ds, "tcp_flags(%hhu),", tcp->hdr.tcp_flags);
    hinic3_ds_put_format(ds, "rx_win(%hu),", ntohs(tcp->hdr.rx_win));
    hinic3_ds_put_format(ds, "cksum(%hu),", ntohs(tcp->hdr.cksum));
    hinic3_ds_put_format(ds, "tcp_urp(%hu),", ntohs(tcp->hdr.tcp_urp));
}

static void hinic3_item_udp_log(const struct rte_flow_item *item, struct ds *ds)
{
    const struct rte_flow_item_udp *udp = (const struct rte_flow_item_udp *)item->spec;
    hinic3_ds_put_format(ds, "UDP:");
    if (udp == NULL) {
        hinic3_ds_put_format(ds, "item is null, ");
        return;
    }
    hinic3_ds_put_format(ds, "src_port(%hu),", ntohs(udp->hdr.src_port));
    hinic3_ds_put_format(ds, "dst_port(%hu),", ntohs(udp->hdr.dst_port));
    hinic3_ds_put_format(ds, "dgram_len(%hu),", ntohs(udp->hdr.dgram_len));
    hinic3_ds_put_format(ds, "dgram_cksum(%hu),", ntohs(udp->hdr.dgram_cksum));
}

static void hinic3_item_sctp_log(const struct rte_flow_item *item, struct ds *ds)
{
    const struct rte_flow_item_sctp *sctp = (const struct rte_flow_item_sctp *)item->spec;
    hinic3_ds_put_format(ds, "SCTP:");
    if (sctp == NULL) {
        hinic3_ds_put_format(ds, "item is null, ");
        return;
    }
    hinic3_ds_put_format(ds, "src_port(%hu),", ntohs(sctp->hdr.src_port));
    hinic3_ds_put_format(ds, "dst_port(%hu),", ntohs(sctp->hdr.dst_port));
    hinic3_ds_put_format(ds, "tag(%u),", ntohs(sctp->hdr.tag));
    hinic3_ds_put_format(ds, "cksum(%u),", ntohs(sctp->hdr.cksum));
}

static void hinic3_item_icmp_log(const struct rte_flow_item *item, struct ds *ds)
{
    const struct rte_flow_item_icmp *icmp = (const struct rte_flow_item_icmp *)item->spec;
    hinic3_ds_put_format(ds, "ICMP:");
    if (icmp == NULL) {
        hinic3_ds_put_format(ds, "item is null, ");
        return;
    }
    hinic3_ds_put_format(ds, "icmp_type(%hhu),", icmp->hdr.icmp_type);
    hinic3_ds_put_format(ds, "icmp_code(%hhu),", icmp->hdr.icmp_code);
    hinic3_ds_put_format(ds, "icmp_cksum(%hu),", ntohs(icmp->hdr.icmp_cksum));
    hinic3_ds_put_format(ds, "icmp_ident(%hu),", ntohs(icmp->hdr.icmp_ident));
    hinic3_ds_put_format(ds, "icmp_seq_nb(%hu),", ntohs(icmp->hdr.icmp_seq_nb));
}

static void hinic3_item_vlan_log(const struct rte_flow_item *item, struct ds *ds)
{
    const struct rte_flow_item_vlan *vlan = (const struct rte_flow_item_vlan *)item->spec;
    hinic3_ds_put_format(ds, "VLAN:");
    if (vlan == NULL) {
        hinic3_ds_put_format(ds, "item is null, ");
        return;
    }
    hinic3_ds_put_format(ds, "vlan_tci(%hu),", ntohs(vlan->tci));
    hinic3_ds_put_format(ds, "inner_type(%hu),", ntohs(vlan->inner_type));
    hinic3_ds_put_format(ds, "has_more_vlan(%u),", (unsigned int)vlan->has_more_vlan);
    hinic3_ds_put_format(ds, "reserved(%u),", (unsigned int)vlan->reserved);
}

static void hinic3_item_tag_log(const struct rte_flow_item* item, struct ds *ds)
{
    const struct rte_flow_item_tag *tag = (const struct rte_flow_item_tag *)item->spec;
    hinic3_ds_put_format(ds, "TAG:");
    if (tag == NULL) {
        hinic3_ds_put_format(ds, "item is null, ");
        return;
    }
    hinic3_ds_put_format(ds, "data(%u),", tag->data);
    hinic3_ds_put_format(ds, "index(%hhu),", tag->index);
}

static void hinic3_item_arp_log(const struct rte_flow_item* item, struct ds *ds)
{
    const struct rte_flow_item_arp_eth_ipv4 *arp = (const struct rte_flow_item_arp_eth_ipv4 *)item->spec;
    hinic3_ds_put_format(ds, "ARP:");
    if (arp == NULL) {
        hinic3_ds_put_format(ds, "item is null, ");
        return;
    }
    hinic3_ds_put_format(ds, "hrd(%hu),", ntohs(arp->hrd));
    hinic3_ds_put_format(ds, "pro(%hu),", ntohs(arp->pro));
    hinic3_ds_put_format(ds, "hln(%hhu),", arp->hln);
    hinic3_ds_put_format(ds, "pln(%hhu),", arp->pln);
    hinic3_ds_put_format(ds, "op(%hu),", ntohs(arp->op));
    hinic3_ds_put_format(ds, "sha(" HINIC3_MAC_FMT "),", HINIC3_OUTPUT_MAC(arp->sha.addr_bytes));
    hinic3_ds_put_format(ds, "spa(%u),", ntohs(arp->spa));
    hinic3_ds_put_format(ds, "tha(" HINIC3_MAC_FMT "),", HINIC3_OUTPUT_MAC(arp->tha.addr_bytes));
    hinic3_ds_put_format(ds, "tpa(%u),", ntohs(arp->tpa));
}

static void hinic3_item_ipv4_log(const struct rte_flow_item* item, struct ds *ds)
{
    const struct rte_flow_item_ipv4 *ipv4 = (const struct rte_flow_item_ipv4 *)item->spec;
    hinic3_ds_put_format(ds, "IPV4:");
    if (ipv4 == NULL) {
        hinic3_ds_put_format(ds, "item is null, ");
        return;
    }
    hinic3_ds_put_format(ds, "version_ihl(%hhu),", ipv4->hdr.version_ihl);
    hinic3_ds_put_format(ds, "type_of_service(%hhu),", ipv4->hdr.type_of_service);
    hinic3_ds_put_format(ds, "total_length(%hu),", ntohs(ipv4->hdr.total_length));
    hinic3_ds_put_format(ds, "packet_id(%hu),", ntohs(ipv4->hdr.packet_id));
    hinic3_ds_put_format(ds, "fragment_offset(%hu),", ntohs(ipv4->hdr.fragment_offset));
    hinic3_ds_put_format(ds, "time_to_live(%hhu),", ipv4->hdr.time_to_live);
    hinic3_ds_put_format(ds, "next_proto_id(%hhu),", ipv4->hdr.next_proto_id);
    hinic3_ds_put_format(ds, "hdr_checksum(%hu),", ntohs(ipv4->hdr.hdr_checksum));
    hinic3_ds_put_format(ds, "src_addr(" IP_FMT "),", IP_ARGS(ipv4->hdr.src_addr));
    hinic3_ds_put_format(ds, "dst_addr(" IP_FMT "),", IP_ARGS(ipv4->hdr.dst_addr));
}

static void hinic3_item_ipv6_log(const struct rte_flow_item* item, struct ds *ds)
{
    const struct rte_flow_item_ipv6 *ipv6 = (const struct rte_flow_item_ipv6 *)item->spec;
    hinic3_ds_put_format(ds, "IPV6:");
    if (ipv6 == NULL) {
        hinic3_ds_put_format(ds, "item is null, ");
        return;
    }
    hinic3_ds_put_format(ds, "vtc_flow(%u),", ntohs(ipv6->hdr.vtc_flow));
    hinic3_ds_put_format(ds, "payload_len(%hu),", ntohs(ipv6->hdr.payload_len));
    hinic3_ds_put_format(ds, "proto(%hhu),", ipv6->hdr.proto);
    hinic3_ds_put_format(ds, "hop_limits(%hhu),", ipv6->hdr.hop_limits);
    hinic3_ds_put_format(ds, "src_addr(");
    hinic3_ipv6_format_addr((const struct in6_addr*)ipv6->hdr.src_addr, ds);
    hinic3_ds_put_format(ds, "),");
    hinic3_ds_put_format(ds, "dst_addr(");
    hinic3_ipv6_format_addr((const struct in6_addr*)ipv6->hdr.dst_addr, ds);
    hinic3_ds_put_format(ds, "),");
    hinic3_ds_put_format(ds, "has_hop_ext(%u),", (unsigned int)ipv6->has_hop_ext);
    hinic3_ds_put_format(ds, "has_route_ext(%u),", (unsigned int)ipv6->has_route_ext);
    hinic3_ds_put_format(ds, "has_frag_ext(%u),", (unsigned int)ipv6->has_frag_ext);
    hinic3_ds_put_format(ds, "has_auth_ext(%u),", (unsigned int)ipv6->has_auth_ext);
    hinic3_ds_put_format(ds, "has_esp_ext(%u),", (unsigned int)ipv6->has_esp_ext);
    hinic3_ds_put_format(ds, "has_dest_ext(%u),", (unsigned int)ipv6->has_dest_ext);
    hinic3_ds_put_format(ds, "has_mobil_ext(%u),", (unsigned int)ipv6->has_mobil_ext);
    hinic3_ds_put_format(ds, "has_hip_ext(%u),", (unsigned int)ipv6->has_hip_ext);
    hinic3_ds_put_format(ds, "has_shim6_ext(%u),", (unsigned int)ipv6->has_shim6_ext);
    hinic3_ds_put_format(ds, "reserved(%u),", (unsigned int)ipv6->reserved);
}

static void hinic3_item_vxlan_log(const struct rte_flow_item* item, struct ds *ds)
{
    const struct rte_flow_item_vxlan *vxlan = (const struct rte_flow_item_vxlan *)item->spec;
    hinic3_ds_put_format(ds, "VXLAN:");
    if (vxlan == NULL) {
        hinic3_ds_put_format(ds, "item is null, ");
        return;
    }
    hinic3_ds_put_format(ds, "flags(%hhu),", vxlan->flags);
    hinic3_ds_put_format(ds, "rsvd(%#x),", ntohl(hinic3_get_hiovs_vni(vxlan->rsvd0)));
    hinic3_ds_put_format(ds, "vni(%u),", ntohl(hinic3_get_hiovs_vni(vxlan->vni)));
    hinic3_ds_put_format(ds, "rsvd1(%#hhx),", vxlan->rsvd1);
}

static const struct hinic3_flow_item_info g_item_log_table[] = {
    {RTE_FLOW_ITEM_TYPE_ETH,          hinic3_item_eth_log},
    {RTE_FLOW_ITEM_TYPE_PORT_ID,      hinic3_item_input_port_log},
    {RTE_FLOW_ITEM_TYPE_TCP,          hinic3_item_tcp_log},
    {RTE_FLOW_ITEM_TYPE_UDP,          hinic3_item_udp_log},
    {RTE_FLOW_ITEM_TYPE_SCTP,         hinic3_item_sctp_log},
    {RTE_FLOW_ITEM_TYPE_ICMP,         hinic3_item_icmp_log},
    {RTE_FLOW_ITEM_TYPE_VLAN,         hinic3_item_vlan_log},
    {RTE_FLOW_ITEM_TYPE_TAG,          hinic3_item_tag_log},
    {RTE_FLOW_ITEM_TYPE_ARP_ETH_IPV4, hinic3_item_arp_log},
    {RTE_FLOW_ITEM_TYPE_VXLAN,        hinic3_item_vxlan_log},
    {RTE_FLOW_ITEM_TYPE_IPV4,         hinic3_item_ipv4_log},
    {RTE_FLOW_ITEM_TYPE_IPV6,         hinic3_item_ipv6_log},
    {RTE_FLOW_ITEM_TYPE_ICMP6,        hinic3_item_icmp_log},
};

static void hinic3_flow_log_original_item(const struct rte_flow_item *item, struct ds *ds)
{
    for (unsigned int i = 0; i < ARRAY_SIZE(g_item_log_table); ++i) {
        if (g_item_log_table[i].item_type == item->type && g_item_log_table[i].log_callback != NULL) {
            g_item_log_table[i].log_callback(item, ds);
            return;
        }
    }
    hinic3_ds_put_format(ds, "unknown item type(%d),", item->type);
}

static void hinic3_action_count(const struct rte_flow_action *action __rte_unused, struct ds *ds)
{
    hinic3_ds_put_format(ds, "COUNT,");
}

static void hinic3_action_set_tag(const struct rte_flow_action* action, struct ds *ds)
{
    const struct rte_flow_action_set_tag *tag = action->conf;
    hinic3_ds_put_format(ds, "SET_TAG:");
    if (tag == NULL) {
        hinic3_ds_put_format(ds, "action is null, ");
        return;
    }
    hinic3_ds_put_format(ds, "data(%u),", tag->data);
    hinic3_ds_put_format(ds, "mask(%u),", tag->mask);
    hinic3_ds_put_format(ds, "index(%hhu),", tag->index);
}

static void hinic3_action_acl_remark(const struct rte_flow_action *action, struct ds *ds)
{
    const struct rte_flow_action_modify_field *modify_field = action->conf;
    hinic3_ds_put_format(ds, "MODIFY_FIELD:");
    if (modify_field == NULL) {
        hinic3_ds_put_format(ds, "action is null, ");
        return;
    }

    uint8_t remark_value = modify_field->src.value[0];
    hinic3_ds_put_format(ds, "remark(%hhu),", remark_value);
}

static void hinic3_action_acl_sample(const struct rte_flow_action *action, struct ds *ds)
{
    hinic3_ds_put_format(ds, "SAMPLE:");
    const struct rte_flow_action_sample *sample = action->conf;
    if (sample == NULL) {
        hinic3_ds_put_format(ds, "acl sample is null, ");
        return;
    }
    const struct rte_flow_action *act = sample->actions;
    if (act == NULL) {
        hinic3_ds_put_format(ds, "acl sample action is null, ");
        return;
    }
    const struct rte_flow_action_port_id *output_port = (const struct rte_flow_action_port_id *)act->conf;
    if (output_port == NULL) {
        hinic3_ds_put_format(ds, "acl sample port id conf is null, ");
        return;
    }

    uint32_t session_id = output_port->reserved;
    hinic3_ds_put_format(ds, "session id(%u),", session_id);
}

static void hinic3_action_push_vlan(const struct rte_flow_action *action __rte_unused, struct ds *ds)
{
    hinic3_ds_put_format(ds, "PUSH_VLAN,");
}

static void hinic3_action_pop_vlan(const struct rte_flow_action *action __rte_unused, struct ds *ds)
{
    hinic3_ds_put_format(ds, "POP_VLAN,");
}

static void hinic3_action_set_vid(const struct rte_flow_action *action, struct ds *ds)
{
    const struct rte_flow_action_of_set_vlan_vid *vid = action->conf;
    hinic3_ds_put_format(ds, "SET_VID:");
    if (vid == NULL) {
        hinic3_ds_put_format(ds, "action is null, ");
        return;
    }
    hinic3_ds_put_format(ds, "vlan_vid(%hu),", ntohs(vid->vlan_vid));
}
 
static void hinic3_action_set_pcp(const struct rte_flow_action *action, struct ds *ds)
{
    const struct rte_flow_action_of_set_vlan_pcp *pcp = action->conf;
    hinic3_ds_put_format(ds, "SET_PCP:");
    if (pcp == NULL) {
        hinic3_ds_put_format(ds, "action is null, ");
        return;
    }
    hinic3_ds_put_format(ds, "vlan_pcp(%hhu),", pcp->vlan_pcp);
}

static void hinic3_action_vxlan_encap(const struct rte_flow_action *action, struct ds *ds)
{
    const struct rte_flow_action_vxlan_encap *vxlan_encap = action->conf;
    hinic3_ds_put_format(ds, "VXLAN_ENCAP[");
    if (vxlan_encap == NULL) {
        hinic3_ds_put_format(ds, "action is null, ");
        return;
    }
    const struct rte_flow_item *encap_items = vxlan_encap->definition;
    const struct rte_flow_item *item = next_no_end_pattern(encap_items, NULL);
    while (item) {
        hinic3_flow_log_original_item(item, ds);
        item = next_no_end_pattern(encap_items, item);
    }
    hinic3_ds_put_format(ds, "END]");
}
 
static void hinic3_action_vxlan_decap(const struct rte_flow_action *action __rte_unused, struct ds *ds)
{
    hinic3_ds_put_format(ds, "VXLAN_DECAP,");
}

static void hinic3_action_drop(const struct rte_flow_action *action __rte_unused, struct ds *ds)
{
    hinic3_ds_put_format(ds, "DROP,");
}

static void hinic3_action_port_id(const struct rte_flow_action *action, struct ds *ds)
{
    hinic3_ds_put_format(ds, "OUTPUT:");
    const struct rte_flow_action_port_id *port_id = (const struct rte_flow_action_port_id *)action->conf;
    if (port_id == NULL) {
        hinic3_ds_put_format(ds, "action is null, ");
        return;
    }
    hinic3_ds_put_format(ds, "port id(%u),", port_id->id);
}

static const struct hinic3_flow_action_info g_action_log_table[] = {
    {RTE_FLOW_ACTION_TYPE_COUNT,           hinic3_action_count},
    {RTE_FLOW_ACTION_TYPE_SET_TAG,         hinic3_action_set_tag},
    {RTE_FLOW_ACTION_TYPE_MODIFY_FIELD,    hinic3_action_acl_remark},
    {RTE_FLOW_ACTION_TYPE_SAMPLE,          hinic3_action_acl_sample},
    {RTE_FLOW_ACTION_TYPE_OF_PUSH_VLAN,    hinic3_action_push_vlan},
    {RTE_FLOW_ACTION_TYPE_OF_POP_VLAN,     hinic3_action_pop_vlan},
    {RTE_FLOW_ACTION_TYPE_OF_SET_VLAN_VID, hinic3_action_set_vid},
    {RTE_FLOW_ACTION_TYPE_OF_SET_VLAN_PCP, hinic3_action_set_pcp},
    {RTE_FLOW_ACTION_TYPE_VXLAN_ENCAP,     hinic3_action_vxlan_encap},
    {RTE_FLOW_ACTION_TYPE_VXLAN_DECAP,     hinic3_action_vxlan_decap},
    {RTE_FLOW_ACTION_TYPE_DROP,            hinic3_action_drop},
    {RTE_FLOW_ACTION_TYPE_PORT_ID,         hinic3_action_port_id},
};

static void hinic3_flow_log_original_action(const struct rte_flow_action *action, struct ds *ds)
{
    for (unsigned int i = 0; i < ARRAY_SIZE(g_action_log_table); ++i) {
        if (g_action_log_table[i].action_type == action->type && g_action_log_table[i].log_callback != NULL) {
            g_action_log_table[i].log_callback(action, ds);
            return;
        }
    }
    hinic3_ds_put_format(ds, "unknown action type(%d),", action->type);
}

void hinic3_flow_log_original_entry(const struct rte_flow_item patterns[], const struct rte_flow_action actions[])
{
    struct ds ds = DS_EMPTY_INITIALIZER;
    const struct rte_flow_item *item = next_no_end_pattern(patterns, NULL);
    const struct rte_flow_action *action = next_no_void_action(actions, NULL);

    while (item) {
        hinic3_flow_log_original_item(item, &ds);
        item = next_no_end_pattern(patterns, item);
    }
    hinic3_ds_put_format(&ds, "ITEMS END. | ");
    while (action && (action->type != RTE_FLOW_ACTION_TYPE_END)) {
        hinic3_flow_log_original_action(action, &ds);
        action = next_no_void_action(actions, action);
    }
    hinic3_ds_put_format(&ds, "ACTIONS END.");
    HINIC3_LOG(DEBUG, FLOW, "%s", hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
}
