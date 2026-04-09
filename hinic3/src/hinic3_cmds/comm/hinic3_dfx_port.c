/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2024 Huawei Technologies Co., Ltd
 */

#include "rte_ethdev.h"
#include "hinic3_ui_string.h"
#include "hinic3_log.h"
#include "hinic3_port_util.h"
#include "hinic3_queue.h"
#include "hinic3_vf_controller.h"
#include "hinic3_offload_flow_port.h"
#include "hinic3_dfx_port.h"

static struct port_status g_port_status[] = {
    {HWPT_FLAVOR_REFCNT_UNUSED,  "unused"},
    {HWPT_FLAVOR_REFCNT_USED,    "used"},
    {HWPT_FLAVOR_REFCNT_INVALID, "invalid"},
};

bool
hinic3_dfx_port_process_args(int argc, const char *argv[], char *netdev_name, bool *is_global)
{
    const int port_name_arg_idx = 2;
    int ret;

    if (argc != port_name_arg_idx)
        return false;

    *is_global = (strcmp("global", argv[port_name_arg_idx - 1]) == 0);
    if (*is_global)
        return true;

    ret = hinic3_get_netdev_name(argv[port_name_arg_idx - 1], netdev_name);
    if (ret != 0)
        return false;

    return true;
}

static void
hinic3_port_virtio_queue_show(struct ds *ds, struct port_stats *vf_stats, struct port_stats *pf_stats)
{
    hinic3_ds_put_format(ds, "%2sTotal statistics are as follows:\n", HINIC3_UI_INDENT_SPACE);
    hinic3_ds_put_format(ds, "%4s%-16s\n%6stotal:       %u\n%6sused now:    %u\n%6sinvalid now: %u\n",
        HINIC3_UI_INDENT_SPACE, "vf num:", HINIC3_UI_INDENT_SPACE,
        vf_stats->total_num, HINIC3_UI_INDENT_SPACE, vf_stats->used_num, HINIC3_UI_INDENT_SPACE, vf_stats->invalid_num);
    hinic3_ds_put_format(ds, "%4s%-16s\n%6stotal:       %u\n%6sused now:    %u\n%6sinvalid now: %u\n",
        HINIC3_UI_INDENT_SPACE, "pf num:", HINIC3_UI_INDENT_SPACE,
        pf_stats->total_num, HINIC3_UI_INDENT_SPACE, pf_stats->used_num, HINIC3_UI_INDENT_SPACE, pf_stats->invalid_num);
    hinic3_ds_put_format(ds, "%4s%-16s \n%6stotal:       %u\n%6sused now:    %u\n", HINIC3_UI_INDENT_SPACE,
        "virtio queue:", HINIC3_UI_INDENT_SPACE,
        vf_stats->virtio_total_num, HINIC3_UI_INDENT_SPACE, vf_stats->virtio_used_num);
    hinic3_ds_put_format(ds, "%2sNote: Each VF/PF occupies an extra Virtio control queue.\n", HINIC3_UI_INDENT_SPACE);
    return;
}

static const char*
hinic3_get_port_name_by_vport_id(uint16_t vport_id)
{
    int ret;
    uint16_t port_id;
    const char *port_name = NULL;
    struct hinic3_vf_dev *vf_dev = NULL;
    ret = hinic3_get_port_id_by_ifindex(vport_id, &port_id);
    if (ret != 0)
        goto err;
    vf_dev = (struct hinic3_vf_dev *)hinic3_get_private_data(port_id);
    if (vf_dev == NULL)
        goto err;
    port_name = vf_dev->dev->device->name;
    if (port_name == NULL)
        goto err;
    return port_name;
err:
    return "unknown";
}

static void
hinic3_put_format_dev_flavor(struct ds *ds, struct hinic3_dev_node *iter, const char* type)
{
    int id;
    const char* status = NULL;
    char pci_addr_buf[HWPT_PCI_ADDR_LEN_MAX] = {0};
    uint16_t vport_id;
    const char* port_name = NULL;
    const char* pci_header = NULL;
    bool real_pci = false;
    id = iter->phy_dev.function_id;
    if (id == -1) {
        HINIC3_LOG(ERR, VPORT, "function id fetch failed.");
        hinic3_ds_put_format(ds, HINIC3_UI_LEADING_SIGN_FAILURE "Function ID fetch failed.\n");
        return;
    }
    if (pci_addr_format(&iter->phy_dev.pci_addr, pci_addr_buf, HWPT_PCI_ADDR_LEN_MAX) != 0) {
        HINIC3_LOG(ERR, VPORT, "pci addr buf parse failed.");
        hinic3_ds_put_format(ds, HINIC3_UI_LEADING_SIGN_FAILURE "Pci addr buf parse failed.\n");
        return;
    }
    vport_id = hinic3_function_id_to_vport_id(id);;
    status = g_port_status[iter->phy_dev.refcnt].str;
    port_name = hinic3_get_port_name_by_vport_id(vport_id);

    real_pci = (hinic3_query_bdf_type_get() == QUERY_BDF_TYPE_REAL);
    if (real_pci == true) {
        pci_header = "host-pci-addr:";
    } else {
        pci_header = "fake-pci-addr:";
    }
    hinic3_ds_put_format(ds, "%2sfunction-id:   %d\n%2s%-15s"
            "%s\n%2stype:          %s\n%2sstatus:        %s\n%2sport-name:     %s\n", HINIC3_UI_INDENT_SPACE,
            id, HINIC3_UI_INDENT_SPACE, pci_header, pci_addr_buf, HINIC3_UI_INDENT_SPACE, type,
            HINIC3_UI_INDENT_SPACE, status, HINIC3_UI_INDENT_SPACE, port_name);
    hinic3_ds_put_format(ds, "\n");
}

static void
hinic3_function_flavor_show(struct ds *ds)
{
    struct dev_mgmt *dev_list = get_netdev_hwpt_flavor();
    struct hinic3_dev_node *iter = NULL;
    struct hinic3_list *list = NULL;
    if (dev_list == NULL) {
        HINIC3_LOG(ERR, VPORT, "Failed to get dev_list");
        return;
    }

    list = &dev_list->pf_list;
    if (hinic3_list_is_empty(list) == false) {
        LIST_FOR_EACH(iter, node, list) {
            hinic3_put_format_dev_flavor(ds, iter, HWPT_HWTYPE_PF_STR);
        }
    }

    list = &dev_list->vf_list;
    if (hinic3_list_is_empty(list) == false) {
        LIST_FOR_EACH(iter, node, list) {
            hinic3_put_format_dev_flavor(ds, iter, HWPT_HWTYPE_VF_STR);
        }
    }
    return;
}

static void
hinic3_vf_flavor_show(struct unixctl_conn *conn, int argc HINIC3_UNUSED,
    const char *argv[] HINIC3_UNUSED, void *aux)
{
    int ret;
    struct ds ds = DS_EMPTY_INITIALIZER;

    if (hinic3_device_mode_get() == DPU_MODE && hinic3_get_port_list_init() == false) {
        ret = hinic3_dpu_vf_flavor_add();
        if (ret != 0) {
            hinic3_command_reply_error(conn, HINIC3_UI_LEADING_SIGN_ERROR "hinic3_dpu_vf_flavor_add err\n");
            return;
        }
        hinic3_set_port_list_init(false);
    }

    hinic3_function_flavor_show(&ds);

    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
    *(int *)aux = 0;
    return;
}

static int
hinic3_get_port_stats(struct port_stats *vf_stats, struct port_stats *pf_stats, bool nic_type)
{
    struct hinic3_dev_node *iter = NULL;
    struct hinic3_dp_extend_info *dp_info = NULL;
    struct hinic3_flow_agent_db *hw_offload = NULL;
    struct dev_mgmt *dev_list = NULL;

    if (nic_type == SMART_NIC_MODE) {
        dev_list = hinic3_updata_netdev_hwpt_flavor();
    } else {
        dev_list = get_netdev_hwpt_flavor();
    }

    if (dev_list == NULL)
        return -1;

    dp_info = hinic3_get_offload_extend_info();
    if ((dp_info == NULL) || (dp_info->hw_offload == NULL))
        return -1;

    if (hinic3_list_is_empty(&dev_list->vf_list) == false) {
        LIST_FOR_EACH(iter, node, &dev_list->vf_list) {
            vf_stats->total_num++;
            if (iter->phy_dev.refcnt == HWPT_FLAVOR_REFCNT_USED) {
                vf_stats->used_num++;
            } else if (iter->phy_dev.refcnt == HWPT_FLAVOR_REFCNT_INVALID) {
                vf_stats->invalid_num++;
            } else if (iter->phy_dev.refcnt == HWPT_FLAVOR_REFCNT_UNUSED) {
                vf_stats->unused_num++;
            }
        }
    }
    if (hinic3_list_is_empty(&dev_list->pf_list) == false) {
        LIST_FOR_EACH(iter, node, &dev_list->pf_list) {
            pf_stats->total_num++;
            if (iter->phy_dev.refcnt == HWPT_FLAVOR_REFCNT_USED) {
                pf_stats->used_num++;
            } else if (iter->phy_dev.refcnt == HWPT_FLAVOR_REFCNT_INVALID) {
                pf_stats->invalid_num++;
            } else if (iter->phy_dev.refcnt == HWPT_FLAVOR_REFCNT_UNUSED) {
                pf_stats->unused_num++;
            }
        }
    }

    hw_offload = dp_info->hw_offload;
    vf_stats->virtio_total_num = hw_offload->queue_num.total_count;
    vf_stats->virtio_used_num = hw_offload->queue_num.used_now;
    pf_stats->virtio_total_num = hw_offload->queue_num.total_count;
    pf_stats->virtio_used_num = hw_offload->queue_num.used_now;

    return 0;
}

static void
hinic3_vf_stats_show(struct unixctl_conn *conn, int argc HINIC3_UNUSED,
    const char *argv[] HINIC3_UNUSED, void *aux)
{
    int ret = 0;
    struct ds ds = DS_EMPTY_INITIALIZER;
    struct port_stats vf_stats = {0};
    struct port_stats pf_stats = {0};
    bool nic_type = hinic3_device_mode_get();

    ret = hinic3_get_port_stats(&vf_stats, &pf_stats, nic_type);
    if (ret != 0)
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_FAILURE "Failed to obtain the port statistics.\n");

    hinic3_port_virtio_queue_show(&ds, &vf_stats, &pf_stats);

    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
    *(int *)aux = 0;
    return;
}

void
unixctl_hinic3_port_cmd_init(void)
{
    enum hinic3_port_cmd_max {
        DUMP_UPCALL_QUEUE_INFO_PARAM = 2,
    };

    hinic3_command_register("hwoff/show-function-flavor", "", 0, 0, hinic3_vf_flavor_show, NULL);
    hinic3_command_register("hwoff/show-function-stats", "", 0, 0, hinic3_vf_stats_show, NULL);
    hinic3_command_register("hwoff/dump-ports", "{ <port-name> | global | { -h | --help } }", 1, 1,
        hinic3_dump_ports_stats_command, NULL);
    hinic3_command_register("hwoff/flush-ports", "{ <port-name> | global | { -h | --help } }", 1, 1,
        hinic3_flush_ports_stats_command, NULL);
    hinic3_command_register("hwoff/dump-bond-slave-info", "{ <bond-name> | { -h | --help } }", 1, 1,
        hinic3_dump_bond_slave_info_cmd, NULL);
    hinic3_command_register("hwoff/dump-ports-queue-info", "{ -p <port-name> | { -h | --help } }", 1, 2,
        hinic3_dump_ports_queue_info_command, NULL);
    if (hinic3_get_virtual_queue_mode_enabled() == false) {
        hinic3_command_register(
            "hwoff/dump-upcall-queue-info", "", 0, 0, hinic3_dump_upcall_queues_info_cmd, NULL);
    } else {
        hinic3_command_register("hwoff/dump-upcall-queue-info",
            "[ -all | -port <port-name> | -virtual <queue-name> | -physical <queue-id> | { -h | --help } ]",
            0, OPTIONAL_ARGUMENT, hinic3_dump_upcall_queues_info_cmd, NULL);
    }
}
