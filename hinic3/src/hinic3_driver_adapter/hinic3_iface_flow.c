/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */

#include "hinic3_iface_flow.h"
#include <string.h>
#include <stdbool.h>
#include "rte_cycles.h"
#include "hinic3_init.h"
#include "hinic3_message.h"
#include "hinic3_log.h"
#include "hinic3_flow_agent.h"
#include "hinic3_flow_agent_public.h"
#include "hinic3_iface_port.h"
#include "hinic3_driver_public.h"
#include "hinic3_iface_flow_api_record.h"
#include "hinic3_parse_agent_config.h"

#define NO_HINIC3_FLOWS_ERR (-4)
#define GLOBAL_CFG_ARGS_SIZE (128)

struct hiovs_flow_map
{
    hinic3_flow_api api_index;
    const char *api_name;
};

static const struct hiovs_flow_map g_flow_api_arr[HINIC3_FLOW_API_MAX] = {
    {HINIC3_FLOW_AGENT_PUT, "hovs_flow_mgmt_put"},
    {HINIC3_FLOW_AGENT_GET_BY_KEY, "hovs_flow_mgmt_get_by_key"},
    {HINIC3_FLOW_AGENT_GET_BY_UFID, "hovs_flow_mgmt_get_by_ufid"},
    {HINIC3_FLOW_AGENT_DEL_BY_UFID, "hovs_flow_mgmt_del_by_ufid"},
    {HINIC3_FLOW_AGENT_DEL_BATCH, "hovs_flow_mgmt_del_by_batch"},
    {HINIC3_FLOW_AGENT_FLUSH, "hovs_flow_mgmt_flush"},
    {HINIC3_FLOW_AGENT_DUMP_START, "hovs_flow_mgmt_dump_start"},
    {HINIC3_FLOW_AGENT_DUMP_NEXT, "hovs_flow_mgmt_dump_next"},
    {HINIC3_FLOW_AGENT_DUMP_DONE, "hovs_flow_mgmt_dump_done"},
    {HINIC3_FLOW_AGENT_GET_MAXFLOWS, "hovs_flow_mgmt_get_maxflows"},
    {HINIC3_FLOW_AGENT_GET_CAPABILITY, "hovs_flow_mgmt_get_capability"},
    {HINIC3_FLOW_AGENT_SET_FORWARD_MODE, "hovs_flow_mgmt_set_forward_mode"},
    {HINIC3_FLOW_AGENT_GET_FORWARD_MODE, "hovs_flow_mgmt_get_forward_mode"},
    {HINIC3_STATISTICS_FLOW_GET_BY_UFID, "hovs_statistics_flow_get_by_ufid"},
    {HINIC3_STATISTICS_FLOW_FLUSH_BY_UFID, "hovs_statistics_flow_flush_by_ufid"},
    {HINIC3_ACL_INDIR_COUNTER_ALLOC,        "hovs_acl_indir_counter_alloc" },
    {HINIC3_ACL_INDIR_COUNTER_GET,          "hovs_acl_indir_counter_get" },
    {HINIC3_ACL_INDIR_COUNTER_FREE,         "hovs_acl_indir_counter_free" },
    {HINIC3_ACL_INDIR_COUNTER_RESET,        "hovs_acl_indir_counter_reset" },
    {HINIC3_FLOW_AGENT_ACL_PUT,             "hovs_acl_mgmt_put"},
    {HINIC3_FLOW_AGENT_ACL_DEL,             "hovs_acl_mgmt_del"},
    {HINIC3_FLOW_AGENT_ACL_GET_STATS,       "hovs_acl_get_stats"},
    {HINIC3_FLOW_AGENT_ACL_FLUSH,           "hovs_acl_mgmt_flush"},
    {HINIC3_FLOW_AGENT_ACL_DUMP_START,      "hovs_acl_mgmt_dump_start"},
    {HINIC3_FLOW_AGENT_ACL_DUMP_NEXT,       "hovs_acl_mgmt_dump_next"},
    {HINIC3_FLOW_AGENT_ACL_DUMP_DONE,       "hovs_acl_mgmt_dump_done"},
    {HINIC3_FLOW_AGENT_DPHASH_PUT,          "hovs_dp_hash_mgmt_put"},
    {HINIC3_FLOW_AGENT_DPHASH_DEL,          "hovs_dp_hash_mgmt_del_by_key"},
    {HINIC3_FLOW_AGENT_DPHASH_GET_STATS,    "hovs_dp_hash_mgmt_get_by_key"},
    {HINIC3_FLOW_AGENT_DPHASH_FLUSH,        "hovs_dp_hash_mgmt_flush"},
    {HINIC3_FLOW_AGENT_DPHASH_DUMP_START,   "hovs_dp_hash_mgmt_dump_start"},
    {HINIC3_FLOW_AGENT_DPHASH_DUMP_NEXT,    "hovs_dp_hash_mgmt_dump_next"},
    {HINIC3_FLOW_AGENT_DPHASH_DUMP_DONE,    "hovs_dp_hash_mgmt_dump_done"},
    {HINIC3_RTE_FLOW_CREATE, "hinic3_rte_flow_create"},
    {HINIC3_RTE_FLOW_QUERY, "hinic3_rte_flow_query"},
    {HINIC3_RTE_FLOW_DELETE, "hinic3_rte_flow_delete"},
    {HINIC3_RTE_FLOW_FLUAH_ALL, "hinic3_rte_flow_flush_all"},
    {HINIC3_RTE_FLOW_DESTROY_BY_BATCH, "hinic3_rte_flow_destroy_by_batch"},
    {HINIC3_RTE_FLOW_DUMP_START, "hinic3_rte_flow_dump_start"},
    {HINIC3_RTE_FLOW_DUMP_NEXT, "hinic3_rte_flow_dump_next"},
    {HINIC3_RTE_FLOW_DUMP_END, "hinic3_rte_flow_dump_end"},
    {HINIC3_RTE_FLOW_HANDLE_CREATE,         "hwoff_rte_flow_handle_create"},
    {HINIC3_RTE_FLOW_HANDLE_QUERY,          "hwoff_rte_flow_handle_query"},
    {HINIC3_RTE_FLOW_HANDLE_DELETE,         "hwoff_rte_flow_handle_delete"},
    {HINIC3_FLOW_AGENT_MEGA_PUT, "hovs_mega_flow_mgmt_put"},
    {HINIC3_FLOW_AGENT_MEGA_DEL_BY_UFID, "hovs_mega_flow_mgmt_del_by_ufid"},
    {HINIC3_FLOW_AGENT_MEGA_FLUSH, "hovs_mega_flow_mgmt_flush"},
    {HINIC3_FLOW_AGENT_MEGA_DUMP_START, "hovs_mega_flow_mgmt_dump_start"},
    {HINIC3_FLOW_AGENT_MEGA_DUMP_NEXT, "hovs_mega_flow_mgmt_dump_next"},
    {HINIC3_FLOW_AGENT_MEGA_DUMP_DONE, "hovs_mega_flow_mgmt_dump_done"},
    {HINIC3_FLOW_AGENT_MEGA_GET_STATS, "hovs_statistics_mega_flow_get_by_ufid"},
    {HINIC3_FLOW_AGENT_MEGA_FORWARD, "hovs_mega_flow_set_l3_forward"},
    {HINIC3_FLOW_BLOCK_SIZE_GET, "hovs_flow_mgmt_get_block_table_size"},
    {HINIC3_FLOW_MODIFY, "hovs_flow_mgmt_update"},
    {HINIC3_FLOW_BLOCK_VERSION_SET, "hovs_flow_mgmt_set_block_version"},
    {HINIC3_FLOW_BLOCK_VERSION_GET, "hovs_flow_mgmt_get_block_version"},
    {HINIC3_FLOW_AGENT_PUT_DP_HASH_CALLBACK, "hwoff_flow_agent_put_dp_hash_callback"},
    {HINIC3_FLOW_AGENT_MODIFY_FLOW_CALLBACK, "hinic3_flow_agent_modify_flow_callback"},
    {HINIC3_FLOW_AGENT_PUT_FLOW_CALLBACK, "hinic3_flow_agent_put_flow_callback"},

    { HINIC3_FLEXDA_FLOW_AGENT_PUT,                 "hovs_flexda_flow_mgmt_put" },
    { HINIC3_FLEXDA_FLOW_AGENT_GET_BY_UFID,         "hovs_flexda_flow_mgmt_get_by_ufid" },
    { HINIC3_FLEXDA_FLOW_AGENT_DEL_BY_UFID,         "hovs_flexda_flow_mgmt_del_by_ufid" },
    { HINIC3_FLEXDA_FLOW_AGENT_DEL_BATCH,           "hovs_flexda_flow_mgmt_del_by_batch" },
    { HINIC3_FLEXDA_FLOW_AGENT_FLUSH,               "hovs_flexda_flow_mgmt_flush" },
    { HINIC3_FLEXDA_FLOW_AGENT_DUMP_START,          "hovs_flexda_flow_mgmt_dump_start" },
    { HINIC3_FLEXDA_FLOW_AGENT_DUMP_NEXT,           "hovs_flexda_flow_mgmt_dump_next" },
    { HINIC3_FLEXDA_FLOW_AGENT_DUMP_DONE,           "hovs_flexda_flow_mgmt_dump_done" },
    { HINIC3_FLEXDA_FLOW_AGENT_GET_MAXFLOWS,        "hovs_flexda_flow_mgmt_get_maxflows" },
    { HINIC3_FLEXDA_STATISTICS_FLOW_GET_BY_UFID,    "hovs_flexda_statistics_flow_get_by_ufid" },
    { HINIC3_FLEXDA_GET_CONFIG_INFO,    "hovs_flexda_get_config_info" },
    { HINIC3_FLEXDA_FREE_CONFIG_INFO,    "hovs_flexda_free_config_info" },
    { HINIC3_FLEXDA_MML_LIB,    "hovs_flexda_mml_lib" },

    {HINIC3_FLOW_ACL_INDIR_COUNTER_ALLOC, "hovs_acl_indir_counter_alloc"},
    {HINIC3_FLOW_ACL_INDIR_COUNTER_GET, "hovs_acl_indir_counter_get"},
    {HINIC3_FLOW_ACL_INDIR_COUNTER_FREE, "hovs_acl_indir_counter_free"},
    {HINIC3_FLOW_ACL_INDIR_COUNTER_RESET, "hovs_acl_indir_counter_reset"},
    {HINIC3_FLOW_ACL_MGMT_PUT, "hovs_acl_mgmt_put"},
    {HINIC3_FLOW_ACL_MGMT_DEL, "hovs_acl_mgmt_del"},
    {HINIC3_FLOW_ACL_GET_STATS, "hovs_acl_get_stats"},
    {HINIC3_FLOW_ACL_MGMT_FLUSH, "hovs_acl_mgmt_flush"},
    {HINIC3_FLOW_ACL_MGMT_DUMP_START, "hovs_acl_mgmt_dump_start"},
    {HINIC3_FLOW_ACL_MGMT_DUMP_NEXT, "hovs_acl_mgmt_dump_next"},
    {HINIC3_FLOW_ACL_MGMT_DUMP_DONE, "hovs_acl_mgmt_dump_done"},

    {HINIC3_FLOW_DP_HASH_MGMT_PUT, "hovs_dp_hash_mgmt_put"},
    {HINIC3_FLOW_DP_HASH_MGMT_DEL_BY_KEY, "hovs_dp_hash_mgmt_del_by_key"},
    {HINIC3_FLOW_DP_HASH_MGMT_GET_BY_KEY, "hovs_dp_hash_mgmt_get_by_key"},
    {HINIC3_FLOW_DP_HASH_MGMT_DUMP_START, "hovs_dp_hash_mgmt_dump_start"},
    {HINIC3_FLOW_DP_HASH_MGMT_DUMP_NEXT, "hovs_dp_hash_mgmt_dump_next"},
    {HINIC3_FLOW_DP_HASH_MGMT_DUMP_DONE, "hovs_dp_hash_mgmt_dump_done"},
    {HINIC3_FLOW_DP_HASH_MGMT_FLUSH, "hovs_dp_hash_mgmt_flush"},
};

static pthread_mutex_t g_hinic3_flow_mutex = PTHREAD_MUTEX_INITIALIZER;

int hinic3_flow_get_capability(struct hinic3_flow_capability *cap)
{
    struct hinic3_drv_ops *ops = NULL;
    struct hovs_flow_capability hovs_cap;
    int ret;

    if (cap == NULL)
    {
        HINIC3_LOG(WARNING, DRIVER, "The hinic3 get capability parameter is NULL!");
        return -EINVAL;
    }
    ops = hinic3_get_drv_ops();
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_flow_mgmt_get_capability, HINIC3_DRV_FUNC_NO_PTR);

    memset(&hovs_cap, 0x0, sizeof(hovs_cap));

    HINIC3_LOG_HINIC_FLOW_API_LOG_WARN(ret, HINIC3_FLOW_AGENT_GET_CAPABILITY,
                                       ops->hovs_flow_mgmt_get_capability(&hovs_cap));

    if (ret != 0)
    {
        HINIC3_LOG(WARNING, DRIVER, "The hinic3 get capability parameter failed, error is %d!", ret);
        return hinic3_convert_error_code(ret);
    }

    cap->vendor_id = hovs_cap.vendor_id;
    cap->supported_dev_types = hovs_cap.supported_dev_types | (1 << PORT_TYPE_VIRTIO_VF) | (1 << PORT_TYPE_HWBOND);
    cap->supported_actions = hovs_cap.supported_actions;
    cap->key_format_type = hovs_cap.key_format_type;
    cap->cleanup_max_level = hovs_cap.cleanup_max_level;
    cap->flags = hovs_cap.flags;
    cap->max_flow_size = hovs_cap.max_flow_size;
    cap->rx_thread_num = hovs_cap.rx_thread_num;
    return ret;
}

int hinic3_flow_put(const struct hinic3_dpif_flow *put, const struct hinic3_nlattr *args, size_t args_len, uint8_t table_id)
{
    struct hinic3_drv_ops *ops = NULL;
    struct hovs_dpif_flow hovs_put = {0};
    int ret;
    if (put == NULL || args == NULL)
    {
        HINIC3_LOG(WARNING, DRIVER, "The hinic3_flow_put pointer parameter is NULL!");
        return -EINVAL;
    }
    ops = hinic3_get_drv_ops();

    hovs_put.key = (const struct nlattr *)put->key;
    hovs_put.key_len = put->key_len;
    hovs_put.mask = (const struct nlattr *)put->mask;
    hovs_put.mask_len = put->mask_len;
    hovs_put.actions = (const struct nlattr *)put->actions;
    hovs_put.action_len = put->action_len;
    hovs_put.mask_present = put->mask_present;
    hovs_put.hw_ufid = put->hw_ufid;
    if (table_id == 0)
    {
        HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_flow_mgmt_put, HINIC3_DRV_FUNC_NO_PTR);
        HINIC3_LOG_HINIC_FLOW_API_LOG_NO_LOG(ret, HINIC3_FLOW_AGENT_PUT,
                                             ops->hovs_flow_mgmt_put(&hovs_put, (const struct nlattr *)args, args_len));
    }
    else
    {
        HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_flexda_flow_mgmt_put, HINIC3_DRV_FUNC_NO_PTR);
        HINIC3_LOG_HINIC_FLOW_API_LOG_NO_LOG(ret, HINIC3_FLEXDA_FLOW_AGENT_PUT,
            ops->hovs_flexda_flow_mgmt_put(table_id,(const struct hovs_flexda_dpif_flow *)&hovs_put, (const struct nlattr *)args, args_len));
    }

    return hinic3_convert_error_code(ret);
}

int hinic3_flow_mgmt_get_by_key(const struct hinic3_nlattr *key, size_t key_len, struct hinic3_dpif_flow *put,
                                uint64_t *get_flow_related_ufid)
{
    if (key == NULL || put == NULL)
    {
        HINIC3_LOG(WARNING, DRIVER, "hinic3_flow_mgmt_get_by_key pointer parameter is NULL!");
        return -EINVAL;
    }
    int ret;
    struct hinic3_drv_ops *ops = NULL;
    struct hovs_dpif_flow_for_get hovs_put = {0};

    hovs_put.key = (struct nlattr *)put->key;
    hovs_put.key_len = put->key_len;
    hovs_put.mask = (struct nlattr *)put->mask;
    hovs_put.mask_len = put->mask_len;
    hovs_put.actions = (struct nlattr *)put->actions;
    hovs_put.action_len = put->action_len;

    ops = hinic3_get_drv_ops();
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_flow_mgmt_get_by_key, HINIC3_DRV_FUNC_NO_PTR);
    HINIC3_LOG_HINIC_FLOW_API_LOG_NO_LOG(ret, HINIC3_FLOW_AGENT_GET_BY_KEY,
                                         ops->hovs_flow_mgmt_get_by_key((const struct nlattr *)key, key_len, &hovs_put));
    if (ret == 0)
    {
        put->hw_ufid = hovs_put.hw_ufid;
        put->key = (struct hinic3_nlattr_obj *)hovs_put.key;
        put->key_len = hovs_put.key_len;
        put->mask = (struct hinic3_nlattr_obj *)hovs_put.mask;
        put->mask_len = hovs_put.mask_len;
        put->actions = (struct hinic3_nlattr_obj *)hovs_put.actions;
        put->mask_present = hovs_put.mask_present;
        put->action_len = hovs_put.action_len;
        *get_flow_related_ufid = hovs_put.related_hw_ufid;
        memcpy(&put->stats, &hovs_put.stats, sizeof(struct hovs_flow_stats));
    }
    return ret;
}

int hinic3_flow_get_by_ufid(uint64_t ufid, struct hinic3_dpif_flow_for_get *get, uint8_t table_id)
{
    struct hinic3_drv_ops *ops = NULL;
    int ret;

    if (get == NULL)
    {
        HINIC3_LOG(WARNING, DRIVER, "The hinic3_flow_get_by_ufid pointer parameter is NULL!");
        return -EINVAL;
    }
    ops = hinic3_get_drv_ops();
    if (table_id == 0)
    {
        struct hovs_dpif_flow_for_get hovs_get;
        memset(&hovs_get, 0, sizeof(hovs_get));
        hovs_get.key = (struct nlattr *)get->key;
        hovs_get.mask = (struct nlattr *)get->mask;
        hovs_get.actions = (struct nlattr *)get->actions;
        HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_flow_mgmt_get_by_ufid, HINIC3_DRV_FUNC_NO_PTR);
        HINIC3_LOG_HINIC_FLOW_API_LOG_NO_LOG(ret, HINIC3_FLOW_AGENT_GET_BY_UFID,
                                             ops->hovs_flow_mgmt_get_by_ufid(ufid, &hovs_get));
        if (ret != 0)
        {
            HINIC3_LOG(WARNING, DRIVER, "The hinic3 flow get by ufid failed, error is %d!", ret);
            return hinic3_convert_error_code(ret);
        }
        get->key_len = hovs_get.key_len;
        get->mask_len = hovs_get.mask_len;
        get->action_len = hovs_get.action_len;
        get->mask_present = hovs_get.mask_present;
        get->ol_ufid = hovs_get.hw_ufid;
        get->related_hw_ufid = hovs_get.related_hw_ufid;

        memcpy(&get->stats, &hovs_get.stats, sizeof(struct hovs_flow_stats));
        return 0;
    }
    else
    {
        struct hovs_flexda_dpif_flow_for_get hovs_flexda_get;
        memset(&hovs_flexda_get, 0, sizeof(hovs_flexda_get));
        hovs_flexda_get.key = (struct nlattr *)get->key;
        hovs_flexda_get.mask = (struct nlattr *)get->mask;
        hovs_flexda_get.actions = (struct nlattr *)get->actions;
        HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_flexda_flow_mgmt_get_by_ufid, HINIC3_DRV_FUNC_NO_PTR);
        HINIC3_LOG_HINIC_FLOW_API_LOG_NO_LOG(ret, HINIC3_FLEXDA_FLOW_AGENT_GET_BY_UFID,
                                             ops->hovs_flexda_flow_mgmt_get_by_ufid(table_id, ufid, &hovs_flexda_get));
        if (ret != 0)
        {
            HINIC3_LOG(WARNING, DRIVER, "hinic3 flow get by ufid failed, error is %d!", ret);
            return hinic3_convert_error_code(ret);
        }
        get->key_len = hovs_flexda_get.key_len;
        get->mask_len = hovs_flexda_get.mask_len;
        get->action_len = hovs_flexda_get.action_len;
        get->mask_present = hovs_flexda_get.mask_present;
        get->ol_ufid = hovs_flexda_get.hw_ufid;
        get->related_hw_ufid = hovs_flexda_get.related_hw_ufid;

        memcpy(&get->stats, &hovs_flexda_get.stats, sizeof(struct hovs_flow_stats));
        return 0;
    }
}

int hinic3_flow_del_by_ufid(uint64_t ufid, uint8_t table_id)
{
    int ret;
    struct hinic3_drv_ops *ops = NULL;
    ops = hinic3_get_drv_ops();
    if (table_id == 0)
    {
        HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_flow_mgmt_del_by_ufid, HINIC3_DRV_FUNC_NO_PTR);
        HINIC3_LOG_HINIC_FLOW_API_LOG_NO_LOG(ret, HINIC3_FLOW_AGENT_DEL_BY_UFID, ops->hovs_flow_mgmt_del_by_ufid(ufid));
    }
    else
    {
        HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_flexda_flow_mgmt_del_by_ufid, HINIC3_DRV_FUNC_NO_PTR);
        HINIC3_LOG_HINIC_FLOW_API_LOG_NO_LOG(ret, HINIC3_FLEXDA_FLOW_AGENT_DEL_BY_UFID, ops->hovs_flexda_flow_mgmt_del_by_ufid(table_id, ufid));
    }
    return hinic3_convert_error_code(ret);
}

int hinic3_flow_del_batch(const uint32_t table_id, const uint64_t *ufids, struct hinic3_dpif_flow_for_get **flows, const size_t cnt)
{
    int ret;
    struct hinic3_drv_ops *ops = NULL;
    if (ufids == NULL || flows == NULL || *flows == NULL)
    {
        HINIC3_LOG(WARNING, DRIVER, "The hinic3_flow_del_batch pointer parameter is NULL!");
        return -EINVAL;
    }

    ops = hinic3_get_drv_ops();
    if (hinic3_card_mod_get() == PROG_MODE)
    {
        HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_flexda_flow_mgmt_del_by_batch, HINIC3_DRV_FUNC_NO_PTR);
        HINIC3_LOG_HINIC_FLOW_API_LOG_NO_LOG(ret, HINIC3_FLEXDA_FLOW_AGENT_DEL_BATCH,
                                             ops->hovs_flexda_flow_mgmt_del_by_batch(table_id, ufids, (struct hovs_dpif_flow_for_get **)flows, cnt));
    }
    else
    {
        HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_flow_mgmt_del_by_batch, HINIC3_DRV_FUNC_NO_PTR);
        HINIC3_LOG_HINIC_FLOW_API_LOG_NO_LOG(ret, HINIC3_FLOW_AGENT_DEL_BATCH,
                                             ops->hovs_flow_mgmt_del_by_batch(ufids, (struct hovs_dpif_flow_for_get **)flows, cnt));
    }
    return hinic3_convert_error_code(ret);
}

int hinic3_flow_flush(void)
{
    int ret;
    struct hinic3_drv_ops *ops = NULL;
    ops = hinic3_get_drv_ops();
    if (hinic3_card_mod_get() == PROG_MODE)
    {
        HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_flexda_flow_mgmt_flush, HINIC3_DRV_FUNC_NO_PTR);
        HINIC3_LOG_HINIC_FLOW_API_LOG_NO_LOG(ret, HINIC3_FLEXDA_FLOW_AGENT_FLUSH, ops->hovs_flexda_flow_mgmt_flush());
    }
    else
    {
        HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_flow_mgmt_flush, HINIC3_DRV_FUNC_NO_PTR);
        HINIC3_LOG_HINIC_FLOW_API_LOG_NO_LOG(ret, HINIC3_FLOW_AGENT_FLUSH, ops->hovs_flow_mgmt_flush());
    }
    return hinic3_convert_error_code(ret);
}

int hinic3_flow_dump_start(void **state)
{
    int ret;
    struct hinic3_drv_ops *ops = NULL;
    ops = hinic3_get_drv_ops();
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_flow_mgmt_dump_start, HINIC3_DRV_FUNC_NO_PTR);
    HINIC3_LOG_HINIC_FLOW_API_LOG_NO_LOG(ret, HINIC3_FLOW_AGENT_DUMP_START, ops->hovs_flow_mgmt_dump_start(state));
    return hinic3_convert_error_code(ret);
}

int hinic3_flow_dump_start_by_table_id(uint32_t table_id, void **state)
{
    int ret;
    struct hinic3_drv_ops *ops = NULL;
    ops = hinic3_get_drv_ops();
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_flexda_flow_mgmt_dump_start, HINIC3_DRV_FUNC_NO_PTR);
    HINIC3_LOG_HINIC_FLOW_API_LOG_NO_LOG(ret, HINIC3_FLEXDA_FLOW_AGENT_DUMP_START, ops->hovs_flexda_flow_mgmt_dump_start(table_id, state));
    return hinic3_convert_error_code(ret);
}

int hinic3_process_dpif_flow_for_get_data(struct hovs_dpif_flow_for_get dump_for_get,
                                          struct hinic3_dpif_flow_for_get *dump)
{
    dump->key_len = dump_for_get.key_len;
    dump->mask_len = dump_for_get.mask_len;
    dump->action_len = dump_for_get.action_len;
    dump->mask_present = dump_for_get.mask_present;
    dump->ol_ufid = dump_for_get.hw_ufid;
    dump->related_hw_ufid = dump_for_get.related_hw_ufid;

    dump->stats.packet_count = dump_for_get.stats.packet_count;
    dump->stats.byte_count = dump_for_get.stats.byte_count;
    dump->stats.ct_loss_pkts = dump_for_get.stats.ct_loss_pkts;
    dump->stats.live_time = dump_for_get.stats.live_time;
    dump->stats.age_time = dump_for_get.stats.age_time;
    dump->stats.tcp_flags = dump_for_get.stats.tcp_flags;

    memcpy(dump->stats.block, dump_for_get.stats.block, sizeof(dump_for_get.stats.block));
    return 0;
}

int hinic3_flow_dump_next(void *state, struct hinic3_dpif_flow_for_get *dump)
{
    if (state == NULL || dump == NULL)
    {
        HINIC3_LOG(WARNING, DRIVER, "The hinic3_flow_dump_next pointer parameter is NULL!");
        return -EINVAL;
    }
    struct hinic3_drv_ops *ops = NULL;
    struct hovs_dpif_flow_for_get dump_for_get;
    int ret;

    ops = hinic3_get_drv_ops();

    memset(&dump_for_get, 0, sizeof(dump_for_get));
    dump_for_get.key = (struct nlattr *)dump->key;
    dump_for_get.mask = (struct nlattr *)dump->mask;
    dump_for_get.actions = (struct nlattr *)dump->actions;

    if (hinic3_card_mod_get() == PROG_MODE)
    {
        HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_flexda_flow_mgmt_dump_next, HINIC3_DRV_FUNC_NO_PTR);
        HINIC3_LOG_HINIC_FLOW_API_LOG_NO_LOG(ret, HINIC3_FLEXDA_FLOW_AGENT_DUMP_NEXT,
                                             ops->hovs_flexda_flow_mgmt_dump_next(state, &dump_for_get));
    }
    else
    {
        HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_flow_mgmt_dump_next, HINIC3_DRV_FUNC_NO_PTR);
        HINIC3_LOG_HINIC_FLOW_API_LOG_NO_LOG(ret, HINIC3_FLOW_AGENT_DUMP_NEXT,
                                             ops->hovs_flow_mgmt_dump_next(state, &dump_for_get));
    }

    if (ret != 0)
    {
        return hinic3_convert_error_code(ret);
    }
    return hinic3_process_dpif_flow_for_get_data(dump_for_get, dump);
}

int hinic3_flow_dump_done(void *state)
{
    int ret;
    struct hinic3_drv_ops *ops = NULL;
    if (state == NULL)
    {
        return 0;
    }

    ops = hinic3_get_drv_ops();
    if (hinic3_card_mod_get() == PROG_MODE)
    {
        HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_flexda_flow_mgmt_dump_done, HINIC3_DRV_FUNC_NO_PTR);
        HINIC3_LOG_HINIC_FLOW_API_LOG_NO_LOG(ret, HINIC3_FLEXDA_FLOW_AGENT_DUMP_DONE, ops->hovs_flexda_flow_mgmt_dump_done(state));
    }
    else
    {
        HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_flow_mgmt_dump_done, HINIC3_DRV_FUNC_NO_PTR);
        HINIC3_LOG_HINIC_FLOW_API_LOG_NO_LOG(ret, HINIC3_FLOW_AGENT_DUMP_DONE, ops->hovs_flow_mgmt_dump_done(state));
    }
    return hinic3_convert_error_code(ret);
}

int hinic3_flow_get_maxflows(uint32_t table_id, uint32_t *max_flows)
{
    int ret;
    struct hinic3_drv_ops *ops = NULL;
    if (max_flows == NULL)
    {
        HINIC3_LOG(WARNING, DRIVER, "The hinic3_flow_get_maxflows pointer parameter is NULL!");
        return -EINVAL;
    }
    ops = hinic3_get_drv_ops();

    if (hinic3_card_mod_get() == PROG_MODE)
    {
        HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_flexda_flow_mgmt_get_maxflows, HINIC3_DRV_FUNC_NO_PTR);
        HINIC3_LOG_HINIC_FLOW_API_LOG(ret, HINIC3_FLOW_AGENT_GET_MAXFLOWS,
                                     ops->hovs_flexda_flow_mgmt_get_maxflows(table_id, max_flows));
    }
    else
    {
        HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_flow_mgmt_get_maxflows, HINIC3_DRV_FUNC_NO_PTR);
        HINIC3_LOG_HINIC_FLOW_API_LOG(ret, HINIC3_FLOW_AGENT_GET_MAXFLOWS, ops->hovs_flow_mgmt_get_maxflows(max_flows));
    }

    return hinic3_convert_error_code(ret);
}

int hinic3_flow_get_maxflows_by_table_id(uint32_t table_id, uint32_t *max_flows)
{
    int ret;
    struct hinic3_drv_ops *ops = NULL;
    if (max_flows == NULL)
    {
        HINIC3_LOG(WARNING, DRIVER, "hinic3_flow_get_maxflows pointer parameter is NULL!");
        return -EINVAL;
    }
    ops = hinic3_get_drv_ops();
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_flexda_flow_mgmt_get_maxflows, HINIC3_DRV_FUNC_NO_PTR);
    HINIC3_LOG_HINIC_FLOW_API_LOG(ret, HINIC3_FLEXDA_FLOW_AGENT_GET_MAXFLOWS, ops->hovs_flexda_flow_mgmt_get_maxflows(table_id, max_flows));

    return hinic3_convert_error_code(ret);
}

int hinic3_flow_set_forward_mode(uint8_t forward_mode)
{
    int ret;
    struct hinic3_drv_ops *ops = NULL;

    ops = hinic3_get_drv_ops();
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_flow_mgmt_set_forward_mode, HINIC3_DRV_FUNC_NO_PTR);

    HINIC3_LOG_HINIC_FLOW_API_LOG(ret, HINIC3_FLOW_AGENT_SET_FORWARD_MODE,
                                  ops->hovs_flow_mgmt_set_forward_mode(forward_mode));
    return hinic3_convert_error_code(ret);
}

int hinic3_flow_get_forward_mode(uint8_t *forward_mode)
{
    int ret;
    struct hinic3_drv_ops *ops = NULL;
    if (forward_mode == NULL)
    {
        HINIC3_LOG(WARNING, DRIVER, "The hinic3_flow_get_forward_mode pointer parameter is NULL!");
        return -EINVAL;
    }

    ops = hinic3_get_drv_ops();
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_flow_mgmt_get_forward_mode, HINIC3_DRV_FUNC_NO_PTR);

    HINIC3_LOG_HINIC_FLOW_API_LOG(ret, HINIC3_FLOW_AGENT_GET_FORWARD_MODE,
                                  ops->hovs_flow_mgmt_get_forward_mode(forward_mode));
    return hinic3_convert_error_code(ret);
}

int hinic3_statistics_flow_get_by_ufid(const uint64_t *ufid, const size_t cnt, struct hinic3_flow_stats *stats, uint8_t table_id)
{
    int ret;
    struct hinic3_drv_ops *ops = NULL;
    ops = hinic3_get_drv_ops();
    if (table_id == 0)
    {
        HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_statistics_flow_get_by_ufid, HINIC3_DRV_FUNC_NO_PTR);
        HINIC3_LOG_HINIC_FLOW_API_LOG_NO_LOG(ret, HINIC3_STATISTICS_FLOW_GET_BY_UFID,
                                             ops->hovs_statistics_flow_get_by_ufid(ufid, cnt, (struct hovs_flow_stats *)stats));
    }
    else
    {
        HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_flexda_statistics_flow_get_by_ufid, HINIC3_DRV_FUNC_NO_PTR);
        HINIC3_LOG_HINIC_FLOW_API_LOG_NO_LOG(ret, HINIC3_FLEXDA_STATISTICS_FLOW_GET_BY_UFID,
                                             ops->hovs_flexda_statistics_flow_get_by_ufid(table_id, ufid, cnt, (struct hovs_flow_stats *)stats));
    }
    return hinic3_convert_error_code(ret);
}

const char *hinic3_flow_get_api_name(hinic3_flow_api api_index)
{
    for (int i = 0; i < HINIC3_FLOW_API_MAX; ++i)
    {
        if (api_index == g_flow_api_arr[i].api_index)
        {
            return g_flow_api_arr[i].api_name;
        }
    }
    return NULL;
}

hinic3_flow_api hinic3_flow_get_api_index(const char *api_name)
{
    for (int i = 0; i < HINIC3_FLOW_API_MAX; ++i)
    {
        if (strncmp(api_name, g_flow_api_arr[i].api_name, strlen(g_flow_api_arr[i].api_name) + 1) == 0)
        {
            return g_flow_api_arr[i].api_index;
        }
    }
    return HINIC3_FLOW_API_MAX;
}

int hinic3_flow_init(void)
{
    struct hinic3_flow_capability cap;
    memset(&cap, 0, sizeof(cap));

    if (hinic3_flow_get_capability(&cap) != 0)
    {
        HINIC3_LOG(ERR, DRIVER, "Failed to get hiovs capability!");
        return -1;
    }

    if (cap.rx_thread_num > HINIC3_RX_THREAD_MAX)
    {
        HINIC3_LOG(ERR, DRIVER, "flow agent rx thread num: %u is greater than max num: %d!", cap.rx_thread_num,
                   HINIC3_RX_THREAD_MAX);
        return -1;
    }

    return 0;
}

int hinic3_flow_class_init(void)
{
    hiovs_api_record *flow_api_record = NULL;

    flow_api_record = hinic3_get_flow_api_record();
    if (flow_api_record == NULL)
    {
        return -1;
    }
    hinic3_convert_error_code_init();

    memset(flow_api_record, 0, sizeof(hiovs_api_record) * HINIC3_FLOW_API_MAX);

    pthread_mutex_lock(&g_hinic3_flow_mutex);
    int ret = hinic3_flow_init();
    if (ret != 0)
    {
        HINIC3_LOG(ERR, DRIVER, "hinic3_flow_init fail ret is %d!", ret);
        pthread_mutex_unlock(&g_hinic3_flow_mutex);
        return ret;
    }

    pthread_mutex_unlock(&g_hinic3_flow_mutex);
    return ret;
}

void hinic3_flow_class_uninit(void)
{
    hiovs_api_record *flow_api_record = hinic3_get_flow_api_record();
    memset(flow_api_record, 0, sizeof(hiovs_api_record) * HINIC3_FLOW_API_MAX);
}

int hinic3_iface_global_cfg_set(struct hiovs_mirror_session_info *hiovs_session_info,
                                enum hinic3_global_cfg_arg_type type)
{
    int ret;
    uint32_t session_id;
    uint8_t buf[GLOBAL_CFG_ARGS_SIZE];
    struct hinic3_nlattr set_nla;
    struct hinic3_drv_ops *ops = NULL;

    ops = hinic3_get_drv_ops();
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_global_cfg_set, HINIC3_DRV_FUNC_NO_PTR);

    hinic3_nlattr_init(&set_nla, buf, GLOBAL_CFG_ARGS_SIZE);

    switch (type)
    {
    case HINIC3_GLOBAL_CFG_ARG_MIRROR_SESSION_SET:
        hinic3_nlattr_put_unspec(&set_nla, type, hiovs_session_info, sizeof(struct hiovs_mirror_session_info));
        break;
    case HINIC3_GLOBAL_CFG_ARG_MIRROR_SESSION_DEL:
        session_id = (uint32_t)hiovs_session_info->session_id;
        hinic3_nlattr_put_u32(&set_nla, type, session_id);
        break;
    default:
        break;
    }

    ret = ops->hovs_global_cfg_set((struct nlattr *)set_nla.data, set_nla.used_len,
                                   (struct nlattr *)set_nla.data, &set_nla.used_len);
    if (ret != 0)
    {
        hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_GLOBAL_CFG_SET, 1);
    }
    return hinic3_convert_error_code(ret);
}

int hinic3_iface_high_priority_upcall_set(const uint16_t port_id, struct hinic3_high_priority_cfgs *cfgs)
{
    int ret;
    struct hinic3_drv_ops *ops = NULL;
    ops = hinic3_get_drv_ops();
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_port_mgmt_set_upcall_priority, HINIC3_DRV_FUNC_NO_PTR);
    ret = ops->hovs_port_mgmt_set_upcall_priority(port_id, cfgs->hovs_cfgs, cfgs->cfgs_len, 1);
    if (ret != 0)
    {
        HINIC3_LOG(ERR, DRIVER, "The hinic3 high priority upcall set fail, ret is %d!", ret);
    }
    return hinic3_convert_error_code(ret);
}

int hinic3_mega_flow_put(const struct hinic3_dpif_flow *put, uint32_t index, uint64_t *ufid)
{
    if ((put == NULL) || (ufid == NULL))
    {
        HINIC3_LOG(WARNING, DRIVER, "The hinic3_mega_flow_put pointer parameter is NULL!");
        return -EINVAL;
    }

    int ret;
    struct hinic3_drv_ops *ops = hinic3_get_drv_ops();
    struct hovs_dpif_flow hovs_put = {0};

    hovs_put.key = (const struct nlattr *)put->key;
    hovs_put.key_len = put->key_len;
    hovs_put.mask = (const struct nlattr *)put->mask;
    hovs_put.mask_len = put->mask_len;
    hovs_put.actions = (const struct nlattr *)put->actions;
    hovs_put.action_len = put->action_len;
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_mega_flow_mgmt_put, HINIC3_DRV_FUNC_NO_PTR);
    HINIC3_LOG_HINIC_FLOW_API_LOG_NO_LOG(ret, HINIC3_FLOW_AGENT_MEGA_PUT,
                                         ops->hovs_mega_flow_mgmt_put(&hovs_put, index, ufid));
    return hinic3_convert_error_code(ret);
}

int hinic3_mega_flow_del_by_ufid(uint64_t ufid)
{
    int ret;
    struct hinic3_drv_ops *ops = hinic3_get_drv_ops();

    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_mega_flow_mgmt_del_by_ufid, HINIC3_DRV_FUNC_NO_PTR);
    HINIC3_LOG_HINIC_FLOW_API_LOG_NO_LOG(ret, HINIC3_FLOW_AGENT_MEGA_DEL_BY_UFID,
                                         ops->hovs_mega_flow_mgmt_del_by_ufid(ufid));
    return hinic3_convert_error_code(ret);
}

int hinic3_mega_flow_flush(void)
{
    int ret;
    struct hinic3_drv_ops *ops = hinic3_get_drv_ops();

    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_mega_flow_mgmt_flush, HINIC3_DRV_FUNC_NO_PTR);
    HINIC3_LOG_HINIC_FLOW_API_LOG_NO_LOG(ret, HINIC3_FLOW_AGENT_MEGA_FLUSH, ops->hovs_mega_flow_mgmt_flush());
    return hinic3_convert_error_code(ret);
}

int hinic3_mega_flow_dump_start(void **state)
{
    if (state == NULL)
    {
        HINIC3_LOG(WARNING, DRIVER, "The hinic3_mega_flow_dump_start pointer parameter is NULL!");
        return -EINVAL;
    }

    int ret;
    struct hinic3_drv_ops *ops = hinic3_get_drv_ops();

    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_mega_flow_mgmt_dump_start, HINIC3_DRV_FUNC_NO_PTR);
    HINIC3_LOG_HINIC_FLOW_API_LOG_NO_LOG(ret, HINIC3_FLOW_AGENT_MEGA_DUMP_START,
                                         ops->hovs_mega_flow_mgmt_dump_start(state));
    return hinic3_convert_error_code(ret);
}

int hinic3_mega_flow_dump_next(void *state, struct hinic3_dpif_flow_for_get *dump)
{
    if (state == NULL || dump == NULL)
    {
        HINIC3_LOG(WARNING, DRIVER, "The hinic3_mega_flow_dump_next pointer parameter is NULL!");
        return -EINVAL;
    }

    int ret;
    struct hinic3_drv_ops *ops = hinic3_get_drv_ops();
    struct hovs_dpif_flow_for_get dump_for_get = {0};

    dump_for_get.key = (struct nlattr *)dump->key;
    dump_for_get.mask = (struct nlattr *)dump->mask;
    dump_for_get.actions = (struct nlattr *)dump->actions;
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_mega_flow_mgmt_dump_next, HINIC3_DRV_FUNC_NO_PTR);
    HINIC3_LOG_HINIC_FLOW_API_LOG_NO_LOG(ret, HINIC3_FLOW_AGENT_MEGA_DUMP_NEXT,
                                         ops->hovs_mega_flow_mgmt_dump_next(state, &dump_for_get));
    if (ret != 0)
    {
        return ret;
    }
    return hinic3_process_dpif_flow_for_get_data(dump_for_get, dump);
}

int hinic3_mega_flow_dump_done(void *state)
{
    if (state == NULL)
        return 0;

    int ret;
    struct hinic3_drv_ops *ops = hinic3_get_drv_ops();

    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_mega_flow_mgmt_dump_done, HINIC3_DRV_FUNC_NO_PTR);
    HINIC3_LOG_HINIC_FLOW_API_LOG_NO_LOG(ret, HINIC3_FLOW_AGENT_MEGA_DUMP_DONE,
                                         ops->hovs_mega_flow_mgmt_dump_done(state));
    if (ret != 0)
    {
        HINIC3_LOG(ERR, DRIVER, "The hinic3_mega_flow_dump_done fail, ret is %d!", ret);
    }
    return hinic3_convert_error_code(ret);
}

int hinic3_statistics_mega_flow_get_by_ufid(const uint64_t ufid, struct hinic3_flow_stats *stats)
{
    if (stats == NULL)
    {
        HINIC3_LOG(WARNING, DRIVER, "The hinic3_statistics_mega_flow_get_by_ufid pointer parameter is NULL!");
        return -EINVAL;
    }

    int ret;
    struct hinic3_drv_ops *ops = hinic3_get_drv_ops();

    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_statistics_mega_flow_get_by_ufid, HINIC3_DRV_FUNC_NO_PTR);
    HINIC3_LOG_HINIC_FLOW_API_LOG_NO_LOG(ret, HINIC3_FLOW_AGENT_MEGA_GET_STATS,
                                         ops->hovs_statistics_mega_flow_get_by_ufid(ufid, (struct hovs_flow_stats *)stats));
    return hinic3_convert_error_code(ret);
}

int hinic3_mega_flow_set_l3_forward(bool flag)
{
    int ret;
    struct hinic3_drv_ops *ops = hinic3_get_drv_ops();

    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_mega_flow_set_l3_forward, HINIC3_DRV_FUNC_NO_PTR);
    HINIC3_LOG_HINIC_FLOW_API_LOG_NO_LOG(ret, HINIC3_FLOW_AGENT_MEGA_FLUSH, ops->hovs_mega_flow_set_l3_forward(flag));
    return hinic3_convert_error_code(ret);
}

int hinic3_flow_modify(const struct hinic3_dpif_flow *put, const struct hinic3_nlattr *args, size_t args_len)
{
    int ret;
    struct hinic3_drv_ops *ops = NULL;
    struct hovs_dpif_flow hovs_put = {0};

    if (put == NULL || args == NULL)
    {
        HINIC3_LOG(WARNING, DRIVER, "The hinic3 flow modify pointer parameter is NULL!");
        return -EINVAL;
    }

    ops = hinic3_get_drv_ops();
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_flow_mgmt_update, HINIC3_DRV_FUNC_NO_PTR);

    hovs_put.key = (struct nlattr *)put->key;
    hovs_put.key_len = put->key_len;
    hovs_put.mask = (const struct nlattr *)put->mask;
    hovs_put.mask_len = put->mask_len;
    hovs_put.actions = (struct nlattr *)put->actions;
    hovs_put.action_len = put->action_len;
    hovs_put.mask_present = put->mask_present;
    hovs_put.hw_ufid = put->hw_ufid;
    HINIC3_LOG_HINIC_FLOW_API_LOG_NO_LOG(ret, HINIC3_FLOW_MODIFY,
                                         ops->hovs_flow_mgmt_update(&hovs_put, (const struct nlattr *)args, args_len));
    return hinic3_convert_error_code(ret);
}

int hinic3_flow_get_block_table_size(uint32_t *block_num)
{
    int ret;
    struct hinic3_drv_ops *ops = NULL;

    if (block_num == NULL)
    {
        HINIC3_LOG(WARNING, DRIVER, "The hinic3_flow_get_block_table_size block_num pointer parameter is NULL!");
        return -EINVAL;
    }

    ops = hinic3_get_drv_ops();
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_flow_mgmt_get_block_table_size, HINIC3_DRV_FUNC_NO_PTR);
    HINIC3_LOG_HINIC_FLOW_API_LOG_NO_LOG(ret, HINIC3_FLOW_BLOCK_SIZE_GET,
                                         ops->hovs_flow_mgmt_get_block_table_size(block_num));
    return hinic3_convert_error_code(ret);
}

void hinic3_flow_set_block_version(uint32_t block_num, uint16_t block_id[], uint16_t block_version[], int result[])
{
    struct hinic3_drv_ops *ops = NULL;

    if (block_id == NULL || block_version == NULL || result == NULL)
    {
        HINIC3_LOG(WARNING, DRIVER, "The hinic3_flow_set_block_version pointer parameter is NULL!");
        return;
    }

    ops = hinic3_get_drv_ops();
    if (ops->hovs_flow_mgmt_set_block_version == NULL)
    {
        for (uint32_t index = 0; index < block_num; index++)
        {
            result[index] = -1;
        }
        return;
    }

    HINIC3_LOG_HINIC_FLOW_API_LOG_NO_LOG_RET(HINIC3_FLOW_BLOCK_VERSION_SET,
                                             ops->hovs_flow_mgmt_set_block_version(block_num, block_id, block_version, result));
}

int hinic3_flow_get_block_version(uint32_t block_num, uint16_t block_id[], uint16_t block_version[])
{
    int ret;
    struct hinic3_drv_ops *ops = NULL;

    if (block_id == NULL || block_version == NULL)
    {
        HINIC3_LOG(WARNING, DRIVER, "The hinic3_flow_get_block_version input pointer parameter is NULL!");
        return -EINVAL;
    }

    ops = hinic3_get_drv_ops();
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_flow_mgmt_get_block_version, HINIC3_DRV_FUNC_NO_PTR);
    HINIC3_LOG_HINIC_FLOW_API_LOG_NO_LOG(ret, HINIC3_FLOW_BLOCK_VERSION_GET,
                                         ops->hovs_flow_mgmt_get_block_version(block_num, block_id, block_version));
    return hinic3_convert_error_code(ret);
}

int hinic3_flexda_get_flow_cfg_info(hovs_flexda_config_info_t *hovs_flexda_config)
{

    int ret;
    struct hinic3_drv_ops *ops = NULL;
    ops = hinic3_get_drv_ops();
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_flexda_get_config_info, HINIC3_DRV_FUNC_NO_PTR);
    HINIC3_LOG_HINIC_FLOW_API_LOG_NO_LOG(ret, HINIC3_FLEXDA_GET_CONFIG_INFO, ops->hovs_flexda_get_config_info(hovs_flexda_config));
    if (ret != 0)
    {
        HINIC3_LOG(ERR, FLOW, "hinic3 get opa get config infofail, ret is %d", ret);
    }
    return hinic3_convert_error_code(ret);
}

void hinic3_flexda_free_flow_cfg_info(hovs_flexda_config_info_t *hovs_flexda_config)
{
    struct hinic3_drv_ops *ops = NULL;
    ops = hinic3_get_drv_ops();
    if (ops->hovs_flexda_free_config_info == NULL)
    {
        HINIC3_LOG(ERR, FLOW, "no hovs_flexda_free_config_info ptr");
        return;
    }
    HINIC3_LOG_HINIC_FLOW_API_LOG_NO_LOG_RET(HINIC3_FLEXDA_FREE_CONFIG_INFO,
                                             ops->hovs_flexda_free_config_info(hovs_flexda_config));
}