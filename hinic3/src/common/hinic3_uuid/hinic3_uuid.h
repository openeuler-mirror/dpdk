/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2024 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_UUID_H
#define HINIC3_UUID_H

struct uuid {
    uint32_t parts[4];
};

#define UUID_LEN 36
#define UUID_ZERO ((struct uuid) { .parts = { 0, 0, 0, 0 } })
#define UUID_BIT 128            /* Number of bits in a UUID. */
#define UUID_OCTET (UUID_BIT / 8) /* Number of bytes in a UUID. */
#define UUID_FMT "%08x-%04x-%04x-%04x-%04x%08x"
#define UUID_ARGS(UUID)                             \
    ((unsigned int) ((UUID)->parts[0])),            \
    ((unsigned int) ((UUID)->parts[1] >> 16)),      \
    ((unsigned int) ((UUID)->parts[1] & 0xffff)),   \
    ((unsigned int) ((UUID)->parts[2] >> 16)),      \
    ((unsigned int) ((UUID)->parts[2] & 0xffff)),   \
    ((unsigned int) ((UUID)->parts[3]))

void hinic3_uuid_zero(struct uuid *uuid);
bool hinic3_uuid_from_string(struct uuid *uuid, const char *s);
bool hinic3_uuid_from_string_prefix(struct uuid *uuid, const char *s);

#endif
