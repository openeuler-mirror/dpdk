/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */
#ifndef HINIC3_AGENT_FLOW_DFX_H
#define HINIC3_AGENT_FLOW_DFX_H
#include <stdint.h>
#include <pthread.h>
#include "hinic3_mutex.h"
#include "hinic3_driver_public.h"
#include "hinic3_command.h"
#include "hinic3_flow_dump_public.h"
#ifdef __cplusplus
extern "C" {
#endif

#define HINIC3_MAX_SPEED_SAMPLE    300
#define HINIC3_SPPED_MIN_INTERVAL  1
#define HINIC3_SPPED_MAX_INTERVAL  10
#define HINIC3_SPPED_MIN_SAMPLE    1
#define HINIC3_SPPED_MAX_SAMPLE    300
#define HOWFF_NO_FLOW_ERR (-4)
#define HINIC3_SHOW_API_NUM_PER_LINE 4
#define US_PER_MS 1000

enum {
    HINIC3_UFID_64_OPT = 1,
    HINIC3_FLOW_KEY_OPT,
    HINIC3_FLOW_FILE_OPT,
    HINIC3_STOP_DUMP_ALL_FLOWS_OPT,
    HINIC3_CHECK_DUMP_ALL_FLOWS_OPT,
    HINIC3_FLOW_CNT_OPT,
    HELP_OPT,
    HINIC3_FLOW_API_NAME_OPT,
    HINIC3_FLOW_ALL_OPT,
    HINIC3_FLOW_CLEAR_OPT,
    HINIC3_FLOW_BLOCK_ID_OPT,
    HINIC3_FLOW_TABLE_ID_OPT,
};

enum {
    HINIC3_SPEED_TASK_CMD_STOP = 1,
    HINIC3_SPEED_TASK_CMD_RESTART,
};

enum {
    HINIC3_SPEED_TASK_ST_SAMPLING = 1,
    HINIC3_SPEED_TASK_ST_IDLE,
};

struct hinic3_speed_cmd_param_t {
    uint32_t total_samples;
    uint32_t interval;
};

struct hinic3_speed_measure_task_t {
    bool alive;
    int  status;
    int command;
    uint32_t total_samples;
    uint32_t interval;
    uint32_t count;
    uint32_t sample_list[HINIC3_MAX_SPEED_SAMPLE];
    pthread_t      thread;
    pthread_cond_t cond;
    struct hinic3_mutex mutex;
};

void unixctl_hinic3_flow_cmd_init(void);

void hinic3_agent_show_flow_api(struct unixctl_conn *conn, int argc, const char *argv[], void *aux);
void hinic3_agent_show_flow_api(struct unixctl_conn *conn, int argc, const char *argv[], void *aux HINIC3_UNUSED);
void hinic3_speed_task_cmd(struct unixctl_conn *conn, int argc, const char *argv[], void *aux);

int hinic3_find_option(const char *name);

#ifdef __cplusplus
}
#endif

#endif
