/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2024 Huawei Technologies Co., Ltd
 */

#include <stdio.h>
#include "hinic3_command.h"
#include "hinic3_cmd_exec.h"
#include "hinic3_capture_utils.h"
#include "hinic3_agent_cmd_format.h"
#include "hinic3_iface_flow.h"
#include "hinic3_ui_string.h"
#include "hinic3_string_util.h"
#include "hinic3_parse_agent_config.h"
#include "hinic3_ds.h"
#include "hinic3_mtr.h"
#include "hinic3_flow_qos.h"
#include "hinic3_offload_flow_public.h"
#include "hinic3_ufid_hmap.h"
#include "hinic3_ufid_map_rte_flow.h"
#include "hinic3_ufid_del_flow.h"
#define ARGC 2

static void hinic3_flow_del_by_ufid_ovs_sub(struct unixctl_conn *conn, int argc, const char *argv[], void *aux)
{
    struct ds ds = DS_EMPTY_INITIALIZER;
    uint64_t ufid;
    uint32_t table_id = HINIC3_DEFAULT_TABLE_ID;
    int ret;
    if (argc != ARGC) {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR HINIC3_UI_FLOW_REQUIRES_WRONG_ARGUMENT_STRING, argc - 1);
        goto out;
    }

    ret = hinic3_parse_hw_ufid_from_string(argv[1], &ufid);
    if (ret != 0) {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR HINIC3_UI_UFID_MAP_HW_UFID_FORMART_ERROR_STRING);
        goto out;
    }

    ret = hinic3_flow_del_by_ufid(ufid, table_id);
    if (ret != 0) {
        hinic3_ds_put_format(&ds, "Failure: Delete hw flow by ufid failed.\n");
        goto out;
    }

    ret = hinic3_flow_destroy_in_hmap_by_ufid(ufid);
    if (ret != 0) {
        hinic3_ds_put_format(&ds, "Failure: Delete rte flow by ufid failed.\n");
        goto out;
    }

    hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_INFO HINIC3_UI_FLOW_DELETE_BY_UFID_SUCCESS);
    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
    return;
out:
        *(int*)aux = -1;
        hinic3_command_reply_error(conn, hinic3_ds_cstr(&ds));
        hinic3_ds_destroy(&ds);
        return;
}

static void hinic3_flow_del_by_ufid_ovs_help(struct unixctl_conn *conn)
{
    struct ds ds = DS_EMPTY_INITIALIZER;
    hinic3_ds_put_format(&ds, "%2s%s%s\n\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_CMD_USAGE_STRING, HINIC3_UI_DEL_FLOW_BY_UFID_TIPS_STRING);
    hinic3_ds_put_format(&ds, "%2s%s\n", HINIC3_UI_INDENT_SPACE, HINIC3_UI_OPTION_LIST_STRING);
    hinic3_ds_put_format(&ds, "%4s%-30s%-s\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_DEL_FLOW_BY_UFID_HELP_UFID_STR, HINIC3_UI_DEL_FLOW_BY_UFID_HELP_STR);
    hinic3_ds_put_format(&ds, "%4s%-30s%-s\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_ESCAPE_MODE_SHOW_HELP_STRING, HINIC3_UI_ESCAPE_MODE_SHOW_HELP_TIPS_STRING);
    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
}

static void hinic3_flow_del_by_ufid_ovs(struct unixctl_conn *conn, int argc, const char *argv[], void *aux)
{
    if ((strcmp(argv[1], "-h") == 0) || (strcmp(argv[1], "--help") == 0)) {
        hinic3_flow_del_by_ufid_ovs_help(conn);
    } else {
        hinic3_flow_del_by_ufid_ovs_sub(conn, argc, argv, aux);
    }
}

void unixctl_hinic3_delete_cmd_init(void)
{
    hinic3_command_register("hwoff/del-flow-by-ufid", "{ <ufid> | { -h | --help } }", 1, 1, hinic3_flow_del_by_ufid_ovs, NULL);
}

int hinic3_del_flow_qos_by_meter_id(uint32_t meter_id)
{
    int ret;
    struct hinic3_mtr_policy_node *policy_node = NULL;
    struct hinic3_meter_node *meter = NULL;
    struct hinic3_meter_node *next_meter = NULL;

    hinic3_meter_list_lock();
    meter = hinic3_meter_find(meter_id);
    if (meter == NULL)
    {
        hinic3_meter_list_unlock();
        hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_METER_NOT_FOUND, 1);
        return -1;
    }
    ret = hinic3_set_flow_qos_to_hovs_sub(meter, true, NULL, NULL);
    if (ret != 0)
    {
        hinic3_meter_list_unlock();
        hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_FLOW_DEL_QOS_HOVS, 1);
        return -1;
    }

    policy_node = meter->policy;
    if (policy_node->has_next_meter == true)
    {
        next_meter = hinic3_meter_find(policy_node->next_meter_id);
        if (next_meter == NULL)
        {
            hinic3_meter_list_unlock();
            hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_METER_NOT_FOUND, 1);
            return -1;
        }
        ret = hinic3_set_flow_qos_to_hovs_sub(next_meter, true, NULL, NULL);
        if (ret != 0)
        {
            hinic3_meter_list_unlock();
            hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_FLOW_DEL_QOS_HOVS, 1);
            return -1;
        }
    }

    hinic3_meter_list_unlock();
    return 0;
}