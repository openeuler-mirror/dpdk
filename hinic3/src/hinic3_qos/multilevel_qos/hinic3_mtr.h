/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2022 Huawei Technologies Co., Ltd
 */
#ifndef HINIC3_METER_H
#define HINIC3_METER_H

#include "ethdev_driver.h"
#include "hinic3_mtr_policy.h"
#include "hinic3_mtr_profile.h"
#include "rte_mtr.h"

#define HINIC3_METER_NUM_MAX 1024 // 0号位置不用
#define HINIC3_METER_PORT_NUM_MAX 64
#define HINIC3_METER_DIR_NUM 4

enum hinic3_qos_tablehead {
    BW_TX_TYPE,
    PPS_TX_TYPE,
    BW_RX_TYPE,
    PPS_RX_TYPE,
    QOS_TABLEHEAD_NUM,
};

enum hinic3_meter_qos_direction {
    HINIC3_TX_METER_QOS,
    HINIC3_RX_METER_QOS,
    HINIC3_NODIR_METER_QOS,
};

struct hinic3_meter_node {
    uint32_t meter_id;
    uint32_t port_id;
    uint16_t group_id;
    uint16_t qos_id;
    uint16_t flow_num;
    enum qos_type_limit type;
    enum hinic3_meter_qos_direction dir;
    struct rte_flow *flow;
    struct hinic3_mtr_policy_node *policy;
    struct hinic3_mtr_profile_node *profile;
    struct hinic3_list node;
};

struct hinic3_meter_list {
    struct hinic3_mutex mutex;
    struct hinic3_list node;
    uint32_t length;
};

struct hinic3_group_port_info {
    uint16_t vport_id;
    struct hinic3_list node;
};

struct hinic3_group_meter_info {
    uint32_t meter_id;
    bool is_used;
};

struct hinic3_group_info {
    uint16_t group_id;
    uint16_t length;
    struct hinic3_group_meter_info meter[HINIC3_METER_DIR_NUM];
    struct hinic3_list node;
    struct hinic3_mutex mutex;
};

struct hinic3_net_meter_info {
    uint32_t meter_id;
    bool is_used;
};

struct hinic3_qos_func_map {
    const char *qos_level;
    int (*func)(uint16_t port_id, uint32_t meter_id, uint32_t *next_meter_id, enum hinic3_meter_qos_direction dir);
};

struct hinic3_clear_qos_func_map {
    void (*func)(struct hinic3_vf_dev *vf_dev, enum hinic3_meter_qos_direction dir, uint64_t *qos_mask);
};

struct hinic3_hqos_stats_context
{
    uint16_t ids;
    int clear;
    int packet_mode;
    enum qos_type_limit type;
    enum hinic3_meter_qos_direction dir;
};

void hinic3_net_qos_clear(void);
struct hinic3_net_meter_info *hinic3_net_meter_info_get(void);
void hinic3_meter_list_lock(void);
void hinic3_meter_list_unlock(void);
struct hinic3_meter_list *hinic3_meter_list_get(void);
void hinic3_group_info_lock(uint16_t group_id);
void hinic3_group_info_unlock(uint16_t group_id);
struct hinic3_meter_node *hinic3_meter_find(uint32_t meter_id);
void hinic3_meter_list_init(void);
void hinic3_meter_group_info_init(void);
struct hinic3_group_info *hinic3_group_info_get(uint16_t group_id);
int hinic3_qos_flow_delete(struct rte_flow *flow);
struct rte_flow *hinic3_set_multi_qos(const struct rte_flow_item *pattern, const struct rte_flow_action *actions);
struct rte_mtr_ops *hinic3_mtr_multi_ops_construct(void);
void hinic3_port_meter_clear(struct hinic3_vf_dev *vf_dev, uint16_t vport_id);
int hinic3_group_meter_remove(uint16_t vport_id, uint16_t group_id);
void hinic3_multi_meter_init(void);
enum hinic3_qos_tablehead hinic3_get_multi_qos_show_tablehead(uint16_t dir, int packet_mode);
enum hinic3_meter_qos_direction hinic3_qos_get_dir(enum hinic3_qos_tablehead index);

#endif
