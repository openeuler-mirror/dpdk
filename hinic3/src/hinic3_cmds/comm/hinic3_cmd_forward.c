/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2025 Huawei Technologies Co., Ltd
 */

#include "hinic3_log.h"
#include "hinic3_ui_string.h"
#include "hinic3_eth_util.h"
#include "hinic3_cmd_forward.h"
#include "hinic3_ufid_map_rte_flow.h"
#include "hinic3_command.h"

static uint8_t g_packet_detect_mode = 0;

static int hinic3_forward_mode_help_info(struct ds *reply)
{
    hinic3_ds_put_format(reply, "%2s%s%s\n\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_CMD_USAGE_STRING, HINIC3_UI_SET_FORWARD_MODE_TIPS_STRING);
    hinic3_ds_put_format(reply, "%2s%s\n", HINIC3_UI_INDENT_SPACE, HINIC3_UI_OPTION_LIST_STRING);
    hinic3_ds_put_format(reply, "%4s%-30s%-s\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_LOW_LATENCY_CMD_STR, HINIC3_UI_SET_FORWARD_MODE_HELP_LOW_STR);
    hinic3_ds_put_format(reply, "%4s%-30s%-s\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_HIGH_THROUGHPUT_CMD_STR, HINIC3_UI_SET_FORWARD_MODE_HELP_HIGH_STR);
    hinic3_ds_put_format(reply, "%4s%-30s%-s\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_ESCAPE_MODE_SHOW_HELP_STRING, HINIC3_UI_ESCAPE_MODE_SHOW_HELP_TIPS_STRING);
    return 0;
}

static void
hinic3_forward_mode_set_cmd(struct unixctl_conn *conn, int argc HINIC3_UNUSED, const char *argv[], void *aux)
{
    enum hinic3_packet_forward_mod mode = 0;
    int ret = 0;

    struct ds reply = DS_EMPTY_INITIALIZER;
    if (strcmp(argv[1], HINIC3_LOW_LATENCY_CMD_STR) == 0) {
        mode = HINIC3_FORWARD_MODE_LATENCY;
    } else if (strcmp(argv[1], HINIC3_HIGH_THROUGHPUT_CMD_STR) == 0) {
        mode = HINIC3_FORWARD_MODE_BANDWIDTH;
    } else if ((strcmp(argv[1], "-h") == 0) || (strcmp(argv[1], "--help") == 0)) {
        hinic3_forward_mode_help_info(&reply);
        *(int *)aux = 0;
        hinic3_command_reply(conn, hinic3_ds_cstr(&reply));
        hinic3_ds_destroy(&reply);
        return;
    } else {
        *(int*)aux = -1;
        hinic3_command_reply_error(conn, HINIC3_UI_LEADING_SIGN_ERROR HINIC3_UI_ERROR_WRONG_PARAMETER HINIC3_SET_FORWARD_MODE_INPUT_ERROR_STRING);
        hinic3_ds_destroy(&reply);
        return;
    }

    if (mode == HINIC3_FORWARD_MODE_BANDWIDTH)
        mode = HINIC3_FORWARD_MODE_30M;

    ret = hinic3_forward_mode_set(mode);
    if (ret != 0) {
        *(int*)aux = -1;
        hinic3_command_reply_error(conn, HINIC3_UI_LEADING_SIGN_FAILURE HINIC3_SET_FORWARD_MODE_FAILED_STRING);
        hinic3_ds_destroy(&reply);
        return;
    }

    *(int *)aux = 0;
    hinic3_command_reply(conn, HINIC3_UI_LEADING_SIGN_INFO HINIC3_SET_FORWARD_MODE_SUCCESS_STRING);
    hinic3_ds_destroy(&reply);
    return;
}

static void
hinic3_forward_mode_get_cmd(struct unixctl_conn *conn, int argc HINIC3_UNUSED, const char *argv[] HINIC3_UNUSED, void *aux)
{
    struct ds reply = DS_EMPTY_INITIALIZER;
    int ret = 0;

    ret = hinic3_forward_mod_get(&reply);
    if (ret != 0) {
        *(int*)aux = -1;
        hinic3_command_reply_error(conn, HINIC3_UI_LEADING_SIGN_FAILURE"get packet forward mode failed\n");
        hinic3_ds_destroy(&reply);
        return;
    }

    hinic3_command_reply(conn,  hinic3_ds_cstr(&reply));
    hinic3_ds_destroy(&reply);
    *(int*)aux = 0;
    return;
}

static int
hinic3_show_flow_escape_mode_help_info(struct ds *reply)
{
    hinic3_ds_put_format(reply, "%2s%s%s\n\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_CMD_USAGE_STRING, HINIC3_UI_ESCAPE_MODE_HELP_TIPS_STRING);
    hinic3_ds_put_format(reply, "%2s%s\n", HINIC3_UI_INDENT_SPACE, HINIC3_UI_FLOW_OPTION_LIST_STRING);
    hinic3_ds_put_format(reply, "%4s%-30s%-s\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_ESCAPE_MODE_ENABLE_FORMAT_STRING, HINIC3_UI_ESCAPE_MODE_ENABLE_TIPS_STRING);
    hinic3_ds_put_format(reply, "%4s%-30s%-s\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_ESCAPE_MODE_DISABLE_FORMAT_STRING, HINIC3_UI_ESCAPE_MODE_DISABLE_TIPS_STRING);
    hinic3_ds_put_format(reply, "%4s%-30s%-s\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_ESCAPE_MODE_SHOW_FORMAT_STRING, HINIC3_UI_ESCAPE_MODE_SHOW_TIPS_STRING);
    hinic3_ds_put_format(reply, "%4s%-30s%-s\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_ESCAPE_MODE_SHOW_HELP_STRING, HINIC3_UI_ESCAPE_MODE_SHOW_HELP_TIPS_STRING);
    return 0;
}

static int
hinic3_show_flow_escape_mode(struct ds *reply)
{
    int ret;
    uint8_t mode;
    uint8_t escape_mode;

    ret = hinic3_flow_get_forward_mode(&mode);
    if (ret != 0) {
        hinic3_ds_put_format(reply, HINIC3_UI_GET_ESCAPE_MODE_FAILED, HINIC3_UI_LEADING_SIGN_FAILURE, ret);
        return ret;
    }

    escape_mode = (HINIC3_ESCAPE_MODE_MASK & mode) >> HINIC3_ESCAPE_MODE_OFFSET;
    if (escape_mode == HINIC3_ESCAPE_MODE_NO_OFFLOAD) {
        hinic3_ds_put_format(reply, HINIC3_UI_SHOW_NO_FLOW_OFFLOAD_MODE, HINIC3_UI_LEADING_SIGN_INFO);
    } else if (escape_mode == HINIC3_ESCAPE_MODE_ALL_OFFLOAD) {
        hinic3_ds_put_format(reply, HINIC3_UI_SHOW_ALL_FLOW_OFFLOAD_MODE, HINIC3_UI_LEADING_SIGN_INFO);
    } else {
        hinic3_ds_put_format(reply, HINIC3_UI_ESCAPE_MODE_INVALID, HINIC3_UI_LEADING_SIGN_FAILURE, escape_mode);
        return -1;
    }

    return 0;
}

static void
hinic3_flow_resource_clear(void)
{
    struct rte_flow_error error = {0};

    int ret = hinic3_flow_flush_all(&error);
    if (ret != 0) {
        HINIC3_LOG(ERR, FLOW, "hinic3 flow resource clear failed, ret %d, error %d", ret, error.type);
        return;
    }

    if (hinic3_check_masked_to_exact_switch() == true)
        hinic3_ufid_map_flush();

    (void)hinic3_reset_offload_flow_nums();
}

static int
hinic3_set_escape_mode(struct ds *reply, bool escape_mode_enable)
{
    int ret;
    uint8_t mode;
    uint8_t version;
    uint8_t forward_mode;
    uint8_t escape_mode;
    struct hinic3_dp_extend_info *extend_info = hinic3_get_offload_extend_info();
    struct hinic3_flow_agent_db *hinic3_db = (struct hinic3_flow_agent_db *)extend_info->hw_offload;
    bool is_openovs = hinic3_check_masked_to_exact_switch();

    ret = hinic3_flow_get_forward_mode(&mode);
    if (ret != 0) {
        hinic3_ds_put_format(reply, HINIC3_UI_GET_ESCAPE_MODE_FAILED, HINIC3_UI_LEADING_SIGN_FAILURE, ret);
        return ret;
    }

    version = (HINIC3_MODE_VERSION_MASK & mode) >> HINIC3_MODE_VERSION_OFFSET;
    forward_mode = (HINIC3_FORWARD_MODE_MASK & mode) >> HINIC3_FORWARD_MODE_OFFSET;

    rte_spinlock_lock(&hinic3_db->operate_disable_lock);
    if (escape_mode_enable) {
        escape_mode = HINIC3_ESCAPE_MODE_NO_OFFLOAD;
        hinic3_db->operate_disable = HINIC3_FLOW_OFFLOAD_DISABLE;
    } else {
        escape_mode = HINIC3_ESCAPE_MODE_ALL_OFFLOAD;
        hinic3_db->operate_disable = HINIC3_FLOW_OFFLOAD_ENABLE;
    }
    hinic3_db->escape_mode = escape_mode;
    rte_spinlock_unlock(&hinic3_db->operate_disable_lock);

    if (escape_mode_enable && is_openovs)
        hinic3_flow_resource_clear();

    mode = (version << HINIC3_MODE_VERSION_OFFSET) | (forward_mode << HINIC3_FORWARD_MODE_OFFSET) |
        (escape_mode << HINIC3_ESCAPE_MODE_OFFSET);
    HINIC3_LOG(INFO, AGENT, "set escape mode : version %u, forward_mode %u, escape_mode %u.", version, forward_mode,
        escape_mode);
    ret = hinic3_flow_set_forward_mode(mode);
    if (ret != 0) {
        hinic3_ds_put_format(reply, HINIC3_UI_ESCAPE_MODE_SET_FAILED, HINIC3_UI_LEADING_SIGN_FAILURE, ret);
        return ret;
    }

    if (escape_mode_enable) {
        hinic3_ds_put_format(reply, "%sEscape mode enabled successfully.\n", HINIC3_UI_LEADING_SIGN_INFO);
    } else {
        hinic3_ds_put_format(reply, "%sEscape mode disabled successfully.\n", HINIC3_UI_LEADING_SIGN_INFO);
    }
    return 0;
}

static void
hinic3_flow_escape_mode_cmd(struct unixctl_conn *conn, int argc HINIC3_UNUSED, const char *argv[], void *aux HINIC3_UNUSED)
{
    struct ds reply = DS_EMPTY_INITIALIZER;
    int ret;
    enum {ARGC = 2};

    if (strcmp(argv[ARGC - 1], "disable") == 0) {
        ret = hinic3_set_escape_mode(&reply, false);
    } else if (strcmp(argv[ARGC - 1], "enable") == 0) {
        ret = hinic3_set_escape_mode(&reply, true);
    } else if (strcmp(argv[ARGC - 1], "show") == 0) {
        ret = hinic3_show_flow_escape_mode(&reply);
    } else if ((strcmp(argv[ARGC - 1], "-h") == 0) || (strcmp(argv[ARGC - 1], "--help") == 0)) {
        ret = hinic3_show_flow_escape_mode_help_info(&reply);
    } else {
        hinic3_ds_put_format(&reply, "%s%s, please input -h or --help to get help info.\n",
            HINIC3_UI_LEADING_SIGN_ERROR, HINIC3_UI_ERROR_WRONG_PARAMETER);
        ret = -1;
    }

    if (ret != 0) {
        hinic3_command_reply_error(conn, hinic3_ds_cstr(&reply));
    } else {
        hinic3_command_reply(conn, hinic3_ds_cstr(&reply));
    }
    hinic3_ds_destroy(&reply);
}

static void
hinic3_show_packet_detect_mode(struct ds *ds)
{
    if (g_packet_detect_mode == HINIC3_PACKET_DETECT_ENABLE) {
        hinic3_ds_put_format_prefix(ds, 0, HINIC3_UI_LEADING_SIGN_INFO,
            "packet detect mode is enabled.\n");
    }
    if (g_packet_detect_mode == HINIC3_PACKET_DETECT_DISABLE) {
        hinic3_ds_put_format_prefix(ds, 0, HINIC3_UI_LEADING_SIGN_INFO,
            "packet detect mode is disabled.\n");
    }
}

static int
hinic3_set_packet_detect_mode(struct ds *ds, enum hinic3_packet_detect_mode detect_mode)
{
    int ret = 0;
    uint8_t buf[8] = {0};
    struct hinic3_nlattr set_nla = {0};
    struct hinic3_drv_ops *ops = hinic3_get_drv_ops();

    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_global_cfg_set, HINIC3_DRV_FUNC_NO_PTR);
    hinic3_nlattr_init(&set_nla, buf, sizeof(buf));
    hinic3_nlattr_put_u8(&set_nla, HINIC3_GLOBAL_CFG_ARG_VXLAN_CHECK_UPCALL, detect_mode);

    ret = ops->hovs_global_cfg_set((struct nlattr *)set_nla.data, set_nla.used_len,
        (struct nlattr *)set_nla.data, &set_nla.used_len);
    if (ret != 0) {
        hinic3_ds_put_format_prefix(ds, 0, HINIC3_UI_LEADING_SIGN_ERROR,
            "hovs_global_cfg_set detect mode failed, ret is %d.\n", ret);
        HINIC3_LOG(ERR, AGENT, "hovs_global_cfg_set detect mode failed, ret is %d", ret);
        return hinic3_convert_error_code(ret);
    }
    g_packet_detect_mode = detect_mode;
    if (g_packet_detect_mode == HINIC3_PACKET_DETECT_ENABLE)
        HINIC3_LOG(INFO, AGENT, "packet detect mode is enabled.");
    if (g_packet_detect_mode == HINIC3_PACKET_DETECT_DISABLE)
        HINIC3_LOG(INFO, AGENT, "packet detect mode is disabled.");

    hinic3_show_packet_detect_mode(ds);
    return hinic3_convert_error_code(ret);
}

static void
hinic3_packet_detect_mode_help_info(struct ds *ds)
{
    hinic3_ds_put_format(ds, "%2s%s\n\n", HINIC3_UI_INDENT_SPACE, HINIC3_UI_DETECT_MODE_USAGE_STR);
    hinic3_ds_put_format(ds, "%2s%s\n", HINIC3_UI_INDENT_SPACE, HINIC3_UI_FLOW_OPTION_LIST_STRING);
    hinic3_ds_put_format(ds, "%4s%-25s%s\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_LOG_LIMIT_ENABLE_FORMAT_STR, HINIC3_UI_DETECT_MODE_ENABLE_TIP_STR);
    hinic3_ds_put_format(ds, "%4s%-25s%s\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_DETECT_MODE_DISABLE_FORMAT_STR, HINIC3_UI_DETECT_MODE_DISABLE_TIP_STR);
    hinic3_ds_put_format(ds, "%4s%-25s%s\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_LOG_LIMIT_SHOW_FORMAT_STR, HINIC3_UI_LOG_LIMIT_SHOW_TIP_STR);
    hinic3_ds_put_format(ds, "%4s%-25s%s\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_HELP_FORMAT_STR, HINIC3_UI_HELP_TIP_STR);
}

static void
hinic3_packet_detect_mode_cmd(struct unixctl_conn *conn, int argc __rte_unused, const char *argv[], void *aux)
{
    *(int *)aux = 0;
    struct ds reply = DS_EMPTY_INITIALIZER;
    
    if (strcmp(argv[1], "--disable") == 0) {
        *(int *)aux = hinic3_set_packet_detect_mode(&reply, HINIC3_PACKET_DETECT_DISABLE);
    } else if (strcmp(argv[1], "--enable") == 0) {
        *(int *)aux = hinic3_set_packet_detect_mode(&reply, HINIC3_PACKET_DETECT_ENABLE);
    } else if (strcmp(argv[1], "--show") == 0) {
        hinic3_show_packet_detect_mode(&reply);
    } else if ((strcmp(argv[1], "-h") == 0) || (strcmp(argv[1], "--help") == 0)) {
        hinic3_packet_detect_mode_help_info(&reply);
    } else {
        hinic3_ds_put_format(&reply, "%s%s, please input -h or --help to get help info.\n",
            HINIC3_UI_LEADING_SIGN_ERROR, HINIC3_UI_ERROR_WRONG_PARAMETER);
        *(int *)aux = -1;
    }

    if (*(int *)aux != 0) {
        hinic3_command_reply_error(conn, hinic3_ds_cstr(&reply));
    } else {
        hinic3_command_reply(conn, hinic3_ds_cstr(&reply));
    }
    hinic3_ds_destroy(&reply);
}

void
unixctl_hinic3_cmd_forward_register(void)
{
    if (hinic3_card_mod_get() != PROG_MODE) {
        hinic3_command_register("hwoff/set-forward-mode", "{ low_latency | high_throughput | { -h | --help } }",
            1, 1, hinic3_forward_mode_set_cmd, NULL);
        hinic3_command_register("hwoff/show-forward-mode", "", 0, 0, hinic3_forward_mode_get_cmd, NULL);
    }
    hinic3_command_register("hwoff/flow-escape-mode",
        "{ enable | disable | show | { -h | --help } }",
        1, 1, hinic3_flow_escape_mode_cmd, NULL);
    if (hinic3_user_scenario_get() == COM_BD) {  
        hinic3_command_register("hwoff/packet-detect-mode",
            "{ --enable | --disable | --show | { -h | --help } }",
            1, 1, hinic3_packet_detect_mode_cmd, NULL);
    }
}
