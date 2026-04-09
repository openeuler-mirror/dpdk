/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Huawei Technologies Co., Ltd
 */

#include "hinic3_ds.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "hinic3_log.h"
#include "hinic3_meminfo.h"

void hinic3_ds_destroy(struct ds *ds)
{
    if (ds->string != NULL) {
        hinic3_free(ds->string);
        ds->string = NULL;
    }
}

const char* hinic3_ds_cstr(struct ds *ds)
{
    if (ds == NULL) {
        return "\0";
    }
    return ds->string == NULL ? "\0" : ds->string;
}

static int hinic3_ds_reserve(struct ds *ds, size_t size)
{
    if (size > ds->allocated) {
        char *new_str = (char *)hinic3_calloc(1, size, HINIC3_COMMON_RESOURCE);
        if (new_str == NULL) {
            HINIC3_LOG(ERR, AGENT, "Ds reserve hinic3_calloc failed!");
            return -1;
        }

        memcpy(new_str, ds->string, ds->length);
        if (ds->string != NULL) {
            hinic3_free(ds->string);
        }

        ds->allocated = size;
        ds->string = new_str;
    }
    return 0;
}

void hinic3_ds_put_cstr(struct ds *ds, const char *s)
{
    hinic3_ds_put_format(ds, "%s", s);
}

void hinic3_ds_put_format_valist(struct ds *ds, const char *format, va_list args)
{
    int writed = -1;
    int ret;
    size_t size = ds->allocated;
    if (size == 0) {
        size = HINIC3_INIT_DS_SIZE;
    }

    while (writed < 0) {
        if (size > HINIC3_MAX_DS_SIZE) {
            char head[HINIC3_LOG_HEAD_LEN + 1] = {0};
            memcpy(head, ds->string, ds->length);
            HINIC3_LOG(ERR, AGENT, "Ds too long, prefix: %s, size:%d vs %d!", head, (int)size, HINIC3_MAX_DS_SIZE);
            return;
        }

        ret = hinic3_ds_reserve(ds, size);
        if (ret != 0) {
            HINIC3_LOG(ERR, AGENT, "Ds reserve failed, ret is %d!", ret);
            return;
        }

        size <<= 1;

        int left = ds->allocated - ds->length;
        va_list args_tmp;
        va_copy(args_tmp, args);

        writed = vsnprintf(&ds->string[ds->length], left, format, args_tmp);
        if (writed < left) {
            ds->length += writed;
        } else {
            writed = -1;
        }

        va_end(args_tmp);
    }
}

void hinic3_ds_put_format(struct ds *ds, const char *format, ...)
{
    va_list args;

    va_start(args, format);
    hinic3_ds_put_format_valist(ds, format, args);
    va_end(args);
}

void hinic3_ds_put_format_prefix(struct ds *ds, uint8_t indent_num, const char *prefix, const char *format, ...)
{
    va_list args;

    for (uint8_t i = 0; i < indent_num; i++) {
        hinic3_ds_put_format(ds, "%s", " ");
    }
    hinic3_ds_put_format(ds, "%s", prefix);

    va_start(args, format);
    hinic3_ds_put_format_valist(ds, format, args);
    va_end(args);
}

void hinic3_ds_clear(struct ds *ds)
{
    ds->length = 0;
}

char *hinic3_ds_put_uninit(struct ds *ds, size_t n)
{
    int ret;
    ret = hinic3_ds_reserve(ds, ds->length + n);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "Ds reserve failed, err is %d!", ret);
        return NULL;
    }

    ds->length += n;
    ds->string[ds->length - 1] = '\0';
    return &ds->string[ds->length - n];
}

void hinic3_ds_put_buffer(struct ds *ds, const char *s, size_t n)
{
    memcpy(hinic3_ds_put_uninit(ds, n), s, n);
}

void hinic3_ds_concat(struct ds *dst_ds, struct ds *src_ds)
{
    hinic3_ds_put_buffer(dst_ds, src_ds->string, src_ds->length);
}
