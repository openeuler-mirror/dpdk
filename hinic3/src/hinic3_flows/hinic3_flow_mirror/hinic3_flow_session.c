 /* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Huawei Technologies Co., Ltd
 */

#include "hinic3_list.h"
#include "hinic3_init.h"
#include "hinic3_bond_controller.h"
#include "hinic3_packet_key_public.h"
#include "hinic3_flow_mirror.h"
#include "hinic3_iface_flow.h"
#include "hinic3_flow_agent.h"
#include "hinic3_agent_cmd_format.h"
#include "hinic3_ui_string.h"
#include "hinic3_meminfo.h"
#include "hinic3_ds.h"
#include "hinic3_command.h"
#include "hinic3_flow_session.h"

#define HINIC3_DUMP_NVGRE_MAX_ITEMS 5
#define HINIC3_SAMPLING_INTERVAL_MAX 1023
#define HINIC3_SHIM_HEADER_LEN_MAX 20

static struct hinic3_mirror_session_info_list g_mirror_session_info;

struct hinic3_mirror_session_info_list *hinic3_get_mirror_session_info(void)
{
    return &g_mirror_session_info;
}

static uint32_t hinic3_show_session_u8_to_u32(uint8_t *src_array, int length)
{
    uint32_t target_value = 0;

    for (int i = 0; i < length; i++) {
        target_value <<= HINIC3_UINT8_SHIFT;
        target_value += src_array[i];
    }
    return target_value;
}

void ovs_mutex_unlock_session(void)
{
    hinic3_spinlock_unlock(&g_mirror_session_info.mutex);
}

void ovs_mutex_lock_session(void)
{
    hinic3_spinlock_lock(&g_mirror_session_info.mutex);
}

enum hinic3_session_tpye hinic3_get_session_type(uint8_t session_id)
{
    return g_mirror_session_info.session_info[session_id].type;
}

static int hinic3_dump_nvgre_vlan_item(struct rte_flow_item *item, uint8_t session_id)
{
    struct hinic3_mirror_session_info session_info = g_mirror_session_info.session_info[session_id];
    struct rte_flow_item_vlan *vlan = hinic3_calloc(1, sizeof(struct rte_flow_item_vlan), HINIC3_FLOWS);
    if (vlan == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3 flow dump, calloc vlan item fail.");
        return -1;
    }
    vlan->tci = session_info.key.tci;
    vlan->inner_type = session_info.key.inner_type;

    item->type = RTE_FLOW_ITEM_TYPE_VLAN;
    item->spec = vlan;
    item->last = NULL;
    item->mask = NULL;
    return 0;
}

static int hinic3_dump_nvgre_ipv4_item(struct rte_flow_item *item, uint8_t session_id)
{
    struct hinic3_mirror_session_info session_info = g_mirror_session_info.session_info[session_id];
    struct rte_flow_item_ipv4 *ipv4 = hinic3_calloc(1, sizeof(struct rte_flow_item_ipv4), HINIC3_FLOWS);
    if (ipv4 == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3 flow dump, calloc ipv4 item fail.");
        return -1;
    }
    ipv4->hdr.dst_addr = session_info.key.dip[0];
    ipv4->hdr.src_addr = session_info.key.sip[0];
    ipv4->hdr.next_proto_id = session_info.key.proto;

    item->type = RTE_FLOW_ITEM_TYPE_IPV4;
    item->spec = ipv4;
    item->last = NULL;
    item->mask = NULL;
    return 0;
}

static int hinic3_dump_nvgre_ipv6_item(struct rte_flow_item *item, uint8_t session_id)
{
    struct hinic3_mirror_session_info session_info = g_mirror_session_info.session_info[session_id];
    struct rte_flow_item_ipv6 *ipv6 = hinic3_calloc(1, sizeof(struct rte_flow_item_ipv6), HINIC3_FLOWS);
    if (ipv6 == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3 flow dump, calloc ipv6 item fail.");
        return -1;
    }

    memcpy(ipv6->hdr.dst_addr, session_info.key.dip, HINIC3_IPV6_ADDR_UINT8_LEN);
    memcpy(ipv6->hdr.src_addr, session_info.key.sip, HINIC3_IPV6_ADDR_UINT8_LEN);
    ipv6->hdr.proto = session_info.key.proto;

    item->type = RTE_FLOW_ITEM_TYPE_IPV6;
    item->spec = ipv6;
    item->last = NULL;
    item->mask = NULL;

    return 0;
}

static int hinic3_dump_nvgre_eth_item(struct rte_flow_item *item, uint8_t session_id)
{
    struct hinic3_mirror_session_info session_info = g_mirror_session_info.session_info[session_id];
    struct rte_flow_item_eth *eth = hinic3_calloc(1, sizeof(struct rte_flow_item_eth), HINIC3_FLOWS);
    if (eth == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3 flow dump, calloc eth item fail.");
        return -1;
    }

    memcpy(&eth->dst, session_info.key.dmac, ETH_ALEN);
    memcpy(&eth->src, session_info.key.smac, ETH_ALEN);
    eth->type = session_info.key.type;

    item->type = RTE_FLOW_ITEM_TYPE_ETH;
    item->spec = eth;
    item->last = NULL;
    item->mask = NULL;

    return 0;
}

static int hinic3_dump_nvgre_gre_item(struct rte_flow_item *item, uint8_t session_id)
{
    struct hinic3_mirror_session_info session_info = g_mirror_session_info.session_info[session_id];
    struct rte_flow_item_gre *gre = hinic3_calloc(1, sizeof(struct rte_flow_item_gre), HINIC3_FLOWS);
    if (gre == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3 flow dump, calloc nvgre item fail.");
        return -1;
    }

    gre->c_rsvd0_ver = session_info.key.tunnel.gre.c_rsvd0_ver;
    gre->protocol = session_info.key.tunnel.gre.protocol;

    item->type = RTE_FLOW_ITEM_TYPE_GRE;
    item->spec = gre;
    item->last = NULL;
    item->mask = NULL;

    return 0;
}

static int hinic3_flow_dump_construct_nvgre_item(struct rte_flow_item *items, uint8_t session_id)
{
    int ret;
    struct hinic3_mirror_session_info session_info = g_mirror_session_info.session_info[session_id];
    if (session_info.ip_type_flag == HINIC3_FLG_MIRROR_IPV4_VLAN_GRE ||
        session_info.ip_type_flag == HINIC3_FLG_MIRROR_IPV6_VLAN_GRE) {
        ret = hinic3_dump_nvgre_vlan_item(items, session_id);
        if (ret != 0) {
            return -1;
        }
        items++;
    }

    if (session_info.ip_type_flag == HINIC3_FLG_MIRROR_IPV4_GRE ||
        session_info.ip_type_flag == HINIC3_FLG_MIRROR_IPV4_VLAN_GRE) {
        ret = hinic3_dump_nvgre_ipv4_item(items, session_id);
        if (ret != 0) {
            return -1;
        }
        items++;
    } else {
        ret = hinic3_dump_nvgre_ipv6_item(items, session_id);
        if (ret != 0) {
            return -1;
        }
        items++;
    }

    ret = hinic3_dump_nvgre_eth_item(items, session_id);
    if (ret != 0) {
        return -1;
    }
    items++;

    ret = hinic3_dump_nvgre_gre_item(items, session_id);
    if (ret != 0) {
        return -1;
    }
    items++;
    items->type = RTE_FLOW_ITEM_TYPE_END;

    return 0;
}

struct rte_flow_action_vxlan_encap *hinic3_flow_dump_sample_nvgre_encap_conf(uint8_t session_id)
{
    int ret;
    struct rte_flow_action_vxlan_encap *nvgre_encap = hinic3_calloc(1,
        sizeof(struct rte_flow_action_vxlan_encap), HINIC3_FLOWS);
    if (nvgre_encap == NULL) {
        HINIC3_LOG(ERR, FLOW, "FLOW DUMP: malloc for nvgre encap failed.");
        return NULL;
    }

    struct rte_flow_item *nvgre_items =
        hinic3_calloc(HINIC3_DUMP_NVGRE_MAX_ITEMS, sizeof(struct rte_flow_item), HINIC3_FLOWS);
    if (nvgre_items == NULL) {
        hinic3_free(nvgre_encap);
        HINIC3_LOG(ERR, FLOW, "FLOW DUMP: malloc for nvgre_items failed");
        return NULL;
    }

    ret = hinic3_flow_dump_construct_nvgre_item(nvgre_items, session_id);
    if (ret != 0) {
        HINIC3_LOG(ERR, FLOW, "FLOW DUMP: get nvgre encap failed");
        goto err;
    }

    nvgre_encap->definition = nvgre_items;
    return nvgre_encap;

err:
    for (int i = 0; i < HINIC3_DUMP_NVGRE_MAX_ITEMS; i++) {
        if (nvgre_items[i].spec != NULL) {
            hinic3_free((void*)(uintptr_t)nvgre_items[i].spec);
        }
    }
    hinic3_free(nvgre_items);
    hinic3_free(nvgre_encap);
    return NULL;
}

static void hinic3_show_session_key(struct ds *ds, struct session_key *key)
{
    hinic3_ds_put_format(ds, "%2ssmac:(" HINIC3_MAC_FMT ")\n", HINIC3_UI_INDENT_SPACE, HINIC3_OUTPUT_MAC(&key->smac[0]));
    hinic3_ds_put_format(ds, "%2sdmac:(" HINIC3_MAC_FMT ")\n", HINIC3_UI_INDENT_SPACE, HINIC3_OUTPUT_MAC(&key->dmac[0]));
    hinic3_ds_put_format(ds, "%2souter_port_id:%u\n", HINIC3_UI_INDENT_SPACE, key->outer_port_id);
    hinic3_ds_put_format(ds, "%2seth_type:%X\n", HINIC3_UI_INDENT_SPACE, htons(key->type));
    hinic3_ds_put_format(ds, "%2svlan_tci:%u\n", HINIC3_UI_INDENT_SPACE, htons(key->tci));
    hinic3_ds_put_format(ds, "%2svlan_type:%X\n", HINIC3_UI_INDENT_SPACE, htons(key->inner_type));
}

static void hinic3_show_session_ipv4(struct ds *ds, struct session_key *key)
{
    hinic3_ds_put_format(ds, "%2ssip:(" IP_FMT ")\n", HINIC3_UI_INDENT_SPACE, IP_ARGS(key->sip[0]));
    hinic3_ds_put_format(ds, "%2sdip:(" IP_FMT ")\n", HINIC3_UI_INDENT_SPACE, IP_ARGS(key->dip[0]));
    hinic3_ds_put_format(ds, "%2snext_proto_id:%u\n", HINIC3_UI_INDENT_SPACE, key->proto);
}

static void hinic3_show_session_ipv6(struct ds *ds, struct session_key *key)
{
    hinic3_ds_put_format(ds, "%2ssip:(" IPV6_FMT ")\n", HINIC3_UI_INDENT_SPACE, IPV6_ARGS(key->sip));
    hinic3_ds_put_format(ds, "%2sdip:(" IPV6_FMT ")\n", HINIC3_UI_INDENT_SPACE, IPV6_ARGS(key->dip));
    hinic3_ds_put_format(ds, "%2sproto:%u\n", HINIC3_UI_INDENT_SPACE, key->proto);
}

static void hinic3_show_session_vxlan(struct ds *ds, struct session_key *key)
{
    uint32_t vni;
    uint32_t vxlan_rsvd0;
    vni = hinic3_show_session_u8_to_u32(key->tunnel.vxlan.vni, HINIC3_VNI_LEN);
    vxlan_rsvd0 = hinic3_show_session_u8_to_u32(key->tunnel.vxlan.vxlan_rsvd0, HINIC3_VXLAN_RSVD0_LEN);
    hinic3_ds_put_format(ds, "%2svxlan_vni:%u\n", HINIC3_UI_INDENT_SPACE, vni);
    hinic3_ds_put_format(ds, "%2svxlan_rsvd0:%u\n", HINIC3_UI_INDENT_SPACE, vxlan_rsvd0);
}

static void hinic3_show_session_gre(struct ds *ds, struct session_key *key)
{
    hinic3_ds_put_format(ds, "%2sc_k_s_rsvd0_ver:%u\n", HINIC3_UI_INDENT_SPACE, key->tunnel.gre.c_rsvd0_ver);
    hinic3_ds_put_format(ds, "%2sprotocol:%X\n", HINIC3_UI_INDENT_SPACE, htons(key->tunnel.gre.protocol));
}

static void hinic3_show_session_vxlan_gpe_shim(struct ds *ds, struct session_key *key)
{
    uint32_t vni;
    uint32_t vxlan_rsvd0;
    uint8_t flags;
    uint8_t next_protocol;
    uint8_t shim_len;
    uint8_t shim_type;

    vni = hinic3_show_session_u8_to_u32(key->tunnel.vxlan_gpe_shim.vni, HINIC3_VNI_LEN);
    vxlan_rsvd0 = hinic3_show_session_u8_to_u32(key->tunnel.vxlan_gpe_shim.vxlan_rsvd0, HINIC3_VXLAN_RSVD0_LEN);
    flags = key->tunnel.vxlan_gpe_shim.flags;
    next_protocol = key->tunnel.vxlan_gpe_shim.next_protocol;
    shim_len = key->tunnel.vxlan_gpe_shim.shim_len;
    shim_type = key->tunnel.vxlan_gpe_shim.shim_type;

    hinic3_ds_put_format(ds, "%2sflags:0x%02x\n", HINIC3_UI_INDENT_SPACE, flags);
    hinic3_ds_put_format(ds, "%2snext_protocol:0x%02x\n", HINIC3_UI_INDENT_SPACE, next_protocol);
    hinic3_ds_put_format(ds, "%2svxlan_vni:%u\n", HINIC3_UI_INDENT_SPACE, vni);
    hinic3_ds_put_format(ds, "%2svxlan_rsvd0:%u\n", HINIC3_UI_INDENT_SPACE, vxlan_rsvd0);    
    hinic3_ds_put_format(ds, "%2sshim_len:%u\n", HINIC3_UI_INDENT_SPACE, shim_len);
    hinic3_ds_put_format(ds, "%2sshim_type:0x%02x\n", HINIC3_UI_INDENT_SPACE, shim_type);
    hinic3_ds_put_format(ds, "%2sshim_header:", HINIC3_UI_INDENT_SPACE);
    for (int i = 0; i < 5; i++) {
        hinic3_ds_put_format(ds, " %08x", ntohl(key->tunnel.vxlan_gpe_shim.data[i]));
    }
    hinic3_ds_put_format(ds, "\n");
}

static void hinic3_show_session_type(struct ds *ds, enum hinic3_session_tpye type)
{
    switch (type) {
        case HINIC3_EMC_VXLAN_SESSION :
            hinic3_ds_put_format(ds, "%2ssession_type:HINIC3_EMC_VXLAN_SESSION\n", HINIC3_UI_INDENT_SPACE);
            break;
        case HINIC3_EMC_GRE_SESSION:
            hinic3_ds_put_format(ds, "%2ssession_type:HINIC3_EMC_GRE_SESSION\n", HINIC3_UI_INDENT_SPACE);
            break;
        case HINIC3_ACL_GRE_SESSION:
            hinic3_ds_put_format(ds, "%2ssession_type:HINIC3_ACL_GRE_SESSION\n", HINIC3_UI_INDENT_SPACE);
            break;
        case HINIC3_MEGA_GRE_SESSION:
            hinic3_ds_put_format(ds, "%2ssession_type:HINIC3_MEGA_GRE_SESSION\n", HINIC3_UI_INDENT_SPACE);
            break;
        case HINIC3_EMC_VXLAN_GPE_SHIM:
            hinic3_ds_put_format(ds, "%2ssession_type:HINIC3_EMC_VXLAN_GPE_SHIM\n", HINIC3_UI_INDENT_SPACE);
            break;
        default:
            break;
    }
}

void hinic3_show_sample_session(struct unixctl_conn *conn, int argc HINIC3_UNUSED, const char *argv[] HINIC3_UNUSED, void *aux)
{
    struct ds ds = DS_EMPTY_INITIALIZER;
    struct session_key *key = NULL;

    ovs_mutex_lock_session();
    for (unsigned int i = 0; i < HINIC3_MIRROR_MAX_SESSION; i++) {
        if (g_mirror_session_info.session_info[i].is_used == 0) {
            continue;
        }
        key = &g_mirror_session_info.session_info[i].key;
        hinic3_ds_put_format(&ds, "*************session_id:%u*************\n",
            g_mirror_session_info.session_info[i].session_id);
        hinic3_show_session_type(&ds, g_mirror_session_info.session_info[i].type);
        hinic3_ds_put_format(&ds, "%2scutoff_len:%u\n", HINIC3_UI_INDENT_SPACE,
            g_mirror_session_info.session_info[i].key.cutoff_len);
        hinic3_ds_put_format(&ds, "%2sflow_list_len:%d\n", HINIC3_UI_INDENT_SPACE,
            g_mirror_session_info.session_info[i].flow_list_len);
        hinic3_ds_put_format(&ds, "%2sdirection:%u\n", HINIC3_UI_INDENT_SPACE,
            g_mirror_session_info.session_info[i].direction);
        hinic3_ds_put_format(&ds, "%2ssampling_interval:%u\n", HINIC3_UI_INDENT_SPACE,
            g_mirror_session_info.session_info[i].sampling_interval);
        if ((g_mirror_session_info.session_info[i].ip_type_flag & HINIC3_FLG_MIRROR_IPV4) == HINIC3_FLG_MIRROR_IPV4) {
            hinic3_show_session_ipv4(&ds, key);
        } else {
            hinic3_show_session_ipv6(&ds, key);
        }
        if (g_mirror_session_info.session_info[i].type == HINIC3_EMC_VXLAN_SESSION) {
            hinic3_show_session_vxlan(&ds, key);
        } else if (g_mirror_session_info.session_info[i].type == HINIC3_EMC_VXLAN_GPE_SHIM) {
            hinic3_show_session_vxlan_gpe_shim(&ds, key);
        } else {
            hinic3_show_session_gre(&ds, key);
        }
        hinic3_show_session_key(&ds, key);
    }
    ovs_mutex_unlock_session();
    if (ds.length == 0) {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_INFO HINIC3_UI_FLOW_DUMP_NO_SESSION_STRING);
    }
    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
    *(int *)aux = 0;
}

int hinic3_flow_get_port_id_by_session(uint8_t session_id, uint16_t *port_id)
{
    if (session_id >= HINIC3_MIRROR_MAX_SESSION || port_id == NULL) {
        return -1;
    }
    *port_id = g_mirror_session_info.session_info[session_id].key.outer_port_id;
    return 0;
}

int hinic3_flow_dump_construct_vxlan_header(uint8_t session_id, struct hinic3_flow_act_vxlan_gpe_header *meta_header)
{
    if (g_mirror_session_info.session_info[session_id].is_used == 0) {
        hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_SESSION_UNUSED, 1);
        return -1;
    }
    struct hinic3_mirror_session_info session_info = g_mirror_session_info.session_info[session_id];
    if (session_info.ip_type_flag == HINIC3_FLG_MIRROR_IPV4_VLAN ||
        session_info.ip_type_flag == HINIC3_FLG_MIRROR_IPV4_VXLAN) {
        meta_header->ip_version = 0;
    } else {
        meta_header->ip_version = 1;
    }
    memcpy(meta_header->dmac, session_info.key.dmac, sizeof(session_info.key.dmac));
    memcpy(meta_header->smac, session_info.key.smac, sizeof(session_info.key.smac));
    memcpy(meta_header->dip, session_info.key.dip, sizeof(session_info.key.dip));
    memcpy(meta_header->sip, session_info.key.sip, sizeof(session_info.key.sip));

    meta_header->vlan_id = session_info.key.tci;
    meta_header->sport = session_info.key.tunnel.vxlan.src_port;
    meta_header->dport = session_info.key.tunnel.vxlan.dst_port;
    meta_header->vxlan.flag = session_info.key.tunnel.vxlan.flags;

    meta_header->vxlan.vni = hinic3_convert_u8_to_u32(session_info.key.tunnel.vxlan.vni, HINIC3_VNI_LEN);
    meta_header->vxlan.rsvd1 = hinic3_convert_u8_to_u32(session_info.key.tunnel.vxlan.vxlan_rsvd0, HINIC3_VXLAN_RSVD0_LEN);

    return 0;
}

static int hinic3_del_session(uint8_t session_id)
{
    int ret;
    if (session_id >= HINIC3_MIRROR_MAX_SESSION) {
        return -1;
    }
    size_t size = sizeof(struct hinic3_mirror_session_info);
    struct hiovs_mirror_session_info hiovs_session_info = {0};
    hiovs_session_info.session_id = session_id;
    ret = hinic3_iface_global_cfg_set(&hiovs_session_info, HINIC3_GLOBAL_CFG_ARG_MIRROR_SESSION_DEL);
    if (ret != 0) {
        return -1;
    }
    memset(&g_mirror_session_info.session_info[session_id], 0, size);
    g_mirror_session_info.used_session_len--;
    return 0;
}

/* 支持以组合的形式flush不同类型的镜像会话 */
int hinic3_session_flush_all(uint32_t type)
{
    int ret;
    ovs_mutex_lock_session();
    for (uint8_t i = 0; i < HINIC3_MIRROR_MAX_SESSION; i++) {
        if (g_mirror_session_info.session_info[i].is_used == 0) {
            continue;
        }
        if ((type & g_mirror_session_info.session_info[i].type) != 0) {
            ret = hinic3_del_session(i);
            if (ret != 0) {
                HINIC3_LOG(ERR, FLOW, "hinic3 flow session delete failed.");
                ovs_mutex_unlock_session();
                return -1;
            }
            g_mirror_session_info.session_info[i].flow_list_len = 0;
            g_mirror_session_info.used_session_len--;
        }
    }
    ovs_mutex_unlock_session();
    return 0;
}

int hinic3_del_rte_flow_in_session(struct rte_flow *flow)
{
    ovs_mutex_lock_session();
    int ret;
    uint8_t session_id = flow->session_id;
    if (g_mirror_session_info.session_info[session_id].flow_list_len > 0) {
        g_mirror_session_info.session_info[session_id].flow_list_len--;
        if (g_mirror_session_info.session_info[session_id].flow_list_len == 0) {
            ret = hinic3_del_session(session_id);
            if (ret != 0) {
                HINIC3_LOG(ERR, FLOW, "hinic3 flow session delete failed.");
                g_mirror_session_info.session_info[session_id].flow_list_len++;
                ovs_mutex_unlock_session();
                return -1;
            }
        }
        ovs_mutex_unlock_session();
        return 0;
    } else {
        HINIC3_LOG(ERR, FLOW, "hinic3 flow session flow list is error.");
        ovs_mutex_unlock_session();
        return -1;
    }
}

static uint8_t hinic3_find_mirror_session(struct hinic3_mirror_session_info *session_info)
{
    for (unsigned int i = 0; i < HINIC3_MIRROR_MAX_SESSION; i++) {
        struct hinic3_mirror_session_info *cur_session_info = &g_mirror_session_info.session_info[i];
        if (cur_session_info->is_used == 0) {
            continue;
        }
        if (memcmp(&cur_session_info->key, &session_info->key, sizeof(struct session_key)) == 0 &&
            session_info->type == cur_session_info->type) {
            return cur_session_info->session_id;
        }
    }

    return HINIC3_MIRROR_MAX_SESSION;
}

static int  hinic3_insert_session_in_list(struct hinic3_mirror_session_info *session_info,
    struct rte_flow *flow HINIC3_UNUSED)
{
    g_mirror_session_info.session_info[session_info->session_id].flow_list_len = 1;
    g_mirror_session_info.used_session_len++;
    if (session_info->key.tunnel.vxlan_gpe_shim.shim_len > HINIC3_SHIM_HEADER_LEN_MAX) {
        hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_SAMPLE_SHIM_ITEM,1);
        return -1;
    }
    memcpy(&g_mirror_session_info.session_info[session_info->session_id], session_info, sizeof(struct hinic3_mirror_session_info));
    g_mirror_session_info.session_info[session_info->session_id].flow_list_len = 1;
    return 0;
}

static void hinic3_construct_hiovs_tunnel_info(const struct hinic3_mirror_session_info *session_info,
    struct hiovs_mirror_session_info *hiovs_session_info)
{
    if (session_info->type == HINIC3_EMC_VXLAN_SESSION) {
        hiovs_session_info->tunnel.vxlan.vxlan_rsvd0 = hinic3_convert_u8_to_u32(
            session_info->key.tunnel.vxlan.vxlan_rsvd0, HINIC3_VXLAN_RSVD0_LEN);
        hiovs_session_info->tunnel.vxlan.vni = hinic3_convert_u8_to_u32(
            session_info->key.tunnel.vxlan.vni, HINIC3_VNI_LEN);
        hiovs_session_info->type = 0; // 0:vxlan镜像 1:GRE镜像
    } else if (session_info->type == HINIC3_EMC_VXLAN_GPE_SHIM) {
        hiovs_session_info->tunnel.vxlan_shim_header.vxlan_rsvd0 = hinic3_convert_u8_to_u32(
            session_info->key.tunnel.vxlan_gpe_shim.vxlan_rsvd0, HINIC3_SHIM_RSVD0_LEN);
        hiovs_session_info->tunnel.vxlan_shim_header.vni = hinic3_convert_u8_to_u32(
            session_info->key.tunnel.vxlan_gpe_shim.vni, HINIC3_VNI_LEN);
        memcpy(hiovs_session_info->tunnel.vxlan_shim_header.data, session_info->key.tunnel.vxlan_gpe_shim.data,
            session_info->key.tunnel.vxlan_gpe_shim.shim_len);
        hiovs_session_info->tunnel.vxlan_shim_header.flag = session_info->key.tunnel.vxlan_gpe_shim.flags;
        hiovs_session_info->tunnel.vxlan_shim_header.protocol = session_info->key.tunnel.vxlan_gpe_shim.next_protocol;
        hiovs_session_info->shim_header_len = session_info->key.tunnel.vxlan_gpe_shim.shim_len;
        hiovs_session_info->type = 0;
        hiovs_session_info->is_shim_header = 1; // 1: vxlan_gpe_shim
    } else {
        hiovs_session_info->tunnel.gre.c_rsvd0_ver = session_info->key.tunnel.gre.c_rsvd0_ver;
        hiovs_session_info->tunnel.gre.protocol = session_info->key.tunnel.gre.protocol;
        hiovs_session_info->type = 1; // 0:vxlan镜像 1:GRE镜像
    }
}

static void hinic3_construct_hiovs_session_info(const struct hinic3_mirror_session_info *session_info,
    struct hiovs_mirror_session_info *hiovs_session_info)
{
    hiovs_session_info->cutoff_len = session_info->key.cutoff_len;
    hiovs_session_info->session_id = session_info->session_id;
    if (session_info->ip_type_flag == HINIC3_FLG_MIRROR_IPV4_VLAN ||
        session_info->ip_type_flag == HINIC3_FLG_MIRROR_IPV4_VXLAN ||
        session_info->ip_type_flag == HINIC3_FLG_MIRROR_IPV4_GRE ||
        session_info->ip_type_flag == HINIC3_FLG_MIRROR_IPV4_VLAN_GRE ||
        session_info->ip_type_flag == HINIC3_FLG_MIRROR_IPV4_VXLAN_GPE_SHIM) {
        hiovs_session_info->is_ipv4 = 1;
    } else {
        hiovs_session_info->is_ipv4 = 0;
    }
    if (session_info->ip_type_flag == HINIC3_FLG_MIRROR_IPV4_VLAN ||
        session_info->ip_type_flag == HINIC3_FLG_MIRROR_IPV6_VLAN ||
        session_info->ip_type_flag == HINIC3_FLG_MIRROR_IPV4_VLAN_GRE ||
        session_info->ip_type_flag == HINIC3_FLG_MIRROR_IPV6_VLAN_GRE) {
        hiovs_session_info->is_vlan = 1;
    } else {
        hiovs_session_info->is_vlan = 0;
    }
    hiovs_session_info->dest_port = htons((uint16_t)session_info->key.outer_port_id);
    hiovs_session_info->vlan_tag = session_info->key.tci;
    memcpy(hiovs_session_info->smac, session_info->key.smac, ETH_ALEN);
    memcpy(hiovs_session_info->dmac, session_info->key.dmac, ETH_ALEN);
    memcpy(hiovs_session_info->sip, session_info->key.sip, HINIC3_MIRROR_IP_LEN);
    memcpy(hiovs_session_info->dip, session_info->key.dip, HINIC3_MIRROR_IP_LEN);
    
    hinic3_construct_hiovs_tunnel_info(session_info, hiovs_session_info);
    hiovs_session_info->enabled = 1;
}

static int hinic3_mirror_set_session_to_hovs(struct hinic3_mirror_session_info *session_info)
{
    if (session_info->sampling_interval > HINIC3_SAMPLING_INTERVAL_MAX) {
        hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_SESSION_RATIO, 1);
        return -1;
    }

    int ret = 0;
    struct hiovs_mirror_session_info hiovs_session_info = { 0 };

    hinic3_construct_hiovs_session_info(session_info, &hiovs_session_info);

    ret = hinic3_iface_global_cfg_set(&hiovs_session_info, HINIC3_GLOBAL_CFG_ARG_MIRROR_SESSION_SET);
    if (ret != 0) {
        return -1;
    }
    return 0;
}

static int hinic3_get_session_id(struct hinic3_mirror_session_info *session_info)
{
    for (unsigned int i = 0; i < HINIC3_MIRROR_MAX_SESSION; i++) {
        if (g_mirror_session_info.session_info[i].is_used == 0) {
            session_info->session_id = i;
            return 0;
        }
    }
    return -1;
}

int hinic3_deal_with_session_info(struct hinic3_mirror_session_info *session_info,
    struct rte_flow *flow, uint8_t mirror_dir_flag)
{
    int ret;
    uint8_t session_id = hinic3_find_mirror_session(session_info);

    session_info->direction = mirror_dir_flag;

    if (session_id != HINIC3_MIRROR_MAX_SESSION) {
        session_info->session_id = session_id;
        g_mirror_session_info.session_info[session_id].flow_list_len++;
    } else {
        ret = hinic3_get_session_id(session_info);
        if (ret != 0) {
            hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_SESSION_FULLED, 1);
            return -1;
        }
        ret = hinic3_insert_session_in_list(session_info, flow);
        if (ret != 0) {
            hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_SAMPLE_SHIM_ITEM,1);
            return -1;
        }
        ret = hinic3_mirror_set_session_to_hovs(session_info);
        if (ret != 0) {
            return -1;
        }
    }
    return 0;
}

void hinic3_mirror_session_info_list_init(void)
{
    g_mirror_session_info.used_session_len = 0;
    hinic3_spinlock_init(&g_mirror_session_info.mutex, PTHREAD_PROCESS_PRIVATE);
    for (unsigned int i = 0; i < HINIC3_MIRROR_MAX_SESSION; i++) {
        g_mirror_session_info.session_info[i].is_used = 0;
        g_mirror_session_info.session_info[i].session_id = (uint8_t)i;
    }
}
