/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Huawei Technologies Co., Ltd
 */

#include <stdlib.h>
#include "rte_flow.h"
#include "hinic3_init.h"
#include "hinic3_log.h"
#include "hinic3_meminfo.h"
#include "hinic3_flow_dump_public.h"

static dump_start g_hinic3_dump_start[HINIC3_FLOW_TYPE_MAX] = {0};
static dump_next g_hinic3_dump_next[HINIC3_FLOW_TYPE_MAX] = {0};
static dump_done g_hinic3_dump_done[HINIC3_FLOW_TYPE_MAX] = {0};

int hinic3_build_item_data(struct hinic3_dump_item_data *data,
    enum hinic3_dump_data_type type, size_t size)
{
    if (data == NULL) {
        HINIC3_LOG(ERR, FLOW, "flow dump build item data, input null ptr");
        return -1;
    }

    if (size == 0) {
        data->is_valid = true;
        return 0;
    }
    if (type == HINIC3_DUMP_DATA_TYPE_KEY) {
        if (data->key == NULL) {
            data->key = hinic3_calloc(1, size, HINIC3_FLOWS);
            if (data->key == NULL) {
                HINIC3_LOG(ERR, FLOW, "hinic3 flow dump, alloc key error");
                return -1;
            }
        }
    } else {
        if (data->mask == NULL) {
            data->mask = hinic3_calloc(1, size, HINIC3_FLOWS);
            if (data->mask == NULL) {
                HINIC3_LOG(ERR, FLOW, "hinic3  dump, alloc mask error");
                return -1;
            }
        }
    }
    data->is_valid = true;
    return 0;
}

void hinic3_clean_items_data(struct hinic3_dump_item_data items[], uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        items[i].is_valid = false;
        if (items[i].key != NULL) {
            hinic3_free(items[i].key);
            items[i].key = NULL;
        }
        if (items[i].mask != NULL) {
            hinic3_free(items[i].mask);
            items[i].mask = NULL;
        }
    }
}

void *hinic3_get_item_data(struct hinic3_dump_item_data *item,
    enum hinic3_dump_data_type type)
{
    if (type == HINIC3_DUMP_DATA_TYPE_KEY) {
        return item->key;
    } else {
        return item->mask;
    }
}

int hinic3_set_dump_start(enum hinic3_flow_type type, dump_start func)
{
    g_hinic3_dump_start[type] = func;
    return 0;
}

int hinic3_set_dump_next(enum hinic3_flow_type type, dump_next func)
{
    g_hinic3_dump_next[type] = func;
    return 0;
}

int hinic3_set_dump_done(enum hinic3_flow_type type, dump_done func)
{
    g_hinic3_dump_done[type] = func;
    return 0;
}

dump_start hinic3_get_dump_start(enum hinic3_flow_type type)
{
    if (type >= HINIC3_FLOW_TYPE_MAX) {
        return NULL;
    }
    return g_hinic3_dump_start[type];
}

dump_next hinic3_get_dump_next(enum hinic3_flow_type type)
{
    if (type >= HINIC3_FLOW_TYPE_MAX) {
        return NULL;
    }
    return g_hinic3_dump_next[type];
}

dump_done hinic3_get_dump_done(enum hinic3_flow_type type)
{
    if (type >= HINIC3_FLOW_TYPE_MAX) {
        return NULL;
    }
    return g_hinic3_dump_done[type];
}
