/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_RTE_FLOW_MPOOL_H
#define HINIC3_RTE_FLOW_MPOOL_H

#include "hinic3_flow_agent_public.h"
#include "hinic3_command.h"
#include "hinic3_mpool.h"
#include "hinic3_ds.h"

#define HINIC3_RTE_FLOW_SINGLE_BLOCK_NUM  (1 * (1 << 20))
#define HINIC3_ACL_FLOW_MAX_NUM  32
#define HINIC3_HANDLE_ELE_MAX_NUM  32
#define HINIC3_DP_HASH_FLOW_NUM (64 * 1024)

#define HINIC3_RTE_FLOW_ELE_MPOOL_NAME           "hwoff_rte_flow_ele_pool"
#define HINIC3_UFID_MAP_MPOOL_NAME               "hwoff_ufid_map_pool"
#define HINIC3_FUZZY_FLOW_ELE_MPOOL_NAME          "hwoff_mega_flow_ele_pool"
#define HINIC3_HANDLE_ELE_MPOOL_NAME             "hwoff_handle_ele_pool"
#define HINIC3_ACL_ELE_MPOOL_NAME                "hwoff_acl_flow_ele_pool"
#define HINIC3_DP_HASH_ELE_MPOOL_NAME            "hwoff_dp_hash_flow_ele_pool"
#define HINIC3_DP_HASH_UFID_MPOOL_NAME           "hwoff_dp_hash_ufid_pool"

struct hinic3_mpool_mgmt_rte_flow {
    struct dpak_mempool *rte_flow_mpool;
    struct dpak_mempool *ufid_map_mpool;
    struct dpak_mempool *fuzzy_flow_mpool;
    struct dpak_mempool *handle_mpool;
    struct dpak_mempool *acl_flow_mpool;
    struct dpak_mempool *dp_hash_flow_mpool;
    struct dpak_mempool *dp_hash_ufid_mpool;
};

int hinic3_init_rte_flow_mpool(void);
void hinic3_uninit_rte_flow_mpool(void);
int hinic3_init_flow_mpool(void);
int hinic3_init_fuzzy_flow_mpool(void);
int hinic3_flush_rte_flow_mpool(void);
int hinic3_flush_ufid_map_mpool(void);
int hinic3_flush_fuzzy_flow_mpool(void);
int hinic3_flush_dp_hash_flow_map_mpool(void);
int hinic3_flush_dp_hash_ufid_map_mpool(void);
void hinic3_free_from_rte_flow_mpool(void *ptr);
void hinic3_free_from_ufid_map_mpool(void *ptr);
void hinic3_free_from_fuzzy_flow_mpool(void *ptr);
void hinic3_free_from_handle_mpool(void *ptr);
void hinic3_free_from_acl_mpool(void *ptr);
void hinic3_free_from_dp_hash_mpool(void *ptr);
void hinic3_free_from_dp_hash_ufid_mpool(void *ptr);
void *hinic3_alloc_from_rte_flow_mpool(void);
void *hinic3_alloc_from_ufid_map_mpool(void);
void *hinic3_alloc_from_fuzzy_flow_mpool(void);
void *hinic3_alloc_from_handle_mpool(void);
void *hinic3_alloc_from_acl_mpool(void);
void *hinic3_alloc_from_dp_hash_mpool(void);
void *hinic3_alloc_from_dp_hash_ufid_mpool(void);

struct hinic3_mpool_mgmt_rte_flow *hinic3_get_mpool_mgmt(void);
void hinic3_dump_mempool_info(struct unixctl_conn *conn, int argc, const char *argv[], void *aux);
#endif
