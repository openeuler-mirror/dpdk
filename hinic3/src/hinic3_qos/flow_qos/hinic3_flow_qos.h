/*
 * 版权所有 (c) 华为技术有限公司 2024-2024
 * 功能描述: meter 初始化头文件
 *  * 创建日期: 2024-09-29
 *  */

#ifndef _HINIC3_FLOW_QOS_H_
#define _HINIC3_FLOW_QOS_H_

#include <stdint.h>
#include "hinic3_mutex.h"
#include "hinic3_hash.h"
#include "rte_mtr.h"
#include "hinic3_mtr.h"

#define METER_UNIT_NUM 2
#define DEC_BASE_NUM 10
#define MULTI_QOS_ARG_NUMS 2
#define MAX_FLOW_QOS_ID_NUM 1024
#define BW_QOS_FLOW_UNIT "bandwidth(kbps)"
#define PPS_QOS_FLOW_UNIT "package rate(pps)"

struct mtr_profile
{
    uint16_t qos_id;
    uint32_t meter_id;
    int packet_mode;
};

struct mtr_info_node
{
    uint32_t mtr_id;
    struct mtr_profile info;
    struct hmap_node node;
};

struct hinic3_mtr_map
{
    struct hmap mtr_map;
    struct hinic3_spinlock lock;
};

struct alloc_qos_id
{
    uint8_t packet_mode;
    uint32_t meter_id[METER_UNIT_NUM];
    uint16_t qos_id;
    bool flags;
};

struct hinic3_qos_array
{
    struct alloc_qos_id qos_ids[MAX_FLOW_QOS_ID_NUM];
    struct hinic3_mutex mutex;
};

void hinic3_mtr_map_init(void);
struct mtr_info_node *hinic3_mtr_node_lookup(uint32_t ovs_meter_id);
void unixctl_meter_dfx_init(void);
void hinic3_qos_array_init(void);
int hinic3_set_flow_qos_to_hovs_sub(struct hinic3_meter_node *meter, bool is_clear, uint16_t *qos_id,
                                   uint16_t *qos_packet_mode);
int hinic3_offload_parse_qos_act_sub(struct hinic3_offload_action *offload_action,
                                      const struct rte_flow_action_meter *mtr, struct rte_flow *mega_flow);
struct hinic3_mtr_map *hinic3_get_meter_info(void);
struct hinic3_qos_array *hinic3_get_qos_ids(void);
#endif