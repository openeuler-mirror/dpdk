/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2025 Huawei Technologies Co., Ltd
 */
#ifndef HINIC3_CMD_ERROR_STATS_H
#define HINIC3_CMD_ERROR_STATS_H
#include <stdint.h>
#include "hinic3_flow_agent_enum.h"
#define HINIC3_ERROR_STRING_MAX_LEN 50

enum hinic3_errstat_print_level {
    HINIC3_ERRSTAT_PRINT_LEVEL_NULL = 0,
    HINIC3_ERRSTAT_PRINT_LEVEL_ERROR = 1,
    HINIC3_ERRSTAT_PRINT_LEVEL_WARNING = 2,
    HINIC3_ERRSTAT_PRINT_LEVEL_ALL = 3
};
void hinic3_flow_agent_error_stats_cmd(struct unixctl_conn *conn, int argc, const char *argv[], void *aux);
void unixctl_hinic3_cmd_error_stats_register(void);
#endif
