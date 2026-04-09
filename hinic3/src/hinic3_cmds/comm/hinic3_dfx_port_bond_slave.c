/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */

#include <stdint.h>
#include "rte_ethdev.h"
#include "rte_string_fns.h"
#include "hinic3_provider.h"
#include "hinic3_iface_global.h"
#include "hinic3_iface_port.h"
#include "hinic3_ui_string.h"
#include "hinic3_log.h"
#include "hinic3_vf_port_qos_public.h"
#include "hinic3_port_util.h"
#include <rte_atomic.h>
#include "hinic3_tlv_key.h"
#include "hinic3_offload_flow_port.h"
#include "hinic3_ds.h"
#include "hinic3_smap.h"
#include "hinic3_command.h"
#include "hinic3_vf_controller.h"
#include "hinic3_bond_controller.h"
#include "hinic3_dfx_port.h"

static void
hinic3_show_bond_slave_stats(struct ds *output_msg, const struct hovs_bond_slave_stats *bond_slave_info,
    uint8_t index)
{
    uint8_t offset = (1 << index);
    uint8_t bond_postion = (bond_slave_info->vld_slave & offset);

    if (bond_postion != 0) {
        hinic3_ds_put_format(output_msg, "\t%-30s : %lu\n", "rx-pkts", bond_slave_info->rx_pkts[index]);
        hinic3_ds_put_format(output_msg, "\t%-30s : %lu\n", "tx-pkts", bond_slave_info->tx_pkts[index]);
        hinic3_ds_put_format(output_msg, "\t%-30s : %lu\n", "rx-bytes", bond_slave_info->rx_bytes[index]);
        hinic3_ds_put_format(output_msg, "\t%-30s : %lu\n", "tx-bytes", bond_slave_info->tx_bytes[index]);
        return;
    }

    hinic3_ds_put_format(output_msg, "\t%-30s : %lu\n", "bond_slave effective Index", bond_slave_info->vld_slave);
    return;
}

static int
hinic3_split_slave_name(char *slaves, char *slaves_name,
    char bond_slave_name[][HINIC3_BOND_ARG_SLAVE_NAME_LEN], int len HINIC3_UNUSED)
{
    int slaves_num = 0;
    int slaves_name_num = 0;
    char *slaves_index[HINIC3_BOND_SLAVE_NUM] = {0};
    char *slaves_name_str[HINIC3_BOND_SLAVE_NUM] = {0};

    slaves_num = rte_strsplit(slaves, HINIC3_BOND_SLAVE_NAME_LEN, slaves_index, HINIC3_BOND_SLAVE_NUM, ',');
    if (slaves_num <= 0 || slaves_num > HINIC3_BOND_SLAVE_NUM) {
        HINIC3_LOG(ERR, VPORT, "failed to get bond slaves num");
        return -1;
    }

    slaves_name_num = rte_strsplit(slaves_name, HINIC3_BOND_SLAVE_NAME_LEN, slaves_name_str, HINIC3_BOND_SLAVE_NUM, ',');
    if (slaves_name_num <= 0 || slaves_name_num > HINIC3_BOND_SLAVE_NUM) {
        HINIC3_LOG(ERR, VPORT, "failed to get bond slaves name num");
        return -1;
    }

    if (slaves_num != slaves_name_num) {
        HINIC3_LOG(ERR, VPORT, "slaves_num is not equal to slaves_name_num");
        return -1;
    }

    for (uint32_t i = 0, j = 0; i < HINIC3_BOND_SLAVE_NUM && j < (uint32_t)slaves_num; i++) {
        char *endPtr = NULL;
        uint32_t index = strtoul(slaves_index[j], &endPtr, STR_TO_DEC_NUM);
        if (endPtr == NULL || *endPtr != '\0')
            return -1;
        if (index >= HINIC3_BOND_SLAVE_NUM)
            return -1;

        if (i == index) {
            strcpy(bond_slave_name[i], slaves_name_str[j]);
            j++;
        }
    }

    return 0;
}



static int
hinic3_process_bond_slave_name(const char *slaves,
    const char *slaves_name, char bond_slave_name[][HINIC3_BOND_ARG_SLAVE_NAME_LEN])
{
    int ret;
    char slaves_str[HINIC3_BOND_SLAVE_NAME_LEN] = {0};
    char slaves_name_str[HINIC3_BOND_SLAVE_NAME_LEN] = {0};

    strncpy(slaves_str, slaves, HINIC3_BOND_SLAVE_NAME_LEN - 1);
    slaves_str[HINIC3_BOND_SLAVE_NAME_LEN - 1] = '\0';

    strncpy(slaves_name_str, slaves_name, HINIC3_BOND_SLAVE_NAME_LEN - 1);
    slaves_name_str[HINIC3_BOND_SLAVE_NAME_LEN - 1] = '\0';

    ret = hinic3_split_slave_name(slaves_str, slaves_name_str, bond_slave_name, HINIC3_BOND_SLAVE_NAME_LEN);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "hinic3_split_slave_name is failed ret is %d!", ret);
        return -1;
    }

    return 0;
}
 
static int
hinic3_get_bond_slave_name_from_map(struct smap *bond_info,
    char bond_slave_name[][HINIC3_BOND_ARG_SLAVE_NAME_LEN])
{
    int ret;
    const char *slaves = NULL;
    const char *slaves_name = NULL;

    slaves = hinic3_smap_get(bond_info, HINIC3_BOND_SLAVES);
    if (slaves == NULL) {
        HINIC3_LOG(ERR, VPORT, "get bond slave name from map, slaves is NULL!");
        return -1;
    }

    slaves_name = hinic3_smap_get(bond_info, HINIC3_BOND_ARG_SLAVE_NAME_STR);
    if (slaves_name == NULL) {
        HINIC3_LOG(ERR, VPORT, "slaves_name is NULL!");
        return -1;
    }

    ret = hinic3_process_bond_slave_name(slaves, slaves_name, bond_slave_name);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "process bond slave name string fail ret is %d!", ret);
        return -1;
    }

    return 0;
}

static int
hinic3_get_bond_slave_name(uint16_t vport_id, char bond_slave_name[][HINIC3_BOND_ARG_SLAVE_NAME_LEN])
{
    int ret;
    uint16_t ifindex = 0;
    struct hinic3_bond_dev *bond_dev = NULL;
    struct smap bond_info;

    ret = hinic3_get_port_id_by_ifindex(vport_id, &ifindex);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "Get pf bond slave name: get port id failed, vport_id %X!", vport_id);
        return -1;
    }
    bond_dev = (struct hinic3_bond_dev *)hinic3_get_private_data(ifindex);
    if (bond_dev == NULL) {
        HINIC3_LOG(ERR, VPORT, "The current device private_data is NULL!");
        return -1;
    }

    hinic3_smap_init(&bond_info);
    ret = hinic3_bond_mgmt_get(bond_dev->bond_id, &bond_info);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "failed to get bond info by bond_id %u ret is %d!", bond_dev->bond_id, ret);
        hinic3_smap_destroy(&bond_info);
        return -1;
    }
    ret = hinic3_get_bond_slave_name_from_map(&bond_info, bond_slave_name);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "failed to get bond name from smap ret is %d!", ret);
        hinic3_smap_destroy(&bond_info);
        return -1;
    }

    hinic3_smap_destroy(&bond_info);
    return 0;
}

static int
hinic3_get_vport_by_netdev_name(const char *cmd, char *netdev_name, uint16_t *port_id)
{
    int ret;
    uint16_t dpdk_index_id;
    uint32_t if_index;

    if (netdev_name == NULL || port_id == NULL)
        return -1;

    ret = rte_eth_dev_get_port_by_name(netdev_name, &dpdk_index_id);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "hwoff/%s failed, get port id error!", cmd);
        return -1;
    }
    ret = hinic3_get_port_ifindex(dpdk_index_id, &if_index);
    if (ret != 0 || if_index > UINT16_MAX) {
        HINIC3_LOG(ERR, VPORT, "hwoff/%s failed, device ifindex error", cmd);
        return -1;
    } else {
        *port_id = (uint16_t)if_index;
    }

    return 0;
}

static int
hinic3_dump_bond_slave_info(struct ds *ds, char *netdev_name, uint32_t netdev_name_len)
{
    int ret = 0;
    uint16_t port_id = 0;
    char bond_slave_name[HINIC3_BOND_SLAVE_NUM][HINIC3_BOND_ARG_SLAVE_NAME_LEN] = {
        {"Invalid"}, {"Invalid"}, {"Invalid"}, {"Invalid"}
    };
    struct hovs_bond_slave_stats bond_slave_info = {0};

    if (netdev_name_len >= HINIC3_NETDEV_NAME_MAX_LENGTH || netdev_name_len <= 0) {
        hinic3_ds_put_format(ds, "%s%s, please input -h or --help to get help info.\n",
            HINIC3_UI_LEADING_SIGN_ERROR, HINIC3_UI_ERROR_WRONG_PARAMETER);
        return -1;
    }
    ret = hinic3_get_vport_by_netdev_name("dump-ports", netdev_name, &port_id);
    if (ret != 0) {
        hinic3_ds_put_format(ds, "%s%s, please input -h or --help to get help info.\n",
            HINIC3_UI_LEADING_SIGN_ERROR, HINIC3_UI_ERROR_WRONG_PARAMETER);
        return -1;
    }

    if (!hinic3_is_bond_by_prefix(port_id)) {
        hinic3_ds_put_format(ds, "%s%s, please input -h or --help to get help info.\n",
            HINIC3_UI_LEADING_SIGN_ERROR, HINIC3_UI_BOND_TYPE_ERROR_STRING);
        return -1;
    }

    ret = hinic3_port_mgmt_get_bond_slave_info(port_id, &bond_slave_info);
    if (ret != 0) {
        hinic3_ds_put_format(ds, HINIC3_UI_DUMP_BOND_SLAVE_FAILURE_STRING, HINIC3_UI_LEADING_SIGN_FAILURE, ret);
        return -1;
    }

    ret = hinic3_get_bond_slave_name(port_id, bond_slave_name);
    if (ret != 0) {
        hinic3_ds_put_format(ds, HINIC3_UI_GET_SLAVE_NAME_STRING, HINIC3_UI_LEADING_SIGN_FAILURE, ret);
        return -1;
    }

    for (uint8_t index = 0; index < HINIC3_BOND_SLAVE_NUM; index++) {
        hinic3_ds_put_format(ds, "BOND SLAVE[%u] INFO:\n", index);
        hinic3_ds_put_format(ds, "\t%-30s : %s\n", "port_name", bond_slave_name[index]);
        hinic3_show_bond_slave_stats(ds, &bond_slave_info, index);
    }

    return 0;
}

void
hinic3_dump_bond_slave_info_cmd(struct unixctl_conn *conn, int argc, const char *argv[], void *aux)
{
    *(int *)aux = -1;
    bool is_global = false;
    char bond_name[HINIC3_NETDEV_NAME_MAX_LENGTH] = {0};
    struct ds ds = DS_EMPTY_INITIALIZER;

    if (argc <= 1) {
        hinic3_ds_put_format(&ds, "%s%s, please input -h or --help to get help info.\n",
            HINIC3_UI_LEADING_SIGN_ERROR, HINIC3_UI_ERROR_WRONG_PARAMETER);
        hinic3_ds_put_format(&ds, "%s%s\n", HINIC3_UI_CMD_USAGE_STRING, HINIC3_UI_DUMP_BOND_SLAVE_STRING);
        *(int *)aux = -1;
    }

    if ((strcmp(argv[1], "-h") == 0) || (strcmp(argv[1], "--help") == 0)) {
        hinic3_ds_put_format(&ds, "%2s%s%s\n\n", HINIC3_UI_INDENT_SPACE,
            HINIC3_UI_CMD_USAGE_STRING, HINIC3_UI_DUMP_BOND_SLAVE_TIPS_STRING);
        hinic3_ds_put_format(&ds, "%2s%s\n", HINIC3_UI_INDENT_SPACE, HINIC3_UI_OPTION_LIST_STRING);
        hinic3_ds_put_format(&ds, "%4s%-30s%-s\n", HINIC3_UI_INDENT_SPACE,
            HINIC3_UI_DUMP_BOND_SLAVE_HELP_BOND_STR, HINIC3_UI_DUMP_BOND_SLAVE_HELP_STR);
        hinic3_ds_put_format(&ds, "%4s%-30s%-s\n", HINIC3_UI_INDENT_SPACE,
            HINIC3_UI_ESCAPE_MODE_SHOW_HELP_STRING, HINIC3_UI_ESCAPE_MODE_SHOW_HELP_TIPS_STRING);
        *(int *)aux = 0;
    } else {
        if (hinic3_dfx_port_process_args(argc, argv, bond_name, &is_global) == true)
            *(int *)aux = hinic3_dump_bond_slave_info(&ds, bond_name, strlen(bond_name));
    }

    if (*(int *)aux != 0) {
        hinic3_command_reply_error(conn, hinic3_ds_cstr(&ds));
        hinic3_ds_destroy(&ds);
        return;
    }

    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
    return;
}
