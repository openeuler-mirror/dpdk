/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Huawei Technologies Co., Ltd
 */

#ifndef _HINIC3_DS_H_
#define _HINIC3_DS_H_
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include "hinic3_meminfo.h"

#define HINIC3_MAX_DS_SIZE (1 << 20) // max print string size 1M
#define HINIC3_LOG_HEAD_LEN 30
#define HINIC3_INIT_DS_SIZE 128

struct ds {
    char *string;
    size_t length;
    size_t allocated;
};

#define DS_EMPTY_INITIALIZER { NULL, 0, 0 }

void hinic3_ds_put_cstr(struct ds *ds, const char *s);
void hinic3_ds_put_format_valist(struct ds *ds, const char *format, va_list args);
void hinic3_ds_put_format(struct ds *ds, const char *format, ...);
/* 带缩进和前缀的格式化函数，indent_num为行首缩进数， prefix为前缀字符串 */
void hinic3_ds_put_format_prefix(struct ds *ds, uint8_t indent_num, const char *prefix, const char *format, ...);
void hinic3_ds_destroy(struct ds *ds);
const char *hinic3_ds_cstr(struct ds *ds);

void hinic3_ds_clear(struct ds *ds);
char *hinic3_ds_put_uninit(struct ds *ds, size_t n);
void hinic3_ds_put_buffer(struct ds *ds, const char *s, size_t n);
void hinic3_ds_concat(struct ds *dst_ds, struct ds *src_ds);

#endif
