/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Huawei Technologies Co., Ltd
 */

#ifndef _HINIC3_STRING_UTIL_H_
#define _HINIC3_STRING_UTIL_H_
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#define HINIC3_HW_UUID_LEN 17
#define HINIC3_HW_UFID_STR_LEN 8
#define HINIC3_HW_UFID_SHFIT 32

int hinic3_hexit_value(unsigned char c);
uintmax_t hinic3_hexits_value(const char *s, size_t n, bool *ok);
int hinic3_parse_hw_ufid_from_string(const char *str, uint64_t *ufid);
int hinic3_parse_uint32_from_string(const char *str, uint32_t *result);

#endif
