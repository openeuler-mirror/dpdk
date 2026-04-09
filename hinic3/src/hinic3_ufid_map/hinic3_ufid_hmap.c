/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Huawei Technologies Co., Ltd
 */

#include "hinic3_packet_key_public.h"
#include "hinic3_util.h"
#include "hinic3_map.h"
#include "hinic3_mpool_rte_flow.h"
#include "hinic3_ufid_hmap.h"
#include "hinic3_flexda_flow_public.h"
#include "hinic3_parse_agent_config.h"

void hinic3_ufid_hmap_init(struct hmap *ufid_hmap)
{
    hinic3_hmap_init(ufid_hmap);
}

static inline void *
hinic3_ufid_map_alloc(struct hmap *ufid_hmap HINIC3_UNUSED, HINIC3_UFID_MAP_TYPE type)
{
    switch (type)
    {
    case EMC_UFID_MAP:
        return hinic3_alloc_from_ufid_map_mpool();
    default:
        return NULL;
    }

    return NULL;
}

static inline void
hinic3_ufid_map_dealloc(struct hmap *ufid_hmap HINIC3_UNUSED, void *ptr, HINIC3_UFID_MAP_TYPE type)
{
    switch (type)
    {
    case EMC_UFID_MAP:
        return hinic3_free_from_ufid_map_mpool(ptr);
    default:
        return;
    }
}

void hinic3_ufid_hmap_destroy(struct hmap *ufid_hmap, HINIC3_UFID_MAP_TYPE type)
{
    if (ufid_hmap)
    {
        hinic3_ufid_hmap_clear(ufid_hmap, type);
        hinic3_hmap_destroy(ufid_hmap);
    }
}

struct hinic3_ufid_hmap_node *hinic3_ufid_hmap_add(struct hmap *ufid_hmap,
                                                   struct rte_flow *key, uint32_t hash, enum hinic3_module module_id, HINIC3_UFID_MAP_TYPE type)
{
    struct hinic3_ufid_hmap_node *node = NULL;

    node = hinic3_ufid_map_alloc(ufid_hmap, type);
    if (HINIC3_UNLIKELY(node == NULL))
    {
        HINIC3_LOG(ERR, FLOW, "Failed to alloc ufid map node!");
        return NULL;
    }
    node->key = key;

    hinic3_hmap_insert(ufid_hmap, &node->node, hash, module_id);
    return node;
}

static bool hinic3_ufid_match_func_flexda(const struct hinic3_conntrack_full_key *full_key1,
                                          const struct hinic3_conntrack_full_key *full_key2)
{
    const struct hydra_flow_item *hydra_key1 = NULL;
    const struct hydra_flow_item *hydra_key2 = NULL;

    if (full_key1->key.meta_num != full_key2->key.meta_num)
    {
        return false;
    }

    if (full_key1->key.table_id != full_key2->key.table_id)
    {
        return false;
    }

    if (full_key1->key.hdr_flags_num != full_key2->key.hdr_flags_num)
    {
        return false;
    }

    if (memcmp(full_key1->key.key, full_key2->key.key, full_key2->key.meta.key_len) != 0)
    {
        return false;
    }

    if (full_key1->key.hydra_key_count != full_key2->key.hydra_key_count)
    {
        return false;
    }

    hydra_key1 = full_key1->key.hydra_key_head;
    hydra_key2 = full_key2->key.hydra_key_head;

    if (hydra_key1 == hydra_key2)
    {
        return true;
    }

    for (int idx = 0; idx < full_key1->key.hydra_key_count; idx++)
    {
        if (hydra_key1->item_type != hydra_key2->item_type)
        {
            return false;
        }

        if (hydra_key1->item_data_size != hydra_key2->item_data_size)
        {
            return false;
        }

        if (memcmp(hydra_key1->item_data, hydra_key2->item_data, hydra_key1->item_data_size) != 0)
        {
            return false;
        }

        hydra_key1 = hydra_key1->next;
        hydra_key2 = hydra_key2->next;
    }

    return true;
}

static bool hinic3_ufid_match_func(const struct hinic3_conntrack_full_key *full_key1,
                                   const struct hinic3_conntrack_full_key *full_key2)
{
    if (full_key1->key.meta_num == full_key2->key.meta_num &&
        memcmp(full_key1->key.key, full_key2->key.key, full_key2->key.meta.key_len) == 0)
    {
        return true;
    }

    return false;
}

static bool hinic3_ufid_match_func_entrance(const struct hinic3_conntrack_full_key *full_key1,
                                            const struct hinic3_conntrack_full_key *full_key2)
{
    if (hinic3_card_mod_get() == PROG_MODE)
        return hinic3_ufid_match_func_flexda(full_key1, full_key2);
    else
        return hinic3_ufid_match_func(full_key1, full_key2);
}

static struct hinic3_ufid_hmap_node *hinic3_ufid_hmap_get_node(const struct hmap *ufid_hmap,
                                                               const struct hinic3_conntrack_full_key *full_key, uint32_t hash)
{
    struct hinic3_ufid_hmap_node *ufid_hmap_node;

    HINIC3_HMAP_FOR_EACH_WITH_HASH(ufid_hmap_node, node, hash, ufid_hmap)
    {
        if (hinic3_ufid_match_func_entrance(&ufid_hmap_node->key->key, full_key))
        {
            return ufid_hmap_node;
        }
    }
    return NULL;
}

struct rte_flow *hinic3_ufid_hmap_get(const struct hmap *ufid_hmap,
                                      const struct hinic3_conntrack_full_key *full_key, uint32_t hash)
{
    struct hinic3_ufid_hmap_node *node = hinic3_ufid_hmap_get_node(ufid_hmap, full_key, hash);
    return node ? node->key : NULL;
}

static void hinic3_ufid_hmap_remove_node(struct hmap *ufid_hmap, struct hinic3_ufid_hmap_node *node, HINIC3_UFID_MAP_TYPE type)
{
    hinic3_hmap_remove(ufid_hmap, &node->node);
    hinic3_ufid_map_dealloc(ufid_hmap, node, type);
    node = NULL;
}

void hinic3_ufid_hmap_clear(struct hmap *ufid_hmap, HINIC3_UFID_MAP_TYPE type)
{
    struct hinic3_ufid_hmap_node *node, *next;
    HINIC3_HMAP_FOR_EACH_SAFE(node, next, node, ufid_hmap)
    {
        hinic3_ufid_hmap_remove_node(ufid_hmap, node, type);
    }
}

int hinic3_ufid_hmap_del_by_key(struct hmap *ufid_hmap, const struct hinic3_conntrack_full_key *full_key,
                                uint32_t hash, HINIC3_UFID_MAP_TYPE type)
{
    struct hinic3_ufid_hmap_node *ufid_hmap_node = NULL;

    ufid_hmap_node = hinic3_ufid_hmap_get_node(ufid_hmap, full_key, hash);
    if (ufid_hmap_node == NULL)
    {
        HINIC3_LOG(ERR, FLOW, "Get ufid hamp node failed, hash is %u!", hash);
        return -1;
    }
    hinic3_ufid_hmap_remove_node(ufid_hmap, ufid_hmap_node, type);
    return 0;
}

static struct hinic3_ufid_hmap_node *hinic3_ufid_hmap_get_node_by_ufid(struct hmap *ufid_hmap, uint64_t hw_ufid)
{
    struct hinic3_ufid_hmap_node *node, *next;
    HINIC3_HMAP_FOR_EACH_SAFE(node, next, node, ufid_hmap)
    {
        if (node->key->hw_ufid == hw_ufid)
        {
            return node;
        }
    }
    return NULL;
}

int hinic3_ufid_hmap_del_by_ufid(struct hmap *ufid_hmap, uint64_t hw_ufid, HINIC3_UFID_MAP_TYPE type)
{
    struct hinic3_ufid_hmap_node *ufid_hmap_node = NULL;

    ufid_hmap_node = hinic3_ufid_hmap_get_node_by_ufid(ufid_hmap, hw_ufid);
    if (ufid_hmap_node == NULL)
    {
        HINIC3_LOG(ERR, FLOW, "Get ufid hmap node by ufid failed, ufid is %" PRIu64 "!", hw_ufid);
        return -1;
    }
    hinic3_ufid_hmap_remove_node(ufid_hmap, ufid_hmap_node, type);
    return 0;
}
