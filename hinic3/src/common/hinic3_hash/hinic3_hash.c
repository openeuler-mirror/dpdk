/* Copyright(c) 2023 Huawei Technologies Co., Ltd
 * 
 * This file contains code segments derived from Nicira, Inc.
 * Original copyright notice:
 * 
 * Copyright (c) 2008, 2009, 2010, 2012, 2013, 2014, 2016 Nicira, Inc.
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


#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <arpa/inet.h>
#include <arm_acle.h>
#include "hinic3_hash.h"

#define HINIC3_HALF_BYTE_SIZE 4
#define HINIC3_BYTE_SIZE 8
#define HINIC3_SHORT_SIZE 16
#define HINIC3_THREE_BYTE_SIZE 24
#define HINIC3_WORD_SIZE 32

uint32_t hinic3_hash_add(uint32_t hash, uint32_t data)
{
    return __crc32cw(hash, data);
}

uint32_t hinic3_hash_add64(uint32_t hash, uint64_t data)
{
    return __crc32cd(hash, data);
}

static inline uint32_t hinic3_hash_finish(uint32_t hash, uint64_t final)
{
    uint32_t hash_num = __crc32cd(hash, final) * 0x805204f3;
    return hash_num ^ (hash_num >> HINIC3_SHORT_SIZE);
}

static inline uint32_t get_unaligned_u32(const uint32_t *p_)
{
    enum array_index {HOST_ID_LOW_BIT = 0, HOST_ID_HIGH_BIT = 1, NET_ID_LOW_BIT = 2, NET_ID_HIGH_BIT = 3};
    const uint8_t *p = (const uint8_t *)p_;
    return ntohl((p[HOST_ID_LOW_BIT] << HINIC3_THREE_BYTE_SIZE) | (p[HOST_ID_HIGH_BIT] << HINIC3_SHORT_SIZE) |
                 (p[NET_ID_LOW_BIT] << HINIC3_BYTE_SIZE) | p[NET_ID_HIGH_BIT]);
}

uint32_t hinic3_hash_bytes(const void *point, size_t n, uint32_t basis)
{
    if (point == NULL || n > UINT32_MAX) {
        return hinic3_hash_finish(basis, 0);
    }
    const uint32_t *p = point;
    uint32_t n_32b = (uint32_t)n;
    uint32_t iter_n = n_32b;
    uint32_t hash;

    hash = basis;
    while (iter_n >= HINIC3_HALF_BYTE_SIZE) {
        hash = hinic3_hash_add(hash, get_unaligned_u32(p));
        iter_n -= HINIC3_HALF_BYTE_SIZE;
        p += 1;
    }

    if (iter_n > 0) {
        uint32_t tmp = 0;

        memcpy(&tmp, p, iter_n);
        hash = hinic3_hash_add(hash, tmp);
    }

    return hinic3_hash_finish(hash, n_32b);
}

uint32_t hinic3_hash_string(const char *s, uint32_t basis)
{
    return hinic3_hash_bytes(s, strlen(s), basis);
}

uint32_t hinic3_hash_2words(uint32_t x, uint32_t y)
{
    return hinic3_hash_finish(hinic3_hash_add(hinic3_hash_add(x, 0), y), HINIC3_BYTE_SIZE);
}


uint32_t hinic3_hash_int(uint32_t x, uint32_t basis)
{
    return hinic3_hash_2words(x, basis);
}

uint32_t hinic3_hash_uint64_basis(const uint64_t x, const uint32_t basis)
{
    return hinic3_hash_finish(hinic3_hash_add64(basis, x), HINIC3_BYTE_SIZE);
}

uint32_t hinic3_hash_uint64(const uint64_t x)
{
    return hinic3_hash_uint64_basis(x, 0);
}
