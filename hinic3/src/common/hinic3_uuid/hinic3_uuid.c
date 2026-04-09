/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2024 Huawei Technologies Co., Ltd
 */

#include <stdbool.h>
#include <string.h>
#include "hinic3_string_util.h"
#include "hinic3_uuid.h"

#define HINIC3_STR_TO_HEX_HEAD_TAIL_SIZE 8
#define HINIC3_STR_TO_HEX_MIDDLE_SIZE 4
#define HINIC3_STR_TO_HEX_MIDDLE_LEFT_SHIFIT 16
#define HINIC3_UUID_PART_ONE 0
#define HINIC3_UUID_PART_TWO 1
#define HINIC3_UUID_PART_THREE 2
#define HINIC3_UUID_PART_FOUR 3

void hinic3_uuid_zero(struct uuid *uuid)
{
    *uuid = UUID_ZERO;
}
bool hinic3_uuid_from_string(struct uuid *uuid, const char *s)
{
    if (!hinic3_uuid_from_string_prefix(uuid, s)) {
        return false;
    } else if (s[UUID_LEN] != '\0') {
        hinic3_uuid_zero(uuid);
        return false;
    } else {
        return true;
    }
}

bool hinic3_uuid_from_string_prefix(struct uuid *uuid, const char *s)
{
    bool ok;
    int offset = 0;
    int check_offset = HINIC3_STR_TO_HEX_HEAD_TAIL_SIZE;

    if (strlen(s) != UUID_LEN) {
        goto error;
    }

    uuid->parts[HINIC3_UUID_PART_ONE] = hinic3_hexits_value(s + offset, HINIC3_STR_TO_HEX_HEAD_TAIL_SIZE, &ok);
    if (!ok || s[check_offset] != '-') {
        goto error;
    }

    offset += (HINIC3_STR_TO_HEX_HEAD_TAIL_SIZE + 1);
    check_offset += (HINIC3_STR_TO_HEX_MIDDLE_SIZE + 1);
    uuid->parts[HINIC3_UUID_PART_TWO] =
        hinic3_hexits_value(s + offset, HINIC3_STR_TO_HEX_MIDDLE_SIZE, &ok) << HINIC3_STR_TO_HEX_MIDDLE_LEFT_SHIFIT;
    if (!ok || s[check_offset] != '-') {
        goto error;
    }

    offset += (HINIC3_STR_TO_HEX_MIDDLE_SIZE + 1);
    check_offset += (HINIC3_STR_TO_HEX_MIDDLE_SIZE + 1);
    uuid->parts[HINIC3_UUID_PART_TWO] += hinic3_hexits_value(s + offset, HINIC3_STR_TO_HEX_MIDDLE_SIZE, &ok);
    if (!ok || s[check_offset] != '-') {
        goto error;
    }

    offset += (HINIC3_STR_TO_HEX_MIDDLE_SIZE + 1);
    check_offset += (HINIC3_STR_TO_HEX_MIDDLE_SIZE + 1);
    uuid->parts[HINIC3_UUID_PART_THREE] =
        hinic3_hexits_value(s + offset, HINIC3_STR_TO_HEX_MIDDLE_SIZE, &ok) << HINIC3_STR_TO_HEX_MIDDLE_LEFT_SHIFIT;
    if (!ok || s[check_offset] != '-') {
        goto error;
    }

    offset += (HINIC3_STR_TO_HEX_MIDDLE_SIZE + 1);
    uuid->parts[HINIC3_UUID_PART_THREE] += hinic3_hexits_value(s + offset, HINIC3_STR_TO_HEX_MIDDLE_SIZE, &ok);
    if (!ok) {
        goto error;
    }

    offset += (HINIC3_STR_TO_HEX_MIDDLE_SIZE);
    uuid->parts[HINIC3_UUID_PART_FOUR] = hinic3_hexits_value(s + offset, HINIC3_STR_TO_HEX_HEAD_TAIL_SIZE, &ok);
    if (!ok) {
        goto error;
    }
    return true;

    error:
        hinic3_uuid_zero(uuid);
        return false;
}
