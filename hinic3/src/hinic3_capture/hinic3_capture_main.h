/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_CAPTURE_MAIN_H
#define HINIC3_CAPTURE_MAIN_H

int hinic3_pcap_init(void);
void hinic3_pcap_uninit(void);
void hinic3_stop_pcap_threads(void);
void pcap_hook_rx_pre(void);
void pcap_task_stop_as_eth_port_del(uint16_t vport_id);
#endif /* HINIC3_CAPTURE_MAIN_H */
