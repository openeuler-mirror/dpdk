/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_AGENT_FLOW_TIME_H
#define HINIC3_AGENT_FLOW_TIME_H
#include <stdint.h>
#include <pthread.h>
#include "hinic3_mutex.h"
#include "hinic3_util.h"
#include "hinic3_command.h"
#ifdef __cplusplus
extern "C" {
#endif

#define HINIC3_STAT_REF_TIME_MIN_SAMPLE    1  /* 获取统计信息全量刷新时间 最小采集次数 */
#define HINIC3_STAT_REF_TIME_MAX_SAMPLE    300  /* 获取统计信息全量刷新时间 最大采集次数 */
#define HINIC3_STAT_REF_TIME_GET_INTERVAL  1000  /* 获取统计信息全量刷新时间 采集间隔 */
#define HINIC3_STAT_REF_TIME_GET_TIMEOUT  5  /* 获取统计信息全量刷新时间 超时时间 */
#define HINIC3_TIME_MIN_SAMPLE    1  /* 获取流表卸载时间 最小采集次数 */
#define HINIC3_TIME_MAX_SAMPLE    300  /* 获取流表卸载时间 最大采集次数 */
#define HINIC3_TIME_GET_INTERVAL  1000  /* 获取流表卸载时间 采集间隔 */
#define HINIC3_TIME_GET_TIMEOUT  5  /* 获取流表卸载时间 超时时间 */
#define SECOND_TO_USECOND 1000000 /* 1s = 1e6 us */

enum {
    HINIC3_STAT_REF_TIME_TASK_CMD_STOP = 1,
    HINIC3_STAT_REF_TIME_TASK_CMD_RESTART,
};

enum {
    HINIC3_TIME_TASK_CMD_STOP = 1,
    HINIC3_TIME_TASK_CMD_RESTART,
};

enum {
    HINIC3_STAT_REF_TIME_TASK_ST_SAMPLING = 1,
    HINIC3_STAT_REF_TIME_TASK_ST_IDLE,
};

enum {
    HINIC3_TIME_TASK_ST_SAMPLING = 1,
    HINIC3_TIME_TASK_ST_IDLE,
};

struct hinic3_time_cmd_param {
    uint32_t total_samples;
};

struct hinic3_stat_refresh_time {
    uint32_t thread_id;
    double time;
};

struct hinic3_time_measure_task_basic {
    bool alive;
    int  status;
    int command;
    uint32_t total_samples;
    uint32_t count;
    pthread_t thread;
    pthread_cond_t cond;
    struct hinic3_mutex mutex;
};

struct hinic3_stat_ref_time_measure_task {
    struct hinic3_time_measure_task_basic basic;
    struct hinic3_stat_refresh_time time_list[HINIC3_STAT_REF_TIME_MAX_SAMPLE];
};

struct hinic3_time {
    uint32_t record;
    double time;
};

struct hinic3_time_measure_task {
    struct hinic3_time_measure_task_basic basic;
    struct hinic3_time time_list[HINIC3_TIME_MAX_SAMPLE];
};

bool hinic3_is_offload_measure_alive(void);
bool hinic3_is_stat_scan_measure_alive(void);
#ifdef __cplusplus
}
#endif

#endif
