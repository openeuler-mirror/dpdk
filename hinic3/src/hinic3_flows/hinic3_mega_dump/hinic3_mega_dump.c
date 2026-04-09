/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2025 Huawei Technologies Co., Ltd
 */

#include "hinic3_ui_string.h"
#include "hinic3_error_stats.h"
#include "hinic3_command.h"
#include "hinic3_iface_flow.h"
#include "hinic3_mega_offload.h"
#include "hinic3_mega_dump_item.h"
#include "hinic3_mega_dump_format.h"
#include "hinic3_mega_dump.h"
#include "hinic3_rte_flow_format.h"

#define HINIC3_DFX_FLOW_ALLOC_MEM_BASE_SIZE 2048

static int
hinic3_mega_flow_dump_format(struct hinic3_dpif_flow_for_get *get, struct hinic3_dump_flow_info *info)
{
    int ret = INT_MAX;
    int err = 0;
    ret = hinic3_mega_item_build(get, info);
    if (ret != 0 && ret != ERANGE) {
        hinic3_mega_item_clean(info);
        return ret;
    } else if (ret == ERANGE) {
        err = ret;
    }

    info->hw_ufid = get->ol_ufid;

    return err;
}

static void
hinic3_flow_mega_format_to_ds(struct hinic3_dump_flow_info *flow, struct ds *ds)
{
    unsigned int i;
    struct hinic3_flow format = {0};
    for (i = 0; i < flow->useful_item_index; ++i) {
        format.items[i].type = flow->items[i].type;
        format.items[i].spec = flow->items[i].spec;
        format.items[i].mask = flow->items[i].mask;
    }
    format.items[i].type = RTE_FLOW_ITEM_TYPE_END;
    format.items[i].spec = NULL;
    format.items[i].mask = NULL;

    format.actions[0].type = RTE_FLOW_ACTION_TYPE_END;
    format.actions[0].conf = NULL;

    hinic3_format_mega_flow(&format, ds);
}

static void
hinic3_agent_mega_flow_format_output(struct hinic3_dpif_flow_for_get *f, struct ds *ds)
{
    struct hinic3_dump_flow_info flow_info = {0};
    (void)hinic3_mega_flow_dump_format(f, &flow_info);
    hinic3_flow_mega_format_to_ds(&flow_info, ds);

    hinic3_ds_put_format(ds, HINIC3_UI_FLOW_STATISTIC_STRING);
    hinic3_ds_put_format(ds, "%s(%llu),%s(%llu)\n",
        HINIC3_UI_FLOW_PACKET_STRING, f->stats.packet_count, HINIC3_UI_FLOW_BYTES_STRING, f->stats.byte_count);

    hinic3_mega_item_clean(&flow_info);
}

static void
hinic3_agent_dump_hinic3_mega_flows(struct unixctl_conn *conn,
    int argc HINIC3_UNUSED, const char *argv[] HINIC3_UNUSED, void *aux)
{
    int ret = 0;
    int total_flows = 0;
    struct ds ds = DS_EMPTY_INITIALIZER;
    struct hinic3_dpif_flow_for_get f = {0};
    void *state = NULL;
    if (argc > 1) {
        hinic3_ds_put_format(&ds, "%s%s, please input -h or -help to get help info.\n", HINIC3_UI_LEADING_SIGN_ERROR,
            HINIC3_UI_ERROR_UNRECOGNIZED_COMMAND);
        hinic3_command_reply_error(conn, hinic3_ds_cstr(&ds));
        *(int *)aux = -1;
        hinic3_ds_destroy(&ds);
        return;
    }

    struct hinic3_nlattr_obj keys[HINIC3_DFX_FLOW_ALLOC_MEM_BASE_SIZE];
    struct hinic3_nlattr_obj actions[HINIC3_DFX_FLOW_ALLOC_MEM_BASE_SIZE];
    struct hinic3_nlattr_obj masks[HINIC3_DFX_FLOW_ALLOC_MEM_BASE_SIZE];
    f.key = keys;
    f.actions = actions;
    f.mask = masks;

    ret = hinic3_mega_flow_dump_start(&state);
    if (ret != HIOVS_OK) {
        if (ret == HIOVS_EEMPTY) {
            hinic3_ds_put_format(
                &ds, "%s%s\n", HINIC3_UI_LEADING_SIGN_INFO, HINIC3_UI_FLOW_DUMP_NO_AVALIBLE_FLOW_STRING);
        } else {
            hinic3_ds_put_format(
                &ds, "%s%s\n", HINIC3_UI_LEADING_SIGN_FAILURE, HINIC3_UI_FLOW_DUMP_ALL_FLOW_START_FAILED_STRING);
        }
        goto err;
    }

    while (hinic3_mega_flow_dump_next(state, &f) == 0) {
        hinic3_agent_mega_flow_format_output(&f, &ds);
        total_flows += 1;
    }

    (void)hinic3_mega_flow_dump_done(state);
    hinic3_ds_put_format(&ds, "%2s%s%" PRIu32 "\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_FLOW_DUMP_TOTAL_FLOW_STRING, total_flows);
    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    *(int *)aux = 0;
    hinic3_ds_destroy(&ds);
    return;
err:
    hinic3_command_reply_error(conn, hinic3_ds_cstr(&ds));
    *(int *)aux = -1;
    hinic3_ds_destroy(&ds);
}

static void
hinic3_agent_dump_rte_flow_mega(struct unixctl_conn *conn, int argc HINIC3_UNUSED, const char *argv[] HINIC3_UNUSED, void *aux)
{
    uint32_t total_flows = 0;
    struct ds ds = DS_EMPTY_INITIALIZER;
    const struct hinic3_mega_table *table = hinic3_get_mega_table();
    for (int i = 0; i < HINIC3_MEGA_FLOW_MAX_NUM; ++i) {
        if (table->flow_table[i].is_used) {
            const struct hinic3_mega_flow *flow = &table->flow_table[i].flow;
            if (hinic3_format_rte_flow_mega(flow, &ds)){
                hinic3_ds_put_format(&ds, "%s%s\n", HINIC3_UI_LEADING_SIGN_FAILURE, HINIC3_UI_FLOW_DUMP_RTE_FAILED_STRING);
                *(int *)aux = -1;
                goto err;
            }
            total_flows++;
        }
    }
    if (total_flows == 0) {
        hinic3_ds_put_format(&ds, "%s%s\n", HINIC3_UI_LEADING_SIGN_INFO, HINIC3_UI_FLOW_DUMP_NO_AVALIBLE_FLOW_STRING);
        goto end;
    }
    hinic3_ds_put_format(&ds, "%2s%s%u\n", HINIC3_UI_INDENT_SPACE, HINIC3_UI_FLOW_DUMP_TOTAL_FLOW_STRING, total_flows);
end:
    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    *(int *)aux = 0;
    hinic3_ds_destroy(&ds);
    return;
err:
    hinic3_command_reply_error(conn, hinic3_ds_cstr(&ds));
    *(int *)aux = -1;
    hinic3_ds_destroy(&ds);
}

static int
hinic3_mega_dump_start(struct hinic3_flow_dump_context *context, struct rte_flow_error *error)
{
    if (context == NULL || error == NULL) {
        return rte_flow_error_set(error, EINVAL, RTE_FLOW_ERROR_TYPE_UNSPECIFIED, NULL,
             "dump mega flow start input param empty");
    }

    int ret = hinic3_mega_flow_dump_start(&(context->hiovs_state));
    if (ret != HIOVS_OK && ret != HIOVS_EEMPTY)
        hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_DUMP_START, 1);

    return ret;
}

static int
hinic3_mega_flow_dump_to_context(struct hinic3_flow_dump_context *dump_context, struct rte_flow_error *error)
{
    struct hinic3_dpif_flow_for_get dump_for_get;
    struct hinic3_dump_flow_info *flow_info = NULL;
    int cur_flow_index = 0;

    struct hinic3_nlattr_obj keys[HINIC3_DFX_FLOW_ALLOC_MEM_BASE_SIZE];
    struct hinic3_nlattr_obj actions[HINIC3_DFX_FLOW_ALLOC_MEM_BASE_SIZE];
    struct hinic3_nlattr_obj masks[HINIC3_DFX_FLOW_ALLOC_MEM_BASE_SIZE];

    dump_for_get.key = keys;
    dump_for_get.actions = actions;
    dump_for_get.mask = masks;

    int ret = hinic3_mega_flow_dump_next(dump_context->hiovs_state, &dump_for_get);
    if (ret != 0) {
        if (ret != HIOVS_EEMPTY) {
            return rte_flow_error_set(error, EPERM, RTE_FLOW_ERROR_TYPE_STATE, NULL,
                "mega flow dump : dump flow hovs failed");
        }
        return ret;
    }
    if (dump_for_get.ol_ufid == HINIC3_MEGA_FLOW_MAX_NUM) {
        return HINIC3_MEGA_FLOW_MAX_NUM;
    }
    cur_flow_index = dump_context->context_mem.cur_flow_num;
    flow_info = &(dump_context->context_mem.flows[cur_flow_index]);

    return hinic3_mega_flow_dump_format(&dump_for_get, flow_info);
}

static int
hinic3_mega_dump_next(struct hinic3_flow_dump_context *context, int count, struct rte_flow_error *error)
{
    if ((context == NULL) || (error == NULL) || (count <= 0)) {
        return rte_flow_error_set(error, EINVAL, RTE_FLOW_ERROR_TYPE_UNSPECIFIED, NULL,
             "dump mega flow next input param empty");
    }

    int ret = 0;
    int err = 0;
    for (int i = 0; i < count; ++i) {
        ret = hinic3_mega_flow_dump_to_context(context, error);
        if (ret == HINIC3_MEGA_FLOW_MAX_NUM) {
            /* 硬件流表数为0时，组件接口返回值为0，流表ufid填为最大规格值 */
            context->context_mem.cur_flow_num = 0;
            return 0;
        } else if (ret == HIOVS_EEMPTY) {
            return err;
        } else if (ret != HIOVS_OK) {
            err = ret;
        }
        context->context_mem.cur_flow_num++;
    }

    return err;
}

static int
hinic3_mega_dump_done(struct hinic3_flow_dump_context *context, struct rte_flow_error *error)
{
    if (context == NULL || error == NULL) {
        return rte_flow_error_set(error, EINVAL, RTE_FLOW_ERROR_TYPE_UNSPECIFIED, NULL,
             "dump mega flow done input param empty");
    }

    int ret =  hinic3_mega_flow_dump_done(context->hiovs_state);
    if (ret != 0)
        hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_DUMP_DONE, 1);

    return ret;
}

void
hinic3_mega_flow_dump_init(void)
{
    hinic3_set_dump_start(HINIC3_FLOW_TYPE_MEGA, hinic3_mega_dump_start);
    hinic3_set_dump_next(HINIC3_FLOW_TYPE_MEGA, hinic3_mega_dump_next);
    hinic3_set_dump_done(HINIC3_FLOW_TYPE_MEGA, hinic3_mega_dump_done);
}

static void hinic3_agent_dump_mega_flows_help(struct unixctl_conn *conn, void *aux)
{
    struct ds ds = DS_EMPTY_INITIALIZER;
    hinic3_ds_put_format(&ds, "%2s%s%s\n\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_CMD_USAGE_STRING, HINIC3_UI_DUMP_FUZZY_TIPS_STRING);
    hinic3_ds_put_format(&ds, "%2s%s\n", HINIC3_UI_INDENT_SPACE, HINIC3_UI_OPTION_LIST_STRING);
    hinic3_ds_put_format(&ds, "%4s%-30s%-s\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_DUMP_RTE_FLOW_HELP_STR, HINIC3_UI_DUMP_DP_HASH_HELP_STR);
    hinic3_ds_put_format(&ds, "%4s%-30s%-s\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_ESCAPE_MODE_SHOW_HELP_STRING, HINIC3_UI_ESCAPE_MODE_SHOW_HELP_TIPS_STRING);

    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    *(int *)aux = 0;
    hinic3_ds_destroy(&ds);
}

static void 
hinic3_agent_dump_mega_flows(struct unixctl_conn *conn, int argc, const char *argv[], void *aux)
{
    enum {
        HELP_ARGC = 2
    };
        
    if ((argc == HELP_ARGC) && ((strcmp(argv[1], "-h") == 0) || (strcmp(argv[1], "--help") == 0))) {
        hinic3_agent_dump_mega_flows_help(conn, aux);
        return;
    }

    if (hinic3_get_dump_rte_flow_options(argc, argv)) {
        hinic3_agent_dump_rte_flow_mega(conn, argc, argv, aux);
    } else {
        hinic3_agent_dump_hinic3_mega_flows(conn, argc, argv, aux);
    }
}

void
unixctl_hinic3_mega_flow_dump_cmd_init(void)
{
    hinic3_command_register("hwoff/dump-fuzzy-flows", "[ -r | { -h | --help } ]", 0, 1, hinic3_agent_dump_mega_flows, NULL);
}