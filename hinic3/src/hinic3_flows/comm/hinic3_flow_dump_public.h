/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Huawei Technologies Co., Ltd
 */
#ifndef HINIC3_FLOW_DUMP_PUBLIC_H
#define HINIC3_FLOW_DUMP_PUBLIC_H

#include "rte_flow.h"
#include "hiovs_acl_api.h"
#include "hinic3_mutex.h"
#include "hinic3_flow_format.h"

#define HINIC3_FLOW_DUMP_MAX_PATTERN HINIC3_FLOW_ITEMS_NUM
#define HINIC3_FLOW_DUMP_MAX_ACTION HINIC3_FLOW_ACTIONS_NUM
#define HINIC3_DFX_FLOW_ALLOC_MEM_BASE_SIZE   2048

typedef struct hinic3_flow_act_vxlan_gpe_header hinic3_act_vxlan_header;

struct hinic3_dump_flow_info {
    struct rte_flow_item items[HINIC3_FLOW_DUMP_MAX_PATTERN];
    unsigned int useful_item_index;
    struct rte_flow_action actions[HINIC3_FLOW_DUMP_MAX_ACTION];
    unsigned int useful_action_index;
    uint64_t hw_ufid;
};

struct hinic3_dump_flow_mem {
    unsigned int max_flow_num;
    unsigned int cur_flow_num;
    long long int start_timestamp;
    struct hinic3_dump_flow_info *flows;
};

enum hinic3_dump_context_status {
    HINIC3_FLOW_DUMPING_READY,
    HINIC3_FLOW_DUMPING,
    HINIC3_FLOW_DUMPING_DONE
};

struct hinic3_flow_dump_context {
    union {
        void *hiovs_state;
    };
    uint32_t type;
    struct hinic3_dump_flow_mem context_mem;
    pthread_t thread_id;
    enum hinic3_dump_context_status dumping;
};

struct hinic3_dump_act_data {
    bool is_valid;
    void *action;
};

struct hinic3_dump_item_data {
    bool is_valid;
    void *key;
    void *mask;
};

enum hinic3_flow_type {
    HINIC3_FLOW_TYPE_EMC,
    HINIC3_FLOW_TYPE_MEGA,
    HINIC3_FLOW_TYPE_DPHASH,
    HINIC3_FLOW_TYPE_ACL,
    HINIC3_FLOW_TYPE_MAX
};

enum hinic3_dump_data_type {
    HINIC3_DUMP_DATA_TYPE_KEY,
    HINIC3_DUMP_DATA_TYPE_MASK,
    HINIC3_DUMP_DATA_TYPE_ACT
};

typedef int (*dump_start)(struct hinic3_flow_dump_context *context, struct rte_flow_error *error);
typedef int (*dump_next)(struct hinic3_flow_dump_context* context, int count,
    struct rte_flow_error *error);
typedef int (*dump_done)(struct hinic3_flow_dump_context *context, struct rte_flow_error *error);

int hinic3_build_item_data(struct hinic3_dump_item_data *data,
    enum hinic3_dump_data_type type, size_t size);

void hinic3_clean_items_data(struct hinic3_dump_item_data items[], uint32_t len);

void *hinic3_get_item_data(struct hinic3_dump_item_data *item,
    enum hinic3_dump_data_type type);

int hinic3_set_dump_start(enum hinic3_flow_type type, dump_start func);

int hinic3_set_dump_next(enum hinic3_flow_type type, dump_next func);

int hinic3_set_dump_done(enum hinic3_flow_type type, dump_done func);

dump_start hinic3_get_dump_start(enum hinic3_flow_type type);

dump_next hinic3_get_dump_next(enum hinic3_flow_type type);

dump_done hinic3_get_dump_done(enum hinic3_flow_type type);
#endif
