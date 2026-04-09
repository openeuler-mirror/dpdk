/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2024 Huawei Technologies Co., Ltd
 */
#ifndef HINIC3_QUERY_FLOW_H
#define HINIC3_QUERY_FLOW_H
#include "hinic3_capture_utils.h"
#include "rte_flow.h"

#define INPUT_KEY_MIN_NUM_7TUPLE 20
#define INPUT_KEY_MAX_NUM_7TUPLE 22
#define INPUT_KEY_MIN_NUM_9TUPLE 22
#define INPUT_KEY_MAX_NUM_9TUPLE 24
#define ARGC_ADD_DOUBLE 2
#define HINIC3_INPUT_ITEM_MAX_NUM 6
#define PCAP_MAX_PORT_ID 65535
#define QUERY_CMD_MIN_PARAM 1
#define QUERY_CMD_MAX_PARAM_7TUPLE 28
#define QUERY_CMD_MAX_PARAM_9TUPLE 30
#define HINIC3_ICMP_FLOW_MASK 8

typedef int (*query_key_func)(struct input_key *g_input_key, struct pcap_key_t *cap_key);
typedef int(*pattern_func)(struct rte_flow_item *, struct input_key *);

struct query_key_parse {
    const char *key_name;
    query_key_func func;
};


void unixctl_hinic3_query_cmd_init(void);

#endif
