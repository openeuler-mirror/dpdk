/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */

#include "hinic3_log.h"

#include <stdbool.h>
#include <string.h>
#include "rte_log.h"
#include "hinic3_util.h"
#include "hinic3_iface_global.h"
#include "hinic3_command.h"
#include "hinic3_option.h"
#include "hinic3_ui_string.h"
#include "hinic3_parse_agent_config.h"
#include "hinic3_timeval.h"

#define SET_LOG_HELP_ARG_NUM 2
#define SET_LOG_MODULE_ARG_NUM 7
#define HINIC3_LOG_MSG_TOKENS (60 * 1000)
#define HINIC3_LOG_RATE_TOKENS  60
#define HINIC3_LOG_BURST_TOKENS 1000
#define TOKEN_BUCKET_INIT(RATE, BURST) { RATE, BURST, 0, LLONG_MIN }
#define SHOW_MAX_NAME_ONE_LINE 4
#define HINIC3_MODULE_NAME_MAX_LEN 30
#define ARGC_ADD_DOUBLE 2
#define TIMESPEC_INITIALIZER {.tv_sec = 0, .tv_nsec = 0}

#define FLEXDA_LOG_PREFIX "[flexda_ovs_adapter]"
#define MAX_BUF_SIZE 512

#define HINIC3_SAT_MUL(X, Y)                                               \
    ((Y) == 0 ? 0                                                       \
     : (X) <= UINT_MAX / (Y) ? (unsigned int) (X) * (unsigned int) (Y)  \
     : UINT_MAX)

#define HINIC3_LOG_RATE_LIMIT_INIT(RATE, BURST)                                 \
        {                                                                 \
            TOKEN_BUCKET_INIT(RATE, HINIC3_SAT_MUL(BURST, HINIC3_LOG_MSG_TOKENS)), \
            0,                              /* first_dropped */           \
            0,                              /* last_dropped */            \
            0,                              /* n_dropped */               \
            HINIC3_MUTEX_INITIALIZER           /* mutex */                   \
        }

enum hinic3_log_level {
    HINIC3_LOG_EMERG_LEVEL,
    HINIC3_LOG_ALERT_LEVEL,
    HINIC3_LOG_CRITICAL_LEVEL,
    HINIC3_LOG_ERR_LEVEL,
    HINIC3_LOG_WARNING_LEVEL,
    HINIC3_LOG_NOTICE_LEVEL,
    HINIC3_LOG_INFO_LEVEL,
    HINIC3_LOG_DEBUG_LEVEL,
    HINIC3_LOG_MAX_LEVEL,
};

static struct hinic3_log_module g_hinic3_log_module_list[] = {
    {"hwoff-agent",          HINIC3_LOG_TYPE_UP,      HINIC3_LOG_AGENT,         RTE_LOG_INFO, TIMESPEC_INITIALIZER},
    {"hwoff-vport",          HINIC3_LOG_TYPE_UP,      HINIC3_LOG_VPORT,         RTE_LOG_INFO, TIMESPEC_INITIALIZER},
    {"hwoff-capture",        HINIC3_LOG_TYPE_UP,      HINIC3_LOG_CAPTURE,       RTE_LOG_INFO, TIMESPEC_INITIALIZER},
    {"hwoff-flow",           HINIC3_LOG_TYPE_UP,      HINIC3_LOG_FLOW,          RTE_LOG_INFO, TIMESPEC_INITIALIZER},
    {"hwoff-security-filter", HINIC3_LOG_TYPE_UP,      HINIC3_LOG_FILTER,        RTE_LOG_INFO, TIMESPEC_INITIALIZER},
    {"hwoff-driver",         HINIC3_LOG_TYPE_UP,      HINIC3_LOG_DRIVER,        RTE_LOG_INFO, TIMESPEC_INITIALIZER},
    {"hwoff-qos",            HINIC3_LOG_TYPE_UP,      HINIC3_LOG_QOS,           RTE_LOG_INFO, TIMESPEC_INITIALIZER},
    {"hwoff-packet",         HINIC3_LOG_TYPE_UP,      HINIC3_LOG_PACKET,        RTE_LOG_INFO, TIMESPEC_INITIALIZER},
    {"hwoff-offload-policy", HINIC3_LOG_TYPE_UP,      HINIC3_LOG_POLICY,        RTE_LOG_INFO, TIMESPEC_INITIALIZER},
    {"hwoff-driver-vport",   HINIC3_LOG_TYPE_DRIVER,  HINIC3_DRIVER_LOG_VPORT,  RTE_LOG_INFO, TIMESPEC_INITIALIZER},
    {"hwoff-driver-bond",    HINIC3_LOG_TYPE_DRIVER,  HINIC3_DRIVER_LOG_BOND,   RTE_LOG_INFO, TIMESPEC_INITIALIZER},
    {"hwoff-driver-flow",    HINIC3_LOG_TYPE_DRIVER,  HINIC3_DRIVER_LOG_FLOW,   RTE_LOG_INFO, TIMESPEC_INITIALIZER},
    {"hwoff-driver-bum",     HINIC3_LOG_TYPE_DRIVER,  HINIC3_DRIVER_LOG_BUM,    RTE_LOG_INFO, TIMESPEC_INITIALIZER},
    {"hwoff-driver-qos",     HINIC3_LOG_TYPE_DRIVER,  HINIC3_DRIVER_LOG_QOS,    RTE_LOG_INFO, TIMESPEC_INITIALIZER},
    {"hwoff-driver-chip",    HINIC3_LOG_TYPE_DRIVER,  HINIC3_DRIVER_LOG_CHIP,   RTE_LOG_INFO, TIMESPEC_INITIALIZER},
    {"hwoff-driver-packet",  HINIC3_LOG_TYPE_DRIVER,  HINIC3_DRIVER_LOG_PACKET, RTE_LOG_INFO, TIMESPEC_INITIALIZER},
};

static struct hinic3_log_level_map g_hinic3_log_level_map[] = {
    {0,                0,                         "disabled"},
    {RTE_LOG_EMERG,    HINIC3_LOG_EMERG_LEVEL,    "emergency"},
    {RTE_LOG_ALERT,    HINIC3_LOG_ALERT_LEVEL,    "alert"},
    {RTE_LOG_CRIT,     HINIC3_LOG_CRITICAL_LEVEL, "critical"},
    {RTE_LOG_ERR,      HINIC3_LOG_ERR_LEVEL,      "error"},
    {RTE_LOG_WARNING,  HINIC3_LOG_WARNING_LEVEL,  "warning"},
    {RTE_LOG_NOTICE,   HINIC3_LOG_NOTICE_LEVEL,   "notice"},
    {RTE_LOG_INFO,     HINIC3_LOG_INFO_LEVEL,     "info"},
    {RTE_LOG_DEBUG,    HINIC3_LOG_DEBUG_LEVEL,    "debug"},
};

static int g_hinic3_log_module_dpdk_id[HINIC3_LOG_MAX];
static int g_hinic3_driver_log_module_dpdk_id[HINIC3_DRIVER_LOG_MAX];
static struct set_log_level_input_key g_set_log_level_input_key = {0};
static struct hinic3_log_timeline g_log_limit_timeline = {0};

static struct hinic3_log_module g_hinic3_flexda_log_module_list[HINIC3_LOG_MAX] = {
    {NULL, HINIC3_LOG_TYPE_UNUSED, HINIC3_LOG_MAX, RTE_LOG_INFO},
    {NULL, HINIC3_LOG_TYPE_UNUSED, HINIC3_LOG_MAX, RTE_LOG_INFO},
    {NULL, HINIC3_LOG_TYPE_UNUSED, HINIC3_LOG_MAX, RTE_LOG_INFO},
    {NULL, HINIC3_LOG_TYPE_UNUSED, HINIC3_LOG_MAX, RTE_LOG_INFO},
    {NULL, HINIC3_LOG_TYPE_UNUSED, HINIC3_LOG_MAX, RTE_LOG_INFO},
    {NULL, HINIC3_LOG_TYPE_UNUSED, HINIC3_LOG_MAX, RTE_LOG_INFO},
    {NULL, HINIC3_LOG_TYPE_UNUSED, HINIC3_LOG_MAX, RTE_LOG_INFO},
    {NULL, HINIC3_LOG_TYPE_UNUSED, HINIC3_LOG_MAX, RTE_LOG_INFO},
};

static char g_hinic3_flexda_name_buffer[HINIC3_LOG_MAX][HINIC3_MODULE_NAME_MAX_LEN] = {0};
static int g_hinic3_flexda_log_module_dpdk_id[HINIC3_LOG_MAX];
static const char *g_hinic3_log_index_map[HINIC3_LOG_MAX] = {
    "AGENT", "VPORT", "CAPTURE", "FLOW", "FILTER", "DRIVER", "QOS", "PACKET", "POLICY"
};

int hinic3_log_init(void)
{
    size_t i;
    int ret;
    int log_id;
    struct hinic3_log_module *module = NULL;

    clock_gettime(CLOCK_MONOTONIC, &g_log_limit_timeline.timeout_ts);
    g_log_limit_timeline.duration = 0;
    for (i = 0; i < ARRAY_SIZE(g_hinic3_log_module_list); i++) {
        module = &g_hinic3_log_module_list[i];
        if (module->type == HINIC3_LOG_TYPE_DRIVER) {
            continue;
        }
        log_id = rte_log_register(module->name);
        if (log_id < 0) {
            return -1;
        }

        if (module->type == HINIC3_LOG_TYPE_UP) {
            g_hinic3_log_module_dpdk_id[module->index] = log_id;
        }

        /* set default level */
        ret = rte_log_set_level(log_id, module->def_rte_level);
        if (ret < 0) {
            return -1;
        }
    }

    return 0;
}

int hinic3_driver_log_init(void)
{
    size_t i;
    int ret;
    int log_id;
    int hinic3_driver_level;
    struct hinic3_log_module *module = NULL;

    for (i = 0; i < ARRAY_SIZE(g_hinic3_log_module_list); i++) {
        module = &g_hinic3_log_module_list[i];
        if (module->type == HINIC3_LOG_TYPE_UP) {
            continue;
        }
        log_id = rte_log_register(module->name);
        if (log_id < 0) {
            return -1;
        }

        if (module->type == HINIC3_LOG_TYPE_DRIVER) {
            g_hinic3_driver_log_module_dpdk_id[module->index] = log_id;
        }

        /* set default level */
        ret = rte_log_set_level(log_id, module->def_rte_level);
        if (ret < 0) {
            HINIC3_LOG(ERR, AGENT, "Set log default level failed, module is %s, ret is %d!", module->name, ret);
            return -1;
        }

        ret = hinic3_global_open_log(module->index, 1);
        if (ret != 0) {
            HINIC3_LOG(ERR, AGENT, "Open driver log failed, module is %s, ret is %d!", module->name, ret);
            return -1;
        }

        hinic3_driver_level = g_hinic3_log_level_map[module->def_rte_level].hinic3_level;
        ret = hinic3_global_set_log_level(module->index, hinic3_driver_level);
        if (ret != 0) {
            HINIC3_LOG(ERR, AGENT, "Set driver log level failed, module is %s, ret is %d!", module->name, ret);
            return -1;
        }
    }

    return 0;
}

static int hinic3_set_dpdk_log_level(struct hinic3_log_module *module, int level)
{
    int ret = 0;
    int module_dpdk_id = 0;
 
    if (module->type == HINIC3_LOG_TYPE_UP) {
        module_dpdk_id = g_hinic3_log_module_dpdk_id[module->index];
    } else {
        module_dpdk_id = g_hinic3_driver_log_module_dpdk_id[module->index];
    }

    ret = rte_log_set_level(module_dpdk_id, level);
    if (ret < 0) {
        HINIC3_LOG(ERR, AGENT, "Set dpdk log level failed, ret is %d!", ret);
        return -1;
    }
 
    module->def_rte_level = level;
    return 0;
}

static int hinic3_set_driver_log_level(struct hinic3_log_module *module, int rte_level)
{
    int hinic3_driver_level = g_hinic3_log_level_map[rte_level].hinic3_level;
     
    //  组件侧不支持level为0的设置，由于dpak不使用error以上级别的日志等级
    // 当设置为emergency或者disabled时默认下发为alert。
    if (hinic3_driver_level == 0) {
        hinic3_driver_level = 1;
    }

    int ret = hinic3_global_set_log_level(module->index, hinic3_driver_level);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "Set driver log level failed, module is %s, ret is %d!", module->name, ret);
    }
    return ret;
}

static const char *hinic3_update_level_timeout(struct hinic3_log_module *module)
{
    if (g_set_log_level_input_key.duration == 0) {
        /* 0表示infinite不复位 */
        module->timeout_ts.tv_nsec = 0;
        module->timeout_ts.tv_sec = 0;
        return "infinite";
    }
 
    struct timespec current_ts = {0};
    clock_gettime(CLOCK_MONOTONIC, &current_ts);
 
    module->timeout_ts.tv_sec = current_ts.tv_sec + g_set_log_level_input_key.duration;
    module->timeout_ts.tv_nsec = current_ts.tv_nsec;
 
    return hinic3_transform_duration_to_string(g_set_log_level_input_key.duration);
}

static int hinic3_set_module_log_level(void)
{
    int ret = 0;
    int rte_level = g_set_log_level_input_key.log_level;
    const char *duration_str = NULL;
    struct hinic3_log_module *module_ins = g_set_log_level_input_key.module;
    int old_rte_level = module_ins->def_rte_level;
 
    if ((unsigned int)rte_level > RTE_LOG_DEBUG) {
        HINIC3_LOG(ERR, AGENT, "Invalid log level is %d!", rte_level);
        return -1;
    }

    ret = hinic3_set_dpdk_log_level(module_ins, rte_level);
    if (ret < 0) {
        return ret;
    }
 
    duration_str = hinic3_update_level_timeout(module_ins);
 
    if (module_ins->type == HINIC3_LOG_TYPE_DRIVER) {
        ret = hinic3_set_driver_log_level(module_ins, rte_level);
        if (ret != 0) {
            (void)hinic3_set_dpdk_log_level(module_ins, old_rte_level);
            return ret;
        }
    }
 
    HINIC3_LOG(INFO, AGENT, "Set log level, module is %s, level is %s, duration is %s.",
        module_ins->name, g_hinic3_log_level_map[rte_level].level_name, duration_str);
        return 0;
}

static uint32_t hinic3_get_log_module_dpdk_id(enum hinic3_log_module_index module_idx)
{
    uint32_t result = 0;

    if (module_idx < HINIC3_LOG_MAX) {
        result = g_hinic3_log_module_dpdk_id[module_idx];
    }

    return result;
}

static struct hinic3_log_module *hinic3_get_log_module_name(const char *module_name)
{
    struct hinic3_log_module *module = NULL;
    for (size_t i = 0; i < ARRAY_SIZE(g_hinic3_log_module_list); ++i) {
        if (strcmp(g_hinic3_log_module_list[i].name, module_name) == 0) {
            module = &g_hinic3_log_module_list[i];
            break;
        }
    }
    return module;
}

static int hinic3_get_log_level_from_name(const char *level_name)
{
    for (size_t i = 0; i < ARRAY_SIZE(g_hinic3_log_level_map); ++i) {
        if (strcmp(level_name, g_hinic3_log_level_map[i].level_name) == 0) {
            return g_hinic3_log_level_map[i].dpdk_level;
        }
    }
    return -1;
}

static int hinic3_set_log_level_help_info(struct ds *ds)
{
    hinic3_ds_put_format(ds, "%2s%s\n\n", HINIC3_UI_INDENT_SPACE, HINIC3_UI_SET_LOG_LEVEL_USAGE_STR);
    hinic3_ds_put_format(ds, "%2s%s\n", HINIC3_UI_INDENT_SPACE, HINIC3_UI_FLOW_OPTION_LIST_STRING);
 
    hinic3_ds_put_format(ds, "%4s%-25s%s\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_LOG_LEVEL_MODULE_FORMAT_STR, HINIC3_UI_LOG_LEVEL_MODULE_TIP_STR);
    hinic3_ds_put_format(ds, "%4s%-25s%s\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_LOG_LEVEL_LEVEL_FORMAT_STR, HINIC3_UI_LOG_LEVEL_LEVEL_TIP_STR);
    hinic3_ds_put_format(ds, "%4s%-25s%s\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_LOG_LEVEL_TIMEOUT_FORMAT_STR, HINIC3_UI_LOG_LEVEL_TIMEOUT_TIP_STR);
    hinic3_ds_put_format(ds, "%4s%-25s%s\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_HELP_FORMAT_STR, HINIC3_UI_HELP_TIP_STR);
 
    hinic3_ds_put_format(ds, "\n");
    hinic3_ds_put_format(ds, "%2s%s", HINIC3_UI_INDENT_SPACE, HINIC3_UI_LOG_LEVEL_SUPPORT_LEVELS_STR);
    for (size_t i = 0; i < ARRAY_SIZE(g_hinic3_log_module_list); ++i) {
        if (i % SHOW_MAX_NAME_ONE_LINE == 0) {
            hinic3_ds_put_format(ds, "\n");
            hinic3_ds_put_format(ds, "%4s", HINIC3_UI_INDENT_SPACE);
        }
        hinic3_ds_put_format(ds, "%-36s", g_hinic3_log_module_list[i].name);
    }
    hinic3_ds_put_format(ds, "\n\n");
    hinic3_ds_put_format(ds, "%2sSupported levels:", HINIC3_UI_INDENT_SPACE);
    for (size_t i = 0; i < ARRAY_SIZE(g_hinic3_log_level_map); ++i) {
        if (i % SHOW_MAX_NAME_ONE_LINE == 0) {
            hinic3_ds_put_format(ds, "\n");
            hinic3_ds_put_format(ds, "%4s", HINIC3_UI_INDENT_SPACE);
        }
        hinic3_ds_put_format(ds, "%-36s", g_hinic3_log_level_map[i].level_name);
    }
    hinic3_ds_put_format(ds, "\n");
    return 0;
}

static void
hinic3_show_log_list(struct unixctl_conn *conn, int argc HINIC3_UNUSED,
    const char *argv[] HINIC3_UNUSED, void *aux)
{
    size_t i;
    uint32_t level;
    const char *duration_str = NULL;
    struct ds ds = DS_EMPTY_INITIALIZER;
    struct timespec infinite_ts = {0};
    struct timespec current_ts = {0};
    clock_gettime(CLOCK_MONOTONIC, &current_ts);
 
    hinic3_ds_put_format(&ds, "%2s%-25s%-10s%s\n", HINIC3_UI_INDENT_SPACE, "Module:", "Level:", "Duration:");
    for (i = 0; i < ARRAY_SIZE(g_hinic3_log_module_list); i++) {
        level = g_hinic3_log_module_list[i].def_rte_level;
        if (hinic3_timespec_compare(&infinite_ts, &g_hinic3_log_module_list[i].timeout_ts) == 0) {
            duration_str = "infinite";
        } else {
            duration_str = hinic3_transform_duration_to_string(
                g_hinic3_log_module_list[i].timeout_ts.tv_sec - current_ts.tv_sec);
        }

        hinic3_ds_put_format(&ds, "%2s%-25s%-10s%s\n", HINIC3_UI_INDENT_SPACE, g_hinic3_log_module_list[i].name,
            g_hinic3_log_level_map[level].level_name, duration_str);
    }
    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
    *(int *)aux = 0;
    return;
}

static int hinic3_key_module_name_parse(const char *value, struct ds *ds)
{
    struct hinic3_log_module *module = NULL;
    module = hinic3_get_log_module_name(value);
    if (!module) {
        hinic3_ds_put_format(ds, HINIC3_UI_LEADING_SIGN_ERROR HINIC3_UI_ERROR_WRONG_PARAMETER
                ", unknown module name: %s, please input -h or –help to get help info!\n", value);
        return -1;
    }
    g_set_log_level_input_key.module = module;
    return 0;
}

static int hinic3_key_log_level_parse(const char *value, struct ds *ds)
{
    int log_level;
    log_level = hinic3_get_log_level_from_name(value);
    if (log_level == -1) {
        hinic3_ds_put_format(ds, HINIC3_UI_LEADING_SIGN_ERROR HINIC3_UI_ERROR_WRONG_PARAMETER
                ", invalid log level: %s, please input -h or –help to get help info!\n", value);
        return -1;
    }
    g_set_log_level_input_key.log_level = log_level;
    return 0;
}

static int hinic3_key_log_duration_parse(const char *value, struct ds *ds)
{
    if (strcmp(value, "infinite") == 0) {
        g_set_log_level_input_key.duration = 0;
        return 0;
    }
 
    if (!is_valid_digit(value)) {
        hinic3_ds_put_format_prefix(ds, 0, HINIC3_UI_LEADING_SIGN_ERROR,
            "%s, %s\n", HINIC3_UI_ERROR_WRONG_PARAMETER, HINIC3_UI_LOG_LIMIT_DISABLE_FAIL_STR);
        g_set_log_level_input_key.duration = -1;
        return -1;
    }
 
    int duration = hinic3_parse_string_to_duration(value);
    if (duration < 1 || duration > HINIC3_MAX_DURATION) {
        hinic3_ds_put_format_prefix(ds, 0, HINIC3_UI_LEADING_SIGN_ERROR,
            "%s, %s\n", HINIC3_UI_ERROR_WRONG_PARAMETER, HINIC3_UI_LOG_LIMIT_DISABLE_ERANGE_STR);
        g_set_log_level_input_key.duration = -1;
        return -1;
    }
 
    g_set_log_level_input_key.duration = duration;
    return 0;
}

static struct set_log_level_sub_key_parser g_input_key_parser[] = {
    {"-m", sizeof("-m"), hinic3_key_module_name_parse},
    {"-l", sizeof("-l"), hinic3_key_log_level_parse},
    {"-t", sizeof("-t"), hinic3_key_log_duration_parse},
};

static int
hinic3_set_log_level_sub_key_parse(struct unixctl_conn *conn HINIC3_UNUSED,
    const char *key_name, const char *value, struct ds *ds)
{
    long unsigned int i = 0;
    set_log_level_sub_key_parse_func func = NULL;
    for (i = 0; i < ARRAY_SIZE(g_input_key_parser); i++) {
        struct set_log_level_sub_key_parser *item = &g_input_key_parser[i];
        if (strcmp(key_name, item->key_name) == 0) {
            func = item->func;
            break;
        }
    }

    if (!func) {
        hinic3_ds_put_format(ds, "%s", HINIC3_UI_LEADING_SIGN_ERROR
                HINIC3_UI_ERROR_UNRECOGNIZED_COMMAND HINIC3_COMMAND_HELP_INFO);
        return -1;
    }

    return func(value, ds);
}

static int hinic3_set_log_level_input_key_parse(struct unixctl_conn *conn, int argc, const char *argv[], struct ds *ds)
{
    int ret;
    int work_argc = argc;
    if (work_argc != SET_LOG_MODULE_ARG_NUM) {
        HINIC3_LOG(ERR, AGENT, "arameters num error.");
        hinic3_ds_put_format(ds, "%s", HINIC3_UI_LEADING_SIGN_ERROR
                HINIC3_UI_ERROR_TOO_MANY_PARAMETER HINIC3_COMMAND_HELP_INFO);
        return -1;
    }
    int i = 1;
    const char **work_argv = argv;

    while (i < work_argc) {
        ret = hinic3_set_log_level_sub_key_parse(conn, work_argv[i], work_argv[i + 1], ds);
        if (ret != 0) {
            return -1;
        }
        i += ARGC_ADD_DOUBLE;
    }

    if (g_set_log_level_input_key.log_level == RTE_LOG_DEBUG && g_set_log_level_input_key.duration == 0) {
        hinic3_ds_put_format(ds, "%s\n", HINIC3_UI_LEADING_SIGN_ERROR
                HINIC3_UI_ERROR_WRONG_PARAMETER ", " HINIC3_UI_LOG_LIMIT_DEBUG_INFINITE_STR);
        return -1;
    }

    return 0;
}

static void hinic3_set_log_level(struct unixctl_conn *conn, int argc, const char *argv[], void *aux)
{
    int ret;
    struct ds ds = DS_EMPTY_INITIALIZER;
    if (strcmp("-h", argv[1]) == 0 || strcmp("--help", argv[1]) == 0) {
        if (argc != SET_LOG_HELP_ARG_NUM) {
            hinic3_ds_put_format(&ds, "%s", HINIC3_UI_LEADING_SIGN_ERROR
                HINIC3_UI_ERROR_TOO_MANY_PARAMETER HINIC3_COMMAND_HELP_INFO);
            goto err_out;
        }
        hinic3_set_log_level_help_info(&ds);
    } else {
        ret = hinic3_set_log_level_input_key_parse(conn, argc, argv, &ds);
        if (ret != 0) {
            goto err_out;
        }
        ret = hinic3_set_module_log_level();
        if (ret != 0) {
            hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_FAILURE "Failed to set log level for module: %s.\n",
                argv[2]);
            goto err_out;
        }
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_INFO "Successfully set log level to %s for module %s.\n",
            argv[4],  argv[2]);
    }

    *(int *)aux = 0;
    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
    return;

err_out:
    *(int *)aux = -1;
    hinic3_command_reply_error(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
    return;
}

static int
hinic3_disable_log_limit(struct ds *ds, int argc, const char *argv[])
{
    struct timespec current_ts = {0};
    clock_gettime(CLOCK_MONOTONIC, &current_ts);
 
    if (argc > OPTIONAL_ARGUMENT && is_valid_digit(argv[OPTIONAL_ARGUMENT])) {
        g_log_limit_timeline.duration = hinic3_parse_string_to_duration(argv[OPTIONAL_ARGUMENT]);
    } else {
        hinic3_ds_put_format_prefix(ds, 0, HINIC3_UI_LEADING_SIGN_ERROR,
            "%s, %s\n", HINIC3_UI_ERROR_WRONG_PARAMETER, HINIC3_UI_LOG_LIMIT_DISABLE_FAIL_STR);
        return -1;
    }

    if (g_log_limit_timeline.duration < 1 || g_log_limit_timeline.duration > HINIC3_MAX_DURATION) {
        hinic3_ds_put_format_prefix(ds, 0, HINIC3_UI_LEADING_SIGN_ERROR,
            "%s, %s\n", HINIC3_UI_ERROR_WRONG_PARAMETER, HINIC3_UI_LOG_LIMIT_DISABLE_ERANGE_STR);
        return -1;
    }

    g_log_limit_timeline.timeout_ts.tv_sec = current_ts.tv_sec + g_log_limit_timeline.duration;
    g_log_limit_timeline.timeout_ts.tv_nsec = current_ts.tv_nsec;
    hinic3_ds_put_format_prefix(ds, 0, HINIC3_UI_LEADING_SIGN_WARNING, HINIC3_UI_LOG_LIMIT_DISABLED_WARN_STR"\n",
        hinic3_transform_duration_to_string(g_log_limit_timeline.timeout_ts.tv_sec - current_ts.tv_sec));
    HINIC3_LOG(INFO, AGENT, "Log limit is disabled, may generate massive logs! Reset countdown: %s.",
        hinic3_transform_duration_to_string(g_log_limit_timeline.timeout_ts.tv_sec - current_ts.tv_sec));
    return 0;
}
 
static void
hinic3_enable_log_limit(struct ds *ds)
{
    g_log_limit_timeline.duration = 0;
    clock_gettime(CLOCK_MONOTONIC, &g_log_limit_timeline.timeout_ts);
    hinic3_ds_put_format_prefix(ds, 0, HINIC3_UI_LEADING_SIGN_INFO, HINIC3_UI_LOG_LIMIT_ENABLED_INFO_STR "\n");
    HINIC3_LOG(INFO, AGENT, "Log limit is enabled.");
}

static void
hinic3_show_log_limit(struct ds *ds)
{
    struct timespec current_ts = {0};
    clock_gettime(CLOCK_MONOTONIC, &current_ts);

    if (hinic3_timespec_compare(&current_ts, &g_log_limit_timeline.timeout_ts) >= 0) {
        hinic3_ds_put_format_prefix(ds, 0, HINIC3_UI_LEADING_SIGN_INFO, HINIC3_UI_LOG_LIMIT_ENABLED_INFO_STR "\n");
    } else {
        hinic3_ds_put_format_prefix(ds, 0, HINIC3_UI_LEADING_SIGN_WARNING, HINIC3_UI_LOG_LIMIT_DISABLED_WARN_STR "\n",
            hinic3_transform_duration_to_string(g_log_limit_timeline.timeout_ts.tv_sec - current_ts.tv_sec));
    }
}

static void
hinic3_help_log_limit(struct ds *ds)
{
    hinic3_ds_put_format(ds, "%2s%s\n\n", HINIC3_UI_INDENT_SPACE, HINIC3_UI_LOG_LIMIT_CTL_USAGE_STR);
    hinic3_ds_put_format(ds, "%2s%s\n", HINIC3_UI_INDENT_SPACE, HINIC3_UI_FLOW_OPTION_LIST_STRING);
    hinic3_ds_put_format(ds, "%4s%-25s%s\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_LOG_LIMIT_DISABLE_FORMAT_STR, HINIC3_UI_LOG_LIMIT_DISABLE_TIP_STR);
    hinic3_ds_put_format(ds, "%4s%-25s%s\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_LOG_LIMIT_ENABLE_FORMAT_STR, HINIC3_UI_LOG_LIMIT_ENABLE_TIP_STR);
    hinic3_ds_put_format(ds, "%4s%-25s%s\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_LOG_LIMIT_SHOW_FORMAT_STR, HINIC3_UI_LOG_LIMIT_SHOW_TIP_STR);
    hinic3_ds_put_format(ds, "%4s%-25s%s\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_HELP_FORMAT_STR, HINIC3_UI_HELP_TIP_STR);
}

static void
hinic3_log_limit_ctl_cmd(struct unixctl_conn *conn, int argc, const char *argv[], void *aux)
{
    struct ds ds = DS_EMPTY_INITIALIZER;
    *(int *)aux = 0;

    if (argc == OPTIONAL_ARGUMENT + 1 && (strcmp(argv[SET_LOG_HELP_ARG_NUM - 1], "--disable") == 0)) {
        *(int *)aux = hinic3_disable_log_limit(&ds, argc, argv);
        goto end;
    }

    if (argc != REQUIRED_ARGUMENT + 1) {
        hinic3_ds_put_format_prefix(&ds, 0, HINIC3_UI_LEADING_SIGN_ERROR, "%s, %s\n",
            HINIC3_UI_ERROR_TOO_MANY_PARAMETER, HINIC3_UI_PLEASE_HELP_STR);
        *(int *)aux = -1;
        goto end;
    }
    if (strcmp(argv[SET_LOG_HELP_ARG_NUM - 1], "--enable") == 0) {
        hinic3_enable_log_limit(&ds);
    } else if (strcmp(argv[SET_LOG_HELP_ARG_NUM - 1], "--show") == 0) {
        hinic3_show_log_limit(&ds);
    } else if ((strcmp(argv[SET_LOG_HELP_ARG_NUM - 1], "-h") == 0) ||
               (strcmp(argv[SET_LOG_HELP_ARG_NUM - 1], "--help") == 0)) {
        hinic3_help_log_limit(&ds);
    } else {
        hinic3_ds_put_format_prefix(&ds, 0, HINIC3_UI_LEADING_SIGN_ERROR, "%s, %s\n",
            HINIC3_UI_ERROR_WRONG_PARAMETER, HINIC3_UI_PLEASE_HELP_STR);
        *(int *)aux = -1;
    }
end:
    if (*(int *)aux != 0) {
        hinic3_command_reply_error(conn, hinic3_ds_cstr(&ds));
    } else {
        hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    }
    hinic3_ds_destroy(&ds);
}

void unixctl_hinic3_cmd_log_register(void)
{
    hinic3_command_register("hwoff/show-log-list", "", 0, 0, hinic3_show_log_list, NULL);
    hinic3_command_register("hwoff/set-log-level", "{ -m <module-name> -l <level> -t <duration> | { -h | --help } }",
        1, SET_LOG_MODULE_ARG_NUM, hinic3_set_log_level, NULL);
    hinic3_command_register("hwoff/log-limit-ctl",
        "{ --disable INTEGER<duration> | --enable | --show | { -h | --help } }",
        1, SET_LOG_HELP_ARG_NUM, hinic3_log_limit_ctl_cmd, NULL);    
}

static inline unsigned int hinic3_sat_mul(unsigned int x, unsigned int y)
{
    return HINIC3_SAT_MUL(x, y);
}

static inline unsigned int hinic3_sat_add(unsigned int x, unsigned int y)
{
    return x + y >= x ? x + y : UINT_MAX;
}

static bool token_bucket_withdraw(struct token_bucket *tb, unsigned int n)
{
    if (tb->tokens < n) {
        long long int now = hinic3_time_msec();
        if (now > tb->last_fill) {
            unsigned long long int elapsed_ull
                = (unsigned long long int) now - tb->last_fill;
            unsigned int elapsed = MIN(UINT_MAX, elapsed_ull);
            unsigned int add = hinic3_sat_mul(tb->rate, elapsed);
            unsigned int tokens = hinic3_sat_add(tb->tokens, add);
            tb->tokens = MIN(tokens, tb->burst);
            tb->last_fill = now;
        }

        if (tb->tokens < n) {
            return false;
        }
    }

    tb->tokens -= n;
    return true;
}

static bool hinic3_log_is_enabled(uint32_t level, uint32_t logtype)
{
    for (size_t i = 0; i < ARRAY_SIZE(g_hinic3_log_module_list); ++i) {
        if (logtype == g_hinic3_log_module_list[i].index) {
            return level >= g_hinic3_log_module_list[i].type;
        }
    }
    return false;
}

static bool hinic3_log_should_drop(uint32_t level, uint32_t logtype, struct vlog_rate_limit *rl)
{
    if (!hinic3_log_is_enabled(level, logtype)) {
        return true;
    }

    hinic3_pthread_mutex_lock(&rl->mutex);
    if (!token_bucket_withdraw(&rl->token_bucket, HINIC3_LOG_MSG_TOKENS)) {
        long long int now = hinic3_time_sec();
        if (!rl->n_dropped) {
            rl->first_dropped = now;
        }
        rl->last_dropped = now;
        rl->n_dropped++;
        hinic3_pthread_mutex_unlock(&rl->mutex);
        return true;
    }

    if (!rl->n_dropped) {
        hinic3_pthread_mutex_unlock(&rl->mutex);
    } else {
        long long int now = hinic3_time_sec();
        unsigned int n_dropped = rl->n_dropped;
        long long int first_dropped_elapsed = now - rl->first_dropped;
        long long int last_dropped_elapsed = now - rl->last_dropped;
        uint32_t dpdk_log_type = hinic3_get_log_module_dpdk_id(logtype);
        rl->n_dropped = 0;
        hinic3_pthread_mutex_unlock(&rl->mutex);

        rte_log(level, dpdk_log_type,
                "[%s] Hinic3 dropped %u log messages in last %lld seconds (most recently, "
                "%lld seconds ago) due to excessive rate",
                hinic3_log_prefix_get(), n_dropped, first_dropped_elapsed, last_dropped_elapsed);
    }

    return false;
}

static void hinic3_log_level_reset(const struct timespec *current_ts)
{
    int ret = 0;
    struct timespec infinite_ts = {0};
    for (long unsigned int i = 0; i < ARRAY_SIZE(g_hinic3_log_module_list); i++) {
        if (hinic3_timespec_compare(&infinite_ts, &g_hinic3_log_module_list[i].timeout_ts) == 0) {
            /* infinte的不复位 */
            continue;
        }
        if (hinic3_timespec_compare(current_ts, &g_hinic3_log_module_list[i].timeout_ts) >= 0) {
            /* 超时后复位日志级别 */
            g_set_log_level_input_key.module = &g_hinic3_log_module_list[i];
            g_set_log_level_input_key.log_level = RTE_LOG_INFO;
            g_set_log_level_input_key.duration = 0;
            ret = hinic3_set_module_log_level();
            if (ret != 0) {
                rte_log(RTE_LOG_ERR, HINIC3_LOG_TYPE_UP,
                    "Failed to reset log level for module: %s, retry in 60 minutes later.",
                    g_hinic3_log_level_map[RTE_LOG_INFO].level_name);
                /* 复位失败一小时后重试 */
                g_hinic3_log_module_list[i].timeout_ts.tv_sec = current_ts->tv_sec + HINIC3_SEC_PER_HOUR;
                g_hinic3_log_module_list[i].timeout_ts.tv_nsec = current_ts->tv_nsec;
            } else {
                /* 复位成功永久化默认等级 */
                g_hinic3_log_module_list[i].timeout_ts.tv_sec = 0;
                g_hinic3_log_module_list[i].timeout_ts.tv_nsec = 0;
            }
        }
    }
}

int hinic3_log_limit(uint32_t level, uint32_t logtype, const char *format, ...)
{
    int ret = 0;
    static struct vlog_rate_limit rl = HINIC3_LOG_RATE_LIMIT_INIT(HINIC3_LOG_RATE_TOKENS, HINIC3_LOG_BURST_TOKENS);
    struct timespec current_ts;
    clock_gettime(CLOCK_MONOTONIC, &current_ts);
    bool limit_switch = hinic3_timespec_compare(&current_ts, &g_log_limit_timeline.timeout_ts) >= 0;
    hinic3_log_level_reset(&current_ts);
    /*
     * 当 limit_switch == false（限速器关闭）时,总是log
     * 当 limit_switch == true （限速器开启）时,由hinic3_log_should_drop判断是否log
     */
    if (!limit_switch || !hinic3_log_should_drop(level, logtype, &rl)) {
        uint32_t dpdk_log_type = hinic3_get_log_module_dpdk_id(logtype);
        va_list ap;
        va_start(ap, format);

        ret = rte_vlog(level, dpdk_log_type, format, ap);
        va_end(ap);
    }

    return ret;
}

bool hinic3_is_flow_debug(void)
{
    return g_hinic3_log_module_list[HINIC3_LOG_FLOW].def_rte_level == RTE_LOG_DEBUG;
}

int hinic3_log_flexda_limit(uint32_t level, uint32_t logtype, const char *format, ...)
{
    uint32_t dpdk_log_id;
    if (logtype < HINIC3_LOG_MAX) {
        dpdk_log_id = hinic3_get_log_module_dpdk_id(logtype);
    } else {
        dpdk_log_id = g_hinic3_flexda_log_module_dpdk_id[logtype - RTE_LOG_MAX];
    }
    va_list ap;
    va_start(ap, format);
        
    int ret = rte_vlog(level, dpdk_log_id, format, ap);
    va_end(ap);

    return ret;
}

int hinic3_log_flexda(uint32_t level, uint32_t module, const char *format, ...)
{
    char str[MAX_BUF_SIZE];
    int len = 0;

    if (module < HINIC3_LOG_MAX) {
        len = snprintf(str, MAX_BUF_SIZE, "%s%s: ",
            FLEXDA_LOG_PREFIX, g_hinic3_log_index_map[module]);
    } else if (module - HINIC3_LOG_MAX < RTE_LOG_MAX &&
        g_hinic3_flexda_log_module_list[module - RTE_LOG_MAX].type == HINIC3_LOG_TYPE_FLEXDA) {
        len = snprintf(str, MAX_BUF_SIZE, "%s%s: ",
            FLEXDA_LOG_PREFIX, g_hinic3_flexda_log_module_list[module - HINIC3_LOG_MAX].name);
    } else {
        HINIC3_LOG(ERR, FLOW, "Failed to log flexda message, invalid module");
        return -1;
    }
    if (len < 0) {
        HINIC3_LOG(ERR, FLOW, "Failed to log flexda message, snprintf_s failed, the module is %u, err is %d",
                module, len);
        return -1;
    }
    
    va_list args;
    va_start(args, format);
    int ret = vsnprintf((char *)&str[len], MAX_BUF_SIZE - len, format, args);
    va_end(args);
    if (ret < 0) {
        HINIC3_LOG(ERR, FLOW, "Failed to log flexda message, the log length is too long, err is %d", ret);
                return -1;
    }

    ret = hinic3_log_flexda_limit(level, module, str);
    if (ret == -1) {
        HINIC3_LOG(ERR, FLOW, "Failed to log flexda message: %s", str);
    }

    return ret;
}

int hinic3_log_module_register(char *name)
{
    size_t i;
    int ret;
    int log_id;
    struct hinic3_log_module *module = NULL;

    if (name == NULL || strlen(name) == 0) {
        HINIC3_LOG(ERR, AGENT, "register log module failed, it is NULL or empty!");
        return -1;
    }
    for (i = 0; i < HINIC3_LOG_MAX; i++) {
        module = &g_hinic3_flexda_log_module_list[i];
        if (module->type != HINIC3_LOG_TYPE_UNUSED) {
            continue;
        }

        log_id = rte_log_register(name);
        if (log_id < 0) {
            HINIC3_LOG(ERR, AGENT, "register log for module %s failed, err is %d.", name, log_id);
            return -1;
        }

        strncpy(g_hinic3_flexda_name_buffer[i], name, HINIC3_MODULE_NAME_MAX_LEN - 1);
        g_hinic3_flexda_name_buffer[i][HINIC3_MODULE_NAME_MAX_LEN - 1] = '\0';
        module->name = g_hinic3_flexda_name_buffer[i];
        module->index = i + RTE_LOG_MAX;
        module->type = HINIC3_LOG_TYPE_FLEXDA;
        g_hinic3_flexda_log_module_dpdk_id[i] = log_id;

        ret = rte_log_set_level(log_id, RTE_LOG_INFO);
        if (ret < 0) {
            module->name = NULL;
            module->type = HINIC3_LOG_TYPE_UNUSED;
            HINIC3_LOG(ERR, AGENT, "set log level for module %s failed, err is %d", name, ret);
            return -1;
        }

        return module->index;
    }

    HINIC3_LOG(ERR, AGENT, "set log level for module %s failed, exceeded the maximum number of modules", name);
    return -1;
}