/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
 * Description:rte flow format to string
 * Create: 2023-10-07
 */
#include "hinic3_flow_format.h"
#include "hinic3_ui_string.h"
#include "hinic3_log.h"
#include "hinic3_agent_cmd_format.h"
#include "hinic3_vxlan.h"
#include "hinic3_eth_packets.h"
#include "hinic3_ds.h"
#include "hinic3_packets.h"

typedef int (*hinic3_flow_item_format_callback)(const struct rte_flow_item *, enum hinic3_key_type, struct ds *);
typedef int (*hinic3_flow_action_format_callback)(const struct rte_flow_action *, struct ds *);

struct hinic3_item_format_info {
    enum rte_flow_item_type type;
    hinic3_flow_item_format_callback call_back;
};

struct hinic3_action_format_info {
    enum rte_flow_action_type type;
    hinic3_flow_action_format_callback call_back;
};

static int hinic3_process_eth(const struct rte_flow_item *item, enum hinic3_key_type type, struct ds *ds)
{
    const struct rte_flow_item_eth *eth = type == HINIC3_ITEM_SPEC ? item->spec : item->mask;
    if (eth == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3_process_eth: Invalid  eth item ptr");
        return -1;
    }

    hinic3_ds_put_format(ds, "%s(" HINIC3_MAC_FMT "),", HINIC3_UI_KEY_SRC_MAC_STR, HINIC3_OUTPUT_MAC(eth->src.addr_bytes));
    hinic3_ds_put_format(ds, "%s(" HINIC3_MAC_FMT "),", HINIC3_UI_KEY_DST_MAC_STR, HINIC3_OUTPUT_MAC(eth->dst.addr_bytes));
    hinic3_ds_put_format(ds, "%s(%#hx),", HINIC3_UI_ETH_TYPE_STR, ntohs(eth->type));
    return 0;
}

static int hinic3_process_vlan(const struct rte_flow_item* item, enum hinic3_key_type type, struct ds *ds)
{
    const struct rte_flow_item_vlan *vlan = type == HINIC3_ITEM_SPEC ? item->spec : item->mask;
    if (vlan == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3_process_vlan: Invalid vlan item ptr");
        return -1;
    }

    hinic3_ds_put_format(ds, "%s(%#hx),", HINIC3_UI_KEY_INNER_VLAN_STR, ntohs(vlan->inner_type));
    hinic3_ds_put_format(ds, "%s(%hu),", HINIC3_UI_KEY_VID_STR, ntohs(vlan->tci));
    return 0;
}

static int hinic3_process_ipv4(const struct rte_flow_item* item, enum hinic3_key_type type, struct ds *ds)
{
    const struct rte_flow_item_ipv4 *ipv4 = type == HINIC3_ITEM_SPEC ? item->spec : item->mask;
    if (ipv4 == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3_process_ipv4: Invalid ipv4 item ptr");
        return -1;
    }

    const char *src_str = item->type == RTE_FLOW_ITEM_TYPE_IPV4 ? HINIC3_UI_KEY_SRC_IP_STR : HINIC3_UI_KEY_SRC_OUT_IP_STR;
    const char *dst_str = item->type == RTE_FLOW_ITEM_TYPE_IPV4 ? HINIC3_UI_KEY_DST_IP_STR : HINIC3_UI_KEY_DST_OUT_IP_STR;

    hinic3_ds_put_format(ds, "%s(" IP_FMT "),", src_str, IP_ARGS(ipv4->hdr.src_addr));
    hinic3_ds_put_format(ds, "%s(" IP_FMT "),", dst_str, IP_ARGS(ipv4->hdr.dst_addr));

    if (item->type == RTE_FLOW_ITEM_TYPE_IPV4) {
        hinic3_ds_put_format(ds, "%s(%hhu),", HINIC3_UI_TOS_STR, ipv4->hdr.type_of_service);
        hinic3_ds_put_format(ds, "%s(%hhu),", HINIC3_UI_KEY_NW_PROTO_STR, ipv4->hdr.next_proto_id);
    }
    return 0;
}

static int hinic3_process_ipv6(const struct rte_flow_item* item, enum hinic3_key_type type, struct ds *ds)
{
    const struct rte_flow_item_ipv6 *ipv6 = type == HINIC3_ITEM_SPEC ? item->spec : item->mask;
    if (ipv6 == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3_process_ipv6: Invalid ipv6 item ptr");
        return -1;
    }
    const char *src_str = item->type == RTE_FLOW_ITEM_TYPE_IPV6 ? HINIC3_UI_KEY_SRC_IP_STR : HINIC3_UI_KEY_SRC_OUT_IP_STR;
    const char *dst_str = item->type == RTE_FLOW_ITEM_TYPE_IPV6 ? HINIC3_UI_KEY_DST_IP_STR : HINIC3_UI_KEY_DST_OUT_IP_STR;

    hinic3_ds_put_format(ds, "%s(", src_str);
    hinic3_ipv6_format_addr((const struct in6_addr*)ipv6->hdr.src_addr, ds);
    hinic3_ds_put_format(ds, "),");

    hinic3_ds_put_format(ds, "%s(", dst_str);
    hinic3_ipv6_format_addr((const struct in6_addr*)ipv6->hdr.dst_addr, ds);
    hinic3_ds_put_format(ds, "),");

    if (item->type == RTE_FLOW_ITEM_TYPE_IPV6) {
        hinic3_ds_put_format(ds, "%s(%u),", HINIC3_UI_VTC_STR, ntohl(ipv6->hdr.vtc_flow));
        hinic3_ds_put_format(ds, "%s(%hhu),", HINIC3_UI_KEY_NW_PROTO_STR, ipv6->hdr.proto);
    }
    return 0;
}

static int hinic3_process_tcp(const struct rte_flow_item* item, enum hinic3_key_type type, struct ds *ds)
{
    const struct rte_flow_item_tcp *tcp = type == HINIC3_ITEM_SPEC ? item->spec : item->mask;
    if (tcp == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3_process_tcp: Invalid tcp item ptr");
        return -1;
    }
    hinic3_ds_put_format(ds, "%s(%hu),", HINIC3_UI_KEY_SRC_PORT_STR, ntohs(tcp->hdr.src_port));
    hinic3_ds_put_format(ds, "%s(%hu),", HINIC3_UI_KEY_DST_PORT_STR, ntohs(tcp->hdr.dst_port));
    return 0;
}

static int hinic3_process_udp(const struct rte_flow_item* item, enum hinic3_key_type type, struct ds *ds)
{
    const struct rte_flow_item_udp *udp = type == HINIC3_ITEM_SPEC ? item->spec : item->mask;
    if (udp == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3_process_udp: Invalid udp item ptr");
        return -1;
    }
    hinic3_ds_put_format(ds, "%s(%hu),", HINIC3_UI_KEY_SRC_PORT_STR, ntohs(udp->hdr.src_port));
    hinic3_ds_put_format(ds, "%s(%hu),", HINIC3_UI_KEY_DST_PORT_STR, ntohs(udp->hdr.dst_port));
    return 0;
}

static int hinic3_process_sctp(const struct rte_flow_item* item, enum hinic3_key_type type, struct ds *ds)
{
    const struct rte_flow_item_sctp *sctp = type == HINIC3_ITEM_SPEC ? item->spec : item->mask;
    if (sctp == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3_process_sctp: Invalid sctp item ptr");
        return -1;
    }
    hinic3_ds_put_format(ds, "%s(%hu),", HINIC3_UI_KEY_SRC_PORT_STR, ntohs(sctp->hdr.src_port));
    hinic3_ds_put_format(ds, "%s(%hu),", HINIC3_UI_KEY_DST_PORT_STR, ntohs(sctp->hdr.dst_port));
    return 0;
}

static int hinic3_process_icmp(const struct rte_flow_item* item, enum hinic3_key_type type, struct ds *ds)
{
    const struct rte_flow_item_icmp *icmp = type == HINIC3_ITEM_SPEC ? item->spec : item->mask;
    if (icmp == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3_process_icmp: Invalid icmp item ptr");
        return -1;
    }

    const char *icmp_type_str = item->type == RTE_FLOW_ITEM_TYPE_ICMP ? HINIC3_UI_KEY_ICMP_TYPE_STR :
                                                                  HINIC3_UI_KEY_ICMP6_TYPE_STR;
    const char *icmp_code_str = item->type == RTE_FLOW_ITEM_TYPE_ICMP ? HINIC3_UI_KEY_ICMP_CODE_STR :
                                                                  HINIC3_UI_KEY_ICMP6_CODE_STR;

    hinic3_ds_put_format(ds, "%s(%hhu),", icmp_type_str, icmp->hdr.icmp_type);
    hinic3_ds_put_format(ds, "%s(%hhu),", icmp_code_str, icmp->hdr.icmp_code);
    return 0;
}

static int hinic3_process_arp(const struct rte_flow_item* item, enum hinic3_key_type type, struct ds *ds)
{
    const struct rte_flow_item_arp_eth_ipv4 *arp = type == HINIC3_ITEM_SPEC ? item->spec : item->mask;
    if (arp == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3_process_icmp: Invalid arp item ptr");
        return -1;
    }

    hinic3_ds_put_format(ds, "%s(%hu),", HINIC3_UI_ARP_OP_STR, ntohs(arp->op));
    return 0;
}

static int hinic3_process_tag_item(const struct rte_flow_item* item, enum hinic3_key_type type, struct ds *ds)
{
    const struct rte_flow_item_tag *tag = type == HINIC3_ITEM_SPEC ? item->spec : item->mask;
    if (tag == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3_process_tag_item: Invalid tag item ptr");
        return -1;
    }

    hinic3_ds_put_format(ds, "%s(%u),", HINIC3_UI_TAG_DATA_STR, tag->data);
    hinic3_ds_put_format(ds, "%s(%hhu),", HINIC3_UI_TAG_INDEX_STR, tag->index);
    return 0;
}

static int hinic3_process_vxlan(const struct rte_flow_item* item, enum hinic3_key_type type, struct ds *ds)
{
    const struct rte_flow_item_vxlan *vxlan = type == HINIC3_ITEM_SPEC ? item->spec : item->mask;
    uint32_t vni;
    uint32_t rsvd;
    if (vxlan == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3_process_vxlan: Invalid tag item ptr");
        return -1;
    }

    vni = hinic3_get_hiovs_vni(vxlan->vni);
    rsvd = hinic3_get_hiovs_vni(vxlan->rsvd0);
    hinic3_ds_put_format(ds, "vni(%u),rsvd(%#hhx),", ntohl(vni), ntohl(rsvd));
    return 0;
}

static int hinic3_process_input_port(const struct rte_flow_item* item, enum hinic3_key_type type, struct ds *ds)
{
    const struct rte_flow_item_port_id *port = type == HINIC3_ITEM_SPEC ? item->spec : item->mask;
    if (port == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3_process_input_port: Invalid port id ptr");
        return -1;
    }

    hinic3_ds_put_format(ds, "%s(%u),", HINIC3_UI_KEY_VPORT_STR, port->id);
    return 0;
}

static struct hinic3_item_format_info item_callback_info[] = {
    {RTE_FLOW_ITEM_TYPE_PORT_ID, hinic3_process_input_port},
    {RTE_FLOW_ITEM_TYPE_ETH, hinic3_process_eth},
    {RTE_FLOW_ITEM_TYPE_VLAN, hinic3_process_vlan},
    {RTE_FLOW_ITEM_TYPE_IPV4, hinic3_process_ipv4},
    {RTE_FLOW_ITEM_TYPE_IPV6, hinic3_process_ipv6},
    {RTE_FLOW_ITEM_TYPE_VXLAN, hinic3_process_vxlan},
    {RTE_FLOW_ITEM_TYPE_TCP, hinic3_process_tcp},
    {RTE_FLOW_ITEM_TYPE_UDP, hinic3_process_udp},
    {RTE_FLOW_ITEM_TYPE_SCTP, hinic3_process_sctp},
    {RTE_FLOW_ITEM_TYPE_TAG, hinic3_process_tag_item},
    {RTE_FLOW_ITEM_TYPE_ICMP, hinic3_process_icmp},
    {RTE_FLOW_ITEM_TYPE_ICMP6, hinic3_process_icmp},
    {RTE_FLOW_ITEM_TYPE_ARP_ETH_IPV4, hinic3_process_arp},
    {RTE_FLOW_ITEM_TYPE_END, NULL}
};

static int hinic3_process_count(const struct rte_flow_action* action HINIC3_UNUSED, struct ds *ds)
{
    hinic3_ds_put_format(ds, "%s,", HINIC3_UI_COUNT_ACTION);
    return 0;
}

static int hinic3_process_set_tag(const struct rte_flow_action* action, struct ds *ds)
{
    const struct rte_flow_action_set_tag *tag = action->conf;
    if (action->conf == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3_process_set_tag: Invalid tag action ptr");
        return -1;
    }
    hinic3_ds_put_format(ds, "%s(%u),", HINIC3_UI_TAG_DATA_STR, tag->data);
    hinic3_ds_put_format(ds, "%s(%hhu),", HINIC3_UI_TAG_INDEX_STR, tag->index);
    return 0;
}

static int hinic3_process_acl_remark(const struct rte_flow_action *action, struct ds *ds)
{
    const struct rte_flow_action_modify_field *modify_field = action->conf;
    if (modify_field == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3_process_acl_remark: Invalid modify field ptr");
        return -1;
    }

    uint8_t remark_value = modify_field->src.value[0];
    hinic3_ds_put_format(ds, "%s(%hhu),", HINIC3_UI_REMARK_ACTION, remark_value);
    return 0;
}

static int hinic3_process_acl_sample(const struct rte_flow_action *action, struct ds *ds)
{
    if (action == NULL || action->conf == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3 process acl sample is null.");
        return -1;
    }

    const struct rte_flow_action_sample *sample = action->conf;
    const struct rte_flow_action *act = sample->actions;
    if (act == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3 process acl sample action is null.");
        return -1;
    }

    const struct rte_flow_action_port_id *output_port = (const struct rte_flow_action_port_id *)act->conf;
    if (output_port == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3 process acl sample port id conf is null.");
        return -1;
    }

    uint32_t session_id = output_port->reserved;

    hinic3_ds_put_format(ds, "%s(%u),", HINIC3_UI_SESSION_ID, session_id);
    return 0;
}

static int hinic3_process_push_vlan(const struct rte_flow_action *action HINIC3_UNUSED, struct ds *ds)
{
    hinic3_ds_put_format(ds, "%s,", "push_vlan");
    return 0;
}

static int hinic3_process_pop_vlan(const struct rte_flow_action *action HINIC3_UNUSED, struct ds *ds)
{
    hinic3_ds_put_format(ds, "%s,", "pop_vlan");
    return 0;
}

static int hinic3_process_vxlan_encap(const struct rte_flow_action *action HINIC3_UNUSED, struct ds *ds)
{
    hinic3_ds_put_format(ds, "%s,", "vxlan_encap");
    return 0;
}

static int hinic3_process_vxlan_decap(const struct rte_flow_action *action HINIC3_UNUSED, struct ds *ds)
{
    hinic3_ds_put_format(ds, "%s,", "vxlan_decap");
    return 0;
}

static int hinic3_process_drop(const struct rte_flow_action *action HINIC3_UNUSED, struct ds *ds)
{
    hinic3_ds_put_format(ds, "%s,", "drop");
    return 0;
}

static int hinic3_process_port_id(const struct rte_flow_action *action HINIC3_UNUSED, struct ds *ds)
{
    hinic3_ds_put_format(ds, "%s,", "port_id");
    return 0;
}

static int hinic3_process_set_vid(const struct rte_flow_action *action HINIC3_UNUSED, struct ds *ds)
{
    hinic3_ds_put_format(ds, "%s,", "set vid");
    return 0;
}

static int hinic3_process_set_pcp(const struct rte_flow_action *action HINIC3_UNUSED, struct ds *ds)
{
    hinic3_ds_put_format(ds, "%s,", "set pcp");
    return 0;
}

static struct hinic3_action_format_info action_callback_info[] = {
    {RTE_FLOW_ACTION_TYPE_COUNT, hinic3_process_count},
    {RTE_FLOW_ACTION_TYPE_SET_TAG, hinic3_process_set_tag},
    {RTE_FLOW_ACTION_TYPE_MODIFY_FIELD, hinic3_process_acl_remark},
    {RTE_FLOW_ACTION_TYPE_SAMPLE, hinic3_process_acl_sample},
    {RTE_FLOW_ACTION_TYPE_OF_PUSH_VLAN, hinic3_process_push_vlan},
    {RTE_FLOW_ACTION_TYPE_OF_POP_VLAN, hinic3_process_pop_vlan},
    {RTE_FLOW_ACTION_TYPE_OF_SET_VLAN_VID, hinic3_process_set_vid},
    {RTE_FLOW_ACTION_TYPE_OF_SET_VLAN_PCP, hinic3_process_set_pcp},
    {RTE_FLOW_ACTION_TYPE_VXLAN_ENCAP, hinic3_process_vxlan_encap},
    {RTE_FLOW_ACTION_TYPE_VXLAN_DECAP, hinic3_process_vxlan_decap},
    {RTE_FLOW_ACTION_TYPE_DROP, hinic3_process_drop},
    {RTE_FLOW_ACTION_TYPE_PORT_ID, hinic3_process_port_id},
    {RTE_FLOW_ACTION_TYPE_END, NULL}
};

static int hinic3_get_item_callback(enum rte_flow_item_type type, struct hinic3_item_format_info *item)
{
    int i = 0;
    while (item_callback_info[i].type != RTE_FLOW_ITEM_TYPE_END) {
        if (item_callback_info[i].type == type) {
            *item = item_callback_info[i];
            return 0;
        }
        ++i;
    }
    return -1;
}

static int hinic3_get_action_callback(enum rte_flow_action_type type, struct hinic3_action_format_info *action)
{
    int i = 0;
    while (action_callback_info[i].type != RTE_FLOW_ACTION_TYPE_END) {
        if (action_callback_info[i].type == type) {
            *action = action_callback_info[i];
            return 0;
        }
        ++i;
    }
    return -1;
}

int
hinic3_format_keys(const struct hinic3_flow *flow, enum hinic3_key_type type, struct ds *ds)
{
    int ret;
    struct rte_flow_item item;
    struct hinic3_item_format_info item_info;

    if (type == HINIC3_ITEM_SPEC) {
        hinic3_ds_put_format(ds, "%s", HINIC3_UI_FLOW_KEY_STRING);
    } else if (type == HINIC3_ITEM_MASK) {
        hinic3_ds_put_format(ds, "%s", HINIC3_UI_ITEM_MASK_STRING);
    }

    for (int i = 0; i < HINIC3_FLOW_ITEMS_NUM && flow->items[i].type != RTE_FLOW_ITEM_TYPE_END; ++i) {
        item = flow->items[i];
        ret = hinic3_get_item_callback(item.type, &item_info);
        if (ret != 0) {
            HINIC3_LOG(ERR, FLOW, "dump invalid rte_item_type, type code %d", item.type);
            return -1;
        }

        if (item_info.call_back != NULL) {
            ret = item_info.call_back(&item, type, ds);
            if (ret != 0) {
                HINIC3_LOG(ERR, FLOW, "hinic3 format keys: format call back failed, item type code %d", item.type);
                return -1;
            }
        }
    }
    hinic3_ds_put_format(ds, "%s ", HINIC3_UI_END);
    return 0;
}

static int hinic3_format_actions(const struct hinic3_flow *flow, struct ds *ds)
{
    int ret;
    struct rte_flow_action action;
    struct hinic3_action_format_info action_info;

    hinic3_ds_put_format(ds, "%s", HINIC3_UI_FLOW_ACTION_STRING);
    for (int i = 0; i < HINIC3_FLOW_ACTIONS_NUM && flow->actions[i].type != RTE_FLOW_ACTION_TYPE_END; ++i) {
        action = flow->actions[i];
        ret = hinic3_get_action_callback(action.type, &action_info);
        if (ret != 0) {
            HINIC3_LOG(ERR, FLOW, "dump invalid rte_action_type, type code %d", action.type);
            return -1;
        }

        if (action_info.call_back != NULL) {
            ret = action_info.call_back(&action, ds);
            if (ret != 0) {
                HINIC3_LOG(ERR, FLOW, "hinic3 format keys: format call back failed, action type code %d", action.type);
                return -1;
            }
        }
    }
    hinic3_ds_put_format(ds, "%s ", HINIC3_UI_END);
    return 0;
}

int hinic3_format_flow(const struct hinic3_flow *flow, bool need_mask, struct ds *ds)
{
    int ret;

    ret = hinic3_format_keys(flow, HINIC3_ITEM_SPEC, ds);
    if (ret != 0) {
        HINIC3_LOG(ERR, FLOW, "hinic3_format_one_flow, format item key failed");
        return -1;
    }

    if (need_mask == true) {
        ret = hinic3_format_keys(flow, HINIC3_ITEM_MASK, ds);
        if (ret != 0) {
            HINIC3_LOG(ERR, FLOW, "hinic3_format_one_flow, format item mask failed");
            return -1;
        }
    }

    ret = hinic3_format_actions(flow, ds);
    if (ret != 0) {
        HINIC3_LOG(ERR, FLOW, "hinic3_format_one_flow, format actions failed");
        return -1;
    }
    return 0;
}

int hinic3_flow_construct_key(struct hinic3_flow *flow, enum rte_flow_item_type type, void *spec)
{
    for (int i = 0; i < HINIC3_FLOW_ITEMS_NUM; ++i) {
        if (flow->items[i].type != RTE_FLOW_ITEM_TYPE_END) {
            continue;
        }
        flow->items[i].type = type;
        flow->items[i].spec = spec;
        flow->items[i].mask = NULL;
        return 0;
    }
    return -1;
}

int hinic3_flow_construct_hydra_key(struct hydra_flow_item *geneve,
    enum hydra_item_type type, void *data, int size)
{
    geneve->item_type = type;
    geneve->item_data = data;
    geneve->item_data_size = size;
    geneve->next = NULL;

    return 0;
}

int hinic3_rte_action_insert(struct rte_flow_action dst_actions[], int dst_size,
    const struct rte_flow_action src_actions[], int src_size)
{
    for (int j = 0; j < src_size && src_actions[j].type != RTE_FLOW_ACTION_TYPE_END; ++j) {
        int location = -1;
        for (int i = 0; i < dst_size - 1; ++i) {
            if (dst_actions[i].type == RTE_FLOW_ACTION_TYPE_END) {
                location = i;
                break;
            }
        }

        if (location == -1) {
            return -1;
        }

        dst_actions[location].conf = src_actions[j].conf;
        dst_actions[location].type = src_actions[j].type;
    }
    return 0;
}

static void
hinic3_free_flow_item(struct hinic3_flow *flow)
{
    for (int i = 0; i < HINIC3_FLOW_ITEMS_NUM; ++i) {
        if (flow->items[i].type == RTE_FLOW_ITEM_TYPE_VOID && flow->items[i].spec != NULL) {
            const struct hydra_flow_item* hydra_free = (const struct hydra_flow_item*)flow->items[i].spec;
            if (hydra_free->item_data != NULL)
                hinic3_free((void *)(uintptr_t)hydra_free->item_data);
        } 
        if (flow->items[i].type != RTE_FLOW_ITEM_TYPE_END) {
            if (flow->items[i].spec != NULL) {
                hinic3_free((void *)(uintptr_t)flow->items[i].spec);
                flow->items[i].spec = NULL;
            }
            if (flow->items[i].mask != NULL) {
                hinic3_free((void *)(uintptr_t)flow->items[i].mask);
                flow->items[i].mask = NULL;
            }
            flow->items[i].type = RTE_FLOW_ITEM_TYPE_END;
        }
    }

    return;
}

static void
hinic3_free_flow_action(struct hinic3_flow *flow) {
    for (int i = 0; i < HINIC3_FLOW_ACTIONS_NUM; ++i) {
        if (flow->actions[i].type == RTE_FLOW_ACTION_TYPE_VOID && flow->actions[i].conf != NULL) {
            const struct hydra_flow_action* hydra_free = (const struct hydra_flow_action*)flow->actions[i].conf;
            if (hydra_free->action_data != NULL)
                hinic3_free((void *)(uintptr_t)hydra_free->action_data);
        }
        if (flow->actions[i].type == RTE_FLOW_ACTION_TYPE_CONNTRACK ||
            flow->actions[i].type == RTE_FLOW_ACTION_TYPE_VOID) {
            if (flow->actions[i].conf != NULL) {
                hinic3_free((void *)(uintptr_t)flow->actions[i].conf);
                flow->actions[i].conf = NULL;
            }
            flow->actions[i].type = RTE_FLOW_ACTION_TYPE_END;
        }
    }
}

void
hinic3_free_flow(struct hinic3_flow *flow)
{
    hinic3_free_flow_item(flow);
    hinic3_free_flow_action(flow);
}
