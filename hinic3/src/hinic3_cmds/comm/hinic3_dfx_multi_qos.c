/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2024 Huawei Technologies Co., Ltd
 */

#include "hinic3_ds.h"
#include "hinic3_ui_string.h"
#include "hinic3_vf_controller.h"
#include "hinic3_drv.h"
#include "hiovs_api.h"
#include "hinic3_command.h"
#include "hinic3_mtr.h"
#include "hinic3_vf_port_qos_public.h"
#include "hinic3_mtr_profile.h"
#include "hinic3_mtr_policy.h"
#include "hinic3_offload_flow_port.h"
#include "rte_ethdev.h"
#include "hinic3_flow_qos.h"
#include "hinic3_dfx_multi_qos.h"

#define MULTI_QOS_MASK_TX (1ULL << 0)
#define MULTI_QOS_MASK_RX (1ULL << 1)
#define MULTI_PPS_QOS_SPEACE_NUMS 6
#define MULTI_KBPS_QOS_SPEACE_NUMS 5

static const char *g_profile_unit[PROFILE_UNIT_NUM] = {
    BW_QOS_UNIT,
    PPS_QOS_UNIT,
};

static const char *g_profile_mode[PROFILE_UNIT_NUM] = {
    BW_QOS_MODE,
    PPS_QOS_MODE,
};

static const char *g_vm_qos_unit[QOS_UNIT_NUM] = {
    BW_QOS_UNIT,
    PPS_QOS_UNIT,
    BW_QOS_UNIT,
    PPS_QOS_UNIT,
};

static const char **
hinic3_vm_qos_unit_get(void)
{
    return g_vm_qos_unit;
}

static int
hinic3_get_ifindex_id_by_name(const char **argv, uint16_t *dpdk_index_id)
{
    char netdev_name[HINIC3_NETDEV_NAME_MAX_LENGTH];
    int ret;

    ret = hinic3_get_netdev_name(argv[1], netdev_name);
    if (ret != 0)
        return -1;

    ret = rte_eth_dev_get_port_by_name(netdev_name, dpdk_index_id);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "hwoff/%s failed, get port id error!", "show-port-qos");
        return -1;
    }
    return 0;
}

static void
hinic3_multi_qos_show_loss(struct ds *ds, struct hovs_hqos_stats_batch_all hovs_stats, uint16_t stats_mask)
{
    if ((stats_mask & MULTI_QOS_MASK_TX) == MULTI_QOS_MASK_TX) {
        hinic3_ds_put_format(ds, "%4s%-30s: %llu\n", HINIC3_UI_INDENT_SPACE, "tx-qos-drop-num",
            hovs_stats.tx_drop_pkts);
        hinic3_ds_put_format(ds, "%4s%-30s: %llu\n", HINIC3_UI_INDENT_SPACE, "tx-qos-drop-byte",
            hovs_stats.tx_drop_bytes);
    } else {
        hinic3_ds_put_format(ds, "%4s%-30s: N/A\n", HINIC3_UI_INDENT_SPACE, "tx-qos-drop-num");
        hinic3_ds_put_format(ds, "%4s%-30s: N/A\n", HINIC3_UI_INDENT_SPACE, "tx-qos-drop-byte");
    }
    if ((stats_mask & MULTI_QOS_MASK_RX) == MULTI_QOS_MASK_RX) {
        hinic3_ds_put_format(ds, "%4s%-30s: %llu\n", HINIC3_UI_INDENT_SPACE, "rx-qos-drop-num",
            hovs_stats.rx_drop_pkts);
        hinic3_ds_put_format(ds, "%4s%-30s: %llu\n", HINIC3_UI_INDENT_SPACE, "rx-qos-drop-byte",
            hovs_stats.rx_drop_bytes);
    } else {
        hinic3_ds_put_format(ds, "%4s%-30s: N/A\n", HINIC3_UI_INDENT_SPACE, "rx-qos-drop-num");
        hinic3_ds_put_format(ds, "%4s%-30s: N/A\n", HINIC3_UI_INDENT_SPACE, "rx-qos-drop-byte");
    }
}

static struct hinic3_vf_dev *
hinic3_multi_qos_get_dev(const char **argv, struct ds *ds)
{
    int ret;
    uint16_t dpdk_index_id = 0;
    struct hinic3_vf_dev *vf_dev = NULL;

    ret = hinic3_get_ifindex_id_by_name(argv, &dpdk_index_id);
    if (ret != 0) {
        hinic3_ds_put_format(ds, "%s%s, %s", HINIC3_UI_LEADING_SIGN_ERROR, HINIC3_UI_ERROR_WRONG_PARAMETER,
            HINIC3_UI_INVALID_PORT_NAME_STR);
        return NULL;
    }

    vf_dev = (struct hinic3_vf_dev *)hinic3_get_private_data(dpdk_index_id);
    if (vf_dev == NULL) {
        hinic3_ds_put_format(ds, "%sInternal failure, fail to get current device data.\n",
            HINIC3_UI_LEADING_SIGN_FAILURE);
        return NULL;
    }

    return vf_dev;
}

static int
hinic3_port_dump_loss_get(struct hinic3_vf_dev *vf_dev, struct ds *ds, const char *argv)
{
    uint16_t stats_mask = 0;
    uint16_t vport_id = vf_dev->vport_id;
    struct hovs_hqos_stats_batch_all stats = {0};

    if (hinic3_hqos_statistics_get_all_batch(QOS_TYPE_FUNC_LIMIT, &vport_id, &stats, 1)) {
        HINIC3_LOG(ERR, QOS, "hinic3 dump qos stats failed. port id is %u", vport_id);
        return -1;
    }
    hinic3_ds_put_format(ds, "  vf name[%s] dump loss:\n", argv);
    if (vf_dev->mtr[BW_TX_TYPE].is_used == true || vf_dev->mtr[PPS_TX_TYPE].is_used == true)
        stats_mask |= MULTI_QOS_MASK_TX;
    if (vf_dev->mtr[BW_RX_TYPE].is_used == true || vf_dev->mtr[PPS_RX_TYPE].is_used == true)
        stats_mask |= MULTI_QOS_MASK_RX;
    hinic3_multi_qos_show_loss(ds, stats, stats_mask);
    return 0;
}

static void
hinic3_multi_port_qos_dump_loss_sub(struct unixctl_conn *conn, int argc, const char *argv[], void *aux)
{
    int ret;
    struct hinic3_vf_dev *vf_dev = NULL;
    struct ds ds = DS_EMPTY_INITIALIZER;

    if (argc != MULTI_QOS_ARG_NUMS) {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR "Invalid number of parameters.\n");
        goto err;
    }

    vf_dev = hinic3_multi_qos_get_dev(argv, &ds);
    if (vf_dev == NULL)
        goto err;

    if (vf_dev->qos_type == VF_NONE_QOS_FLAG) {
        hinic3_ds_put_format(&ds, "%sPort has no qos.\n", HINIC3_UI_LEADING_SIGN_FAILURE);
        goto err;
    }

    ret = hinic3_port_dump_loss_get(vf_dev, &ds, argv[1]);
    if (ret != 0) {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR "Dump hardware stats error.\n");
        goto err;
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
hinic3_multi_port_qos_dump_loss_help(struct unixctl_conn *conn)
{
    struct ds ds = DS_EMPTY_INITIALIZER;
    hinic3_ds_put_format(&ds, "%2s%s%s\n\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_CMD_USAGE_STRING, HINIC3_UI_DUMP_PORT_QOS_LOST_TIPS_STRING);
    hinic3_ds_put_format(&ds, "%2s%s\n", HINIC3_UI_INDENT_SPACE, HINIC3_UI_OPTION_LIST_STRING);
    hinic3_ds_put_format(&ds, "%4s%-30s%-s\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_SHOW_PORT_QOS_HELP_NAME_STR, HINIC3_UI_SHOW_PORT_QOS_HELP_STR);
    hinic3_ds_put_format(&ds, "%4s%-30s%-s\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_ESCAPE_MODE_SHOW_HELP_STRING, HINIC3_UI_ESCAPE_MODE_SHOW_HELP_TIPS_STRING);
    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
}

static void
hinic3_multi_port_qos_dump_loss(struct unixctl_conn *conn, int argc HINIC3_UNUSED, const char *argv[],
    void *aux)
{
    if ((strcmp(argv[1], "-h") == 0) || (strcmp(argv[1], "--help") == 0)) {
        hinic3_multi_port_qos_dump_loss_help(conn);
    } else {
        hinic3_multi_port_qos_dump_loss_sub(conn, argc, argv, aux);
    }
    return;
}

static int
hinic3_get_qos_one_port_info(uint16_t vport_id, struct vm_ports_info *ports_info)
{
    int ret;
    struct hinic3_drv_ops *ops = NULL;
    struct hovs_port_stats stats = {0};

    ops = hinic3_get_drv_ops();
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_port_statistics_get, HINIC3_DRV_FUNC_NO_PTR);

    ret = ops->hovs_port_statistics_get(vport_id, &stats);
    if (ret != 0)
        return -1;

    ports_info->rx_pkts = stats.rx_pkts;
    ports_info->tx_pkts = stats.tx_pkts;
    ports_info->rx_bytes = stats.rx_bytes;
    ports_info->tx_bytes = stats.tx_bytes;

    ports_info->time_moment = hinic3_time_msec();
    if (ports_info->time_moment < 0)
        return -1;
    return 0;
}

static void
hinic3_qos_speed_print(struct unixctl_conn *conn, const struct vm_ports_info *start,
    const struct vm_ports_info *end)
{
    long long int time_exec = end->time_moment - start->time_moment;
    if (time_exec <= 0) {
        hinic3_command_reply_error(conn, HINIC3_UI_LEADING_SIGN_ERROR HINIC3_UI_TIME_EXEC_ERR_STR);
        return;
    }

    struct ds ds = DS_EMPTY_INITIALIZER;
    float rx_bps = end->rx_bytes > start->rx_bytes ?
        (((float)(end->rx_bytes - start->rx_bytes)) * BYTE_TO_BIT / time_exec) : 0;
    uint64_t rx_pps = end->rx_pkts > start->rx_pkts ?
        ((end->rx_pkts - start->rx_pkts) * SEC_TO_MSEC_BASE)/ time_exec : 0;
    float tx_bps = end->tx_bytes > start->tx_bytes ?
        (((float)(end->tx_bytes - start->tx_bytes)) * BYTE_TO_BIT / time_exec) : 0;
    uint64_t tx_pps = end->tx_pkts > start->tx_pkts ?
        ((end->tx_pkts - start->tx_pkts) * SEC_TO_MSEC_BASE) / time_exec : 0;

    hinic3_ds_put_format(&ds, "  rx-bytes(kbit/s):    %.2f \n", rx_bps);
    hinic3_ds_put_format(&ds, "  rx-pkts(p/s):        %lu \n\n", rx_pps);
    hinic3_ds_put_format(&ds, "  tx-bytes(kbit/s):    %.2f\n", tx_bps);
    hinic3_ds_put_format(&ds, "  tx-pkts(p/s):        %lu\n", tx_pps);
    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);

    return;
}

static void
hinic3_multi_port_qos_speed_show_sub(struct unixctl_conn *conn, int argc, const char *argv[], void *aux)
{
    int ret;
    struct ds ds = DS_EMPTY_INITIALIZER;
    struct hinic3_vf_dev *vf_dev = NULL;
    struct vm_ports_info first_sample_value = {0};
    struct vm_ports_info second_sample_value = {0};

    if (argc != MULTI_QOS_ARG_NUMS) {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR "Invalid number of parameters.\n");
        goto err;
    }

    vf_dev = hinic3_multi_qos_get_dev(argv, &ds);
    if (vf_dev == NULL)
        goto err;

    if (vf_dev->group_qos_id == 0 && vf_dev->qos_type == VF_NONE_QOS_FLAG) {
        hinic3_ds_put_format(&ds, "%sPort has no qos.\n", HINIC3_UI_LEADING_SIGN_FAILURE);
        goto err;
    }

    ret = hinic3_get_qos_one_port_info(vf_dev->vport_id, &first_sample_value);
    if (ret != 0) {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR HINIC3_UI_FIRST_GET_PORT_INFO_ERR_STR);
        goto err;
    }

    usleep(SAMPLING_INTERVAL);
    ret = hinic3_get_qos_one_port_info(vf_dev->vport_id, &second_sample_value);
    if (ret != 0) {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR HINIC3_UI_SECOND_GET_PORT_INFO_ERR_STR);
        goto err;
    }

    hinic3_qos_speed_print(conn, &first_sample_value, &second_sample_value);
    *(int *)aux = 0;
    hinic3_ds_destroy(&ds);
    return;
err:
    hinic3_command_reply_error(conn, hinic3_ds_cstr(&ds));
    *(int *)aux = -1;
    hinic3_ds_destroy(&ds);
}

static void
hinic3_multi_port_qos_speed_show_help(struct unixctl_conn *conn)
{
    struct ds ds = DS_EMPTY_INITIALIZER;
    hinic3_ds_put_format(&ds, "%2s%s%s\n\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_CMD_USAGE_STRING, HINIC3_UI_SHOW_PORT_QOS_SPEED_TIPS_STRING);
    hinic3_ds_put_format(&ds, "%2s%s\n", HINIC3_UI_INDENT_SPACE, HINIC3_UI_OPTION_LIST_STRING);
    hinic3_ds_put_format(&ds, "%4s%-30s%-s\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_SHOW_PORT_QOS_HELP_NAME_STR, HINIC3_UI_SHOW_PORT_QOS_HELP_STR);
    hinic3_ds_put_format(&ds, "%4s%-30s%-s\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_ESCAPE_MODE_SHOW_HELP_STRING, HINIC3_UI_ESCAPE_MODE_SHOW_HELP_TIPS_STRING);
    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
}

static void
hinic3_multi_port_qos_speed_show(struct unixctl_conn *conn, int argc HINIC3_UNUSED, const char *argv[], void *aux)
{
    if ((strcmp(argv[1], "-h") == 0) || (strcmp(argv[1], "--help") == 0)) {
        hinic3_multi_port_qos_speed_show_help(conn);
    } else {
        hinic3_multi_port_qos_speed_show_sub(conn, argc, argv, aux);
    }
    return;
}

static int
hinic3_get_multi_qos_ports_info(struct hinic3_list *port_list, struct vm_ports_info *ports_info)
{
    struct hinic3_group_port_info *iter = NULL;
    struct hinic3_drv_ops *ops = NULL;
    int ret;
    struct hovs_port_stats stats = {0};

    ops = hinic3_get_drv_ops();
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_port_statistics_get, HINIC3_DRV_FUNC_NO_PTR);

    LIST_FOR_EACH(iter, node, port_list) {
        ret = ops->hovs_port_statistics_get(iter->vport_id, &stats);
        if (ret != 0)
            return -1;
        ports_info->rx_pkts += stats.rx_pkts;
        ports_info->tx_pkts += stats.tx_pkts;
        ports_info->rx_bytes += stats.rx_bytes;
        ports_info->tx_bytes += stats.tx_bytes;
        memset(&stats, 0, sizeof(struct hovs_port_stats));
    }
    ports_info->time_moment = hinic3_time_msec();
    if (ports_info->time_moment < 0)
        return -1;
    return 0;
}

static void
hinic3_multi_qos_speed_show_sub(struct unixctl_conn *conn, int argc, const char *argv[], void *aux)
{
    int ret;
    uint16_t group_id;
    char *endPtr = NULL;
    struct vm_ports_info first_sample_value = {0};
    struct vm_ports_info second_sample_value = {0};
    struct hinic3_group_info *group_info = NULL;

    if (argc != MULTI_QOS_ARG_NUMS) {
        hinic3_command_reply_error(conn, HINIC3_UI_LEADING_SIGN_ERROR "Invalid number of parameters.\n");
        *(int *)aux = -1;
        return;
    }

    unsigned long tmp_group_id = strtoul((const char *)argv[1], &endPtr, DEC_BASE_NUM);
    if (tmp_group_id > MAX_QOS_ID_NUM || tmp_group_id == 0 || endPtr == NULL || *endPtr != '\0') {
        hinic3_command_reply_error(conn, HINIC3_UI_LEADING_SIGN_ERROR HINIC3_UI_INVALID_GROUP_ID_STR);
        *(int *)aux = -1;
        return;
    }
    group_id = (uint16_t)tmp_group_id;
    hinic3_group_info_lock(group_id);
    group_info = hinic3_group_info_get(group_id);
    if (group_info->length == 0) {
        hinic3_command_reply_error(conn, HINIC3_UI_LEADING_SIGN_ERROR "The number of ports bound to the group is 0.\n");
        goto err;
    }

    ret = hinic3_get_multi_qos_ports_info(&group_info->node, &first_sample_value);
    if (ret != 0) {
        hinic3_command_reply_error(conn, HINIC3_UI_LEADING_SIGN_ERROR HINIC3_UI_FIRST_GET_PORT_INFO_ERR_STR);
        goto err;
    }

    usleep(SAMPLING_INTERVAL);
    ret = hinic3_get_multi_qos_ports_info(&group_info->node, &second_sample_value);
    if (ret != 0) {
        hinic3_command_reply_error(conn, HINIC3_UI_LEADING_SIGN_ERROR HINIC3_UI_SECOND_GET_PORT_INFO_ERR_STR);
        goto err;
    }

    hinic3_qos_speed_print(conn, &first_sample_value, &second_sample_value);
    *(int *)aux = 0;
    hinic3_group_info_unlock(group_id);
    return;
err:
    *(int *)aux = -1;
    hinic3_group_info_unlock(group_id);
}

static void hinic3_multi_qos_speed_show_help(struct unixctl_conn *conn)
{
    struct ds ds = DS_EMPTY_INITIALIZER;
    hinic3_ds_put_format(&ds, "%2s%s%s\n\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_CMD_USAGE_STRING, HINIC3_UI_SHOW_QOS_SPEED_TIPS_STRING);
    hinic3_ds_put_format(&ds, "%2s%s\n", HINIC3_UI_INDENT_SPACE, HINIC3_UI_OPTION_LIST_STRING);
    hinic3_ds_put_format(&ds, "%4s%-30s%-s\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_DUMP_QOS_LOSS_HRLP_TITLE_STRING, HINIC3_UI_SHOW_QOS_SPEED_HELP_STR);
    hinic3_ds_put_format(&ds, "%4s%-30s%-s\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_ESCAPE_MODE_SHOW_HELP_STRING, HINIC3_UI_ESCAPE_MODE_SHOW_HELP_TIPS_STRING);
    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
}

static void
hinic3_multi_qos_speed_show(struct unixctl_conn *conn, int argc HINIC3_UNUSED, const char *argv[], void *aux)
{
    if ((strcmp(argv[1], "-h") == 0) || (strcmp(argv[1], "--help") == 0))
        hinic3_multi_qos_speed_show_help(conn);
    else
        hinic3_multi_qos_speed_show_sub(conn, argc, argv, aux);
}

static int
hinic3_show_multi_port_qos(struct ds *ds, enum hinic3_qos_tablehead index,
    uint32_t meter_id, uint16_t vport_id)
{
    int ret;
    struct hinic3_meter_node *meter = NULL;
    struct qos_single_value *profile = NULL;
    struct qos_single_value qos_value = {0};
    group_qos_type_key *qos_key = hinic3_get_qos_type_key();
    const char **vm_qos_unit = hinic3_vm_qos_unit_get();
    enum hinic3_meter_qos_direction dir = hinic3_qos_get_dir(index);

    hinic3_meter_list_lock();
    meter = hinic3_meter_find(meter_id);
    if (meter == NULL || meter->profile == NULL) {
        hinic3_meter_list_unlock();
        HINIC3_LOG(ERR, QOS, "Multi qos show: meter id is not find.");
        hinic3_ds_put_format(ds, HINIC3_UI_LEADING_SIGN_ERROR "Meter id is not find. meater id is %u\n", meter_id);
        return -1;
    }
    profile = &meter->profile->profile;

    ret = hinic3_port_qos_limit_get(vport_id, dir, profile->packet_mode, &qos_value);
    if (ret != 0) {
        hinic3_meter_list_unlock();
        HINIC3_LOG(ERR, QOS, "Multi qos show: get qos value failed.");
        hinic3_ds_put_format(ds, HINIC3_UI_LEADING_SIGN_ERROR "Get qos value failed.\n");
        return -1;
    }
    hinic3_ds_put_format(ds, "%4smeter id: %u\n", HINIC3_UI_INDENT_SPACE, meter_id);
    hinic3_ds_put_format(ds, "%4s%s(%s):\n", HINIC3_UI_INDENT_SPACE, qos_key->qos_type_key[index], vm_qos_unit[index]);
    hinic3_ds_put_format(ds, "%6sPIR: %" PRIu64 " (%" PRIu64 ")\n", HINIC3_UI_INDENT_SPACE, profile->max_rate,
        qos_value.max_rate);
    hinic3_ds_put_format(ds, "%6sPBS: %" PRIu64 " (%" PRIu64 ")\n", HINIC3_UI_INDENT_SPACE, profile->max_burst,
        qos_value.max_burst);
    hinic3_ds_put_format(ds, "%6sCIR: %" PRIu64 " (%" PRIu64 ")\n", HINIC3_UI_INDENT_SPACE, profile->min_rate,
        qos_value.min_rate);
    hinic3_ds_put_format(ds, "%6sCBS: %" PRIu64 " (%" PRIu64 ")\n", HINIC3_UI_INDENT_SPACE, profile->min_burst,
        qos_value.min_burst);
    hinic3_meter_list_unlock();    
    return 0;
}

static void
hinic3_multi_port_qos_dump_show_sub(struct unixctl_conn *conn, int argc, const char *argv[], void *aux)
{
    int ret = 0;
    struct hinic3_vf_dev *vf_dev = NULL;
    struct ds ds = DS_EMPTY_INITIALIZER;

    if (argc != MULTI_QOS_ARG_NUMS) {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR "Invalid number of parameters.\n");
        goto err;
    }

    vf_dev = hinic3_multi_qos_get_dev(argv, &ds);
    if (vf_dev == NULL)
        goto err;

    if (vf_dev->qos_type == VF_NONE_QOS_FLAG) {
        hinic3_ds_put_format(&ds, "%sPort has no qos.\n", HINIC3_UI_LEADING_SIGN_FAILURE);
        goto err;
    }

    hinic3_ds_put_format(&ds, "  vf name[%s] qos info:\n", argv[1]);
    for (int i = 0; i < QOS_TABLEHEAD_NUM; i++) {
        if (vf_dev->mtr[i].is_used == true)
            ret |= hinic3_show_multi_port_qos(&ds, i, vf_dev->mtr[i].mtr_id, vf_dev->vport_id);
    }

    if (ret != 0)
        goto err;
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
hinic3_multi_port_qos_dump_show_help(struct unixctl_conn *conn)
{
    struct ds ds = DS_EMPTY_INITIALIZER;
    hinic3_ds_put_format(&ds, "%2s%s%s\n\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_CMD_USAGE_STRING, HINIC3_UI_SHOW_PORT_QOS_TIPS_STRING);
    hinic3_ds_put_format(&ds, "%2s%s\n", HINIC3_UI_INDENT_SPACE, HINIC3_UI_OPTION_LIST_STRING);
    hinic3_ds_put_format(&ds, "%4s%-30s%-s\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_SHOW_PORT_QOS_HELP_NAME_STR, HINIC3_UI_SHOW_PORT_QOS_HELP_STR);
    hinic3_ds_put_format(&ds, "%4s%-30s%-s\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_ESCAPE_MODE_SHOW_HELP_STRING, HINIC3_UI_ESCAPE_MODE_SHOW_HELP_TIPS_STRING);
    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
}

static void
hinic3_multi_port_qos_dump_show(struct unixctl_conn *conn, int argc HINIC3_UNUSED, const char *argv[], void *aux)
{
    if ((strcmp(argv[1], "-h") == 0) || (strcmp(argv[1], "--help") == 0))
        hinic3_multi_port_qos_dump_show_help(conn);
    else
        hinic3_multi_port_qos_dump_show_sub(conn, argc, argv, aux);
}

static int
hinic3_show_multi_qos(struct ds *ds, enum hinic3_qos_tablehead index, uint16_t group_id,
    struct qos_single_value *profile)
{
    int ret;
    struct qos_single_value qos_value = {0};
    group_qos_type_key *qos_key = hinic3_get_qos_type_key();
    const char **vm_qos_unit = hinic3_vm_qos_unit_get();
    enum hinic3_meter_qos_direction dir = hinic3_qos_get_dir(index);

    ret = hinic3_vm_qos_limit_get(group_id, dir, profile->packet_mode, &qos_value);
    if (ret != 0) {
        hinic3_ds_put_format(ds, HINIC3_UI_LEADING_SIGN_ERROR "Failed to get qos info for group_id %u.\n", group_id);
        return ret;
    }

    if (profile->is_RFC2697) {
        hinic3_ds_put_format(ds, "%4s%s(%s):\n", HINIC3_UI_INDENT_SPACE, qos_key->qos_type_key[index],
            vm_qos_unit[index]);
        hinic3_ds_put_format(ds, "%6sEBS: %" PRIu64 " (%" PRIu64 ")\n", HINIC3_UI_INDENT_SPACE,
            profile->max_burst, qos_value.max_burst);
        hinic3_ds_put_format(ds, "%6sCIR: %" PRIu64 " (%" PRIu64 ")\n", HINIC3_UI_INDENT_SPACE,
            profile->min_rate, qos_value.min_rate);
        hinic3_ds_put_format(ds, "%6sCBS: %" PRIu64 " (%" PRIu64 ")\n", HINIC3_UI_INDENT_SPACE,
            profile->min_burst, qos_value.min_burst);
    } else {
        hinic3_ds_put_format(ds, "%4s%s(%s):\n", HINIC3_UI_INDENT_SPACE, qos_key->qos_type_key[index],
            vm_qos_unit[index]);
        hinic3_ds_put_format(ds, "%6sPIR: %" PRIu64 " (%" PRIu64 ")\n", HINIC3_UI_INDENT_SPACE,
            profile->max_rate, qos_value.max_rate);
        hinic3_ds_put_format(ds, "%6sPBS: %" PRIu64 " (%" PRIu64 ")\n", HINIC3_UI_INDENT_SPACE,
            profile->max_burst, qos_value.max_burst);
        hinic3_ds_put_format(ds, "%6sCIR: %" PRIu64 " (%" PRIu64 ")\n", HINIC3_UI_INDENT_SPACE,
            profile->min_rate, qos_value.min_rate);
        hinic3_ds_put_format(ds, "%6sCBS: %" PRIu64 " (%" PRIu64 ")\n", HINIC3_UI_INDENT_SPACE,
            profile->min_burst, qos_value.min_burst);
    }
    return 0;
}

static int
hinic3_multi_qos_dump_show_vport(struct ds *ds, struct hinic3_group_info *group_info)
{
    int i = 0;
    uint16_t port_id;
    char pci_id[PATH_MAX] = {0};
    struct hinic3_group_port_info *iter = NULL;
    struct hinic3_vf_dev *vf_dev = NULL;

    hinic3_ds_put_format(ds, "%4spci-id:\n", HINIC3_UI_INDENT_SPACE);
    LIST_FOR_EACH(iter, node, &group_info->node) {
        if (hinic3_get_port_id_by_ifindex(iter->vport_id, &port_id) != 0) {
            hinic3_ds_put_format(ds, HINIC3_UI_LEADING_SIGN_ERROR "Get port_id failed, vport id = %u.\n",
                iter->vport_id);
            return -1;
        }
        vf_dev = (struct hinic3_vf_dev *)hinic3_get_private_data(port_id);
        if (vf_dev == NULL) {
            hinic3_ds_put_format(ds, HINIC3_UI_LEADING_SIGN_ERROR "Get vf_dev failed, vport id = %u, port id = %u\n",
                iter->vport_id, port_id);
                return -1;
        }
        int ret = snprintf(pci_id, sizeof(pci_id), "pci-id[%d]:", i++);
        if (ret <= 0)
            return -1;
        if (hinic3_get_port_conf_type() == PORT_CONF_FUNC) {
            int function_id = hinic3_get_function_id_by_pci(&vf_dev->pci_addr);
            hinic3_ds_put_format(ds, "%6s%s function id %d\n", HINIC3_UI_INDENT_SPACE, pci_id, function_id);
        } else {
            hinic3_ds_put_format(ds, "%6s%s %04x:%02x:%02x.%x\n", HINIC3_UI_INDENT_SPACE, pci_id,
                vf_dev->pci_addr.domain, vf_dev->pci_addr.bus, vf_dev->pci_addr.devid, vf_dev->pci_addr.function);
        }
        memset(pci_id, 0, sizeof(pci_id));
    }
    return 0;
}

static int
hinic3_multi_qos_dump_show_one(struct ds *ds, struct hinic3_group_info *group_info)
{
    int ret;
    struct hinic3_meter_node *meter = NULL;
    struct qos_single_value *profile = NULL;

    hinic3_ds_put_format(ds, "  group id[%u] qos info:\n", group_info->group_id);
    for (int i = 0; i < QOS_TABLEHEAD_NUM; i++) {
        if (group_info->meter[i].is_used == true) {
            hinic3_meter_list_lock();
            meter = hinic3_meter_find(group_info->meter[i].meter_id);
            if (meter == NULL) {
                hinic3_meter_list_unlock();
                HINIC3_LOG(ERR, QOS, "Multi qos: Meter id is not find, meter id is %u.",
                    group_info->meter[i].meter_id);
                hinic3_ds_put_format(ds, HINIC3_UI_LEADING_SIGN_ERROR "Meter id is not find. meter id is %u\n",
                    group_info->meter[i].meter_id);
                return -1;
            }
            profile = &meter->profile->profile;
            hinic3_meter_list_unlock();
            ret = hinic3_show_multi_qos(ds, i, group_info->group_id, profile);
            if (ret != 0)
                return -1;
            hinic3_ds_put_format(ds, "%4s%s %u\n", HINIC3_UI_INDENT_SPACE, "meter id:",
                group_info->meter[i].meter_id);
            hinic3_ds_put_format(ds, "\n");
        }
    }

    hinic3_ds_put_format(ds, "%4s%s %u\n", HINIC3_UI_INDENT_SPACE, "port nums:", group_info->length);
    ret = hinic3_multi_qos_dump_show_vport(ds, group_info);
    return ret;
}

static int
hinic3_multi_qos_dump_show_all(struct ds *ds)
{
    int ret;
    int group_num = 0;
    struct hinic3_group_info *group_info = NULL;

    for (int group_id = 0; group_id < MAX_QOS_ID_NUM + 1; group_id++) {
        hinic3_group_info_lock(group_id);
        group_info = hinic3_group_info_get(group_id);
        if (group_info->length != 0) {
            ret = hinic3_multi_qos_dump_show_one(ds, group_info);
            if (ret != 0) {
                hinic3_group_info_unlock(group_id);
                return -1;
            }
            group_num++;
            hinic3_ds_put_format(ds, "\n");
        }
        hinic3_group_info_unlock(group_id);
    }
    hinic3_ds_put_format(ds, "  group nums: %d\n", group_num);
    return 0;
}

static void
hinic3_multi_qos_dump_show_sub(struct unixctl_conn *conn, int argc, const char *argv[], void *aux)
{
    int ret;
    uint16_t group_id;
    char *endPtr = NULL;
    struct ds ds = DS_EMPTY_INITIALIZER;
    struct hinic3_group_info *group_info = NULL;

    if (argc == 1) {
        ret = hinic3_multi_qos_dump_show_all(&ds);
    } else if (argc == MULTI_QOS_ARG_NUMS) {
        unsigned long tmp_group_id = strtoul((const char *)argv[1], &endPtr, DEC_BASE_NUM);
        if (tmp_group_id > MAX_QOS_ID_NUM || tmp_group_id == 0 || endPtr == NULL || *endPtr != '\0') {
            hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR HINIC3_UI_INVALID_GROUP_ID_STR);
            ret = -1;
        } else {
            group_id = (uint16_t)tmp_group_id;
            hinic3_group_info_lock(group_id);
            group_info = hinic3_group_info_get(group_id);
            if (group_info->length == 0) {
                hinic3_group_info_unlock(group_id);
                hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR "The number of ports bound to the group is 0.\n");
                ret = -1;
            } else {
                ret = hinic3_multi_qos_dump_show_one(&ds, group_info);
            }
            hinic3_group_info_unlock(group_id);
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
hinic3_multi_qos_dump_show_help(struct unixctl_conn *conn)
{
    struct ds ds = DS_EMPTY_INITIALIZER;
    hinic3_ds_put_format(&ds, "%2s%s%s\n\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_CMD_USAGE_STRING, HINIC3_UI_SHOW_PORT_QOS_TIPS_STRING);
    hinic3_ds_put_format(&ds, "%2s%s\n", HINIC3_UI_INDENT_SPACE, HINIC3_UI_OPTION_LIST_STRING);
    hinic3_ds_put_format(&ds, "%4s%-30s%-s\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_SHOW_PORT_QOS_HELP_NAME_STR, HINIC3_UI_SHOW_PORT_QOS_HELP_STR);
    hinic3_ds_put_format(&ds, "%4s%-30s%-s\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_ESCAPE_MODE_SHOW_HELP_STRING, HINIC3_UI_ESCAPE_MODE_SHOW_HELP_TIPS_STRING);
    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
}

static void
hinic3_multi_qos_dump_show(struct unixctl_conn *conn, int argc, const char *argv[], void *aux)
{
    enum { HELP_ARGC = 2 };

    if ((argc == HELP_ARGC) && ((strcmp(argv[1], "-h") == 0) || (strcmp(argv[1], "--help") == 0)))
        hinic3_multi_qos_dump_show_help(conn);
    else
        hinic3_multi_qos_dump_show_sub(conn, argc, argv, aux);
}

static void
hinic3_multi_meter_show_one_sub(struct ds *ds, struct hinic3_meter_node *meter)
{
    switch (meter->dir) {
        case HINIC3_TX_METER_QOS:
            hinic3_ds_put_format(ds, "%4s%-20s %s\n", "", "direction:", "TX");
            break;
        case HINIC3_RX_METER_QOS:
            hinic3_ds_put_format(ds, "%4s%-20s %s\n", "", "direction:", "RX");
            break;
        case HINIC3_NODIR_METER_QOS:
            hinic3_ds_put_format(ds, "%4s%-20s %s\n", "", "direction:", "NONE");
            break;
        default:
            break;
    }

    if (meter->policy->has_next_meter == true)
        hinic3_ds_put_format(ds, "    %-20s %u\n", "next meter id:", meter->policy->next_meter_id);
}

static void
hinic3_multi_meter_show_one(struct ds *ds, struct hinic3_meter_node *meter)
{
    hinic3_ds_put_format(ds, "  meter id: %u\n", meter->meter_id);
    switch (meter->type) {
        case QOS_TYPE_MAX:
            hinic3_ds_put_format(ds, "    %-20s %s\n", "type:", "NONE");
            break;
        case QOS_TYPE_VM_LIMIT:
            hinic3_ds_put_format(ds, "    %-20s %s\n", "type:", "GROUP");
            hinic3_ds_put_format(ds, "    %-20s %u\n", "group id:", meter->group_id);
            break;
        case QOS_TYPE_FUNC_LIMIT:
            hinic3_ds_put_format(ds, "    %-20s %s\n", "type:", "PORT");
            struct hinic3_vf_dev *vf_dev = (struct hinic3_vf_dev *)hinic3_get_private_data(meter->port_id);
            if (vf_dev == NULL) {
                hinic3_ds_put_format(ds, HINIC3_UI_LEADING_SIGN_ERROR "Get vf_dev failed, port id = %u\n",
                    meter->port_id);
                return;
            }
            if (hinic3_get_port_conf_type() == PORT_CONF_FUNC) {
                int function_id = hinic3_get_function_id_by_pci(&vf_dev->pci_addr);
                hinic3_ds_put_format(ds, "    %-20s %d\n", "func id:", function_id);
            } else {
                hinic3_ds_put_format(ds, "    %-20s %04x:%02x:%02x.%x\n", "pci addr:", vf_dev->pci_addr.domain,
                    vf_dev->pci_addr.bus, vf_dev->pci_addr.devid, vf_dev->pci_addr.function);
            }
            break;
        case QOS_TYPE_NET_LIMIT:
            hinic3_ds_put_format(ds, "    %-20s %s\n", "type:", "NET");
            break;
        case QOS_TYPE_FLOW_LIMIT:
            hinic3_ds_put_format(ds, "    %-20s %s\n", "type:", "FLOW");
            hinic3_ds_put_format(ds, "    %-20s %u\n", "flow num:", meter->flow_num);
            hinic3_ds_put_format(ds, "    %-20s %u\n", "qos id:", meter->qos_id);
        default:
            break;
    }
    hinic3_ds_put_format(ds, "    %-20s %u\n", "policy-id:", meter->policy->policy_id);
    hinic3_ds_put_format(ds, "    %-20s %u\n", "profile-id:", meter->profile->profile_id);
    hinic3_multi_meter_show_one_sub(ds, meter);
}

static void
hinic3_multi_meter_show_all(struct ds *ds)
{
    struct hinic3_meter_node *iter = NULL;
    struct hinic3_meter_list *meter_list = hinic3_meter_list_get();
    if (meter_list->length == 0) {
        hinic3_ds_put_format(ds, HINIC3_UI_LEADING_SIGN_INFO "No meter.\n");
        return;
    }
    hinic3_meter_list_lock();
    LIST_FOR_EACH(iter, node, &meter_list->node) {
        hinic3_multi_meter_show_one(ds, iter);
        hinic3_ds_put_format(ds, "\n");
    }
    hinic3_ds_put_format(ds, "  meter-num: %u\n", meter_list->length);
    hinic3_meter_list_unlock();
}

static void
hinic3_multi_meter_show_sub(struct unixctl_conn *conn, int argc, const char *argv[], void *aux)
{
    int ret = 0;
    uint32_t meter_id;
    char *endPtr = NULL;
    struct ds ds = DS_EMPTY_INITIALIZER;
    struct hinic3_meter_node *meter = NULL;

    if (argc == 1) {
        hinic3_multi_meter_show_all(&ds);
    } else if (argc == MULTI_QOS_ARG_NUMS) {
        unsigned long tmp_meter_id = strtoul(argv[1], &endPtr, DEC_BASE_NUM);
        if (tmp_meter_id > UINT32_MAX || endPtr == NULL || *endPtr != '\0') {
            hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR HINIC3_UI_INVALID_METER_ID_STR);
            ret = -1;
        } else {
            meter_id = (uint32_t)tmp_meter_id;
            hinic3_meter_list_lock();
            meter = hinic3_meter_find(meter_id);
            if (meter == NULL) {
                hinic3_meter_list_unlock();
                hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR HINIC3_UI_INVALID_METER_ID_STR);
                ret = -1;
            } else {
                hinic3_multi_meter_show_one(&ds, meter);
                hinic3_meter_list_unlock();
            }
        }
    } else {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR "Invalid number of parameters.\n");
        ret = -1;
    }
    *(int *)aux = ret;
    if (ret != 0) {
        hinic3_command_reply_error(conn, hinic3_ds_cstr(&ds));
    } else {
        hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    }
    hinic3_ds_destroy(&ds);
}

static void
hinic3_multi_meter_show_help(struct unixctl_conn *conn)
{
    struct ds ds = DS_EMPTY_INITIALIZER;
    hinic3_ds_put_format(&ds, "%2s%s%s\n\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_CMD_USAGE_STRING, HINIC3_UI_SHOW_METER_TIPS_STRING);
    hinic3_ds_put_format(&ds, "%2s%s\n", HINIC3_UI_INDENT_SPACE, HINIC3_UI_OPTION_LIST_STRING);
    hinic3_ds_put_format(&ds, "%4s%-30s%-s\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_SHOW_METER_STR, HINIC3_UI_SHOW_METER_HELP_STR);
    hinic3_ds_put_format(&ds, "%4s%-30s%-s\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_ESCAPE_MODE_SHOW_HELP_STRING, HINIC3_UI_ESCAPE_MODE_SHOW_HELP_TIPS_STRING);
    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
}

static void
hinic3_multi_meter_show(struct unixctl_conn *conn, int argc, const char *argv[], void *aux)
{
    enum { HELP_ARGC = 2 };

    if ((argc == HELP_ARGC) && ((strcmp(argv[1], "-h") == 0) || (strcmp(argv[1], "--help") == 0)))
        hinic3_multi_meter_show_help(conn);
    else
        hinic3_multi_meter_show_sub(conn, argc, argv, aux);
}

static void
hinic3_multi_policy_show_one(struct ds *ds, struct hinic3_mtr_policy_node *policy)
{
    hinic3_ds_put_format(ds, "  policy id: %u\n", policy->policy_id);
    if (policy->has_next_meter == true)
        hinic3_ds_put_format(ds, "    %-15s %u\n", "next meter id:", policy->next_meter_id);
    else
        hinic3_ds_put_format(ds, "    %-15s NONE\n", "next meter id:");
}

static void
hinic3_multi_policy_show_all(struct ds *ds)
{
    struct hinic3_mtr_policy_node *iter = NULL;
    struct hinic3_mtr_policy_list *policy_list = hinic3_policy_list_get();

    if (policy_list->length == 0) {
        hinic3_ds_put_format(ds, HINIC3_UI_LEADING_SIGN_INFO "No policy.\n");
        return;
    }
    hinic3_mtr_policy_list_lock();
    LIST_FOR_EACH(iter, node, &policy_list->node) {
        hinic3_multi_policy_show_one(ds, iter);
        hinic3_ds_put_format(ds, "\n");
    }
    hinic3_ds_put_format(ds, "  policy-num: %u\n", policy_list->length);
    hinic3_mtr_policy_list_unlock();
}

static void
hinic3_multi_policy_show_sub(struct unixctl_conn *conn, int argc, const char *argv[], void *aux)
{
    int ret = 0;
    uint32_t policy_id;
    char *endPtr = NULL;
    struct ds ds = DS_EMPTY_INITIALIZER;
    struct hinic3_mtr_policy_node *policy = NULL;

    if (argc == 1) {
        hinic3_multi_policy_show_all(&ds);
    } else if (argc == MULTI_QOS_ARG_NUMS) {
        policy_id = strtoul(argv[1], &endPtr, DEC_BASE_NUM);
        if (endPtr == NULL || *endPtr != '\0') {
            hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR HINIC3_UI_INVALID_POLICY_ID_STR);
            ret = -1;
            goto end;
        }
        policy = hinic3_mtr_policy_find(policy_id);
        if (policy == NULL) {
            hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR HINIC3_UI_INVALID_POLICY_ID_STR);
            ret = -1;
        } else {
            hinic3_mtr_policy_list_lock();
            hinic3_multi_policy_show_one(&ds, policy);
            hinic3_mtr_policy_list_unlock();
        }
    } else {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR "Invalid number of parameters.\n");
        ret = -1;
    }
end:
    *(int *)aux = ret;
    if (ret != 0) {
        hinic3_command_reply_error(conn, hinic3_ds_cstr(&ds));
    } else {
        hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    }
    hinic3_ds_destroy(&ds);
}

static void
hinic3_multi_policy_show_help(struct unixctl_conn *conn)
{
    struct ds ds = DS_EMPTY_INITIALIZER;
    hinic3_ds_put_format(&ds, "%2s%s%s\n\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_CMD_USAGE_STRING, HINIC3_UI_SHOW_POLICY_TIPS_STRING);
    hinic3_ds_put_format(&ds, "%2s%s\n", HINIC3_UI_INDENT_SPACE, HINIC3_UI_OPTION_LIST_STRING);
    hinic3_ds_put_format(&ds, "%4s%-30s%-s\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_SHOW_POLICY_STR, HINIC3_UI_SHOW_POLICY_HELP_STR);
    hinic3_ds_put_format(&ds, "%4s%-30s%-s\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_ESCAPE_MODE_SHOW_HELP_STRING, HINIC3_UI_ESCAPE_MODE_SHOW_HELP_TIPS_STRING);
    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
}

static void
hinic3_multi_policy_show(struct unixctl_conn *conn, int argc, const char *argv[], void *aux)
{
    enum { HELP_ARGC = 2 };

    if ((argc == HELP_ARGC) && ((strcmp(argv[1], "-h") == 0) || (strcmp(argv[1], "--help") == 0)))
        hinic3_multi_policy_show_help(conn);
    else
        hinic3_multi_policy_show_sub(conn, argc, argv, aux);
}

static void
hinic3_multi_profile_show_one(struct ds *ds, struct hinic3_mtr_profile_node *profile)
{
    int index;
    struct qos_single_value *qos_value = &profile->profile;
    int value_ui_space;

    index = (qos_value->packet_mode == QOS_BW_TYPE) ? 0 : 1;

    if (index == 0) {
        value_ui_space = MULTI_KBPS_QOS_SPEACE_NUMS;
    } else {
        value_ui_space = MULTI_PPS_QOS_SPEACE_NUMS;
    }

    hinic3_ds_put_format(ds, "%2sprofile id: %u\n", HINIC3_UI_INDENT_SPACE, profile->profile_id);

    if (qos_value->is_RFC2697) {
        hinic3_ds_put_format(ds, "%4stype:%10ssrtcm_rfc2697\n", HINIC3_UI_INDENT_SPACE,
            HINIC3_UI_INDENT_SPACE);
        hinic3_ds_put_format(ds, "%4sEBS(%s):%*s%u\n", HINIC3_UI_INDENT_SPACE,
            g_profile_unit[index], value_ui_space, HINIC3_UI_INDENT_SPACE, qos_value->max_burst);
        hinic3_ds_put_format(ds, "%4sCIR(%s):%*s%u\n", HINIC3_UI_INDENT_SPACE,
            g_profile_unit[index], value_ui_space, HINIC3_UI_INDENT_SPACE, qos_value->min_rate);
        hinic3_ds_put_format(ds, "%4sCBS(%s):%*s%u\n", HINIC3_UI_INDENT_SPACE,
            g_profile_unit[index], value_ui_space, HINIC3_UI_INDENT_SPACE, qos_value->min_burst);
        hinic3_ds_put_format(ds, "%4smode:%10s%s\n", HINIC3_UI_INDENT_SPACE,
            value_ui_space, HINIC3_UI_INDENT_SPACE, g_profile_mode[index]);
    } else {
        hinic3_ds_put_format(ds, "%4stype:%10strtcm_rfc2698\n", HINIC3_UI_INDENT_SPACE,
            HINIC3_UI_INDENT_SPACE);
        hinic3_ds_put_format(ds, "%4sPIR(%s):%*s%u\n", HINIC3_UI_INDENT_SPACE,
            g_profile_unit[index], value_ui_space, HINIC3_UI_INDENT_SPACE, qos_value->max_rate);
        hinic3_ds_put_format(ds, "%4sPBS(%s):%*s%u\n", HINIC3_UI_INDENT_SPACE,
            g_profile_unit[index], value_ui_space, HINIC3_UI_INDENT_SPACE, qos_value->max_burst);
        hinic3_ds_put_format(ds, "%4sCIR(%s):%*s%u\n", HINIC3_UI_INDENT_SPACE,
            g_profile_unit[index], value_ui_space, HINIC3_UI_INDENT_SPACE, qos_value->min_rate);
        hinic3_ds_put_format(ds, "%4sCBS(%s):%*s%u\n", HINIC3_UI_INDENT_SPACE,
            g_profile_unit[index], value_ui_space, HINIC3_UI_INDENT_SPACE, qos_value->min_burst);
        hinic3_ds_put_format(ds, "%4smode:%10s%s\n", HINIC3_UI_INDENT_SPACE,
            HINIC3_UI_INDENT_SPACE, g_profile_mode[index]);
    }
}

static void
hinic3_multi_profile_show_all(struct ds *ds)
{
    struct hinic3_mtr_profile_node *iter = NULL;
    struct hinic3_mtr_profile_list *profile_list = hinic3_profile_list_get();

    if (profile_list->length == 0) {
        hinic3_ds_put_format(ds, HINIC3_UI_LEADING_SIGN_INFO "No profile.\n");
        return;
    }
    hinic3_mtr_profile_list_lock();
    LIST_FOR_EACH(iter, node, &profile_list->node) {
        hinic3_multi_profile_show_one(ds, iter);
        hinic3_ds_put_format(ds, "\n");
    }
    hinic3_ds_put_format(ds, "%2sprofile-num: %u\n", HINIC3_UI_INDENT_SPACE, profile_list->length);
    hinic3_mtr_profile_list_unlock();
}

static void
hinic3_multi_profile_show_sub(struct unixctl_conn *conn, int argc, const char *argv[], void *aux)
{
    int ret = 0;
    uint32_t porfile_id;
    char *endPtr = NULL;
    struct ds ds = DS_EMPTY_INITIALIZER;
    struct hinic3_mtr_profile_node *profile = NULL;

    if (argc == 1) {
        hinic3_multi_profile_show_all(&ds);
    } else if (argc == MULTI_QOS_ARG_NUMS) {
        porfile_id = strtoul(argv[1], &endPtr, DEC_BASE_NUM);
        if (endPtr == NULL || *endPtr != '\0') {
            hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR HINIC3_UI_INVALID_PROFILE_ID_STR);
            ret = -1;
            goto end;
        }
        profile = hinic3_mtr_profile_find(porfile_id);
        if (profile == NULL) {
            hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR HINIC3_UI_INVALID_PROFILE_ID_STR);
            ret = -1;
        } else {
            hinic3_mtr_policy_list_lock();
            hinic3_multi_profile_show_one(&ds, profile);
            hinic3_mtr_policy_list_unlock();
        }
    } else {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR "Invalid number of parameters.\n");
        ret = -1;
    }

end:
    *(int *)aux = ret;
    if (ret != 0)
        hinic3_command_reply_error(conn, hinic3_ds_cstr(&ds));
    else
        hinic3_command_reply(conn, hinic3_ds_cstr(&ds));

    hinic3_ds_destroy(&ds);
}

static void
hinic3_multi_profile_show_help(struct unixctl_conn *conn)
{
    struct ds ds = DS_EMPTY_INITIALIZER;
    hinic3_ds_put_format(&ds, "%2s%s%s\n\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_CMD_USAGE_STRING, HINIC3_UI_SHOW_PROFILE_TIPS_STRING);
    hinic3_ds_put_format(&ds, "%2s%s\n", HINIC3_UI_INDENT_SPACE, HINIC3_UI_OPTION_LIST_STRING);
    hinic3_ds_put_format(&ds, "%4s%-30s%-s\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_SHOW_PROFILE_STR, HINIC3_UI_SHOW_PROFILE_HELP_STR);
    hinic3_ds_put_format(&ds, "%4s%-30s%-s\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_ESCAPE_MODE_SHOW_HELP_STRING, HINIC3_UI_ESCAPE_MODE_SHOW_HELP_TIPS_STRING);
    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
}

static void
hinic3_multi_profile_show(struct unixctl_conn *conn, int argc, const char *argv[], void *aux)
{
    enum { HELP_ARGC = 2 };

    if ((argc == HELP_ARGC) && ((strcmp(argv[1], "-h") == 0) || (strcmp(argv[1], "--help") == 0)))
        hinic3_multi_profile_show_help(conn);
    else
        hinic3_multi_profile_show_sub(conn, argc, argv, aux);
}

static void
hinic3_multi_qos_dump_loss_sub(struct unixctl_conn *conn, int argc, const char *argv[], void *aux)
{
    uint16_t group_id;
    char *endPtr = NULL;
    uint16_t stats_mask = 0;
    struct ds ds = DS_EMPTY_INITIALIZER;
    struct hinic3_group_info *group_info = NULL;
    struct hovs_hqos_stats_batch_all hovs_stats = {0};

    if (argc != MULTI_QOS_ARG_NUMS) {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR "Invalid number of parameters.\n");
        goto err;
    }

    unsigned long tmp_group_id = strtoul(argv[1], &endPtr, DEC_BASE_NUM);
    if (tmp_group_id > MAX_QOS_ID_NUM || tmp_group_id == 0 || endPtr == NULL || *endPtr != '\0') {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR HINIC3_UI_INVALID_GROUP_ID_STR);
        goto err;
    }

    group_id = (uint16_t)tmp_group_id;
    group_info = hinic3_group_info_get(group_id);
    if (group_info->length == 0) {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR "Group has no port.\n");
        goto err;
    }

    if (hinic3_hqos_statistics_get_all_batch(QOS_TYPE_VM_LIMIT, &group_id, &hovs_stats, 1) != 0)
        HINIC3_LOG(ERR, QOS, "Get qos statistics failed.");

    if (group_info->meter[BW_TX_TYPE].is_used == true || group_info->meter[PPS_TX_TYPE].is_used == true)
        stats_mask |= MULTI_QOS_MASK_TX;

    if (group_info->meter[BW_RX_TYPE].is_used == true || group_info->meter[PPS_RX_TYPE].is_used == true)
        stats_mask |= MULTI_QOS_MASK_RX;

    hinic3_ds_put_format(&ds, "%2sgroup id[%u] dump loss:\n", HINIC3_UI_INDENT_SPACE, group_id);
    hinic3_multi_qos_show_loss(&ds, hovs_stats, stats_mask);
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
hinic3_multi_qos_dump_loss_help(struct unixctl_conn *conn)
{
    struct ds ds = DS_EMPTY_INITIALIZER;
    hinic3_ds_put_format(&ds, "%2s%s%s\n\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_CMD_USAGE_STRING, HINIC3_UI_DUMP_QOS_LOSS_TIPS_STRING);
    hinic3_ds_put_format(&ds, "%2s%s\n", HINIC3_UI_INDENT_SPACE, HINIC3_UI_OPTION_LIST_STRING);
    hinic3_ds_put_format(&ds, "%4s%-30s%-s\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_DUMP_QOS_LOSS_HRLP_TITLE_STRING, HINIC3_UI_DUMP_QOS_LOSS_HELP_STR);
    hinic3_ds_put_format(&ds, "%4s%-30s%-s\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_ESCAPE_MODE_SHOW_HELP_STRING, HINIC3_UI_ESCAPE_MODE_SHOW_HELP_TIPS_STRING);
    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
}

static void
hinic3_multi_qos_dump_loss(struct unixctl_conn *conn, int argc, const char *argv[], void *aux)
{
    if ((strcmp(argv[1], "-h") == 0) || (strcmp(argv[1], "--help") == 0))
        hinic3_multi_qos_dump_loss_help(conn);
    else
        hinic3_multi_qos_dump_loss_sub(conn, argc, argv, aux);
}

static int
hinic3_multi_net_qos_show_one(uint32_t meter_id, enum hinic3_qos_tablehead index, struct ds *ds)
{
    int ret;
    uint64_t max_rate;
    uint64_t max_burst;
    struct hinic3_meter_node *meter = NULL;
    struct qos_single_value *profile = NULL;
    group_qos_type_key *qos_key = hinic3_get_qos_type_key();
    const char **vm_qos_unit = hinic3_vm_qos_unit_get();

    hinic3_meter_list_lock();
    meter = hinic3_meter_find(meter_id);
    if (meter == NULL || meter->profile == NULL) {
        hinic3_meter_list_unlock();
        HINIC3_LOG(ERR, QOS, "Multi net qos show: meter id is not find. meter id is %u.", meter_id);
        hinic3_ds_put_format(ds, HINIC3_UI_LEADING_SIGN_ERROR "Meter id is not find. meter id is %u.\n", meter_id);
        return -1;
    }
    profile = &meter->profile->profile;

    ret = hinic3_net_qos_limit_get(0, meter->dir, profile->packet_mode, &max_rate, &max_burst);
    if (ret != 0) {
        hinic3_meter_list_unlock();
        HINIC3_LOG(ERR, QOS, "Multi net qos show: get net qos value failed.");
        hinic3_ds_put_format(ds, HINIC3_UI_LEADING_SIGN_ERROR "Get qos value failed.\n");
        return -1;
    }

    hinic3_ds_put_format(ds, "%4s%s(%s):\n", HINIC3_UI_INDENT_SPACE, qos_key->qos_type_key[index], vm_qos_unit[index]);
    hinic3_ds_put_format(ds, "%6sCIR: %" PRIu64 " (%" PRIu64 ")\n", HINIC3_UI_INDENT_SPACE, profile->min_rate,
        max_rate);
    hinic3_ds_put_format(ds, "%6sCBS: %" PRIu64 " (%" PRIu64 ")\n", HINIC3_UI_INDENT_SPACE, profile->min_burst,
        max_burst);
    hinic3_meter_list_unlock();    
    return 0;
}

static void
hinic3_multi_net_qos_dump_show(struct unixctl_conn *conn, int argc, const char *argv[] HINIC3_UNUSED, void *aux)
{
    int ret;
    struct ds ds = DS_EMPTY_INITIALIZER;
    struct hinic3_net_meter_info *net_meter = hinic3_net_meter_info_get();

    if (argc != 1) {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR "Invalid number of parameters.\n");
        goto err;
    }
    hinic3_ds_put_format(&ds, "%2sNET QoS info:\n", HINIC3_UI_INDENT_SPACE);
    for (int i = 0; i < QOS_TABLEHEAD_NUM; i++) {
        if (net_meter[i].is_used == true) {
            ret = hinic3_multi_net_qos_show_one(net_meter[i].meter_id, i, &ds);
            if (ret != 0) {
                goto err;
            }
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
hinic3_multi_net_qos_dump_loss(struct unixctl_conn *conn, int argc, const char *argv[] HINIC3_UNUSED, void *aux)
{
    uint16_t host_id = 0;
    uint16_t stats_mask = 0;
    struct ds ds = DS_EMPTY_INITIALIZER;
    struct hovs_hqos_stats_batch_all hovs_stats = {0};
    struct hinic3_net_meter_info *net_meter = hinic3_net_meter_info_get();

    if (argc != 1) {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR "Invalid number of parameters.\n");
        goto err;
    }

    if (hinic3_hqos_statistics_get_all_batch(QOS_TYPE_NET_LIMIT, &host_id, &hovs_stats, 1) != 0) {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR "Get stats failed.\n");
        goto err;
    }

    if (net_meter[BW_TX_TYPE].is_used == true || net_meter[PPS_TX_TYPE].is_used == true)
        stats_mask |= MULTI_QOS_MASK_TX;
    if (net_meter[BW_RX_TYPE].is_used == true || net_meter[PPS_RX_TYPE].is_used == true)
        stats_mask |= MULTI_QOS_MASK_RX;

    hinic3_ds_put_format(&ds, "%2snet qos loss:\n", HINIC3_UI_INDENT_SPACE);
    hinic3_multi_qos_show_loss(&ds, hovs_stats, stats_mask);
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
hinic3_multi_qos_show_stats(struct ds *ds, struct hovs_hqos_stats_batch_all hovs_stats, uint16_t stats_mask)
{
    hinic3_ds_put_format(ds, "%2sGreen Color:\n", HINIC3_UI_INDENT_SPACE);
    if ((stats_mask & MULTI_QOS_MASK_TX) == MULTI_QOS_MASK_TX) {
        hinic3_ds_put_format(ds, "%4s%-30s: %u\n", HINIC3_UI_INDENT_SPACE, "tx-pkts-num",
            hovs_stats.tx_pkts[RTE_COLOR_GREEN]);
        hinic3_ds_put_format(ds, "%4s%-30s: %u\n", HINIC3_UI_INDENT_SPACE, "tx-byte-num",
            hovs_stats.tx_bytes[RTE_COLOR_GREEN]);
    } else {
        hinic3_ds_put_format(ds, "%4s%-30s: N/A\n", HINIC3_UI_INDENT_SPACE, "tx-pkts-num");
        hinic3_ds_put_format(ds, "%4s%-30s: N/A\n", HINIC3_UI_INDENT_SPACE, "tx-byte-num");
    }
    if ((stats_mask & MULTI_QOS_MASK_RX) == MULTI_QOS_MASK_RX) {
        hinic3_ds_put_format(ds, "%4s%-30s: %u\n", HINIC3_UI_INDENT_SPACE, "rx-pkts-num",
            hovs_stats.rx_pkts[RTE_COLOR_GREEN]);
        hinic3_ds_put_format(ds, "%4s%-30s: %u\n", HINIC3_UI_INDENT_SPACE, "rx-byte-num",
            hovs_stats.rx_bytes[RTE_COLOR_GREEN]);
    } else {
        hinic3_ds_put_format(ds, "%4s%-30s: N/A\n", HINIC3_UI_INDENT_SPACE, "rx-pkts-num");
        hinic3_ds_put_format(ds, "%4s%-30s: N/A\n", HINIC3_UI_INDENT_SPACE, "rx-byte-num");
    }
}

static void
hinic3_multi_qos_stats_show_sub(struct unixctl_conn *conn, int argc, const char *argv[], void *aux)
{
    uint16_t group_id;
    char *endPtr = NULL;
    uint16_t stats_mask = 0;
    struct ds ds = DS_EMPTY_INITIALIZER;
    struct hinic3_group_info *group_info = NULL;
    struct hovs_hqos_stats_batch_all hovs_stats = {0};

    if (argc != MULTI_QOS_ARG_NUMS) {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR "Invalid number of parameters.\n");
        goto err;
    }

    unsigned long tmp_group_id = strtoul(argv[1], &endPtr, DEC_BASE_NUM);
    if (tmp_group_id > MAX_QOS_ID_NUM || tmp_group_id == 0 || endPtr == NULL || *endPtr != '\0') {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR HINIC3_UI_INVALID_GROUP_ID_STR);
        goto err;
    }
    group_id = (uint16_t)tmp_group_id;
    group_info = hinic3_group_info_get(group_id);
    if (group_info->length == 0) {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR "Group has no port.\n");
        goto err;
    }

    if (hinic3_hqos_statistics_get_all_batch(QOS_TYPE_VM_LIMIT, &group_info->group_id, &hovs_stats, 1) != 0) {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR "Get stats failed.\n");
        goto err;
    }
    if (group_info->meter[BW_TX_TYPE].is_used == true || group_info->meter[PPS_TX_TYPE].is_used == true)
        stats_mask |= MULTI_QOS_MASK_TX;
    if (group_info->meter[BW_RX_TYPE].is_used == true || group_info->meter[PPS_RX_TYPE].is_used == true)
        stats_mask |= MULTI_QOS_MASK_RX;

    hinic3_multi_qos_show_stats(&ds, hovs_stats, stats_mask);
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
hinic3_multi_qos_stats_show_help(struct unixctl_conn *conn)
{
    struct ds ds = DS_EMPTY_INITIALIZER;
    hinic3_ds_put_format(&ds, "%2s%s%s\n\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_CMD_USAGE_STRING, HINIC3_UI_DUMP_QOS_STATS_TIPS_STRING);
    hinic3_ds_put_format(&ds, "%2s%s\n", HINIC3_UI_INDENT_SPACE, HINIC3_UI_OPTION_LIST_STRING);
    hinic3_ds_put_format(&ds, "%4s%-30s%-s\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_DUMP_QOS_LOSS_HRLP_TITLE_STRING, HINIC3_UI_DUMP_FLOW_QOS_STAT_HELP_STR);
    hinic3_ds_put_format(&ds, "%4s%-30s%-s\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_ESCAPE_MODE_SHOW_HELP_STRING, HINIC3_UI_ESCAPE_MODE_SHOW_HELP_TIPS_STRING);
    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
}

static void
hinic3_multi_qos_stats_show(struct unixctl_conn *conn, int argc, const char *argv[], void *aux)
{
    if ((strcmp(argv[1], "-h") == 0) || (strcmp(argv[1], "--help") == 0))
        hinic3_multi_qos_stats_show_help(conn);
    else
        hinic3_multi_qos_stats_show_sub(conn, argc, argv, aux);
}

static void
hinic3_multi_port_qos_stats_show_sub(struct unixctl_conn *conn, int argc, const char *argv[], void *aux)
{
    uint16_t stats_mask = 0;
    struct hinic3_vf_dev *vf_dev = NULL;
    struct ds ds = DS_EMPTY_INITIALIZER;
    struct hovs_hqos_stats_batch_all hovs_stats = {0};

    if (argc != MULTI_QOS_ARG_NUMS) {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR "Invalid number of parameters.\n");
        goto err;
    }

    vf_dev = hinic3_multi_qos_get_dev(argv, &ds);
    if (vf_dev == NULL)
        goto err;

    if (vf_dev->qos_type == VF_NONE_QOS_FLAG) {
        hinic3_ds_put_format(&ds, "%sPort has no qos.\n", HINIC3_UI_LEADING_SIGN_FAILURE);
        goto err;
    }

    if (hinic3_hqos_statistics_get_all_batch(QOS_TYPE_FUNC_LIMIT, &vf_dev->vport_id, &hovs_stats, 1) != 0) {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR "Get stats failed.\n");
        goto err;
    }

    if (vf_dev->mtr[BW_TX_TYPE].is_used == true || vf_dev->mtr[PPS_TX_TYPE].is_used == true)
        stats_mask |= MULTI_QOS_MASK_TX;
    if (vf_dev->mtr[BW_RX_TYPE].is_used == true || vf_dev->mtr[PPS_RX_TYPE].is_used == true)
        stats_mask |= MULTI_QOS_MASK_RX;

    hinic3_multi_qos_show_stats(&ds, hovs_stats, stats_mask);
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
hinic3_multi_port_qos_stats_show_help(struct unixctl_conn *conn)
{
    struct ds ds = DS_EMPTY_INITIALIZER;
    hinic3_ds_put_format(&ds, "%2s%s%s\n\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_CMD_USAGE_STRING, HINIC3_UI_DUMP_PORT_QOS_STATS_TIPS_STRING);
    hinic3_ds_put_format(&ds, "%2s%s\n", HINIC3_UI_INDENT_SPACE, HINIC3_UI_OPTION_LIST_STRING);
    hinic3_ds_put_format(&ds, "%4s%-30s%-s\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_SHOW_PORT_QOS_HELP_NAME_STR, HINIC3_UI_SHOW_PORT_QOS_HELP_STR);
    hinic3_ds_put_format(&ds, "%4s%-30s%-s\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_ESCAPE_MODE_SHOW_HELP_STRING, HINIC3_UI_ESCAPE_MODE_SHOW_HELP_TIPS_STRING);
    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
}

static void
hinic3_multi_port_qos_stats_show(struct unixctl_conn *conn, int argc, const char *argv[],
    void *aux)
{
    if ((strcmp(argv[1], "-h") == 0) || (strcmp(argv[1], "--help") == 0))
        hinic3_multi_port_qos_stats_show_help(conn);
    else
        hinic3_multi_port_qos_stats_show_sub(conn, argc, argv, aux);
}

static void
hinic3_multi_net_qos_stats_show(struct unixctl_conn *conn, int argc, const char *argv[] HINIC3_UNUSED, void *aux)
{
    uint16_t stats_mask = 0;
    uint16_t host_id = 0;
    struct ds ds = DS_EMPTY_INITIALIZER;
    struct hovs_hqos_stats_batch_all hovs_stats = {0};
    struct hinic3_net_meter_info *net_qos_info = hinic3_net_meter_info_get();

    if (argc != 1) {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR "Invalid number of parameters.\n");
        goto err;
    }

    if (hinic3_hqos_statistics_get_all_batch(QOS_TYPE_NET_LIMIT, &host_id, &hovs_stats, 1) != 0) {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR "Get stats failed.\n");
        goto err;
    }

    if (net_qos_info[BW_TX_TYPE].is_used == true || net_qos_info[PPS_TX_TYPE].is_used == true)
        stats_mask |= MULTI_QOS_MASK_TX;
    if (net_qos_info[BW_RX_TYPE].is_used == true || net_qos_info[PPS_RX_TYPE].is_used == true)
        stats_mask |= MULTI_QOS_MASK_RX;

    hinic3_multi_qos_show_stats(&ds, hovs_stats, stats_mask);
    *(int *)aux = 0;
    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
    return;
err:
    *(int *)aux = -1;
    hinic3_command_reply_error(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
}

void
unixctl_hinic3_multi_qos_dfx_init(void)
{
    hinic3_command_register("hwoff/dump-qos-loss", "{ INTEGER<1-1023> | { -h | --help } }", 1, 1, hinic3_multi_qos_dump_loss, NULL);
    hinic3_command_register("hwoff/show-qos-speed", "{ INTEGER<1-1023> | { -h | --help }}", 1, 1, hinic3_multi_qos_speed_show, NULL);
    hinic3_command_register("hwoff/show-port-qos", "{ <port-name> | { -h | --help } }", 1, 1, hinic3_multi_port_qos_dump_show, NULL);
    hinic3_command_register("hwoff/dump-qos", "[ INTEGER<1-1023> | { -h | --help } ]", 0, 1, hinic3_multi_qos_dump_show, NULL);

    hinic3_command_register("hwoff/dump-port-qos-loss", "{ <port-name> | { -h | --help } }", 1, 1,
                            hinic3_multi_port_qos_dump_loss, NULL);
    hinic3_command_register("hwoff/show-meter", "[ <meter_id> | { -h | --help } ]", 0, 1, hinic3_multi_meter_show, NULL);
    hinic3_command_register("hwoff/show-policy", "[ <policy_id> | { -h | --help } ]", 0, 1, hinic3_multi_policy_show, NULL);
    hinic3_command_register("hwoff/show-profile", "[ <profile_id> | { -h | --help } ]", 0, 1, hinic3_multi_profile_show, NULL);
    hinic3_command_register("hwoff/show-port-qos-speed", "{ <port-name> | { -h | --help } }", 1, 1,
                            hinic3_multi_port_qos_speed_show, NULL);
    hinic3_command_register("hwoff/dump-net-qos", "", 0, 0, hinic3_multi_net_qos_dump_show, NULL);
    hinic3_command_register("hwoff/dump-net-qos-loss", "", 0, 0, hinic3_multi_net_qos_dump_loss, NULL);
    hinic3_command_register("hwoff/dump-qos-stats", "{ INTEGER<1-1023> | { -h | --help } }", 1, 1, hinic3_multi_qos_stats_show, NULL);
    hinic3_command_register("hwoff/dump-port-qos-stats", "{ <port-name> | { -h | --help }  }", 1, 1,
                            hinic3_multi_port_qos_stats_show, NULL);
    hinic3_command_register("hwoff/dump-net-qos-stats", "", 0, 0, hinic3_multi_net_qos_stats_show, NULL);
}
