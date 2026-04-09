/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2022 Huawei Technologies Co., Ltd
 */

#include "errno.h"
#include "hinic3_log.h"
#include "rte_flow.h"
#include "rte_mtr_driver.h"
#include "hinic3_meminfo.h"
#include "hinic3_mutex.h"
#include "hinic3_mtr_policy.h"

#define HINIC3_METER_POLICY_MAX 1024

struct hinic3_mtr_policy_list g_mtr_policy_list = { 0 };

struct hinic3_mtr_policy_list *hinic3_policy_list_get(void)
{
    return &g_mtr_policy_list;
}

void hinic3_mtr_policy_list_lock(void)
{
    hinic3_pthread_mutex_lock(&g_mtr_policy_list.mutex);
}

void hinic3_mtr_policy_list_unlock(void)
{
    hinic3_pthread_mutex_unlock(&g_mtr_policy_list.mutex);
}

struct hinic3_mtr_policy_node *hinic3_mtr_policy_find(uint32_t policy_id)
{
    struct hinic3_mtr_policy_node *iter = NULL;

    LIST_FOR_EACH(iter, node, &g_mtr_policy_list.node) {
        if (iter->policy_id == policy_id) {
            return iter;
        }
    }
    return NULL;
}

static void hinic3_meter_color_action_parse(const struct rte_flow_action *actions, struct hinic3_mtr_policy_node *fmp)
{
    const struct rte_flow_action *action = NULL;
    const struct rte_flow_action_meter *mtr = NULL;

    for (action = actions; action->type != RTE_FLOW_ACTION_TYPE_END; action++) {
        if (action->type == RTE_FLOW_ACTION_TYPE_METER && action->conf != NULL) {
            mtr = (const struct rte_flow_action_meter *)action->conf;
            fmp->next_meter_id = mtr->mtr_id;
            fmp->has_next_meter = true;
        }
    }
}

static int hinic3_fill_policy_actions(struct hinic3_mtr_policy_node *fmp, struct rte_mtr_meter_policy_params *policy)
{
    const struct rte_flow_action **actions = (const struct rte_flow_action**)&policy->actions;
    for (int i = 0; i < RTE_COLORS; i++) {
        if (actions[i] != NULL) {
            hinic3_meter_color_action_parse(actions[i], fmp);
        }
    }
    return 0;
}

int hinic3_meter_policy_delete(struct rte_eth_dev *dev, uint32_t policy_id, struct rte_mtr_error *error)
{
    if (dev == NULL || error == NULL) {
        HINIC3_LOG(ERR, QOS, "Policy delete: Policy parameter is empty.");
        return -rte_mtr_error_set(error, EPERM, RTE_MTR_ERROR_TYPE_METER_POLICY, NULL,
            "Policy delete: parameter is empty.");
    }

    struct hinic3_mtr_policy_node *policy = NULL;

    hinic3_mtr_policy_list_lock();
    policy = hinic3_mtr_policy_find(policy_id);
    if (policy == NULL) {
        hinic3_mtr_policy_list_unlock();
        HINIC3_LOG(ERR, QOS, "Policy delete: Policy id is no exist.");
        return -rte_mtr_error_set(error, ENOTSUP, RTE_MTR_ERROR_TYPE_METER_POLICY_ID, NULL,
            "Policy id is invalid.");
    }

    if (policy->used_num > 0) {
        hinic3_mtr_policy_list_unlock();
        HINIC3_LOG(ERR, QOS, "Policy delete: Policy id is being used.");
        return -rte_mtr_error_set(error, ENOTSUP, RTE_MTR_ERROR_TYPE_METER_POLICY_ID, NULL,
            "Policy id is being used.");
    }

    hinic3_list_remove(&policy->node);
    g_mtr_policy_list.length--;
    hinic3_free(policy);

    hinic3_mtr_policy_list_unlock();
    return 0;
}

int hinic3_meter_policy_add(struct rte_eth_dev *eth_dev, uint32_t policy_id,
    struct rte_mtr_meter_policy_params *policy, struct rte_mtr_error *error)
{
    if (eth_dev == NULL || policy == NULL || error == NULL) {
        HINIC3_LOG(ERR, QOS, "Policy add: Meter policy parameter is empty.");
        return -rte_mtr_error_set(error, EPERM, RTE_MTR_ERROR_TYPE_METER_POLICY, NULL,
            "Meter policy parameter is empty.");
    }

    if (g_mtr_policy_list.length > HINIC3_METER_POLICY_MAX) {
        HINIC3_LOG(ERR, QOS, "Policy add: Meter policy reaches the upper limit.(1024)");
        return -rte_mtr_error_set(error, ENOMEM, RTE_MTR_ERROR_TYPE_METER_POLICY, NULL,
            "Meter policy reaches the upper limit.");
    }

    int ret;
    struct hinic3_mtr_policy_node *policy_node = NULL;

    hinic3_mtr_policy_list_lock();
    policy_node = hinic3_mtr_policy_find(policy_id);
    if (policy_node != NULL) {
        hinic3_mtr_policy_list_unlock();
        HINIC3_LOG(ERR, QOS, "Policy add: Policy already exist.");
        return -rte_mtr_error_set(error, EEXIST, RTE_MTR_ERROR_TYPE_METER_POLICY_ID, NULL,
            "Policy already exist.");
    }

    policy_node = hinic3_calloc(1, sizeof(struct hinic3_mtr_policy_node), HINIC3_QOS);
    if (policy_node == NULL) {
        hinic3_mtr_policy_list_unlock();
        HINIC3_LOG(ERR, QOS, "Policy add: Policy memory alloc failed.");
        return -rte_mtr_error_set(error, ENOMEM, RTE_MTR_ERROR_TYPE_UNSPECIFIED, NULL,
            "Policy memory failed.");
    }

    policy_node->has_next_meter = false;

    ret = hinic3_fill_policy_actions(policy_node, policy);
    if (ret != 0) {
        hinic3_free(policy_node);
        hinic3_mtr_policy_list_unlock();
        HINIC3_LOG(ERR, QOS, "Policy add: Polict action is invalid.");
        return -rte_mtr_error_set(error, EPERM, RTE_MTR_ERROR_TYPE_METER_POLICY, NULL,
            "Polict action is invalid.");
    }

    hinic3_list_init(&policy_node->node);
    policy_node->used_num = 0;
    policy_node->policy_id = policy_id;
    hinic3_list_insert(&g_mtr_policy_list.node, &policy_node->node);
    g_mtr_policy_list.length++;
    hinic3_mtr_policy_list_unlock();

    return 0;
}

void hinic3_mtr_policy_list_init(void)
{
    hinic3_pthread_mutex_init(&g_mtr_policy_list.mutex);
    hinic3_list_init(&g_mtr_policy_list.node);
    g_mtr_policy_list.length = 0;
}
