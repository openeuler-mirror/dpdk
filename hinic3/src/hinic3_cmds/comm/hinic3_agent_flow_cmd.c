/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */

#include "hinic3_util.h"
#include "hinic3_map.h"
#include "hinic3_eth_packets.h"
#include "rte_config.h"
#include <rte_atomic.h>
#include "hinic3_log.h"
#include "hinic3_iface_flow.h"
#include "hinic3_flow_agent.h"
#include "hinic3_agent_cmd_format.h"
#include "hinic3_ui_string.h"
#include "hinic3_option.h"
#include "hinic3_thread.h"
#include "hinic3_string_util.h"
#include "hinic3_flow_dump_public.h"
#include "hinic3_meminfo.h"
#include "hinic3_command.h"
#include "hinic3_ds.h"
#include "hinic3_dfx_multi_qos.h"
#include "hinic3_file_util.h"
#include "hinic3_agent_flow_cmd.h"

#define UUID_64_LEN (18)
#define UUID_64_FMT "%08x-%08x"

static const struct hinic3_opt flow_cmd_opts[] = {
    {"ufid",  REQUIRED_ARGUMENT, 0, HINIC3_UFID_64_OPT},
    {"stop",  NO_ARGUMENT,       0, HINIC3_STOP_DUMP_ALL_FLOWS_OPT},
    {"check", NO_ARGUMENT,       0, HINIC3_CHECK_DUMP_ALL_FLOWS_OPT},
    {"-f",    REQUIRED_ARGUMENT, 0, HINIC3_FLOW_FILE_OPT},
    {"-n",    NO_ARGUMENT,       0, HINIC3_FLOW_CNT_OPT},
    {"-h",    NO_ARGUMENT,       0, HELP_OPT},
    {"--help",    NO_ARGUMENT,       0, HELP_OPT},
    {"api",   REQUIRED_ARGUMENT, 0, HINIC3_FLOW_API_NAME_OPT},
    {"all",   NO_ARGUMENT,       0, HINIC3_FLOW_ALL_OPT},
    {"clear", NO_ARGUMENT,       0, HINIC3_FLOW_CLEAR_OPT},
    {"-i",    REQUIRED_ARGUMENT, 0, HINIC3_FLOW_BLOCK_ID_OPT},
    {"-t",   REQUIRED_ARGUMENT, 0,  HINIC3_FLOW_TABLE_ID_OPT },
};

int
hinic3_find_option(const char *name)
{
    int option_val;
    size_t idx = 0;

    if (name == NULL)
        return -1;
    for (idx = 0; idx < sizeof(flow_cmd_opts) / sizeof(struct hinic3_opt); idx++) {
        if (strcmp(name, flow_cmd_opts[idx].name) == 0) {
            option_val = flow_cmd_opts[idx].val;
            return option_val;
        }
    }
    return -1;
}

static void
hinic3_offload_flow_num_get(struct unixctl_conn *conn, int argc HINIC3_UNUSED, const char *argv[] HINIC3_UNUSED,
    void *aux)
{
    uint32_t tmp_value;
    struct ds ds = DS_EMPTY_INITIALIZER;

    tmp_value = hinic3_get_offload_flow_nums();
    hinic3_ds_put_format(&ds, "%2soffload-flow-num: %u\n", HINIC3_UI_INDENT_SPACE, tmp_value);
    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
    *(int *)aux = 0;
    return;
}

void
unixctl_hinic3_flow_cmd_init(void)
{
    enum hinic3_flow_cmd_max {
        SHOW_FLOW_API_PARAM = 3,
        SHOW_OFFLOAD_SPEED_PARAM = 5,
    };

    hinic3_command_register("hwoff/show-flow-api", "{ all | clear { all | <api-name> } | "
    "api <api-name> | { -h | --help } }", 1, SHOW_FLOW_API_PARAM,
        hinic3_agent_show_flow_api, NULL);
    hinic3_command_register("hwoff/flow-offload-speed-stat",
        "{ start -i INTEGER<1-10> -t INTEGER<1-300> | restart [ -i INTEGER<1-10> "
        "| -t INTEGER<1-300> ] * | stop | show | { -h | --help } }", 1,
        SHOW_OFFLOAD_SPEED_PARAM, hinic3_speed_task_cmd, NULL);
    hinic3_command_register("hwoff/show-offload-flow-num", "", 0, 0, hinic3_offload_flow_num_get, NULL);
}
