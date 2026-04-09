/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2022 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_VF_PORT_QOS_PUBLIC_H
#define HINIC3_VF_PORT_QOS_PUBLIC_H

#include "hinic3_util.h"
#include "hinic3_vf_controller.h"
#include "hinic3_mutex.h"
#define MAX_GROUP_QOS_TYPE_LEN                     64

#define HINIC3_GROUP_QOS_BW_INGRESS                 "ingress_bw"
#define HINIC3_GROUP_QOS_PPS_INGRESS                "ingress_pps"
#define HINIC3_GROUP_QOS_BW_EGRESS                  "egress_bw"
#define HINIC3_GROUP_QOS_PPS_EGRESS                 "egress_pps"

#define VF_NONE_QOS_FLAG                            0
#define VF_PORT_QOS_FLAG                            1
#define VF_GROUP_QOS_FLAG                           2
#define VF_QOS_TYPE_MAX_NUM                         4

#define ARGS_GROUP_QOS_ID_MAX_LEN                   512
#define MAX_QOS_ID_NUM                              1023
#define MIN_QOS_ID_NUM                              1

#define QOS_BW_TYPE                                 0
#define QOS_PPS_TYPE                                1

#define UNBIND_QOS_ID                               0xffff
#define KB_TO_B                                     1000
#define BYTE_TO_BIT                                 8

#define MAX_BW_BURST_QOS                            4096000UL
#define MAX_BW_RATE_QOS                             400000000UL
#define MIN_BW_RATE_QOS                             1000UL
#define MAX_PPS_BURST_QOS                           1000000UL
#define MAX_PPS_RATE_QOS                            128000000UL
#define MIN_PPS_RATE_QOS                            1000UL

enum hinic3_group_qos_type {
    HINIC3_ARGS_GROUP_QOS_BW_INGRESS,
    HINIC3_ARGS_GROUP_QOS_PPS_INGRESS,
    HINIC3_ARGS_GROUP_QOS_BW_EGRESS,
    HINIC3_ARGS_GROUP_QOS_PPS_EGRESS,
    HINIC3_ARGS_GROUP_QOS_MAX_TYPE,
};

typedef struct {
    char qos_type_key[HINIC3_ARGS_GROUP_QOS_MAX_TYPE][MAX_GROUP_QOS_TYPE_LEN];
} group_qos_type_key;

struct qos_single_value {
    uint64_t max_rate;
    uint64_t max_burst;
    uint64_t min_rate;
    uint64_t min_burst;
    bool is_RFC2697;
    int packet_mode;
};

group_qos_type_key *hinic3_get_qos_type_key(void);
bool is_hinic3_vf_dev(uint16_t port_id);
void hinic3_qos_init(void);
int hinic3_vm_qos_limit_set(uint16_t qos_id, uint16_t dir, uint16_t type, struct qos_single_value qos_value);
int hinic3_vm_qos_limit_get(uint16_t qos_id, uint16_t dir, uint16_t type, struct qos_single_value *qos_value);
int hinic3_port_qos_limit_set(uint16_t port_id, uint16_t dir, uint16_t type, const struct qos_single_value *qos_value);
int hinic3_port_qos_limit_get(uint16_t port_id, uint16_t dir, uint16_t type, struct qos_single_value *qos_value);
int hinic3_net_qos_limit_set(uint16_t host_id, uint16_t dir, uint16_t type, uint64_t max_rate, uint64_t max_burst);
int hinic3_net_qos_limit_get(uint16_t host_id, uint16_t dir, uint16_t type, uint64_t *max_rate, uint64_t *max_burst);
int hinic3_flow_qos_limit_set(uint16_t qos_id, uint16_t type, uint64_t max_rate, uint64_t max_burst,
                             uint64_t min_rate, uint64_t min_burst);
int hinic3_flow_qos_limit_get(uint16_t qos_id, uint16_t type, uint64_t *max_rate, uint64_t *max_burst,
                             uint64_t *min_rate, uint64_t *min_burst);
int hinic3_vf_qos_statistics_get_all_batch(uint16_t *qos_array, struct hovs_qos_stats_batch_all *stats, size_t cnt);
int hinic3_vf_qos_statistics_clear_batch(uint16_t *qos_array, size_t cnt);
int hinic3_hqos_statistics_clear(enum qos_type_limit type, uint16_t dir, uint16_t *ids, size_t cnt);
int hinic3_hqos_statistics_get_all_batch(enum qos_type_limit type, uint16_t *ids,
    struct hovs_hqos_stats_batch_all *stats, size_t cnt);
int check_vf_bw_args(struct qos_single_value vm_qos);
int check_vf_pps_args(struct qos_single_value vm_qos);
int hinic3_port_mgmt_set_qos_id(uint16_t vport_id, uint16_t bucket_id);
int hinic3_vf_qos_statistics_clear(uint16_t *vport_id);
#endif
