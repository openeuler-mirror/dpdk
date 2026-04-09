/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */

#include <errno.h>
#include <unistd.h>
#include <sys/eventfd.h>
#include <sys/epoll.h>
#include <sched.h>
#include <time.h>
#include "rte_cycles.h"
#include "rte_lcore.h"
#include "rte_spinlock.h"
#include "hinic3_thread.h"

#include "hinic3_log.h"
#include "hinic3_init.h"
#include "hinic3_iface_port.h"
#include "hinic3_capture_core.h"
#include "hinic3_capture_filter.h"
#include "hinic3_capture_utils.h"
#include "hinic3_capture_save.h"
#include "hinic3_capture_command.h"
#include "hinic3_nlattr.h"
#include "hinic3_util.h"
#include "rte_ethdev.h"
#include "hinic3_hugepage_meminfo.h"
#include "hinic3_cmd_exec.h"
#include "hinic3_ds.h"
#include "hinic3_capture_main.h"

static int
hinic3_pcap_thread(void)
{
    int ret;
    struct pcap_task_save_t *pcap_task_save = NULL;
    struct hinic3_init_arg *init_arg = NULL;
    uint32_t pcap_cpu_id;

    pcap_task_save = hinic3_get_cap_task_save();
    if (pcap_task_save == NULL)
        return -1;

    pcap_task_save->thread_exit = PCAP_THREAD_NORMAL_STATUS;
    ret = hinic3_thread_create(&pcap_task_save->thread, "hinic3_capture_thread", pcap_thread_main, NULL, HINIC3_CAPTURE);
    if (ret != 0) {
        HINIC3_LOG(ERR, CAPTURE, "Failed to create pcap thread. errno is %d!", ret);
        return -1;
    }
    if (hinic3_check_masked_to_exact_switch() == false) {
        hinic3_set_ctrl_thread_cpu_affinity(&pcap_task_save->thread);
        return 0;
    }

    init_arg = hinic3_get_init_arg();
    pcap_cpu_id = init_arg->pcap_cpu;
    (void)hinic3_set_single_cpu_affinity(&pcap_task_save->thread, pcap_cpu_id);

    return 0;
}

static int
hinic3_pcap_mem_pool_init(void)
{
    int socket_id;
    uint16_t bond_rx_depth = hinic3_bond_rx_depth_get();
    uint32_t mbuf_size = hinic3_mbuf_size_get();
    struct rte_mempool **pcap_shared_mp = NULL;
    uint32_t n_mbufs = PCAP_MAX_CAP_TASK * bond_rx_depth;

    pcap_shared_mp = hinic3_get_pcap_shared_mp();
    socket_id = (int)rte_lcore_to_socket_id(rte_get_main_lcore());
    if (*pcap_shared_mp == NULL) {
        *pcap_shared_mp = hinic3_rte_pktmbuf_pool_create("hinic3-pcap-mempool:", n_mbufs,
            RTE_MEMPOOL_CACHE_MAX_SIZE, 0, mbuf_size, socket_id);
        if (*pcap_shared_mp == NULL) {
            HINIC3_LOG(ERR, CAPTURE, "Failed to create pcap shared memory pool!");
            return -1;
        }
    }
    return 0;
}

static int
pcap_resource_init(void)
{
    int size;
    int ret;
    struct rte_mempool *pool = NULL;
    struct pcap_task_mgr_t *task_mgr = pcap_get_task_mgr();

    size = sizeof(struct pcap_task_mgr_t);
    memset(task_mgr, 0, size);

    pool = rte_mempool_lookup(PCAP_MEMPOOL_NAME);
    if (pool == NULL) {
        pool = hinic3_rte_mempool_create(PCAP_MEMPOOL_NAME, PCAP_MEMPOOL_SIZE,
                                        sizeof(struct pcap_pkt_dump_data), 0, 0, NULL, NULL, NULL, NULL,
                                        rte_socket_id(), 0);
        if (pool == NULL) {
            HINIC3_LOG(ERR, CAPTURE, "Create packet capture mempool failed!");
            return -1;
        }
    }
    task_mgr->mem_pool = pool;

    task_mgr->epoll_fd = epoll_create(PCAP_EPOLL_SIZE);
    if (task_mgr->epoll_fd < 0) {
        HINIC3_LOG(ERR, CAPTURE, "Create epoll fail, errno is %d!", errno);
        hinic3_rte_mempool_free(task_mgr->mem_pool);
        task_mgr->mem_pool = NULL;
        return -1;
    }

    ret = pthread_create(&task_mgr->save_thread, NULL, pcap_save_thread, NULL);
    if (ret != 0) {
        HINIC3_LOG(ERR, CAPTURE, "Create save_thread fail, errno is %d!", errno);
        hinic3_rte_mempool_free(task_mgr->mem_pool);
        task_mgr->mem_pool = NULL;
        close(task_mgr->epoll_fd);
        task_mgr->epoll_fd = -1;
        return -1;
    }
    hinic3_set_ctrl_thread_cpu_affinity(&task_mgr->save_thread);

    rte_spinlock_init(&task_mgr->lock);
    task_mgr->task_cnt = 0;

    HINIC3_LOG(INFO, CAPTURE, "pcap_init success.");
    return 0;
}

int
hinic3_pcap_init(void)
{
    int ret = 0;
    uint64_t begin_time = 0;

    begin_time = rte_get_tsc_hz() / PCAP_TIME_S_TO_MS;
    hinic3_set_ticks_per_ms(begin_time);
    ret = pcap_resource_init();
    if (ret != 0) {
        HINIC3_LOG(ERR, CAPTURE, "pcap_resource_init failed, err is %d!", ret);
        return -1;
    }

    ret = hinic3_pcap_mem_pool_init();
    if (ret != 0) {
        HINIC3_LOG(ERR, CAPTURE, "pcap memory pool init failed, err is %d!", ret);
        return -1;
    }

    ret = hinic3_pcap_thread();
    if (ret != 0) {
        HINIC3_LOG(ERR, CAPTURE, "pcap thread init fialed, err is %d!", ret);
        return -1;
    }

    pcap_unix_cmd_register();
    return 0;
}

void
hinic3_pcap_uninit(void)
{
    struct pcap_task_mgr_t *task_mgr = pcap_get_task_mgr();

    hinic3_rte_mempool_free(task_mgr->mem_pool);
    task_mgr->mem_pool = NULL;
    close(task_mgr->epoll_fd);
    task_mgr->epoll_fd = -1;
    task_mgr->thread_exit = true;
    hinic3_stop_pcap_threads();
}
