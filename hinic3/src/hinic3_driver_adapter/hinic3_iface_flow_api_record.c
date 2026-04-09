 /* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */
#include <string.h>
#include <stdbool.h>
#include "rte_cycles.h"
#include "hinic3_init.h"
#include "hinic3_message.h"
#include "hinic3_log.h"
#include "hinic3_flow_agent_public.h"
#include "hinic3_iface_port.h"
#include "hinic3_driver_public.h"
#include "hinic3_iface_flow.h"
#include <rte_atomic.h>
#include "hinic3_iface_flow_api_record.h"

hiovs_api_record g_flow_api_record[HINIC3_FLOW_API_MAX];

void hinic3_flow_api_record_error(int ret_code, hinic3_flow_api api_index, uint64_t exec_time)
{
    if (api_index >= HINIC3_FLOW_API_MAX) {
        HINIC3_LOG(WARNING, DRIVER, "Flow api record error, api_index is %d more than max index %d!", api_index,
            HINIC3_FLOW_API_MAX);
        return;
    }

    if (ret_code != 0) {
        rte_atomic64_inc(&g_flow_api_record[api_index].call_error_count);
        g_flow_api_record[api_index].last_rtn_value = ret_code;
        g_flow_api_record[api_index].time_error = time(NULL);
        HINIC3_LOG(ERR, DRIVER, "Flow api record error, return %d, cost %" PRIu64 " us!", ret_code, exec_time);
    }
}

void hinic3_add_flow_api_record(hinic3_flow_api api_index, uint64_t exec_time)
{
    if (api_index >= HINIC3_FLOW_API_MAX) {
        HINIC3_LOG(WARNING, DRIVER, "Add flow api called record error, api_index is %d more than max index %d!", api_index,
            HINIC3_FLOW_API_MAX);
        return;
    }

    rte_atomic64_inc(&g_flow_api_record[api_index].call_total_count);
    /* the total_api_time allows integer inversion and will not affect the function */
    g_flow_api_record[api_index].total_api_time += exec_time;
    if (exec_time > g_flow_api_record[api_index].max_api_time) {
        g_flow_api_record[api_index].max_api_time = exec_time;
    }
    if (exec_time < g_flow_api_record[api_index].min_api_time || g_flow_api_record[api_index].min_api_time == 0) {
        g_flow_api_record[api_index].min_api_time = exec_time;
    }
}

void hinic3_flow_api_record_error_no_log(int ret_code, hinic3_flow_api api_index)
{
    if (api_index >= HINIC3_FLOW_API_MAX) {
        return;
    }
    if (ret_code != 0) {
        rte_atomic64_inc(&g_flow_api_record[api_index].call_error_count);
        g_flow_api_record[api_index].last_rtn_value = ret_code;
        g_flow_api_record[api_index].time_error = time(NULL);
    }
}

static int hinic3_flow_process_all_api_record(bool is_clear, hiovs_api_record *records, int record_len)
{
    if (is_clear) {
        memset(g_flow_api_record, 0, sizeof(hiovs_api_record) * HINIC3_FLOW_API_MAX);
        return 0;
    }

    if (records == NULL) {
        HINIC3_LOG(ERR, DRIVER, "hinic3_flow_process_all_api_record point parameter err!");
        return -1;
    }

    for (int i = 0; i < record_len && i < HINIC3_FLOW_API_MAX; ++i) {
        records[i] = g_flow_api_record[i];
    }
    return 0;
}

static int hinic3_flow_process_single_api_record(const char *api_name, bool is_clear, hiovs_api_record *records)
{
    hinic3_flow_api api_index = hinic3_flow_get_api_index(api_name);
    if (api_index >= HINIC3_FLOW_API_MAX) {
        return -1;
    }
    if (is_clear) {
        memset(&g_flow_api_record[api_index], 0, sizeof(hiovs_api_record));
        return 0;
    }

    if (records == NULL) {
        HINIC3_LOG(ERR, DRIVER, "hinic3_flow_process_single_api_record point parameter err!");
        return -1;
    }

    records[HINIC3_DEFAULT_API_RECORD_INDEX] = g_flow_api_record[api_index];
    return 0;
}


int hinic3_flow_get_api_record(const char *api_name, bool is_all, bool is_clear, hiovs_api_record *records,
    int record_len)
{
    if (api_name == NULL) {
        HINIC3_LOG(ERR, DRIVER, "hinic3_flow_get_api_record point parameter err!");
        return -1;
    }

    if (is_all) {
        return hinic3_flow_process_all_api_record(is_clear, records, record_len);
    }

    return hinic3_flow_process_single_api_record(api_name, is_clear, records);
}

hiovs_api_record *hinic3_get_flow_api_record(void)
{
    return g_flow_api_record;
}
