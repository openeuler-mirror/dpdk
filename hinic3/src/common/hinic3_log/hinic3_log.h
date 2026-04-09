/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_LOG_H
#define HINIC3_LOG_H

#include <time.h>
#include "rte_log.h"
#include "hinic3_init_arg.h"
#include "hinic3_mutex.h"
#include "hinic3_ds.h"

#define HINIC3_MAX_LOG_NAME 16

#define HINIC3_DEFAULT_MAJOR_VERSION_STRING "(null)"
#define HINIC3_DEFAULT_SUB_VERSION_STRING "(null)"

#ifdef BUILD_MAJOR_VERSION
#define HINIC3_BUILD_MAJOR_VERSION BUILD_MAJOR_VERSION
#else
#define HINIC3_BUILD_MAJOR_VERSION HINIC3_DEFAULT_MAJOR_VERSION_STRING
#endif

#ifdef BUILD_SUB_VERSION
#define HINIC3_BUILD_SUB_VERSION BUILD_SUB_VERSION
#else
#define HINIC3_BUILD_SUB_VERSION HINIC3_DEFAULT_SUB_VERSION_STRING
#endif

enum hinic3_log_module_index {
    HINIC3_LOG_AGENT = 0,
    HINIC3_LOG_VPORT,
    HINIC3_LOG_CAPTURE,
    HINIC3_LOG_FLOW,
    HINIC3_LOG_DRIVER,
    HINIC3_LOG_FILTER,
    HINIC3_LOG_QOS,
    HINIC3_LOG_PACKET,
    HINIC3_LOG_POLICY,
    HINIC3_LOG_MAX,
};

enum hinic3_driver_log_module_index {
    HINIC3_DRIVER_LOG_VPORT = 1,
    HINIC3_DRIVER_LOG_BOND,
    HINIC3_DRIVER_LOG_FLOW,
    HINIC3_DRIVER_LOG_BUM,
    HINIC3_DRIVER_LOG_QOS,
    HINIC3_DRIVER_LOG_CHIP,
    HINIC3_DRIVER_LOG_PACKET,
    HINIC3_DRIVER_LOG_MAX,
};

enum hinic3_log_type_e {
    HINIC3_LOG_TYPE_UP,
    HINIC3_LOG_TYPE_DRIVER,
    HINIC3_LOG_TYPE_FLEXDA,
    HINIC3_LOG_TYPE_UNUSED,
};

typedef int (*set_log_level_sub_key_parse_func)(const char *value, struct ds *ds);

struct set_log_level_sub_key_parser {
    const char *key_name;
    size_t key_len;
    set_log_level_sub_key_parse_func func;
};

struct hinic3_log_module {
    const char *name;
    uint32_t type;
    uint32_t index;
    int def_rte_level;
    struct timespec timeout_ts;
};

struct set_log_level_input_key {
    struct hinic3_log_module *module;
    int log_level;
    time_t duration;
};

struct token_bucket {
    unsigned int rate;
    unsigned int burst;
    unsigned int tokens;
    long long int last_fill;
};

struct vlog_rate_limit {
    struct token_bucket token_bucket;
    long long int first_dropped;
    long long int last_dropped;
    unsigned int n_dropped;
    struct hinic3_mutex mutex;
};

struct hinic3_log_level_map {
    int dpdk_level;
    int hinic3_level;
    char level_name[HINIC3_MAX_LOG_NAME];
};

struct hinic3_log_timeline {
    struct timespec timeout_ts;
    time_t duration;
};

int hinic3_log_init(void);
int hinic3_driver_log_init(void);
void unixctl_hinic3_cmd_log_register(void);
int hinic3_log_limit(uint32_t level, uint32_t logtype, const char *format, ...);
bool hinic3_is_flow_debug(void);
int hinic3_log_flexda_limit(uint32_t level, uint32_t logtype, const char *format, ...);
int hinic3_log_flexda(uint32_t level, uint32_t module, const char *format, ...);
int hinic3_log_module_register(char *name);

#define HINIC3_LOG(l, t, format, ...)                                              \
    hinic3_log_limit(RTE_LOG_ ## l, HINIC3_LOG_ ## t, "[%s]" # t ": " format "\n", \
        hinic3_log_prefix_get(), ## __VA_ARGS__)
#endif
