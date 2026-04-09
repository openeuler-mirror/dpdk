/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2024 Huawei Technologies Co., Ltd
 */

#include "hinic3_agent_flow_cmd.h"
#include "hinic3_ui_string.h"
#include "hinic3_iface_flow_api_record.h"
#include "hinic3_iface_flow.h"
#include "hinic3_meminfo.h"
#include "hinic3_command.h"
#include "hinic3_driver_public.h"
#include "hinic3_ds.h"
#include <rte_atomic.h>
#define SHOW_FLOW_API_UI_FOUR_SPACE 4
#define SHOW_FLOW_API_UI_SIX_SPACE 6

static void
hinic3_agent_show_api_record_info(int index, hiovs_api_record *records, struct ds *reply, bool is_all)
{
   char time_str[HINIC3_TIME_STR_LEN] = {0};
    char *pstr = time_str;
    int value_ui_space;
    if (is_all) {
        value_ui_space = SHOW_FLOW_API_UI_SIX_SPACE;
    } else {
        value_ui_space = SHOW_FLOW_API_UI_FOUR_SPACE;
    }

    hinic3_ds_put_format(reply, "%*s" HINIC3_UI_FLOW_API_COST_MAX_TIME_STRING, value_ui_space, HINIC3_UI_INDENT_SPACE,
        records[index].max_api_time);
    hinic3_ds_put_format(reply, "%*s" HINIC3_UI_FLOW_API_COST_MIN_TIME_STRING, value_ui_space, HINIC3_UI_INDENT_SPACE,
        records[index].min_api_time);

    if ((uint64_t)rte_atomic64_read(&records[index].call_total_count) != 0) {
        hinic3_ds_put_format(reply, "%*s" HINIC3_UI_FLOW_API_COST_AVERAGE_TIME_STRING, value_ui_space,
            HINIC3_UI_INDENT_SPACE,
            records[index].total_api_time / (uint64_t)rte_atomic64_read(&records[index].call_total_count));
    } else {
        hinic3_ds_put_format(reply, "%*s" HINIC3_UI_FLOW_API_COST_AVERAGE_TIME_STRING, value_ui_space,
            HINIC3_UI_INDENT_SPACE, (uint64_t)0);
    }

    hinic3_ds_put_format(reply, "%*s" HINIC3_UI_FLOW_API_TOTAL_CALL_TIMES_STRING, value_ui_space, HINIC3_UI_INDENT_SPACE,
        (uint64_t)rte_atomic64_read(&records[index].call_total_count));
    hinic3_ds_put_format(reply, "%*s" HINIC3_UI_FLOW_API_ERROR_CALL_TIMES_STRING, value_ui_space, HINIC3_UI_INDENT_SPACE,
        (uint64_t)rte_atomic64_read(&records[index].call_error_count));

    if ((uint64_t)rte_atomic64_read(&records[index].call_error_count) != 0) {
        ctime_r(&records[index].time_error, pstr);
        hinic3_ds_put_format(reply, "%*s" HINIC3_UI_FLOW_API_LATEST_ERROR_CALL_INFO_STRING, value_ui_space,
            HINIC3_UI_INDENT_SPACE, records[index].last_rtn_value, pstr);
    }
}

static int
hinic3_agent_show_single_flow_api(int index, const char *argv[], struct ds *reply, hiovs_api_record *records)
{
    int err = hinic3_flow_get_api_record(argv[index], false, false, records, 1);
    if (err != 0) {
        hinic3_ds_put_format(reply, HINIC3_UI_LEADING_SIGN_ERROR HINIC3_UI_ERROR_WRONG_PARAMETER
            HINIC3_UI_FLOW_UNKOWN_API_STRING, argv[index]);
        return err;
    }

    hinic3_ds_put_format(reply, "%2s" HINIC3_UI_FLOW_QUERY_API_NAME_STRING, HINIC3_UI_INDENT_SPACE, argv[index]);
    hinic3_agent_show_api_record_info(HINIC3_DEFAULT_API_RECORD_INDEX, records, reply, false);
    return err;
}

static int
hinic3_agent_process_single_flow_api(int argc, const char *argv[], struct ds *reply)
{
    enum {
        ARG_API = 1,
        ARG_NAME = 2,
        ARGC = 3
    };
    int ret;
    hiovs_api_record tmp_records = {0};
    hiovs_api_record *records = &tmp_records;

    if (argc != ARGC) {
        hinic3_ds_put_format(reply, HINIC3_UI_LEADING_SIGN_ERROR HINIC3_UI_FLOW_OPTIONS_WRONG_ARGUMENTS_STRING,
            argv[ARG_API], ARG_NAME);
        return -1;
    }

    ret = hinic3_agent_show_single_flow_api(ARG_NAME, argv, reply, records);
    return ret;
}

static void
hinic3_agent_show_per_flow_api_info(int index, hiovs_api_record *records, struct ds *ds)
{
    const char *api_name = hinic3_flow_get_api_name(index);
    if (api_name == NULL)
        return;

    hinic3_ds_put_format(ds, "%4s" HINIC3_UI_FLOW_QUERY_API_NAME_STRING, HINIC3_UI_INDENT_SPACE, api_name);
    hinic3_agent_show_api_record_info(index, records, ds, true);
}

static int
hinic3_agent_show_all_flow_api_info(int argc, struct ds *ds)
{
    enum {
        ARGC = 2
    };
    if (argc > ARGC) {
        hinic3_ds_put_format(ds, HINIC3_UI_LEADING_SIGN_ERROR HINIC3_UI_FLOW_REQUIRES_WRONG_ARGUMENT_STRING, ARGC - 1);
        return -1;
    }

    hiovs_api_record *records =
        (hiovs_api_record *)hinic3_calloc(HINIC3_FLOW_API_MAX, sizeof(hiovs_api_record), HINIC3_COMMAND);
    if (records == NULL) {
        hinic3_ds_put_format(ds, HINIC3_UI_LEADING_SIGN_ERROR HINIC3_UI_FLOW_ALLOC_RECORDS_ERROR_STRING);
        return -1;
    }

    int ret = hinic3_flow_get_api_record("", true, false, records, HINIC3_FLOW_API_MAX);
    if (ret != 0) {
        hinic3_ds_put_format(ds, HINIC3_UI_LEADING_SIGN_FAILURE HINIC3_UI_FLOW_QUERY_RECORDS_FAILED_STRING,
            HINIC3_UI_FLOW_ALL_STRING);
        hinic3_free(records);
        return ret;
    }

    hinic3_ds_put_format(ds, "%2s" HINIC3_UI_FLOW_API_COUNT_STRING, HINIC3_UI_INDENT_SPACE, HINIC3_FLOW_API_MAX);

    for (int i = 0; i < HINIC3_FLOW_API_MAX; ++i) {
        hinic3_agent_show_per_flow_api_info(i, records, ds);
        if (i != HINIC3_FLOW_API_MAX - 1) {
            hinic3_ds_put_format(ds, "\n");
        }
    }

    hinic3_free(records);
    return ret;
}

static int
hinic3_agent_show_flow_api_help(int argc, struct ds *ds)
{
    const char *api_name = NULL;
    enum {
        ARGC = 2
    };

    if (argc > ARGC) {
        hinic3_ds_put_format(ds, HINIC3_UI_FLOW_REQUIRES_WRONG_ARGUMENT_STRING, ARGC - 1);
        return -1;
    }

    hinic3_ds_put_format(ds, "%2s%s", HINIC3_UI_INDENT_SPACE, HINIC3_UI_FLOW_SHOW_FLOW_API_HELP_STRING);
    hinic3_ds_put_format(ds, "\n");
    hinic3_ds_put_format(ds, "%2s%s\n", HINIC3_UI_INDENT_SPACE, HINIC3_UI_FLOW_OPTION_LIST_STRING);
    hinic3_ds_put_format(ds, "%4s%s", HINIC3_UI_INDENT_SPACE, HINIC3_UI_FLOW_SHOW_ALL_API_HELP_STRING);
    hinic3_ds_put_format(ds, "%4s%s", HINIC3_UI_INDENT_SPACE, HINIC3_UI_FLOW_CLEAR_ALL_API_HELP_STRING);
    hinic3_ds_put_format(ds, "%4s%s", HINIC3_UI_INDENT_SPACE, HINIC3_UI_FLOW_CLEAR_ONE_API_HELP_STRING);
    hinic3_ds_put_format(ds, "%4s%s", HINIC3_UI_INDENT_SPACE, HINIC3_UI_FLOW_SHOW_ONE_API_HELP_STRING);
    hinic3_ds_put_format(ds, "%4s%s", HINIC3_UI_INDENT_SPACE, HINIC3_UI_FLOW_HELP_STRING);
    hinic3_ds_put_format(ds, "\n");
    hinic3_ds_put_format(ds, "%2s" HINIC3_UI_FLOW_ALL_API_TIPS_STRING, HINIC3_UI_INDENT_SPACE);

    for (int i = 0; i < HINIC3_FLOW_API_MAX; ++i) {
        api_name = hinic3_flow_get_api_name(i);
        if (api_name == NULL) {
            continue;
        }
        if (i % HINIC3_SHOW_API_NUM_PER_LINE == 0) {
            hinic3_ds_put_format(ds, "\n");
            hinic3_ds_put_format(ds, "%4s", HINIC3_UI_INDENT_SPACE);
        }
        hinic3_ds_put_format(ds, "%-40s", api_name);
    }
    hinic3_ds_put_format(ds, "\n");
    return 0;
}

static int
hinic3_agent_clear_api_info(int argc, const char *argv[], struct ds *ds)
{
    int ret;
    enum {
        ARGC = 3
    };
    hiovs_api_record tmp_records = {0};
    hiovs_api_record *records = &tmp_records;

    if (argc != ARGC) {
        hinic3_ds_put_format(ds, HINIC3_UI_LEADING_SIGN_ERROR HINIC3_UI_FLOW_REQUIRES_WRONG_ARGUMENT_STRING, ARGC - 1);
        return -1;
    }

    if (strcmp("all", argv[ARGC - 1]) == 0) {
        ret = hinic3_flow_get_api_record("", true, true, NULL, 0);
        if (ret != 0) {
            HINIC3_LOG(INFO, AGENT, "clear all flow api record failed.");
            hinic3_ds_put_format(ds, HINIC3_UI_LEADING_SIGN_ERROR HINIC3_UI_FLOW_CLEAR_RECORDS_FAILED_STRING,
                HINIC3_UI_FLOW_ALL_STRING);
            return ret;
        }
        HINIC3_LOG(INFO, AGENT, "clear all flow api record successful.");
        hinic3_ds_put_format(ds, HINIC3_UI_LEADING_SIGN_INFO HINIC3_UI_FLOW_CLEAR_RECORDS_SUCCESS_STRING,
            HINIC3_UI_FLOW_ALL_STRING);
        return ret;
    }
    ret = hinic3_flow_get_api_record(argv[ARGC - 1], false, true, records, 1);
    if (ret != 0) {
        HINIC3_LOG(INFO, AGENT, "clear flow api record failed.");
        hinic3_ds_put_format(ds, HINIC3_UI_LEADING_SIGN_FAILURE HINIC3_UI_FLOW_CLEAR_RECORDS_FAILED_STRING,
            argv[ARGC - 1]);
    } else {
        HINIC3_LOG(INFO, AGENT, "clear flow api record success.");
        hinic3_ds_put_format(ds, HINIC3_UI_LEADING_SIGN_INFO HINIC3_UI_FLOW_CLEAR_RECORDS_SUCCESS_STRING,
            argv[ARGC - 1]);
    }
    return ret;
}

void
hinic3_agent_show_flow_api(struct unixctl_conn *conn, int argc, const char *argv[], void *aux)
{
    struct ds reply = DS_EMPTY_INITIALIZER;
    enum {
        ARGC = 2
    };

    int argv_index = ARGC - 1;
    int option_val = hinic3_find_option(argv[argv_index]);
    int ret = -1;
    if (option_val == -1) {
        hinic3_ds_put_format(&reply, HINIC3_UI_LEADING_SIGN_ERROR HINIC3_UI_FLOW_INPUT_UNKOWN_OPTION_STRING,
            argv[argv_index]);
        goto out;
    }

    switch (option_val) {
        case HINIC3_FLOW_API_NAME_OPT:
            ret = hinic3_agent_process_single_flow_api(argc, argv, &reply);
            break;

        case HINIC3_FLOW_ALL_OPT:
            ret = hinic3_agent_show_all_flow_api_info(argc, &reply);
            break;

        case HINIC3_FLOW_CLEAR_OPT:
            ret = hinic3_agent_clear_api_info(argc, argv, &reply);
            break;

        case HELP_OPT:
            ret = hinic3_agent_show_flow_api_help(argc, &reply);
            break;

        default:
            hinic3_ds_put_format(&reply, HINIC3_UI_FLOW_INPUT_UNKOWN_OPTION_STRING, argv[argv_index]);
            break;
    }

out:
    if (ret != 0) {
        hinic3_command_reply_error(conn, hinic3_ds_cstr(&reply));
        *(int *)aux = -1;
    } else {
        hinic3_command_reply(conn, hinic3_ds_cstr(&reply));
        *(int *)aux = 0;
    }
    hinic3_ds_destroy(&reply);
}
