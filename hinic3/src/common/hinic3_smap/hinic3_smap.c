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

#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include "hinic3_hash.h"
#include "hinic3_string_util.h"
#include "hinic3_util.h"
#include "hinic3_log.h"
#include "hinic3_meminfo.h"
#include "hinic3_smap.h"

static struct smap_node *hinic3_smap_add__(struct smap *, char *, void *,
    size_t hash, enum hinic3_module module_id);
static struct smap_node *hinic3_smap_find__(const struct smap *, const char *key,
    size_t key_len, size_t hash);

void hinic3_smap_init(struct smap *smap)
{
    hinic3_hmap_init(&smap->map);
}

void hinic3_smap_destroy(struct smap *smap)
{
    if (smap) {
        hinic3_smap_clear(smap);
        hinic3_hmap_destroy(&smap->map);
    }
}

struct smap_node *hinic3_smap_add(struct smap *smap, const char *key, const char *value, enum hinic3_module module_id)
{
    size_t key_len = strlen(key);

    return hinic3_smap_add__(smap, hinic3_xmemdup0(key, key_len, module_id), hinic3_xstrdup(value, module_id),
                      hinic3_hash_bytes(key, key_len, 0), module_id);
}

static struct smap_node *hinic3_smap_get_node(const struct smap *smap, const char *key)
{
    size_t key_len = strlen(key);
    return hinic3_smap_find__(smap, key, key_len, hinic3_hash_bytes(key, key_len, 0));
}

static void hinic3_smap_remove_node(struct smap *smap, struct smap_node *node)
{
    hinic3_hmap_remove(&smap->map, &node->node);
    hinic3_free(node->key);
    hinic3_free(node->value);
    hinic3_free(node);
}

void hinic3_smap_clear(struct smap *smap)
{
    struct smap_node *node, *next;

    HINIC3_SMAP_FOR_EACH_SAFE(node, next, smap) {
        hinic3_smap_remove_node(smap, node);
    }
}

static const char* hinic3_smap_get_def(const struct smap *smap, const char *key, const char *def)
{
    struct smap_node *node = hinic3_smap_get_node(smap, key);
    return node ? node->value : def;
}

const char* hinic3_smap_get(const struct smap *smap, const char *key)
{
    return hinic3_smap_get_def(smap, key, NULL);
}

size_t hinic3_smap_count(const struct smap *smap)
{
    return hinic3_hmap_count(&smap->map);
}

void hinic3_smap_clone(struct smap *dst, const struct smap *src, enum hinic3_module module_id)
{
    const struct smap_node *node;

    hinic3_smap_init(dst);
    HINIC3_SMAP_FOR_EACH(node, src) {
        hinic3_smap_add__(dst, hinic3_xstrdup(node->key, module_id), hinic3_xstrdup(node->value, module_id),
            node->node.hash, module_id);
    }
}

bool hinic3_smap_equal(const struct smap *smap1, const struct smap *smap2)
{
    const struct smap_node *node;

    if (hinic3_smap_count(smap1) != hinic3_smap_count(smap2)) {
        return false;
    }

    HINIC3_SMAP_FOR_EACH(node, smap1) {
        const char *value2 = hinic3_smap_get(smap2, node->key);
        if (!value2 || strcmp(node->value, value2)) {
            return false;
        }
    }
    return true;
}

static struct smap_node *hinic3_smap_add__(struct smap *smap, char *key, void *value, size_t hash,
    enum hinic3_module module_id)
{
    struct smap_node *node = hinic3_xmalloc(sizeof (struct smap_node), module_id);
    if (node == NULL) {
        HINIC3_LOG(ERR, AGENT, "Malloc failed when smap add node!");
        return NULL;
    }
    node->key = key;
    node->value = value;

    hinic3_hmap_insert(&smap->map, &node->node, hash, module_id);
    return node;
}

static void hinic3_smap_add_format_varg(struct smap *smap, const char *key, const char *format, va_list args,
    enum hinic3_module module_id)
{
    char *value;
    size_t key_len;

    value = hinic3_xvasprintf(format, args, module_id);
    key_len = strlen(key);
    hinic3_smap_add__(smap, hinic3_xmemdup0(key, key_len, module_id), value, hinic3_hash_bytes(key, key_len, 0),
        module_id);
}

static struct smap_node* hinic3_smap_find__(const struct smap *smap, const char *key,
    size_t key_len, size_t hash)
{
    struct smap_node *node;

    HINIC3_HMAP_FOR_EACH_WITH_HASH (node, node, hash, &smap->map) {
        if (!strncmp(node->key, key, key_len) && !node->key[key_len]) {
            return node;
        }
    }

    return NULL;
}

void hinic3_smap_add_format(enum hinic3_module module_id, struct smap *smap, const char *key, const char *format, ...)
{
    va_list args;

    va_start(args, format);

    hinic3_smap_add_format_varg(smap, key, format, args, module_id);

    va_end(args);
}
