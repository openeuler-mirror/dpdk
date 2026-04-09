 /* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <rte_atomic.h>
#include <dlfcn.h>
#include "rte_cycles.h"
#include "hinic3_smap.h"
#include "hinic3_map.h"
#include "hinic3_log.h"
#include "hinic3_util.h"
#include "hinic3_provider.h"
#include "hinic3_tlv_key.h"
#include "hinic3_iface_port_util.h"
#include "hinic3_iface_port_api_record.h"

hiovs_api_record g_port_api_record[HINIC3_PORT_API_MAX];
hiovs_api_record *hinic3_get_port_api_record(void)
{
    return g_port_api_record;
}

void hinic_port_api_record_error(int ret_code, hinic3_port_api api_index, uint64_t exec_time)
{
    hiovs_api_record *p_record = NULL;

    p_record = &g_port_api_record[api_index];
    rte_atomic64_inc(&p_record->call_error_count);
    p_record->last_rtn_value = ret_code;
    p_record->time_error = time(NULL);
    HINIC3_LOG(ERR, DRIVER, "Hinic HWAPI api %d called, return %d, cost %" PRIu64 " us!",
        api_index, ret_code, exec_time);
}

void hinic3_port_fill_api_record(hinic3_port_api api_index, uint64_t exec_time)
{
    hiovs_api_record *p_record = NULL;

    p_record = &g_port_api_record[api_index];
    rte_atomic64_inc(&p_record->call_total_count);
    /* the total_api_time allows integer inversion and will not affect the function */
    p_record->total_api_time += exec_time;
    if (exec_time > p_record->max_api_time) {
        p_record->max_api_time = exec_time;
    }
    if (exec_time < p_record->min_api_time || p_record->min_api_time == 0) {
        p_record->min_api_time = exec_time;
    }
}

static int hinic3_clear_api_record(const char *api_name, bool is_all)
{
    hinic3_port_api api_index;

    if (is_all) {
        memset(g_port_api_record, 0, sizeof(hiovs_api_record) * HINIC3_PORT_API_MAX);
        return 0;
    }

    api_index = hinic3_port_get_api_index(api_name);
    if (api_index >= HINIC3_PORT_API_MAX) {
        return -1;
    }

    memset(&g_port_api_record[api_index], 0, sizeof(hiovs_api_record));
    return 0;
}

int hinic3_port_get_api_record(const char *api_name, bool is_all, bool is_clear,
                              hiovs_api_record *records, int record_len)
{
    int i;
    hinic3_port_api api_index;
    int min_len = HINIC3_MIN(record_len, HINIC3_PORT_API_MAX);

    if (is_clear) {
        return hinic3_clear_api_record(api_name, is_all);
    }

    /* not clear */
    if (is_all) {
        for (i = 0; i < min_len; ++i) {
            records[i] = g_port_api_record[i];
        }
        return 0;
    }

    api_index = hinic3_port_get_api_index(api_name);
    if (api_index >= HINIC3_PORT_API_MAX) {
        return -1;
    }

    records[0] = g_port_api_record[api_index];
    return 0;
}
