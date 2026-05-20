/* Copyright(c) 2023 Huawei Technologies Co., Ltd
 * 
 * This file contains code segments derived from Nicira, Inc.
 * Original copyright notice:
 * 
 * Copyright (c) 2009, 2010, 2011, 2012 Nicira, Inc.
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

#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include "hinic3_hash.h"
#include "hinic3_string_util.h"
#include "hinic3_util.h"
#include "hinic3_hmap.h"
#include "hinic3_meminfo.h"
#include "hinic3_shash.h"

static struct shash_node *hinic3_shash_find__(const struct shash*,
    const char *name, size_t name_len, size_t hash);

static size_t hinic3_hash_name(const char *name)
{
    return hinic3_hash_string(name, 0);
}

void hinic3_shash_init(struct shash *sh)
{
    hinic3_hmap_init(&sh->map);
}

static void hinic3_shash_clear(struct shash *sh)
{
    struct shash_node *node, *next;

    HINIC3_SHASH_FOR_EACH_SAFE (node, next, sh) {
        hinic3_hmap_remove(&sh->map, &node->node);
        hinic3_free(node->name);
        hinic3_free(node);
    }
}

void hinic3_shash_destroy(struct shash *sh)
{
    if (sh) {
        hinic3_shash_clear(sh);
        hinic3_hmap_destroy(&sh->map);
    }
}

void hinic3_shash_destroy_free_data(struct shash *sh)
{
    if (sh) {
        hinic3_shash_clear_free_data(sh);
        hinic3_hmap_destroy(&sh->map);
    }
}

void hinic3_shash_clear_free_data(struct shash *sh)
{
    struct shash_node *node, *next;

    HINIC3_SHASH_FOR_EACH_SAFE (node, next, sh) {
        hinic3_hmap_remove(&sh->map, &node->node);
        hinic3_free((void *)(uintptr_t)node->data);
        hinic3_free(node->name);
        hinic3_free(node);
    }
}

static struct shash_node* hinic3_shash_add_nocopy__(struct shash *sh, char *name,
    const void *data, size_t hash, enum hinic3_module module_id)
{
    struct shash_node *node = NULL;

    node = hinic3_xmalloc(sizeof (struct shash_node), module_id);
    if (node == NULL) {
        return NULL;
    }

    node->name = name;
    node->data = data;

    hinic3_hmap_insert(&sh->map, &node->node, hash, module_id);

    return node;
}

static struct shash_node* hinic3_shash_add_nocopy(struct shash *sh, char *name, const void *data, enum hinic3_module module_id)
{
    return hinic3_shash_add_nocopy__(sh, name, data, hinic3_hash_name(name), module_id);
}

static struct shash_node* hinic3_shash_add(struct shash *sh, const char *name, const void *data, enum hinic3_module module_id)
{
    return hinic3_shash_add_nocopy(sh, hinic3_xstrdup(name, module_id), data, module_id);
}

bool hinic3_shash_add_once(struct shash *sh, const char *name, const void *data, enum hinic3_module module_id)
{
    if (!hinic3_shash_find(sh, name)) {
        hinic3_shash_add(sh, name, data, module_id);
        return true;
    } else {
        return false;
    }
}

char* hinic3_shash_steal(struct shash *sh, struct shash_node *node)
{
    char *name = node->name;

    hinic3_hmap_remove(&sh->map, &node->node);
    hinic3_free(node);

    return name;
}

static struct shash_node* hinic3_shash_find__(const struct shash *sh, const char *name,
    size_t name_len, size_t hash)
{
    struct shash_node *node = NULL;

    HINIC3_HMAP_FOR_EACH_WITH_HASH (node, node, hash, &sh->map) {
        if (!strncmp(node->name, name, name_len) && !node->name[name_len]) {
            return node;
        }
    }

    return NULL;
}

struct shash_node* hinic3_shash_find(const struct shash *sh, const char *name)
{
    return hinic3_shash_find__(sh, name, strlen(name), hinic3_hash_name(name));
}

const void* hinic3_shash_find_data(const struct shash *sh, const char *name)
{
    struct shash_node *node = hinic3_shash_find(sh, name);
    return node ? node->data : NULL;
}