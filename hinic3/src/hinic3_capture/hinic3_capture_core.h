/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_CAPTURE_CORE_H
#define HINIC3_CAPTURE_CORE_H

#include "hinic3_capture_utils.h"
#include "hinic3_capture_filter.h"

struct pcap_pkt_dump_data {
    struct pcap_file_record_hdr record_hdr;
    char data[PCAP_DUMP_SIZE];
};

void pcap_comm_task_exec(struct pcap_task_t *cap_task, struct pcap_pkt_summary *buf, uint32_t buf_cnt);
void pcap_hinic3_task_exec(struct pcap_task_t *cap_task);

#endif /* HINIC3_CAPTURE_CORE_H */
