/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Huawei Technologies Co., Ltd
 */

#ifndef __HINIC3_OPTION_H__
#define __HINIC3_OPTION_H__
#define NO_ARGUMENT    0
#define REQUIRED_ARGUMENT 1
#define OPTIONAL_ARGUMENT 2

struct hinic3_opt {
    const char* name;
    int has_arg;
    int *flag;
    int val;
};

#endif
