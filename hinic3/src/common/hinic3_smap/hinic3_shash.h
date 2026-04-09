/* Copyright(c) 2023 Huawei Technologies Co., Ltd
 * 
 * This file contains code segments derived from Nicira, Inc.
 * Original copyright notice:
 * 
 * Copyright (c) 2009, 2010, 2011, 2016 Nicira, Inc.
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

#ifndef HINIC3_SHASH_H
#define HINIC3_SHASH_H

#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include "hinic3_hmap.h"

#define HINIC3_SHASH_FOR_EACH(SHASH_NODE, SHASH)                               \
    HINIC3_HMAP_FOR_EACH_INIT (SHASH_NODE, node, &(SHASH)->map)

#define HINIC3_SHASH_FOR_EACH_SAFE(SHASH_NODE, NEXT, SHASH)        \
    HINIC3_HMAP_FOR_EACH_SAFE_INIT (                               \
        SHASH_NODE, NEXT, node, &(SHASH)->map)

void hinic3_shash_init(struct shash *shash);
void hinic3_shash_destroy(struct shash *shash);
void hinic3_shash_destroy_free_data(struct shash *sh);
const void* hinic3_shash_find_data(const struct shash *shash, const char *name);
struct shash_node* hinic3_shash_find(const struct shash *sh, const char *name);
bool hinic3_shash_add_once(struct shash *shash, const char *name, const void *data, enum hinic3_module module_id);
char* hinic3_shash_steal(struct shash *sh, struct shash_node *node);
void hinic3_shash_clear_free_data(struct shash *sh);

#endif
