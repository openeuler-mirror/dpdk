/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */

#include <errno.h>
#include <unistd.h>
#include <sys/uio.h>
#include <sys/eventfd.h>
#include <sys/epoll.h>
#include <sched.h>
#include <time.h>
#include <errno.h>
#include "rte_cycles.h"
#include "rte_lcore.h"
#include "rte_spinlock.h"

#include "hinic3_log.h"
#include "hinic3_init.h"
#include "hinic3_iface_port.h"
#include "hinic3_capture_core.h"
#include "hinic3_capture_filter.h"
#include "hinic3_capture_save.h"
#include "hinic3_nlattr.h"
#include "hinic3_util.h"
#include "rte_ethdev.h"
#include "hinic3_eth_util.h"
#include "hinic3_check_thread_health_state.h"
#include "hinic3_cmd_exec.h"
#include "hinic3_ds.h"
#include "hinic3_capture_main.h"

static int
pcap_stop_timeout_task(uint32_t pcap_id)
{
    struct pcap_stop_task_ctl stop_ctl;

    stop_ctl.task = NULL;
    stop_ctl.is_stopped = false;
    stop_ctl.result = 0;
    pcap_task_stop_set(pcap_id, &stop_ctl);
    if (!stop_ctl.task) {
        HINIC3_LOG(ERR, CAPTURE, "Capture task of pcap id %u not found!", pcap_id);
        return -1;
    }
    if (stop_ctl.result != 0) {
        HINIC3_LOG(ERR, CAPTURE, "Capture task of pcap id %u stopped fail!", pcap_id);
        return -1;
    }

    if (stop_ctl.is_stopped) {
        HINIC3_LOG(INFO, CAPTURE, "Stop capture task %u success.", pcap_id);
        pcap_task_destroy(stop_ctl.task);
        return 0;
    }

    return -1;
}

void
pcap_hook_rx_pre(void)
{
    int i;
    struct pcap_port_t port_mirror;
    struct pcap_task_batch task_batch;
    struct pcap_task_t *pcap_task_record = NULL;

    if (pcap_switch_get() == 0)
        return;

    pcap_task_batch_init(&task_batch);
    pcap_port_tasks_get(&task_batch, &port_mirror);
    if (task_batch.count == 0) {
        if (pcap_task_get() != 0) {
            pcap_task_set(0);
            pcap_time_set(hinic3_time_sec());
        }
        if (pcap_cpu_usage_get() == PCAP_CPU_LOW) {
            usleep(PCAP_PERIOD_US);
        }
        return;
    }

    pcap_task_set(task_batch.count);
    for (i = 0; i < task_batch.count; i++) {
        pcap_task_record = task_batch.task_array[i];
        if (pcap_timeout_check_s(pcap_task_record->key.task_start_time, pcap_task_record->key.task_time)) {
            pcap_task_record->stop_flag = true;
            pcap_task_record->key.task_time = 0;
        }

        pcap_hinic3_task_exec(pcap_task_record);
        pcap_task_put(pcap_task_record);
    }

    for (i = 0; i < task_batch.count; i++) {
        pcap_task_record = task_batch.task_array[i];
        if (pcap_task_record->stop_flag == true && pcap_task_record->key.task_time == 0) {
            HINIC3_LOG(INFO, CAPTURE, "Going to stop capture task %d because timeout.", pcap_task_record->pcap_id);
            int ret = pcap_stop_timeout_task(pcap_task_record->pcap_id);
            if (ret != 0)
                HINIC3_LOG(ERR, CAPTURE, "timeout capture task %u stop failed!", pcap_task_record->pcap_id);
            continue;
        }
    }

    return;
}