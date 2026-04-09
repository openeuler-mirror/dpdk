/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * Description: hinic3 rte_flow结构体格式化实现
 * Create: 2025-11-11
 */
#include "hinic3_rte_flow_format.h"
#include "hinic3_ui_string.h"
#include "hinic3_log.h"
#include "hinic3_parse_agent_config.h"
#include "hinic3_agent_cmd_format.h"
#include "hinic3_agent_cmd_format_flexda.h"
#include "hinic3_flow_format.h"
#include "hinic3_mega_dump_format.h"
#include "hinic3_offload_flow.h"

typedef void (*hinic3_format_fullkey_callback) (const struct hinic3_conntrack_key *key, struct hinic3_nlattr *attr);

struct hinic3_format_fullkey_callback_info {
    hinic3_format_fullkey_callback callback;
    const char *str;
};

static void
hinic3_process_fullkey_eth(const struct hinic3_conntrack_key *key, struct hinic3_nlattr *values)
{
    const struct hinic3_key_mac *mac_tuple = (const struct hinic3_key_mac *)key->key;
    if (hinic3_card_mod_get() == PROG_MODE) {
        hinic3_nlattr_put_unspec(values, HINIC3_FLOW_KEY_DEF_ETH_DMAC, mac_tuple->dmac, sizeof(mac_tuple->dmac));
        hinic3_nlattr_put_unspec(values, HINIC3_FLOW_KEY_DEF_ETH_SMAC, mac_tuple->smac, sizeof(mac_tuple->smac));
    } else {
        hinic3_nlattr_put_unspec(values, HINIC3_FLOW_KEY_DST_MAC, mac_tuple->dmac, sizeof(mac_tuple->dmac));
        hinic3_nlattr_put_unspec(values, HINIC3_FLOW_KEY_SRC_MAC, mac_tuple->smac, sizeof(mac_tuple->smac));
    }
}

static void
hinic3_process_fullkey_vxlan(const struct hinic3_conntrack_key *key, struct hinic3_nlattr *values)
{
    const struct hinic3_key_vxlan *vxlan = &(((const struct hinic3_key_vxlan_vid_5tuple *)key->key)->vxlan);
    if (hinic3_card_mod_get() == PROG_MODE) {
        hinic3_nlattr_put_unspec(values, HINIC3_FLOW_KEY_DPDK_VXLAN, (const void *)&(vxlan->vni), HINIC3_VXLAN_VNI_DATA_SIZE);
    } else {
        hinic3_nlattr_put_u32(values, HINIC3_FLOW_KEY_VNI, vxlan->vni);
    }
}

static void
hinic3_process_fullkey_vlan(const struct hinic3_conntrack_key *key, struct hinic3_nlattr *values)
{
    uint16_t outer_vid = ((const struct hinic3_key_vid_5tuple *)key->key)->outer_vid;
    if (hinic3_card_mod_get() == PROG_MODE) {
        if (!hinic3_support_vlan_tci_get())
            hinic3_nlattr_put_u16(values, HINIC3_FLOW_KEY_OUTER_VID, outer_vid);
        else
            hinic3_nlattr_put_u16(values, HINIC3_FLOW_KEY_OUTER_TCI, outer_vid);
    } else {
        hinic3_nlattr_put_u16(values, HINIC3_FLOW_KEY_DEF_VLAN_TCI, outer_vid);
    }
}

static void
hinic3_process_fullkey_ipv4(const struct hinic3_conntrack_key *key, struct hinic3_nlattr *values)
{
    const struct hinic3_key_5tuple *tuple = (const struct hinic3_key_5tuple *)key->key;
    if (hinic3_card_mod_get() == PROG_MODE) {
        hinic3_nlattr_put_u32(values, HINIC3_FLOW_KEY_DEF_IPV4_SIP, tuple->ip.src.ipv4);
        hinic3_nlattr_put_u32(values, HINIC3_FLOW_KEY_DEF_IPV4_DIP, tuple->ip.dst.ipv4);
        hinic3_nlattr_put_u8(values, HINIC3_FLOW_KEY_DEF_IPV4_PROTOCOL, key->meta.protocol);
    } else {
        hinic3_nlattr_put_u16(values, HINIC3_FLOW_KEY_DL_TYPE, tuple->ip.ether_type);
        hinic3_nlattr_put_u32(values, HINIC3_FLOW_KEY_SRC_IP, tuple->ip.src.ipv4);
        hinic3_nlattr_put_u32(values, HINIC3_FLOW_KEY_DST_IP, tuple->ip.dst.ipv4);
    }
}

static void
hinic3_process_fullkey_ipv6(const struct hinic3_conntrack_key *key, struct hinic3_nlattr *values)
{
    const struct hinic3_key_5tuple *tuple = (const struct hinic3_key_5tuple *)key->key;
    if (hinic3_card_mod_get() == PROG_MODE) {
        hinic3_nlattr_put_unspec(values, HINIC3_FLOW_KEY_DEF_IPV6_SIP, &tuple->ip.src.ipv6,
            sizeof(tuple->ip.src.ipv6));
        hinic3_nlattr_put_unspec(values, HINIC3_FLOW_KEY_DEF_IPV6_DIP, &tuple->ip.dst.ipv6,
            sizeof(tuple->ip.dst.ipv6));
        hinic3_nlattr_put_u8(values, HINIC3_FLOW_KEY_DEF_IPV6_PROTOCOL, key->meta.protocol);
    } else {
        hinic3_nlattr_put_u16(values, HINIC3_FLOW_KEY_DL_TYPE, tuple->ip.ether_type);
        hinic3_nlattr_put_unspec(values, HINIC3_FLOW_KEY_SRC_IPV6, &tuple->ip.src.ipv6, sizeof(tuple->ip.src.ipv6));
        hinic3_nlattr_put_unspec(values, HINIC3_FLOW_KEY_DST_IPV6, &tuple->ip.dst.ipv6, sizeof(tuple->ip.dst.ipv6));
    }
}

static void
hinic3_process_fullkey_gre(const struct hinic3_conntrack_key *key HINIC3_UNUSED, struct hinic3_nlattr *values HINIC3_UNUSED)
{
    return;
}

static void
hinic3_process_fullkey_gre_key(const struct hinic3_conntrack_key *key HINIC3_UNUSED, struct hinic3_nlattr *values HINIC3_UNUSED)
{
    return;
}

static void
hinic3_process_fullkey_tcp(const struct hinic3_conntrack_key *key, struct hinic3_nlattr *values)
{
    const struct hinic3_key_5tuple *tuple = (const struct hinic3_key_5tuple *)key->key;
    if (hinic3_card_mod_get() == PROG_MODE) {
        hinic3_nlattr_put_u16(values, HINIC3_FLOW_KEY_DEF_TCP_SPORT, tuple->port_src);
        hinic3_nlattr_put_u16(values, HINIC3_FLOW_KEY_DEF_TCP_DPORT, tuple->port_dst);
    } else {
        if (key->meta.tcp_udp_flag == HINIC3_CT_DEFAULT_FLAG) {
            const struct hinic3_key_raw_ip *raw_ip = (const struct hinic3_key_raw_ip *)key->key;
            hinic3_nlattr_put_u8(values, HINIC3_FLOW_KEY_PROTOCOL, raw_ip->protocol);
        } else {
            hinic3_nlattr_put_u16(values, HINIC3_FLOW_KEY_SRC_PORT, tuple->port_src);
            hinic3_nlattr_put_u16(values, HINIC3_FLOW_KEY_DST_PORT, tuple->port_dst);
            hinic3_nlattr_put_u8(values, HINIC3_FLOW_KEY_PROTOCOL, hinic3_ct_get_proto_by_flag(key->meta.tcp_udp_flag));
        }
    }
}

static void
hinic3_process_fullkey_udp(const struct hinic3_conntrack_key *key, struct hinic3_nlattr *values)
{
    const struct hinic3_key_5tuple *tuple = (const struct hinic3_key_5tuple *)key->key;
    if (hinic3_card_mod_get() == PROG_MODE) {
        hinic3_nlattr_put_u16(values, HINIC3_FLOW_KEY_DEF_UDP_SPORT, tuple->port_src);
        hinic3_nlattr_put_u16(values, HINIC3_FLOW_KEY_DEF_UDP_DPORT, tuple->port_dst);
    } else {
        if (key->meta.tcp_udp_flag == HINIC3_CT_DEFAULT_FLAG) {
            const struct hinic3_key_raw_ip *raw_ip = (const struct hinic3_key_raw_ip *)key->key;
            hinic3_nlattr_put_u8(values, HINIC3_FLOW_KEY_PROTOCOL, raw_ip->protocol);
        } else {
            hinic3_nlattr_put_u16(values, HINIC3_FLOW_KEY_SRC_PORT, tuple->port_src);
            hinic3_nlattr_put_u16(values, HINIC3_FLOW_KEY_DST_PORT, tuple->port_dst);
            hinic3_nlattr_put_u8(values, HINIC3_FLOW_KEY_PROTOCOL, hinic3_ct_get_proto_by_flag(key->meta.tcp_udp_flag));
        }
    }
}

static void
hinic3_process_fullkey_port(const struct hinic3_conntrack_key *key, struct hinic3_nlattr *values)
{
    const struct hinic3_key_vxlan_vid_5tuple *port_key = (const struct hinic3_key_vxlan_vid_5tuple *)key->key;
    if (hinic3_card_mod_get() == PROG_MODE) {
        hinic3_nlattr_put_u16(values, HINIC3_FLOW_KEY_DPDK_PORT_ID, port_key->input_port);
    } else {
        hinic3_nlattr_put_u16(values, HINIC3_FLOW_KEY_IN_PORT, port_key->input_port);
    }
}

static void
hinic3_process_fullkey_raw_protocol(const struct hinic3_conntrack_key *key HINIC3_UNUSED, struct hinic3_nlattr *values HINIC3_UNUSED)
{
    return;
}

static void
hinic3_process_fullkey_icmp(const struct hinic3_conntrack_key *key, struct hinic3_nlattr *values)
{
    const struct hinic3_key_5tuple *tuple = (const struct hinic3_key_5tuple *)key->key;
    const struct hinic3_key_raw_ip *raw_ip = (const struct hinic3_key_raw_ip *)key->key;
    if (hinic3_card_mod_get() == PROG_MODE) {
        uint8_t icmp_type = ((tuple->port_src & HINIC3_ICMP_TYPE_MASK) >> HINIC3_BIT_MID_MOVE_INDEX);
        uint8_t icmp_code = (tuple->port_src & HINIC3_ICMP_CODE_MASK);
        uint16_t icmp_id = tuple->port_dst;
        hinic3_nlattr_put_u8(values, HINIC3_FLOW_KEY_DEF_ICMP_TYPE, icmp_type);
        hinic3_nlattr_put_u8(values, HINIC3_FLOW_KEY_DEF_ICMP_CODE, icmp_code);
        hinic3_nlattr_put_u16(values, HINIC3_FLOW_KEY_DEF_ICMP_IDENT, icmp_id);
    } else {
        if (key->meta.tcp_udp_flag == HINIC3_CT_DEFAULT_FLAG) {
            hinic3_nlattr_put_u8(values, HINIC3_FLOW_KEY_PROTOCOL, raw_ip->protocol);
        } else {
            hinic3_nlattr_put_u16(values, HINIC3_FLOW_KEY_SRC_PORT, tuple->port_src);
            hinic3_nlattr_put_u16(values, HINIC3_FLOW_KEY_DST_PORT, tuple->port_dst);
            hinic3_nlattr_put_u8(values, HINIC3_FLOW_KEY_PROTOCOL, hinic3_ct_get_proto_by_flag(key->meta.tcp_udp_flag));
        }
    }
}

static void
hinic3_process_fullkey_icmp6(const struct hinic3_conntrack_key *key, struct hinic3_nlattr *values)
{
    const struct hinic3_key_5tuple *tuple = (const struct hinic3_key_5tuple *)key->key;
    const struct hinic3_key_raw_ip *raw_ip = (const struct hinic3_key_raw_ip *)key->key;
    if (hinic3_card_mod_get() == PROG_MODE) {
        uint8_t icmp_type = ((tuple->port_src & HINIC3_ICMP_TYPE_MASK) >> HINIC3_BIT_MID_MOVE_INDEX);
        uint8_t icmp_code = (tuple->port_src & HINIC3_ICMP_CODE_MASK);
        uint16_t icmp_id = tuple->port_dst;
        hinic3_nlattr_put_u8(values, HINIC3_FLOW_KEY_DEF_ICMP6_TYPE, icmp_type);
        hinic3_nlattr_put_u8(values, HINIC3_FLOW_KEY_DEF_ICMP6_CODE, icmp_code);
        hinic3_nlattr_put_u16(values, HINIC3_FLOW_KEY_DEF_ICMP_IDENT, icmp_id);
    } else {
        if (key->meta.tcp_udp_flag == HINIC3_CT_DEFAULT_FLAG) {
            hinic3_nlattr_put_u8(values, HINIC3_FLOW_KEY_PROTOCOL, raw_ip->protocol);
        } else {
            hinic3_nlattr_put_u16(values, HINIC3_FLOW_KEY_SRC_PORT, tuple->port_src);
            hinic3_nlattr_put_u16(values, HINIC3_FLOW_KEY_DST_PORT, tuple->port_dst);
            hinic3_nlattr_put_u8(values, HINIC3_FLOW_KEY_PROTOCOL, hinic3_ct_get_proto_by_flag(key->meta.tcp_udp_flag));
        }
    }
}

static void
hinic3_process_fullkey_hydra_items(const struct hinic3_conntrack_key *key, struct hinic3_nlattr *values)
{
    hinic3_flow_agent_construct_hydra_key(values, key);
}

static struct hinic3_format_fullkey_callback_info callback_infos[] = {
    [HINIC3_KEY_FLAG_TYPE_ETH] = {hinic3_process_fullkey_eth, "eth"},
    [HINIC3_KEY_FLAG_TYPE_VXLAN] = {hinic3_process_fullkey_vxlan, "vxlan"},
    [HINIC3_KEY_FLAG_TYPE_VLAN] = {hinic3_process_fullkey_vlan, "vlan"},
    [HINIC3_KEY_FLAG_TYPE_OUTER_IPV4] = {hinic3_process_fullkey_ipv4, "outer ipv4"},
    [HINIC3_KEY_FLAG_TYPE_OUTER_IPV6] = {hinic3_process_fullkey_ipv6, "outer ipv6"},
    [HINIC3_KEY_FLAG_TYPE_IPV4] = {hinic3_process_fullkey_ipv4, "ipv4"},
    [HINIC3_KEY_FLAG_TYPE_IPV6] = {hinic3_process_fullkey_ipv6, "ipv6"},
    [HINIC3_KEY_FLAG_TYPE_GRE] = {hinic3_process_fullkey_gre, "gre"},
    [HINIC3_KEY_FLAG_TYPE_GRE_KEY] = {hinic3_process_fullkey_gre_key, "gre key"},
    [HINIC3_KEY_FLAG_TYPE_TCP] = {hinic3_process_fullkey_tcp, "tcp"},
    [HINIC3_KEY_FLAG_TYPE_UDP] = {hinic3_process_fullkey_udp, "udp"},
    [HINIC3_KEY_FLAG_TYPE_PORT] = {hinic3_process_fullkey_port, "port"},
    [HINIC3_KEY_FLAG_TYPE_RAW_PROTOCOL] = {hinic3_process_fullkey_raw_protocol, "raw protocol"},
    [HINIC3_KEY_FLAG_TYPE_ICMP] = {hinic3_process_fullkey_icmp, "icmp"},
    [HINIC3_KEY_FLAG_TYPE_ICMP6] = {hinic3_process_fullkey_icmp6, "icmp6"},
    [HINIC3_KEY_FLAG_TYPE_END] = {NULL, "null"},
};

static inline void
hinic3_format_rte_flow_ufid(uint64_t ufid, struct ds *ds)
{
    hinic3_ds_put_format(ds, "%s", HINIC3_UI_FLOW_UFID_STRING);
    hinic3_ds_put_format(ds, HINIC3_HW_UFID_FMT, HINIC3_HW_UFID_ARGS(ufid));
    hinic3_ds_put_format(ds, ", ");
}

static inline void
hinic3_format_offload_status(uint32_t offload, struct ds *ds)
{
    hinic3_ds_put_format(ds, "%s: %u,", HINIC3_UI_FLOW_OFFLOAD_STRING, offload);
}

static void
hinic3_print_fullkey_flags_info(uint32_t flags)
{
    struct ds ds = DS_EMPTY_INITIALIZER;
    for (size_t i = 0; i < HINIC3_KEY_FLAG_TYPE_END; ++i) {
        enum hinic3_conntrack_key_flags_type type = (enum hinic3_conntrack_key_flags_type)i;
		const char* has = hinic3_full_key_flag_get_at(flags, type) ? "1" : "0";
        hinic3_ds_put_format(&ds, "%d.[%s]:%s, ", i, callback_infos[type].str, has);
    }
    HINIC3_LOG(DEBUG, FLOW, "fullkey: %s.", hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
}

static void 
hinic3_process_fullkey_items(const struct hinic3_conntrack_key *key, struct hinic3_nlattr *values)
{
    uint32_t flags = key->hdr_flags_num;
    for (size_t i = 0; i < HINIC3_KEY_FLAG_TYPE_END; ++i) {
        enum hinic3_conntrack_key_flags_type type = (enum hinic3_conntrack_key_flags_type)i;
        if (hinic3_full_key_flag_get_at(flags, type)) {
            callback_infos[type].callback(key, values);
        }
    }
    if (HINIC3_UNLIKELY(hinic3_is_flow_debug()))
        hinic3_print_fullkey_flags_info(flags);
    if (hinic3_card_mod_get() == PROG_MODE)
        hinic3_process_fullkey_hydra_items(key, values);
}

void
hinic3_format_conntrack_fullkey(const struct hinic3_conntrack_full_key *fullkey, struct ds *ds)
{
    int type = fullkey->key.meta.type;
    if (type >= HINIC3_KEY_END)
        return;
    struct hinic3_nlattr key;
    uint8_t key_buf[HINIC3_MSG_MAX_BUF];
    hinic3_nlattr_init(&key, key_buf, HINIC3_MSG_MAX_BUF);
    hinic3_ds_put_format(ds, "%s%d, ", HINIC3_UI_FLOW_CONNTRACK_KEY_TUPLE_TYPE_STRING, type);
    hinic3_ds_put_format(ds, "%s%d, ", HINIC3_UI_FLOW_KEY_FLAG_STRING, fullkey->key.hdr_flags_num);
    hinic3_ds_put_format(ds, "%s", HINIC3_UI_FLOW_KEY_STRING);
    
    hinic3_process_fullkey_items(&fullkey->key, &key);

    if (hinic3_card_mod_get() == PROG_MODE)
        hinic3_flexda_flow_key_format_output(&key, ds);
    else
        hinic3_flow_key_format_output(&key, ds);
    hinic3_ds_put_format(ds, " END ");
}

void
hinic3_format_rte_flow(const struct rte_flow *flow, struct ds *ds)
{
    const struct hinic3_conntrack_full_key *key = &flow->key;
    uint8_t table_id = flow->table_id;
    hinic3_format_rte_flow_ufid(flow->hw_ufid, ds);
    hinic3_ds_put_format(ds, "%s: %u,", HINIC3_UI_FLOW_OFFLOAD_STRING, flow->flags.is_offload);
    hinic3_ds_put_format(ds, "%s%d, ", HINIC3_UI_FLOW_CONNTRACK_KEY_TUPLE_TYPE_STRING, key->key.meta.type);
    hinic3_ds_put_format(ds, "%s%d, ", HINIC3_UI_FLOW_KEY_FLAG_STRING, key->key.hdr_flags_num);
    hinic3_ds_put_format(ds, "%s", HINIC3_UI_FLOW_KEY_STRING);
    hinic3_format_conntrack_fullkey(key, ds);
    if (IS_FLEXDA_FUZZY_TABLE(table_id)) {
        const struct fuzzy_flow* fuzzy_flow = (const struct fuzzy_flow*)flow;
        const struct hinic3_conntrack_full_key *mask = &fuzzy_flow->mask;
        const struct hinic3_conntrack_full_key *raw = &fuzzy_flow->raw_key;
        hinic3_ds_put_format(ds, "%s", HINIC3_UI_FLOW_MASK_STRING);
        hinic3_format_conntrack_fullkey(mask, ds);
        hinic3_ds_put_format(ds, "%s", HINIC3_UI_FLOW_RAW_KEY_STRING);
        hinic3_format_conntrack_fullkey(raw, ds);
    }
    hinic3_ds_put_format(ds, "\n");
}

static const void* hinic3_get_mega_flow_item(const struct hinic3_mega_key *key, enum rte_flow_item_type type)
{
    switch (type) {
        case RTE_FLOW_ITEM_TYPE_PORT_ID:
            return (const void*)&key->in_port;
        case RTE_FLOW_ITEM_TYPE_ETH:
            return (const void*)&key->eth;
        case RTE_FLOW_ITEM_TYPE_VXLAN:
            return (const void*)&key->vxlan;
        default:
            return NULL;
    }
    return NULL;
}

static inline void
hinic3_format_rte_flow_mage_index(uint32_t index, struct ds *ds)
{
    hinic3_ds_put_format(ds, "%s:%u", HINIC3_UI_FLOW_MEGA_INDEX_STRING, index);
}

int
hinic3_format_rte_flow_mega(const struct hinic3_mega_flow *flow, struct ds *ds)
{
    struct hinic3_flow format;
    hinic3_format_rte_flow_ufid(flow->ufid, ds);
    hinic3_format_rte_flow_mage_index(flow->index, ds);
    const struct hinic3_mega_flow_full_key* key = &flow->full_key;
    size_t i = 0;
    for (; i < key->item_num; ++i) {
        enum rte_flow_item_type type = key->item_types[i];
        format.items[i].type = type;
        format.items[i].spec = hinic3_get_mega_flow_item(&key->mega_key, type);
        format.items[i].mask = hinic3_get_mega_flow_item(&key->mask, type);
    }
    format.items[i].type = RTE_FLOW_ITEM_TYPE_END;
    format.items[i].spec = NULL;
    format.items[i].mask = NULL;
    return hinic3_format_mega_flow_items(&format, ds);
}

void
hinic3_log_fullkey_nlattr(const char* str, const struct hinic3_nlattr *key)
{
    struct ds ds = DS_EMPTY_INITIALIZER;
    if (hinic3_card_mod_get() == PROG_MODE)
        hinic3_flexda_flow_key_format_output(key, &ds);
    else
        hinic3_flow_key_format_output(key, &ds);
    HINIC3_LOG(INFO, FLOW, "%s key_nlattr : %s", str, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
}

bool
hinic3_get_dump_rte_flow_options(int argc, const char *argv[])
{
    for (int i = argc; i > 0; --i) {
        if (strcmp("-r", argv[i - 1]) == 0) {
            return true;
        }
    }
    return false;
}

void hinic3_log_fullkey(const char* str, const struct hinic3_conntrack_full_key *fullkey)
{
    struct ds ds = DS_EMPTY_INITIALIZER;
    hinic3_format_conntrack_fullkey(fullkey, &ds);
    HINIC3_LOG(INFO, FLOW, "%s fullkey : %s", str, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
}

