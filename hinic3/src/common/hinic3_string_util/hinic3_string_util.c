/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Huawei Technologies Co., Ltd
 */

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include "hinic3_util.h"
#include "hinic3_meminfo.h"
#include "hinic3_string_util.h"

#define LEFT_SHIFTING_FOUR 4

int hinic3_hexit_value(unsigned char c)
{
    static const signed char tbl[UCHAR_MAX + 1] = {
#define SHT(x)                                    \
    ((x) >= '0' && (x) <= '9'   ? (x) - '0'       \
     : (x) >= 'a' && (x) <= 'f' ? (x) - 'a' + 0xa \
     : (x) >= 'A' && (x) <= 'F' ? (x) - 'A' + 0xa \
                                : -1)
#define SHT0(x) SHT(x), SHT((x) + 1), SHT((x) + 2), SHT((x) + 3)
#define SHT1(x) SHT0(x), SHT0((x) + 4), SHT0((x) + 8), SHT0((x) + 12)
#define SHT2(x) SHT1(x), SHT1((x) + 16), SHT1((x) + 32), SHT1((x) + 48)
        SHT2(0), SHT2(64), SHT2(128), SHT2(192)};

    return tbl[c];
}

uintmax_t hinic3_hexits_value(const char *s, size_t n, bool *ok)
{
    uintmax_t val = 0;
    for (size_t index = 0; index < n; index++)
    {
        int hexit = hinic3_hexit_value(s[index]);
        if (hexit < 0)
        {
            *ok = false;
            return UINTMAX_MAX;
        }
        val = (val << LEFT_SHIFTING_FOUR) + hexit;
    }
    *ok = true;
    return val;
}

int hinic3_parse_hw_ufid_from_string(const char *str, uint64_t *ufid)
{
    const char *s = str;
    bool ok = false;

    if (strlen(s) < HINIC3_HW_UUID_LEN)
    {
        return -1;
    }

    *ufid = hinic3_hexits_value(s, HINIC3_HW_UFID_STR_LEN, &ok) << HINIC3_HW_UFID_SHFIT;
    if (!ok || s[HINIC3_HW_UFID_STR_LEN] != '-')
    {
        return -1;
    }

    *ufid += hinic3_hexits_value(s + HINIC3_HW_UFID_STR_LEN + 1, HINIC3_HW_UFID_STR_LEN, &ok);
    if (!ok)
    {
        return -1;
    }

    if (s[HINIC3_HW_UUID_LEN] != '\0')
    {
        return -1;
    }
    return 0;
}

int hinic3_parse_uint32_from_string(const char *str, uint32_t *result)
{
    char *end_ptr;
    errno = 0;
    const unsigned long temp_val = strtoul(str, &end_ptr, 0);
    if (errno == 0 && end_ptr != str && *end_ptr == '\0' && temp_val <= UINT32_MAX)
    {
        *result = (uint32_t)temp_val;
    }
    else
    {
        return -1;
    }
    return 0;
}
