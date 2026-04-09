/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_DFX_PORT_H
#define HINIC3_DFX_PORT_H

#include <stdint.h>
#include <stdbool.h>
#include "rte_ethdev.h"
#include "hinic3_util.h"
#include "hinic3_command.h"
#include "hinic3_option.h"
#include "hinic3_provider.h"
#include "hinic3_ds.h"

struct port_status {
    uint8_t refcnt;
    const char *str;
};

struct port_stats {
    uint32_t total_num;
    uint32_t unused_num;
    uint32_t used_num;
    uint32_t invalid_num;
    uint32_t virtio_total_num;
    uint32_t virtio_used_num;
};

bool hinic3_dfx_port_process_args(int argc, const char *argv[], char *netdev_name, bool *is_global);
int hinic3_get_bond_link_from_netdev(struct ds *output_msg, const char *netdev_name, uint32_t *link_status);

void hinic3_dump_bond_slave_info_cmd(struct unixctl_conn *conn, int argc, const char *argv[], void *aux);
void hinic3_dump_ports_stats_command(struct unixctl_conn *conn, int argc, const char *argv[], void *aux);
void hinic3_flush_ports_stats_command(struct unixctl_conn *conn, int argc, const char *argv[], void *aux);
void hinic3_dump_upcall_queues_info_cmd(struct unixctl_conn *conn,
    int argc HINIC3_UNUSED, const char *argv[] HINIC3_UNUSED, void *aux HINIC3_UNUSED);
void hinic3_dump_ports_queue_info_command(struct unixctl_conn *conn, int argc, const char *argv[], void *aux);

void unixctl_hinic3_port_cmd_init(void);

#endif
