/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_VF_DFX_H
#define HINIC3_VF_DFX_H

#include "hinic3_util.h"
#include "hinic3_command.h"

#define HWPT_ETH_DEVICE_FORMAT_PATH             "/sys/bus/pci/devices/%04x:%02x:%02x.%u/net"

#define LCORE_INDEX_INVALID                     (-1)

#define PROFILE_UNIT_NUM                        2
#define QOS_UNIT_NUM                            4
#define QOS_MAX_NUM                             512
#define BW_QOS_UNIT                             "kbps"
#define PPS_QOS_UNIT                            "pps"

#define BW_QOS_MODE                             "byte"
#define PPS_QOS_MODE                            "packet"

#define BYTE_TO_BIT                             8
#define SAMPLING_INTERVAL                       500000
#define MAX_BUCKET_ID                           512
#define DEC_BASE_NUM                            10

struct vm_ports_info {
    uint64_t rx_pkts;
    uint64_t tx_pkts;
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    long long int time_moment;
};

void unixctl_hinic3_multi_qos_dfx_init(void);

#endif
