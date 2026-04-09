 /* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_IFACE_PORT_API_RECORD_H
#define HINIC3_IFACE_PORT_API_RECORD_H

#include "hinic3_iface_port.h"

void hinic3_port_fill_api_record(hinic3_port_api api_index, uint64_t exec_time);
void hinic_port_api_record_error(int ret_code, hinic3_port_api api_index, uint64_t exec_time);
hiovs_api_record *hinic3_get_port_api_record(void);


/* * some API returns long long int, so must rewrite one. */
#define RTF_LOG_HINIC_PORT_API_LOG_LONG_INT(TIME_START, EXEC_TIME, RET_CODE, API_INDEX, API_FUNC) \
    do {                                                                                          \
        TIME_START = rte_get_tsc_cycles();                                                        \
        RET_CODE = API_FUNC;                                                                      \
        EXEC_TIME = 0;                                                                            \
        uint64_t hz = rte_get_tsc_hz();                                                           \
        if (hz != 0) {                                                                            \
            EXEC_TIME = ((rte_get_tsc_cycles() - (TIME_START)) * 1000000UL) / hz;                 \
        }                                                                                         \
        hinic3_port_fill_api_record(API_INDEX, EXEC_TIME);                                         \
        if ((RET_CODE) != 0) {                                                                    \
            hinic_port_api_record_error(RET_CODE, API_INDEX, EXEC_TIME);                          \
        }                                                                                         \
    } while (0)

#define RTF_LOG_HINIC_PORT_API_LOG_LESS_THAN_ZERO(TIME_START, EXEC_TIME, RET_CODE, API_INDEX, API_FUNC) \
    do {                                                                                                \
        TIME_START = rte_get_tsc_cycles();                                                              \
        RET_CODE = API_FUNC;                                                                            \
        EXEC_TIME = 0;                                                                                  \
        uint64_t hz = rte_get_tsc_hz();                                                                 \
        if (hz != 0) {                                                                                  \
            EXEC_TIME = ((rte_get_tsc_cycles() - (TIME_START)) * 1000000UL) / hz;                       \
        }                                                                                               \
        hinic3_port_fill_api_record(API_INDEX, EXEC_TIME);                                               \
        if ((RET_CODE) < 0) {                                                                           \
            hinic_port_api_record_error(RET_CODE, API_INDEX, EXEC_TIME);                                \
        }                                                                                               \
    } while (0)

#define RTF_LOG_HINIC_PORT_API_LOG_VOID(TIME_START, EXEC_TIME, API_INDEX, API_FUNC)                         \
    do {                                                                                                    \
        TIME_START = rte_get_tsc_cycles();                                                                  \
        API_FUNC;                                                                                           \
        EXEC_TIME = 0;                                                                                      \
        uint64_t hz = rte_get_tsc_hz();                                                                     \
        if (hz != 0) {                                                                                      \
            EXEC_TIME = ((rte_get_tsc_cycles() - (TIME_START)) * 1000000UL) / hz;                           \
        }                                                                                                   \
        hinic3_port_fill_api_record(API_INDEX, EXEC_TIME);                                                   \
        HINIC3_LOG(WARNING, DRIVER, "Hinic HWAPI %s called, return void, cost %" PRIu64 " us.", __FUNCTION__, \
            EXEC_TIME);                                                                                     \
    } while (0)

#endif
