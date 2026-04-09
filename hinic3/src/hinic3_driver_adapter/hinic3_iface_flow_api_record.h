 /* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_IFACE_FLOW_API_RECORD_H
#define HINIC3_IFACE_FLOW_API_RECORD_H

#include "hinic3_iface_flow.h"

#define USEC_PER_SEC         (1000000UL)

void hinic3_flow_api_record_error_no_log(int ret_code, hinic3_flow_api api_index);
void hinic3_flow_api_record_error(int ret_code, hinic3_flow_api api_index, uint64_t exec_time);
void hinic3_add_flow_api_record(hinic3_flow_api api_index, uint64_t exec_time);
hiovs_api_record *hinic3_get_flow_api_record(void);

#define HINIC3_LOG_HINIC_FLOW_API_LOG(RET_CODE, API_INDEX, API_FUNC)                \
    do {                                                                           \
        uint64_t time_start = rte_get_tsc_cycles();                                \
        RET_CODE = API_FUNC;                                                       \
        uint64_t exec_time = 0;                                                    \
        uint64_t hz = rte_get_tsc_hz();                                            \
        if (hz != 0) {                                                             \
            exec_time = ((rte_get_tsc_cycles() - time_start) * USEC_PER_SEC) / hz; \
        }                                                                          \
        hinic3_add_flow_api_record(API_INDEX, exec_time);                           \
        hinic3_flow_api_record_error(RET_CODE, API_INDEX, exec_time);               \
    } while (0)

#define HINIC3_LOG_HINIC_FLOW_API_LOG_WARN(RET_CODE, API_INDEX, API_FUNC)           \
    do {                                                                           \
        uint64_t time_start = rte_get_tsc_cycles();                                \
        RET_CODE = API_FUNC;                                                       \
        uint64_t exec_time = 0;                                                    \
        uint64_t hz = rte_get_tsc_hz();                                            \
        if (hz != 0) {                                                             \
            exec_time = ((rte_get_tsc_cycles() - time_start) * USEC_PER_SEC) / hz; \
        }                                                                          \
        hinic3_add_flow_api_record(API_INDEX, exec_time);                           \
        hinic3_flow_api_record_error(RET_CODE, API_INDEX, exec_time);               \
    } while (0)

#define HINIC3_LOG_HINIC_FLOW_API_LOG_NO_LOG(RET_CODE, API_INDEX, API_FUNC)         \
    do {                                                                           \
        uint64_t time_start = rte_get_tsc_cycles();                                \
        RET_CODE = API_FUNC;                                                       \
        uint64_t exec_time = 0;                                                    \
        uint64_t hz = rte_get_tsc_hz();                                            \
        if (hz != 0) {                                                             \
            exec_time = ((rte_get_tsc_cycles() - time_start) * USEC_PER_SEC) / hz; \
        }                                                                          \
        hinic3_add_flow_api_record(API_INDEX, exec_time);                           \
        hinic3_flow_api_record_error_no_log(RET_CODE, API_INDEX);                   \
    } while (0)

#define HINIC3_LOG_HINIC_FLOW_ISNULL_API_LOG_NO_LOG(RET_CODE, API_INDEX, API_FUNC)  \
    do {                                                                           \
        uint64_t time_start = rte_get_tsc_cycles();                                \
        RET_CODE = API_FUNC;                                                       \
        uint64_t exec_time = 0;                                                    \
        uint64_t hz = rte_get_tsc_hz();                                            \
        if (hz != 0) {                                                             \
            exec_time = ((rte_get_tsc_cycles() - time_start) * USEC_PER_SEC) / hz; \
        }                                                                          \
        hinic3_add_flow_api_record(API_INDEX, exec_time);                           \
        hinic3_flow_api_record_error_no_log(((RET_CODE) == NULL), API_INDEX);       \
    } while (0)

#define HINIC3_LOG_HINIC_FLOW_API_LOG_NO_LOG_RET(API_INDEX, API_FUNC)               \
    do {                                                                           \
        uint64_t time_start = rte_get_tsc_cycles();                                \
        API_FUNC;                                                                  \
        uint64_t exec_time = 0;                                                    \
        uint64_t hz = rte_get_tsc_hz();                                            \
        if (hz != 0) {                                                             \
            exec_time = ((rte_get_tsc_cycles() - time_start) * USEC_PER_SEC) / hz; \
        }                                                                          \
        hinic3_add_flow_api_record(API_INDEX, exec_time);                           \
        hinic3_flow_api_record_error_no_log(0, API_INDEX);                           \
    } while (0)

#endif
