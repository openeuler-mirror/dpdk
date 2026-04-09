/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */

#include <errno.h>
#include <sys/uio.h>
#include <sys/time.h>
#include <sys/eventfd.h>
#include <sys/epoll.h>
#include "rte_cycles.h"
#include "rte_mbuf.h"
#include "hinic3_log.h"
#include "hinic3_iface_global.h"
#include "hinic3_capture_filter.h"
#include "hinic3_capture_save.h"
#include "hinic3_capture_core.h"

static inline void
pcap_hinic3_pkt_summary_init(struct pcap_pkt_summary *buf, struct rte_mbuf *pkt)
{
    struct hinic3_pcap_probe_user_data *usrdata = NULL;

    buf->data = rte_pktmbuf_mtod(pkt, void*);
    buf->len = rte_pktmbuf_data_len(pkt);

    usrdata = (struct hinic3_pcap_probe_user_data *)(&pkt->dynfield1[1]);
    buf->pkt_len = (usrdata->org_pkt_len >= buf->len) ? usrdata->org_pkt_len : buf->len;
    buf->vlan_id = (pkt->ol_flags & RTE_MBUF_F_RX_VLAN) ? ((pkt->vlan_tci) & VLAN_VID_MASK) : 0;
}

static void
pcap_hinic3_pkts_process(struct pcap_task_t *cap_task, struct pcap_task_mirror_t *task_mirror,
    struct rte_mbuf **rx_pkts, uint32_t cnt)
{
    uint32_t i;
    int save_cnt;
    uint32_t fail_cnt;
    struct pcap_save_stats_t save_stats;
    struct pcap_pkt_summary buf[PKT_MAX_BURST];
    struct pcap_pkt_summary *save_list[PKT_MAX_BURST] = {NULL};

    save_cnt = 0;
    fail_cnt = 0;
    memset(buf, 0, sizeof(buf));
    for (i = 0; i < cnt; i++) {
        pcap_hinic3_pkt_summary_init(&buf[i], rx_pkts[i]);
        pcap_pkt_parse(&buf[i]);
        if (buf[i].ctx.parse_result) {
            save_list[save_cnt] = &buf[i];
            save_cnt++;
        } else
            fail_cnt++;
    }

    memset(&save_stats, 0, sizeof(save_stats));
    save_stats.parse_fail_cnt = fail_cnt;
    if (save_cnt > 0)
        pcap_pkt_save(cap_task, task_mirror->remain_count, save_list, save_cnt, &save_stats);
    pcap_task_stats_update(cap_task, &save_stats);

    return;
}

void
pcap_comm_task_exec(struct pcap_task_t *cap_task, struct pcap_pkt_summary *buf, uint32_t buf_cnt)
{
    uint32_t i;
    uint32_t fail_cnt;
    uint32_t save_cnt;
    bool flag = false;
    struct pcap_task_mirror_t task_mirror;
    struct pcap_save_stats_t save_stats;
    struct pcap_pkt_summary *tmp_buf = NULL;
    struct pcap_pkt_summary *save_list[PKT_MAX_BURST] = {NULL};

    pcap_task_mirror_get(cap_task, &task_mirror);
    if (cap_task->stop_flag || cap_task->remain_count == 0 || cap_task->wr_fail_flag)
        return;

    save_cnt = 0;
    fail_cnt = 0;
    for (i = 0; i < buf_cnt; i++) {
        tmp_buf = &buf[i];
        if (!tmp_buf->ctx.parse_result) {
            fail_cnt++;
            continue;
        }

        flag = pcap_pkt_filter(tmp_buf, &cap_task->key);
        if (!flag)
            continue;

        save_list[save_cnt] = tmp_buf;
        save_cnt++;
    }

    memset(&save_stats, 0, sizeof(save_stats));
    save_stats.parse_fail_cnt = fail_cnt;
    if (save_cnt > 0) {
        pcap_pkt_save(cap_task, task_mirror.remain_count, save_list, save_cnt, &save_stats);
    }
    pcap_task_stats_update(cap_task, &save_stats);
    return;
}

void
pcap_hinic3_task_exec(struct pcap_task_t *cap_task)
{
    uint32_t pkt_cnt;
    struct pcap_task_mirror_t task_mirror;
    struct rte_mbuf *rx_pkts[PKT_MAX_BURST] = { NULL };
    struct hinic3_pcap_probe_rule_q_map *queue_info = &cap_task->driver.queue_info;

    /* maybe more than one pmds will exec a task, so need to lock */
    pcap_task_spin_lock(cap_task);

    if (cap_task->stop_flag || cap_task->remain_count == 0 || cap_task->wr_fail_flag) {
        pcap_task_spin_unlock(cap_task);
        return;
    }

    task_mirror.stop_flag = cap_task->stop_flag;
    task_mirror.wr_fail_flag = cap_task->wr_fail_flag;
    task_mirror.remain_count = cap_task->remain_count;

    pkt_cnt = hinic3_global_rte_eth_rx_burst(queue_info->hinic3_dpdk_port_id, queue_info->queue_id,
                                            rx_pkts, PKT_MAX_BURST);
    pcap_task_spin_unlock(cap_task);
    if (pkt_cnt == 0)
        return;

    pcap_hinic3_pkts_process(cap_task, &task_mirror, rx_pkts, pkt_cnt);
    rte_pktmbuf_free_bulk(rx_pkts, pkt_cnt);
    return;
}
