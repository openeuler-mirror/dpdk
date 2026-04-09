/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2025 Huawei Technologies Co., Ltd
 */

#include <string.h>
#include "hinic3_ds.h"
#include "hinic3_log.h"
#include "hinic3_ui_string.h"
#include "hinic3_error_stats.h"
#include "hinic3_command.h"
#include "hinic3_cmd_error_stats.h"
#define HINIC3_ERROR_STATS_CMD_ARG_NUM_MIN 2
#define HINIC3_ERROR_STATS_CMD_ARG_NUM_MAX 3

static const char *g_module_name[] = {
    [HINIC3_COMMON_RESOURCE] = "hinic3-common",
    [HINIC3_CAPTURE] = "hinic3-capture",
    [HINIC3_COMMAND] = "hinic3-command",
    [HINIC3_CT_OFFLOAD] = "hinic3-ct-offload",
    [HINIC3_DRIVER_ADAPTER] = "hinic3-driver-adapter",
    [HINIC3_FLOWS] = "hinic3-flows",
    [HINIC3_OVS_FLOW] = "hinic3-ovs-flows",
    [HINIC3_INIT] = "hinic3-init",
    [HINIC3_POLICY] = "hinic3-policy",
    [HINIC3_PORTS] = "hinic3-ports",
    [HINIC3_QOS] = "hinic3-qos",
    [HINIC3_SECURITY_FILTER] = "hinic3-security-filter",
    [HINIC3_UFID_MAP] = "hinic3-ufid-map",
    [HINIC3_OVS_MEMPOOL] = "hinic3-ovs-mempool",
    [HINIC3_OVS_UFID_MEMPOOL] = "hinic3-ovs-ufid-mempool",
    [HINIC3_OVS_UFID_MAP] = "hinic3-ovs-ufid-map",
    [HINIC3_PACKET_PARSE] = "hinic3-packet-parse",
    [HINIC3_OVS_SMAC] = "hinic3-ovs-smac",
    [HINIC3_OVS_HINIC3_PRIVATE] = "hinic3-ovs-private-data",
    [HIOVS_MEM] = "hiovs-mem",
    [HINIC3_CMD] = "hwoff-command",
    [HINIC3_MODULE_MAX] = "unknown module",
};

static void
hinic3_error_stats_print_module_sub(struct ds *output_str, enum hinic3_module module,
    uint16_t level, bool *header)
{
    int ret;
    for (int error_idx = HINIC3_ERRSTAT_START + 1; error_idx < HINIC3_ERRSTAT_END; error_idx++) {
        struct hinic3_error_stats stats = {0};
        char buf[HINIC3_ERROR_STRING_MAX_LEN] = {0};
        ret = hinic3_get_error_stats(error_idx, &stats);
        if (ret != 0 || stats.count == 0 || stats.level != level || stats.module != module || stats.string == NULL)
            continue;
        if (*header == true) {
            const char* name = g_module_name[module];
            if (name == NULL) {
                HINIC3_LOG(ERR, AGENT, "Can not find module name!");
                return;
            }
            hinic3_ds_put_format(output_str, "%2s%s:\n", HINIC3_UI_INDENT_SPACE, name);
            *header = false;
        }
        ret = snprintf(buf, sizeof(buf), "%s:", stats.string);
        if (ret <= 0) {
            HINIC3_LOG(ERR, AGENT, "snprintf for error_string failed.");
            return;
        }
        hinic3_ds_put_format(output_str, "%4s%-50s%u\n",
            HINIC3_UI_INDENT_SPACE, buf, stats.count);
    }
}

static void
hinic3_error_stats_print_module(struct ds *output_str, uint16_t module,
    enum hinic3_errstat_print_level print_level)
{
    bool header = true;
    // 先打印ERROR类型
    if ((print_level & HINIC3_ERRSTAT_PRINT_LEVEL_ERROR) == HINIC3_ERRSTAT_PRINT_LEVEL_ERROR)
        hinic3_error_stats_print_module_sub(output_str, module, HINIC3_ERRSTAT_LEVEL_ERROR, &header);
    // 再打印WARNING类型
    if ((print_level & HINIC3_ERRSTAT_PRINT_LEVEL_WARNING) == HINIC3_ERRSTAT_PRINT_LEVEL_WARNING)
        hinic3_error_stats_print_module_sub(output_str, module, HINIC3_ERRSTAT_LEVEL_WARNING, &header);
}

static int
hinic3_error_stats_print_all_modules(struct ds *output_str, const char* level_str)
{
    enum hinic3_errstat_print_level level = HINIC3_ERRSTAT_PRINT_LEVEL_NULL;
    if (strcmp("all", level_str) == 0) {
        level = HINIC3_ERRSTAT_PRINT_LEVEL_ALL;
    } else if (strcmp("error", level_str) == 0) {
        level = HINIC3_ERRSTAT_PRINT_LEVEL_ERROR;
    } else if (strcmp("warning", level_str) == 0) {
        level = HINIC3_ERRSTAT_PRINT_LEVEL_WARNING;
    } else {
        return -1;
    }
    for (int module_idx = 0; module_idx < HINIC3_MODULE_MAX; module_idx++) {
        hinic3_error_stats_print_module(output_str, module_idx, level);
    }
    return 0;
}
static void
hinic3_flow_agent_error_stats_cmd_help(struct ds *output_str)
{
    hinic3_ds_put_format(output_str,
        "%2sUsage: dpak-ovs-ctl hwoff/show-error-stats { -l ENUM<all,error,warning> | { -h | --help } }\n\n",
        HINIC3_UI_INDENT_SPACE);
    hinic3_ds_put_format(output_str, "%2sOptions list:\n", HINIC3_UI_INDENT_SPACE);
    hinic3_ds_put_format(output_str, "%4s%-30sSpecify the level of printing\n", HINIC3_UI_INDENT_SPACE, "-l");
    hinic3_ds_put_format(output_str, "%6s%-28sPrint error and warning stats\n", HINIC3_UI_INDENT_SPACE, "all");
    hinic3_ds_put_format(output_str, "%6s%-28sPrint error level stats\n", HINIC3_UI_INDENT_SPACE, "error");
    hinic3_ds_put_format(output_str, "%6s%-28sPrint warning level stats\n", HINIC3_UI_INDENT_SPACE, "warning");
    hinic3_ds_put_format(output_str, "%4s%-30sShow help command\n", HINIC3_UI_INDENT_SPACE, "-h, --help");
}

void
hinic3_flow_agent_error_stats_cmd(struct unixctl_conn *conn, int argc, const char *argv[], void *aux)
{
    if (argc > HINIC3_ERROR_STATS_CMD_ARG_NUM_MAX || argc < HINIC3_ERROR_STATS_CMD_ARG_NUM_MIN) {
        hinic3_command_reply_error(conn, HINIC3_UI_LEADING_SIGN_ERROR "Invalid number of parameters.\n");
        *(int *)aux = -1;
        return;
    }

    int ret = 0;
    struct ds output_str = DS_EMPTY_INITIALIZER;
    if ((strcmp("-h", argv[1]) == 0) || (strcmp("--help", argv[1]) == 0)) {
        hinic3_flow_agent_error_stats_cmd_help(&output_str);
    } else if (strcmp("-l", argv[1]) == 0 && argc == HINIC3_ERROR_STATS_CMD_ARG_NUM_MAX) {
        ret = hinic3_error_stats_print_all_modules(&output_str, argv[HINIC3_ERROR_STATS_CMD_ARG_NUM_MAX-1]);
        if (ret != 0) {
            *(int *)aux = -1;
            hinic3_command_reply_error(conn, HINIC3_UI_LEADING_SIGN_ERROR "Wrong parameter.\n");
            goto end;
        }
    } else {
        *(int *)aux = -1;
        hinic3_command_reply_error(conn, HINIC3_UI_LEADING_SIGN_ERROR "Unrecognized command.\n");
        goto end;
    }
    *(int *)aux = 0;
    hinic3_command_reply(conn, hinic3_ds_cstr(&output_str));
end:
    hinic3_ds_destroy(&output_str);
}

void
unixctl_hinic3_cmd_error_stats_register(void)
{
    hinic3_command_register("hwoff/show-error-stats", "", 1, 2,
        hinic3_flow_agent_error_stats_cmd, NULL);
}
