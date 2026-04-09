/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_UFID_HMAP_H
#define HINIC3_UFID_HMAP_H

#include "hinic3_flow_agent_public.h"
#include "hinic3_hmap.h"

struct hinic3_ufid_hmap_node {
    struct hmap_node node;
    struct rte_flow *key;
};

typedef enum {
    EMC_UFID_MAP,
    DP_HASH_UFID_MAP,
    UFID_MAP_MAX,
} HINIC3_UFID_MAP_TYPE;

void hinic3_ufid_hmap_init(struct hmap *ufid_hmap);
void hinic3_ufid_hmap_destroy(struct hmap *ufid_hmap, HINIC3_UFID_MAP_TYPE type);
struct hinic3_ufid_hmap_node *hinic3_ufid_hmap_add(struct hmap *ufid_hmap,
    struct rte_flow *key, uint32_t hash, enum hinic3_module module_id, HINIC3_UFID_MAP_TYPE type);
struct rte_flow *hinic3_ufid_hmap_get(const struct hmap *ufid_hmap,
    const struct hinic3_conntrack_full_key *full_key, uint32_t hash);
void hinic3_ufid_hmap_clear(struct hmap *ufid_hmap, HINIC3_UFID_MAP_TYPE type);
int hinic3_ufid_hmap_del_by_key(struct hmap *ufid_hmap, const struct hinic3_conntrack_full_key *full_key, uint32_t hash,
    HINIC3_UFID_MAP_TYPE type);
int hinic3_ufid_hmap_del_by_ufid(struct hmap *ufid_hmap, uint64_t hw_ufid, HINIC3_UFID_MAP_TYPE type);
#endif
