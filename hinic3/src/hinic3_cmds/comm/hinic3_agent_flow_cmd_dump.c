/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2024 Huawei Technologies Co., Ltd
 */

#include "hinic3_agent_flow_cmd_dump.h"
#include "hinic3_agent_flow_cmd.h"
#include "hinic3_agent_cmd_format.h"
#include "hinic3_parse_agent_config.h"
#include "hinic3_iface_flow.h"
#include "hinic3_ui_string.h"
#include "hinic3_file_util.h"
#include "hinic3_meminfo.h"
#include "hinic3_string_util.h"
#include "hinic3_dfx_multi_qos.h"
#include "hinic3_offload_flow_public.h"
#include "hinic3_ufid_map_rte_flow.h"
#include "hinic3_rte_flow_format.h"
#include "hinic3_flexda_flow_public.h"
#include "hinic3_agent_cmd_format_flexda.h"

static struct hinic3_flow_dump_cmd g_hinic3_flow_dump_cmd;
static struct hinic3_dump_all_flows_mgmt_t dump_flows_mgmt = {
    .dumping = HINIC3_DUMP_FLOWS_IDLE,
    .start = NULL,
    .file = NULL,
    .is_dpak_flow = false};

struct hinic3_dump_all_flows_mgmt_t *
hinic3_cmd_get_flow_dump_mgmt(void)
{
    return &dump_flows_mgmt;
}

struct hinic3_flow_dump_cmd *
hinic3_cmd_get_flow_dump_ops(void)
{
    return &g_hinic3_flow_dump_cmd;
}

void hinic3_cmd_set_flow_get_maxflows_ops(int (*ops)(uint32_t, uint32_t *))
{
    g_hinic3_flow_dump_cmd.hinic3_flow_get_maxflows_ops = ops;
}

void hinic3_cmd_set_flow_dump_start_ops(int (*ops)(void **))
{
    g_hinic3_flow_dump_cmd.hinic3_flow_dump_start_ops = ops;
}

void hinic3_cmd_set_flow_dump_start_by_table_id_ops(int (*ops)(uint32_t, void **))
{
    g_hinic3_flow_dump_cmd.hinic3_flow_dump_start_by_table_id_ops = ops;
}

void hinic3_cmd_set_flow_dump_next_ops(int (*ops)(void *, struct hinic3_dpif_flow_for_get *))
{
    g_hinic3_flow_dump_cmd.hinic3_flow_dump_next_ops = ops;
}

void hinic3_cmd_set_flow_dump_done_ops(int (*ops)(void *))
{
    g_hinic3_flow_dump_cmd.hinic3_flow_dump_done_ops = ops;
}

void hinic3_cmd_set_flow_key_format_output_ops(void (*ops)(const struct hinic3_nlattr *, struct ds *))
{
    g_hinic3_flow_dump_cmd.hinic3_flow_key_format_output_ops = ops;
}

void hinic3_cmd_set_flow_ops_status(enum hinic3_flow_type type)
{
    g_hinic3_flow_dump_cmd.type = type;
}

static void
hinic3_agent_dump_hinic3_emc_flow_init(void)
{
    hinic3_cmd_set_flow_get_maxflows_ops(hinic3_flow_get_maxflows);
    if (hinic3_card_mod_get() == PROG_MODE)
    {
        hinic3_cmd_set_flow_dump_start_by_table_id_ops(hinic3_flow_dump_start_by_table_id);
        hinic3_cmd_set_flow_key_format_output_ops(hinic3_flexda_flow_key_format_output);
    }
    else
    {
        hinic3_cmd_set_flow_key_format_output_ops(hinic3_flow_key_format_output);
    }
    hinic3_cmd_set_flow_dump_start_ops(hinic3_flow_dump_start);
    hinic3_cmd_set_flow_dump_next_ops(hinic3_flow_dump_next);
    hinic3_cmd_set_flow_dump_done_ops(hinic3_flow_dump_done);
    hinic3_cmd_set_flow_ops_status(HINIC3_FLOW_TYPE_EMC);
}

static int
hinic3_agent_set_dump_start(struct hinic3_dump_all_flows_mgmt_t *mgmt, struct ds *ds)
{
    hinic3_pthread_mutex_lock(&mgmt->mutex_lock);
    if (mgmt->dumping != HINIC3_DUMP_FLOWS_IDLE)
    {
        hinic3_pthread_mutex_unlock(&mgmt->mutex_lock);
        hinic3_ds_put_format(ds, "%s%s\n", HINIC3_UI_LEADING_SIGN_FAILURE, HINIC3_UI_FLOW_DUMP_ALL_FLOW_TIPS_STRING);
        return -1;
    }
    mgmt->dumping = HINIC3_DUMP_FLOWS_RUNNING;
    hinic3_pthread_mutex_unlock(&mgmt->mutex_lock);
    return 0;
}

static void
hinic3_agent_dump_flows_handle_ret(int ret, struct ds *ds)
{
    if (ret != HOWFF_NO_FLOW_ERR)
    {
        hinic3_ds_put_format(ds, "%s%s\n", HINIC3_UI_LEADING_SIGN_FAILURE,
                             HINIC3_UI_FLOW_DUMP_ALL_FLOW_START_FAILED_STRING);
    }
    else
    {
        hinic3_ds_put_format(ds, "%s%s\n", HINIC3_UI_LEADING_SIGN_INFO, HINIC3_UI_FLOW_DUMP_NO_AVALIBLE_FLOW_STRING);
    }
}

int hinic3_alloc_mem_for_flow(struct hinic3_dpif_flow_for_get *f)
{
    if (f->key == NULL)
    {
        f->key = hinic3_calloc(1, HINIC3_DFX_FLOW_ALLOC_MEM_BASE_SIZE, HINIC3_COMMAND);
        if (f->key == NULL)
        {
            HINIC3_LOG(WARNING, AGENT, "calloc memory for hw flow key error.");
            return -1;
        }
    }

    if (f->actions == NULL)
    {
        f->actions = hinic3_calloc(1, HINIC3_DFX_FLOW_ALLOC_MEM_BASE_SIZE, HINIC3_COMMAND);
        if (f->actions == NULL)
        {
            HINIC3_LOG(WARNING, AGENT, "calloc memory for hw flow actions error.");
            hinic3_free(f->key);
            f->key = NULL;
            return -1;
        }
    }

    if (f->mask == NULL)
    {
        f->mask = hinic3_calloc(1, HINIC3_DFX_FLOW_ALLOC_MEM_BASE_SIZE, HINIC3_COMMAND);
        if (f->mask == NULL)
        {
            HINIC3_LOG(WARNING, AGENT, "calloc memory for hw flow mask error.");
            hinic3_free(f->key);
            f->key = NULL;
            hinic3_free(f->actions);
            f->actions = NULL;
            return -1;
        }
    }
    return 0;
}

void hinic3_agent_free_mem_for_flow(struct hinic3_dpif_flow_for_get *f)
{
    if (f->key != NULL)
    {
        hinic3_free(f->key);
        f->key = NULL;
    }
    f->key_len = 0;

    if (f->actions != NULL)
    {
        hinic3_free(f->actions);
        f->actions = NULL;
    }
    f->action_len = 0;

    if (f->mask != NULL)
    {
        hinic3_free(f->mask);
        f->mask = NULL;
    }
    f->mask_len = 0;
}

static int
hinic3_agent_dump_all_flows_to_screen(struct hinic3_dpif_flow_for_get *f,
                                      struct hinic3_dump_all_flows_mgmt_t *mgmt, struct ds *ds)
{
    int ret;
    void *state = NULL;
    uint64_t total_loss_ct_pkts = 0;
    uint32_t total_flows = 0;

    if (hinic3_card_mod_get() == PROG_MODE) {
       ret = hinic3_flexda_cmd_flow_dump_start_by_table_id(1, &state);
    } else {
       ret = hinic3_cmd_flow_dump_start(&state);
    }

    if (ret != 0)
    {
        hinic3_agent_dump_flows_handle_ret(ret, ds);
        hinic3_set_dumping_status(mgmt, HINIC3_DUMP_FLOWS_IDLE);
        return -1;
    }

    ret = hinic3_alloc_mem_for_flow(f);
    if (ret != 0)
    {
        hinic3_ds_put_format(ds, "%s%s\n", HINIC3_UI_LEADING_SIGN_FAILURE, HINIC3_UI_FLOW_ALLOC_FLOW_INFO_ERROR_STRING);
        (void)hinic3_cmd_flow_dump_done(state);
        hinic3_set_dumping_status(mgmt, HINIC3_DUMP_FLOWS_IDLE);
        return -1;
    }

    total_flows = 0;
    while ((hinic3_cmd_flow_dump_next(state, f)) == 0 && total_flows <= HINIC3_DUMP_FLOWS_MAX_ONCE)
    {
        hinic3_agent_flow_format_output(HINIC3_HYDRA_TYPE_TABLE_START, f, ds);
        hinic3_ds_put_format(ds, "\n");
        total_loss_ct_pkts += f->stats.ct_loss_pkts;
        total_flows++;
    }

    (void)hinic3_cmd_flow_dump_done(state);
    hinic3_ds_put_format(ds, "%2s%s%" PRIu32 ", %s%" PRIu64 "\n", HINIC3_UI_INDENT_SPACE,
                         HINIC3_UI_FLOW_DUMP_TOTAL_FLOW_STRING, total_flows,
                         HINIC3_UI_FLOW_DUMP_TOTAL_CT_LOST_PKT_STRING, total_loss_ct_pkts);

    if (total_flows > HINIC3_DUMP_FLOWS_MAX_ONCE)
    {
        hinic3_ds_put_format(ds, HINIC3_UI_FLOW_UP_DISPLAY_MAX_TIPS_STRING,
                             HINIC3_UI_LEADING_SIGN_INFO, HINIC3_DUMP_FLOWS_MAX_ONCE);
    }

    hinic3_agent_free_mem_for_flow(f);
    hinic3_set_dumping_status(mgmt, HINIC3_DUMP_FLOWS_IDLE);
    return 0;
}

static int
hinic3_agent_dump_flows(struct ds *ds)
{
    uint32_t total_flows = 0;
    struct hinic3_dump_all_flows_mgmt_t *mgmt = &dump_flows_mgmt;
    struct hinic3_dpif_flow_for_get f;

    memset(&f, 0, sizeof(struct hinic3_dpif_flow_for_get));
    
    if (hinic3_agent_set_dump_start(mgmt, ds) != 0)
        return -1;

    int ret = hinic3_cmd_flow_get_maxflows(HINIC3_HYDRA_TYPE_TABLE_START, &total_flows);
    if (ret != 0)
    {
        hinic3_ds_put_format(ds, "%s%s\n", HINIC3_UI_LEADING_SIGN_FAILURE,
                             HINIC3_UI_FLOW_DUMP_GET_FLOW_COUNT_FAILED_STRING);
        hinic3_set_dumping_status(mgmt, HINIC3_DUMP_FLOWS_IDLE);
        return -1;
    }

    if (total_flows > HINIC3_DUMP_FLOWS_MAX_ONCE)
    {
        hinic3_ds_put_format(ds, HINIC3_UI_FLOW_UP_MAX_TIPS_STRING, HINIC3_UI_INDENT_SPACE,
                             total_flows, HINIC3_DUMP_FLOWS_MAX_ONCE);
        hinic3_set_dumping_status(mgmt, HINIC3_DUMP_FLOWS_IDLE);
        return 0;
    }

    return hinic3_agent_dump_all_flows_to_screen(&f, mgmt, ds);
}

static int
hinic3_agent_dump_flow_by_ufid(uint32_t table_id, const uint64_t *ufid, struct ds *ds)
{
    struct hinic3_dpif_flow_for_get f = {0};
    int ret;

    ret = hinic3_alloc_mem_for_flow(&f);
    if (ret != 0)
    {
        hinic3_ds_put_format(ds, "%s%s\n", HINIC3_UI_LEADING_SIGN_FAILURE, HINIC3_UI_FLOW_ALLOC_FLOW_INFO_ERROR_STRING);
        return -1;
    }

    ret = hinic3_flow_get_by_ufid(*ufid, &f, table_id);

    if (ret != 0)
    {
        if (ret != HOWFF_NO_FLOW_ERR)
        {
            hinic3_ds_put_format(ds, "%s%s\n", HINIC3_UI_LEADING_SIGN_FAILURE, HINIC3_UI_FLOW_DUMP_FLOW_ERROR_STRING);
        }
        else
        {
            hinic3_ds_put_format(ds, "%s%s\n", HINIC3_UI_LEADING_SIGN_INFO, HINIC3_UI_FLOW_DUMP_NO_AVALIBLE_FLOW_STRING);
        }
        hinic3_agent_free_mem_for_flow(&f);
        return -1;
    }
    hinic3_agent_flow_format_output(table_id, &f, ds);
    hinic3_agent_free_mem_for_flow(&f);
    return 0;
}

static int
hinic3_flexda_agent_dump_specific_flow(int argc, const char *argv[], struct ds *ds)
{
    enum
    {
        ARGC = 5
    };
    uint64_t ufid;
    if (argc != ARGC)
    {
        hinic3_ds_put_format(ds, "%s%s, please input -h or --help to get help info.\n",
                             HINIC3_UI_LEADING_SIGN_ERROR, HINIC3_UI_ERROR_INCOMPLETE_COMMAND);
        return -1;
    }

    int ret = hinic3_parse_hw_ufid_from_string(argv[ARGC - 3], &ufid);
    if (ret != 0)
    {
        hinic3_ds_put_format(ds, "%s%s", HINIC3_UI_LEADING_SIGN_ERROR, HINIC3_UI_UFID_MAP_HW_UFID_FORMART_ERROR_STRING);
        return -1;
    }

    uint32_t table_id = HINIC3_HYDRA_TYPE_TABLE_START;
    ret = hinic3_parse_uint32_from_string(argv[ARGC - 1], &table_id);
    if (ret != 0) {
        hinic3_ds_put_format(ds, "Error: %s, invalid table id.\n", HINIC3_UI_ERROR_WRONG_PARAMETER);
        return -1;
    }
    return hinic3_agent_dump_flow_by_ufid(table_id, &ufid, ds);
}

static int
hinic3_agent_dump_specific_flow(int argc, const char *argv[], struct ds *ds)
{
    enum {
        ARGC = 3
    };
    uint64_t ufid;
    if (argc != ARGC) {
        hinic3_ds_put_format(ds, "%s%s, please input -h or --help to get help info.\n",
            HINIC3_UI_LEADING_SIGN_ERROR, HINIC3_UI_ERROR_INCOMPLETE_COMMAND);
        return -1;
    }

    int ret = hinic3_parse_hw_ufid_from_string(argv[ARGC - 1], &ufid);
    if (ret != 0) {
        hinic3_ds_put_format(ds, "%s%s", HINIC3_UI_LEADING_SIGN_ERROR, HINIC3_UI_UFID_MAP_HW_UFID_FORMART_ERROR_STRING);
        return -1;
    }
    return hinic3_agent_dump_flow_by_ufid(0, &ufid, ds);
}

static void
hinic3_agent_dump_flows_help(struct ds *ds)
{
    if (hinic3_card_mod_get() == PROG_MODE){
        hinic3_ds_put_format(ds, "%2s%sdpak-ovs-ctl hwoff/dump-hwoff-flows [ ufid <hw-ufid> -t <table-id> | -n [ -t <table-id> ] |"
                            " -f <file-name> | -i <block-id> | -t <table-id> | check | stop | { -h | --help } | -r [ { -f <file-name> | -t <table-id> } ] ]\n\n",
                        HINIC3_UI_INDENT_SPACE,
                        HINIC3_UI_CMD_USAGE_STRING);
    } else {
        hinic3_ds_put_format(ds, "%2s%sdpak-ovs-ctl hwoff/dump-hwoff-flows [ ufid <hw-ufid> | -n |"
                            " -f <file-name> | -i <block-id> | check | stop | { -h | --help } | -r [ -f <file-name>] ]\n\n",
                        HINIC3_UI_INDENT_SPACE,
                        HINIC3_UI_CMD_USAGE_STRING);
    }
    hinic3_ds_put_format(ds, "%2s%s\n", HINIC3_UI_INDENT_SPACE, HINIC3_UI_FLOW_OPTION_LIST_STRING);
    hinic3_ds_put_format(ds, "%4s%-30s%-s\n", HINIC3_UI_INDENT_SPACE,
                         HINIC3_UI_FLOW_DUMP_UFID_FORMAT_STRING, HINIC3_UI_FLOW_DUMP_UFID_TIPS_STRING);
    hinic3_ds_put_format(ds, "%4s%-30s%-s\n", HINIC3_UI_INDENT_SPACE,
                         HINIC3_UI_FLOW_DUMP_BLOCK_ID_FORMAT_STRING, HINIC3_UI_FLOW_DUMP_BLOCK_ID_TIPS_STRING);
    hinic3_ds_put_format(ds, "%4s%-30s%-s\n", HINIC3_UI_INDENT_SPACE,
                         HINIC3_UI_FLOW_DUMP_FILE_FORMAT_STRING, HINIC3_UI_FLOW_DUMP_FILE_TIPS_STRING);
    hinic3_ds_put_format(ds, "%4s%-30s%-s\n", HINIC3_UI_INDENT_SPACE,
                         HINIC3_UI_FLOW_DUMP_COUNT_FORMAT_STRING, HINIC3_UI_FLOW_DUMP_COUNT_TIPS_STRING);
    hinic3_ds_put_format(ds, "%4s%-30s%-s\n", HINIC3_UI_INDENT_SPACE,
                         HINIC3_UI_FLOW_DUMP_CHECK_FORMAT_STRING, HINIC3_UI_FLOW_DUMP_CHECK_TIPS_STRING);
    hinic3_ds_put_format(ds, "%4s%-30s%-s\n", HINIC3_UI_INDENT_SPACE,
                         HINIC3_UI_FLOW_DUMP_STOP_FORMAT_STRING, HINIC3_UI_FLOW_DUMP_STOP_TIPS_STRING);
    hinic3_ds_put_format(ds, "%4s%-30s%-s\n", HINIC3_UI_INDENT_SPACE,
                         HINIC3_UI_FLOW_DUMP_HELP_FORMAT_STRING, HINIC3_UI_FLOW_DUMP_HELP_TIPS_STRING);
    hinic3_ds_put_format(ds, "%4s%-30s%-s\n", HINIC3_UI_INDENT_SPACE,
                         HINIC3_UI_FLOW_DUMP_RTE_FLOW_FILE_FORMAT_STRING, HINIC3_UI_FLOW_DUMP_RTE_FILE_TIPS_STRING);
}

static int
hinic3_agent_get_flows_cnt(int argc, struct ds *ds)
{
    enum
    {
        ARGC = 2
    };
    if (argc != ARGC)
    {
        hinic3_ds_put_format(ds, "%s%s, please input -h or --help to get help info.\n",
                             HINIC3_UI_LEADING_SIGN_ERROR, HINIC3_UI_ERROR_TOO_MANY_PARAMETER);
        return -1;
    }

    uint32_t max_flow_cnt;
    int ret = hinic3_cmd_flow_get_maxflows(HINIC3_HYDRA_TYPE_TABLE_START, &max_flow_cnt);
    if (ret != 0)
    {
        hinic3_ds_put_format(ds, "%s%s\n", HINIC3_UI_LEADING_SIGN_FAILURE,
                             HINIC3_UI_FLOW_DUMP_GET_FLOW_COUNT_FAILED_STRING);
        return -1;
    }
    hinic3_ds_put_format(ds, HINIC3_UI_FLOW_DUMP_FLOW_COUNT_STRING, max_flow_cnt);

    return 0;
}

static int
hinic3_flexda_agent_get_flows_cnt(int argc, const char *argv[], struct ds *ds)
{
    enum
    {
        ARGC = 2,
        ARG_TABLE_ID = 4
    };
    if (argc == ARGC) {
        uint32_t max_flow_cnt;
        int ret = hinic3_cmd_flow_get_maxflows(HINIC3_HYDRA_TYPE_TABLE_START, &max_flow_cnt);
        if (ret != 0)
        {
            hinic3_ds_put_format(ds, "%s%s\n", HINIC3_UI_LEADING_SIGN_FAILURE,
                                HINIC3_UI_FLOW_DUMP_GET_FLOW_COUNT_FAILED_STRING);
            return -1;
        }
        hinic3_ds_put_format(ds, HINIC3_UI_FLOW_DUMP_FLOW_COUNT_STRING, max_flow_cnt);
    } else if (argc == ARG_TABLE_ID && (strcmp("-t", argv[ARGC]) == 0)) {
        uint32_t table_id;
        int ret = hinic3_parse_uint32_from_string(argv[ARG_TABLE_ID - 1], &table_id);
        if (ret != 0) {
                    hinic3_ds_put_format(ds, "Error: %s, invalid table id.\n", HINIC3_UI_ERROR_WRONG_PARAMETER);
                                return -1;
        }
        uint32_t max_flow_cnt;
        ret = hinic3_cmd_flow_get_maxflows(table_id, &max_flow_cnt);
        if (ret != 0) {
            hinic3_ds_put_format(ds, "%s%s\n", HINIC3_UI_LEADING_SIGN_FAILURE, HINIC3_UI_FLOW_DUMP_GET_FLOW_COUNT_FAILED_STRING);
            return -1;
        }
            hinic3_ds_put_format(ds, HINIC3_UI_FLOW_DUMP_FLOW_COUNT_BY_TABLE_ID_STRING, table_id, max_flow_cnt);
    } else {
        hinic3_ds_put_format(ds, "%s%s, please input -h or --help to get help info.\n",
                             HINIC3_UI_LEADING_SIGN_ERROR, HINIC3_UI_ERROR_WRONG_PARAMETER);
        return -1;
    }


    return 0;
}

static int
hinic3_agent_get_block_id_from_flow(const struct hinic3_dpif_flow_for_get *f, uint16_t *return_block_id)
{
    struct hinic3_nlattr flow_actions;
    hinic3_nlattr_itr nla = NULL;
    const struct hinic3_flow_act_block_version *block = NULL;

    hinic3_nlattr_init(&flow_actions, f->actions, f->action_len);
    hinic3_nlattr_reset_itr(&flow_actions, f->action_len);
    HINIC3_NLATTR_FOR_EACH(nla, &flow_actions)
    {
        enum hinic3_flow_action_type type = (enum hinic3_flow_action_type)hinic3_nlattr_get_itr_type(nla);
        if (type == HINIC3_FLOW_ACT_BLOCK_VERSION)
        {
            block = (const struct hinic3_flow_act_block_version *)hinic3_nlattr_get_itr_data(nla);
            if (return_block_id != NULL)
            {
                *return_block_id = block->block_id;
            }
            return 0;
        }
    }
    return -1;
}

static int
hinic3_agent_dump_flows_to_screen_by_block_id(struct hinic3_dpif_flow_for_get *f,
                                              const uint32_t block_id_input, struct ds *ds)
{
    struct hinic3_dump_all_flows_mgmt_t *mgmt = &dump_flows_mgmt;
    uint64_t total_loss_pkts = 0;
    uint16_t block_id = 0;
    int dump_flows = 0;
    void *state = NULL;

    if (hinic3_agent_set_dump_start(mgmt, ds) != 0)
        return -1;

    int ret = hinic3_cmd_flow_dump_start(&state);
    if (ret != 0)
    {
        hinic3_agent_dump_flows_handle_ret(ret, ds);
        hinic3_set_dumping_status(mgmt, HINIC3_DUMP_FLOWS_IDLE);
        return -1;
    }

    ret = hinic3_alloc_mem_for_flow(f);
    if (ret != 0)
    {
        hinic3_ds_put_format(ds, "%s%s\n", HINIC3_UI_LEADING_SIGN_FAILURE, HINIC3_UI_FLOW_ALLOC_FLOW_INFO_ERROR_STRING);
        (void)hinic3_cmd_flow_dump_done(state);
        hinic3_set_dumping_status(mgmt, HINIC3_DUMP_FLOWS_IDLE);
        return -1;
    }

    while ((hinic3_cmd_flow_dump_next(state, f)) == 0)
    {
        ret = hinic3_agent_get_block_id_from_flow(f, &block_id);
        if (ret != 0)
        {
            hinic3_ds_put_format(ds, "%s%s\n", HINIC3_UI_LEADING_SIGN_FAILURE,
                                 HINIC3_UI_FLOW_GET_BLOCK_ID_FROM_FLOW_FAILED_STRING);
            break;
        }
        if (block_id_input == block_id)
        {
            if (dump_flows <= HINIC3_DUMP_FLOWS_MAX_ONCE)
            {
                hinic3_agent_flow_format_output(HINIC3_HYDRA_TYPE_TABLE_START, f, ds);
                hinic3_ds_put_format(ds, "\n");
            }
            total_loss_pkts += f->stats.ct_loss_pkts;
            dump_flows++;
        }
    }
    (void)hinic3_cmd_flow_dump_done(state);
    hinic3_ds_put_format_prefix(ds, INDENT_0, HINIC3_UI_LEADING_SIGN_INFO, " %s%" PRIu32 ", %s%" PRIu64 ".\n",
                                HINIC3_UI_FLOW_DUMP_TOTAL_FLOW_STRING, dump_flows, HINIC3_UI_FLOW_DUMP_TOTAL_CT_LOST_PKT_STRING, total_loss_pkts);
    if (dump_flows > HINIC3_DUMP_FLOWS_MAX_ONCE)
    {
        hinic3_ds_clear(ds);
        hinic3_ds_put_format_prefix(ds, INDENT_2, HINIC3_UI_EMPTY_STRING,
                                    HINIC3_UI_FLOW_WITH_BLOCKID_UP_MAX_TIPS_STRING, block_id_input, dump_flows, HINIC3_DUMP_FLOWS_MAX_ONCE);
    }
    hinic3_agent_free_mem_for_flow(f);
    hinic3_set_dumping_status(mgmt, HINIC3_DUMP_FLOWS_IDLE);
    return 0;
}

static int
hinic3_agent_dump_flows_by_block_id(int argc, const char *argv[], struct ds *ds)
{
    enum
    {
        ARGC = 3
    };
    if (argc != ARGC)
    {
        hinic3_ds_put_format(ds, "%s%s, please input -h or --help to get help info.\n",
                             HINIC3_UI_LEADING_SIGN_ERROR, HINIC3_UI_ERROR_TOO_MANY_PARAMETER);
        return -1;
    }

    uint32_t block_num = 0;
    uint32_t block_id_input = 0;
    char *endPtr = NULL;
    int ret = hinic3_flow_get_block_table_size(&block_num);
    if (ret != 0)
    {
        hinic3_ds_put_format(ds, "%s%s\n", HINIC3_UI_LEADING_SIGN_FAILURE,
                             HINIC3_UI_FLOW_DUMP_GET_BLOCK_SIZE_FAILED_STRING);
        return -1;
    }

    block_id_input = strtoul(argv[ARGC - 1], &endPtr, DEC_BASE_NUM);
    if (block_id_input >= block_num || endPtr == NULL || *endPtr != '\0')
    {
        hinic3_ds_put_format(ds, "%s%s%s\n", HINIC3_UI_LEADING_SIGN_ERROR, HINIC3_UI_ERROR_WRONG_PARAMETER, HINIC3_UI_FLOW_DUMP_BLOCK_ID_ERROR_STRING);
        return -1;
    }

    struct hinic3_dpif_flow_for_get f = {0};
    return hinic3_agent_dump_flows_to_screen_by_block_id(&f, block_id_input, ds);
}

static int hinic3_agent_dump_flows_to_screen_by_table_id(struct hinic3_dpif_flow_for_get *f,
                                                         const uint32_t table_id_input, struct ds *ds)
{
    struct hinic3_dump_all_flows_mgmt_t *mgmt = &dump_flows_mgmt;
    uint64_t total_loss_pkts = 0;
    int dump_flows_count = 0;
    void *state = NULL;

    if (hinic3_agent_set_dump_start(mgmt, ds) != 0)
    {
        return -1;
    }

    int ret = hinic3_flexda_cmd_flow_dump_start_by_table_id(table_id_input, &state);
    if (ret != 0)
    {
        hinic3_agent_dump_flows_handle_ret(ret, ds);
        hinic3_set_dumping_status(mgmt, HINIC3_DUMP_FLOWS_IDLE);
        return -1;
    }

    ret = hinic3_alloc_mem_for_flow(f);
    if (ret != 0)
    {
        hinic3_ds_put_format(ds, "%s%s\n", HINIC3_UI_LEADING_SIGN_FAILURE, HINIC3_UI_FLOW_ALLOC_FLOW_INFO_ERROR_STRING);
        (void)hinic3_cmd_flow_dump_done(state);
        hinic3_set_dumping_status(mgmt, HINIC3_DUMP_FLOWS_IDLE);
        return -1;
    }

    while ((hinic3_cmd_flow_dump_next(state, f)) == 0 && dump_flows_count <= HINIC3_DUMP_FLOWS_MAX_ONCE)
    {
        hinic3_agent_flow_format_output(table_id_input, f, ds);
        hinic3_ds_put_format(ds, "\n");
        total_loss_pkts += f->stats.ct_loss_pkts;
        dump_flows_count++;
    }

    (void)hinic3_cmd_flow_dump_done(state);
    hinic3_ds_put_format(ds, "%2s%s%" PRIu32 ", %s%" PRIu64 "\n", HINIC3_UI_INDENT_SPACE,
                         HINIC3_UI_FLOW_DUMP_TOTAL_FLOW_STRING, dump_flows_count,
                         HINIC3_UI_FLOW_DUMP_TOTAL_CT_LOST_PKT_STRING, total_loss_pkts);
    if (dump_flows_count > HINIC3_DUMP_FLOWS_MAX_ONCE)
    {
        hinic3_ds_put_format(ds, HINIC3_UI_FLOW_UP_DISPLAY_MAX_TIPS_STRING,
                             HINIC3_UI_LEADING_SIGN_INFO, HINIC3_DUMP_FLOWS_MAX_ONCE);
    }

    hinic3_agent_free_mem_for_flow(f);
    hinic3_set_dumping_status(mgmt, HINIC3_DUMP_FLOWS_IDLE);
    return 0;
}

static int hinic3_agent_dump_flows_by_table_id(int argc, const char *argv[], struct ds *ds)
{
    enum
    {
        ARGC = 3
    };
    if (argc != ARGC)
    {
        hinic3_ds_put_format(ds, "%s%s, please input -h or --help to get help info.\n",
                             HINIC3_UI_LEADING_SIGN_ERROR, HINIC3_UI_ERROR_WRONG_PARAMETER);
        return -1;
    }

    char *endPtr = NULL;
    uint32_t table_id = strtoul(argv[ARGC - 1], &endPtr, DEC_BASE_NUM);
    if (endPtr == NULL || *endPtr != '\0' || hinic3_flexda_flow_check_table_id_valid(table_id) != 0)
    {
        hinic3_ds_put_format(ds, "%s%s\n", HINIC3_UI_LEADING_SIGN_ERROR, HINIC3_UI_FLOW_DUMP_TABLE_ID_ERROR_STRING);
        return -1;
    }

    struct hinic3_dpif_flow_for_get f = {0};
    return hinic3_agent_dump_flows_to_screen_by_table_id(&f, table_id, ds);
}

static int
hinic3_agent_dump_rte_flow_to_screen(struct ds *ds, uint32_t table_id)
{
    size_t total = 0;
    const struct hinic3_ufid_map_table *table = hinic3_get_rte_flow_map_table();
    total = hinic3_flexda_dump_hmap_get_flow_num(table_id);
    if (total > HINIC3_DUMP_FLOWS_MAX_ONCE) {
        hinic3_ds_put_format(ds, HINIC3_UI_FLOW_UP_MAX_TIPS_STRING, HINIC3_UI_INDENT_SPACE, total, HINIC3_DUMP_FLOWS_MAX_ONCE);
        return 0;
    }
    for (int i = 0; i < OFFLOAD_FLOW_BUCKETS; ++i)
    {
        const struct hmap *map = &table->hash_table_array[i].key_hmap;
        if (hinic3_hmap_is_empty(map))
        {
            continue;
        }
        struct hinic3_ufid_hmap_node *node, *next;
        HINIC3_HMAP_FOR_EACH_SAFE(node, next, node, map)
        {
            struct rte_flow* flow = node->key;
            if (table_id != flow->table_id)
                continue;
            hinic3_format_rte_flow(flow, ds);
        }
    }
    hinic3_ds_put_format(ds, "%2s%s%zu\n", HINIC3_UI_INDENT_SPACE,
                         HINIC3_UI_FLOW_DUMP_TOTAL_FLOW_STRING, total);
    return 0;
}

static int
hinic3_agent_process_dump_option(int index, int argc, const char *argv[], struct ds *ds)
{
    enum
    {
        HINIC3_ARG_FILE_NAME_IDX = 2,
        HINIC3_ARG_FILE_ARGC = 3
    };
    int option_val = hinic3_find_option(argv[index]);
    if (option_val == -1)
    {
        hinic3_ds_put_format(ds, "%s%s, please input -h or --help to get help info.\n", HINIC3_UI_LEADING_SIGN_ERROR,
                             HINIC3_UI_ERROR_UNRECOGNIZED_COMMAND);
        return -1;
    }

    switch (option_val)
    {
    case HINIC3_UFID_64_OPT:
        if (hinic3_card_mod_get() == PROG_MODE) {
            return hinic3_flexda_agent_dump_specific_flow(argc, argv, ds);
        } else {
            return hinic3_agent_dump_specific_flow(argc, argv, ds);
        }
    case HINIC3_STOP_DUMP_ALL_FLOWS_OPT:
        return hinic3_agent_force_stop_dump_all_flows_to_file(argc, argv, ds);

    case HINIC3_CHECK_DUMP_ALL_FLOWS_OPT:
        return hinic3_agent_check_status_dump_flows_to_file(argc, ds);

    case HINIC3_FLOW_FILE_OPT:
        if (argc != HINIC3_ARG_FILE_ARGC)
        {
            hinic3_ds_put_format(ds, "%s%s, please input -h or --help to get help info.\n",
                                 HINIC3_UI_LEADING_SIGN_ERROR, HINIC3_UI_ERROR_WRONG_PARAMETER);
            return -1;
        }
        return hinic3_agent_dump_flows_to_file(argv[HINIC3_ARG_FILE_NAME_IDX], ds);

    case HINIC3_FLOW_CNT_OPT:
        if (hinic3_card_mod_get() == PROG_MODE) {
            return hinic3_flexda_agent_get_flows_cnt(argc, argv, ds);
        } else {
            return hinic3_agent_get_flows_cnt(argc, ds);
        }
    case HELP_OPT:
        hinic3_agent_dump_flows_help(ds);
        break;

    case HINIC3_FLOW_BLOCK_ID_OPT:
        return hinic3_agent_dump_flows_by_block_id(argc, argv, ds);
    case HINIC3_FLOW_TABLE_ID_OPT:
        return hinic3_agent_dump_flows_by_table_id(argc, argv, ds);

    default:
        hinic3_ds_put_format(ds, "%s%s, please input -h or --help to get help info.\n", HINIC3_UI_LEADING_SIGN_ERROR,
                             HINIC3_UI_ERROR_WRONG_PARAMETER);
        return -1;
    }
    return 0;
}

static int
hinic3_agent_process_dump_rte_flow_options(int argc, const char *argv[], struct ds *ds)
{
    enum
    {
        HINIC3_DUMP_RTE_ARG_FIRST_OPTION = 1,
        HINIC3_DUMP_RTE_ARG_MIN_COUNT = 2,
        HINIC3_DUMP_RTE_ARG_SECOND_OPTION = 2,
        HINIC3_DUMP_RTE_ARG_FILE_OR_TABLE = 3,
        HINIC3_DUMP_RTE_ARG_MAX_COUNT = 4
    };
    const char *filename = NULL;
    struct hinic3_dump_all_flows_mgmt_t *mgmt = NULL;
    uint32_t table_id = hinic3_card_mod_get() == PROG_MODE ? 1 : 0;
    if (argc == HINIC3_DUMP_RTE_ARG_MIN_COUNT && strcmp("-r", argv[HINIC3_DUMP_RTE_ARG_FIRST_OPTION]) == 0)
    {
        return hinic3_agent_dump_rte_flow_to_screen(ds, table_id);
    }
    if (argc != HINIC3_DUMP_RTE_ARG_MAX_COUNT)
    {
        goto unrecognized;
    }
    const char* option = argv[HINIC3_DUMP_RTE_ARG_SECOND_OPTION];
    if (strcmp("-f", option) == 0)
    {
        filename = argv[HINIC3_DUMP_RTE_ARG_FILE_OR_TABLE];
        mgmt = hinic3_cmd_get_flow_dump_mgmt();
        mgmt->is_dpak_flow = true;
        return hinic3_agent_dump_flows_to_file(filename, ds);
    } else if (strcmp("-t", option) == 0){
        char *endPtr = NULL;
        unsigned long dump_table_id = strtoul(argv[HINIC3_DUMP_RTE_ARG_FILE_OR_TABLE], &endPtr, DEC_BASE_NUM);
        if (endPtr == NULL || *endPtr != '\0' || dump_table_id > UINT32_MAX ||
            hinic3_flexda_flow_check_table_id_valid((uint32_t)dump_table_id) != 0)
        {
            hinic3_ds_put_format(ds, "%s%s\n", HINIC3_UI_LEADING_SIGN_ERROR, HINIC3_UI_FLOW_DUMP_TABLE_ID_ERROR_STRING);
            return -1;
        }
        return hinic3_agent_dump_rte_flow_to_screen(ds, (uint32_t)dump_table_id);
    }
unrecognized:
    hinic3_ds_put_format(ds, "%s%s, please input -h or --help to get help info.\n", HINIC3_UI_LEADING_SIGN_ERROR,
                         HINIC3_UI_ERROR_UNRECOGNIZED_COMMAND);
    return -1;
}

void hinic3_agent_dump_hinic3_flows(struct unixctl_conn *conn, int argc, const char *argv[], void *aux,
                                    enum hinic3_flow_type type)
{
    struct ds ds = DS_EMPTY_INITIALIZER;
    int ret;
    enum
    {
        ARGC = 1
    };
    if (type != hinic3_cmd_get_flow_ops_status())
    {
        hinic3_ds_put_format(&ds, "%s%s\n", HINIC3_UI_LEADING_SIGN_FAILURE, HINIC3_UI_FLOW_DUMP_ALL_FLOW_TIPS_STRING);
        ret = -1;
        goto out;
    }

    if (argc == ARGC)
    {
        ret = hinic3_agent_dump_flows(&ds);
        goto out;
    }

    if (hinic3_get_dump_rte_flow_options(argc, argv))
    {
        ret = hinic3_agent_process_dump_rte_flow_options(argc, argv, &ds);
    }
    else
    {
        ret = hinic3_agent_process_dump_option(ARGC, argc, argv, &ds);
    }

out:
    if (ret != 0)
    {
        hinic3_command_reply_error(conn, hinic3_ds_cstr(&ds));
        *(int *)aux = -1;
    }
    else
    {
        hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
        *(int *)aux = 0;
    }
    hinic3_ds_destroy(&ds);
}

void hinic3_agent_dump_hinic3_flow_init(void)
{
    hinic3_cmd_set_flow_key_format_output_ops(hinic3_flow_key_format_output);
}

void hinic3_agent_dump_hinic3_emc_flows(struct unixctl_conn *conn, int argc, const char *argv[],
                                        void *aux)
{
    hinic3_agent_dump_hinic3_emc_flow_init();
    hinic3_agent_dump_hinic3_flows(conn, argc, argv, aux, HINIC3_FLOW_TYPE_EMC);
}

void unixctl_hinic3_flow_dump_cmd_init(void)
{
    enum hinic3_flow_cmd_max
    {
        DUMP_HINIC3_FLOWS_PARAM = 3,
        FLEXDA_DUMP_HINIC3_FLOWS_PARAM = 4,
    };

    if (hinic3_card_mod_get() == PROG_MODE)
    {
        hinic3_command_register("hwoff/dump-hwoff-flows",
                               "[ ufid <hw-ufid> -t <table-id> | -n [ -t <table-id> ] | -f <file-name> | -i <block-id> | -r [ { -f <file-name> | -t <tabl-id> } ] "
                               " | check | stop | { -h | --help } ]",
                               0,
                               FLEXDA_DUMP_HINIC3_FLOWS_PARAM, hinic3_agent_dump_hinic3_emc_flows, NULL);
    }
    else
    {
        hinic3_command_register("hwoff/dump-hwoff-flows",
                                "[ ufid <hw-ufid> | -n | -f <file-name> | -i <block-id> | -r [ -f <file-name> ]"
                                " | check | stop | { -h | --help } ]", 0,
                               DUMP_HINIC3_FLOWS_PARAM, hinic3_agent_dump_hinic3_emc_flows, NULL);
    }
    hinic3_pthread_mutex_init(&dump_flows_mgmt.mutex_lock);
}
