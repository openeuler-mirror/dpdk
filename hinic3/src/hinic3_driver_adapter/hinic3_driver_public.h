 /* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_DRIVER_PUBLIC_H
#define HINIC3_DRIVER_PUBLIC_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>
#include <rte_atomic.h>
#include "rte_bus_vdev.h"
#include "hinic3_flow_agent_public.h"
#include "hinic3_nlattr.h"
#include "hinic3_message.h"
#include "hinic3_flexda_flow_public.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    HINIC3_MODE_VERSION_00,
    HINIC3_MODE_VERSION_01,
    HINIC3_MODE_VERSION_MAX
};

struct hiovs_error_code_map {
    enum hinic3_api_return_val hiovs_err_code;
    int errno_code;
};

typedef struct tag_hiovs_api_record {
    /* the max time that api executes */
    uint64_t max_api_time;
    /* the min time that api executes */
    uint64_t min_api_time;
    /* the total time that api executes, which allows integer inversion and will not affect the function */
    uint64_t total_api_time;
    /* the total count that api executes */
    rte_atomic64_t  call_total_count;
    /* the total count that api executes error */
    rte_atomic64_t  call_error_count;
    /* some api returns long long int, so use the max */
    long long int last_rtn_value;
    /* the time when error occurs */
    time_t time_error;
} hiovs_api_record;

struct hinic3_dpif_flow {
    /* Flow to Put */
    struct hinic3_nlattr_obj *key;
    /* length of key in bytes */
    size_t key_len;
    /* mask to put */
    struct hinic3_nlattr_obj *mask;
    /* length of mask in bytes */
    size_t mask_len;
    /* actions to perform on flow */
    struct hinic3_nlattr_obj *actions;
    size_t action_len;
    uint8_t mask_present;
    uint64_t hw_ufid;
    struct hinic3_flow_stats stats;
};

struct hinic3_dpif_flow_for_get {
    /* Flow to Put */
    struct hinic3_nlattr_obj *key;
    /* length of key in bytes */
    size_t key_len;
    /* mask to put */
    struct hinic3_nlattr_obj *mask;
    /* length of mask in bytes */
    size_t mask_len;
    /* actions to perform on flow */
    struct hinic3_nlattr_obj *actions;
    size_t action_len;
    uint8_t mask_present;
    uint64_t ol_ufid;
    uint64_t related_hw_ufid;
    struct hinic3_flow_stats stats;
};


struct hinic3_flow_del_buf {
    struct hinic3_dpif_flow_for_get get_flow;
    uint8_t key_buf[HINIC3_MSG_MAX_BUF];
    uint8_t actions_buf[HINIC3_MSG_MAX_BUF];
};

struct hw_flow_smac_element {
    uint8_t smac[ETH_ALEN];
    struct hmap_node node;
    struct hinic3_list hw_flow_list;
};

struct hw_element {
    /* hardware flow ufid. */
    struct rte_flow *hw_flow;
    uint64_t hw_ufid;
    uint64_t hw_flow_hash;
    /* record all mapped rte_flow. */
    struct hmap mega_ufid_related;
    struct hmap_node node;
    struct hinic3_list  list_node;
    struct hw_flow_smac_element *smac_ele;
    /* New variable should add here, cannot be added after p_extra */
    /* private info of hardware flow. */
    hinic3_u128 policy_id;
    char p_extra[0];
};

struct hinic3_flow_del_context {
    /* current num of hardware flow */
    size_t num_entries;
    struct hinic3_flow_del_context_per_table *tables[HINIC3_HYDRA_TYPE_TABLE_END];
};

struct hinic3_flow_del_context_per_table {
    uint32_t table_id;
    /* current num of hardware flow */
    size_t num_entries;
     /* per del ufid list */
    uint64_t ufids[HINIC3_MAX_ENTRY_PER_BATCH_DEL];
    /* per del flow info ptr */
    struct hinic3_dpif_flow_for_get *flows[HINIC3_MAX_ENTRY_PER_BATCH_DEL];
    /* per del flow info */
    struct hinic3_flow_del_buf buf[HINIC3_MAX_ENTRY_PER_BATCH_DEL];
    struct hw_element *hw_ele[HINIC3_MAX_ENTRY_PER_BATCH_DEL];
};

extern struct hiovs_error_code_map *error_code_map;
extern int array_size;

static inline int hinic3_convert_error_code(int error_code)
{
    for (int i = 0; i < array_size; i++) {
        if (error_code == error_code_map[i].hiovs_err_code) {
            return error_code_map[i].errno_code;
        }
    }
    return error_code;
}

typedef int (*hinic3_flow_callback_t)(uint64_t ufid, const struct hinic3_dpif_flow_for_get *flow,
    const struct hinic3_nlattr_obj *args, size_t args_len);
typedef int (*hinic3_flow_put_cb_t)(uint64_t ufid, const struct hinic3_nlattr_obj *args, size_t args_len);
typedef int (*hinic3_flow_age_cb_t)(uint64_t ufid, const struct hinic3_dpif_flow_for_get *flow,
    const struct hinic3_nlattr_obj *args, size_t args_len);
typedef int (*hinic3_flush_done_cb_t)(void);
struct hinic3_flow_del_context *hinic3_del_batch_context_alloc(void);
void hinic3_del_batch_context_free(struct hinic3_flow_del_context *batch);
void hinic3_del_hw_flows_batch(struct hinic3_flow_del_context *batch, struct hw_element *hw_ele);
const struct rte_vdev_driver* vpmd_vdev_driver_get(void);
void hinic3_convert_error_code_init(void);

#ifdef __cplusplus
}
#endif

#endif
