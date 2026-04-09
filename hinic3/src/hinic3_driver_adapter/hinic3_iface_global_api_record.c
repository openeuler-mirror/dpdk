 /* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */
#include <string.h>
#include <pthread.h>
#include <rte_atomic.h>
#include "rte_cycles.h"
#include "rte_ether.h"
#include "rte_lcore.h"
#include "rte_malloc.h"
#include "hinic3_log.h"
#include "hinic3_smap.h"
#include "hinic3_init.h"
#include "hinic3_provider.h"
#include "hinic3_message.h"
#include "hinic3_tlv_key.h"
#include "hinic3_iface_flow.h"
#include "hinic3_eth_util.h"
#include "hinic3_flow_agent.h"
#include "hinic3_util.h"
#include "hinic3_iface_global.h"
#include "hinic3_ds.h"
#include "hinic3_iface_global_api_record.h"

hiovs_api_record g_global_api_record[HINIC3_GLOBAL_API_MAX];


int hinic3_global_get_api_record(const char *api_name, bool is_all, bool is_clear,
                                hiovs_api_record *records, int record_len)
{
    int i;
    hinic3_global_api api_index;

    if (is_all) {
        if (is_clear) {
            memset(g_global_api_record, 0, sizeof(hiovs_api_record) * HINIC3_GLOBAL_API_MAX);
            return 0;
        }
        for (i = 0; i < record_len && i < HINIC3_GLOBAL_API_MAX; ++i) {
            records[i] = g_global_api_record[i];
        }
        return 0;
    }

    api_index = hinic3_global_get_api_index(api_name);
    if (api_index >= HINIC3_GLOBAL_API_MAX) {
        return -1;
    }

    if (is_clear) {
        memset(&g_global_api_record[api_index], 0, sizeof(hiovs_api_record));
    }
    records[0] = g_global_api_record[api_index];
    return 0;
}

void hinic_global_api_record_error(int ret_code, hinic3_port_api api_index, uint64_t exec_time)
{
    if (ret_code != 0) {
        rte_atomic64_inc(&g_global_api_record[api_index].call_error_count);
        g_global_api_record[api_index].last_rtn_value = ret_code;
        g_global_api_record[api_index].time_error = time(NULL);
        HINIC3_LOG(ERR, DRIVER, "hinic HWAPI called, return %d, cost %"PRIu64"us!", ret_code, exec_time);
    }
}

void hinic_global_fill_api_record(hinic3_global_api api_index, uint64_t exec_time)
{
    rte_atomic64_inc(&g_global_api_record[api_index].call_total_count);
    /* the total_api_time allows integer inversion and will not affect the function */
    g_global_api_record[api_index].total_api_time += exec_time;
    if (exec_time > g_global_api_record[api_index].max_api_time) {
        g_global_api_record[api_index].max_api_time = exec_time;
    }
    if (exec_time < g_global_api_record[api_index].min_api_time || g_global_api_record[api_index].min_api_time == 0) {
        g_global_api_record[api_index].min_api_time = exec_time;
    }
}

hiovs_api_record *hinic3_get_global_api_record(void)
{
    return g_global_api_record;
}
