/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Huawei Technologies Co., Ltd
 */

#ifndef _HINIC3_COMMAND_H_
#define _HINIC3_COMMAND_H_

#include "stdio.h"

#define HINIC3_CMD_INFO_MAX_LEN 200
#define HINIC3_CMD_NAME_MAX_LEN 50

struct unixctl_conn {
    int sock_fd;
    FILE *sock_file;
};

typedef void unixctl_cb_func(struct unixctl_conn *, int argc, const char *argv[], void *aux);

struct hinic3_command {
    unixctl_cb_func *cb_func;
    int min_argc;
    int max_argc;
    const char *usage;
};

void hinic3_command_register(const char *name, const char *usage, int min_args, int max_args, unixctl_cb_func *cb,
    void *aux);
void hinic3_command_register_or_update(const char *name, const char *usage, int min_args, int max_args, unixctl_cb_func *cb,
    void *aux);
void hinic3_command_reply_error(struct unixctl_conn *conn, const char *error);
void hinic3_command_reply(struct unixctl_conn *conn, const char *body);

void hinic3_command_hmap_init(void);
int hinic3_command_mgr_init(void);
void hinic3_command_mgr_uninit(void);

struct hinic3_command_info {
    char func_name[HINIC3_CMD_NAME_MAX_LEN];
    char info[HINIC3_CMD_INFO_MAX_LEN];
};

enum hinic3_command_parameter_num_error_type {
    HINIC3_COMMAND_ERROR_TYPE_NULL = 1,
    HINIC3_COMMAND_ERROR_TYPE_EXCESSIVE,
    HINIC3_COMMAND_ERROR_TYPE_INSUFFICIENT,
};

#endif
