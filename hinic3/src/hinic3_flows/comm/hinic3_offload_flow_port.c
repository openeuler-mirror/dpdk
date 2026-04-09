/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Huawei Technologies Co., Ltd
 */
#include <stdbool.h>
#include <string.h>
#include "hinic3_log.h"
#include "hinic3_ui_string.h"
#include "hinic3_mutex.h"
#include "hinic3_offload_flow_port.h"

#define HINIC3_VF_PORT_MAX_NUM          1024
#define HINIC3_PF_PORT_MAX_NUM          32
#define HINIC3_BOND_PORT_MAX_NUM        2
#define HINIC3_PORT_INDEX_OFFSET        1
#define HINIC3_PORT_MAP_MAX_LENGTH     (HINIC3_VF_PORT_MAX_NUM + HINIC3_PF_PORT_MAX_NUM + \
                                       HINIC3_BOND_PORT_MAX_NUM + HINIC3_PORT_INDEX_OFFSET)

static struct hinic3_rwlock g_hinic3_port_lock;
static struct hinic3_port_node g_hinic3_port_map[HINIC3_PORT_MAP_MAX_LENGTH] = {0};

void hinic3_ifindex_port_map_init(void)
{
    hinic3_rwlock_init(&g_hinic3_port_lock);
}

void hinic3_ifindex_port_map_uninit(void)
{
    hinic3_rwlock_destroy(&g_hinic3_port_lock);
}

void hinic3_ifindex_port_map_rwlock(void)
{
    hinic3_rwlock_wrlock(&g_hinic3_port_lock);
}

void hinic3_ifindex_port_map_rwunlock(void)
{
    hinic3_rwlock_wrunlock(&g_hinic3_port_lock);
}

void hinic3_ifindex_port_map_rdlock(void)
{
    hinic3_rwlock_rdlock(&g_hinic3_port_lock);
}

void hinic3_ifindex_port_map_rdunlock(void)
{
    hinic3_rwlock_rdunlock(&g_hinic3_port_lock);
}

struct hinic3_port_node *hinic3_get_port_map(void)
{
    return g_hinic3_port_map;
}

static int hinic3_ifindex_port_find(uint16_t vport_id, struct hinic3_port_node **node)
{
    for (int i = 0; i < HINIC3_PORT_MAP_MAX_LENGTH; ++i) {
        if (g_hinic3_port_map[i].is_used == true && g_hinic3_port_map[i].hinic3_vport_id == vport_id) {
            *node = &g_hinic3_port_map[i];
            return 0;
        }
    }
    return -1;
}

static int hinic3_ifindex_port_insert(uint16_t vport_id, uint16_t port_index)
{
    int ret;
    struct hinic3_port_node *node = NULL;

    hinic3_rwlock_wrlock(&g_hinic3_port_lock);
    ret = hinic3_ifindex_port_find(vport_id, &node);
    if (ret == 0) {
        node->hinic3_port_index = port_index;
        hinic3_rwlock_wrunlock(&g_hinic3_port_lock);
        return 0;
    }

    for (int i = 0; i < HINIC3_PORT_MAP_MAX_LENGTH; ++i) {
        if (!g_hinic3_port_map[i].is_used) {
            g_hinic3_port_map[i].hinic3_vport_id = vport_id;
            g_hinic3_port_map[i].hinic3_port_index = port_index;
            g_hinic3_port_map[i].is_used = true;
            hinic3_rwlock_wrunlock(&g_hinic3_port_lock);
            return 0;
        }
    }
    HINIC3_LOG(ERR, FLOW, "hinic3_ifindex_port_insert: insert port failed");
    hinic3_rwlock_wrunlock(&g_hinic3_port_lock);
    return -1;
}

int hinic3_ifindex_port_remove(uint16_t vport_id)
{
    struct hinic3_port_node *node = NULL;
    int ret;

    hinic3_rwlock_wrlock(&g_hinic3_port_lock);
    ret = hinic3_ifindex_port_find(vport_id, &node);
    if (ret != 0) {
        HINIC3_LOG(ERR, FLOW, "hinic3_ifindex_port_remove: remove port node failed");
        hinic3_rwlock_wrunlock(&g_hinic3_port_lock);
        return -1;
    }

    node->hinic3_vport_id = 0;
    node->hinic3_port_index = 0;
    node->is_used = false;
    hinic3_rwlock_wrunlock(&g_hinic3_port_lock);
    return 0;
}

int hinic3_set_port_map_by_ifindex(uint16_t vport_id, uint16_t port_index)
{
    int ret = 0;
    ret = hinic3_ifindex_port_insert(vport_id, port_index);
    if (ret != 0) {
        HINIC3_LOG(ERR, FLOW, "hinic3_set_port_map_by_ifindex: set port id map failed");
        return -1;
    }
    return ret;
}

int hinic3_get_port_id_by_ifindex(uint16_t vport_id, uint16_t *port_index)
{
    struct hinic3_port_node *node = NULL;
    int ret;

    if (port_index == NULL) {
        return -1;
    }

    hinic3_rwlock_rdlock(&g_hinic3_port_lock);
    ret = hinic3_ifindex_port_find(vport_id, &node);
    if (ret != 0) {
        hinic3_rwlock_rdunlock(&g_hinic3_port_lock);
        return -1;
    }

    *port_index = node->hinic3_port_index;
    hinic3_rwlock_rdunlock(&g_hinic3_port_lock);
    return 0;
}
