/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2025 Huawei Technologies Co., Ltd
 */

#include "hinic3_mtr.h"
#include "hinic3_bond_controller.h"
#include "hinic3_port_util.h"
#include "hinic3_vf_controller.h"
#include "hinic3_iface_flow_api_record.h"
#include "hinic3_mega_offload.h"
#include "hinic3_ufid_map_rte_flow.h"
#include "hinic3_offload_flow.h"
#include "hinic3_eth_util.h"

#define HINIC3_FLOW_ATTR_OPERATION_OFFSET 8
#define HINIC3_FLOW_ATTR_OPERATION_MASK 0x1

enum hinic3_emc_flow_operation
{
    HINIC3_EMC_FLOW_CREATE_MODE,
    HINIC3_EMC_FLOW_MODIFY_MODE
};

int
hinic3_eth_flow_validate(struct rte_eth_dev *dev, const struct rte_flow_attr *attr,
    const struct rte_flow_item *pattern, const struct rte_flow_action *actions, struct rte_flow_error *error)
{
    (void)dev;
    (void)attr;
    (void)pattern;
    (void)actions;
    (void)error;

    return 0;
}

static int
hinic3_eth_flow_query_ecology(struct rte_eth_dev *dev, struct rte_flow *flow,
    const struct rte_flow_action *actions, void *data, struct rte_flow_error *error)
{
    if (flow->flags.is_dumb == 1) {
        HINIC3_LOG(WARNING, FLOW, "classify flow query not support.");
        return 0;
    }

    if (flow->flags.is_mega == 1)
        return hinic3_mega_flow_query(dev, flow, data, error);

    return hinic3_flow_query_emc(dev, flow, actions, data, error);
}

int 
hinic3_eth_flow_flush(struct rte_eth_dev *dev, struct rte_flow_error *error)
{
    if (dev == NULL) {
        HINIC3_LOG(ERR, FLOW, "Invalid parameter, dev is NULL!");
        rte_flow_error_set(error, EINVAL, RTE_FLOW_ERROR_TYPE_UNSPECIFIED, NULL, "Invalid parameter, dev is NULL.");
        return -EINVAL;
    }

    int ret = 0;
    uint16_t port_id = UINT16_MAX;

    ret = hinic3_get_id_from_dev(dev, &port_id);
    if (ret != 0) {
        HINIC3_LOG(ERR, FLOW, "rte flow flush: Get port id failed!");
        rte_flow_error_set(error, EINVAL, RTE_FLOW_ERROR_TYPE_UNSPECIFIED, NULL, "Get port id failed.");
        return -EINVAL;
    }

    ret = hinic3_del_flow_in_hmap_by_port(port_id);
    if (ret != 0) {
        HINIC3_LOG(ERR, FLOW, "flush flow by port error!");
        rte_flow_error_set(error, EPERM, RTE_FLOW_ERROR_TYPE_UNSPECIFIED, NULL, "flush flow by port error.");
        return -EPERM;
    }

    ret = hinic3_mega_flow_flush_by_port(port_id, error);
    if (ret != 0) {
        HINIC3_LOG(ERR, FLOW, "flush mega flow by port error!");
        rte_flow_error_set(error, EPERM, RTE_FLOW_ERROR_TYPE_UNSPECIFIED, NULL, "flush mega flow by port error.");
        return -EPERM;
    }

    return 0;
}

static int
hinic3_eth_flow_destroy_ecology(struct rte_eth_dev *dev, struct rte_flow *flow, struct rte_flow_error *error)
{
    int ret = 0;

    if (flow->flags.is_dumb == 1) {
        hinic3_rte_or_fuzzy_flow_dealloc(flow);
        return 0;
    } else if (flow->flags.is_multi_level_qos == 1) {
        ret = hinic3_qos_flow_delete(flow);
        if (ret != 0) {
            return rte_flow_error_set(error, EPERM, RTE_FLOW_ERROR_TYPE_UNSPECIFIED, NULL, "del multi qos error.");
        }
        return 0;
    }

    return hinic3_flow_emc_destroy(dev, flow, error);
}

int
hinic3_eth_flow_destroy(struct rte_eth_dev *dev, struct rte_flow *flow, struct rte_flow_error *error)
{
    int ret = 0;
    if (dev == NULL || flow == NULL || error == NULL) {
        hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_FLOW_DEL_INPUT_NULL, 1);
        return -EINVAL;
    }

    HINIC3_LOG_HINIC_FLOW_API_LOG_NO_LOG(ret, HINIC3_RTE_FLOW_DELETE,
        hinic3_eth_flow_destroy_ecology(dev, flow, error));
    return ret;
}

int
hinic3_eth_get_aged_flow(struct rte_eth_dev *dev, void **context, uint32_t nb_contexts, struct rte_flow_error *err)
{
    if (hinic3_is_ethdev_valid(dev) == false)
        return -EINVAL;

    struct hinic3_bond_dev* bond_dev = hinic3_ethdev_get_bond_private(dev);
    struct hinic3_vf_dev* vf_dev = hinic3_ethdev_get_vf_private(dev);
    struct rte_aged_flow_list* aged_flow_list = NULL;
    struct rte_aged_flow_list_node* iter = NULL;
    struct rte_aged_flow_list_node* next = NULL;
    int aged_flow_num = 0;

    if (nb_contexts > 0 && context == NULL)
        return rte_flow_error_set(err, EINVAL, RTE_FLOW_ERROR_TYPE_UNSPECIFIED, NULL, "empty context");

    if (hinic3_is_bond_by_prefix(bond_dev->vport_id) == false)
        aged_flow_list = &vf_dev->aged_flow_list;
    else
        aged_flow_list = &bond_dev->aged_flow_list;

    if (nb_contexts == 0) {
        return hinic3_list_size(&aged_flow_list->list_head);
    } else {
        LIST_FOR_EACH_SAFE(iter, next, node, &aged_flow_list->list_head) {
            if (aged_flow_num == (int)nb_contexts) {
                return aged_flow_num;
            }
            hinic3_pthread_mutex_lock(&aged_flow_list->mutex);
            if (iter->flow->context == NULL) {
                context[aged_flow_num] = iter->flow;
            } else {
                context[aged_flow_num] = iter->flow->context;
            }
            aged_flow_num++;
            hinic3_pthread_mutex_unlock(&aged_flow_list->mutex);
        }
    }
    return aged_flow_num;
}

static bool
hinic3_check_offload_disable(const struct hinic3_flow_agent_db *hinic3_db)
{
    if (hinic3_db->operate_disable == 1) {
        hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_OFFLOAD_DISABLE, 1);
        return true;
    }
    return false;
}

static uint32_t
hinic3_get_flow_operation_type(const struct rte_flow_attr *attr)
{
    uint32_t flow_type = (attr->reserved >> HINIC3_FLOW_ATTR_OPERATION_OFFSET) & HINIC3_FLOW_ATTR_OPERATION_MASK;
    return flow_type;
}

static bool
hinic3_is_three_layer_mega_flow_key(const struct rte_flow_item pattern[])
{
    uint32_t feature_value = 0;
    uint32_t rx_flow = (1 << RTE_FLOW_ITEM_TYPE_ETH) | (1 << RTE_FLOW_ITEM_TYPE_VXLAN);
    uint32_t tx_flow = (1 << RTE_FLOW_ITEM_TYPE_PORT_ID);
    const struct rte_flow_item *item = NULL;

    item = next_no_end_pattern(pattern, NULL);
    while (item != NULL) {
        if (item->spec == NULL) {
            HINIC3_LOG(WARNING, FLOW, "item type is %d, but item spec is null!", item->type);
            item = next_no_end_pattern(pattern, item);
            continue;
        }
        feature_value |= (1 << item->type);
        item = next_no_end_pattern(pattern, item);
    }

    if ((feature_value == rx_flow) || (feature_value == tx_flow))
        return true;
    else
        return false;
}

static struct rte_flow *
hinic3_offload_flow_ecology(const struct rte_flow_item pattern[], const struct rte_flow_action actions[],
    struct hinic3_flow_agent_db *hw_offload, struct rte_flow_error *error, uint8_t table_id)
{
    struct rte_flow *flow = NULL;

    if (hinic3_check_fuzzy_flow_switch() == true && hinic3_is_three_layer_mega_flow_key(pattern) == true)
        flow = hinic3_mega_flow_offload(pattern, error);
    else
        flow = hinic3_offload_flow(pattern, actions, hw_offload, error, table_id);

    return flow;
}

static struct rte_flow *
hinic3_eth_flow_create_ecology(struct rte_eth_dev *dev, const struct rte_flow_attr *attr,
    const struct rte_flow_item *pattern, const struct rte_flow_action *actions, struct rte_flow_error *error)
{
    uint16_t port_id;
    struct rte_flow *flow = NULL;
    uint8_t table_id = attr->group;
    if (attr->ingress == 1) {
        flow = hinic3_rte_flow_alloc();
        if (flow == NULL) {
            hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_EMC_FLOW_ALLOC, 1);
            return NULL;
        }
        flow->flags.is_dumb = 1;
        return flow;
    } else if (actions->type == RTE_FLOW_ACTION_TYPE_METER) {
        flow = hinic3_set_multi_qos(pattern, actions);
        if (flow == NULL) {
            hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_SET_MULTI_QOS, 1);
            rte_flow_error_set(error, EPERM, RTE_FLOW_ERROR_TYPE_UNSPECIFIED, NULL, "set multi qos error.");
        }
        return flow;
    }

    if (hinic3_get_id_from_dev(dev, &port_id) != 0) {
        hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_EMC_FLOW_GET_PORT_ID, 1);
        rte_flow_error_set(error, EPERM, RTE_FLOW_ERROR_TYPE_UNSPECIFIED, NULL, "get port id by dev error.");
        return NULL;
    }

    struct hinic3_dp_extend_info *offload_extend_info = hinic3_get_offload_extend_info();
    if (HINIC3_UNLIKELY(offload_extend_info == NULL || offload_extend_info->hw_offload == NULL)) {
        hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_ETH_FLOW_CREATE_ECOLOGY_NO_INIT, 1);
        rte_flow_error_set(error, EPERM, RTE_FLOW_ERROR_TYPE_UNSPECIFIED, NULL, "get emc db info failed");
        return NULL;
    }

    if (hinic3_check_offload_disable(offload_extend_info->hw_offload) == true) {
        rte_flow_error_set(error, EPERM, RTE_FLOW_ERROR_TYPE_UNSPECIFIED, NULL, "flow offload disable");
        return NULL;
    }

    if (hinic3_get_flow_operation_type(attr) == HINIC3_EMC_FLOW_MODIFY_MODE)
        flow = hinic3_modify_flow(pattern, actions, offload_extend_info->hw_offload, error);
    else
        flow = hinic3_offload_flow_ecology(pattern, actions, offload_extend_info->hw_offload, error, table_id);

    if (flow != NULL)
        flow->port_id = port_id;

    return flow;
}

struct rte_flow *
hinic3_eth_flow_create(struct rte_eth_dev *dev, const struct rte_flow_attr *attr,
    const struct rte_flow_item *pattern, const struct rte_flow_action *actions, struct rte_flow_error *error)
{
    struct rte_flow *flow = NULL;
    if (dev == NULL || pattern == NULL || actions == NULL || error == NULL || attr == NULL) {
        hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_FLOW_CREATE_INPUT_NULL, 1);
        return NULL;
    }

    if (HINIC3_UNLIKELY(hinic3_is_flow_debug()))
        hinic3_flow_log_original_entry(pattern, actions);

    HINIC3_LOG_HINIC_FLOW_ISNULL_API_LOG_NO_LOG(flow, HINIC3_RTE_FLOW_CREATE,
        hinic3_eth_flow_create_ecology(dev, attr, pattern, actions, error));
    return flow;
}

int
hinic3_eth_flow_query(struct rte_eth_dev *dev, struct rte_flow *flow, const struct rte_flow_action *actions,
    void *data, struct rte_flow_error *error)
{
    int ret = 0;
    if (dev == NULL || flow == NULL || data == NULL || actions == NULL || error == NULL) {
        hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_FLOW_QUERY_INPUT_NULL, 1);
        return -EINVAL;
    }

    HINIC3_LOG_HINIC_FLOW_API_LOG_NO_LOG(ret, HINIC3_RTE_FLOW_QUERY,
        hinic3_eth_flow_query_ecology(dev, flow, actions, data, error));
    return ret;
}
