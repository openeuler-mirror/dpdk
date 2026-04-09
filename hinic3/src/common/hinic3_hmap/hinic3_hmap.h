/* Copyright(c) 2023 Huawei Technologies Co., Ltd
 * 
 * This file contains code segments derived from Nicira, Inc.
 * Original copyright notice:
 * 
 * Copyright (c) 2008, 2009, 2010, 2012, 2013, 2015, 2016 Nicira, Inc.
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

#ifndef _HINIC3_HMAP_H_
#define _HINIC3_HMAP_H_

#include <stdio.h>
#include <stdint.h>
#include "hinic3_map.h"   /* 为了保持与ovs原生接口兼容，使用原生ovs软件hmap的数据结构定义 */
#include "hinic3_util.h"

#define HINIC3_HMAP_FOR_EACH(NODE, MEMBER, HMAP) \
    HINIC3_HMAP_FOR_EACH_INIT(NODE, MEMBER, HMAP, (void) 0)

#define HINIC3_HMAP_FOR_EACH_INIT(NODE, MEMBER, HMAP, ...)                     \
    for (HINIC3_CONTAINER_INIT(NODE, hinic3_hmap_first(HMAP), MEMBER);   \
         ((NODE) != HINIC3_OBJECT_CONTAINING(NULL, (NODE), MEMBER))                \
         || (((NODE) = NULL), false);                                     \
         HINIC3_CONTAINER_ASSIGN(NODE, hinic3_hmap_next(HMAP, &(NODE)->MEMBER), MEMBER))

#define HINIC3_HMAP_FOR_EACH_WITH_HASH(NODE, MEMBER, HASH, HMAP)               \
    for (HINIC3_CONTAINER_INIT(NODE, hinic3_hmap_first_with_hash(HMAP, HASH), MEMBER); \
         ((NODE) != HINIC3_OBJECT_CONTAINING(NULL, (NODE), MEMBER))                \
         || (((NODE) = NULL), false);                                     \
         HINIC3_CONTAINER_ASSIGN(NODE, hinic3_hmap_next_with_hash(&(NODE)->MEMBER),    MEMBER))

#define HINIC3_HMAP_FOR_EACH_SAFE(NODE, NEXT, MEMBER, HMAP) \
    HINIC3_HMAP_FOR_EACH_SAFE_INIT(NODE, NEXT, MEMBER, HMAP, (void) 0)

#define HINIC3_HMAP_FOR_EACH_SAFE_INIT(NODE, NEXT, MEMBER, HMAP, ...)          \
    for (HINIC3_CONTAINER_INIT(NODE, hinic3_hmap_first(HMAP), MEMBER);   \
         ((NODE != HINIC3_OBJECT_CONTAINING(NULL, (NODE), MEMBER))               \
          || ((NODE = NULL), false)                                     \
          ? HINIC3_CONTAINER_INIT(NEXT, hinic3_hmap_next(HMAP, &(NODE)->MEMBER), MEMBER), 1 : 0);          \
         (NODE) = (NEXT))

void hinic3_hmap_clear(struct hmap *hamp);
void hinic3_hmap_init(struct hmap *);
void hinic3_hmap_destroy(struct hmap *);
bool hinic3_hmap_is_empty(const struct hmap *hmap);
size_t hinic3_hmap_count(const struct hmap *hmap);
struct hmap_node* hinic3_hmap_first(const struct hmap *hmap);
struct hmap_node* hinic3_hmap_next(const struct hmap *hmap, const struct hmap_node *node);
void hinic3_hmap_insert_at(struct hmap *, struct hmap_node *, size_t hash, const char *where,
    enum hinic3_module module_id);
void hinic3_hmap_remove(struct hmap *, struct hmap_node *);
void hinic3_hmap_swap(struct hmap *a, struct hmap *b);
void hinic3_hmap_moved(struct hmap *hmap);
struct hmap_node* hinic3_hmap_first_with_hash(const struct hmap *hmap, size_t hash);
struct hmap_node* hinic3_hmap_next_with_hash(const struct hmap_node *node);

static inline void hinic3_hmap_insert(struct hmap *hmap,
    struct hmap_node *node, size_t hash, enum hinic3_module module_id)
{
    hinic3_hmap_insert_at(hmap, node, hash, HINIC3_SOURCE_LOCATOR, module_id);
}
#endif
