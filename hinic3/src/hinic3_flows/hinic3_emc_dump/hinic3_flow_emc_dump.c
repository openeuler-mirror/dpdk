/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2024 Huawei Technologies Co., Ltd
 */
#include "rte_flow.h"
#include "hinic3_flow_dump_item.h"
#include "hinic3_flow_dump_action.h"
#include "hinic3_iface_flow.h"
#include "hinic3_message.h"
#include "hinic3_driver_public.h"
#include "hinic3_flow_dump_public.h"
#include "hinic3_flow_agent.h"
#include "hinic3_flow_emc_dump.h"
static int hinic3_flow_dump_format(struct hinic3_dump_flow_mem* memory, struct hinic3_dpif_flow_for_get* dump_for_get,
                                  struct rte_flow_error *error)
{
    int ret = hinic3_hinic3_flow_info_item_build(&memory->flows[memory->cur_flow_num],
                                               dump_for_get ->key, dump_for_get->key_len);
    if (ret != 0) {
        HINIC3_LOG(ERR, FLOW, "hinic3_flow_dump_format build flow item failed");
        return rte_flow_error_set(error, EINVAL, RTE_FLOW_ERROR_TYPE_ITEM_SPEC,
                                  NULL, "Dump flow item spec failed");
    }

    ret = hinic3_hinic3_flow_info_action_build(&memory->flows[memory->cur_flow_num],
                                             dump_for_get->actions, dump_for_get ->action_len);
    if (ret != 0) {
        hinic3_free_one_flow_items(memory->flows[memory->cur_flow_num].items,
                                  memory->flows[memory->cur_flow_num].useful_item_index);
        HINIC3_LOG(ERR, FLOW, "hinic3_flow_dump_format build flow action failed");
        return rte_flow_error_set(error, EINVAL, RTE_FLOW_ERROR_TYPE_ACTION_CONF,
                                  NULL, "Dump flow item action conf failed");
    }

    return ret;
}

static int hinic3_flow_dump_to_context(struct hinic3_flow_dump_context* dump_context, struct rte_flow_error *error)
{
    if (dump_context == NULL) {
        return -1;
    }
    struct hinic3_dpif_flow_for_get dump_for_get;

    struct hinic3_nlattr_obj keys[HINIC3_DFX_FLOW_ALLOC_MEM_BASE_SIZE];
    struct hinic3_nlattr_obj actions[HINIC3_DFX_FLOW_ALLOC_MEM_BASE_SIZE];
    struct hinic3_nlattr_obj masks[HINIC3_DFX_FLOW_ALLOC_MEM_BASE_SIZE];

    dump_for_get.key = keys;
    dump_for_get.actions = actions;
    dump_for_get.mask = masks;

    int ret = hinic3_flow_dump_next(dump_context->hiovs_state, &dump_for_get);
    if (ret != 0) {
        if (ret != HIOVS_EEMPTY) {
            HINIC3_LOG(ERR, FLOW, "hiovs_state dump next error error code is %d", ret);
            return rte_flow_error_set(error, EINVAL, RTE_FLOW_ERROR_TYPE_STATE,
                                      NULL, "Dump flow hovs failed");
        }
        return ret;
    }

    ret = hinic3_flow_dump_format(&(dump_context->context_mem), &dump_for_get, error);
    return ret;
}

static int hinic3_emc_flow_dump_next_sub(struct hinic3_flow_dump_context* dump_context, int count,
    struct rte_flow_error *error)
{
    int ret = 0;
    for (int i = 0; i < count; ++i) {
        ret = hinic3_flow_dump_to_context(dump_context, error);
        if (ret != 0) {
            ret = ret == HIOVS_EEMPTY ? 0 : ret;
            break;
        }
        dump_context->context_mem.cur_flow_num++;
    }

    return ret;
}

static int hinic3_emc_flow_dump_start_sub(struct hinic3_flow_dump_context *context)
{
    int ret = 0;
    ret = hinic3_flow_dump_start(&(context->hiovs_state));
    if (ret != HIOVS_OK && ret != HIOVS_EEMPTY) {
        hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_DUMP_START_HARD, 1);
        return ret;
    }
    return 0;
}

static int hinic3_emc_flow_dump_done_sub(struct hinic3_flow_dump_context *context)
{
    int ret = 0;
    if (context->hiovs_state != NULL) {
        ret = hinic3_flow_dump_done(context->hiovs_state);
    }

    if (ret != HIOVS_OK) {
        hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_DUMP_DONE_HARD, 1);
        return ret;
    }
    return ret;
}

static int hinic3_emc_flow_dump_start(struct hinic3_flow_dump_context *context, struct rte_flow_error *error)
{
    if (context == NULL || error == NULL) {
        return -1;
    }

    int ret = 0;
    ret = hinic3_emc_flow_dump_start_sub(context);
    if (ret != 0) {
        hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_DUMP_START, 1);
    }
    return ret;
}

static int hinic3_emc_flow_dump_next(struct hinic3_flow_dump_context *context, int count, struct rte_flow_error *error)
{
    if (context == NULL || error == NULL) {
        return -1;
    }

    int ret = 0;
    ret = hinic3_emc_flow_dump_next_sub(context, count, error);
    if (ret != 0) {
        hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_DUMP_NEXT, 1);
    }
    return ret;
}

static int hinic3_emc_flow_dump_done(struct hinic3_flow_dump_context *context, struct rte_flow_error *error)
{
    if (context == NULL || error == NULL) {
        return -1;
    }

    int ret = 0;
    ret = hinic3_emc_flow_dump_done_sub(context);
    if (ret != 0) {
        hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_DUMP_DONE, 1);
    }
    return ret;
}

void hinic3_emc_flow_dump_init(void)
{
    hinic3_set_dump_start(HINIC3_FLOW_TYPE_EMC, hinic3_emc_flow_dump_start);
    hinic3_set_dump_next(HINIC3_FLOW_TYPE_EMC, hinic3_emc_flow_dump_next);
    hinic3_set_dump_done(HINIC3_FLOW_TYPE_EMC, hinic3_emc_flow_dump_done);
};
