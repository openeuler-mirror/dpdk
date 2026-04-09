/*
 * 版权所有 (c) 华为技术有限公司 2024-2024
 * 功能描述: meter 初始化头文件
 *  * 创建日期: 2024-09-29
 *  */

#include <stdint.h>
#include "hinic3_hmap.h"
#include "rte_mtr_driver.h"
#include "hinic3_log.h"
#include "hinic3_init.h"
#include "hinic3_vf_port_qos_public.h"
#include "hinic3_ui_string.h"
#include "hinic3_dfx_multi_qos.h"
#include "hinic3_flow_qos.h"

#define ALREADY_SET 1
#define QOS_FLOW_BW_TYPE 1
#define QOS_FLOW_PPS_TYPE 2

static struct hinic3_mtr_map g_meter_info;
struct hinic3_qos_array g_qos_ids = {0};
static const char *g_flow_qos_unit[METER_UNIT_NUM] = {
    BW_QOS_FLOW_UNIT,
    PPS_QOS_FLOW_UNIT,
};

struct hinic3_mtr_map *
hinic3_get_meter_info(void)
{
    return &g_meter_info;
}

struct hinic3_qos_array *
hinic3_get_qos_ids(void)
{
    return &g_qos_ids;
}

void
hinic3_mtr_map_init(void)
{
    hinic3_hmap_init(&g_meter_info.mtr_map);
}

void
hinic3_qos_array_init(void)
{
    for (int i = 0; i < MAX_FLOW_QOS_ID_NUM; i++) {
        g_qos_ids.qos_ids[i].qos_id = i;
        g_qos_ids.qos_ids[i].flags = false;
    }
    hinic3_pthread_mutex_init(&g_qos_ids.mutex);
}

struct mtr_info_node *
hinic3_mtr_node_lookup(uint32_t ovs_meter_id)
{
    struct mtr_info_node *mtr_node = NULL;
    HINIC3_HMAP_FOR_EACH_WITH_HASH(mtr_node, node, hinic3_hash_uint64((uint64_t)(ovs_meter_id)), &g_meter_info.mtr_map)
    {
        if (ovs_meter_id == mtr_node->mtr_id)
            return mtr_node;
    }
    return NULL;
}

static uint16_t
hinic3_find_unused_qos_id(void)
{
    for (uint16_t i = 0; i < MAX_FLOW_QOS_ID_NUM; i++) {
        if (g_qos_ids.qos_ids[i].flags == false)
            return i;
    }
    return MAX_FLOW_QOS_ID_NUM;
}

static bool
hinic3_check_flow_qos_packet_mode(uint16_t *qos_packet_mode, int packet_mode)
{
    // 流表级QoS packet mode, 0b0001:bps, 0b0010:pps, 0b0011:bps、pps.
    switch (packet_mode) {
    case QOS_BW_TYPE:
        if ((*qos_packet_mode & QOS_FLOW_BW_TYPE) != 0)
            return false;
        *qos_packet_mode |= QOS_FLOW_BW_TYPE;
        return true;
    case QOS_PPS_TYPE:
        if ((*qos_packet_mode & QOS_FLOW_PPS_TYPE) != 0)
            return false;
        *qos_packet_mode |= QOS_FLOW_PPS_TYPE;
        return true;
    default:
        return false;
    }
}

static void
hinic3_multi_flow_qos_set(struct hinic3_meter_node *meter, uint16_t qos_id, 
    uint16_t qos_packet_mode, struct qos_single_value *profile)
{
    meter->type = QOS_TYPE_FLOW_LIMIT;
    meter->flow_num++;
    meter->qos_id = qos_id;
    g_qos_ids.qos_ids[meter->qos_id].packet_mode = qos_packet_mode;
    g_qos_ids.qos_ids[qos_id].meter_id[profile->packet_mode] = meter->meter_id;
    g_qos_ids.qos_ids[qos_id].flags = true;
}

int
hinic3_set_flow_qos_to_hovs_sub(struct hinic3_meter_node *meter, bool is_clear,
                                   uint16_t *qos_id, uint16_t *qos_packet_mode)
{
    int ret;
    struct qos_single_value *profile = NULL;
    // 如果meter已经被非流表级QoS占用，则返回失败
    if (meter->type != QOS_TYPE_MAX && meter->type != QOS_TYPE_FLOW_LIMIT) {
        hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_METER_TYPE, 1);
        return -1;
    }

    profile = &meter->profile->profile;

    if (is_clear == true) {
        meter->flow_num--;
        if (meter->flow_num == 0) {
            meter->type = QOS_TYPE_MAX;
            g_qos_ids.qos_ids[meter->qos_id].flags = false;
            ret = hinic3_flow_qos_limit_set(meter->qos_id, profile->packet_mode, 0, 0, 0, 0);
            if (ret != 0) {
                HINIC3_LOG(ERR, FLOW, "hinic3 flow qos clear failed!");
                return -1;
            }
        }
        return 0;
    }
    // 已经下发过流表级QoS，则不再重复下发
    if (meter->type == QOS_TYPE_FLOW_LIMIT) {
        meter->flow_num++;
        *qos_id = meter->qos_id;
        *qos_packet_mode = g_qos_ids.qos_ids[meter->qos_id].packet_mode;
        return ALREADY_SET;
    }
    if (*qos_id == MAX_FLOW_QOS_ID_NUM) {
        *qos_id = hinic3_find_unused_qos_id();
        if (*qos_id >= MAX_FLOW_QOS_ID_NUM) {
            hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_FLOW_QOS_FULL, 1);
            return -1;
        }
    }
    if (hinic3_check_flow_qos_packet_mode(qos_packet_mode, profile->packet_mode)) {
        ret = hinic3_flow_qos_limit_set(*qos_id, profile->packet_mode, profile->max_rate, profile->max_burst,
                                       profile->min_rate, profile->min_burst);
    } else {
        hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_FLOW_QOS_PACKET_MODE, 1);
        return -1;
    }
    if (ret != 0) {
        hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_FLOW_SET_QOS_HOVS, 1);
        return ret;
    }
    hinic3_multi_flow_qos_set(meter, *qos_id, *qos_packet_mode, profile);
    return 0;
}

static void
hinic3_multi_flow_qos_conf_set(const struct hinic3_meter_node *meter, uint16_t qos_id,
    uint16_t qos_packet_mode, struct hovs_qos_action *qos_action, struct rte_flow *flow)
{
    qos_action->qos_flag = meter->profile->profile.packet_mode;
    qos_action->qos_type = qos_packet_mode;
    qos_action->qos_id = qos_id;
    flow->meter_id = meter->meter_id;
    flow->flags.has_flow_qos = 1;
}

static int
hinic3_set_flow_qos_to_hovs(struct hinic3_nlattr *hinic3_actions, uint32_t meter_id, struct rte_flow *flow)
{
    int ret;
    uint16_t qos_packet_mode = 0;
    uint16_t qos_id = MAX_FLOW_QOS_ID_NUM;
    struct hovs_qos_action qos_action = {0};
    struct hinic3_mtr_policy_node *policy_node = NULL;
    struct hinic3_meter_node *meter = NULL;
    struct hinic3_meter_node *next_meter = NULL;

    hinic3_meter_list_lock();
    meter = hinic3_meter_find(meter_id);
    if (meter == NULL) {
        hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_METER_NOT_FOUND, 1);
        goto err;
    }
    policy_node = meter->policy;

    ret = hinic3_set_flow_qos_to_hovs_sub(meter, false, &qos_id, &qos_packet_mode);
    if (ret != 0 && ret != ALREADY_SET) {
        goto err;
    }

    if (policy_node->has_next_meter == true) {
        next_meter = hinic3_meter_find(policy_node->next_meter_id);
        if (next_meter == NULL) {
            hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_METER_NOT_FOUND, 1);
            goto clear;
        }
    
        if (ret != ALREADY_SET) {
            ret = hinic3_set_flow_qos_to_hovs_sub(next_meter, false, &qos_id, &qos_packet_mode);
        } else {
            next_meter->flow_num++;
            ret = 0;
        }
        if (ret != 0)
            goto clear;
    }
    hinic3_meter_list_unlock();
    hinic3_multi_flow_qos_conf_set(meter, qos_id, qos_packet_mode, &qos_action, flow);
    hinic3_nlattr_put_unspec(hinic3_actions, HINIC3_FLOW_ACT_QOS, &qos_action, sizeof(struct hovs_qos_action));
    return 0;
clear:
    hinic3_set_flow_qos_to_hovs_sub(meter, true, NULL, NULL);
err:
    hinic3_meter_list_unlock();
    return -1;
}

int
hinic3_offload_parse_qos_act_sub(struct hinic3_offload_action *offload_action,
                                      const struct rte_flow_action_meter *mtr, struct rte_flow *mega_flow)
{
    struct hinic3_nlattr *hinic3_actions = &offload_action->act_nla;

    if (mtr == NULL) {
        hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_METER_ACTION, 1);
        return -1;
    }

    return hinic3_set_flow_qos_to_hovs(hinic3_actions, mtr->mtr_id, mega_flow);
}

static void
hinic3_multi_flow_qos_show_all(struct ds *ds)
{
    hinic3_pthread_mutex_lock(&g_qos_ids.mutex);
    for (int i = 0; i < MAX_FLOW_QOS_ID_NUM; i++) {
        hinic3_ds_put_format(ds, "%2s%-15s%u\n", HINIC3_UI_INDENT_SPACE, "qos id:", g_qos_ids.qos_ids[i].qos_id);
        if (g_qos_ids.qos_ids[i].flags == true)
            hinic3_ds_put_format(ds, "%2s%-15s%s\n", HINIC3_UI_INDENT_SPACE, "is used:", "true");
        else
            hinic3_ds_put_format(ds, "%2s%-15s%s\n", HINIC3_UI_INDENT_SPACE, "is used:", "false");

        hinic3_ds_put_format(ds, "\n");
    }
    hinic3_ds_put_format(ds, "%2s%-15s%u\n", HINIC3_UI_INDENT_SPACE, "qos num:", MAX_FLOW_QOS_ID_NUM);
    hinic3_pthread_mutex_unlock(&g_qos_ids.mutex);
}

static void
hinic3_multi_flow_qos_show_part(struct ds *ds, bool is_uesd)
{
    int count = 0;
    hinic3_pthread_mutex_lock(&g_qos_ids.mutex);
    for (int i = 0; i < MAX_FLOW_QOS_ID_NUM; i++) {
        if (g_qos_ids.qos_ids[i].flags == is_uesd) {
            hinic3_ds_put_format(ds, "%2s%-15s%u\n", HINIC3_UI_INDENT_SPACE, "qos id:", g_qos_ids.qos_ids[i].qos_id);
            if (g_qos_ids.qos_ids[i].flags == true)
                hinic3_ds_put_format(ds, "%2s%-15s%s\n", HINIC3_UI_INDENT_SPACE, "is used:", "true");
            else
                hinic3_ds_put_format(ds, "%2s%-15s%s\n", HINIC3_UI_INDENT_SPACE, "is used:", "false");

            hinic3_ds_put_format(ds, "\n");
            count++;
        }
    }
    hinic3_pthread_mutex_unlock(&g_qos_ids.mutex);

    if (is_uesd)
        hinic3_ds_put_format(ds, "%2s%-15s%d\n", HINIC3_UI_INDENT_SPACE, "used num:", count);
    else
        hinic3_ds_put_format(ds, "%2s%-15s%d\n", HINIC3_UI_INDENT_SPACE, "unused num:", count);
}

static bool
hinic3_check_qos_id_used(uint16_t qos_id)
{
    return g_qos_ids.qos_ids[qos_id].flags;
}

static bool
hinic3_check_flow_qos_rate(uint64_t max_rate, int packet_mode)
{
    // 如果max_rate的值为0，或者最大值，则没有下发该packet_mode的流表级QoS
    if (max_rate == 0)
        return false;

    if (packet_mode == QOS_BW_TYPE) {
        if (max_rate == MAX_BW_RATE_QOS)
            return false;
    } else {
        if (max_rate == MAX_PPS_RATE_QOS)
            return false;
    }

    return true;
}

static int
hinic3_multi_flow_qos_dump_one_sub(struct ds *ds, uint16_t qos_id, int packet_mode)
{
    int ret;
    uint64_t max_rate;
    uint64_t max_burst;
    uint64_t min_rate;
    uint64_t min_burst;
    struct hinic3_meter_node *meter = NULL;
    struct qos_single_value *profile = NULL;

    ret = hinic3_flow_qos_limit_get(qos_id, packet_mode, &max_rate, &max_burst, &min_rate, &min_burst);
    if (ret != 0) {
        hinic3_ds_put_format(ds, HINIC3_UI_LEADING_SIGN_ERROR "Failed to get qos info for qos_id %u.\n", qos_id);
        return ret;
    }

    // 如果没有下过该packet_mode的流表级QoS，则不显示
    if (hinic3_check_flow_qos_rate(max_rate, packet_mode) == false)
        return 0;

    hinic3_meter_list_lock();
    meter = hinic3_meter_find(g_qos_ids.qos_ids[qos_id].meter_id[packet_mode]);
    if (meter == NULL) {
        hinic3_ds_put_format(ds, HINIC3_UI_LEADING_SIGN_ERROR "Meter id not found. meter id is %u.",
                            g_qos_ids.qos_ids[qos_id].meter_id[packet_mode]);
        hinic3_meter_list_unlock();
        return -1;
    }
    profile = &meter->profile->profile;

    hinic3_ds_put_format(ds, "%4s%s:\n", HINIC3_UI_INDENT_SPACE, g_flow_qos_unit[packet_mode]);
    hinic3_ds_put_format(ds, "%6smeter id: %u\n", HINIC3_UI_INDENT_SPACE, meter->meter_id);
    hinic3_ds_put_format(ds, "%6sPIR:      %" PRIu64 " (%" PRIu64 ")\n", HINIC3_UI_INDENT_SPACE,
                        profile->max_rate, max_rate);
    hinic3_ds_put_format(ds, "%6sPBS:      %" PRIu64 " (%" PRIu64 ")\n", HINIC3_UI_INDENT_SPACE,
                        profile->max_burst, max_burst);
    hinic3_ds_put_format(ds, "%6sCIR:      %" PRIu64 " (%" PRIu64 ")\n", HINIC3_UI_INDENT_SPACE,
                        profile->min_rate, min_rate);
    hinic3_ds_put_format(ds, "%6sCBS:      %" PRIu64 " (%" PRIu64 ")\n", HINIC3_UI_INDENT_SPACE,
                        profile->min_burst, min_burst);
    hinic3_meter_list_unlock();
    return 0;
}

static int
hinic3_multi_flow_qos_dump_one(struct ds *ds, uint16_t qos_id)
{
    int ret;
    hinic3_pthread_mutex_lock(&g_qos_ids.mutex);
    hinic3_ds_put_format(ds, "%2sqos id: %u\n", HINIC3_UI_INDENT_SPACE, qos_id);
    ret = hinic3_multi_flow_qos_dump_one_sub(ds, qos_id, QOS_BW_TYPE);
    if (ret != 0) {
        hinic3_pthread_mutex_unlock(&g_qos_ids.mutex);
        return ret;
    }
    ret = hinic3_multi_flow_qos_dump_one_sub(ds, qos_id, QOS_PPS_TYPE);
    hinic3_pthread_mutex_unlock(&g_qos_ids.mutex);
    return ret;
}

static int
hinic3_multi_flow_qos_dump_all(struct ds *ds)
{
    int ret = 0;
    int count = 0;
    for (int i = 0; i < MAX_FLOW_QOS_ID_NUM; i++) {
        if (g_qos_ids.qos_ids[i].flags == true) {
            ret = hinic3_multi_flow_qos_dump_one(ds, i);
            if (ret != 0)
                return -1;

            count++;
        }
    }
    hinic3_ds_put_format(ds, "%2sused num: %d\n", HINIC3_UI_INDENT_SPACE, count);
    return 0;
}

static int
hinic3_multi_flow_qos_stats_show_sub(struct ds *ds, uint16_t qos_id)
{
    int ret;
    struct hovs_hqos_stats_batch_all hovs_stats = {0};

    ret = hinic3_hqos_statistics_get_all_batch(QOS_TYPE_FLOW_LIMIT, &qos_id, &hovs_stats, 1);
    if (ret != 0) {
        hinic3_ds_put_format(ds, HINIC3_UI_LEADING_SIGN_ERROR "Failed to get qos stats for qos_id %u.\n", qos_id);
        return ret;
    }

    hinic3_ds_put_format(ds, "%2sGreen Color:\n", HINIC3_UI_INDENT_SPACE);
    hinic3_ds_put_format(ds, "%4spkts-num:%-10u\n", HINIC3_UI_INDENT_SPACE, hovs_stats.tx_pkts[RTE_COLOR_GREEN]);
    hinic3_ds_put_format(ds, "%4sbyte-num:%-10u\n", HINIC3_UI_INDENT_SPACE, hovs_stats.tx_bytes[RTE_COLOR_GREEN]);
    return 0;
}

static int
hinic3_multi_flow_qos_loss_sub(struct ds *ds, uint16_t qos_id)
{
    int ret;
    struct hovs_hqos_stats_batch_all hovs_stats = {0};

    ret = hinic3_hqos_statistics_get_all_batch(QOS_TYPE_FLOW_LIMIT, &qos_id, &hovs_stats, 1);
    if (ret != 0) {
        hinic3_ds_put_format(ds, HINIC3_UI_LEADING_SIGN_ERROR "Failed to get qos stats for qos_id %u.\n", qos_id);
        return ret;
    }

    hinic3_ds_put_format(ds, "%2sflow-drop:\n", HINIC3_UI_INDENT_SPACE);
    hinic3_ds_put_format(ds, "%4sqos-drop-pkts:  %u\n", HINIC3_UI_INDENT_SPACE, hovs_stats.tx_drop_pkts);
    hinic3_ds_put_format(ds, "%4sqos-drop-bytes: %u\n", HINIC3_UI_INDENT_SPACE, hovs_stats.tx_drop_bytes);
    return 0;
}

static void
hinic3_multi_flow_qos_show_help(struct ds *ds)
{
    hinic3_ds_put_format(ds, "%2sUsage: hwoff/show-flow-qos { --all | --used | --unused | { -h | --help } } \n",
                        HINIC3_UI_INDENT_SPACE);
    hinic3_ds_put_format(ds, "%2sOptions list:\n", HINIC3_UI_INDENT_SPACE);
    hinic3_ds_put_format(ds, "%4s--all           Show all qos id.\n", HINIC3_UI_INDENT_SPACE);
    hinic3_ds_put_format(ds, "%4s--used          Show used qos id.\n", HINIC3_UI_INDENT_SPACE);
    hinic3_ds_put_format(ds, "%4s--unused        Show unused qos id.\n", HINIC3_UI_INDENT_SPACE);
    hinic3_ds_put_format(ds, "%4s-h, --help      Display the help information.\n", HINIC3_UI_INDENT_SPACE);
}

static void
hinic3_multi_flow_qos_show_sub(struct ds *ds, bool is_all, bool is_used)
{
    if (is_all == true)
        hinic3_multi_flow_qos_show_all(ds);
    else
        hinic3_multi_flow_qos_show_part(ds, is_used);
}

static void
hinic3_multi_flow_qos_show(struct unixctl_conn *conn, int argc, const char *argv[], void *aux)
{
    struct ds ds = DS_EMPTY_INITIALIZER;

    if (argc != MULTI_QOS_ARG_NUMS) {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR "Invalid number of parameters.\n");
        goto err;
    }

    if (strcmp("--help", argv[MULTI_QOS_ARG_NUMS - 1]) == 0 || strcmp("-h", argv[MULTI_QOS_ARG_NUMS - 1]) == 0) {
        hinic3_multi_flow_qos_show_help(&ds);
    } else if (strcmp("--used", argv[MULTI_QOS_ARG_NUMS - 1]) == 0) {
        hinic3_multi_flow_qos_show_sub(&ds, false, true);
    } else if (strcmp("--unused", argv[MULTI_QOS_ARG_NUMS - 1]) == 0) {
        hinic3_multi_flow_qos_show_sub(&ds, false, false);
    } else if (strcmp("--all", argv[MULTI_QOS_ARG_NUMS - 1]) == 0) {
        hinic3_multi_flow_qos_show_sub(&ds, true, true);
    } else {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR, "Invalid argument.\n\n");
        hinic3_multi_flow_qos_show_help(&ds);
    }
    *(int *)aux = 0;
    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
    return;
err:
    *(int *)aux = -1;
    hinic3_command_reply_error(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
}

static void
hinic3_multi_flow_qos_dump_sub(struct unixctl_conn *conn, int argc, const char *argv[], void *aux)
{
    int ret;
    uint16_t qos_id;
    char *endPtr = NULL;
    struct ds ds = DS_EMPTY_INITIALIZER;

    if (argc == 1) {
        ret = hinic3_multi_flow_qos_dump_all(&ds);
    } else if (argc == MULTI_QOS_ARG_NUMS) {
        qos_id = (uint16_t)strtoul((const char *)argv[1], &endPtr, DEC_BASE_NUM);
        if (qos_id > MAX_QOS_ID_NUM || endPtr == NULL || *endPtr != '\0') {
            hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR HINIC3_UI_INVALID_QOS_ID_STR);
            ret = -1;
        } else if (hinic3_check_qos_id_used(qos_id) == true) {
            ret = hinic3_multi_flow_qos_dump_one(&ds, qos_id);
        } else {
            hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR "Qos id is not used.\n");
            ret = -1;
        }
    } else {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR "Invalid number of parameters.\n");
        ret = -1;
    }

    if (ret != 0) {
        *(int *)aux = -1;
        hinic3_command_reply_error(conn, hinic3_ds_cstr(&ds));
    } else {
        *(int *)aux = 0;
        hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    }
    hinic3_ds_destroy(&ds);
}

static void
hinic3_multi_flow_qos_dump_help(struct unixctl_conn *conn)
{
    struct ds ds = DS_EMPTY_INITIALIZER;
    hinic3_ds_put_format(&ds, "%2s%s%s\n\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_CMD_USAGE_STRING, HINIC3_UI_DUMP_FLOW_QOS_TIPS_STRING);
    hinic3_ds_put_format(&ds, "%2s%s\n", HINIC3_UI_INDENT_SPACE, HINIC3_UI_OPTION_LIST_STRING);
    hinic3_ds_put_format(&ds, "%4s%-30s%-s\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_DUMP_QOS_STAT_HRLP_TITLE_STRING, HINIC3_UI_DUMP_FLOW_QOS_LOSS_HELP_STR);
    hinic3_ds_put_format(&ds, "%4s%-30s%-s\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_ESCAPE_MODE_SHOW_HELP_STRING, HINIC3_UI_ESCAPE_MODE_SHOW_HELP_TIPS_STRING);
    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
}

static void
hinic3_multi_flow_qos_dump(struct unixctl_conn *conn, int argc, const char *argv[], void *aux)
{
    enum { HELP_ARGC = 2 };

    if ((argc == HELP_ARGC) && ((strcmp(argv[1], "-h") == 0) || (strcmp(argv[1], "--help") == 0)))
        hinic3_multi_flow_qos_dump_help(conn);
    else
        hinic3_multi_flow_qos_dump_sub(conn, argc, argv, aux);
}

static void
hinic3_multi_flow_qos_stats_show_main(struct unixctl_conn *conn, int argc, const char *argv[], void *aux)
{
    uint16_t qos_id;
    char *endPtr = NULL;
    struct ds ds = DS_EMPTY_INITIALIZER;

    if (argc != MULTI_QOS_ARG_NUMS) {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR "Invalid number of parameters.\n");
        goto err;
    } else {
        qos_id = (uint16_t)strtoul((const char *)argv[1], &endPtr, DEC_BASE_NUM);
        if (qos_id > MAX_QOS_ID_NUM || endPtr == NULL || *endPtr != '\0') {
            hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR HINIC3_UI_INVALID_QOS_ID_STR);
            goto err;
        } else if (hinic3_check_qos_id_used(qos_id) == true) {
            if (hinic3_multi_flow_qos_stats_show_sub(&ds, qos_id) != 0)
                goto err;
        } else {
            hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR "Qos id is not used.\n");
            goto err;
        }
    }
    *(int *)aux = 0;
    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
    return;
err:
    *(int *)aux = -1;
    hinic3_command_reply_error(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
}

static void hinic3_multi_flow_qos_stats_show_help(struct unixctl_conn *conn)
{
    struct ds ds = DS_EMPTY_INITIALIZER;
    hinic3_ds_put_format(&ds, "%2s%s%s\n\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_CMD_USAGE_STRING, HINIC3_UI_DUMP_FLOW_QOS_STATS_TIPS_STRING);
    hinic3_ds_put_format(&ds, "%2s%s\n", HINIC3_UI_INDENT_SPACE, HINIC3_UI_OPTION_LIST_STRING);
    hinic3_ds_put_format(&ds, "%4s%-30s%-s\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_DUMP_QOS_STAT_HRLP_TITLE_STRING, HINIC3_UI_DUMP_QOS_HELP_STR);
    hinic3_ds_put_format(&ds, "%4s%-30s%-s\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_ESCAPE_MODE_SHOW_HELP_STRING, HINIC3_UI_ESCAPE_MODE_SHOW_HELP_TIPS_STRING);
    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
}

static void
hinic3_multi_flow_qos_stats_show(struct unixctl_conn *conn, int argc, const char *argv[], void *aux)
{
    if ((strcmp(argv[1], "-h") == 0) || (strcmp(argv[1], "--help") == 0))
        hinic3_multi_flow_qos_stats_show_help(conn);
    else
        hinic3_multi_flow_qos_stats_show_main(conn, argc, argv, aux);
}

static void
hinic3_multi_flow_qos_loss_main(struct unixctl_conn *conn, int argc, const char *argv[], void *aux)
{
    uint16_t qos_id;
    char *endPtr = NULL;
    struct ds ds = DS_EMPTY_INITIALIZER;
    if (argc != MULTI_QOS_ARG_NUMS) {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR "Invalid number of parameters.\n");
        goto err;
    } else {
        qos_id = (uint16_t)strtoul((const char *)argv[1], &endPtr, DEC_BASE_NUM);
        if (qos_id > MAX_QOS_ID_NUM || endPtr == NULL || *endPtr != '\0') {
            hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR HINIC3_UI_INVALID_QOS_ID_STR);
            goto err;
        } else if (hinic3_check_qos_id_used(qos_id) == true) {
            if (hinic3_multi_flow_qos_loss_sub(&ds, qos_id) != 0)
                goto err;
        } else {
            hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR "Qos id is not used.\n");
            goto err;
        }
    }
    *(int *)aux = 0;
    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
    return;
err:
    *(int *)aux = -1;
    hinic3_command_reply_error(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
}

static void
hinic3_multi_flow_qos_loss_help(struct unixctl_conn *conn)
{
    struct ds ds = DS_EMPTY_INITIALIZER;
    hinic3_ds_put_format(&ds, "%2s%s%s\n\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_CMD_USAGE_STRING, HINIC3_UI_DUMP_FLOW_QOS_LOST_TIPS_STRING);
    hinic3_ds_put_format(&ds, "%2s%s\n", HINIC3_UI_INDENT_SPACE, HINIC3_UI_OPTION_LIST_STRING);
    hinic3_ds_put_format(&ds, "%4s%-30s%-s\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_DUMP_QOS_STAT_HRLP_TITLE_STRING, HINIC3_UI_DUMP_QOS_HELP_STR);
    hinic3_ds_put_format(&ds, "%4s%-30s%-s\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_ESCAPE_MODE_SHOW_HELP_STRING, HINIC3_UI_ESCAPE_MODE_SHOW_HELP_TIPS_STRING);
    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
}

static void
hinic3_multi_flow_qos_loss(struct unixctl_conn *conn, int argc, const char *argv[], void *aux)
{
    if ((strcmp(argv[1], "-h") == 0) || (strcmp(argv[1], "--help") == 0))
        hinic3_multi_flow_qos_loss_help(conn);
    else
        hinic3_multi_flow_qos_loss_main(conn, argc, argv, aux);
}

void unixctl_meter_dfx_init(void)
{
    hinic3_command_register("hwoff/show-flow-qos", "{ --all | --used | --unused | { -h | --help } }", 1, 1,
                            hinic3_multi_flow_qos_show, NULL);
    hinic3_command_register("hwoff/dump-flow-qos", "[ INTEGER<0-1023> | { -h | --help } ]", 0, 1, hinic3_multi_flow_qos_dump, NULL);
    hinic3_command_register("hwoff/dump-flow-qos-stats", "{ INTEGER<0-1023> | { -h | --help } }", 1, 1,
                            hinic3_multi_flow_qos_stats_show, NULL);
    hinic3_command_register("hwoff/dump-flow-qos-loss", "{ INTEGER<0-1023> | { -h | --help } }", 1, 1,
                            hinic3_multi_flow_qos_loss, NULL);
}