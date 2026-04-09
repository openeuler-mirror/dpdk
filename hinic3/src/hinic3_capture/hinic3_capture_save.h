/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_CAPTURE_SAVE_H
#define HINIC3_CAPTURE_SAVE_H

void pcap_pkt_save(struct pcap_task_t *cap_task, uint64_t remain_cnt, struct pcap_pkt_summary **buf,
                   uint32_t count, struct pcap_save_stats_t *save_stats);
void *pcap_save_thread(void *args);
#endif /* HINIC3_CAPTURE_SAVE_H */
