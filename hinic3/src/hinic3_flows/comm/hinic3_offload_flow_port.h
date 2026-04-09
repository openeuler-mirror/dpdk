/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Huawei Technologies Co., Ltd
 */
#ifndef HINIC3_OFFLOAD_FLOW_PORT_H
#define HINIC3_OFFLOAD_FLOW_PORT_H
#include <stdint.h>
struct hinic3_port_node {
    uint16_t hinic3_vport_id;
    uint16_t hinic3_port_index;
    bool is_used;
};

int hinic3_set_port_map_by_ifindex(uint16_t vport_id, uint16_t port_index);
int hinic3_get_port_id_by_ifindex(uint16_t vport_id, uint16_t *port_index);
void hinic3_ifindex_port_map_init(void);
void hinic3_ifindex_port_map_uninit(void);
int hinic3_ifindex_port_remove(uint16_t vport_id);
void hinic3_ifindex_port_map_rwlock(void);
void hinic3_ifindex_port_map_rwunlock(void);
void hinic3_ifindex_port_map_rdlock(void);
void hinic3_ifindex_port_map_rdunlock(void);
struct hinic3_port_node *hinic3_get_port_map(void);
#endif
