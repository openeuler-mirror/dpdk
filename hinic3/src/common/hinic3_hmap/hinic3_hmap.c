/* Copyright(c) 2023 Huawei Technologies Co., Ltd
 * 
 * This file contains code segments derived from Nicira, Inc.
 * Original copyright notice:
 * 
 * Copyright (c) 2008, 2009, 2010, 2012, 2013, 2015, 2019 Nicira, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "hinic3_hash.h"
#include "hinic3_util.h"
#include "hinic3_log.h"
#include "hinic3_meminfo.h"
#include "hinic3_hmap.h"

void hinic3_hmap_init(struct hmap *hmap)
{
    hmap->buckets = &hmap->one;
    hmap->one = NULL;
    hmap->mask = 0;
    hmap->n = 0;
}

void hinic3_hmap_destroy(struct hmap *hmap)
{
    if (hmap != NULL && hmap->buckets != &hmap->one) {
        hinic3_free(hmap->buckets);
        hmap->buckets = NULL;
    }
}

bool hinic3_hmap_is_empty(const struct hmap *hmap)
{
    return hmap->n == 0;
}

size_t hinic3_hmap_count(const struct hmap *hmap)
{
    return hmap->n;
}

void hinic3_hmap_clear(struct hmap *hmap)
{
    size_t size;
    if (hmap->n > 0) {
        hmap->n = 0;
        size = (hmap->mask + 1) * sizeof(*hmap->buckets);
        memset(hmap->buckets, 0, size);
    }
}

static inline struct hmap_node* hinic3_hmap_next_(const struct hmap *hmap, size_t start)
{
    struct hmap_node *node = NULL;
    size_t i;

    for (i = start; i <= hmap->mask; i++) {
        node = hmap->buckets[i];
        if (node != NULL) {
            return node;
        }
    }

    return NULL;
}

struct hmap_node* hinic3_hmap_first(const struct hmap *hmap)
{
    return hinic3_hmap_next_(hmap, 0);
}

struct hmap_node* hinic3_hmap_next(const struct hmap *hmap, const struct hmap_node *node)
{
    return (node->next ? node->next : hinic3_hmap_next_(hmap, (node->hash & hmap->mask) + 1));
}

void hinic3_hmap_moved(struct hmap *hmap)
{
    if (!hmap->mask) {
        hmap->buckets = &hmap->one;
    }
}

void hinic3_hmap_swap(struct hmap *a, struct hmap *b)
{
    struct hmap tmp = *a;
    *a = *b;
    *b = tmp;
    hinic3_hmap_moved(a);
    hinic3_hmap_moved(b);
}

static inline void hinic3_hmap_insert_fast(struct hmap *hmap, struct hmap_node *node, size_t hash)
{
    struct hmap_node **bucket = &hmap->buckets[hash & hmap->mask];
    node->hash = hash;
    node->next = *bucket;
    *bucket = node;
    hmap->n++;
}

static void hinic3_resize(struct hmap *hmap, size_t new_mask, const char *where HINIC3_UNUSED, enum hinic3_module module_id)
{
    struct hmap tmp;
    size_t i;

    hinic3_hmap_init(&tmp);
    if (new_mask) {
        tmp.buckets = hinic3_xmalloc(sizeof (*tmp.buckets) * (new_mask + 1), module_id);
        if (tmp.buckets == NULL) {
            HINIC3_LOG(ERR, AGENT, "malloc failed when resize!");
            goto err;
        }
        tmp.mask = new_mask;
        for (i = 0; i <= tmp.mask; i++) {
            tmp.buckets[i] = NULL;
        }
    }

    for (i = 0; i <= hmap->mask; i++) {
        struct hmap_node *node, *next;
        for (node = hmap->buckets[i]; node; node = next) {
            next = node->next;
            hinic3_hmap_insert_fast(&tmp, node, node->hash);
        }
    }
    hinic3_hmap_swap(hmap, &tmp);
err:
    hinic3_hmap_destroy(&tmp);
}

static size_t hinic3_calc_mask(size_t capacity)
{
    size_t mask = capacity / 0x2;
    mask |= mask >> 0x1;
    mask |= mask >> 0x2;
    mask |= mask >> 0x4;
    mask |= mask >> 0x8;
    mask |= mask >> 0x10;
    mask |= mask >> 0x20;

    mask |= (mask & 0x1) << 0x1;

    return mask;
}

static void hinic3_hmap_expand_at(struct hmap *hmap, const char *where, enum hinic3_module module_id)
{
    size_t new_mask = hinic3_calc_mask(hmap->n);
    if (new_mask > hmap->mask) {
        hinic3_resize(hmap, new_mask, where, module_id);
    }
}

void hinic3_hmap_insert_at(struct hmap *hmap, struct hmap_node *node, size_t hash,
    const char *where, enum hinic3_module module_id)
{
    hinic3_hmap_insert_fast(hmap, node, hash);
    if (hmap->n / 0x2 > hmap->mask) {
        hinic3_hmap_expand_at(hmap, where, module_id);
    }
}

void hinic3_hmap_remove(struct hmap *hmap, struct hmap_node *node)
{
    struct hmap_node **bucket = &hmap->buckets[node->hash & hmap->mask];
    while (*bucket != node) {
        bucket = &(*bucket)->next;
    }
    *bucket = node->next;
    hmap->n--;
}

static inline struct hmap_node* hinic3_hmap_next_with_hash_(struct hmap_node *node, size_t hash)
{
    while (node != NULL && node->hash != hash) {
        node = node->next;
    }

    return node;
}

struct hmap_node* hinic3_hmap_first_with_hash(const struct hmap *hmap, size_t hash)
{
    return hinic3_hmap_next_with_hash_(hmap->buckets[hash & hmap->mask], hash);
}

struct hmap_node* hinic3_hmap_next_with_hash(const struct hmap_node *node)
{
    return hinic3_hmap_next_with_hash_(node->next, node->hash);
}
