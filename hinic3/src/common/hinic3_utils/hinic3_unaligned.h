/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Huawei Technologies Co., Ltd
 */

#ifndef _HINIC3_UNALIGNED_H_
#define _HINIC3_UNALIGNED_H_

#include "hinic3_types.h"
#define HINIC3_SHIFTS_LEN 16

static inline uint32_t hinic3_get_16aligned_be32(const hinic3_16aligned_be32 *x)
{
    return ((uint32_t)x->lo << HINIC3_SHIFTS_LEN) | x->hi;
}

#endif
