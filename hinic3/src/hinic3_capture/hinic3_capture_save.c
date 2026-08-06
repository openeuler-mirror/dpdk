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
#include "hinic3_nlattr.h"
#include "hinic3_util.h"
#include "rte_ethdev.h"
#include "hinic3_eth_util.h"
#include "hinic3_check_thread_health_state.h"
#include "hinic3_cmd_exec.h"
#include "hinic3_ds.h"
#include "hinic3_capture_save.h"


static inline void
pcap_pkt_save_info_get(struct pcap_pkt_summary *buf, const struct pcap_key_t *key,
                                           struct pcap_pkt_save_info *info)
{
    if (pcap_mode_get() == ONLY_HEADER) {
        if (key->vxlan_inner && buf->pkt_header.is_vxlan) {
            info->save_head = buf->pkt_header.out_header.save_head;
            info->save_len = buf->pkt_header.out_header.save_len + buf->pkt_header.inner_header.save_len;
        } else {
            info->save_head = buf->pkt_header.out_header.save_head;
            info->save_len = buf->pkt_header.out_header.save_len;
        }
    } else {
        info->save_head = buf->data;
        info->save_len = buf->len;
    }
}

static inline uint32_t
pcap_calc_write_pkt_cnt(uint32_t byte_cnt, struct iovec *iov, uint32_t count)
{
    uint32_t work_byte_cnt = byte_cnt;
    uint32_t wr_cnt = 0;

    for (uint32_t i = 0; i < count; i++) {
        if (work_byte_cnt < iov[i].iov_len)
            break;
        wr_cnt++;
        work_byte_cnt -= iov[i].iov_len;
    }
    return wr_cnt;
}

static void
pcap_pkt_file_write(const struct pcap_task_t *cap_task, struct iovec *iov, uint32_t iov_cnt,
                                struct pcap_save_stats_t *save_stats)
{
    ssize_t byte_cnt;
    uint32_t wr_cnt;

    byte_cnt = writev(fileno(cap_task->save_file), iov, iov_cnt);
    if (byte_cnt <= 0) {
        wr_cnt = 0;
        HINIC3_LOG(WARNING, CAPTURE, "writev fail, ret is %ld, errno is %d, "
            "pcap file rotate may occur!", byte_cnt, errno);
    } else {
        wr_cnt = pcap_calc_write_pkt_cnt(byte_cnt, iov, iov_cnt);
    }

    if (wr_cnt >= iov_cnt)
        save_stats->wr_iov_cnt = iov_cnt;
    else {
        save_stats->wr_iov_cnt = wr_cnt;
        save_stats->write_fail = true;
        HINIC3_LOG(ERR, CAPTURE, "write_cnt is %u, fail_cnt is %u!", wr_cnt, iov_cnt - wr_cnt);
    }
    return;
}

static void
pcap_pkt_file_save(struct pcap_task_t *cap_task, uint64_t remain_cnt, struct pcap_pkt_summary **buf,
                               uint32_t count, struct pcap_save_stats_t *save_stats)
{
    uint32_t i;
    uint32_t iov_cnt = 0;
    int idx;
    struct timeval tv;
    struct pcap_pkt_save_info save_info;
    struct pcap_pkt_summary *tmp_buf = NULL;
    struct pcap_file_record_hdr record_hdr[PKT_MAX_BURST];
    struct iovec iov[PKT_MAX_BURST * PCAP_DOUBLE];
    uint32_t first_file_pkt_cnt;
    uint32_t pkt_to_write = count;

    if (cap_task->filenum > 1 &&
        cap_task->file_pkt_cnt + count > cap_task->count_per_file &&
        cap_task->file_index + 1 < cap_task->filenum) {
        first_file_pkt_cnt = cap_task->count_per_file - cap_task->file_pkt_cnt;
    } else {
        first_file_pkt_cnt = count;
    }

    gettimeofday(&tv, NULL);
    for (i = 0; i < first_file_pkt_cnt; i++) {
        tmp_buf = buf[i];
        if (tmp_buf == NULL) {
            continue;
        }
        pcap_pkt_save_info_get(tmp_buf, &cap_task->key, &save_info);

        record_hdr[i].pkt_ts_sec = (uint32_t)tv.tv_sec;
        record_hdr[i].pkt_ts_usec = (uint32_t)tv.tv_usec;
        record_hdr[i].pkt_incl_len = save_info.save_len;
        record_hdr[i].pkt_orig_len = tmp_buf->pkt_len;

        idx = i * PCAP_DOUBLE;
        iov[idx].iov_base = &(record_hdr[i]);
        iov[idx].iov_len = sizeof(struct pcap_file_record_hdr);
        iov[idx + 1].iov_base = save_info.save_head;
        iov[idx + 1].iov_len = save_info.save_len;
        iov_cnt += PCAP_DOUBLE;
    }

    if (remain_cnt < first_file_pkt_cnt)
        iov_cnt = remain_cnt * PCAP_DOUBLE;

    pcap_pkt_file_write(cap_task, iov, iov_cnt, save_stats);
    save_stats->wr_cnt = save_stats->wr_iov_cnt / PCAP_DOUBLE;
    cap_task->file_pkt_cnt += save_stats->wr_cnt;

    if (pkt_to_write > first_file_pkt_cnt &&
        !cap_task->stop_flag &&
        cap_task->file_index + 1 < cap_task->filenum) {
        cap_task->file_index++;
        if (pcap_file_rotate(cap_task) != 0) {
            HINIC3_LOG(ERR, CAPTURE, "pcap_file_rotate fail!");
            cap_task->file_index--;
            return;
        }

        iov_cnt = 0;
        for (i = first_file_pkt_cnt; i < count; i++) {
            tmp_buf = buf[i];
            if (tmp_buf == NULL) {
                continue;
            }
            pcap_pkt_save_info_get(tmp_buf, &cap_task->key, &save_info);

            record_hdr[i - first_file_pkt_cnt].pkt_ts_sec = (uint32_t)tv.tv_sec;
            record_hdr[i - first_file_pkt_cnt].pkt_ts_usec = (uint32_t)tv.tv_usec;
            record_hdr[i - first_file_pkt_cnt].pkt_incl_len = save_info.save_len;
            record_hdr[i - first_file_pkt_cnt].pkt_orig_len = tmp_buf->pkt_len;

            idx = (i - first_file_pkt_cnt) * PCAP_DOUBLE;
            iov[idx].iov_base = &(record_hdr[i - first_file_pkt_cnt]);
            iov[idx].iov_len = sizeof(struct pcap_file_record_hdr);
            iov[idx + 1].iov_base = save_info.save_head;
            iov[idx + 1].iov_len = save_info.save_len;
            iov_cnt += PCAP_DOUBLE;
        }

        pcap_pkt_file_write(cap_task, iov, iov_cnt, save_stats);
        save_stats->wr_cnt = save_stats->wr_iov_cnt / PCAP_DOUBLE;
        cap_task->file_pkt_cnt += save_stats->wr_cnt;
    }

    return;
}

static void
pcap_epoll_event_process(uint32_t pcap_id)
{
    uint32_t i;
    uint64_t save_cnt;
    uint32_t pkt_cnt;
    uint64_t value;
    struct pcap_save_stats_t save_stats;
    struct pcap_task_mirror_t task_mirror;
    struct pcap_task_t *cap_task = NULL;
    struct pcap_pkt_dump_data *tmp_data = NULL;
    struct iovec iov[PKT_MAX_BURST];
    struct pcap_pkt_dump_data *pkt_dump_list[PKT_MAX_BURST] = { NULL };
    struct pcap_task_mgr_t *task_mgr = pcap_get_task_mgr();

    cap_task = pcap_task_get_by_id(pcap_id, &task_mirror);
    if (!cap_task)
        return;

    /* get packet from ring */
    pkt_cnt = pcap_rte_ring_dequeue_burst(cap_task->ring, (void **)pkt_dump_list, PKT_MAX_BURST, NULL);
    if (pkt_cnt == 0) {
        pcap_task_put(cap_task);
        return;
    }

    if (task_mirror.remain_count == 0 || task_mirror.wr_fail_flag) {
        pcap_task_put(cap_task);
        pcap_rte_mempool_put_bulk(task_mgr->mem_pool, (void * const *)pkt_dump_list, pkt_cnt);
        return;
    }

    /* save packet */
    for (i = 0; i < pkt_cnt; i++) {
        tmp_data = pkt_dump_list[i];
        iov[i].iov_base = &tmp_data->record_hdr;
        iov[i].iov_len = (sizeof(struct pcap_file_record_hdr) + tmp_data->record_hdr.pkt_incl_len);
    }
    save_cnt = (pkt_cnt > task_mirror.remain_count) ? task_mirror.remain_count : pkt_cnt;

    uint32_t first_file_pkt_cnt;
    if (cap_task->filenum > 1 &&
        cap_task->file_pkt_cnt + save_cnt > cap_task->count_per_file &&
        cap_task->file_index + 1 < cap_task->filenum) {
        first_file_pkt_cnt = cap_task->count_per_file - cap_task->file_pkt_cnt;
    } else {
        first_file_pkt_cnt = save_cnt;
    }

    memset(&save_stats, 0, sizeof(save_stats));
    pcap_pkt_file_write(cap_task, iov, first_file_pkt_cnt, &save_stats);
    save_stats.wr_cnt = save_stats.wr_iov_cnt;
    cap_task->file_pkt_cnt += save_stats.wr_cnt;
    pcap_task_stats_update(cap_task, &save_stats);

    if (save_cnt > first_file_pkt_cnt &&
        !cap_task->stop_flag &&
        cap_task->file_index + 1 < cap_task->filenum) {
        cap_task->file_index++;
        if (pcap_file_rotate(cap_task) != 0) {
            HINIC3_LOG(ERR, CAPTURE, "pcap_file_rotate fail in epoll!");
            cap_task->file_index--;
            pcap_task_put(cap_task);
            pcap_rte_mempool_put_bulk(task_mgr->mem_pool, (void * const *)pkt_dump_list, pkt_cnt);
            return;
        }

        memset(&save_stats, 0, sizeof(save_stats));
        pcap_pkt_file_write(cap_task, iov + first_file_pkt_cnt, save_cnt - first_file_pkt_cnt, &save_stats);
        save_stats.wr_cnt = save_stats.wr_iov_cnt;
        cap_task->file_pkt_cnt += save_stats.wr_cnt;
        pcap_task_stats_update(cap_task, &save_stats);

    }

    if (pcap_rte_ring_count(cap_task->ring) == 0)
        eventfd_read(cap_task->event_fd, &value);

    pcap_task_put(cap_task);
    pcap_rte_mempool_put_bulk(task_mgr->mem_pool, (void * const *)pkt_dump_list, pkt_cnt);
    return;
}

static void
pcap_pkt_ring_save(struct pcap_task_t *cap_task, struct pcap_pkt_summary **buf,
                               uint32_t count, struct pcap_save_stats_t *save_stats)
{
    uint32_t i;
    int ret;
    uint32_t enq_cnt;
    uint32_t save_cnt = 0;
    uint32_t really_len;
    struct timeval tv;
    struct pcap_pkt_save_info save_info;
    struct pcap_pkt_summary *tmp_buf = NULL;
    struct pcap_pkt_dump_data *tmp_item = NULL;
    struct pcap_task_mgr_t *task_mgr = pcap_get_task_mgr();
    struct pcap_pkt_dump_data *dump_list[PKT_MAX_BURST] = { NULL };

    gettimeofday(&tv, NULL);
    for (i = 0; i < count; i++) {
        tmp_buf = buf[i];
        ret = pcap_rte_mempool_get(task_mgr->mem_pool, (void **)(&tmp_item));
        if (ret != 0) {
            save_stats->en_ring_fail_cnt++;
            continue;
        }

        pcap_pkt_save_info_get(tmp_buf, &cap_task->key, &save_info);
        tmp_item->record_hdr.pkt_ts_sec = tv.tv_sec;
        tmp_item->record_hdr.pkt_ts_usec = tv.tv_usec;
        really_len = (save_info.save_len > PCAP_DUMP_SIZE) ? PCAP_DUMP_SIZE : save_info.save_len;
        tmp_item->record_hdr.pkt_incl_len = really_len;
        tmp_item->record_hdr.pkt_orig_len = tmp_buf->pkt_len;

        memcpy(tmp_item->data, save_info.save_head, really_len);
        if (tmp_item->data == NULL) {
            pcap_rte_mempool_put(task_mgr->mem_pool, tmp_item);
            save_stats->en_ring_fail_cnt++;
            continue;
        }
        dump_list[save_cnt] = tmp_item;
        save_cnt++;
    }
    if (save_cnt <= 0) {
        HINIC3_LOG(ERR, CAPTURE, "The packet to ring failed, save_cnt is %u!", save_cnt);
        return;
    }

    enq_cnt = pcap_rte_ring_enqueue_burst(cap_task->ring, (void * const *)dump_list, save_cnt, NULL);
    if (enq_cnt < save_cnt) {
        save_stats->en_ring_fail_cnt += save_cnt - enq_cnt;
        pcap_rte_mempool_put_bulk(task_mgr->mem_pool, (void * const *)&dump_list[enq_cnt], save_cnt - enq_cnt);
    }
    if (enq_cnt > 0)
        eventfd_write(cap_task->event_fd, 1);

    return;
}

void *
pcap_save_thread(void *args HINIC3_UNUSED)
{
    int i;
    int nfds;
    struct epoll_event events[PCAP_MAX_CAP_TASK];
    struct pcap_task_mgr_t *task_mgr = pcap_get_task_mgr();

    pthread_setname_np(pthread_self(), "hinic3_pcap_save");
    enum check_thread_item_type check_thread = PCAP_SAVE_THREAD;
    long long start = hinic3_time_msec();
    while (!task_mgr->thread_exit) {
        start = hinic3_thread_signal_increase(start, check_thread, HINIC3_PCAP_SAVE_THREAD_SIGNAL_INCREASE_INTER);
        nfds = epoll_wait(task_mgr->epoll_fd, events, PCAP_MAX_CAP_TASK, PCAP_EPOLL_TIMEOUT);
        for (i = 0; i < nfds; ++i)
            pcap_epoll_event_process(events[i].data.u32);
    }

    return NULL;
}

void
pcap_pkt_save(struct pcap_task_t *cap_task, uint64_t remain_cnt, struct pcap_pkt_summary **buf,
                   uint32_t count, struct pcap_save_stats_t *save_stats)
{
    if (cap_task->key.write_way == WRITE_BY_PMD)
        pcap_pkt_file_save(cap_task, remain_cnt, buf, count, save_stats);
    else
        pcap_pkt_ring_save(cap_task, buf, count, save_stats);
}
