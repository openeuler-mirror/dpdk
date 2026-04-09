/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Huawei Technologies Co., Ltd
 */

#include "hinic3_log.h"
#include "hinic3_util.h"
#include "hinic3_thread.h"
#include "hinic3_iface_flow.h"
#include "hinic3_ds.h"
#include "hinic3_parse_agent_config.h"
#include "hinic3_ui_string.h"
#include "hinic3_iface_global.h"
#include "hinic3_check_thread_health_state.h"

#define HINIC3_MAX_THREAD_NUMS (CONST_THREAD_NUM + HINIC3_OFFLOAD_THREAD_NUM_MAX)
#define HINIC3_CHECK_THREAD_INTER_TIME 60
#define HINIC3_HPD_THREAD_INTER_TIME 200000
#define HINIC3_THREAD_NAME_MAX_LEN 50

struct check_thread {
    int thread_health;
    pthread_t thread;
    uint32_t thread_exit;
    uint32_t signal[HINIC3_MAX_THREAD_NUMS];
    int thread_status[HINIC3_MAX_THREAD_NUMS];
};

struct hinic3_hpd_mgr {
    pthread_t thread;
    uint32_t thread_exit;
};

static struct check_thread g_check_thread = { 0 };
static struct hinic3_hpd_mgr g_hpd_mgr = { 0 };
static struct hinic3_thread_name g_thread_name[HINIC3_MAX_THREAD_NUMS] = {
    {CAPTURE_THREAD, "hinic3-capture-t"},
    {LISTEN_THREAD, "hinic3-listen-th"},
    {PCAP_SAVE_THREAD, "hinic3-pcap-save"},
    {HPD_THREAD, "hinic3-hpd-th"},
    {OFFLOAD_THREAD_ONE, "hinic3-offload-0"},
    {OFFLOAD_THREAD_TWO, "hinic3-offload-1"},
    {OFFLOAD_THREAD_THREE, "hinic3-offload-2"},
    {OFFLOAD_THREAD_FOUR, "hinic3-offload-3"},
};

static void hinic3_signal_increase(uint32_t thread_index)
{
    if (thread_index >= HINIC3_MAX_THREAD_NUMS) {
        HINIC3_LOG(ERR, AGENT, "HINIC3 CHECK THREAD: thread_index %u exceeded the number of check thread!", thread_index);
        return;
    }
    g_check_thread.signal[thread_index]++;
}

static void *
hinic3_check_thread_main(void *arg HINIC3_UNUSED)
{
    uint32_t prev_signal[HINIC3_MAX_THREAD_NUMS] = {0};
    uint32_t offload_thread_num = hinic3_offload_thread_num_get();
    uint32_t check_thread_num = CONST_THREAD_NUM + offload_thread_num;
    bool is_check_hpd_thread = hinic3_support_port_hot_plug();

    while (g_check_thread.thread_exit == HINIC3_THREAD_NORMAL_STATUS) {
        sleep(HINIC3_CHECK_THREAD_INTER_TIME);
        g_check_thread.thread_health = 0;
        memset(g_check_thread.thread_status, 0, sizeof(g_check_thread.thread_status));
        for (uint32_t thread_num = 0; thread_num < check_thread_num; thread_num++) {
            if (strcmp(g_thread_name[thread_num].thread_name, "hinic3-hpd-th") == 0 && !is_check_hpd_thread) {
                continue;
            }
            if (g_check_thread.signal[thread_num] == prev_signal[thread_num]) {
                g_check_thread.thread_health = -(thread_num + 1);
                g_check_thread.thread_status[thread_num] = -1;
                /* Marks different thread exception states,
                -(thread_num + 1) represents (thread_num + 1) thread exception */
                HINIC3_LOG(ERR, AGENT, "Check thread error, %s thread is abnormal!",
                    g_thread_name[thread_num].thread_name);
            }
            prev_signal[thread_num] = g_check_thread.signal[thread_num];
        }
    }
    return NULL;
}

void
hinic3_thread_stats_show(struct unixctl_conn *conn, int argc HINIC3_UNUSED,
    const char *argv[] HINIC3_UNUSED, void *aux)
{
    struct ds ds = DS_EMPTY_INITIALIZER;
    uint32_t offload_thread_num = hinic3_offload_thread_num_get();
    uint32_t check_thread_num = CONST_THREAD_NUM + offload_thread_num;

    for (uint32_t index = 0; index < check_thread_num; index++) {
        char thread_name[HINIC3_THREAD_NAME_MAX_LEN] = {0};
        int ret = snprintf(thread_name, sizeof(thread_name),
         "%s:", g_thread_name[index].thread_name);
        if (ret <= 0) {
            HINIC3_LOG(ERR, AGENT, "snprintf for thread_name %s failed!", g_thread_name[index].thread_name);
            return;
        }
        if (g_check_thread.thread_status[index] == -1) {
            hinic3_ds_put_format(&ds, "%2s%s%6s%5sunhealthy\n", HINIC3_UI_INDENT_SPACE, thread_name,
                HINIC3_UI_INDENT_SPACE, HINIC3_UI_INDENT_SPACE);
        } else {
            hinic3_ds_put_format(&ds, "%2s%s%6s%5shealthy\n", HINIC3_UI_INDENT_SPACE, thread_name,
                HINIC3_UI_INDENT_SPACE, HINIC3_UI_INDENT_SPACE);
        }
    }
    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
    *(int *)aux = 0;
    return;
}

void unixctl_thread_status_dfx_init(void)
{
    hinic3_command_register("hwoff/show-thread-stats", "", 0, 0, hinic3_thread_stats_show, NULL);
}

long long hinic3_thread_signal_increase(long long start, uint32_t index, uint32_t signal_increase_inter)
{
    long long end = hinic3_time_msec();
    if ((end - start) / SEC_TO_MSEC_BASE >= signal_increase_inter) {
        hinic3_signal_increase(index);
        start = end;
    }

    return start;
}

int hinic3_check_thread_health_state(void)
{
    return g_check_thread.thread_health;
}

static void *hinic3_check_hpd_main(void *arg HINIC3_UNUSED)
{
    enum check_thread_item_type check_thread = HPD_THREAD;
    long long start = hinic3_time_msec();

    while (g_hpd_mgr.thread_exit == HINIC3_THREAD_NORMAL_STATUS) {
        start = hinic3_thread_signal_increase(start, check_thread, HINIC3_HPD_THREAD_SIGNAL_INCREASE_INTER);
        usleep(HINIC3_HPD_THREAD_INTER_TIME);
        (void)hinic3_global_rte_eth_rx_burst(0, (uint16_t)HINIC3_PTHREAD_HOTPLUG_CTX_CALL, NULL, 0);
    }
    return NULL;
}

int hinic3_polling_thread_init(void)
{
    int ret;
    ret = hinic3_thread_create(&g_check_thread.thread, "hinic3_check_th", hinic3_check_thread_main, NULL, HINIC3_INIT);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "hinic3 check thread create failed, err is %d!", ret);
        return -1;
    }

    hinic3_set_ctrl_thread_cpu_affinity(&g_check_thread.thread);
    if (hinic3_support_port_hot_plug()) {
        ret = hinic3_thread_create(&g_hpd_mgr.thread, "hinic3_hpd_th", hinic3_check_hpd_main, NULL, HINIC3_INIT);
        if (ret != 0) {
            HINIC3_LOG(ERR, AGENT, "hinic3 hpd thread create failed, err is %d!", ret);
            return -1;
        }
        hinic3_set_ctrl_thread_cpu_affinity(&g_hpd_mgr.thread);
    }

    return 0;
}

void hinic3_polling_thread_uninit(void)
{
    g_check_thread.thread_exit = HINIC3_THREAD_EXIT_STATUS;
    if (hinic3_support_port_hot_plug()) {
        g_hpd_mgr.thread_exit = HINIC3_THREAD_EXIT_STATUS;
    }
}
