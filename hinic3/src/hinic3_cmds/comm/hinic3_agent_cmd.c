/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */

#include "hinic3_agent_flow_cmd.h"
#include "hinic3_key_query.h"
#include "hinic3_agent_cmd_format.h"
#include "hinic3_dfx_multi_qos.h"
#include "hinic3_dfx_port.h"
#include "hinic3_init.h"
#include "hinic3_iface_global.h"
#include "hinic3_iface_flow.h"
#include "hinic3_tlv_key.h"
#include "hinic3_util.h"
#include "hinic3_log.h"
#include "hinic3_ui_string.h"
#include "hinic3_flow_agent.h"
#include "hinic3_command.h"
#include "hinic3_log.h"
#include "hinic3_check_thread_health_state.h"
#include "hinic3_agent_cmd_time.h"
#include "hinic3_flow_dump.h"
#include "hinic3_ufid_map_rte_flow.h"
#include "hinic3_flow_session.h"
#include "hinic3_ds.h"
#include "hinic3_eth_util.h"
#include "hinic3_flow_dump_public.h"
#include "hinic3_age_delete_flow.h"
#include "hinic3_ui_string.h"
#include "hinic3_parse_agent_config.h"
#include "hinic3_agent_flow_cmd_dump.h"
#include "hinic3_mega_dump.h"
#include "hinic3_cmd_error_stats.h"
#include "hinic3_cmd_forward.h"
#include "hinic3_cmd_exec.h"
#include "hinic3_cmd_meminfo.h"
#include "hinic3_ufid_del_flow.h"
#include "hinic3_flow_qos.h"
#include "hinic3_agent_cmd.h"

#define HINIC3_BATCH_BOLCK_NUM 128
#define HINIC3_DUMP_BATCH_MIN_ARGC 1
#define HINIC3_DUMP_BATCH_MAX_ARGC 2
#define HINIC3_AGENT_DUMP_IDLE_SLEEP 1

static void
unixctl_hinic3_agent_cmd_init(void)
{
    hinic3_command_register("hwoff/show-sample-session", "", 0, 0, hinic3_show_sample_session, NULL);
}
static void
unixctl_common_cmd_register(void)
{
    if (hinic3_card_mod_get() == PROG_MODE)
    {
        hinic3_command_register("hwoff/show-hmap-flow-num", "[ -t <table-id> | { -h | --help } ]",
                               0, HINIC3_DUMP_BATCH_MAX_ARGC, hinic3_flexda_dump_hmap_flow_num, NULL);
    } else {
        hinic3_command_register("hwoff/show-hmap-flow-num", "", 0, 0, hinic3_dump_hmap_flow_num, NULL);
    }
}

void hinic3_agent_cmd_init(void)
{
    unixctl_hinic3_cmd_error_stats_register();
    unixctl_hinic3_cmd_forward_register();
    unixctl_hinic3_cmd_exec_register();
    unixctl_hinic3_cmd_meminfo_register();
    unixctl_hinic3_cmd_log_register();

    unixctl_hinic3_flow_cmd_init();
    unixctl_hinic3_flow_dump_cmd_init();
    unixctl_hinic3_multi_qos_dfx_init();
    unixctl_hinic3_port_cmd_init();
    unixctl_hinic3_agent_cmd_init();
    unixctl_hinic3_trace_flow_init();
    unixctl_thread_status_dfx_init();
    unixctl_common_cmd_register();
    unixctl_hinic3_query_cmd_init();
    unixctl_hinic3_delete_cmd_init();
    unixctl_meter_dfx_init();

    if (hinic3_check_fuzzy_flow_switch() == true)
        unixctl_hinic3_mega_flow_dump_cmd_init();
}
