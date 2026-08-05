 /* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_FLOW_INTERFACE_H
#define HINIC3_FLOW_INTERFACE_H

#include <stdint.h>
#include <stddef.h>
#include "rte_common.h"
#include "rte_pci.h"
#include "hinic3_util.h"
#include "hinic3_nlattr.h"
#include "hinic3_driver_public.h"
#include "hiovs_api.h"
#include "hinic3_message.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef DEBUG_MODE
#define HINIC3_DEBUG_PRINT_FLOW_INFO
#define HINIC3_DEBUG_PRINT_CT_OFFLOAD
#endif

/* in a byte:
 * bit 6-7: version
 * bit 3-5: forward mode
 * bit 0-2: permit mode
 */
#define HINIC3_PERMIT_MODE_OFFSET 0U
#define HINIC3_FORWARD_MODE_OFFSET 3U
#define HINIC3_MODE_VERSION_OFFSET 6U
#define HINIC3_DEFAULT_API_RECORD_INDEX 0
#define HINIC3_RTE_ETH_TYPE_MAX 2


typedef enum tag_hinic3_flow_api {
    HINIC3_FLOW_AGENT_PUT = 0,
    HINIC3_FLOW_AGENT_GET_BY_KEY,
    HINIC3_FLOW_AGENT_GET_BY_UFID,
    HINIC3_FLOW_AGENT_DEL_BY_UFID,
    HINIC3_FLOW_AGENT_DEL_BATCH,
    HINIC3_FLOW_AGENT_FLUSH,
    HINIC3_FLOW_AGENT_DUMP_START,
    HINIC3_FLOW_AGENT_DUMP_NEXT,
    HINIC3_FLOW_AGENT_DUMP_DONE,
    HINIC3_FLOW_AGENT_GET_MAXFLOWS,
    HINIC3_FLOW_AGENT_GET_MEGA_MAXFLOWS,
    HINIC3_FLOW_AGENT_GET_CAPABILITY,
    HINIC3_FLOW_AGENT_SET_FORWARD_MODE,
    HINIC3_FLOW_AGENT_GET_FORWARD_MODE,
    HINIC3_STATISTICS_FLOW_GET_BY_UFID,
    HINIC3_STATISTICS_FLOW_FLUSH_BY_UFID,
    HINIC3_ACL_INDIR_COUNTER_ALLOC,
    HINIC3_ACL_INDIR_COUNTER_GET,
    HINIC3_ACL_INDIR_COUNTER_FREE,
    HINIC3_ACL_INDIR_COUNTER_RESET,
    HINIC3_FLOW_AGENT_ACL_PUT,
    HINIC3_FLOW_AGENT_ACL_DEL,
    HINIC3_FLOW_AGENT_ACL_GET_STATS,
    HINIC3_FLOW_AGENT_ACL_FLUSH,
    HINIC3_FLOW_AGENT_ACL_DUMP_START,
    HINIC3_FLOW_AGENT_ACL_DUMP_NEXT,
    HINIC3_FLOW_AGENT_ACL_DUMP_DONE,
    HINIC3_FLOW_AGENT_DPHASH_PUT,
    HINIC3_FLOW_AGENT_DPHASH_DEL,
    HINIC3_FLOW_AGENT_DPHASH_GET_STATS,
    HINIC3_FLOW_AGENT_DPHASH_FLUSH,
    HINIC3_FLOW_AGENT_DPHASH_DUMP_START,
    HINIC3_FLOW_AGENT_DPHASH_DUMP_NEXT,
    HINIC3_FLOW_AGENT_DPHASH_DUMP_DONE,
    HINIC3_RTE_FLOW_CREATE,
    HINIC3_RTE_FLOW_QUERY,
    HINIC3_RTE_FLOW_DELETE,
    HINIC3_RTE_FLOW_FLUAH_ALL,
    HINIC3_RTE_FLOW_DESTROY_BY_BATCH,
    HINIC3_RTE_FLOW_DUMP_START,
    HINIC3_RTE_FLOW_DUMP_NEXT,
    HINIC3_RTE_FLOW_DUMP_END,
    HINIC3_RTE_FLOW_HANDLE_CREATE,
    HINIC3_RTE_FLOW_HANDLE_QUERY,
    HINIC3_RTE_FLOW_HANDLE_DELETE,
    HINIC3_FLOW_AGENT_MEGA_PUT,
    HINIC3_FLOW_AGENT_MEGA_DEL_BY_UFID,
    HINIC3_FLOW_AGENT_MEGA_FLUSH,
    HINIC3_FLOW_AGENT_MEGA_DUMP_START,
    HINIC3_FLOW_AGENT_MEGA_DUMP_NEXT,
    HINIC3_FLOW_AGENT_MEGA_DUMP_DONE,
    HINIC3_FLOW_AGENT_MEGA_GET_STATS,
    HINIC3_FLOW_AGENT_MEGA_FORWARD,
    HINIC3_FLOW_BLOCK_SIZE_GET,
    HINIC3_FLOW_MODIFY,
    HINIC3_FLOW_BLOCK_VERSION_SET,
    HINIC3_FLOW_BLOCK_VERSION_GET,
    HINIC3_FLOW_AGENT_PUT_DP_HASH_CALLBACK,
    HINIC3_FLOW_AGENT_MODIFY_FLOW_CALLBACK,
    HINIC3_FLOW_AGENT_PUT_FLOW_CALLBACK,

    HINIC3_FLEXDA_FLOW_AGENT_PUT,
    HINIC3_FLEXDA_FLOW_AGENT_GET_BY_UFID,
    HINIC3_FLEXDA_FLOW_AGENT_DEL_BY_UFID,
    HINIC3_FLEXDA_FLOW_AGENT_DEL_BATCH,
    HINIC3_FLEXDA_FLOW_AGENT_FLUSH,
    HINIC3_FLEXDA_FLOW_AGENT_DUMP_START,
    HINIC3_FLEXDA_FLOW_AGENT_DUMP_NEXT,
    HINIC3_FLEXDA_FLOW_AGENT_DUMP_DONE,
    HINIC3_FLEXDA_FLOW_AGENT_GET_MAXFLOWS,
    HINIC3_FLEXDA_STATISTICS_FLOW_GET_BY_UFID,
    HINIC3_FLEXDA_GET_CONFIG_INFO,
    HINIC3_FLEXDA_FREE_CONFIG_INFO,
    HINIC3_FLEXDA_MML_LIB,
    
    HINIC3_FLOW_ACL_INDIR_COUNTER_ALLOC,
    HINIC3_FLOW_ACL_INDIR_COUNTER_GET,
    HINIC3_FLOW_ACL_INDIR_COUNTER_FREE,
    HINIC3_FLOW_ACL_INDIR_COUNTER_RESET,
    HINIC3_FLOW_ACL_MGMT_PUT,
    HINIC3_FLOW_ACL_MGMT_DEL,
    HINIC3_FLOW_ACL_GET_STATS,
    HINIC3_FLOW_ACL_MGMT_FLUSH,
    HINIC3_FLOW_ACL_MGMT_DUMP_START,
    HINIC3_FLOW_ACL_MGMT_DUMP_NEXT,
    HINIC3_FLOW_ACL_MGMT_DUMP_DONE,

    HINIC3_FLOW_DP_HASH_MGMT_PUT,
    HINIC3_FLOW_DP_HASH_MGMT_DEL_BY_KEY,
    HINIC3_FLOW_DP_HASH_MGMT_GET_BY_KEY,
    HINIC3_FLOW_DP_HASH_MGMT_DUMP_START,
    HINIC3_FLOW_DP_HASH_MGMT_DUMP_NEXT,
    HINIC3_FLOW_DP_HASH_MGMT_DUMP_DONE,
    HINIC3_FLOW_DP_HASH_MGMT_FLUSH,
    HINIC3_FLOW_API_MAX,
} hinic3_flow_api;

struct hinic3_high_priority_cfgs {
    uint8_t cfgs_len;
    struct hovs_high_priority_protocol_cfg hovs_cfgs[HINIC3_RTE_ETH_TYPE_MAX];
};

int hinic3_flow_class_init(void);
void hinic3_flow_class_uninit(void);
int hinic3_flow_init(void);
int hinic3_flow_get_api_record(const char *api_name, bool is_all, bool is_clear, hiovs_api_record *records,
    int record_len);
const char *hinic3_flow_get_api_name(hinic3_flow_api api_index);
int hinic3_statistics_flow_get_by_ufid(const uint64_t *ufid, const size_t cnt, struct hinic3_flow_stats *stats, uint8_t table_id);
int hinic3_flow_get_forward_mode(uint8_t *forward_mode);
int hinic3_flow_set_forward_mode(uint8_t forward_mode);
int hinic3_flow_get_maxflows(uint32_t table_id, uint32_t *max_flows);
int hinic3_flow_get_maxflows_by_table_id(uint32_t table_id, uint32_t *max_flows);
int hinic3_get_mega_get_flow_num(uint32_t *flow_num, uint32_t *max_flow_num);
int hinic3_flow_dump_done(void *state);
int hinic3_flow_dump_next(void *state, struct hinic3_dpif_flow_for_get *dump);
int hinic3_flow_dump_start(void **state);
int hinic3_flow_dump_start_by_table_id(uint32_t table_id, void **state);
int hinic3_flow_del_batch(const uint32_t table_id, const uint64_t *ufids, struct hinic3_dpif_flow_for_get **flows, const size_t cnt);
int hinic3_flow_del_by_ufid(uint64_t ufid, uint8_t table_id);
int hinic3_flow_get_by_ufid(uint64_t ufid, struct hinic3_dpif_flow_for_get *get, uint8_t table_id);
int hinic3_flow_put(const struct hinic3_dpif_flow *put, const struct hinic3_nlattr *args, size_t args_len, uint8_t table_id);
int hinic3_flow_mgmt_get_by_key(const struct hinic3_nlattr *key, size_t key_len, struct hinic3_dpif_flow *get,
    uint64_t *g_get_flow_related_ufid);
int hinic3_flow_get_capability(struct hinic3_flow_capability *cap);
int hinic3_flow_flush(void);
uint32_t hinic3_get_offload_thread_num(void);
int hinic3_iface_global_cfg_set(struct hiovs_mirror_session_info *hiovs_session_info,
    enum hinic3_global_cfg_arg_type type);
int hinic3_iface_high_priority_upcall_set(const uint16_t port_id, struct hinic3_high_priority_cfgs *cfgs);
uint32_t hinic3_get_offload_thread_num(void);
hinic3_flow_api hinic3_flow_get_api_index(const char *api_name);
int hinic3_process_dpif_flow_for_get_data(
    struct hovs_dpif_flow_for_get dump_for_get, struct hinic3_dpif_flow_for_get *dump);
int hinic3_mega_flow_put(const struct hinic3_dpif_flow *put, uint32_t index, uint64_t *ufid);
int hinic3_mega_flow_del_by_ufid(uint64_t ufid);
int hinic3_mega_flow_dump_start(void **state);
int hinic3_mega_flow_dump_next(void *state, struct hinic3_dpif_flow_for_get *dump);
int hinic3_mega_flow_dump_done(void *state);
int hinic3_mega_flow_flush(void);
int hinic3_mega_flow_set_l3_forward(bool flag);
int hinic3_statistics_mega_flow_get_by_ufid(const uint64_t ufid, struct hinic3_flow_stats *stats);
int hinic3_flow_modify(const struct hinic3_dpif_flow *put, const struct hinic3_nlattr *args, size_t args_len);
int hinic3_flow_get_block_table_size(uint32_t *block_num);
void hinic3_flow_set_block_version(uint32_t block_num, uint16_t block_id[], uint16_t block_version[], int result[]);
int hinic3_flow_get_block_version(uint32_t block_num, uint16_t block_id[], uint16_t block_version[]);
int hinic3_flexda_get_flow_cfg_info(hovs_flexda_config_info_t *hovs_flexda_config);
void hinic3_flexda_free_flow_cfg_info(hovs_flexda_config_info_t *hovs_flexda_config);
#ifdef __cplusplus
}
#endif

#endif
