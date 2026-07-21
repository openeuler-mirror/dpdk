/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2022 Huawei Technologies Co., Ltd
 */

#include "hinic3_age_delete_flow.h"
#include "hinic3_ufid_del_flow.h"
#include "hinic3_flow_session.h"
#include "hinic3_ufid_map_rte_flow.h"
#include "hinic3_iface_flow.h"
#include "hinic3_provider.h"
#include "hinic3_iface_global.h"
#include "hinic3_flow_agent.h"
#include "hinic3_log.h"
#include "hinic3_parse_agent_config.h"
#include "hinic3_flow_dump_item_key.h"
#include "ethdev_driver.h"
#include "hinic3_bond_controller.h"
#include "hinic3_vf_controller.h"
#include "hinic3_offload_flow_port.h"
#include "hinic3_port_util.h"
#include "hinic3_mtr.h"
#include "hinic3_flow_qos.h"
#include "hinic3_offload_flow.h"

static int hinic3_flow_destroy_check(struct rte_flow *flow, struct rte_flow_error *error)
{
    int ret = 0;
    if (flow->flags.is_mem_used == 0)
    {
        return rte_flow_error_set(error, EIO, RTE_FLOW_ERROR_TYPE_UNSPECIFIED, NULL,
                                  "Flow destroy: Duplicate memory used.");
    }

    if (flow->flags.is_offload == 0)
    {
        hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_FLOW_NOT_OFFLOADED, 1);
        return rte_flow_error_set(error, EBUSY, RTE_FLOW_ERROR_TYPE_UNSPECIFIED, NULL,
                                  "Flow destroy: Flow not completely offloaded.");
    }

    if (flow->flags.is_sample == HINIC3_FLOW_EMC_MIRROR)
    {
        ret = hinic3_del_rte_flow_in_session(flow);
        if (ret != 0)
        {
            hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_AGE_DEL_FLOW_IN_SESSION, 1);
            return rte_flow_error_set(error, EBUSY, RTE_FLOW_ERROR_TYPE_STATE, NULL,
                                      "Flow destroy: Driver interface error.");
        }
    }
    if (flow->flags.has_flow_qos == 1)
    {
        ret = hinic3_del_flow_qos_by_meter_id(flow->meter_id);
        if (ret != 0)
        {
            hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_DEL_FLOW_QOS, 1);
            return rte_flow_error_set(error, EPERM, RTE_FLOW_ERROR_TYPE_STATE, NULL,
                                      "Flow destroy: Flow qos delete error.");
        }
        flow->flags.has_flow_qos = 0;
    }
    return 0;
}

static void hinic3_remove_aged_flow_ele(struct rte_eth_dev *dev, struct rte_flow *flow)
{
    if (hinic3_is_ethdev_valid(dev) == false)
        return;

    struct rte_aged_flow_list *aged_flow_list = NULL;
    struct rte_aged_flow_list_node *iter = NULL;
    struct rte_aged_flow_list_node *next = NULL;
    struct hinic3_bond_dev *bond_dev = hinic3_ethdev_get_bond_private(dev);
    struct hinic3_vf_dev *vf_dev = hinic3_ethdev_get_vf_private(dev);

    if (hinic3_is_bond_by_prefix(bond_dev->vport_id) == false)
        aged_flow_list = &vf_dev->aged_flow_list;
    else
        aged_flow_list = &bond_dev->aged_flow_list;

    hinic3_pthread_mutex_lock(&aged_flow_list->mutex);
    LIST_FOR_EACH_SAFE(iter, next, node, &aged_flow_list->list_head)
    {
        if (iter->flow == flow)
        {
            hinic3_list_remove(&iter->node);
            hinic3_free(iter);
        }
    }
    hinic3_pthread_mutex_unlock(&aged_flow_list->mutex);
}

static int hinic3_flow_destroy_sub(struct rte_eth_dev *dev, struct rte_flow *flow, struct rte_flow_error *error)
{
    int ret;
    struct hash_table_node *rte_bucket = NULL;

    rte_bucket = hinic3_get_offload_flow_bucket(flow->flow_hash, flow->table_id);
    if (rte_bucket == NULL) {
        return rte_flow_error_set(error, EINVAL, RTE_FLOW_ERROR_TYPE_STATE, NULL,
                                  "Flow destroy: rte bucket is NULL.");
    }
    hinic3_spinlock_lock(&rte_bucket->spinlock);

    ret = hinic3_flow_destroy_check(flow, error);
    if (ret != 0)
    {
        hinic3_spinlock_unlock(&rte_bucket->spinlock);
        return ret;
    }

    ret = hinic3_del_rte_flow_in_hmap(rte_bucket, flow);
    if (ret != 0)
    {
        hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_DEL_RTE_FLOW_IN_HMAP, 1);
        hinic3_spinlock_unlock(&rte_bucket->spinlock);
        return rte_flow_error_set(error, EBUSY, RTE_FLOW_ERROR_TYPE_UNSPECIFIED, NULL,
                                  "Flow destroy: Failed to del flow in hmap.");
    }

    if (flow->flags.is_aged == 0)
    {
        ret = hinic3_flow_del_by_ufid(flow->hw_ufid, flow->table_id);
        if (ret != 0)
        {
            hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_DEL_HARD_FLOW, 1);
            hinic3_spinlock_unlock(&rte_bucket->spinlock);
            (void)rte_flow_error_set(error, -ret, RTE_FLOW_ERROR_TYPE_STATE, NULL, "Flow destroy : Driver interface error.");
            return ret;
        }
    }
    else if (hinic3_support_hardware_flow_age_set())
    {
        (void)hinic3_remove_aged_flow_ele(dev, flow);
    }

    (void)hinic3_rte_or_fuzzy_flow_dealloc(flow);

    if (hinic3_get_offload_flow_nums() > 0)
    {
        hinic3_dec_offload_flow_nums();
    }
    hinic3_spinlock_unlock(&rte_bucket->spinlock);
    return 0;
}

int hinic3_flow_emc_destroy(struct rte_eth_dev *dev, struct rte_flow *flow, struct rte_flow_error *error)
{
    int ret;
    ret = hinic3_try_rlock_flush_all();
    if (ret == EBUSY)
    {
        return 0;
    }

    if (ret != 0)
    {
        return rte_flow_error_set(error, EPERM, RTE_FLOW_ERROR_TYPE_UNSPECIFIED, NULL,
                                  "Flow destroy: Flush_all rlock failed.");
    }

    ret = hinic3_flow_destroy_sub(dev, flow, error);
    (void)hinic3_runlock_flush_all();

    return ret;
}

int hinic3_flow_agent_age_callback(uint64_t hw_ufid __rte_unused, const struct hinic3_dpif_flow_for_get *flow,
                                   const struct hinic3_nlattr *args __rte_unused)
{
    struct hinic3_flow_agent_db *hw_offload = NULL;
    struct hinic3_dp_extend_info *dp_info = hinic3_get_offload_extend_info();
    struct hinic3_conntrack_full_key full_key = {0};
    struct hinic3_nlattr nla_args;
    int ret = 0;

    if ((flow == NULL) || (args == NULL) || (dp_info == NULL))
    {
        return -1;
    }

    hw_offload = dp_info->hw_offload;
    if (hw_offload == NULL)
    {
        return -1;
    }

    if (hinic3_support_hardware_flow_age_set())
    {
        hinic3_nlattr_init(&nla_args, flow->key, flow->key_len);
        hinic3_nlattr_reset_itr(&nla_args, flow->key_len);
        hinic3_revert_key(&full_key.key, &nla_args);

        hinic3_nlattr_init(&nla_args, flow->actions, flow->action_len);
        hinic3_nlattr_reset_itr(&nla_args, flow->action_len);

        full_key.key.meta.key_len = sizeof(struct hinic3_conntrack_full_key) - sizeof(struct hinic3_conntrack_key);
        hinic3_revert_action(&full_key.key, &nla_args);
        ret = hinic3_deal_aged_flow_by_event(&full_key);
        if (ret != 0)
        {
            HINIC3_LOG(ERR, AGENT, "failed to del aged flow by event");
            return -1;
        }
        return 0;
    }

    /* 硬件已卸载流表数量减一 */
    rte_atomic32_dec(&hw_offload->forward_engine.current_flow_size);
    return 0;
}

int hinic3_flow_flush_all(struct rte_flow_error *error)
{
    int ret;
    (void)hinic3_wlock_flush_all();
    ret = hinic3_session_flush_all(HINIC3_EMC_VXLAN_SESSION | HINIC3_EMC_GRE_SESSION);
    if (ret != 0)
    {
        (void)hinic3_wunlock_flush_all();
        hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_MIRROR_SESSION_FLUSH, 1);
        return rte_flow_error_set(error, EPERM, RTE_FLOW_ERROR_TYPE_STATE, NULL,
                                  "Flow flush all: Session driver interface error.");
    }
    ret = hinic3_flush_rte_flow_mpool();
    if (ret != 0)
    {
        (void)hinic3_wunlock_flush_all();
        hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_EMC_MPOOL_FLUSH, 1);
        return rte_flow_error_set(error, -ret, RTE_FLOW_ERROR_TYPE_STATE, NULL,
                                  "Flow flush all: Dpak interface error.");
    }
    (void)hinic3_ufid_map_flush();
    ret = hinic3_flush_ufid_map_mpool();
    if (ret != 0)
    {
        (void)hinic3_wunlock_flush_all();
        hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_UFID_MAP_MPOOL_FLUSH, 1);
        return rte_flow_error_set(error, -ret, RTE_FLOW_ERROR_TYPE_STATE, NULL,
                                  "Flow flush all: Dpak interface error.");
    }
    ret = hinic3_flow_flush();
    if (ret != 0)
    {
        (void)hinic3_wunlock_flush_all();
        hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_EMC_HARD_FLOW_FLUSH, 1);
        (void)rte_flow_error_set(error, -ret, RTE_FLOW_ERROR_TYPE_STATE, NULL,
                                 "Flow flush all: Driver interface error.");
        return ret;
    }

    hinic3_reset_offload_flow_nums();
    (void)hinic3_wunlock_flush_all();
    return 0;
}
