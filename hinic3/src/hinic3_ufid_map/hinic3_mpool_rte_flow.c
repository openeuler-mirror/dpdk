/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Huawei Technologies Co., Ltd
 */

#include <stdint.h>
#include "hinic3_flow_session.h"
#include "hinic3_ufid_hmap.h"
#include "hinic3_iface_global.h"
#include "hinic3_ds.h"
#include "hinic3_command.h"
#include "hinic3_parse_agent_config.h"
#include "hinic3_mega_offload.h"
#include "hinic3_mpool_rte_flow.h"

#define HINIC3_FUZZY_FLOW_MAX_NUM 512
#define HINIC3_FUZZY_FLOW_TABLE_MAX_NUM 2

static struct hinic3_mpool_mgmt_rte_flow g_mpool_mgmt = { 0 };

struct hinic3_mpool_mgmt_rte_flow *hinic3_get_mpool_mgmt(void)
{
    return &g_mpool_mgmt;
}

void hinic3_uninit_rte_flow_mpool(void)
{
    dpak_mempool_destroy(g_mpool_mgmt.rte_flow_mpool);
    dpak_mempool_destroy(g_mpool_mgmt.ufid_map_mpool);
    if (hinic3_check_fuzzy_flow_switch()){
        dpak_mempool_destroy(g_mpool_mgmt.fuzzy_flow_mpool);
    }
    dpak_mempool_destroy(g_mpool_mgmt.handle_mpool);
    dpak_mempool_destroy(g_mpool_mgmt.acl_flow_mpool);
    dpak_mempool_destroy(g_mpool_mgmt.dp_hash_flow_mpool);
    dpak_mempool_destroy(g_mpool_mgmt.dp_hash_ufid_mpool);
}

int hinic3_flush_rte_flow_mpool(void)
{
    return dpak_mempool_flush(g_mpool_mgmt.rte_flow_mpool);
}

int hinic3_flush_ufid_map_mpool(void)
{
    return dpak_mempool_flush(g_mpool_mgmt.ufid_map_mpool);
}

int hinic3_flush_fuzzy_flow_mpool(void)
{
    return dpak_mempool_flush(g_mpool_mgmt.fuzzy_flow_mpool);
}

int hinic3_flush_dp_hash_flow_map_mpool(void)
{
    return dpak_mempool_flush(g_mpool_mgmt.dp_hash_flow_mpool);
}

int hinic3_flush_dp_hash_ufid_map_mpool(void)
{
    return dpak_mempool_flush(g_mpool_mgmt.dp_hash_ufid_mpool);
}

int hinic3_init_flow_mpool(void)
{
    uint32_t max_flow_num = hinic3_max_flow_num_get();

    g_mpool_mgmt.rte_flow_mpool = hinic3_create_single_mpool(HINIC3_RTE_FLOW_ELE_MPOOL_NAME, sizeof(struct rte_flow),
        max_flow_num * HINIC3_RTE_FLOW_SINGLE_BLOCK_NUM, HINIC3_UFID_MAP, false);
    if (g_mpool_mgmt.rte_flow_mpool == NULL) {
        HINIC3_LOG(ERR, FLOW, "Init flow mpool: failed to create single rte flow mpool!");
        return -1;
    }

    size_t ufid_map_size = max_flow_num * HINIC3_RTE_FLOW_SINGLE_BLOCK_NUM;
    if (hinic3_check_fuzzy_flow_flexda_switch()) {
        ufid_map_size += HINIC3_FUZZY_FLOW_MAX_NUM * HINIC3_FUZZY_FLOW_TABLE_MAX_NUM;
    }
    g_mpool_mgmt.ufid_map_mpool = hinic3_create_single_mpool(HINIC3_UFID_MAP_MPOOL_NAME,
        sizeof(struct hinic3_ufid_hmap_node), ufid_map_size, HINIC3_UFID_MAP, false);
    if (g_mpool_mgmt.ufid_map_mpool == NULL) {
        dpak_mempool_destroy(g_mpool_mgmt.rte_flow_mpool);
        g_mpool_mgmt.rte_flow_mpool = NULL;
        HINIC3_LOG(ERR, FLOW, "Init flow mpool: failed to create single ufid map mpool!");
        return -1;
    }

    return 0;
}

#define HINIC3_FLEXDA_FUZZY_FLOW_MAX_NUM 512
#define HINIC3_FLEXDA_FUZZY_FLOW_TABLE_MAX_NUM 2

int hinic3_init_fuzzy_flow_mpool(void)
{
    if (hinic3_check_fuzzy_flow_switch() == false)
        return 0;

    size_t fuzzy_flow_size;
    size_t fuzzy_flow_cap;
    if (hinic3_check_fuzzy_flow_flexda_switch()) {
        fuzzy_flow_size = sizeof(struct fuzzy_flow);
        fuzzy_flow_cap = HINIC3_FLEXDA_FUZZY_FLOW_MAX_NUM * HINIC3_FLEXDA_FUZZY_FLOW_TABLE_MAX_NUM;
    } else {
        fuzzy_flow_size = sizeof(struct mega_flow);
        fuzzy_flow_cap = HINIC3_MEGA_FLOW_MAX_NUM;
    }
    g_mpool_mgmt.fuzzy_flow_mpool = hinic3_create_single_mpool(
        HINIC3_FUZZY_FLOW_ELE_MPOOL_NAME, fuzzy_flow_size, fuzzy_flow_cap, HINIC3_UFID_MAP, false);
    if (g_mpool_mgmt.fuzzy_flow_mpool == NULL)
        return -1;

    return 0;
}

int hinic3_init_rte_flow_mpool(void)
{
    int ret = 0;

    ret = hinic3_init_flow_mpool();
    if (ret != 0) {
        HINIC3_LOG(ERR, FLOW, "Init flow mpool failed!");
        goto clean;
    }

    ret = hinic3_init_fuzzy_flow_mpool();
    if (ret != 0) {
        HINIC3_LOG(ERR, FLOW, "Init fuzzy flow mpool failed!");
        goto clean;
    }

    return 0;
clean:
    hinic3_uninit_rte_flow_mpool();
    return -1;
}

void hinic3_free_from_rte_flow_mpool(void *ptr)
{
    if (ptr == NULL) {
        HINIC3_LOG(ERR, FLOW, "Ptr is null in rte_flow_mpool!");
        return;
    }

    dpak_mempool_free(g_mpool_mgmt.rte_flow_mpool, ptr);
}

void hinic3_free_from_ufid_map_mpool(void *ptr)
{
    if (ptr == NULL) {
        HINIC3_LOG(ERR, FLOW, "Ptr is null in ufid_map_mpool!");
        return;
    }

    dpak_mempool_free(g_mpool_mgmt.ufid_map_mpool, ptr);
}

void hinic3_free_from_fuzzy_flow_mpool(void *ptr)
{
    if (ptr == NULL) {
        HINIC3_LOG(ERR, FLOW, "Ptr is null in mega_flow_mpool!");
        return;
    }

    dpak_mempool_free(g_mpool_mgmt.fuzzy_flow_mpool, ptr);
}

void hinic3_free_from_dp_hash_ufid_mpool(void *ptr)
{
    if (ptr == NULL) {
        HINIC3_LOG(ERR, FLOW, "Ptr is null in dp_hash_ufid_mpool!");
        return;
    }

    dpak_mempool_free(g_mpool_mgmt.dp_hash_ufid_mpool, ptr);
}

void hinic3_free_from_dp_hash_mpool(void *ptr)
{
    if (ptr == NULL) {
        HINIC3_LOG(ERR, FLOW, "Ptr is null in dp_hash_mpool!");
        return;
    }

    dpak_mempool_free(g_mpool_mgmt.dp_hash_flow_mpool, ptr);
}

void hinic3_free_from_acl_mpool(void *ptr)
{
    if (ptr == NULL) {
        HINIC3_LOG(ERR, FLOW, "Ptr is null in acl_mpool!");
        return;
    }

    dpak_mempool_free(g_mpool_mgmt.acl_flow_mpool, ptr);
}

void hinic3_free_from_handle_mpool(void *ptr)
{
    if (ptr == NULL) {
        HINIC3_LOG(ERR, FLOW, "Ptr is null in handle_mpool!");
        return;
    }

    dpak_mempool_free(g_mpool_mgmt.handle_mpool, ptr);
}

void *hinic3_alloc_from_rte_flow_mpool(void)
{
    return dpak_mempool_alloc(g_mpool_mgmt.rte_flow_mpool, HINIC3_UFID_MAP);
}

void *hinic3_alloc_from_ufid_map_mpool(void)
{
    return dpak_mempool_alloc(g_mpool_mgmt.ufid_map_mpool, HINIC3_UFID_MAP);
}

void *hinic3_alloc_from_fuzzy_flow_mpool(void)
{
    return dpak_mempool_alloc(g_mpool_mgmt.fuzzy_flow_mpool, HINIC3_UFID_MAP);
}

void *hinic3_alloc_from_handle_mpool(void)
{
    return dpak_mempool_alloc(g_mpool_mgmt.handle_mpool, HINIC3_UFID_MAP);
}

void *hinic3_alloc_from_acl_mpool(void)
{
    return dpak_mempool_alloc(g_mpool_mgmt.acl_flow_mpool, HINIC3_UFID_MAP);
}

void *hinic3_alloc_from_dp_hash_mpool(void)
{
    return dpak_mempool_alloc(g_mpool_mgmt.dp_hash_flow_mpool, HINIC3_UFID_MAP);
}

void *hinic3_alloc_from_dp_hash_ufid_mpool(void)
{
    return dpak_mempool_alloc(g_mpool_mgmt.dp_hash_ufid_mpool, HINIC3_UFID_MAP);
}

void
hinic3_dump_mempool_info(struct unixctl_conn *conn, int argc HINIC3_UNUSED,
    const char *argv[] HINIC3_UNUSED, void *aux)
{
    struct ds ds = DS_EMPTY_INITIALIZER;
    hinic3_show_one_mpool(g_mpool_mgmt.rte_flow_mpool, &ds, false);
    hinic3_show_one_mpool(g_mpool_mgmt.ufid_map_mpool, &ds, false);

    if (hinic3_acl_flow_get() == true) {
        hinic3_show_one_mpool(g_mpool_mgmt.handle_mpool, &ds, false);
        hinic3_show_one_mpool(g_mpool_mgmt.acl_flow_mpool, &ds, false);
    }
    if (hinic3_dp_hash_flow_get() == true) {
        hinic3_show_one_mpool(g_mpool_mgmt.dp_hash_flow_mpool, &ds, false);
        hinic3_show_one_mpool(g_mpool_mgmt.dp_hash_ufid_mpool, &ds, false);
    }

    if (hinic3_check_fuzzy_flow_switch() == true)
        hinic3_show_one_mpool(g_mpool_mgmt.fuzzy_flow_mpool, &ds, true);

    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
    *(int *)aux = 0;
    return;
}
