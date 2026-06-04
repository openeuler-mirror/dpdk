/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2025 Huawei Technologies Co., Ltd
 */
#ifndef HINIC3_FLEXDA_FLOW_PUBLIC_H
#define HINIC3_FLEXDA_FLOW_PUBLIC_H
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "hinic3_drv.h"
#include "hinic3_parse_agent_config.h"

#define HINIC3_HYDRA_TYPE_TABLE_NONE 0x0
#define HINIC3_HYDRA_TYPE_TABLE_START 0x1
#define HINIC3_HYDRA_TYPE_TABLE_END 0x10
#define HINIC3_HYDRA_TYPE_ACTION_START 0x1001
#define HINIC3_HYDRA_TYPE_ACTION_END 0x2000
#define HINIC3_HYDRA_TYPE_KEY_START 0x2001
#define HINIC3_HYDRA_TYPE_KEY_END 0x3000
#define HINIC3_HYDRA_TYPE_DUMP_FORMAT_END  0x5

#define HINIC3_HYDRA_NAME_MAX_LENGTH 64
#define HINIC3_HYDRA_KEY_MAX_NUM     128
#define HINIC3_HYDRA_ALLOC_SIZE_MAX  1024
#define HINIC3_HYDRA_TYPE_DUMP_FORMAT_END  0x5

#define HINIC3_HYDRA_TYPE_TABLE_MAX (HINIC3_HYDRA_TYPE_TABLE_END - HINIC3_HYDRA_TYPE_TABLE_START + 1)
#define HINIC3_HYDRA_TYPE_KEY_MAX (HINIC3_HYDRA_TYPE_KEY_END + 1)
#define HINIC3_HYDRA_TYPE_ACTION_MAX (HINIC3_HYDRA_TYPE_ACTION_END +1)
#define IS_FLEXDA_FUZZY_TABLE(table_id) \
    ((hinic3_check_fuzzy_flow_flexda_switch()) && \
    ((hinic3_flexda_flow_get_table_type(table_id) == HOVS_FLEXDA_FLOWTABLE_TYPE_PRE_FUZZY_TABLE) || \
    (hinic3_flexda_flow_get_table_type(table_id) == HOVS_FLEXDA_FLOWTABLE_TYPE_POST_FUZZY_TABLE)))

enum hovs_flexda_dump_format_type {
    HOVS_FLEXDA_DUMP_FORMAT_HEXBYTE, /* 16进制展示 */
    HOVS_FLEXDA_DUMP_FORMAT_MAC,     /* mac地址展示 */
    HOVS_FLEXDA_DUMP_FORMAT_IPV4,    /* ipv4地址展示 */
    HOVS_FLEXDA_DUMP_FORMAT_IPV6,    /* ipv6地址展示 */
    HOVS_FLEXDA_DUMP_FORMAT_UNIT,    /* 10进制展示 */
};

typedef enum tag_hiovs_hydra_type {
    HIOVS_HYDRA_TYPE_KEY = 0,
    HIOVS_HYDRA_TYPE_ACTION,
    HIOVS_HYDRA_TYPE_TABLE,
} hinic3_hydra_type;

struct hydra_flow_action {
    uint32_t action_type;
    void *action_data;
    size_t action_data_size;
};

struct hydra_flow_item {
    uint32_t item_type;
    void *item_data;
    size_t item_data_size;
    struct hydra_flow_item *next;
};

enum hydra_action_type {
    HYDRA_ACTION_INVALID = 0x1000,
    HYDRA_ACTION_GENEVE_TUNNEL_POP,
    HYDRA_ACTION_GENEVE_TUNNEL_PUSH,
    HYDRA_ACTION_OVS_ACTION_MOD_TTL,
};

enum hydra_item_type {
    HYDRA_ITEM_INVALID = 0x2000,
    HYDRA_ITEM_MYMAINPIPE_GENEVE_EXACT_OPTION_CLASS,
    HYDRA_ITEM_MYMAINPIPE_GENEVE_EXACT_TYPE,
    HYDRA_ITEM_MYMAINPIPE_GENEVE_EXACT_LENGTH,
    HYDRA_ITEM_MYMAINPIPE_GENEVE_EXACT_VALUE,
    HYDRA_ITEM_MYMAINPIPE_GENEVE_EXACT_VNI,
    HYDRA_ITEM_MYMAINPIPE_GENEVE_EXACT_FLAGS,
    HYDRA_ITEM_MYMAINPIPE_GENEVE_EXACT_DST_PORT,
    HYDRA_ITEM_MYMAINPIPE_GENEVE_EXACT_INNER_ETH,
    HYDRA_ITEM_MYMAINPIPE_GENEVE_EXACT_SIP,
    HYDRA_ITEM_MYMAINPIPE_GENEVE_EXACT_DIP,
};

static inline int hinic3_flexda_flow_check_table_id_valid(uint32_t table_id)
{
    if (table_id < HINIC3_HYDRA_TYPE_TABLE_START || table_id > HINIC3_HYDRA_TYPE_TABLE_END) {
        return -1;
    }
    return 0;
}

static inline int hinic3_flexda_flow_check_key_type_valid(uint16_t key_type)
{
    if (key_type > HINIC3_HYDRA_TYPE_KEY_END) {
        return -1;
    }
    return 0;
}

static inline int hinic3_flexda_flow_check_action_type_valid(uint16_t action_type)
{
    if (action_type > HINIC3_HYDRA_TYPE_ACTION_END) {
        return -1;
    }
    return 0;
}

static inline int hinic3_flexda_flow_get_table_index(uint32_t table_id)
{
    return (int)(table_id - HINIC3_HYDRA_TYPE_TABLE_START);
}

static inline int hinic3_flexda_flow_check_dump_format_type_valid(uint16_t dump_format_type)
{
    if (dump_format_type > HINIC3_HYDRA_TYPE_DUMP_FORMAT_END) {
        return -1;
    }
    return 0;
}

int hinic3_flexda_flow_parse_config_info(hovs_flexda_config_info_t *hovs_flexda_config);
const char *hinic3_flexda_flow_get_hydra_name(hinic3_hydra_type hydra_type, uint32_t value);
bool hinic3_flexda_flow_is_in_main_table(uint32_t table_id);
int hinic3_flexda_flow_get_table_flow_num(uint32_t table_id, uint32_t *table_flow_num);
int hinic3_flexda_flow_get_table_type(uint32_t table_id);
uint32_t hinic3_flexda_flow_get_total_flow_num(void);
int hinic3_flexda_flow_get_table_num(void);
const struct hinic3_flexda_config_info_t* hinic3_flexda_get_flow_config(void);
void hinic3_flexda_free_flow_config(void);
bool hinic3_flexda_flow_action_is_in_table(uint32_t action_type);
const hovs_flexda_config_dump_info_t* hinic3_flexda_flow_get_hydra_config_dump_info(hinic3_hydra_type hydra_type,
    uint32_t value);
bool hinic3_flexda_flow_dump_format_size_check(uint16_t size, uint16_t dump_format);
bool hinic3_flexda_flow_key_is_in_table(uint32_t key_type);
bool hinic3_is_flexda_fuzzy_flow_table(uint32_t table_id);

int32_t hovs_flexda_get_config_info(hovs_flexda_config_info_t *config_info);
void hovs_flexda_free_config_info(hovs_flexda_config_info_t *config_info);
uint16_t hovs_flexda_rte_tx_burst(uint16_t dpdk_port_id, uint16_t dpdk_queue_id, void **tx_pkts, uint16_t nb_pkts);
int hovs_flexda_flow_mgmt_put(const struct hovs_flexda_dpif_flow *put, const struct nlattr *args, size_t args_len);
int hovs_flexda_flow_mgmt_get_by_ufid(uint32_t table_id, uint64_t ufid, struct hovs_flexda_dpif_flow_for_get *get);
int hovs_flexda_flow_mgmt_del_by_ufid(uint32_t table_id, uint64_t ufid);
int hovs_flexda_flow_mgmt_del_by_batch(uint32_t table_id, const uint64_t *ufids, struct hovs_dpif_flow_for_get **flows, const size_t cnt);
int hovs_flexda_flow_mgmt_flush(void);
int hovs_flexda_flow_mgmt_dump_start(uint32_t table_id, void **state);
int hovs_flexda_flow_mgmt_dump_next(void *state, struct hovs_dpif_flow_for_get *dump);
int hovs_flexda_flow_mgmt_dump_done(void *state);
int hovs_flexda_flow_mgmt_get_maxflows(uint32_t table_id, uint32_t *max_flows);
int hovs_flexda_statistics_flow_get_by_ufid(uint32_t table_id, const uint64_t *ufid, const size_t cnt, struct hovs_flow_stats *stats);
int hovs_flexda_global_cfg_set(const struct nlattr *args, size_t args_len, struct nlattr *unset_args,
    size_t *unset_args_len);
int32_t hovs_flexda_lib_init(void *arg);
int hovs_flexda_lib_deinit(void *arg);
int hovs_flexda_mml_lib(const char *buf_in, uint32_t in_size, char *buf_out, uint32_t *out_len, uint32_t max_buf_out_len);


#endif