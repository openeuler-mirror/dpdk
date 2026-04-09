/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2022 Huawei Technologies Co., Ltd
 */
#include "rte_log.h"
#include "rte_ethdev.h"
#include "hinic3_iface_port.h"
#include "hinic3_offload_action_public.h"
#include "hinic3_offload_flow_port.h"
#include "hinic3_flow_mirror.h"
#include "hinic3_parse_agent_config.h"
#include "hinic3_port_util.h"
#include "hinic3_offload_action.h"
#include "hinic3_flow_qos.h"

#define HINIC3_RTE_VLAN_PCP_MASK 0x07
#define HINIC3_FLEXDA_RTE_VLAN_PCP_MASK  0xe0
#define VLAN_PCP_MOVE 13
#define FLEXDA_VLAN_PCP_MOVE 8
#define HWOF_DEFALUT_DP_HASH_OUT_PORT 0X0080
#define VLAN_VID_MAX 4094
#define VLAN_PCP_MAX 7
struct hinic3_ct_offload_ops g_hinic3_ct_offload_ops = {0};

struct hinic3_ct_offload_ops* hinic3_get_ct_offload_ops(void)
{
    return &g_hinic3_ct_offload_ops;
}

static inline int hinic3_offload_parse_vlan_act(struct hinic3_offload_action *offload_action,
    const struct rte_flow_action_of_set_vlan_pcp *rte_vlan_pcp,
    const struct rte_flow_action_of_set_vlan_vid *rte_vlan_vid)
{  
    uint16_t vlan_tci = 0;
    uint16_t vlan_id = 0;
    uint16_t vlan_pcp = 0;
    struct hinic3_nlattr *hinic3_actions = &offload_action->act_nla;

    if (hinic3_card_mod_get() == PROG_MODE) {
        vlan_tci = ntohs(rte_vlan_vid->vlan_vid);
        vlan_id = (rte_vlan_pcp->vlan_pcp & HINIC3_FLEXDA_RTE_VLAN_PCP_MASK) << FLEXDA_VLAN_PCP_MOVE;
        vlan_id = (vlan_id | vlan_tci);
        offload_action->has_vlan_push = true;
        offload_action->vlan_id = htons(vlan_id);
        return hinic3_nlattr_put_u16(hinic3_actions, HINIC3_FLOW_ACT_DPDK_VLAN_PUSH, htons(vlan_id));
    }

    vlan_id = ntohs(rte_vlan_vid->vlan_vid);
    vlan_pcp = (rte_vlan_pcp->vlan_pcp & HINIC3_RTE_VLAN_PCP_MASK) << VLAN_PCP_MOVE;

    if (vlan_id > VLAN_VID_MAX)
        return -1;

    if (rte_vlan_pcp->vlan_pcp > VLAN_PCP_MAX)
        return -1;

    vlan_tci = (vlan_pcp | vlan_id);

    offload_action->has_vlan_push = true;
    offload_action->vlan_id = htons(vlan_tci);

    return hinic3_nlattr_put_u16(hinic3_actions, HINIC3_FLOW_ACT_VLAN_PUSH, htons(vlan_tci));
}

/* 此处取端口私有数据的首个uint16_t的数据作为端口的vport_id */
uint32_t hinic3_process_port_id(uint32_t port_id)
{
    void *data = NULL;
    data = hinic3_get_private_data(port_id);
    if (data == NULL) {
        return UINT32_MAX;
    }
    return *(uint16_t*)data;
}

static void hinic3_add_simple_ct_action(struct hinic3_offload_action *offload_action,
                                       struct hinic3_conntrack_full_key *key)
{
    struct hinic3_ct_tcp_state ct_state = {0};
    ct_state.max_win = HINIC3_DEFAULT_WINDOW;
    if (hinic3_status_packet_upcall_get() == DEFAULT_PUT) {
        hinic3_nlattr_put_unspec(&offload_action->act_nla, HINIC3_FLOW_ACT_CT, &ct_state, sizeof(ct_state));
    } else if (hinic3_status_packet_upcall_get() == SELECT_PUT) {
        if (key->key.meta.need_ct_action == 1) {
            hinic3_nlattr_put_unspec(&offload_action->act_nla, HINIC3_FLOW_ACT_CT, &ct_state, sizeof(ct_state));
        }
    }
}

static int
hinic3_offload_parse_qos_act_ovs(struct hinic3_offload_action *offload_action,
        const struct rte_flow_action_meter *mtr)
{
    struct mtr_info_node *info_node = NULL;
    struct hovs_qos_action qos_action = {0};
    struct hinic3_nlattr *hinic3_actions = &offload_action->act_nla;

    if (mtr == NULL) {
        HINIC3_LOG(ERR, FLOW, "meter action is null.");
        return -1;
    }

    info_node = hinic3_mtr_node_lookup(mtr->mtr_id);
    if (info_node == NULL) {
        HINIC3_LOG(ERR, FLOW, "meter id is not found.");
        return -1;
    }

    qos_action.qos_id = info_node->info.qos_id;
    // 流表级qos_type变更，变更前：0，bps；1，pps，变更后：1，bps；2，pps；3，bps+pps
    qos_action.qos_type = info_node->info.packet_mode + 1;
    hinic3_nlattr_put_unspec(hinic3_actions, HINIC3_FLOW_ACT_QOS, &qos_action, sizeof(struct hovs_qos_action));

    return 0;
}

static int
hinic3_offload_parse_qos_act(struct hinic3_offload_action *offload_action,
    const struct rte_flow_action_meter *mtr, struct rte_flow *mega_flow)
{
    if (hinic3_support_multi_qos_get() == false)
        return hinic3_offload_parse_qos_act_ovs(offload_action, mtr);
    else
        return hinic3_offload_parse_qos_act_sub(offload_action, mtr, mega_flow);
    
}

static int hinic3_add_port_id_action(const struct rte_flow_action *act, struct hinic3_offload_action *offload_action)
{
    if (act->conf == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3load port id action is null.");
        return -1;
    }

    const struct rte_flow_action_port_id *output_port = NULL;
    uint32_t output_port_id;
    output_port = (const struct rte_flow_action_port_id *)act->conf;
    output_port_id = hinic3_process_port_id(output_port->id);
    if (output_port_id > UINT16_MAX) {
        HINIC3_LOG(ERR, FLOW, "port_id exceeds the maximum range of driver.");
        return -1;
    }
    if (hinic3_card_mod_get() == PROG_MODE)
        hinic3_nlattr_put_u16(&offload_action->act_nla, HINIC3_FLOW_ACT_DPDK_PORT_ID, htons((uint16_t)output_port_id));
    else
        hinic3_nlattr_put_u16(&offload_action->act_nla, HINIC3_FLOW_ACT_OUTPUT, htons((uint16_t)output_port_id));
    offload_action->has_output = true;
    return 0;
}

static int hinic3_offload_parse_void_act(const struct rte_flow_action *act, struct hinic3_offload_action *offload_action)
{
    if (act == NULL || act->conf == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3 load hydra action is NULL.");
        return -1;
    }
    const struct hydra_flow_action *hydra_act = (const struct hydra_flow_action *)act->conf;

    if (hinic3_flexda_flow_action_is_in_table(hydra_act->action_type)) {
        hinic3_nlattr_put_unspec(
        &offload_action->act_nla, hydra_act->action_type, hydra_act->action_data, hydra_act->action_data_size);
        return 0;
    }

    if ((hydra_act->action_type >= HINIC3_HYDRA_TYPE_ACTION_START &&
        hydra_act->action_type <= HINIC3_HYDRA_TYPE_ACTION_END)) {
        hinic3_nlattr_put_unspec(
        &offload_action->act_nla, hydra_act->action_type, hydra_act->action_data, hydra_act->action_data_size);
        return 0;
    }

    return -1;
}

static uint32_t hinic3_process_represent_port_id(uint16_t port_id)
{
    void *data = NULL;
    data = hinic3_get_private_data(port_id);
    if (data == NULL) {
        return UINT32_MAX;
    }
    return *(uint16_t*)data;
}

static int hinic3_add_represented_port_id_action(const struct rte_flow_action *act,
                                                struct hinic3_offload_action *offload_action)
{
    if (act->conf == NULL) {
        HINIC3_LOG(ERR, FLOW, "hwoffload port id action is null.");
        return -1;
    }

    const struct rte_flow_action_ethdev *output_port = NULL;
    uint32_t output_port_id;
    output_port = (const struct rte_flow_action_ethdev *)act->conf;
    output_port_id = hinic3_process_represent_port_id(output_port->port_id);
    if (output_port_id > UINT16_MAX) {
        HINIC3_LOG(ERR, FLOW, "port_id exceeds the maximum range of driver.");
        return -1;
    }
    hinic3_nlattr_put_u16(&offload_action->act_nla, HINIC3_FLOW_ACT_DPDK_PORT_ID, htons((uint16_t)output_port_id));
        offload_action->has_output = true;
    return 0;
}

static int hinic3_offload_parse_conntrack_act(struct hinic3_nlattr *hwoff_actions, const struct rte_flow_action *act,
    struct hinic3_flow_offload_param *param, struct hinic3_conntrack_full_key *full_key)
{
    if (hinic3_con_track_get() == true && g_hinic3_ct_offload_ops.ct_offload_action != NULL) {
        return g_hinic3_ct_offload_ops.ct_offload_action(hwoff_actions, act, param, full_key);
    } else {
        return -1;
    }
}
                                                    
int hinic3_offload_parse_action(const struct rte_flow_action actions[], struct rte_flow *mega_flow,
    struct hinic3_flow_offload_param *param, uint8_t mirror_dir_flag)
{
    int ret;
    const struct rte_flow_action_of_set_vlan_pcp *rte_vlan_pcp = NULL;
    const struct rte_flow_action *act = next_action(actions, NULL);
    const struct rte_flow_action_of_set_vlan_vid *rte_vlan_vid = NULL;
    bool has_set_vlan_pcp = false;
    bool has_set_vlan_vid = false;
    struct hinic3_offload_action *offload_action = &param->cur_actions;

    if (actions == NULL) {
        return -1;
    }
    while (act && (act->type != RTE_FLOW_ACTION_TYPE_END)) {
        ret = 0;
        switch (act->type) {
            case RTE_FLOW_ACTION_TYPE_VOID:
                ret = hinic3_offload_parse_void_act(act, offload_action);
                break;
            case RTE_FLOW_ACTION_TYPE_PORT_ID:
                ret = hinic3_add_port_id_action(act, offload_action);
                break;
            case RTE_FLOW_ACTION_TYPE_REPRESENTED_PORT:
                ret = hinic3_add_represented_port_id_action(act, offload_action);
                break;
            case RTE_FLOW_ACTION_TYPE_CONNTRACK:
                ret = hinic3_offload_parse_conntrack_act(&offload_action->act_nla, act, param, &mega_flow->key);
                break;
            case RTE_FLOW_ACTION_TYPE_OF_SET_VLAN_PCP:
                has_set_vlan_pcp = true;
                rte_vlan_pcp = act->conf;
                if (has_set_vlan_pcp && has_set_vlan_vid) {
                    ret = hinic3_offload_parse_vlan_act(offload_action, rte_vlan_pcp, rte_vlan_vid);
                }
                break;
            case RTE_FLOW_ACTION_TYPE_OF_SET_VLAN_VID:
                has_set_vlan_vid = true;
                rte_vlan_vid = act->conf;
                if (has_set_vlan_pcp && has_set_vlan_vid) {
                    ret = hinic3_offload_parse_vlan_act(offload_action, rte_vlan_pcp, rte_vlan_vid);
                }
                break;
            case RTE_FLOW_ACTION_TYPE_SAMPLE:
                mega_flow->flags.is_sample = HINIC3_FLOW_EMC_MIRROR;
                ret = hinic3_offload_parse_sample_act(act, &offload_action->act_nla, mega_flow, mirror_dir_flag);
                break;
            case RTE_FLOW_ACTION_TYPE_METER:
                ret = hinic3_offload_parse_qos_act(offload_action, act->conf, mega_flow);
                break;
            default:
                ret = hinic3_offload_parse_action_sub(act, offload_action, mega_flow);
                break;
        }
        if (ret != 0) {
            HINIC3_LOG(ERR, FLOW, "Invalid flow action type %d", act->type);
            return -1;
        }
        act = next_action(actions, act);
    }
    (void)hinic3_add_simple_ct_action(offload_action, &mega_flow->key);
    return 0;
}
