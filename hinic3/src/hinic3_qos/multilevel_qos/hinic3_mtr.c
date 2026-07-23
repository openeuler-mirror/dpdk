/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2022 Huawei Technologies Co., Ltd
 */
#include "errno.h"
#include "hinic3_log.h"
#include "hinic3_mutex.h"
#include "hinic3_meminfo.h"
#include "rte_ethdev.h"
#include "rte_mtr_driver.h"
#include "hinic3_port_util.h"
#include "hinic3_vf_port_qos_public.h"
#include "hinic3_flow_agent_public.h"
#include "hinic3_offload_action.h"
#include "hinic3_mtr.h"

#define MAX_UINT32_NUM              0xFFFFFFFF

#define QOS_PORT_BPS (1LLU << 0)
#define QOS_PORT_PPS (1LLU << 1)
#define QOS_VM_BPS   (1LLU << 2)
#define QOS_VM_PPS   (1LLU << 3)
#define QOS_NET_BPS  (1LLU << 4)
#define QOS_NET_PPS  (1LLU << 5)

#define QOS_LEVEL_MAX_NUM           6
#define QOS_CFG_FILE                "/etc/dpak/net/agent_config.ini"

uint64_t g_hinic3_qos_mask_array[QOS_LEVEL_MAX_NUM] = {
    QOS_PORT_BPS,
    QOS_PORT_PPS,
    QOS_VM_BPS,
    QOS_VM_PPS,
    QOS_NET_BPS,
    QOS_NET_PPS,
};

struct hinic3_meter_list g_hinic3_meter_list = { 0 };
struct hinic3_net_meter_info g_hinic3_net_meter_info[HINIC3_METER_DIR_NUM] = { 0 };

/* group 0 作为垃圾桶，如果只有端口限速，需要将端口绑定到group 0上 */
struct hinic3_group_info g_hinic3_group_infos[HINIC3_METER_NUM_MAX] = { 0 };

struct hinic3_meter_list *hinic3_meter_list_get(void)
{
    return &g_hinic3_meter_list;
}

struct hinic3_group_info *hinic3_group_info_get(uint16_t group_id)
{
    return &g_hinic3_group_infos[group_id];
}

struct hinic3_net_meter_info *hinic3_net_meter_info_get(void)
{
    return g_hinic3_net_meter_info;
}

void hinic3_meter_list_lock(void)
{
    hinic3_pthread_mutex_lock(&g_hinic3_meter_list.mutex);
}

void hinic3_meter_list_unlock(void)
{
    hinic3_pthread_mutex_unlock(&g_hinic3_meter_list.mutex);
}

void hinic3_group_info_lock(uint16_t group_id)
{
    hinic3_pthread_mutex_lock(&g_hinic3_group_infos[group_id].mutex);
}

void hinic3_group_info_unlock(uint16_t group_id)
{
    hinic3_pthread_mutex_unlock(&g_hinic3_group_infos[group_id].mutex);
}

static void hinic3_meter_mutex_all_lock(void)
{
    hinic3_mtr_policy_list_lock();
    hinic3_mtr_profile_list_lock();
    hinic3_meter_list_lock();
}

static void hinic3_meter_mutex_all_unlock(void)
{
    hinic3_mtr_profile_list_unlock();
    hinic3_mtr_policy_list_unlock();
    hinic3_meter_list_unlock();
}

struct hinic3_meter_node *hinic3_meter_find(uint32_t meter_id)
{
    struct hinic3_meter_node *iter = NULL;

    LIST_FOR_EACH(iter, node, &g_hinic3_meter_list.node) {
        if (iter->meter_id == meter_id) {
            return iter;
        }
    }
    return NULL;
}

enum hinic3_qos_tablehead hinic3_get_multi_qos_show_tablehead(uint16_t dir, int packet_mode)
{
    switch (dir) {
        case HINIC3_TX_METER_QOS:
            switch (packet_mode) {
                case QOS_BW_TYPE:
                    return BW_TX_TYPE;
                case QOS_PPS_TYPE:
                    return PPS_TX_TYPE;
                default:
                    HINIC3_LOG(ERR, QOS, "Multi qos show: Wrong meter type.");
                    return QOS_TABLEHEAD_NUM;
            }
        case HINIC3_RX_METER_QOS:
            switch (packet_mode) {
                case QOS_BW_TYPE:
                    return BW_RX_TYPE;
                case QOS_PPS_TYPE:
                    return PPS_RX_TYPE;
                default:
                    HINIC3_LOG(ERR, QOS, "Multi qos show: Wrong meter type.");
                    return QOS_TABLEHEAD_NUM;
            }
        default:
            HINIC3_LOG(ERR, QOS, "Multi qos show: Wrong meter dir.");
            return QOS_TABLEHEAD_NUM;
    }
}

static void hinic3_update_profile(struct hinic3_meter_node *meter, struct hinic3_mtr_profile_node *profile)
{
    if (meter->profile->used_num > 0) {
        meter->profile->used_num--;
    } else {
        HINIC3_LOG(ERR, QOS, "Meter update: meter profile number error.");
    }
    meter->profile = profile;
    profile->used_num++;
}

static int hinic3_meter_profile_update(struct rte_eth_dev *dev, uint32_t meter_id, uint32_t meter_profile_id,
    struct rte_mtr_error *error)
{
    if (dev == NULL || error == NULL) {
        HINIC3_LOG(ERR, QOS, "Meter update: Meter parameter is empty.");
        return -rte_mtr_error_set(error, EPERM, RTE_MTR_ERROR_TYPE_MTR_PARAMS, NULL, "Meter parameter is empty.");
    }

    int ret = 0;
    uint16_t hovs_port_id;
    struct hinic3_meter_node *meter = NULL;
    struct hinic3_mtr_profile_node *profile_node = NULL;

    hinic3_mtr_profile_list_lock();
    profile_node = hinic3_mtr_profile_find(meter_profile_id);
    if (profile_node == NULL) {
        hinic3_mtr_profile_list_unlock();
        HINIC3_LOG(ERR, QOS, "Meter update: Profile id is not find.");
        return -rte_mtr_error_set(error, EPERM, RTE_MTR_ERROR_TYPE_METER_PROFILE_ID, NULL, "Profile id is not find.");
    }

    hinic3_meter_list_lock();
    meter = hinic3_meter_find(meter_id);
    if (meter == NULL) {
        hinic3_meter_list_unlock();
        hinic3_mtr_profile_list_unlock();
        HINIC3_LOG(ERR, QOS, "Meter update: Meter id is not find.");
        return -rte_mtr_error_set(error, EPERM, RTE_MTR_ERROR_TYPE_MTR_ID, NULL, "Meter id is not find.");
    }
    hinic3_update_profile(meter, profile_node);

    switch (meter->type) {
        case QOS_TYPE_VM_LIMIT:
            ret = hinic3_vm_qos_limit_set(meter->group_id, meter->dir,
                profile_node->profile.packet_mode, profile_node->profile);
            break;
        case QOS_TYPE_FUNC_LIMIT:
            hovs_port_id = (uint16_t)hinic3_process_port_id(meter->port_id);
            ret = hinic3_port_qos_limit_set(hovs_port_id, meter->dir,
                profile_node->profile.packet_mode, &profile_node->profile);
            break;
        case QOS_TYPE_NET_LIMIT:
            ret = hinic3_net_qos_limit_set(0, meter->dir, profile_node->profile.packet_mode,
                profile_node->profile.min_rate, profile_node->profile.min_burst);
            break;
        default:
            break;
    }
    hinic3_meter_list_unlock();
    hinic3_mtr_profile_list_unlock();

    if (ret != 0) {
        HINIC3_LOG(ERR, QOS, "Meter update: Update limit to hiovs failed.");
        return -rte_mtr_error_set(error, EPERM, RTE_MTR_ERROR_TYPE_UNSPECIFIED, NULL, "Dpak internal error.");
    }
    return 0;
}

static void hinic3_meter_node_remove(struct hinic3_meter_node *meter)
{
    if (meter->policy->used_num > 0 && meter->profile->used_num > 0) {
        meter->policy->used_num--;
        meter->profile->used_num--;
    } else {
        HINIC3_LOG(ERR, QOS, "Meter destroy: policy or profile num error.");
    }

    hinic3_list_remove(&meter->node);
    hinic3_free(meter);
    g_hinic3_meter_list.length--;
}

static int hinic3_port_meter_remove_from_group(struct hinic3_group_port_info *port_info)
{
    int ret;
    ret = hinic3_port_mgmt_set_qos_id(port_info->vport_id, UNBIND_QOS_ID);
    if (ret != 0) {
        HINIC3_LOG(ERR, QOS, "Meter destroy: Port unbind failed.port id is %u.", port_info->vport_id);
        return -1;
    }

    hinic3_list_remove(&port_info->node);
    hinic3_free(port_info);
    return 0;
}

static int hinic3_meter_group_qos_remove(struct hinic3_meter_node *meter)
{
    int ret;
    struct hinic3_group_port_info *iter = NULL;
    struct hinic3_group_port_info *next_iter = NULL;
    struct qos_single_value qos_value = { 0 };
    struct hinic3_group_info *group_info = &g_hinic3_group_infos[meter->group_id];
    enum hinic3_qos_tablehead index = hinic3_get_multi_qos_show_tablehead(meter->dir,
        meter->profile->profile.packet_mode);
    if (index >= QOS_TABLEHEAD_NUM) {
        HINIC3_LOG(ERR, QOS, "Meter destroy: get qos table head failed!");
        return -1;
    }
    qos_value.is_RFC2697 = meter->profile->profile.is_RFC2697;
    qos_value.packet_mode = meter->profile->profile.packet_mode;
    hinic3_group_info_lock(meter->group_id);
    ret = hinic3_vm_qos_limit_set(meter->group_id, meter->dir, meter->profile->profile.packet_mode, qos_value);
    if (ret != 0) {
        HINIC3_LOG(ERR, QOS, "Meter destroy: delete group qos failed!");
        hinic3_group_info_unlock(meter->group_id);
        return -1;
    }

    group_info->meter[index].is_used = false;
    if (group_info->meter[BW_TX_TYPE].is_used == false && group_info->meter[PPS_TX_TYPE].is_used == false &&
        group_info->meter[BW_RX_TYPE].is_used == false && group_info->meter[PPS_RX_TYPE].is_used == false) {
        LIST_FOR_EACH_SAFE(iter, next_iter, node, &group_info->node) {
            ret = hinic3_port_meter_remove_from_group(iter);
            if (ret != 0) {
                hinic3_group_info_unlock(meter->group_id);
                return -1;
            }
            group_info->length--;
        }
        if (group_info->length != 0) {
            HINIC3_LOG(ERR, QOS, "Meter destroy: group info length error. group id is %u, length is %u!",
                meter->group_id, group_info->length);
            group_info->length = 0;
        }
    }

    hinic3_group_info_unlock(meter->group_id);
    hinic3_meter_node_remove(meter);
    return 0;
}

static int hinic3_meter_net_qos_remove(struct hinic3_meter_node *meter)
{
    int ret;
    enum hinic3_qos_tablehead index = hinic3_get_multi_qos_show_tablehead(meter->dir,
        meter->profile->profile.packet_mode);
    if (index >= QOS_TABLEHEAD_NUM) {
        HINIC3_LOG(ERR, QOS, "Meter destroy: get qos table head failed!");
        return -1;
    }

    ret = hinic3_net_qos_limit_set(0, meter->dir, meter->profile->profile.packet_mode, 0, 0);
    if (ret != 0) {
        HINIC3_LOG(ERR, QOS, "Meter destroy: delete net qos failed!");
        return -1;
    }
    g_hinic3_net_meter_info[index].is_used = false;
    hinic3_meter_node_remove(meter);
    return 0;
}

static int hinic3_vm_qos_clear(struct hinic3_vf_dev *vf_dev, enum hinic3_qos_tablehead index)
{
    int ret;

    vf_dev->mtr[index].is_used = false;
    /* 没有配置VM QOS，不需要清理 */
    if (vf_dev->group_qos_id == 0) {
        return 0;
    }

    if (vf_dev->mtr[BW_TX_TYPE].is_used == false && vf_dev->mtr[PPS_TX_TYPE].is_used == false &&
        vf_dev->mtr[BW_RX_TYPE].is_used == false && vf_dev->mtr[PPS_RX_TYPE].is_used == false) {
        vf_dev->qos_type = VF_NONE_QOS_FLAG;
        ret = hinic3_group_meter_remove(vf_dev->vport_id, vf_dev->group_qos_id);
        if (ret != 0) {
            HINIC3_LOG(ERR, QOS, "Multi delete: remove group qos failed.");
            return -1;
        }
        vf_dev->group_qos_id = 0;
    }
    return 0;
}

static int hinic3_meter_destroy(struct rte_eth_dev *dev, uint32_t meter_id, struct rte_mtr_error *error)
{
    if (dev == NULL || error == NULL) {
        HINIC3_LOG(ERR, QOS, "Meter destroy: Meter parameter is empty.");
        return -rte_mtr_error_set(error, EPERM, RTE_MTR_ERROR_TYPE_MTR_PARAMS, NULL,
            "Meter parameter is empty.");
    }

    int ret = 0;
    struct hinic3_meter_node *meter = NULL;

    hinic3_meter_list_lock();
    meter = hinic3_meter_find(meter_id);
    if (meter == NULL) {
        hinic3_meter_list_unlock();
        HINIC3_LOG(ERR, QOS, "Meter destroy: Meter id is not find.");
        return -rte_mtr_error_set(error, EPERM, RTE_MTR_ERROR_TYPE_MTR_ID, NULL,
            "Meter id is not find.");
    }

    switch (meter->type) {
        case QOS_TYPE_MAX:
            hinic3_meter_node_remove(meter);
            break;
        case QOS_TYPE_VM_LIMIT:
            ret = hinic3_meter_group_qos_remove(meter);
            break;
        case QOS_TYPE_FUNC_LIMIT:
            HINIC3_LOG(ERR, QOS, "Meter destroy: Meter id is bind port.");
            ret = -1;
            break;
        case QOS_TYPE_NET_LIMIT:
            ret = hinic3_meter_net_qos_remove(meter);
            break;
        case QOS_TYPE_FLOW_LIMIT:
            HINIC3_LOG(ERR, QOS, "Meter destroy: Meter id is bind flow.");
            ret = -1;
            break;
        default:
            break;
    }

    hinic3_meter_list_unlock();
    if (ret != 0) {
        return -rte_mtr_error_set(error, EPERM, RTE_MTR_ERROR_TYPE_UNSPECIFIED, NULL,
        "Dpak internal error.");
    }
    return 0;
}

static int hinic3_get_meter_info(struct rte_mtr_params *params, struct hinic3_meter_node *meter)
{
    struct hinic3_mtr_policy_node *policy = NULL;
    struct hinic3_mtr_profile_node *profile = NULL;
    uint32_t policy_id = params->meter_policy_id;
    uint32_t profile_id = params->meter_profile_id;

    policy = hinic3_mtr_policy_find(policy_id);
    if (policy == NULL) {
        HINIC3_LOG(ERR, QOS, "Meter add: Meter policy id is not find.");
        return -RTE_MTR_ERROR_TYPE_METER_POLICY_ID;
    }
    meter->policy = policy;

    profile = hinic3_mtr_profile_find(profile_id);
    if (profile == NULL) {
        HINIC3_LOG(ERR, QOS, "Meter add: Meter profile id is not find.");
        return -RTE_MTR_ERROR_TYPE_METER_PROFILE_ID;
    }
    meter->profile = profile;
    return 0;
}

static void hinic3_meter_conf_set(struct hinic3_meter_node *meter, uint32_t meter_id)
{
    meter->profile->used_num++;
    meter->policy->used_num++;
    meter->type = QOS_TYPE_MAX;
    meter->group_id = 0;
    meter->flow = NULL;
    meter->meter_id = meter_id;
    meter->dir = HINIC3_NODIR_METER_QOS;
    meter->flow_num = 0;
    g_hinic3_meter_list.length++;
}

static int hinic3_meter_create(struct rte_eth_dev *dev, uint32_t meter_id, struct rte_mtr_params *params,
    int shared HINIC3_UNUSED, struct rte_mtr_error *error)
{
    if (dev == NULL || params == NULL || error == NULL) {
        HINIC3_LOG(ERR, QOS, "Meter add: Meter parameter is empty.");
        return -rte_mtr_error_set(error, EPERM, RTE_MTR_ERROR_TYPE_MTR_PARAMS, NULL,
            "Meter parameter is empty.");
    }

    int ret;
    struct hinic3_meter_node *meter = NULL;

    hinic3_meter_mutex_all_lock();
    meter = hinic3_meter_find(meter_id);
    if (meter != NULL) {
        hinic3_meter_mutex_all_unlock();
        HINIC3_LOG(ERR, QOS, "Meter add: Meter id is exist.");
        return -rte_mtr_error_set(error, EEXIST, RTE_MTR_ERROR_TYPE_MTR_ID, NULL,
            "Meter id is exist.");
    }

    meter = hinic3_calloc(1, sizeof(struct hinic3_meter_node), HINIC3_QOS);
    if (meter == NULL) {
        hinic3_meter_mutex_all_unlock();
        HINIC3_LOG(ERR, QOS, "Meter add: Meter memory alloc failed.");
        return -rte_mtr_error_set(error, ENOMEM, RTE_MTR_ERROR_TYPE_UNSPECIFIED, NULL,
            "Meter memory alloc failed.");
    }

    ret = hinic3_get_meter_info(params, meter);
    switch (ret) {
        case -RTE_MTR_ERROR_TYPE_METER_POLICY_ID:
            hinic3_free(meter);
            hinic3_meter_mutex_all_unlock();
            return -rte_mtr_error_set(error, EPERM, RTE_MTR_ERROR_TYPE_METER_POLICY_ID, NULL,
                "Meter add: Meter policy id is not find.");
        case -RTE_MTR_ERROR_TYPE_METER_PROFILE_ID:
            hinic3_free(meter);
            hinic3_meter_mutex_all_unlock();
            return -rte_mtr_error_set(error, EPERM, RTE_MTR_ERROR_TYPE_METER_PROFILE_ID, NULL,
                "Meter add: Meter profile id is not find.");
        default:
            break;
    }

    hinic3_list_init(&meter->node);
    hinic3_meter_conf_set(meter, meter_id);
    hinic3_list_insert(&g_hinic3_meter_list.node, &meter->node);
    hinic3_meter_mutex_all_unlock();
    return 0;
}

static int hinic3_get_qos_stats(struct hinic3_hqos_stats_context stats_context, struct rte_mtr_stats *stats)
{
    int ret;
    struct hovs_hqos_stats_batch_all hovs_stats = {0};
    ret = hinic3_hqos_statistics_get_all_batch(stats_context.type, &stats_context.ids, &hovs_stats, 1);
    if (ret != 0)
    {
        HINIC3_LOG(ERR, QOS, "Get %d qos statistics failed. ret = %d, ids = %u!",
                  stats_context.type, ret, stats_context.ids);
        return -1;
    }

    if (stats_context.dir == HINIC3_TX_METER_QOS)
    {
        stats->n_pkts[RTE_COLOR_GREEN] = hovs_stats.tx_pkts[RTE_COLOR_GREEN];
        stats->n_bytes[RTE_COLOR_GREEN] = hovs_stats.tx_bytes[RTE_COLOR_GREEN];
        stats->n_bytes_dropped = hovs_stats.tx_drop_bytes;
        stats->n_pkts_dropped = hovs_stats.tx_drop_pkts;
    }
    else if (stats_context.dir == HINIC3_RX_METER_QOS)
    {
        stats->n_pkts[RTE_COLOR_GREEN] = hovs_stats.tx_pkts[RTE_COLOR_GREEN];
        stats->n_bytes[RTE_COLOR_GREEN] = hovs_stats.tx_bytes[RTE_COLOR_GREEN];
        stats->n_bytes_dropped = hovs_stats.tx_drop_bytes;
        stats->n_pkts_dropped = hovs_stats.tx_drop_pkts;
    }
    else if (stats_context.type == QOS_TYPE_FLOW_LIMIT)
    {
        // 流表级限速不区分方向，只使用tx表示绿色报文
        stats->n_pkts[RTE_COLOR_GREEN] = hovs_stats.tx_pkts[RTE_COLOR_GREEN];
        stats->n_bytes[RTE_COLOR_GREEN] = hovs_stats.tx_bytes[RTE_COLOR_GREEN];
        // 流表级限速的模式下，tx代表丢包
        stats->n_bytes_dropped = hovs_stats.tx_drop_bytes;
        stats->n_pkts_dropped = hovs_stats.tx_drop_pkts;
    }

    if (stats_context.clear != 0)
    {
        ret = hinic3_hqos_statistics_clear(stats_context.type, stats_context.dir, &stats_context.ids, 1);
        if (ret != 0)
        {
            HINIC3_LOG(ERR, QOS, "Clear %d qos statistics failed. ret = %d, ids = %u!",
                      stats_context.type, ret, stats_context.dir);
            return -1;
        }
    }
    return 0;
}

static int hinic3_get_meter_stats(struct hinic3_meter_node *meter, struct rte_mtr_stats *stats, int clear)
{
    int ret;
    struct hinic3_vf_dev *vf_dev = NULL;
    struct rte_eth_dev *dev = NULL;
    struct hinic3_hqos_stats_context stats_context = {0};
    stats_context.dir = meter->dir;
    stats_context.clear = clear;

    switch (meter->type)
    {
    case QOS_TYPE_VM_LIMIT:
        stats_context.type = QOS_TYPE_VM_LIMIT;
        stats_context.ids = meter->group_id;
        ret = hinic3_get_qos_stats(stats_context, stats);
        break;
    case QOS_TYPE_FUNC_LIMIT:
        if (meter->flow == NULL)
        {
            HINIC3_LOG(ERR, QOS, "Meter is not bind flow!");
            return -1;
        }
        RTE_ETH_VALID_PORTID_OR_ERR_RET(meter->flow->port_id, -1);
        dev = &rte_eth_devices[meter->flow->port_id];
        vf_dev = dev->data->dev_private;
        if (vf_dev == NULL)
        {
            HINIC3_LOG(ERR, QOS, "The current device private_data is NULL!");
            return -1;
        }
        stats_context.type = QOS_TYPE_FUNC_LIMIT;
        stats_context.ids = vf_dev->vport_id;
        ret = hinic3_get_qos_stats(stats_context, stats);
        break;
    case QOS_TYPE_NET_LIMIT:
        stats_context.type = QOS_TYPE_NET_LIMIT;
        ret = hinic3_get_qos_stats(stats_context, stats);
        break;
    case QOS_TYPE_FLOW_LIMIT:
        stats_context.type = QOS_TYPE_FLOW_LIMIT;
        stats_context.ids = meter->qos_id;
        ret = hinic3_get_qos_stats(stats_context, stats);
        break;
    default:
        ret = -1;
        HINIC3_LOG(ERR, QOS, "Meter type is error!");
        break;
    }
    if (ret != 0)
    {
        return -1;
    }

    return 0;
}

static int hinic3_meter_stats_read(struct rte_eth_dev *eth_dev, uint32_t mtr_id, struct rte_mtr_stats *stats,
    uint64_t *stats_mask, int clear, struct rte_mtr_error *error)
{
    int ret;
    struct hinic3_meter_node *meter = NULL;

    if (eth_dev == NULL || stats == NULL || stats_mask == NULL || error == NULL) {
        HINIC3_LOG(ERR, QOS, "Multi meter stats read parameter is empty!");
        return rte_mtr_error_set(error, EPERM, RTE_MTR_ERROR_TYPE_UNSPECIFIED, NULL,
            "Meter stats read parameter is empty.");
    }

    hinic3_meter_list_lock();
    meter = hinic3_meter_find(mtr_id);
    if (meter == NULL) {
        hinic3_meter_list_unlock();
        HINIC3_LOG(ERR, QOS, "Meter id is not find!");
        return -rte_mtr_error_set(error, EEXIST, RTE_MTR_ERROR_TYPE_MTR_ID, NULL,
            "Meter id is not find.");
    }

    if (meter->type == QOS_TYPE_MAX) {
        hinic3_meter_list_unlock();
        HINIC3_LOG(ERR, QOS, "Meter is not bind port!");
        return -rte_mtr_error_set(error, EPERM, RTE_MTR_ERROR_TYPE_MTR_ID, NULL,
            "Meter is not bind port.");
    }

    ret = hinic3_get_meter_stats(meter, stats, clear);
    if (ret != 0) {
        hinic3_meter_list_unlock();
        return -rte_mtr_error_set(error, EPERM, RTE_MTR_ERROR_TYPE_UNSPECIFIED, NULL,
            "Dpak internal error.");
    }
    *stats_mask = RTE_MTR_STATS_N_BYTES_DROPPED | RTE_MTR_STATS_N_PKTS_DROPPED |
        RTE_MTR_STATS_N_PKTS_GREEN | RTE_MTR_STATS_N_BYTES_GREEN;
    hinic3_meter_list_unlock();
    return 0;
}

static int hinic3_meter_actions_parse(const struct rte_flow_action *actions, uint32_t *meter_id,
    enum hinic3_meter_qos_direction *direction, uint16_t *port_id)
{
    const struct rte_flow_action *act = next_action(actions, NULL);

    while (act && act->type != RTE_FLOW_ACTION_TYPE_END) {
        if (act->conf == NULL) {
            HINIC3_LOG(ERR, QOS, "Multi qos: rte_flow_action->conf is null!");
            return -1;
        }
        switch (act->type) {
            case RTE_FLOW_ACTION_TYPE_METER:
                *meter_id = ((const struct rte_flow_action_meter*)act->conf)->mtr_id;
                break;
            case RTE_FLOW_ACTION_TYPE_PORT_ID:
                *direction = HINIC3_RX_METER_QOS;
                *port_id = ((const struct rte_flow_action_port_id*)act->conf)->id;
                break;
            default:
                break;
        }
        act = next_action(actions, act);
    }
    return 0;
}

static int hinic3_meter_pattern_parse(const struct rte_flow_item *pattern, uint16_t *port_id)
{
    const struct rte_flow_item *item = next_no_end_pattern(pattern, NULL);

    while (item) {
        if (item->spec == NULL) {
            HINIC3_LOG(ERR, QOS, "Multi qos: rte_flow_item->spec is null.");
            return -1;
        }
        switch (item->type) {
            case RTE_FLOW_ITEM_TYPE_PORT_ID:
                *port_id = ((const struct rte_flow_item_port_id*)item->spec)->id;
                break;
            default:
                break;
        }
        item = next_no_end_pattern(pattern, item);
    }
    return 0;
}

static int hinic3_add_port_to_list(uint16_t vport_id, struct hinic3_meter_node *meter)
{
    int ret;
    struct hinic3_group_port_info *port_info = NULL;

    if (meter->group_id >= HINIC3_METER_NUM_MAX || 
        g_hinic3_group_infos[meter->group_id].length > HINIC3_METER_PORT_NUM_MAX) {
        HINIC3_LOG(ERR, QOS, "Multi qos: Group port number reached the upper limit. group id is %u.",
            meter->group_id);
        return -1;
    }

    ret = hinic3_port_mgmt_set_qos_id(vport_id, meter->group_id);
    if (ret != 0) {
        HINIC3_LOG(ERR, QOS, "Multi qos: Failed to bind the port to the group qos, vport_id is %u.", vport_id);
        return -1;
    }

    port_info = hinic3_calloc(1, sizeof(struct hinic3_group_port_info), HINIC3_QOS);
    if (port_info == NULL) {
        HINIC3_LOG(ERR, QOS, "Multi qos: Group port info alloc failed.");
        return -1;
    }

    port_info->vport_id = vport_id;
    hinic3_list_init(&port_info->node);
    hinic3_list_insert(&g_hinic3_group_infos[meter->group_id].node, &port_info->node);
    g_hinic3_group_infos[meter->group_id].length++;
    return 0;
}

static uint16_t hinic3_meter_group_find_location(void)
{
    int i;
    for (i = 1; i < HINIC3_METER_NUM_MAX; i++) {
        if (g_hinic3_group_infos[i].length == 0) {
            return i;
        }
    }
    return 0;
}

static int hinic3_qos_set_to_hovs(enum qos_type_limit qos_type, uint16_t ids, enum hinic3_meter_qos_direction dir,
    struct hinic3_mtr_profile_node *profile_node)
{
    int ret;

    /* 下发限速前，先清理计数器 */
    ret = hinic3_hqos_statistics_clear(qos_type, dir, &ids, 1);
    if (ret != 0) {
        HINIC3_LOG(ERR, QOS, "Multi qos: Failed to clear qos statistics. ids is %u. qos_type is %d. ret is %d.",
            ids, qos_type, ret);
        return -1;
    }

    switch (qos_type) {
        case QOS_TYPE_FUNC_LIMIT:
            ret = hinic3_port_qos_limit_set(ids, dir, profile_node->profile.packet_mode, &profile_node->profile);
            break;
        case QOS_TYPE_VM_LIMIT:
            ret = hinic3_vm_qos_limit_set(ids, dir, profile_node->profile.packet_mode, profile_node->profile);
            break;
        case QOS_TYPE_NET_LIMIT:
            /* 目前net级只支持单速单桶，忽略PIR(max_rate)和PBS(max_burst),传入cir(min_rate)和cbs(min_burst) */
            ret = hinic3_net_qos_limit_set(ids, dir, profile_node->profile.packet_mode,
                profile_node->profile.min_rate, profile_node->profile.min_burst);
            break;
        default:
            break;
    }
    if (ret != 0) {
        HINIC3_LOG(ERR, QOS, "Multi qos: set qos failed. ids is %u, qos type is %d.", ids, qos_type);
        return -1;
    }
    return 0;
}

static int hinic3_add_group_to_hovs(struct hinic3_vf_dev *vf_dev, struct hinic3_meter_node *meter,
    enum hinic3_qos_tablehead index, enum hinic3_meter_qos_direction dir)
{
    int ret = 0;
    struct hinic3_mtr_profile_node *profile_node = NULL;

    if (vf_dev->group_qos_id > 0 && g_hinic3_group_infos[vf_dev->group_qos_id].meter[index].is_used == false) {
        /* 如果vf_dev已经绑定group，且group的dir方向没有限速，则正常下发group限速 */
        meter->group_id = vf_dev->group_qos_id;
        hinic3_group_info_lock(meter->group_id);
    } else if (vf_dev->group_qos_id > 0 && g_hinic3_group_infos[vf_dev->group_qos_id].meter[index].is_used == true) {
        /* 如果vf_dev已经绑定group，且group的dir方向已经有限速，则返回失败 */
        HINIC3_LOG(ERR, QOS, "Multi qos: Group %u index %d is already set meter %u, Please delete or update.",
            vf_dev->group_qos_id, index, meter->meter_id);
        return -1;
    } else {
        /* 如果vf_dev没有绑定group，则新分配一个group_id，并把端口绑定在group中 */
        meter->group_id = hinic3_meter_group_find_location();
        if (meter->group_id == 0) {
            HINIC3_LOG(ERR, QOS, "Multi qos: Group number reached the upper limit.");
            return -1;
        }
        hinic3_group_info_lock(meter->group_id);
        ret = hinic3_add_port_to_list(vf_dev->vport_id, meter);
        if (ret != 0) {
            hinic3_group_info_unlock(meter->group_id);
            return -1;
        }
    }

    profile_node = meter->profile;

    ret = hinic3_qos_set_to_hovs(QOS_TYPE_VM_LIMIT, meter->group_id, dir, profile_node);
    if (ret != 0) {
        hinic3_group_info_unlock(meter->group_id);
        return -1;
    }

    vf_dev->group_qos_id = meter->group_id;
    meter->type = QOS_TYPE_VM_LIMIT;
    meter->dir = dir;
    g_hinic3_group_infos[meter->group_id].meter[index].meter_id = meter->meter_id;
    g_hinic3_group_infos[meter->group_id].meter[index].is_used = true;
    hinic3_group_info_unlock(meter->group_id);
    return 0;
}

static int hinic3_get_packet_mode(enum hinic3_qos_tablehead index)
{
    switch (g_hinic3_qos_mask_array[index]) {
        case QOS_PORT_BPS:
        case QOS_VM_BPS:
        case QOS_NET_BPS:
            return QOS_BW_TYPE;
        case QOS_PORT_PPS:
        case QOS_VM_PPS:
        case QOS_NET_PPS:
            return QOS_PPS_TYPE;
        default:
            return -1;
    }
}

static int hinic3_group_meter_clear(uint32_t meter_id, uint16_t group_id)
{
    int ret;
    struct hinic3_meter_node *meter = NULL;
    struct qos_single_value qos_value = {0};

    meter = hinic3_meter_find(meter_id);
    if (meter == NULL) {
        HINIC3_LOG(ERR, QOS, "Meter clear id is not find!");
        return -1;
    }
    qos_value.is_RFC2697 = meter->profile->profile.is_RFC2697;
    qos_value.packet_mode = meter->profile->profile.packet_mode;
    ret = hinic3_vm_qos_limit_set(group_id, meter->dir, meter->profile->profile.packet_mode, qos_value);
    if (ret != 0) {
        HINIC3_LOG(ERR, QOS, "Group meter del failed!");
        return -1;
    }
    meter->type = QOS_TYPE_MAX;
    meter->group_id = 0;
    meter->dir = HINIC3_NODIR_METER_QOS;
    return 0;
}

static int hinic3_group_meter_remove_sub(struct hinic3_group_info *group_info, uint16_t group_id, int index)
{
    int ret;

    if (group_info->meter[index].is_used == true) {
        ret = hinic3_group_meter_clear(group_info->meter[index].meter_id, group_id);
        if (ret != 0) {
            return -1;
        }
        group_info->meter[index].is_used = false;
    }
    return 0;
}

int hinic3_group_meter_remove(uint16_t vport_id, uint16_t group_id)
{
    int ret = 0;
    struct hinic3_group_port_info *iter = NULL;
    struct hinic3_group_port_info *next_iter = NULL;
    if (group_id >= HINIC3_METER_NUM_MAX) {
        HINIC3_LOG(ERR, QOS, "hinic3_group_meter_remove: group_id %u is invalid.", group_id);
        return -1;
    }
    struct hinic3_group_info *group_info = &g_hinic3_group_infos[group_id];

    hinic3_group_info_lock(group_id);
    LIST_FOR_EACH_SAFE(iter, next_iter, node, &group_info->node) {
        if (iter->vport_id == vport_id) {
            ret = hinic3_port_meter_remove_from_group(iter);
            if (ret != 0) {
                hinic3_group_info_unlock(group_id);
                return -1;
            }
            group_info->length--;
        }
    }
    if (group_info->length == 0) {
        for (int i = 0; i < HINIC3_METER_DIR_NUM; i++) {
            ret = hinic3_group_meter_remove_sub(group_info, group_id, i);
            if (ret != 0) {
                HINIC3_LOG(ERR, QOS, "Meter clear failed. group id is %u, index is %d.", group_id, i);
            }
        }
    }
    hinic3_group_info_unlock(group_id);
    return ret;
}

static int hinic3_add_port_to_group(struct hinic3_vf_dev *vf_dev, struct hinic3_meter_node *meter,
    enum hinic3_meter_qos_direction dir)
{
    int ret;
    if (vf_dev->group_qos_id > 0 && vf_dev->group_qos_id != meter->group_id) {
        HINIC3_LOG(ERR, QOS, "Multi qos: dev and meter is already bind group, please update or delete.");
        return -1;
    } else if (vf_dev->group_qos_id > 0 && vf_dev->group_qos_id == meter->group_id) {
        /* 端口已经绑定在group中，直接返回 */
        return 0;
    }

    if (dir != meter->dir) {
        HINIC3_LOG(ERR, QOS, "Multi qos: dir error, meter_id is %u, vport_id is %u, dir is %d", meter->meter_id,
            vf_dev->vport_id, dir);
        return -1;
    }
    hinic3_group_info_lock(meter->group_id);
    ret = hinic3_add_port_to_list(vf_dev->vport_id, meter);
    if (ret != 0) {
        hinic3_group_info_unlock(meter->group_id);
        return -1;
    }
    vf_dev->group_qos_id = meter->group_id;
    hinic3_group_info_unlock(meter->group_id);
    return 0;
}

static int hinic3_update_net_qos(enum hinic3_qos_tablehead index)
{
    struct hinic3_meter_node *meter = NULL;

    meter = hinic3_meter_find(g_hinic3_net_meter_info[index].meter_id);
    if (meter == NULL) {
        HINIC3_LOG(ERR, QOS, "Muli qos: update net qos failed, meter id is %u.",
            g_hinic3_net_meter_info[index].meter_id);
        return -1;
    }
    meter->type = QOS_TYPE_MAX;

    return 0;
}

static int hinic3_remove_port_qos(uint32_t meter_id, uint16_t vport_id)
{
    int ret;
    struct hinic3_meter_node *meter = NULL;
    struct qos_single_value qos_value = { 0 };

    hinic3_meter_list_lock();
    meter = hinic3_meter_find(meter_id);
    if (meter == NULL) {
        hinic3_meter_list_unlock();
        HINIC3_LOG(ERR, QOS, "Multi meter: meter id is not find. meter id is %u.", meter_id);
        return -1;
    }

    if (meter->type != QOS_TYPE_FUNC_LIMIT) {
        hinic3_meter_list_unlock();
        HINIC3_LOG(ERR, QOS, "Multi meter: meter type error.");
    }

    ret = hinic3_port_qos_limit_set(vport_id, meter->dir, meter->profile->profile.packet_mode, &qos_value);
    if (ret != 0) {
        HINIC3_LOG(ERR, QOS, "Multi delete: remove port tx qos failed.");
    }
    meter->type = QOS_TYPE_MAX;
    hinic3_free(meter->flow);
    meter->flow = NULL;
    hinic3_meter_list_unlock();
    return 0;
}

void hinic3_port_meter_clear(struct hinic3_vf_dev *vf_dev, uint16_t vport_id)
{
    int ret = 0;
    for (int i = 0; i < QOS_TABLEHEAD_NUM; i++) {
        if (vf_dev->mtr[i].is_used == true) {
            if (hinic3_remove_port_qos(vf_dev->mtr[i].mtr_id, vport_id) != 0) {
                HINIC3_LOG(ERR, QOS, "Multi qos: remove port qos failed. vport id is %u, index is %d.",
                    vport_id, i);
            }
        }
    }

    ret = hinic3_vf_qos_statistics_clear(&vf_dev->vport_id);
    if (ret != 0) {
        HINIC3_LOG(ERR, QOS, "Multi qos: group port statistics not clear. vport id is %u", vf_dev->vport_id);
    }
}

static void hinic3_qos_port_clear(struct hinic3_vf_dev *vf_dev, enum hinic3_meter_qos_direction dir, uint64_t *qos_mask)
{
    int ret;
    enum hinic3_qos_tablehead index;
    struct hinic3_meter_node *meter = NULL;
    struct hinic3_mtr_profile_node profile_node = { 0 };

    if ((*qos_mask & QOS_PORT_BPS) != 0) {
        *qos_mask &= ~QOS_PORT_BPS;
        index = hinic3_get_multi_qos_show_tablehead(dir, QOS_BW_TYPE);
    } else {
        *qos_mask &= ~QOS_PORT_PPS;
        index = hinic3_get_multi_qos_show_tablehead(dir, QOS_PPS_TYPE);
    }
    if (index >= QOS_TABLEHEAD_NUM) {
        HINIC3_LOG(ERR, QOS, "Multi qos: get qos index fail.");
        return;
    }

    meter = hinic3_meter_find(vf_dev->mtr[index].mtr_id);
    if (meter == NULL) {
        HINIC3_LOG(ERR, QOS, "Multi qos: meter not find. meter id is %u.", vf_dev->mtr[index].mtr_id);
        return;
    }
    profile_node.profile.packet_mode = meter->profile->profile.packet_mode;

    ret = hinic3_qos_set_to_hovs(QOS_TYPE_FUNC_LIMIT, vf_dev->vport_id, dir, &profile_node);
    if (ret != 0) {
        HINIC3_LOG(ERR, QOS, "Multi qos: clear port qos fail. vport id is %u.", vf_dev->vport_id);
        return;
    }

    meter->type = QOS_TYPE_MAX;
    meter->dir = HINIC3_NODIR_METER_QOS;
    vf_dev->mtr[index].is_used = false;
    vf_dev->qos_type = VF_NONE_QOS_FLAG;
}

static void hinic3_qos_vm_clear_sub(struct hinic3_group_info *group_info, struct hinic3_meter_node *meter,
    enum hinic3_qos_tablehead index, uint16_t vport_id)
{
    int ret = 0;
    struct hinic3_group_port_info *iter = NULL;
    struct hinic3_group_port_info *next_iter = NULL;

    /* 如果端口在group中且唯一，则清除相应方向的group限速 */
    hinic3_group_info_lock(group_info->group_id);
    LIST_FOR_EACH_SAFE(iter, next_iter, node, &group_info->node) {
        if (iter->vport_id == vport_id && group_info->length == 1) {
            ret = hinic3_group_meter_clear(meter->meter_id, group_info->group_id);
            group_info->meter[index].is_used = false;
        }
    }

    if (ret != 0) {
        HINIC3_LOG(ERR, QOS, "Multi qos: clear group qos fail. group id is %u.", group_info->group_id);
    }

    hinic3_group_info_unlock(group_info->group_id);
}

static void hinic3_qos_vm_clear(struct hinic3_vf_dev *vf_dev, enum hinic3_meter_qos_direction dir, uint64_t *qos_mask)
{
    int ret;
    bool is_unbind = true;
    enum hinic3_qos_tablehead index;
    struct hinic3_meter_node *meter = NULL;
    struct hinic3_group_info *group_info = NULL;

    if ((*qos_mask & QOS_VM_BPS) != 0) {
        *qos_mask &= ~QOS_VM_BPS;
        index = hinic3_get_multi_qos_show_tablehead(dir, QOS_BW_TYPE);
    } else {
        *qos_mask &= ~QOS_VM_PPS;
        index = hinic3_get_multi_qos_show_tablehead(dir, QOS_PPS_TYPE);
    }
    if (index >= QOS_TABLEHEAD_NUM) {
        HINIC3_LOG(ERR, QOS, "Multi qos: get qos index fail.");
        return;
    }

    if (vf_dev->group_qos_id == 0 || vf_dev->group_qos_id >= HINIC3_METER_NUM_MAX 
        || g_hinic3_group_infos[vf_dev->group_qos_id].meter[index].is_used == false) {
        return;
    }

    group_info = &g_hinic3_group_infos[vf_dev->group_qos_id];
    meter = hinic3_meter_find(group_info->meter[index].meter_id);
    if (meter == NULL) {
        return;
    }

    if (meter->type != QOS_TYPE_VM_LIMIT) {
        return;
    }

    /* 如果group中还用别的模式、别的方向的限速，则不能将端口移除group */
    for (uint8_t i = 0; i < HINIC3_METER_DIR_NUM; i++) {
        if (i != index && group_info->meter[i].is_used == true) {
            is_unbind = false;
        }
    }

    if (is_unbind == true) {
        ret = hinic3_group_meter_remove(vf_dev->vport_id, vf_dev->group_qos_id);
        if (ret != 0) {
            HINIC3_LOG(ERR, QOS, "Multi qos: remove port qos fail. vport id is %u. group id is %u.",
                vf_dev->vport_id, vf_dev->group_qos_id);
            return;
        }
        vf_dev->group_qos_id = 0;
        meter->type = QOS_TYPE_MAX;
        meter->dir = HINIC3_NODIR_METER_QOS;
    } else {
        hinic3_qos_vm_clear_sub(group_info, meter, index, vf_dev->vport_id);
    }
}

static void hinic3_qos_net_clear(struct hinic3_vf_dev *vf_dev HINIC3_UNUSED, enum hinic3_meter_qos_direction dir, uint64_t *qos_mask)
{
    int ret;
    enum hinic3_qos_tablehead index;
    struct hinic3_meter_node *meter = NULL;
    struct hinic3_mtr_profile_node profile_node = { 0 };

    if ((*qos_mask & QOS_NET_BPS) != 0) {
        *qos_mask &= ~QOS_NET_BPS;
        index = hinic3_get_multi_qos_show_tablehead(dir, QOS_BW_TYPE);
    } else {
        *qos_mask &= ~QOS_NET_PPS;
        index = hinic3_get_multi_qos_show_tablehead(dir, QOS_PPS_TYPE);
    }
    if (index >= QOS_TABLEHEAD_NUM) {
        HINIC3_LOG(ERR, QOS, "Multi qos: get qos index fail.");
        return;
    }

    meter = hinic3_meter_find(g_hinic3_net_meter_info[index].meter_id);
    if (meter == NULL) {
        HINIC3_LOG(ERR, QOS, "Multi qos: meter not find. meter id is %u.", g_hinic3_net_meter_info[index].meter_id);
        return;
    }
    if (meter->type != QOS_TYPE_NET_LIMIT) {
        HINIC3_LOG(WARNING, QOS, "Multi qos: net qos meter type is %d.", meter->type);
    }
    profile_node.profile.packet_mode = meter->profile->profile.packet_mode;

    ret = hinic3_qos_set_to_hovs(QOS_TYPE_NET_LIMIT, 0, dir, &profile_node);
    if (ret != 0) {
        HINIC3_LOG(ERR, QOS, "Multi qos: clear net qos fail. ret is %d. dir is %d. packet_mode is %d.",
            ret, dir, profile_node.profile.packet_mode);
        return;
    }

    g_hinic3_net_meter_info[index].is_used = false;
    meter->type = QOS_TYPE_MAX;
    meter->dir = HINIC3_NODIR_METER_QOS;
}

static struct hinic3_clear_qos_func_map g_hinic3_clear_qos_func_array[] = {
    {hinic3_qos_port_clear},
    {hinic3_qos_port_clear},
    {hinic3_qos_vm_clear},
    {hinic3_qos_vm_clear},
    {hinic3_qos_net_clear},
    {hinic3_qos_net_clear},
};

static void hinic3_clear_qos(struct hinic3_vf_dev *vf_dev, enum hinic3_meter_qos_direction dir, uint64_t *qos_mask)
{
    for (int i = 0; i < QOS_LEVEL_MAX_NUM; i++) {
        if ((*qos_mask & g_hinic3_qos_mask_array[i]) != 0) {
            g_hinic3_clear_qos_func_array[i].func(vf_dev, dir, qos_mask);
        }
    }
}

static void hinic3_set_next_meter(struct hinic3_mtr_policy_node *policy, uint32_t *next_meter_id)
{
    if (policy->has_next_meter == true) {
        *next_meter_id = policy->next_meter_id;
    } else {
        *next_meter_id = MAX_UINT32_NUM;
    }
}

static int hinic3_qos_port_set(uint16_t port_id, uint32_t meter_id, uint32_t *next_meter_id,
    enum hinic3_meter_qos_direction dir)
{
    int ret;
    enum hinic3_qos_tablehead index;
    struct hinic3_meter_node *meter = NULL;
    struct hinic3_mtr_profile_node *profile_node = NULL;
    struct hinic3_vf_dev *vf_dev = (struct hinic3_vf_dev *)hinic3_get_private_data(port_id);
    if (vf_dev == NULL) {
        HINIC3_LOG(ERR, QOS, "Multi qos: vf dev is not find, port id is %" PRIu16 ".", port_id);
        return -1;
    }

    meter = hinic3_meter_find(meter_id);
    if (meter == NULL) {
        HINIC3_LOG(ERR, QOS, "Multi qos: Meter id is not find, meter id is %u.", meter_id);
        return -1;
    }

    if (meter->type != QOS_TYPE_MAX) {
        HINIC3_LOG(ERR, QOS, "Multi qos: meter type is not right. meter id is %u.", meter_id);
        return -1;
    }
    profile_node = meter->profile;

    index = hinic3_get_multi_qos_show_tablehead(dir, profile_node->profile.packet_mode);
    if (index >= QOS_TABLEHEAD_NUM) {
        HINIC3_LOG(ERR, QOS, "Multi qos: get qos index fail. dir is %d. packet mode is %d.",
            dir, profile_node->profile.packet_mode);
        return -1;
    }

    if (vf_dev->mtr[index].is_used == true) {
        HINIC3_LOG(ERR, QOS, "Multi qos: port %u already qos, meter id is %u.", vf_dev->vport_id,
            vf_dev->mtr[index].mtr_id);
        return -1;
    }

    ret = hinic3_qos_set_to_hovs(QOS_TYPE_FUNC_LIMIT, vf_dev->vport_id, dir, profile_node);
    if (ret != 0) {
        return -1;
    }

    vf_dev->qos_type = VF_PORT_QOS_FLAG;
    vf_dev->mtr[index].direction = dir;
    vf_dev->mtr[index].is_used = true;
    vf_dev->mtr[index].mtr_id = meter_id;
    meter->dir = dir;
    meter->type = QOS_TYPE_FUNC_LIMIT;
    meter->port_id = port_id;
    hinic3_set_next_meter(meter->policy, next_meter_id);

    return 0;
}

static int hinic3_qos_vm_set(uint16_t port_id, uint32_t meter_id, uint32_t *next_meter_id,
    enum hinic3_meter_qos_direction dir)
{
    int ret = -1;
    enum hinic3_qos_tablehead index;
    struct hinic3_meter_node *meter = NULL;
    struct hinic3_mtr_profile_node *profile_node = NULL;
    struct hinic3_vf_dev *vf_dev = (struct hinic3_vf_dev*)hinic3_get_private_data(port_id);
    if (vf_dev == NULL) {
        HINIC3_LOG(ERR, QOS, "Multi qos: vf dev is not find, port id is %" PRIu16 ".", port_id);
        return -1;
    }

    meter = hinic3_meter_find(meter_id);
    if (meter == NULL) {
        HINIC3_LOG(ERR, QOS, "Multi qos: Meter id is not find, meter id is %u.", meter_id);
        return -1;
    }

    if (meter->type > QOS_TYPE_MAX) {
        HINIC3_LOG(ERR, QOS, "Multi qos: Meter type is not right, meter id is %u.", meter_id);
        return -1;
    }
    profile_node = meter->profile;

    index = hinic3_get_multi_qos_show_tablehead(dir, profile_node->profile.packet_mode);
    if (index >= QOS_TABLEHEAD_NUM) {
        HINIC3_LOG(ERR, QOS, "Multi qos: get qos index fail. dir is %d. packet mode is %d.",
            dir, profile_node->profile.packet_mode);
        return -1;
    }

    switch (meter->type) {
        case QOS_TYPE_MAX:
            ret = hinic3_add_group_to_hovs(vf_dev, meter, index, dir);
            break;
        case QOS_TYPE_VM_LIMIT:
            ret = hinic3_add_port_to_group(vf_dev, meter, dir);
            break;
        case QOS_TYPE_FUNC_LIMIT:
            HINIC3_LOG(ERR, QOS, "Multi qos: group meter id is already used. meter id is %u", meter->meter_id);
            break;
        default:
            break;
    }
    hinic3_set_next_meter(meter->policy, next_meter_id);

    return ret;
}

static int hinic3_qos_net_set(uint16_t port_id HINIC3_UNUSED, uint32_t meter_id, uint32_t *next_meter_id,
    enum hinic3_meter_qos_direction dir)
{
    int ret = -1;
    enum hinic3_qos_tablehead index;
    struct hinic3_meter_node *meter = NULL;
    struct hinic3_mtr_profile_node *profile_node = NULL;

    meter = hinic3_meter_find(meter_id);
    if (meter == NULL) {
        HINIC3_LOG(ERR, QOS, "Multi qos net set: meter not find. meter id is %u.", meter_id);
        return -1;
    }
    profile_node = meter->profile;

    index = hinic3_get_multi_qos_show_tablehead(dir, profile_node->profile.packet_mode);
    if (index >= QOS_TABLEHEAD_NUM) {
        HINIC3_LOG(ERR, QOS, "Multi qos: get qos index fail. dir is %d. packet mode is %d.",
            dir, profile_node->profile.packet_mode);
        return -1;
    }

    if (meter->type == QOS_TYPE_NET_LIMIT && g_hinic3_net_meter_info[index].is_used == true &&
        g_hinic3_net_meter_info[index].meter_id == meter_id) {
        goto end;
    } else if (meter->type != QOS_TYPE_MAX) {
        HINIC3_LOG(ERR, QOS, "Multi qos: net meter id already used.");
        return -1;
    }

    ret = hinic3_qos_set_to_hovs(QOS_TYPE_NET_LIMIT, 0, dir, profile_node);
    if (ret != 0) {
        return -1;
    }

    if (g_hinic3_net_meter_info[index].is_used == true) {
        ret = hinic3_update_net_qos(index);
        if (ret != 0) {
            return -1;
        }
    }
    g_hinic3_net_meter_info[index].meter_id = meter_id;
    g_hinic3_net_meter_info[index].is_used = true;
    meter->dir = dir;
    meter->type = QOS_TYPE_NET_LIMIT;
end:
    hinic3_set_next_meter(meter->policy, next_meter_id);
    return 0;
}

static struct hinic3_qos_func_map g_hinic3_qos_func_array[] = {
    {"port_bps",    hinic3_qos_port_set},
    {"port_pps",    hinic3_qos_port_set},
    {"vm_bps",      hinic3_qos_vm_set},
    {"vm_pps",      hinic3_qos_vm_set},
    {"net_bps",     hinic3_qos_net_set},
    {"net_pps",     hinic3_qos_net_set},
};

static bool hinic3_check_qos_level(uint64_t *qos_mask, int index, uint32_t meter_id, int *ret)
{
    int packet_mode;
    struct hinic3_meter_node *meter = NULL;

    if (((*qos_mask) & g_hinic3_qos_mask_array[index]) == g_hinic3_qos_mask_array[index]) {
        HINIC3_LOG(ERR, QOS, "Multi qos: qos level repeat. Please check configuration.");
        *ret = -1;
        return false;
    }
    meter = hinic3_meter_find(meter_id);
    if (meter == NULL) {
        HINIC3_LOG(ERR, QOS, "Multi qos: meter not find. meter id is %u.", meter_id);
        *ret = -1;
        return false;
    }
    packet_mode = hinic3_get_packet_mode(index);
    if (packet_mode < 0 || packet_mode != meter->profile->profile.packet_mode) {
        HINIC3_LOG(ERR, QOS, "Multi qos: packet mode wroing. qos level is %d. packet mode is %d. meter id is %u.",
            index, packet_mode, meter_id);
        *ret = -1;
        return false;
    }
    *qos_mask |= g_hinic3_qos_mask_array[index];
    *ret = 0;
    return true;
}

static int hinic3_set_qos_to_hovs(char *qos_level, uint16_t port_id, uint32_t meter_id,
    enum hinic3_meter_qos_direction dir)
{
    int ret = 0;
    bool flag = false;
    char *token = NULL;
    char delimiter  = ',';
    char *qos_level_str = NULL;
    uint32_t next_meter_id = 0;
    uint32_t cur_meter_id = meter_id;
    uint64_t qos_mask = 0;
    void *private_data = NULL;

    private_data = hinic3_get_private_data(port_id);
    if (private_data == NULL) {
        HINIC3_LOG(ERR, QOS, "Multi qos: The current device private_data is NULL.");
        return -1;
    }

    if (hinic3_is_bond_by_prefix(*(uint16_t*)private_data)) {
        HINIC3_LOG(ERR, QOS, "Bond device is not support set QoS.");
        return -1;
    }

    token = strtok_r(qos_level, &delimiter, &qos_level_str);
    while (token != NULL) {
        for (int i = 0; i < QOS_LEVEL_MAX_NUM; i++) {
            if (strcmp(token, g_hinic3_qos_func_array[i].qos_level) == 0 &&
                hinic3_check_qos_level(&qos_mask, i, cur_meter_id, &ret) == true) {
                ret = g_hinic3_qos_func_array[i].func(port_id, cur_meter_id, &next_meter_id, dir);
                flag = true;
            }
            if (ret != 0) {
                HINIC3_LOG(ERR, QOS, "Multi qos: set qos failed. qos level is %s. ret is %d", token, ret);
                goto clear;
            }
        }
        if (flag == false) {
            HINIC3_LOG(ERR, QOS, "Multi qos: qos level is wrong. qos level is %s.", token);
            goto clear;
        }
        if (next_meter_id == MAX_UINT32_NUM) {
            break;
        }
        cur_meter_id = next_meter_id;
        token = strtok_r(NULL, &delimiter, &qos_level_str);
        flag = false;
    }
    return 0;
clear:
    hinic3_clear_qos((struct hinic3_vf_dev*)private_data, dir, &qos_mask);
    return -1;
}

static int hinic3_set_meter_conf(uint32_t meter_id, struct rte_flow *flow,
    enum hinic3_meter_qos_direction dir, uint16_t port_id)
{
    enum hinic3_qos_tablehead index;
    struct hinic3_meter_node *meter = NULL;
    meter = hinic3_meter_find(meter_id);
    if (meter == NULL) {
        HINIC3_LOG(ERR, QOS, "Multi qos: meter id is not find, meter id is %u.", meter_id);
        return -1;
    }

    index = hinic3_get_multi_qos_show_tablehead(dir, meter->profile->profile.packet_mode);
    if (index >= QOS_TABLEHEAD_NUM) {
        HINIC3_LOG(ERR, QOS, "Multi qos: get qos index fail. dir is %d. packet mode is %d.",
            dir, meter->profile->profile.packet_mode);
        return -1;
    }

    flow->flags.is_multi_level_qos = 1;
    flow->flags.multi_level_qos_index = index;
    flow->flags.is_mem_used = 1;
    flow->flags.is_offload = 1;
    flow->port_id = port_id;
    meter->flow = flow;
    return 0;
}

struct rte_flow *hinic3_set_multi_qos(const struct rte_flow_item *pattern, const struct rte_flow_action *actions)
{
    int ret;
    uint16_t port_id = 0;
    uint32_t meter_id = 0;
    struct rte_flow *flow = NULL;
    char qos_level[QOS_LEVEL_MAX_LENGTH] = {0};
    enum hinic3_meter_qos_direction dir = HINIC3_TX_METER_QOS;

    strncpy(qos_level, hinic3_get_multi_qos_level(), QOS_LEVEL_MAX_LENGTH - 1);
    qos_level[QOS_LEVEL_MAX_LENGTH - 1] = '\0';

    ret = hinic3_meter_actions_parse(actions, &meter_id, &dir, &port_id);
    if (ret != 0) {
        return NULL;
    }

    // 当限速为TX方向时，需要去item中解出port_id
    if (dir == HINIC3_TX_METER_QOS) {
        ret = hinic3_meter_pattern_parse(pattern, &port_id);
        if (ret != 0) {
            return NULL;
        }
    }

    hinic3_meter_list_lock();

    ret = hinic3_set_qos_to_hovs(qos_level, port_id, meter_id, dir);
    if (ret != 0) {
        goto end;
    }

    flow = hinic3_calloc(1, sizeof(struct rte_flow), HINIC3_QOS);
    if (flow == NULL) {
        goto end;
    }

    ret = hinic3_set_meter_conf(meter_id, flow, dir, port_id);
    if (ret != 0) {
        hinic3_free(flow);
        flow = NULL;
    }
end:
    hinic3_meter_list_unlock();
    return flow;
}

enum hinic3_meter_qos_direction hinic3_qos_get_dir(enum hinic3_qos_tablehead index)
{
    switch (index) {
        case BW_TX_TYPE:
        case PPS_TX_TYPE:
            return HINIC3_TX_METER_QOS;
        case BW_RX_TYPE:
        case PPS_RX_TYPE:
            return HINIC3_RX_METER_QOS;
        default:
            return HINIC3_NODIR_METER_QOS;
    }
}

static int hinic3_meter_clear_by_flow_sub(enum hinic3_qos_tablehead index, struct hinic3_vf_dev *vf_dev)
{
    int ret;
    struct hinic3_meter_node *meter = NULL;
    struct hinic3_mtr_profile_node *profile_node = NULL;
    struct qos_single_value qos_value = { 0 };
    enum hinic3_meter_qos_direction dir = hinic3_qos_get_dir(index);

    meter = hinic3_meter_find(vf_dev->mtr[index].mtr_id);
    if (meter == NULL) {
        HINIC3_LOG(ERR, QOS, "Multi delete: meter id is not find.");
        return -1;
    }

    if (meter->type != QOS_TYPE_FUNC_LIMIT) {
        HINIC3_LOG(ERR, QOS, "Multi delete: meter qos type error. meter id is %u", meter->meter_id);
        return -1;
    }

    profile_node = meter->profile;
    ret = hinic3_port_qos_limit_set(vf_dev->vport_id, dir, profile_node->profile.packet_mode, &qos_value);
    if (ret != 0) {
        HINIC3_LOG(ERR, QOS, "Multi delete: remove port qos failed.");
        return -1;
    }

    ret = hinic3_vf_qos_statistics_clear(&vf_dev->vport_id);
    if (ret != 0) {
        HINIC3_LOG(ERR, QOS, "Multi qos: group port statistics not clear. vport id is %u", vf_dev->vport_id);
    }

    ret = hinic3_vm_qos_clear(vf_dev, index);
    if (ret != 0) {
        return -1;
    }

    meter->type = QOS_TYPE_MAX;
    meter->dir = HINIC3_NODIR_METER_QOS;
    meter->flow = NULL;
    return 0;
}

static int hinic3_meter_clear_by_flow(struct rte_flow *flow, struct hinic3_vf_dev *vf_dev)
{
    int ret;

    ret = hinic3_meter_clear_by_flow_sub(flow->flags.multi_level_qos_index, vf_dev);
    if (ret != 0) {
        HINIC3_LOG(ERR, QOS, "Multi qos: del meter failed. qos index is %d, vport id is %u",
            flow->flags.multi_level_qos_index, vf_dev->vport_id);
        return -1;
    }
    switch (flow->flags.multi_level_qos_index) {
        case BW_TX_TYPE:
            if (vf_dev->mtr[PPS_TX_TYPE].is_used == true) {
                ret = hinic3_meter_clear_by_flow_sub(PPS_TX_TYPE, vf_dev);
            }
            break;
        case PPS_TX_TYPE:
            if (vf_dev->mtr[BW_TX_TYPE].is_used == true) {
                ret = hinic3_meter_clear_by_flow_sub(BW_TX_TYPE, vf_dev);
            }
            break;
        case BW_RX_TYPE:
            if (vf_dev->mtr[PPS_RX_TYPE].is_used == true) {
                ret = hinic3_meter_clear_by_flow_sub(PPS_RX_TYPE, vf_dev);
            }
            break;
        case PPS_RX_TYPE:
            if (vf_dev->mtr[BW_RX_TYPE].is_used == true) {
                ret = hinic3_meter_clear_by_flow_sub(BW_RX_TYPE, vf_dev);
            }
            break;
        default:
            break;
    }
    if (ret != 0) {
        return -1;
    }
    return 0;
}

int hinic3_qos_flow_delete(struct rte_flow *flow)
{
    int ret;
    struct hinic3_vf_dev *vf_dev = NULL;

    vf_dev = (struct hinic3_vf_dev*)hinic3_get_private_data(flow->port_id);
    if (vf_dev == NULL) {
        HINIC3_LOG(ERR, QOS, "The current device private_data is NULL.");
        return -1;
    }

    hinic3_meter_list_lock();
    ret = hinic3_meter_clear_by_flow(flow, vf_dev);
    hinic3_meter_list_unlock();
    if (ret != 0) {
        return -1;
    }
    hinic3_free(flow);
    return 0;
}

void hinic3_net_qos_clear(void)
{
    /* 清理所有方向，所有类型(pps\bps)的业务平面级限速 */
    /* hinic3_net_qos_limit_set(host_id, dir, type, max_rate, max_burst) */
    /* host_id暂时无用，传入0即可 */
    hinic3_net_qos_limit_set(0, 0, 0, 0, 0);
    hinic3_net_qos_limit_set(0, 1, 0, 0, 0);
    hinic3_net_qos_limit_set(0, 0, 1, 0, 0);
    hinic3_net_qos_limit_set(0, 1, 1, 0, 0);
}

void hinic3_meter_list_init(void)
{
    hinic3_pthread_mutex_init(&g_hinic3_meter_list.mutex);
    hinic3_list_init(&g_hinic3_meter_list.node);
    g_hinic3_meter_list.length = 0;

    g_hinic3_net_meter_info[HINIC3_TX_METER_QOS].is_used = false;
    g_hinic3_net_meter_info[HINIC3_RX_METER_QOS].is_used = false;
}

void hinic3_meter_group_info_init(void)
{
    for (int i = 0; i < HINIC3_METER_NUM_MAX; i++) {
        g_hinic3_group_infos[i].group_id = i;
        hinic3_list_init(&g_hinic3_group_infos[i].node);
        hinic3_pthread_mutex_init(&g_hinic3_group_infos[i].mutex);
    }
}

struct rte_mtr_ops *hinic3_mtr_multi_ops_construct(void)
{
    struct rte_mtr_ops *mtr_ops = NULL;
    mtr_ops = (struct rte_mtr_ops *)hinic3_calloc(1, sizeof(struct rte_mtr_ops), HINIC3_QOS);
    if (mtr_ops == NULL) {
        return NULL;
    }

    mtr_ops->create = hinic3_meter_create;
    mtr_ops->destroy = hinic3_meter_destroy;
    mtr_ops->meter_profile_add = hinic3_multi_meter_profile_add;
    mtr_ops->meter_profile_delete = hinic3_multi_meter_profile_del;
    mtr_ops->meter_profile_update = hinic3_meter_profile_update;
    mtr_ops->meter_policy_add = hinic3_meter_policy_add;
    mtr_ops->meter_policy_delete = hinic3_meter_policy_delete;
    mtr_ops->stats_read = hinic3_meter_stats_read;

    return mtr_ops;
}

void hinic3_multi_meter_init(void)
{
    hinic3_meter_group_info_init();
    hinic3_meter_list_init();
    hinic3_meter_profile_list_init();
    hinic3_mtr_policy_list_init();
}
