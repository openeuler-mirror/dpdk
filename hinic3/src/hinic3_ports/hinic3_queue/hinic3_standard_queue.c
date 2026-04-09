/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2025 Huawei Technologies Co., Ltd
 */

#include "hinic3_port_util.h"
#include "hinic3_standard_queue.h"

int
hinic3_standard_queue_valid(const struct hinic3_standard_queue *stdqueue)
{
    if (HINIC3_UNLIKELY(stdqueue == NULL))
        return -EINVAL;

    if (HINIC3_UNLIKELY(stdqueue->queue_info == NULL))
        return -EINVAL;

    if (HINIC3_UNLIKELY(stdqueue->hiovs_queue_id == INVALID_UPCALL_QUEUE_ID))
        return -EINVAL;

    return 0;
}
