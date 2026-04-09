/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_CHECK_THREAD_HEALTH_STATE_H
#define HINIC3_CHECK_THREAD_HEALTH_STATE_H

#include <time.h>
#include <stdlib.h>
#include <stdint.h>
#include "hinic3_command.h"

#define CONST_THREAD_NUM 4
#define HINIC3_OFFLOAD_THREAD_NUM_MAX 4
#define HINIC3_PCAP_SAVE_THREAD_SIGNAL_INCREASE_INTER 5
#define HINIC3_CAPTURE_THREAD_SIGNAL_INCREASE_INTER 5
#define HINIC3_LISTEN_THREAD_SIGNAL_INCREASE_INTER 5
#define HINIC3_HPD_THREAD_SIGNAL_INCREASE_INTER 5
#define HINIC3LOAD_THREAD_SIGNAL_INCREASE_INTER 5
enum check_thread_item_type {
    CAPTURE_THREAD,
    LISTEN_THREAD,
    PCAP_SAVE_THREAD,
    HPD_THREAD,
    OFFLOAD_THREAD_ONE,
    OFFLOAD_THREAD_TWO,
    OFFLOAD_THREAD_THREE,
    OFFLOAD_THREAD_FOUR,
};

struct hinic3_thread_name {
    enum check_thread_item_type thread_id;
    const char *thread_name;
};

int hinic3_check_thread_health_state(void);
int hinic3_polling_thread_init(void);
void hinic3_polling_thread_uninit(void);
long long hinic3_thread_signal_increase(long long start, uint32_t index, uint32_t signal_increase_inter);
void unixctl_thread_status_dfx_init(void);
void hinic3_thread_stats_show(struct unixctl_conn *conn, int argc HINIC3_UNUSED,
    const char *argv[] HINIC3_UNUSED, void *aux);

#endif
