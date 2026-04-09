/* Copyright(c) 2023 Huawei Technologies Co., Ltd
 * 
 * This file contains code segments derived from Nicira, Inc.
 * Original copyright notice:
 * 
 * Copyright (c) 2012, 2014, 2015, 2016 Nicira, Inc.
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

#ifndef _HINIC3_SMAP_H_
#define _HINIC3_SMAP_H_

#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include "hinic3_map.h"
#include "hinic3_hmap.h"

#define HINIC3_SMAP_FOR_EACH(SMAP_NODE, SMAP)                                  \
    HINIC3_HMAP_FOR_EACH_INIT (SMAP_NODE, node, &(SMAP)->map)

#define HINIC3_SMAP_FOR_EACH_SAFE(SMAP_NODE, NEXT, SMAP)           \
    HINIC3_HMAP_FOR_EACH_SAFE_INIT (   SMAP_NODE, NEXT, node, &(SMAP)->map)

void hinic3_smap_init(struct smap *smap);
void hinic3_smap_destroy(struct smap *smap);
struct smap_node *hinic3_smap_add(struct smap *smap, const char *key, const char *value, enum hinic3_module module_id);
bool hinic3_smap_equal(const struct smap *smap1, const struct smap *smap2);
void hinic3_smap_clone(struct smap *dst, const struct smap *src, enum hinic3_module module_id);
const char *hinic3_smap_get(const struct smap *smap, const char *key);
size_t hinic3_smap_count(const struct smap *smap);
void hinic3_smap_clear(struct smap *smap);
void hinic3_smap_add_format(enum hinic3_module module_id, struct smap *smap, const char *key, const char *format, ...);
#endif
