/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Huawei Technologies Co., Ltd
 */


#include "hinic3_mutex.h"
#include <error.h>
#include "hinic3_eth_packets.h"
#include "hinic3_flow_dump_public.h"
#include "hinic3_flow_dump_item.h"
#include "hinic3_flow_dump_action.h"
#include "hinic3_flow_emc_dump.h"
#include "hinic3_iface_flow.h"
#include "hinic3_packets_types.h"
#include "hinic3_message.h"
#include "hinic3_types.h"
#include "hinic3_init.h"
#include "hinic3_log.h"
#include "hinic3_timeval.h"
#include "hinic3_ui_string.h"
#include "hinic3_nlattr.h"
#include "hinic3_util.h"
#include "hinic3_offload_flow_public.h"
#include "hinic3_flow_agent.h"
#include "hinic3_iface_flow_api_record.h"
#include "hinic3_meminfo.h"
#include "hinic3_ds.h"
#include "hinic3_flow_dump.h"

#define HINIC3_FLOW_DUMP_THREAD_NUM 10
#define HINIC3_DUMP_CONTEXT_EMPTY 1
#define HINIC3_FLOW_TYPE_SHIFT       30
#define HINIC3_ONCE_FLOW_DUMP_MAX_NUM 10000 // 6M

static struct hinic3_flow_dump_context g_hinic3_dump_contexts[HINIC3_FLOW_DUMP_THREAD_NUM];
static struct hinic3_mutex g_dump_contexts_lock;

void hinic3_dump_flow_init(void)
{
    hinic3_emc_flow_dump_init();
    hinic3_pthread_mutex_init(&g_dump_contexts_lock);
}

void hinic3_dump_contexts_lock(void)
{
   hinic3_pthread_mutex_lock(&g_dump_contexts_lock);
}

void hinic3_dump_contexts_unlock(void)
{
   hinic3_pthread_mutex_unlock(&g_dump_contexts_lock);
}

static int hinic3_dump_flow_start_by_hiovs(struct hinic3_flow_dump_context *context, uint32_t type,
    struct rte_flow_error *error)
{
    int ret = 0;
    dump_start start = hinic3_get_dump_start(type);
    if (start == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3 dump start: unsupported flow type %u", type);
        return -1;
    }

    ret = start(context, error);
    if (ret != HIOVS_OK && ret != HIOVS_EEMPTY) {
        HINIC3_LOG(ERR, FLOW, "hinic3 flow dump: hiovs flow error, error code is %d, flow type is %u", ret, type);
    }

    return ret;
}

static int hinic3_flow_dump_context_get(struct hinic3_flow_dump_context** context)
{
    for (int i = 0; i < HINIC3_FLOW_DUMP_THREAD_NUM; ++i) {
        if (g_hinic3_dump_contexts[i].dumping == HINIC3_FLOW_DUMPING_READY) {
                *context = &(g_hinic3_dump_contexts[i]);
                return 0;
            }
    }
    HINIC3_LOG(ERR, FLOW, "hinic3 flow dump: too much dump task, a maximum of three tasks are supported");
    return -1;
}

static void hinic3_flow_cump_context_init(uint32_t type, struct hinic3_flow_dump_context *dump_context)
{
    dump_context->dumping = HINIC3_FLOW_DUMPING;
    dump_context->thread_id = pthread_self();
    dump_context->context_mem.start_timestamp = hinic3_time_msec();
    dump_context->type = type >> HINIC3_FLOW_TYPE_SHIFT;
}

static int hinic3_accurate_flow_dump_start_sub(void **context, uint32_t type, struct rte_flow_error *error)
{
    if (context == NULL || error == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3 flow dump: context or error is null");
        return -EINVAL;
    }
    struct hinic3_flow_dump_context *dump_context = NULL;
    error->message = NULL;

    hinic3_pthread_mutex_lock(&g_dump_contexts_lock);
    int ret = hinic3_flow_dump_context_get(&dump_context);
    if (ret != 0) {
        hinic3_pthread_mutex_unlock(&g_dump_contexts_lock);
        return rte_flow_error_set(error, EBUSY, RTE_FLOW_ERROR_TYPE_UNSPECIFIED, NULL, "Dump context is busy");
    }
    ret = hinic3_dump_flow_start_by_hiovs(dump_context, type >> HINIC3_FLOW_TYPE_SHIFT, error);
    if (ret != 0) {
        hinic3_pthread_mutex_unlock(&g_dump_contexts_lock);
        (void)rte_flow_error_set(error, -ret, RTE_FLOW_ERROR_TYPE_UNSPECIFIED, NULL, "Hiovs dump start failed");
        return ret;
    }

    (void)hinic3_flow_cump_context_init(type, dump_context);
    *context = (void*)dump_context;

    hinic3_pthread_mutex_unlock(&g_dump_contexts_lock);
    return 0;
}

static struct hinic3_dump_flow_info* hinic3_dump_flow_infos_construct(uint32_t count)
{
    struct hinic3_dump_flow_info* flows = hinic3_calloc(count, sizeof(struct hinic3_dump_flow_info), HINIC3_FLOWS);
    if (flows == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3 flow dump: malloc for dump memory failed");
    }
    return flows;
}

int hinic3_eth_flow_dump_start(void **context, uint32_t type, struct rte_flow_error *error)
{
    int ret = 0;
    HINIC3_LOG_HINIC_FLOW_API_LOG_NO_LOG(ret, HINIC3_RTE_FLOW_DUMP_START,
        hinic3_accurate_flow_dump_start_sub(context, type, error));
    return ret;
}

static void hinic3_free_dump_flow_info(struct hinic3_dump_flow_info* flow)
{
    if (flow == NULL) {
        return;
    }
    hinic3_free_one_flow_actions(flow->actions, HINIC3_FLOW_DUMP_MAX_ACTION);
    hinic3_free_one_flow_items(flow->items, HINIC3_FLOW_DUMP_MAX_PATTERN);
}

static void hinic3_free_dump_flow_info_arr(struct hinic3_dump_flow_info* flow, unsigned int flow_num)
{
    for (unsigned int i = 0; i < flow_num; ++i) {
        hinic3_free_dump_flow_info(&flow[i]);
    }
}

static void hinic3_dump_flow_mem_destruct(struct hinic3_dump_flow_mem *dump_flow_mem)
{
    if (dump_flow_mem == NULL) {
        return;
    }

    hinic3_free_dump_flow_info_arr(dump_flow_mem->flows, dump_flow_mem->max_flow_num);
    hinic3_free(dump_flow_mem->flows);
    dump_flow_mem->flows = NULL;

    dump_flow_mem->cur_flow_num = 0;
    dump_flow_mem->max_flow_num = 0;
}

static void hinic3_dump_next_output(const struct hinic3_dump_flow_mem* context_mem, uint32_t dumped_count,
                                   struct rte_flow_item **pattern[], struct rte_flow_action **actions[])
{
    for (uint32_t i = 0; i < dumped_count; ++i) {
        struct hinic3_dump_flow_info* flow = &context_mem->flows[i];
        pattern[i] = (struct rte_flow_item**)(flow->items);
        actions[i] = (struct rte_flow_action**)(flow->actions);
    }

    return;
}

static int hinic3_dump_context_input_check(const struct hinic3_flow_dump_context* context,
                                   enum hinic3_dump_context_status type)
{
    if (context->thread_id != pthread_self()) {
        hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_DUMP_INPUT_CHECK_PTHREAD, 1);
        return -1;
    }

    if (context->dumping != type) {
        hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_DUMP_INPUT_CHECK_TYPE, 1);
        return -1;
    }
    return 0;
}

static int hinic3_accurate_flow_dump_next_sub(void *context, uint32_t count, uint32_t *dumped_count,
    struct rte_flow_item **pattern[], struct rte_flow_action **actions[], struct rte_flow_error *error)
{
    if (context == NULL || dumped_count == NULL || error == NULL || pattern == NULL || actions == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3_dump_next input NULL pointer");
        return -EINVAL;
    }
    if (count > HINIC3_ONCE_FLOW_DUMP_MAX_NUM || count == 0) {
        return rte_flow_error_set(error, EINVAL, RTE_FLOW_ERROR_TYPE_STATE, NULL,
            "count cannot exceed 10000 or equal to 0");
    }

    struct hinic3_flow_dump_context *dump_context = (struct hinic3_flow_dump_context *)context;

    dump_next dump_func = NULL;
    dump_func = hinic3_get_dump_next(dump_context->type);
    if (dump_func == NULL) {
        return rte_flow_error_set(error, EINVAL, RTE_FLOW_ERROR_TYPE_STATE, NULL, "dump next input unsupported type");
    }

    int ret = hinic3_dump_context_input_check(dump_context, HINIC3_FLOW_DUMPING);
    if (ret != 0) {
        return rte_flow_error_set(error, EINVAL, RTE_FLOW_ERROR_TYPE_STATE, NULL, NULL);
    }

    if (dump_context->type != HINIC3_FLOW_TYPE_ACL) {
        if (dump_context->hiovs_state == NULL) {
            *dumped_count = 0;
            return 0;
        }
    }

    struct hinic3_dump_flow_info *flows = hinic3_dump_flow_infos_construct(count);
    if (flows == NULL) {
        hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_DUMP_NEXT_MEM, 1);
        return rte_flow_error_set(error, ENOMEM, RTE_FLOW_ERROR_TYPE_ITEM,
                                  NULL, "malloc for dump flow failed");
    }

    if (dump_context->context_mem.flows != NULL) {
        hinic3_free_dump_flow_info_arr(dump_context->context_mem.flows, dump_context->context_mem.max_flow_num);
        dump_context->context_mem.cur_flow_num = 0;
        hinic3_free(dump_context->context_mem.flows);
    }

    dump_context->context_mem.flows = flows;
    dump_context->context_mem.max_flow_num = count;

    ret = dump_func(dump_context, count, error);
    if (ret != HIOVS_OK && ret != HIOVS_EEMPTY) {
        hinic3_free(dump_context->context_mem.flows);
        return ret;
    }

    *dumped_count = dump_context->context_mem.cur_flow_num;
    hinic3_dump_next_output(&dump_context->context_mem, count, pattern, actions);

    return ret;
}

int hinic3_eth_flow_dump_next(void *context, uint32_t count, uint32_t *dumped_count,
    struct rte_flow_item **pattern[], struct rte_flow_action **actions[], struct rte_flow_error *error)
{
    int ret = 0;
    HINIC3_LOG_HINIC_FLOW_API_LOG_NO_LOG(ret, HINIC3_RTE_FLOW_DUMP_NEXT,
        hinic3_accurate_flow_dump_next_sub(context, count, dumped_count, pattern, actions, error));
    return ret;
}

static void hinic3_flow_dump_context_reset(struct hinic3_flow_dump_context *context)
{
    if (context == NULL) {
        return;
    }

    hinic3_dump_flow_mem_destruct(&context->context_mem);
    context->dumping = HINIC3_FLOW_DUMPING_READY;
    return;
}

static int hinic3_accurate_flow_dump_done_sub(void *context, struct rte_flow_error *error)
{
    if (context == NULL || error == NULL) {
        return -EINVAL;
    }

    struct hinic3_flow_dump_context *dump_context = (struct hinic3_flow_dump_context*)context;
    int ret = hinic3_dump_context_input_check(dump_context, HINIC3_FLOW_DUMPING);
    if (ret != 0) {
        return rte_flow_error_set(error, EINVAL, RTE_FLOW_ERROR_TYPE_UNSPECIFIED, NULL,
            "hinic3 flow dump: hinic3 dump done input error");
    }

    dump_done done_func = NULL;
    done_func = hinic3_get_dump_done(dump_context->type);
    if (done_func == NULL) {
        return rte_flow_error_set(error, EINVAL, RTE_FLOW_ERROR_TYPE_UNSPECIFIED,
            NULL, "hinic3 dump done input unsupported type");
    }

    ret = done_func(dump_context, error);
    if (ret != HIOVS_OK) {
        rte_flow_error_set(error, EPERM, RTE_FLOW_ERROR_TYPE_STATE, NULL,
                                    "flow dump : dump done failed");
    } else {
        dump_context->hiovs_state = NULL;
    }

    hinic3_flow_dump_context_reset(dump_context);
    return ret;
}

int hinic3_eth_flow_dump_done(void *context, struct rte_flow_error *error)
{
    int ret = 0;
    HINIC3_LOG_HINIC_FLOW_API_LOG_NO_LOG(ret, HINIC3_RTE_FLOW_DUMP_END,
        hinic3_accurate_flow_dump_done_sub(context, error));
    return ret;
}

void hinic3_dump_context(struct ds *ds)
{
    if (ds == NULL) {
        return;
    }

    struct hinic3_flow_dump_context *context = g_hinic3_dump_contexts;
    hinic3_pthread_mutex_lock(&g_dump_contexts_lock);
    for (int i = 0; i < HINIC3_FLOW_DUMP_THREAD_NUM; i++, context++) {
        if (i != 0) {
            hinic3_ds_put_format(ds, "\n");
        }
        enum hinic3_dump_context_status status = context->dumping;
        hinic3_ds_put_format(ds, "%2s%-15s%d\n", HINIC3_UI_INDENT_SPACE, HINIC3_UI_DUMP_CONTEXT_INDEX_STRING, i);
        if (status == HINIC3_FLOW_DUMPING_READY) {
            hinic3_ds_put_format(ds, "%2s%-15s%s\n", HINIC3_UI_INDENT_SPACE,
                HINIC3_UI_DUMP_CONTEXT_STATUS_STRING, HINIC3_UI_DUMP_CONTEXT_IDLE_STRING);
            continue;
        }
        long long int start_timestamp = context->context_mem.start_timestamp;
        char start_time_buf[HINIC3_MS_STR_SIZE] = {'\0'};
        time_msec_to_str(start_timestamp, start_time_buf, HINIC3_MS_STR_SIZE);
        hinic3_ds_put_format(ds, "%2s%-15s%s\n", HINIC3_UI_INDENT_SPACE,
            HINIC3_UI_DUMP_CONTEXT_STATUS_STRING, HINIC3_UI_DUMP_CONTEXT_DUMPING_STRING);
        hinic3_ds_put_format(ds, "%2s%-15s%llu\n", HINIC3_UI_INDENT_SPACE,
            HINIC3_UI_DUMP_CONTEXT_THREAD_ID_STRING, context->thread_id);
        hinic3_ds_put_format(ds, "%2s%-15s%u\n", HINIC3_UI_INDENT_SPACE,
            HINIC3_UI_DUMP_CONTEXT_COUNT_STRING, context->context_mem.cur_flow_num);
        hinic3_ds_put_format(ds, "%2s%-15s%s\n", HINIC3_UI_INDENT_SPACE,
            HINIC3_UI_DUMP_CONTEXT_START_TIME_STRING, start_time_buf);
        hinic3_ds_put_format(ds, "%2s%-15s%lld ms\n", HINIC3_UI_INDENT_SPACE,
            HINIC3_UI_DUMP_CONTEXT_RUNNING_TIME_STRING, hinic3_time_msec() - start_timestamp);
    }
    hinic3_pthread_mutex_unlock(&g_dump_contexts_lock);
}
