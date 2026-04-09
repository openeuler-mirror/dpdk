/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2024 Huawei Technologies Co., Ltd
 */
#ifndef HINIC3_CMD_MEMINFO_H
#define HINIC3_CMD_MEMINFO_H

#include "hinic3_meminfo.h"
#include "hinic3_hugepage_meminfo.h"
#include "hinic3_command.h"

void hinic3_show_meminfo_command(struct unixctl_conn *conn, int argc, const char *argv[], void *aux);
void hinic3_show_hugepage_meminfo_command(struct unixctl_conn *conn, int argc, const char *argv[], void *aux HINIC3_UNUSED);
void unixctl_hinic3_cmd_meminfo_register(void);

#endif
