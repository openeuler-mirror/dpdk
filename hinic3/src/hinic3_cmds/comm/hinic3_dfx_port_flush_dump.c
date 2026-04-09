/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2024 Huawei Technologies Co., Ltd
 */

#include "rte_ethdev.h"
#include "hinic3_ui_string.h"
#include "hinic3_meminfo.h"
#include "hinic3_command.h"
#include "hinic3_ds.h"
#include <rte_atomic.h>
#include "hinic3_iface_global.h"
#include "hinic3_vf_port_qos_public.h"
#include "hinic3_bond_controller.h"
#include "hinic3_agent_cmd_format.h"
#include "hinic3_dfx_port.h"

#define HINIC3_LINK_STATUS_STR_LEN     10
#define HINIC3_LINK_STATUS_NUM         2
#define HINIC3_SHARE_UPCALL_STATUS_NUM 2

#define RTE_ETH_LINK_UP_STR      "up"
#define RTE_ETH_LINK_DOWN_STR    "down"

typedef enum {
    COMMAND_TYPE_OF_DUMP,
    COMMAND_TYPE_OF_FLUSH,
} hinic3_command_type;

struct hinic3_link_status {
    uint32_t link_status;
    char link_status_str[HINIC3_LINK_STATUS_STR_LEN];
};

static struct hinic3_link_status g_hinic3_link_status[HINIC3_LINK_STATUS_NUM] = {
    {RTE_ETH_LINK_DOWN, RTE_ETH_LINK_DOWN_STR},
    {RTE_ETH_LINK_UP, RTE_ETH_LINK_UP_STR},
};

static const char *g_hinic3_share_upcall_status[HINIC3_SHARE_UPCALL_STATUS_NUM] = {
    "false",
    "true"
};

static int
hinic3_get_rx_qos_drop_num(struct hinic3_vf_dev *vf_dev, uint32_t *drop_num)
{
    int ret;
    struct hovs_qos_stats_batch_all qos_stats = {0};

    ret = hinic3_vf_qos_statistics_get_all_batch(&vf_dev->qos_id, &qos_stats, 1);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "Failed to invoke the driver qos_statistics interface.");
        return -1;
    }
     *drop_num = qos_stats.drop_pkg_num.rx_drop_num;
    return 0;
}

static int
hinic3_show_vf_port_info(struct ds *output_msg, uint16_t dpdk_index_id, hinic3_port_stats *port_stats,
    uint32_t link_status)
{
    int ret;
    void *private_data = NULL;
    struct hinic3_vf_dev *vf_dev = NULL;
    uint32_t qos_drop_num;

    private_data = hinic3_get_private_data(dpdk_index_id);
    if (private_data == NULL) {
        hinic3_ds_put_format(output_msg, "%sThe current device's private data is NULL\n", HINIC3_UI_LEADING_SIGN_ERROR);
        return -1;
    }
    vf_dev = (struct hinic3_vf_dev *)private_data;

    ret = hinic3_get_rx_qos_drop_num(vf_dev, &qos_drop_num);
    if (ret != 0) {
        hinic3_ds_put_format(output_msg, "%shwoff/dump-ports %u failed, get qos dropped num error\n",
            HINIC3_UI_LEADING_SIGN_ERROR, vf_dev->vport_id);
        return -1;
    }
    port_stats->rx_qos_dropped = qos_drop_num;

    if ((link_status & HW_ETH_DEVICE_STATUS) != 0) {
        link_status = RTE_ETH_LINK_UP;
    } else {
        link_status = RTE_ETH_LINK_DOWN;
    }

    hinic3_ds_put_format(output_msg, "\t%-30s : %s\n", "link-status",
        g_hinic3_link_status[link_status].link_status_str);
    hinic3_ds_put_format(output_msg, "\t%-30s : %s\n", "used_share_upcall",
        g_hinic3_share_upcall_status[vf_dev->share_upcall]);
    hinic3_ds_put_format(output_msg, "\t%-30s : %lld\n", "upcall-pkt-num",
        rte_atomic64_read(&vf_dev->vf_upcall_pk_num));
    hinic3_ds_put_format(output_msg, "\t%-30s : %lld\n", "reinject-pkt-num",
        rte_atomic64_read(&vf_dev->vf_reinject_pk_num));
    return 0;
}

static int
hinic3_show_bond_port_info(struct ds *output_msg, uint16_t dpdk_index_id,
    hinic3_port_stats *port_stats HINIC3_UNUSED, uint32_t link_status)
{
    int ret;
    void *private_data = NULL;
    struct hinic3_bond_dev *pf_dev = NULL;
    struct hovs_bond_slave_stats bond_slave_info = { 0 };

    private_data = hinic3_get_private_data(dpdk_index_id);
    if (private_data == NULL) {
        hinic3_ds_put_format(output_msg, "%sThe current device's private data is NULL\n", HINIC3_UI_LEADING_SIGN_ERROR);
        return -1;
    }
    pf_dev = (struct hinic3_bond_dev *)private_data;

    ret = hinic3_port_mgmt_get_bond_slave_info(pf_dev->vport_id, &bond_slave_info);
    if (ret != 0) {
        hinic3_ds_put_format(output_msg, "%scan't dump bond slave info, errno = %d\n", HINIC3_UI_LEADING_SIGN_ERROR, ret);
        return -1;
    }

    for (uint8_t index = 1; index < HINIC3_BOND_SLAVE_NUM; index++) {
        bond_slave_info.rx_pkts[0] += bond_slave_info.rx_pkts[index];
        bond_slave_info.tx_pkts[0] += bond_slave_info.tx_pkts[index];
        bond_slave_info.rx_bytes[0] += bond_slave_info.rx_bytes[index];
        bond_slave_info.tx_bytes[0] += bond_slave_info.tx_bytes[index];
    }

    hinic3_ds_put_format(output_msg, "\t%-30s : %s\n", "link-status",
        g_hinic3_link_status[link_status].link_status_str);
    hinic3_ds_put_format(output_msg, "\t%-30s : %lld\n", "upcall-pkt-num",
        rte_atomic64_read(&pf_dev->bond_upcall_pk_num));
    hinic3_ds_put_format(output_msg, "\t%-30s : %lld\n", "reinject-pkt-num",
        rte_atomic64_read(&pf_dev->bond_reinject_pk_num));

    hinic3_ds_put_format(output_msg, "\t%-30s : %llu\n", "rx-pkts(net)", bond_slave_info.rx_pkts[0]);
    hinic3_ds_put_format(output_msg, "\t%-30s : %llu\n", "tx-pkts(net)", bond_slave_info.tx_pkts[0]);
    hinic3_ds_put_format(output_msg, "\t%-30s : %llu\n", "rx-bytes(net)", bond_slave_info.rx_bytes[0]);
    hinic3_ds_put_format(output_msg, "\t%-30s : %llu\n", "tx-bytes(net)", bond_slave_info.tx_bytes[0]);

    return 0;
}

static void
hinic3_show_help_str(struct ds *output_msg, hinic3_command_type type)
{
    const char *command_str = NULL;

    if (type == COMMAND_TYPE_OF_DUMP) {
        hinic3_ds_put_format(output_msg,
            "%2sUsage: dpak-ovs-ctl hwoff/dump-ports { <port-name> | global | { -h | --help } }\n\n",
            HINIC3_UI_INDENT_SPACE);
        command_str = "Dump";
    } else {
        hinic3_ds_put_format(output_msg,
            "%2sUsage: dpak-ovs-ctl hwoff/flush-ports { <port-name> | global | { -h | --help } }\n\n",
            HINIC3_UI_INDENT_SPACE);
        command_str = "Flush";
    }

    hinic3_ds_put_format(output_msg, "%2sOptions list:\n", HINIC3_UI_INDENT_SPACE);
    hinic3_ds_put_format(output_msg, "%4s%-30s%s the stats of a port\n",
        HINIC3_UI_INDENT_SPACE, "<port-name>", command_str);
    hinic3_ds_put_format(output_msg, "%4s%-30s%s the chip global stats\n", HINIC3_UI_INDENT_SPACE, "global", command_str);
    hinic3_ds_put_format(output_msg, "%4s%-30sDisplay the help information\n", HINIC3_UI_INDENT_SPACE, "-h, --help");
}

int
hinic3_get_bond_link_from_netdev(struct ds *output_msg, const char *netdev_name, uint32_t *link_status)
{
    int ret;
    const char *bond_name = NULL;

    if (strlen(HINIC3_ETH_VDEV_DRV_NAME) >= strlen(netdev_name)) {
        hinic3_ds_put_format(output_msg,  "%sPort mgmt get bond name err.\n", HINIC3_UI_LEADING_SIGN_ERROR);
        return -1;
    }
    bond_name = netdev_name + strlen(HINIC3_ETH_VDEV_DRV_NAME);
    ret = hinic3_get_bond_link_status(bond_name, link_status);
    if (ret != 0) {
        hinic3_ds_put_format(output_msg,  "%sPort mgmt get bond state err, ret is %d.\n",
            HINIC3_UI_LEADING_SIGN_ERROR, ret);
        return ret;
    }
    return ret;
}

static int
hinic3_dump_port_status(struct ds *output_msg, const char *netdev_name)
{
    int ret = 0;
    uint16_t dpdk_index_id;
    uint16_t port_id;
    uint32_t link_status;
    hinic3_port_stats port_stats = {0};
    uint32_t if_index;

    ret = rte_eth_dev_get_port_by_name(netdev_name, &dpdk_index_id);
    if (ret != 0) {
        hinic3_ds_put_format(output_msg, "%sWrong parameter, get port id error, wrong port name '%s'!!\n",
            HINIC3_UI_LEADING_SIGN_ERROR, netdev_name);
        return -1;
    }
    ret = hinic3_get_port_ifindex(dpdk_index_id, &if_index);
    if (ret != 0 || if_index > UINT16_MAX) {
        hinic3_ds_put_format(output_msg, "hwoff/dump-ports %s failed, device ifindex error \n", netdev_name);
        return -1;
    } else {
        port_id = (uint16_t)if_index;
    }
    ret = hinic3_port_statistics_get(port_id, &port_stats);
    if (ret != 0) {
        hinic3_ds_put_format(output_msg, "hwoff/dump-ports %s failed with error %d \n", netdev_name, ret);
        return -1;
    }

    hinic3_ds_put_format(output_msg, "\t%-30s : %hu\n", "vport_id", port_id);

    if (is_hinic3_vf_dev(dpdk_index_id) == true) {
        ret = hinic3_port_mgmt_get_usage_state(port_id, &link_status);
        if (ret != 0) {
            hinic3_ds_put_format(output_msg,  "%sPort mgmt get vf link state err, ret is %d.\n",
                HINIC3_UI_LEADING_SIGN_ERROR, ret);
            return ret;
        }

        ret = hinic3_show_vf_port_info(output_msg, dpdk_index_id, &port_stats, link_status);
    } else {
        ret = hinic3_get_bond_link_from_netdev(output_msg, netdev_name, &link_status);
        if (ret != 0) {
            hinic3_ds_put_format(output_msg,  "%sPort mgmt get bond link state err, ret is %d.\n",
                HINIC3_UI_LEADING_SIGN_ERROR, ret);
            return ret;
        }
        ret = hinic3_show_bond_port_info(output_msg, dpdk_index_id, &port_stats, link_status);
    }

    hinic3_show_base_port_stats(output_msg, &port_stats);
    hinic3_show_check_port_stats(output_msg, &port_stats);
    hinic3_show_hiovs_port_stats(output_msg, &port_stats);
    hinic3_show_upcall_port_stats(output_msg, &port_stats);
    return ret;
}

static int
hinic3_dump_chip_global_stats(struct ds *output_msg)
{
    int error;
    struct hinic3_global_stats chip_global_stats = {0};

    error = hinic3_global_statistics_get(&chip_global_stats);
    if (error != 0) {
        hinic3_ds_put_format(output_msg, "%sDump ports global failed with error :%d.\n",
            HINIC3_UI_LEADING_SIGN_FAILURE, error);
        return -1;
    }
    hinic3_ds_put_format(output_msg, "global statistics:\n");
    hinic3_ds_put_format(output_msg, "\t%-30s : %u\n", "g-offload-flow-num", chip_global_stats.offload_flow_num);
    hinic3_ds_put_format(output_msg, "\t%-30s : %u\n", "g-offload-qpc-num", chip_global_stats.offload_qpc_num);
    hinic3_ds_put_format(output_msg, "\t%-30s : %u\n", "g-same-flow_multi-gpa",
        chip_global_stats.same_flow_with_multi_gpa);
    hinic3_ds_put_format(output_msg, "\t%-30s : %u\n", "g-vxlan-rx-vtep-miss", chip_global_stats.vxlan_rx_vtep_miss);
    hinic3_ds_put_format(output_msg, "\t%-30s : %u\n", "g-upcall-pf0-invalid-count",
        chip_global_stats.upcall_pf0_invalid_count);
    hinic3_ds_put_format(output_msg, "\t%-30s : %u\n", "g-upcall-pf0-fail-drop",
        chip_global_stats.upcall_pf0_rx_wqe_fail_dropped);
    hinic3_ds_put_format(output_msg, "\t%-30s : %u\n", "g-upcall-pf1-invalid-count",
        chip_global_stats.upcall_pf1_invalid_count);
    hinic3_ds_put_format(output_msg, "\t%-30s : %u\n", "g-upcall-pf1-fail-drop",
        chip_global_stats.upcall_pf1_rx_wqe_fail_dropped);
    hinic3_ds_put_format(output_msg, "\t%-30s : %lu\n", "g-upcall-limit-drop-total",
        chip_global_stats.upcall_limit_drop_total);
    hinic3_ds_put_format(output_msg, "\t%-30s : %lu\n", "g-upcall-from-vf-total",
        chip_global_stats.upcall_from_vf_total);
    hinic3_ds_put_format(output_msg, "\t%-30s : %lu\n", "g-upcall-from-hwbond-total",
        chip_global_stats.upcall_from_hwbond_total);
    hinic3_ds_put_format(output_msg, "\t%-30s : %lu\n", "g-flow-default-upcall", chip_global_stats.flow_default_upcall);
    hinic3_ds_put_format(output_msg, "\t%-30s : %lu\n", "g-flow-miss-upcall", chip_global_stats.flow_miss_upcall);
    hinic3_ds_put_format(output_msg, "\t%-30s : %lu\n", "g-flow-invalid-upcall", chip_global_stats.flow_invalid_upcall);
    hinic3_ds_put_format(output_msg, "\t%-30s : %lu\n", "g-qu-prefetch-stats-fail",
        chip_global_stats.qu_prefetch_statics_fail);
    return 0;
}

static int
hinic3_dump_status(struct ds *output_msg, char *netdev_name, bool is_global)
{
    int ret;
    if (is_global) {
        ret = hinic3_dump_chip_global_stats(output_msg);
    } else {
        ret = hinic3_dump_port_status(output_msg, netdev_name);
    }
    return ret;
}

void
hinic3_dump_ports_stats_command(struct unixctl_conn *conn, int argc, const char *argv[], void *aux)
{
    bool is_global = false;
    char netdev_name[HINIC3_NETDEV_NAME_MAX_LENGTH] = {0};
    struct ds ds = DS_EMPTY_INITIALIZER;
    const hinic3_command_type command_type = COMMAND_TYPE_OF_DUMP;

    if (argc > 1 && ((strcmp("-h", argv[1]) == 0) || (strcmp("--help", argv[1]) == 0))) {
        hinic3_show_help_str(&ds, command_type);
        *(int *)aux = 0;
    } else if (hinic3_dfx_port_process_args(argc, argv, netdev_name, &is_global) == true) {
        *(int *)aux = hinic3_dump_status(&ds, netdev_name, is_global);
    } else {
        *(int *)aux = -1;
        hinic3_ds_put_format(&ds, "%s%s, please input -h or --help to get help info.\n",
            HINIC3_UI_LEADING_SIGN_ERROR, HINIC3_UI_ERROR_WRONG_PARAMETER);
    }
    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
}

static int
hinic3_flush_chip_global_stats(struct ds *output_msg)
{
    HINIC3_LOG(INFO, AGENT, "flush global statistics");
    int err = hinic3_global_statistics_flush();
    if (err != 0) {
        hinic3_ds_put_format(output_msg, "%sFlush global stat failed, error is %d.\n",
            HINIC3_UI_LEADING_SIGN_FAILURE, err);
        return -1;
    }
    hinic3_ds_put_format(output_msg, "%sFlush global stat successfully.\n", HINIC3_UI_LEADING_SIGN_INFO);
    return 0;
}

static int
hinic3_flush_rx_qos_drop_num(struct hinic3_vf_dev *vf_dev)
{
    int ret;

    ret = hinic3_vf_qos_statistics_clear_batch(&vf_dev->qos_id, 1);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "Failed to invoke the driver qos_statistics interface.");
        return -1;
    }
    return 0;
}

static void
hinic3_flush_vf_queue_info(struct hinic3_vf_dev *vf_dev)
{
    for (uint8_t idx = 0; idx < MAX_RX_QUEUE_PER_VPORT; ++idx) {
        if (vf_dev->upcall_queue.is_queue_valid[idx] == false)
            continue;
        rte_atomic64_set(&vf_dev->upcall_queue.rx_queues[idx].pkt_stats, 0);
    }

    for (uint8_t idx = 0; idx < MAX_TX_QUEUE_PER_VPORT; ++idx) {
        if (vf_dev->reinject_queue.is_queue_valid[idx] == false)
            continue;
        rte_atomic64_set(&vf_dev->reinject_queue.tx_queues[idx].pkt_stats, 0);
    }
    return;
}

static void
hinic3_flush_bond_queue_info(struct hinic3_bond_dev *bond_dev)
{
    for (uint8_t idx = 0; idx < MAX_RX_QUEUE_PER_VPORT; ++idx) {
        if (bond_dev->upcall_queue.is_queue_valid[idx] == false)
            continue;
        rte_atomic64_set(&bond_dev->upcall_queue.rx_queues[idx].pkt_stats, 0);
    }

    for (uint8_t idx = 0; idx < MAX_TX_QUEUE_PER_VPORT; ++idx) {
        if (bond_dev->reinject_queue.is_queue_valid[idx] == false)
            continue;
        rte_atomic64_set(&bond_dev->reinject_queue.tx_queues[idx].pkt_stats, 0);
    }
    return;
}

static void
hinic3_flush_vf_port_info(struct ds *output_msg, uint16_t dpdk_index_id)
{
    int ret;
    void *private_data = NULL;
    struct hinic3_vf_dev *vf_dev = NULL;

    private_data = hinic3_get_private_data(dpdk_index_id);
    if (private_data == NULL) {
        hinic3_ds_put_format(output_msg, "%sThe current device's private data is NULL.\n",
            HINIC3_UI_LEADING_SIGN_FAILURE);
        return;
    }
    vf_dev = (struct hinic3_vf_dev *)private_data;
    ret = hinic3_flush_rx_qos_drop_num(vf_dev);
    if (ret != 0) {
        hinic3_ds_put_format(output_msg, "%sFlush ports %u failed, flush qos dropped num error.\n",
            HINIC3_UI_LEADING_SIGN_FAILURE, vf_dev->vport_id);
        return;
    }
    hinic3_flush_vf_queue_info(vf_dev);
    rte_atomic64_set(&vf_dev->vf_upcall_pk_num, 0);
    rte_atomic64_set(&vf_dev->vf_upcall_pk_byt, 0);
    rte_atomic64_set(&vf_dev->vf_reinject_pk_num, 0);
    rte_atomic64_set(&vf_dev->vf_reinject_pk_byt, 0);

    return;
}

static void
hinic3_flush_pf_port_info(struct ds *output_msg, uint16_t dpdk_index_id)
{
    void *private_data = NULL;
    struct hinic3_bond_dev *pf_dev = NULL;

    private_data = hinic3_get_private_data(dpdk_index_id);
    if (private_data == NULL) {
        hinic3_ds_put_format_prefix(output_msg, 0, HINIC3_UI_LEADING_SIGN_FAILURE, 
            "The current device's private data is NULL.\n");
        return;
    }
    pf_dev = (struct hinic3_bond_dev *)private_data;

    hinic3_flush_bond_queue_info(pf_dev);
    rte_atomic64_set(&pf_dev->bond_upcall_pk_num, 0);
    rte_atomic64_set(&pf_dev->bond_upcall_pk_byt, 0);
    rte_atomic64_set(&pf_dev->bond_reinject_pk_num, 0);
    rte_atomic64_set(&pf_dev->bond_reinject_pk_byt, 0);
}

static int
hinic3_flush_port_stats(struct ds *output_msg, char *netdev_name)
{
    int ret;
    uint16_t dpdk_index_id;
    uint16_t port_id;
    uint32_t if_index;

    ret = rte_eth_dev_get_port_by_name(netdev_name, &dpdk_index_id);
    if (ret != 0) {
        hinic3_ds_put_format(output_msg, "%sWrong parameter, get port id error, wrong port name '%s'!\n",
            HINIC3_UI_LEADING_SIGN_ERROR, netdev_name);
        return -1;
    }
    ret = hinic3_get_port_ifindex(dpdk_index_id, &if_index);
    if (ret != 0 || if_index > UINT16_MAX) {
        hinic3_ds_put_format(output_msg, "%shwoff/dump-ports %s failed, device ifindex error. \n",
            HINIC3_UI_LEADING_SIGN_ERROR, netdev_name);
        return -1;
    } else {
        port_id = (uint16_t)if_index;
    }
    ret = hinic3_port_statistics_flush(port_id);
    if (ret != 0) {
        hinic3_ds_put_format(output_msg, "%shwoff/flush-ports %s failed with error %d. \n",
            HINIC3_UI_LEADING_SIGN_FAILURE, netdev_name, ret);
        return -1;
    }

    if (is_hinic3_vf_dev(dpdk_index_id) == true) {
        hinic3_flush_vf_port_info(output_msg, dpdk_index_id);
    } else {
        hinic3_flush_pf_port_info(output_msg, dpdk_index_id);
    }
    hinic3_ds_put_format(output_msg, "%sFlush %s stats successfully.\n", HINIC3_UI_LEADING_SIGN_INFO, netdev_name);
    return 0;
}

static int
hinic3_flush_stats(struct ds *output_msg, char *netdev_name, bool is_global)
{
    int ret;
    if (is_global) {
        ret = hinic3_flush_chip_global_stats(output_msg);
    } else {
        ret = hinic3_flush_port_stats(output_msg, netdev_name);
    }
    return ret;
}

void
hinic3_flush_ports_stats_command(struct unixctl_conn *conn, int argc, const char *argv[], void *aux)
{
    bool is_global = false;
    char netdev_name[HINIC3_NETDEV_NAME_MAX_LENGTH] = {0};
    struct ds ds = DS_EMPTY_INITIALIZER;
    const hinic3_command_type command_type = COMMAND_TYPE_OF_FLUSH;

    if (argc > 1 && ((strcmp("-h", argv[1]) == 0) || (strcmp("--help", argv[1]) == 0))) {
        hinic3_show_help_str(&ds, command_type);
        *(int *)aux = 0;
    } else if (hinic3_dfx_port_process_args(argc, argv, netdev_name, &is_global) == true) {
        *(int *)aux = hinic3_flush_stats(&ds, netdev_name, is_global);
    } else {
        hinic3_ds_put_format(&ds, "%s%s, please input -h or --help to get help info.\n",
            HINIC3_UI_LEADING_SIGN_ERROR, HINIC3_UI_ERROR_WRONG_PARAMETER);
        *(int *)aux = -1;
    }
    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
}
