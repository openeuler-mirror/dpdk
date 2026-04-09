/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Huawei Technologies Co., Ltd
 */
#include "hinic3_flow_dump_action.h"
#include "hinic3_offload_flow_port.h"
#include "hinic3_util.h"
#include "hinic3_message.h"
#include "hinic3_log.h"
#include "hinic3_flow_session.h"
#include "hinic3_vxlan_dump.h"
#include "hinic3_meminfo.h"

#define VLAN_TCI_OFFSET 0xF000
#define VLAN_VID_BIT 0xFF0F
#define VLAN_PCP_BIT 0XE000
#define HINIC3_VXLAN_ITEM_NUMBER 5
#define HINIC3_SAMLPE_ACTION_NUM 3
#define HINIC3_PORT_TRANS_MASK 0x1000
#define HINIC3_PORT_BIT_MASK 0X3FFF
#define HINIC3_VLAN_VID_BIT 13
#define DUMP_VLAN_PCP_OFFSET 8
#define HINIC3_DP_HASH_DUMP_DEFALUT_PORT 0X0080
typedef int (*hinic3_action_conf)(const hinic3_nlattr_itr, void **);

struct hinic3_flow_action_info_s {
    enum hinic3_flow_action_type hinic3_type;
    enum rte_flow_action_type rte_type;
    hinic3_action_conf conf_call_back;
};

static void hinic3_free_rte_sample_action(struct rte_flow_action *action)
{
    if (action == NULL || action->type != RTE_FLOW_ACTION_TYPE_SAMPLE) {
        return;
    }

    if (action->conf == NULL) {
        return;
    }

    const struct rte_flow_action_sample *sample = action->conf;
    const struct rte_flow_action *sample_action = sample->actions;

    struct rte_flow_action_port_id *port_id = NULL;
    struct rte_flow_action_vxlan_encap *vxlan_encap = NULL;

    if (sample_action != NULL) {
        port_id = (struct rte_flow_action_port_id *)(uintptr_t)sample_action[0].conf;
        vxlan_encap = (struct rte_flow_action_vxlan_encap *)(uintptr_t)sample_action[1].conf;

        hinic3_free_vxlan_header(vxlan_encap);
        hinic3_free(port_id);
        hinic3_free((void *)(uintptr_t)sample_action);
    }

    hinic3_free((void *)(uintptr_t)sample);
    return;
}

static void hinic3_free_rte_flow_action(struct rte_flow_action *action)
{
    if (action == NULL) {
        return;
    }

    if (action->conf != NULL) {
        switch (action->type) {
            case RTE_FLOW_ACTION_TYPE_VXLAN_ENCAP:
                hinic3_free_vxlan_header((void *)(uintptr_t)action->conf);
                break;
            case RTE_FLOW_ACTION_TYPE_SAMPLE:
                hinic3_free_rte_sample_action(action);
                break;
            default:
                hinic3_free((void *)(uintptr_t)action->conf);
                action->conf = NULL;
                break;
        }
    }
}

void hinic3_free_one_flow_actions(struct rte_flow_action *actions, int action_num)
{
    if (actions == NULL) {
        return;
    }
    for (int i = 0; i < action_num; ++i) {
        hinic3_free_rte_flow_action(&actions[i]);
    }
}

static struct rte_flow_action *hinic3_build_rte_flow_action(enum rte_flow_action_type type, const void *conf)
{
    struct rte_flow_action *action = hinic3_calloc(1, sizeof(struct rte_flow_action), HINIC3_FLOWS);
    if (action == NULL) {
        HINIC3_LOG(ERR, FLOW, "HINIC3 FLOW DUMP: malloc for flow action failed");
        return NULL;
    }
    action->type = type;
    action->conf = conf;
    return action;
}

static int hinic3_dump_flow_action_insert(struct hinic3_dump_flow_info *flow, struct rte_flow_action *action)
{
    if (flow == NULL || action == NULL) {
        return -1;
    }
    unsigned int index = flow->useful_action_index;
    if (index < HINIC3_FLOW_DUMP_MAX_ACTION) {
        memcpy(&flow->actions[index], action, sizeof(struct rte_flow_action));
        flow->useful_action_index++;

        hinic3_free(action);
        action = NULL;
        return 0;
    }
    return -1;
}

// construct rte_flow_action conf
static void *hinic3_get_vlan_push_conf(void)
{
    uint16_t *eth = hinic3_calloc(1, sizeof(uint16_t), HINIC3_FLOWS);
    if (eth == NULL) {
        return NULL;
    }
    *eth = htons(ETH_TYPE_VLAN);
    return (void *)eth;
}

static void *hinic3_get_conf_set_vlan_vid(uint16_t vid)
{
    uint16_t *vlan_vid = hinic3_calloc(1, sizeof(uint16_t), HINIC3_FLOWS);
    if (vlan_vid == NULL) {
        return NULL;
    }
    *vlan_vid = (vid & VLAN_VID_BIT);
    return (void *)vlan_vid;
}

static void *hwof_get_conf_set_vlan_pcp(uint16_t vid)
{
    uint8_t *vlan_pcp = hinic3_calloc(1, sizeof(uint8_t), HINIC3_FLOWS);
    if (vlan_pcp == NULL) {
        return NULL;
    }
    *vlan_pcp = (ntohs(vid) & VLAN_PCP_BIT) >> DUMP_VLAN_PCP_OFFSET;
    return (void *)vlan_pcp;
}

static struct rte_flow_action *hinic3_vlan_action_factory(enum rte_flow_action_type rte_type, uint16_t vid)
{
    void *conf = NULL;
    switch (rte_type) {
        case RTE_FLOW_ACTION_TYPE_OF_PUSH_VLAN:
            conf = hinic3_get_vlan_push_conf();
            break;
        case RTE_FLOW_ACTION_TYPE_OF_SET_VLAN_VID:
            conf = hinic3_get_conf_set_vlan_vid(vid);
            break;
        case RTE_FLOW_ACTION_TYPE_OF_SET_VLAN_PCP:
            conf = hwof_get_conf_set_vlan_pcp(vid);
            break;
        default:
            break;
    }
    if (conf == NULL) {
        return NULL;
    }

    struct rte_flow_action *action = hinic3_build_rte_flow_action(rte_type, conf);
    if (action == NULL) {
        hinic3_free(conf);
    }

    return action;
}

static int hinic3_flow_dump_mac_conf_get(const hinic3_nlattr_itr nla, void **dst_conf)
{
    const void *conf_meta = NULL;
    void *conf = hinic3_calloc(1, sizeof(struct eth_address), HINIC3_FLOWS);
    if (conf == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3 flow dump, build rte flow action conf failed");
        return -1;
    }
    conf_meta = (const void *)hinic3_nlattr_get_itr_unspec(nla, sizeof(struct eth_address));
    if (conf_meta == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3 flow dump, get mac conf failed");
        hinic3_free(conf);
        return -1;
    }

    memcpy(conf, conf_meta, sizeof(struct eth_address));
    *dst_conf = conf;
    return 0;
}

static int hinic3_flow_dump_u32_conf_get(const hinic3_nlattr_itr nla, void **dst_conf)
{
    rte_be32_t *conf = hinic3_calloc(1, sizeof(rte_be32_t), HINIC3_FLOWS);
    if (conf == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3 flow dump, build rte flow action conf failed");
        return -1;
    }

    rte_be32_t src_conf = hinic3_nlattr_get_itr_u32(nla);
    *conf = src_conf;
    *dst_conf = conf;
    return 0;
}

static int hinic3_flow_dump_ipv4_conf_get(const hinic3_nlattr_itr nla, void **dst_conf)
{
    return hinic3_flow_dump_u32_conf_get(nla, dst_conf);
}

static int hinic3_flow_dump_ipv6_conf_get(const hinic3_nlattr_itr nla, void **dst_conf)
{
    const in6_addr_t *meta_ipv6 = NULL;

    void *conf = hinic3_calloc(1, sizeof(in6_addr_t), HINIC3_FLOWS);
    if (conf == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3 flow dump, build rte flow action conf failed");
        return -1;
    }

    meta_ipv6 = hinic3_nlattr_get_itr_unspec(nla, sizeof(in6_addr_t));
    if (meta_ipv6 == NULL) {
        hinic3_free(conf);
        return -1;
    }

    memcpy(conf, meta_ipv6, sizeof(in6_addr_t));
    *dst_conf = conf;
    return 0;
}

static int hinic3_flow_dump_port_conf(const hinic3_nlattr_itr nla, void **dst_conf)
{
    struct rte_flow_action_port_id *conf = hinic3_calloc(1, sizeof(struct rte_flow_action_port_id), HINIC3_FLOWS);
    if (conf == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3 flow dump, build rte flow action conf failed");
        return -1;
    }

    uint16_t meta_port = hinic3_nlattr_get_itr_u16(nla);
    uint16_t port_net = ntohs(meta_port);
    conf->id = port_net;

    uint16_t dpdk_port;
    int res = hinic3_get_port_id_by_ifindex(port_net, &dpdk_port);
    if (res != 0) {
        HINIC3_LOG(ERR, FLOW, "hinic3  flow dump, get port id failed, vport_id %X", port_net);
        hinic3_free(conf);
        return -1;
    }
    conf->id = dpdk_port;
    *dst_conf = conf;
    return 0;
}

static int hinic3_flow_dump_vxlan_conf_get(const hinic3_nlattr_itr nla, void **dst_conf)
{
    hinic3_act_vxlan_header *meta_header =
        (hinic3_act_vxlan_header *)(uintptr_t)hinic3_nlattr_get_itr_unspec(nla, sizeof(hinic3_act_vxlan_header));
    if (meta_header == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3 flow dump, get vxlan header failed");
        return -1;
    }

    void *conf = hinic3_get_vxlan_gpe_header_items(meta_header);
    if (conf == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3 flow dump, get vxlan header items failed");
        return -1;
    }
    *dst_conf = conf;
    return 0;
}

static struct rte_flow_action_port_id *hinic3_flow_dump_sample_port_conf(uint8_t session_id)
{
    struct rte_flow_action_port_id *output_port = NULL;
    int ret = 0;
    output_port =
        (struct rte_flow_action_port_id *)hinic3_calloc(1, sizeof(struct rte_flow_action_port_id), HINIC3_FLOWS);
    if (output_port == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3 flow dump, build rte flow action conf failed");
        return NULL;
    }
    uint16_t port_net;
    ret = hinic3_flow_get_port_id_by_session(session_id, &port_net);
    if (ret != 0) {
        hinic3_free(output_port);
        return NULL;
    }

    uint16_t port_dpdk;
    ret = hinic3_get_port_id_by_ifindex(port_net, &port_dpdk);
    if (ret != 0) {
        HINIC3_LOG(ERR, FLOW, "hinic3 flow dump sample port info,  get port id failed, vport_id %X", port_net);
        hinic3_free(output_port);
        return NULL;
    }
    output_port->id = port_dpdk;
    output_port->reserved = session_id; // 利用port_id action中的reserved字段暂存session_id
    return output_port;
}

static struct rte_flow_action_vxlan_encap *hinic3_flow_dump_sample_vxlan_encap_conf(uint8_t session_id)
{
    struct hinic3_flow_act_vxlan_gpe_header meta_header = { 0 };
    int ret = hinic3_flow_dump_construct_vxlan_header(session_id, &meta_header);
    if (ret != 0) {
        return NULL;
    }

    return hinic3_get_vxlan_gpe_header_items(&meta_header);
}

static struct rte_flow_action *hinic3_flow_dump_construct_sample_actions(const struct hiovs_mirror_info *mirror_info)
{
    enum hinic3_session_tpye session_type;
    struct rte_flow_action *actions = NULL;
    uint8_t session_id = mirror_info->sessions[1];
    actions =
        (struct rte_flow_action *)hinic3_calloc(HINIC3_SAMLPE_ACTION_NUM, sizeof(struct rte_flow_action), HINIC3_FLOWS);
    if (actions == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3 flow dump, build rte flow action conf failed");
        return NULL;
    }

    struct rte_flow_action *port_id_action = actions;
    port_id_action->type = RTE_FLOW_ACTION_TYPE_PORT_ID;
    port_id_action->conf = hinic3_flow_dump_sample_port_conf(session_id);
    if (port_id_action->conf == NULL) {
        hinic3_free(actions);
        HINIC3_LOG(ERR, FLOW, "hinic3 flow dump, construct sample port conf filed");
        return NULL;
    }

    session_type = hinic3_get_session_type(session_id);
    struct rte_flow_action *vxlan_encap_action = ++port_id_action;
    if (session_type == HINIC3_EMC_VXLAN_SESSION) {
        vxlan_encap_action->type = RTE_FLOW_ACTION_TYPE_VXLAN_ENCAP;
        vxlan_encap_action->conf = hinic3_flow_dump_sample_vxlan_encap_conf(session_id);
    } else {
        vxlan_encap_action->type = RTE_FLOW_ACTION_TYPE_NVGRE_ENCAP;
        vxlan_encap_action->conf = hinic3_flow_dump_sample_nvgre_encap_conf(session_id);
    }
    if (vxlan_encap_action->conf == NULL) {
        hinic3_free((void *)(uintptr_t)port_id_action->conf);
        hinic3_free(actions);
        HINIC3_LOG(ERR, FLOW, "hinic3 flow dump, construct sample port conf filed");
        return NULL;
    }

    struct rte_flow_action *end_action = ++vxlan_encap_action;
    end_action->type = RTE_FLOW_ACTION_TYPE_END;
    end_action->conf = NULL;

    return actions;
}

static struct rte_flow_action_sample *hinic3_flow_dump_get_sample_conf(const struct hiovs_mirror_info *mirror_info)
{
    struct rte_flow_action_sample *conf = NULL;
    conf = (struct rte_flow_action_sample *)hinic3_calloc(1, sizeof(struct rte_flow_action_sample), HINIC3_FLOWS);
    if (conf == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3 flow dump, build rte flow action conf failed");
        return NULL;
    }

    conf->ratio = 1;
    conf->actions = hinic3_flow_dump_construct_sample_actions(mirror_info);
    if (conf->actions == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3 flow dump, construct sample actions failed");
        hinic3_free(conf);
        return NULL;
    }
    return conf;
}

int hinic3_flow_dump_acl_sample_conf_get(const hinic3_nlattr_itr nla, void **dst_conf)
{
    uint16_t session_id = hinic3_nlattr_get_itr_u16(nla);
    struct hiovs_mirror_info mirror_info = { 0 };
    mirror_info.sessions[1] = session_id;

    *dst_conf = hinic3_flow_dump_get_sample_conf(&mirror_info);
    if (*dst_conf == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3 flow dump, construct acl sample action failed.");
        return -1;
    }
    return 0;
}

int hinic3_flow_dump_sample_conf_get(const hinic3_nlattr_itr nla, void **dst_conf)
{
    const struct hiovs_mirror_info *mirror_info = hinic3_nlattr_get_itr_unspec(nla, sizeof(struct hiovs_mirror_info));
    if (mirror_info == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3 flow dump, get sample failed");
        return -1;
    }

    void *conf = hinic3_flow_dump_get_sample_conf(mirror_info);
    if (conf == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3 flow dump, construct emc sample action failed.");
        return -1;
    }
    *dst_conf = conf;
    return 0;
}

static int hinic3_flow_dump_block_conf_get(const hinic3_nlattr_itr nla, void **dst_conf)
{
    const struct hinic3_flow_act_block_version *flow_block = NULL;

    struct rte_flow_action_set_tag *conf = hinic3_calloc(1, sizeof(struct rte_flow_action_set_tag), HINIC3_FLOWS);
    if (conf == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3 flow dump, build rte flow action conf failed");
        return -1;
    }

    flow_block = hinic3_nlattr_get_itr_unspec(nla, sizeof(struct hinic3_flow_act_block_version));
    if (flow_block == NULL) {
        hinic3_free(conf);
        return -1;
    }
    conf->data = flow_block->block_version;
    conf->mask = flow_block->block_id;
    conf->index = 0xFE;

    *dst_conf = conf;
    return 0;
}

// rte_flow_action info table
static struct hinic3_flow_action_info_s g_hinic3_flow_action_conf_table[] = {
    {HINIC3_FLOW_ACT_OUTPUT, RTE_FLOW_ACTION_TYPE_PORT_ID, hinic3_flow_dump_port_conf},
    {HINIC3_FLOW_ACT_VXL_GPE_PUSH, RTE_FLOW_ACTION_TYPE_VXLAN_ENCAP, hinic3_flow_dump_vxlan_conf_get},
    {HINIC3_FLOW_ACT_SET_SMAC, RTE_FLOW_ACTION_TYPE_SET_MAC_SRC, hinic3_flow_dump_mac_conf_get},
    {HINIC3_FLOW_ACT_SET_DMAC, RTE_FLOW_ACTION_TYPE_SET_MAC_DST, hinic3_flow_dump_mac_conf_get},
    {HINIC3_FLOW_ACT_SET_SIP, RTE_FLOW_ACTION_TYPE_SET_IPV4_SRC, hinic3_flow_dump_ipv4_conf_get},
    {HINIC3_FLOW_ACT_SET_DIP, RTE_FLOW_ACTION_TYPE_SET_IPV4_DST, hinic3_flow_dump_ipv4_conf_get},
    {HINIC3_FLOW_ACT_SET_SIPV6, RTE_FLOW_ACTION_TYPE_SET_IPV6_SRC, hinic3_flow_dump_ipv6_conf_get},
    {HINIC3_FLOW_ACT_SET_DIPV6, RTE_FLOW_ACTION_TYPE_SET_IPV6_DST, hinic3_flow_dump_ipv6_conf_get},
    {HINIC3_FLOW_ACT_DROP, RTE_FLOW_ACTION_TYPE_DROP, NULL},
    {HINIC3_FLOW_ACT_DEC_TTL, RTE_FLOW_ACTION_TYPE_DEC_TTL, NULL},
    {HINIC3_FLOW_ACT_VLAN_POP, RTE_FLOW_ACTION_TYPE_OF_POP_VLAN, NULL},
    {HINIC3_FLOW_ACT_VXL_POP, RTE_FLOW_ACTION_TYPE_VXLAN_DECAP, NULL},
    {HINIC3_FLOW_ACT_COUNT, RTE_FLOW_ACTION_TYPE_COUNT, NULL},
    {HINIC3_FLOW_ACT_MIRROR, RTE_FLOW_ACTION_TYPE_SAMPLE, hinic3_flow_dump_sample_conf_get},
    {HINIC3_FLOW_ACT_BLOCK_VERSION, RTE_FLOW_ACTION_TYPE_SET_TAG, hinic3_flow_dump_block_conf_get},
};

static struct hinic3_flow_action_info_s *hinic3_get_flow_action_info(enum hinic3_flow_action_type type)
{
    int size = sizeof(g_hinic3_flow_action_conf_table) / sizeof(struct hinic3_flow_action_info_s);
    for (int i = 0; i < size; ++i) {
        if (g_hinic3_flow_action_conf_table[i].hinic3_type == type) {
            return &g_hinic3_flow_action_conf_table[i];
        }
    }
    return NULL;
}

static int hinic3_trans_action_type_by_hiovs(enum hinic3_flow_action_type type, const hinic3_nlattr_itr nla,
    void **dst_conf, enum rte_flow_action_type *rte_type)
{
    struct hinic3_flow_action_info_s *action_info = hinic3_get_flow_action_info(type);
    if (action_info == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3 flow dump, invalid hinic3_flow_action_type, type: %d", type);
        return -1;
    }

    if (action_info->conf_call_back == NULL) {
        *rte_type = action_info->rte_type;
        *dst_conf = NULL;
        return 0;
    }

    int ret = action_info->conf_call_back(nla, dst_conf);
    if (ret != 0) {
        HINIC3_LOG(ERR, FLOW, "hinic3 flow dump, get hinic3_fow_action conf failed");
        return -1;
    }

    *rte_type = action_info->rte_type;
    return 0;
}

static struct rte_flow_action *hinic3_flow_action_factory(enum hinic3_flow_action_type type, const hinic3_nlattr_itr nla)
{
    void *conf = NULL;
    enum rte_flow_action_type rte_type = 0;

    int ret = hinic3_trans_action_type_by_hiovs(type, nla, &conf, &rte_type);
    if (ret != 0) {
        HINIC3_LOG(ERR, FLOW, "hinic3 flow dump, get hinic3_fow_action form hiovs failed");
        return NULL;
    }

    struct rte_flow_action *action = hinic3_build_rte_flow_action(rte_type, conf);
    if (action == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3 flow dump, action factory error, rte_type: %d", rte_type);
        if (conf != NULL) {
            hinic3_free(conf);
        }
    }

    return action;
}

static int hinic3_build_vlan_action(struct hinic3_dump_flow_info *flow, hinic3_nlattr_itr nla)
{
    uint16_t vid = hinic3_nlattr_get_itr_u16(nla);
    struct rte_flow_action *action = NULL;
    action = hinic3_vlan_action_factory(RTE_FLOW_ACTION_TYPE_OF_PUSH_VLAN, vid);
    if (action == NULL) {
        goto err_free;
    }

    int ret = hinic3_dump_flow_action_insert(flow, action);
    if (ret != 0) {
        goto err_free;
    }

    action = hinic3_vlan_action_factory(RTE_FLOW_ACTION_TYPE_OF_SET_VLAN_VID, vid);
    if (action == NULL) {
        goto err_free;
    }

    ret = hinic3_dump_flow_action_insert(flow, action);
    if (ret != 0) {
        goto err_free;
    }

    action = hinic3_vlan_action_factory(RTE_FLOW_ACTION_TYPE_OF_SET_VLAN_PCP, vid);
    if (action == NULL) {
        goto err_free;
    }

    ret = hinic3_dump_flow_action_insert(flow, action);
    if (ret != 0) {
        goto err_free;
    }
    return 0;

err_free:
    HINIC3_LOG(ERR, FLOW, "hinic3 flow dump, build vlan action error");
    hinic3_free_rte_flow_action(action);
    hinic3_free(action);
    return -1;
}

static bool hinic3_dp_hash_output_port_check(const hinic3_nlattr_itr nla)
{
    uint16_t port = hinic3_nlattr_get_itr_u16(nla);
    return port == HINIC3_DP_HASH_DUMP_DEFALUT_PORT ? true : false;
}


int hinic3_build_flow_action_sub(struct hinic3_dump_flow_info *flow, const struct hinic3_nlattr *key)
{
    hinic3_nlattr_itr nla = NULL;

    int ret = 0;
    struct rte_flow_action *action = NULL;

    HINIC3_NLATTR_FOR_EACH(nla, key) {
        enum hinic3_flow_action_type type = hinic3_nlattr_get_itr_type(nla);
        // vlan: one hinic3_action map to three rte_actions;
        if (type == HINIC3_FLOW_ACT_VLAN_PUSH) {
            ret = hinic3_build_vlan_action(flow, nla);
            if (ret != 0) {
                goto err_func;
            }
        } else if (type == HINIC3_FLOW_ACT_CT ||
            (type == HINIC3_FLOW_ACT_OUTPUT && hinic3_dp_hash_output_port_check(nla))) {
            continue;
        } else {
            action = hinic3_flow_action_factory(type, nla);
            if (action == NULL) {
                goto err_func;
            }
            ret = hinic3_dump_flow_action_insert(flow, action);
            if (ret != 0) {
                goto err_func;
            }
        }
    }
    return 0;

err_func:
    HINIC3_LOG(ERR, FLOW, "hinic3 flow dump, build rte flow action error");
    hinic3_free_rte_flow_action(action);
    if (action != NULL) {
        hinic3_free(action);
    }
    return -1;
}

int hinic3_hinic3_flow_info_action_build(struct hinic3_dump_flow_info *flow, struct hinic3_nlattr_obj *dump_key,
    size_t key_length)
{
    struct hinic3_nlattr flow_action;
    hinic3_nlattr_init(&flow_action, dump_key, key_length);
    hinic3_nlattr_reset_itr(&flow_action, key_length);

    int ret = hinic3_build_flow_action_sub(flow, &flow_action);
    if (ret != 0) {
        HINIC3_LOG(ERR, FLOW, "hinic3 flow dump, build flow_info action error");
    }
    return ret;
}
