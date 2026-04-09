/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */

#ifndef __HINIC3_CMD_EXEC_H__
#define __HINIC3_CMD_EXEC_H__

#include "hinic3_command.h"
#include "hinic3_util.h"

void unixctl_hinic3_lib_cmd_exec(struct unixctl_conn *conn, int argc HINIC3_UNUSED, const char *argv[],
    void *aux HINIC3_UNUSED);
void unixctl_hinic3_cmd_exec_register(void);
#endif
