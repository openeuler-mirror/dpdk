/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2024 Huawei Technologies Co., Ltd
 */

#include "hinic3_ui_string.h"
#include "hinic3_ds.h"
#include "hinic3_vf_port_qos_public.h"
#include "hinic3_vf_controller.h"
#include "hinic3_bond_controller.h"
#include "hinic3_dfx_port.h"

static int
hinic3_dump_virtual_upcall_queues_info(struct ds *ds)
{
    struct hinic3_virtual_queue_info info = {0};
    int ret = hinic3_virtual_queue_get_upcall_info(&info);
    if (ret != 0) {
        hinic3_ds_put_format_prefix(
            ds, 0, HINIC3_UI_LEADING_SIGN_FAILURE, "%s%d\n", HINIC3_UI_DFX_COMMAND_VIRTUAL_QUEUE_ERROR_STRING, ret);
        return -EPERM;
    }

    hinic3_ds_put_format_prefix(
        ds, INDENT_2, HINIC3_UI_EMPTY_STRING, HINIC3_UI_DFX_COMMAND_UPCALL_VIRTUAL_INFO_HEADER_STRING "\n");
    hinic3_ds_put_format_prefix(ds, INDENT_4, HINIC3_UI_EMPTY_STRING,
        "%-45s%u\n", HINIC3_UI_DFX_COMMAND_MAX_PHYSICAL_QUEUE_STRING, info.physical_queue_num);
    hinic3_ds_put_format_prefix(ds, INDENT_4, HINIC3_UI_EMPTY_STRING,
        "%-45s%u\n", HINIC3_UI_DFX_COMMAND_VIRTUAL_UPCALL_QUEUE_MAX_STRING, info.total_virtual_queue_num);
    hinic3_ds_put_format_prefix(ds, INDENT_4, HINIC3_UI_EMPTY_STRING,
        "%-45s%u\n", HINIC3_UI_DFX_COMMAND_VIRTUAL_UPCALL_QUEUE_AVAILIABLE_STRING, info.left_virtual_queue_num);
    for (uint8_t group_index = 0; group_index < info.virtual_queue_group_num; ++group_index) {
        hinic3_ds_put_format_prefix(ds, INDENT_4, HINIC3_UI_EMPTY_STRING,
            "%s%-3u%-37s%u\n", HINIC3_UI_DFX_COMMAND_GROUP_STRING, group_index,
            HINIC3_UI_DFX_COMMAND_AVAILIBLE_QUEUE_EVERY_GROUP_STRING, info.left_group_queue_num[group_index]);
    }
    hinic3_ds_put_format_prefix(ds, INDENT_4, HINIC3_UI_EMPTY_STRING,
        "%-45s%u\n", HINIC3_UI_DFX_COMMAND_USED_VIRTUAL_UPCALL_QUEUE_STRING,
        info.total_virtual_queue_num - info.left_virtual_queue_num);

    return 0;
}

static void
hinic3_dump_virtual_queue_table_item_to_screen(
    struct ds *ds, const char *port_name, struct hinic3_queue *rxq)
{
    if (rxq == NULL)
        return;

    struct hinic3_virtual_queue *virtual_rxq = rxq->variant.virtqueue;

    if (virtual_rxq == NULL)
        return;

    hinic3_ds_put_format_prefix(ds, INDENT_2, HINIC3_UI_EMPTY_STRING, "port-name:    %s\n", port_name);
    hinic3_ds_put_format_prefix(ds, INDENT_2, HINIC3_UI_EMPTY_STRING, "queue-id:     %u\n", rxq->queue_id);
    hinic3_ds_put_format_prefix(ds, INDENT_2, HINIC3_UI_EMPTY_STRING, "vport-id:     %u\n", rxq->vport_id);
    hinic3_ds_put_format_prefix(ds, INDENT_2, HINIC3_UI_EMPTY_STRING, "queue-name:   %s\n", virtual_rxq->ring_name);
}

static char *
hinic3_get_port_name_by_virtual_queue(struct ds *ds, struct hinic3_virtual_queue *virtual_rxq)
{
    struct rte_eth_dev_data *data = NULL;
    struct hinic3_vf_dev *vf_dev = NULL;
    char *port_name = NULL;
    struct hinic3_queue *rxq = virtual_rxq->queue_info;

    vf_dev = (struct hinic3_vf_dev *)hinic3_get_private_data(rxq->dpdk_index_id);
    if (vf_dev == NULL) {
        hinic3_ds_put_format_prefix(ds, 0, HINIC3_UI_LEADING_SIGN_ERROR,
            HINIC3_UI_PORT_DUMP_UPCALL_QUEUE_VF_DEV_ERR_STR"\n");
        return NULL;
    }
    data = vf_dev->dev->data;
    port_name = data->name;

    return port_name + strlen(HINIC3_ETH_VDEV_DRV_NAME);
}

static int hinic3_dump_virtual_upcall_queue_all(struct ds *ds)
{
    const char *port_name = NULL;
    struct hinic3_queue *queue = NULL;
    struct hinic3_virtual_queue *virtual_queue = NULL;
    struct hinic3_physical_queue *physical_queue = NULL;
    struct hinic3_virtual_queue_manager *manager = hinic3_get_virtual_queue_manager();
    struct hinic3_virtual_queue_info info = {0};
    bool need_newline = false;
    hinic3_virtual_queue_get_upcall_info(&info);
    if (info.total_virtual_queue_num == info.left_virtual_queue_num) {
        hinic3_ds_put_format_prefix(
            ds, 0, HINIC3_UI_LEADING_SIGN_INFO, "%s\n", HINIC3_UI_PORT_DUMP_UPCALL_QUEUE_ALL_IDLE_STRING);
        return 0;
    }

    for (uint8_t i = 0; i < manager->virtual_queue_group_num; ++i) {
        for (uint16_t j = 0; j < manager->physical_queue_num; ++j) {
            physical_queue = &manager->physical_queues[j];
            if (physical_queue->is_virtual_queue_valids[i] == false) {
                continue;
            }
            virtual_queue = physical_queue->virtual_queues[i];
            queue = virtual_queue->queue_info;
            if (queue == NULL) {
                continue;
            }

            port_name = hinic3_get_port_name_by_virtual_queue(ds, virtual_queue);
            if (port_name == NULL) {
                continue;
            }

            if (need_newline) {
                hinic3_ds_put_format(ds, "\n");
            }
            hinic3_dump_virtual_queue_table_item_to_screen(ds, port_name, queue);
            need_newline = true;
        }
    }

    return 0;
}

static int
hinic3_dump_virtual_upcall_queue_by_port(struct ds *ds, const char *port_name)
{
    int ret;
    uint16_t dpdk_index_id = 0;
    char netdev_name[HINIC3_NETDEV_NAME_MAX_LENGTH] = {0};
    struct hinic3_vf_dev *vf_dev = NULL;
    struct hinic3_queue *rxq = NULL;

    ret = hinic3_get_netdev_name(port_name, netdev_name);
    if (ret != 0) {
        hinic3_ds_put_format_prefix(ds, 0, HINIC3_UI_LEADING_SIGN_ERROR, HINIC3_UI_INVALID_PORT_NAME_STR "\n");
        return -EINVAL;
    }

    ret = rte_eth_dev_get_port_by_name(netdev_name, &dpdk_index_id);
    if (ret != 0) {
        hinic3_ds_put_format_prefix(ds, 0, HINIC3_UI_LEADING_SIGN_ERROR,
            HINIC3_UI_PORT_DUMP_UPCALL_QUEUE_PORT_ID_ERR_STR"\n");
        return -EINVAL;
    }

    vf_dev = (struct hinic3_vf_dev *)hinic3_get_private_data(dpdk_index_id);
    if (vf_dev == NULL) {
        hinic3_ds_put_format_prefix(ds, 0, HINIC3_UI_LEADING_SIGN_ERROR,
            HINIC3_UI_PORT_DUMP_UPCALL_QUEUE_VF_DEV_ERR_STR"\n");
        return -EINVAL;
    }

    for (uint8_t idx = 0; idx < MAX_RX_QUEUE_PER_VPORT; ++idx) {
        if (vf_dev->upcall_queue.is_queue_valid[idx] == false)
            continue;
        if (idx != 0)
            hinic3_ds_put_format_prefix(ds, INDENT_0, HINIC3_UI_EMPTY_STRING, "\n");
        rxq = &vf_dev->upcall_queue.rx_queues[idx];
        hinic3_dump_virtual_queue_table_item_to_screen(ds, port_name, rxq);
    }

    return 0;
}

static int
hinic3_dump_virtual_upcall_queue_by_name(struct ds *ds, const char *ring_name)
{
    struct hinic3_queue *rxq = NULL;
    struct hinic3_virtual_queue *virtual_rxq = NULL;
    char *port_name = NULL;

    virtual_rxq = hinic3_get_virtual_queue_by_ring(ring_name);
    if (virtual_rxq == NULL) {
        hinic3_ds_put_format_prefix(ds, 0, HINIC3_UI_LEADING_SIGN_ERROR,
            HINIC3_UI_PORT_DUMP_UPCALL_QUEUE_FIND_VIRTUAL_QUEUE_ERR_STR"\n", ring_name);
        return -EINVAL;
    }

    if (virtual_rxq->ring == NULL) {
        hinic3_ds_put_format_prefix(ds, 0, HINIC3_UI_LEADING_SIGN_INFO,
            HINIC3_UI_PORT_DUMP_UPCALL_QUEUE_VIRTUAL_QUEUE_DEACTIVE_ERR_STR"\n", ring_name);
        return 0;
    }

    port_name = hinic3_get_port_name_by_virtual_queue(ds, virtual_rxq);
    rxq = virtual_rxq->queue_info;
    hinic3_dump_virtual_queue_table_item_to_screen(ds, port_name, rxq);

    return 0;
}

static int
hinic3_dump_virtual_upcall_queue_by_physical_queue(struct ds *ds, const char *physical_queue_id_str)
{
    uint16_t physical_queue_id;
    char *endPtr = NULL;

    struct hinic3_queue *rxq = NULL;
    struct hinic3_virtual_queue *virtual_rxq = NULL;
    struct hinic3_physical_queue *physical_rxq = NULL;
    char *port_name = NULL;

    physical_queue_id = strtoul(physical_queue_id_str, &endPtr, STR_TO_DEC_NUM);
    if (endPtr == NULL || *endPtr != '\0') {
        hinic3_ds_put_format_prefix(ds, 0, HINIC3_UI_LEADING_SIGN_ERROR, HINIC3_UI_ERROR_WRONG_PARAMETER"\n");
        return -EINVAL;
    }

    physical_rxq = hinic3_get_physical_queue_by_index(physical_queue_id);
    if (physical_rxq == NULL) {
        hinic3_ds_put_format_prefix(ds, 0, HINIC3_UI_LEADING_SIGN_ERROR, HINIC3_UI_ERROR_WRONG_PARAMETER"\n");
        return -EINVAL;
    }

    if (physical_rxq->virtual_queue_valid_cnt == 0) {
        hinic3_ds_put_format_prefix(ds, 0, HINIC3_UI_LEADING_SIGN_INFO,
            HINIC3_UI_PORT_DUMP_UPCALL_QUEUE_PHYSICAL_QUEUE_DEACTIVE_ERR_STR"\n", physical_queue_id);
        return 0;
    }

    for (uint8_t index = 0; index < HINIC3_VIRTUAL_QUEUE_MULTIPLEX_MAX; ++index) {
        if (physical_rxq->is_virtual_queue_valids[index] == false)
            continue;
        if (index != 0)
            hinic3_ds_put_format_prefix(ds, INDENT_0, HINIC3_UI_EMPTY_STRING, "\n");
        virtual_rxq = physical_rxq->virtual_queues[index];
        port_name = hinic3_get_port_name_by_virtual_queue(ds, virtual_rxq);
        rxq = virtual_rxq->queue_info;
        if (port_name == NULL || rxq == NULL) {
            hinic3_ds_clear(ds);
            hinic3_ds_put_format_prefix(ds, 0, HINIC3_UI_LEADING_SIGN_FAILURE,
                HINIC3_UI_PORT_DUMP_UPCALL_QUEUE_PORT_NAME_ERR_STR"\n");
            return -EFAULT;
        }
        hinic3_dump_virtual_queue_table_item_to_screen(ds, port_name, rxq);
    }

    return 0;
}


static int
hinic3_dump_virtual_upcall_queue_help(struct ds *ds)
{
    hinic3_ds_put_format_prefix(ds, INDENT_2, HINIC3_UI_EMPTY_STRING, HINIC3_UI_PORT_DUMP_UPCALL_QUEUE_STRING"\n\n");
    hinic3_ds_put_format_prefix(ds, INDENT_2, HINIC3_UI_EMPTY_STRING, HINIC3_UI_FLOW_OPTION_LIST_STRING"\n");
    hinic3_ds_put_format_prefix(ds, INDENT_4, HINIC3_UI_EMPTY_STRING, "%-30s%-s\n",
        HINIC3_UI_FLOW_DUMP_HELP_FORMAT_STRING, HINIC3_UI_FLOW_DUMP_HELP_TIPS_STRING);
    hinic3_ds_put_format_prefix(ds, INDENT_4, HINIC3_UI_EMPTY_STRING, "%-30s%-s\n",
        HINIC3_UI_PORT_DUMP_UPCALL_QUEUE_ALL_FORMAT_STRING, HINIC3_UI_PORT_DUMP_UPCALL_QUEUE_ALL_TIPS_STRING);
    hinic3_ds_put_format_prefix(ds, INDENT_4, HINIC3_UI_EMPTY_STRING, "%-30s%-s\n",
        HINIC3_UI_PORT_DUMP_UPCALL_QUEUE_PORT_FORMAT_STRING, HINIC3_UI_PORT_DUMP_UPCALL_QUEUE_PORT_TIPS_STRING);
    hinic3_ds_put_format_prefix(ds, INDENT_4, HINIC3_UI_EMPTY_STRING, "%-30s%-s\n",
        HINIC3_UI_PORT_DUMP_UPCALL_QUEUE_VIRTUAL_FORMAT_STRING, HINIC3_UI_PORT_DUMP_UPCALL_QUEUE_VIRTUAL_TIPS_STRING);
    hinic3_ds_put_format_prefix(ds, INDENT_4, HINIC3_UI_EMPTY_STRING, "%-30s%-s\n",
        HINIC3_UI_PORT_DUMP_UPCALL_QUEUE_PHYSICAL_FORMAT_STRING, HINIC3_UI_PORT_DUMP_UPCALL_QUEUE_PHYSICAL_TIPS_STRING);

    return 0;
}

enum {
    HINIC3_HELP_QUEUE_OPT,
    HINIC3_ALL_QUEUE_OPT,
    HINIC3_PORT_QUEUE_OPT,
    HINIC3_VIRTUAL_QUEUE_OPT,
    HINIC3_PHYSICAL_QUEUE_OPT,
};

static const struct hinic3_opt upcall_queue_cmd_opts[] = {
    {"-h",        REQUIRED_ARGUMENT, 0, HINIC3_HELP_QUEUE_OPT},
    {"--help",    REQUIRED_ARGUMENT, 0, HINIC3_HELP_QUEUE_OPT},
    {"-a",        REQUIRED_ARGUMENT, 0, HINIC3_ALL_QUEUE_OPT},
    {"-all",      REQUIRED_ARGUMENT, 0, HINIC3_ALL_QUEUE_OPT},
    {"-p",        OPTIONAL_ARGUMENT, 0, HINIC3_PORT_QUEUE_OPT},
    {"-port",     OPTIONAL_ARGUMENT, 0, HINIC3_PORT_QUEUE_OPT},
    {"-v",        OPTIONAL_ARGUMENT, 0, HINIC3_VIRTUAL_QUEUE_OPT},
    {"-virtual",  OPTIONAL_ARGUMENT, 0, HINIC3_VIRTUAL_QUEUE_OPT},
    {"-f",        OPTIONAL_ARGUMENT, 0, HINIC3_PHYSICAL_QUEUE_OPT},
    {"-physical", OPTIONAL_ARGUMENT, 0, HINIC3_PHYSICAL_QUEUE_OPT},
};

static int
hinic3_check_option(struct ds *ds, int argc, const char *name)
{
    int option_val;
    size_t idx = 0;
    for (idx = 0; idx < sizeof(upcall_queue_cmd_opts) / sizeof(struct hinic3_opt); idx++) {
        if (strcmp(name, upcall_queue_cmd_opts[idx].name) == 0) {
            if (argc != upcall_queue_cmd_opts[idx].has_arg + 1) {
                hinic3_ds_put_format_prefix(ds, 0, HINIC3_UI_LEADING_SIGN_ERROR,
                    HINIC3_UI_FLOW_REQUIRES_WRONG_ARGUMENT_STRING, upcall_queue_cmd_opts[idx].has_arg);
                return -EINVAL;
            }
            option_val = upcall_queue_cmd_opts[idx].val;
            return option_val;
        }
    }
    hinic3_ds_put_format_prefix(ds, 0, HINIC3_UI_LEADING_SIGN_ERROR,
        HINIC3_UI_FLOW_REQUIRES_WRONG_ARGUMENT_STRING, OPTIONAL_ARGUMENT);
    return -EINVAL;
}

static int
hinic3_dump_normal_upcall_queues_info(struct ds *ds, int argc)
{
    if (argc != 1) {
        hinic3_ds_put_format_prefix(ds, 0, HINIC3_UI_LEADING_SIGN_ERROR, HINIC3_UI_ERROR_TOO_MANY_PARAMETER"\n");
        return -EINVAL;
    }

    struct hinic3_port_upcall_info upcall_info = { 0 };
    uint8_t share_upcall_num = hinic3_get_vf_share_upcall_num();
    uint16_t share_ref_cnt = hinic3_get_vf_share_upcall_ref_cnt();
    int ret = hinic3_port_mgmt_get_upcall_info(&upcall_info);
    if (ret != 0) {
        hinic3_ds_put_format_prefix(
            ds, 0, HINIC3_UI_LEADING_SIGN_FAILURE, "%s%d\n", HINIC3_UI_DFX_COMMAND_UPCALL_QUEUE_ERROR_STRING, ret);
        return -EINVAL;
    }

    hinic3_ds_put_format_prefix(
        ds, INDENT_2, HINIC3_UI_EMPTY_STRING, HINIC3_UI_DFX_COMMAND_UPCALL_INFO_HEADER_STRING "\n");
    hinic3_ds_put_format_prefix(ds, INDENT_4, HINIC3_UI_EMPTY_STRING,
        "%-35s%u\n", HINIC3_UI_DFX_COMMAND_MAX_UPCALL_QUEUE_STRING, upcall_info.total_upcall_qnum);
    hinic3_ds_put_format_prefix(ds, INDENT_4, HINIC3_UI_EMPTY_STRING,
        "%-35s%u\n", HINIC3_UI_DFX_COMMAND_AVAILIBLE_UPCALL_QUEUE_STRING, upcall_info.left_upcall_qnum);
    hinic3_ds_put_format_prefix(ds, INDENT_4, HINIC3_UI_EMPTY_STRING,
        "%-35s%u\n", HINIC3_UI_DFX_COMMAND_USED_UPCALL_QUEUE_STRING,
        upcall_info.total_upcall_qnum - upcall_info.left_upcall_qnum);
    if (hinic3_check_masked_to_exact_switch() == true) {
        hinic3_ds_put_format_prefix(ds, INDENT_4, HINIC3_UI_EMPTY_STRING,
            "%-35s%u %s%u\n", HINIC3_UI_DFX_COMMAND_SHARE_UPCALL_QUEUE_STRING, share_upcall_num,
            HINIC3_UI_DFX_COMMAND_REF_COUNT_STRING, share_ref_cnt);
    }
    return 0;
}

static int
hinic3_dump_virtual_upcall_queues_option(struct ds *ds, int argc, const char *argv[])
{
    int ret = -1;
    if (argc == 1) {
        ret = hinic3_dump_virtual_upcall_queues_info(ds);
        return ret;
    }
    int option_val = hinic3_check_option(ds, argc, argv[1]);

    switch (option_val) {
        case HINIC3_HELP_QUEUE_OPT:
            ret = hinic3_dump_virtual_upcall_queue_help(ds);
            break;

        case HINIC3_ALL_QUEUE_OPT:
            ret = hinic3_dump_virtual_upcall_queue_all(ds);
            break;

        case HINIC3_PORT_QUEUE_OPT:
            ret = hinic3_dump_virtual_upcall_queue_by_port(ds, argv[argc - 1]);
            break;

        case HINIC3_VIRTUAL_QUEUE_OPT:
            ret = hinic3_dump_virtual_upcall_queue_by_name(ds, argv[argc - 1]);
            break;

        case HINIC3_PHYSICAL_QUEUE_OPT:
            ret = hinic3_dump_virtual_upcall_queue_by_physical_queue(ds, argv[argc - 1]);
            break;

        default:
            hinic3_ds_put_format_prefix(ds, 0, HINIC3_UI_LEADING_SIGN_ERROR, HINIC3_UI_ERROR_WRONG_PARAMETER"\n");
            return -EINVAL;
    }

    return ret;
}

void
hinic3_dump_upcall_queues_info_cmd(struct unixctl_conn *conn, int argc, const char *argv[], void *aux)
{
    int ret;
    struct ds ds = DS_EMPTY_INITIALIZER;

    if (hinic3_get_virtual_queue_mode_enabled() == false) {
        ret = hinic3_dump_normal_upcall_queues_info(&ds, argc);
    } else {
        ret = hinic3_dump_virtual_upcall_queues_option(&ds, argc, argv);
    }

    if (ret != 0) {
        *(int *)aux = -EINVAL;
        hinic3_command_reply_error(conn, hinic3_ds_cstr(&ds));
        hinic3_ds_destroy(&ds);
        return;
    }

    *(int *)aux = 0;
    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
}

static void
hinic3_dump_ports_queue_info_help(struct ds *ds)
{
    hinic3_ds_put_format(ds,
        "%2sUsage: dpak-ovs-ctl hwoff/dump-ports-queue-info { -p <port-name> | { -h | --help } }\n\n",
        HINIC3_UI_INDENT_SPACE);
    hinic3_ds_put_format(ds, "%2sOptions list:\n", HINIC3_UI_INDENT_SPACE);
    hinic3_ds_put_format(ds, "%4s%-30sDump one port queue info\n", HINIC3_UI_INDENT_SPACE, "-p <port-name>");
    hinic3_ds_put_format(ds, "%4s%-30sShow help command\n", HINIC3_UI_INDENT_SPACE, "-h, --help");
}

enum {
    HINIC3_DUMP_PORT_QUEUE_HELP_OPT,
    HINIC3_DUMP_PORT_QUEUE_OPT,
};

static const struct hinic3_opt dump_queue_cmd_opts[] = {
    {"-h",        NO_ARGUMENT,       0, HINIC3_DUMP_PORT_QUEUE_HELP_OPT},
    {"--help",    NO_ARGUMENT,       0, HINIC3_DUMP_PORT_QUEUE_HELP_OPT},
    {"-p",        REQUIRED_ARGUMENT, 0, HINIC3_DUMP_PORT_QUEUE_OPT},
};

static int
hinic3_dump_queue_info_check_option(struct ds *ds, int argc, const char *name)
{
    enum {
        ARGC = 2
    };

    size_t idx = 0;
    for (idx = 0; idx < sizeof(dump_queue_cmd_opts) / sizeof(struct hinic3_opt); idx++) {
        if (strcmp(name, dump_queue_cmd_opts[idx].name) == 0) {
            if (argc != dump_queue_cmd_opts[idx].has_arg + ARGC) {
                hinic3_ds_put_format_prefix(ds, 0, HINIC3_UI_LEADING_SIGN_ERROR,
                    HINIC3_UI_FLOW_REQUIRES_WRONG_ARGUMENT_STRING, dump_queue_cmd_opts[idx].has_arg + 1);
                return -1;
            }
            return dump_queue_cmd_opts[idx].val;
        }
    }
    return -1;
}

static void
hinic3_dump_vf_queue_stats_to_screen(struct ds *output_msg, struct hinic3_vf_dev *vf_dev)
{
    struct hinic3_queue *rxq = NULL;
    struct hinic3_queue *txq = NULL;

    hinic3_ds_put_format(output_msg, HINIC3_DUMP_PORT_RX_QUEUE_INFO, HINIC3_UI_INDENT_SPACE);
    for (uint8_t idx = 0; idx < MAX_RX_QUEUE_PER_VPORT; ++idx) {
        if (vf_dev->upcall_queue.is_queue_valid[idx] == false)
            continue;
        if (idx != 0)
            hinic3_ds_put_format_prefix(output_msg, INDENT_2, HINIC3_UI_EMPTY_STRING, "\n");

        rxq = &vf_dev->upcall_queue.rx_queues[idx];
        hinic3_ds_put_format(output_msg, HINIC3_DUMP_PORT_QUEUE_ID, HINIC3_UI_INDENT_SPACE, rxq->queue_id);
        hinic3_ds_put_format(output_msg, HINIC3_DUMP_PORT_RX_QUEUE_PKTS, HINIC3_UI_INDENT_SPACE,
            rte_atomic64_read(&rxq->pkt_stats));
    }

    hinic3_ds_put_format(output_msg, "\n");
    hinic3_ds_put_format(output_msg, HINIC3_DUMP_PORT_TX_QUEUE_INFO, HINIC3_UI_INDENT_SPACE);
    for (uint8_t idx = 0; idx < MAX_TX_QUEUE_PER_VPORT; ++idx) {
        if (vf_dev->reinject_queue.is_queue_valid[idx] == false)
            continue;
        if (idx != 0)
            hinic3_ds_put_format_prefix(output_msg, INDENT_2, HINIC3_UI_EMPTY_STRING, "\n");

        txq = &vf_dev->reinject_queue.tx_queues[idx];
        hinic3_ds_put_format(output_msg, HINIC3_DUMP_PORT_QUEUE_ID, HINIC3_UI_INDENT_SPACE, txq->queue_id);
        hinic3_ds_put_format(output_msg, HINIC3_DUMP_PORT_TX_QUEUE_PKTS, HINIC3_UI_INDENT_SPACE,
            rte_atomic64_read(&txq->pkt_stats));
    }
}

static void
hinic3_dump_bond_queue_stats_to_screen(struct ds *output_msg, struct hinic3_bond_dev *bond_dev)
{
    struct hinic3_queue *rxq = NULL;
    struct hinic3_queue *txq = NULL;
    hinic3_ds_put_format(output_msg, HINIC3_DUMP_PORT_RX_QUEUE_INFO, HINIC3_UI_INDENT_SPACE);
    for (uint8_t idx = 0; idx < MAX_RX_QUEUE_PER_VPORT; ++idx) {
        if (bond_dev->upcall_queue.is_queue_valid[idx] == false)
            continue;
        if (idx != 0)
            hinic3_ds_put_format_prefix(output_msg, INDENT_2, HINIC3_UI_EMPTY_STRING, "\n");

        rxq = &bond_dev->upcall_queue.rx_queues[idx];
        hinic3_ds_put_format(output_msg, HINIC3_DUMP_PORT_QUEUE_ID, HINIC3_UI_INDENT_SPACE, rxq->queue_id);
        hinic3_ds_put_format(output_msg, HINIC3_DUMP_PORT_RX_QUEUE_PKTS, HINIC3_UI_INDENT_SPACE,
            rte_atomic64_read(&rxq->pkt_stats));
    }

    hinic3_ds_put_format(output_msg, "\n");
    hinic3_ds_put_format(output_msg, HINIC3_DUMP_PORT_TX_QUEUE_INFO, HINIC3_UI_INDENT_SPACE);
    for (uint8_t idx = 0; idx < MAX_TX_QUEUE_PER_VPORT; ++idx) {
        if (bond_dev->reinject_queue.is_queue_valid[idx] == false)
            continue;
        if (idx != 0)
            hinic3_ds_put_format_prefix(output_msg, INDENT_2, HINIC3_UI_EMPTY_STRING, "\n");

        txq = &bond_dev->reinject_queue.tx_queues[idx];
        hinic3_ds_put_format(output_msg, HINIC3_DUMP_PORT_QUEUE_ID, HINIC3_UI_INDENT_SPACE, txq->queue_id);
        hinic3_ds_put_format(output_msg, HINIC3_DUMP_PORT_TX_QUEUE_PKTS, HINIC3_UI_INDENT_SPACE,
            rte_atomic64_read(&txq->pkt_stats));
    }
}

static int
hinic3_show_vf_port_queue_stats_info(struct ds *output_msg, uint16_t dpdk_index_id, uint32_t link_status HINIC3_UNUSED)
{
    struct hinic3_vf_dev *vf_dev = NULL;
    vf_dev = (struct hinic3_vf_dev *)hinic3_get_private_data(dpdk_index_id);
    if (vf_dev == NULL) {
        hinic3_ds_put_format(output_msg, "%sThe current device's private data is NULL!\n", HINIC3_UI_LEADING_SIGN_ERROR);
        return -1;
    }

    hinic3_dump_vf_queue_stats_to_screen(output_msg, vf_dev);
    return 0;
}

static int
hinic3_show_pf_port_queue_stats_info(struct ds *output_msg, uint16_t dpdk_index_id, uint32_t link_status HINIC3_UNUSED)
{
    struct hinic3_bond_dev *bond_dev = NULL;
    bond_dev = (struct hinic3_bond_dev *)hinic3_get_private_data(dpdk_index_id);
    if (bond_dev == NULL) {
        hinic3_ds_put_format(output_msg, "%sThe current device's private data is NULL\n", HINIC3_UI_LEADING_SIGN_ERROR);
        return -1;
    }

    hinic3_dump_bond_queue_stats_to_screen(output_msg, bond_dev);
    return 0;
}

static int
hinic3_dump_ports_queue_info_sub(struct ds *output_msg, const char *port_name)
{
    int ret = 0;
    char netdev_name[HINIC3_NETDEV_NAME_MAX_LENGTH] = {0};
    uint16_t dpdk_index_id;
    uint16_t port_id;
    uint32_t link_status;
    uint32_t if_index;

    ret = hinic3_get_netdev_name(port_name, netdev_name);
    if (ret != 0) {
        hinic3_ds_put_format_prefix(output_msg, 0, HINIC3_UI_LEADING_SIGN_ERROR,
            HINIC3_UI_PORT_DUMP_QUEUE_NETDEV_ERR_STR "\n");
        return -1;
    }

    ret = rte_eth_dev_get_port_by_name(netdev_name, &dpdk_index_id);
    if (ret != 0) {
        hinic3_ds_put_format_prefix(output_msg, 0, HINIC3_UI_LEADING_SIGN_ERROR,
            HINIC3_UI_PORT_DUMP_QUEUE_PORT_ID_ERR_STR "\n", netdev_name);
        return -1;
    }

    ret = hinic3_get_port_ifindex(dpdk_index_id, &if_index);
    if (ret != 0 || if_index > UINT16_MAX) {
        hinic3_ds_put_format_prefix(output_msg, 0, HINIC3_UI_LEADING_SIGN_ERROR,
            HINIC3_UI_PORT_DUMP_QUEUE_DEVICE_IFINDEX_ERR_STR "\n", netdev_name);
        return -1;
    }
    port_id = (uint16_t)if_index;

    if (is_hinic3_vf_dev(dpdk_index_id) == true) {
        ret = hinic3_port_mgmt_get_usage_state(port_id, &link_status);
        if (ret != 0) {
            hinic3_ds_put_format_prefix(output_msg, 0, HINIC3_UI_LEADING_SIGN_ERROR, HINIC3_UI_PORT_GET_VF_LINK_ERR "\n",
                ret);
            return ret;
        }

        ret = hinic3_show_vf_port_queue_stats_info(output_msg, dpdk_index_id, link_status);
    } else {
        ret = hinic3_get_bond_link_from_netdev(output_msg, netdev_name, &link_status);
        if (ret != 0) {
            hinic3_ds_put_format_prefix(output_msg, 0, HINIC3_UI_LEADING_SIGN_ERROR,
                HINIC3_DUMP_PORT_GET_BOND_LINK_ERR "\n", ret);
            return ret;
        }
        ret = hinic3_show_pf_port_queue_stats_info(output_msg, dpdk_index_id, link_status);
    }
    return ret;
}

static int
hinic3_dump_ports_queue_info_options(struct ds *ds, int argc, const char *argv[])
{
    int ret = 0;
    int option_val = hinic3_dump_queue_info_check_option(ds, argc, argv[1]);
    switch (option_val) {
        case HINIC3_DUMP_PORT_QUEUE_HELP_OPT:
            hinic3_dump_ports_queue_info_help(ds);
            break;

        case HINIC3_DUMP_PORT_QUEUE_OPT:
            ret = hinic3_dump_ports_queue_info_sub(ds, argv[argc - 1]);
            break;

        default:
            hinic3_ds_put_format_prefix(ds, 0, HINIC3_UI_LEADING_SIGN_ERROR, HINIC3_UI_ERROR_WRONG_PARAMETER "\n");
            return -1;
    }

    return ret;
}

void
hinic3_dump_ports_queue_info_command(struct unixctl_conn *conn, int argc, const char *argv[], void *aux)
{
    int ret;
    struct ds ds = DS_EMPTY_INITIALIZER;

    ret = hinic3_dump_ports_queue_info_options(&ds, argc, argv);
    if (ret != 0) {
        *(int *)aux = -1;
        hinic3_command_reply_error(conn, hinic3_ds_cstr(&ds));
        hinic3_ds_destroy(&ds);
        return;
    }

    *(int *)aux = 0;
    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
}
