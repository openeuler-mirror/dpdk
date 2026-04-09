/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2022 Huawei Technologies Co., Ltd
 */

#include "errno.h"
#include "hinic3_log.h"
#include "hinic3_mutex.h"
#include "hinic3_meminfo.h"
#include "rte_mtr_driver.h"
#include "hinic3_vf_port_qos_public.h"
#include "hinic3_mtr_profile.h"

#define HINIC3_METER_PROFILE_MAX 2048

struct hinic3_mtr_profile_list g_hinic3_mtr_profile_list = {0};

struct hinic3_mtr_profile_list *hinic3_profile_list_get(void)
{
    return &g_hinic3_mtr_profile_list;
}

void hinic3_mtr_profile_list_lock(void)
{
    hinic3_pthread_mutex_lock(&g_hinic3_mtr_profile_list.mutex);
}

void hinic3_mtr_profile_list_unlock(void)
{
    hinic3_pthread_mutex_unlock(&g_hinic3_mtr_profile_list.mutex);
}

struct hinic3_mtr_profile_node *hinic3_mtr_profile_find(uint32_t profile_id)
{
    struct hinic3_mtr_profile_node *iter = NULL;

    LIST_FOR_EACH(iter, node, &g_hinic3_mtr_profile_list.node) {
        if (iter->profile_id == profile_id) {
            return iter;
        }
    }
    return NULL;
}

int hinic3_multi_meter_profile_del(struct rte_eth_dev *dev, uint32_t profile_id, struct rte_mtr_error *error)
{
    if (dev == NULL || error == NULL) {
        HINIC3_LOG(ERR, QOS, "Profile delete: Profile delete parameter is empty.");
        return -rte_mtr_error_set(error, EPERM, RTE_MTR_ERROR_TYPE_METER_PROFILE, NULL,
            "Profile delete parameter is empty.");
    }

    struct hinic3_mtr_profile_node *profile = NULL;

    hinic3_mtr_profile_list_lock();
    profile = hinic3_mtr_profile_find(profile_id);
    if (profile == NULL) {
        hinic3_mtr_profile_list_unlock();
        HINIC3_LOG(ERR, QOS, "Profile delete: Profile id is no exist.");
        return -rte_mtr_error_set(error, ENOTSUP, RTE_MTR_ERROR_TYPE_METER_PROFILE_ID, NULL,
            "Profile id is no exist.");
    }

    if (profile->used_num > 0) {
        hinic3_mtr_profile_list_unlock();
        HINIC3_LOG(ERR, QOS, "Profile delete: Profile id is being used.");
        return -rte_mtr_error_set(error, ENOTSUP, RTE_MTR_ERROR_TYPE_METER_PROFILE_ID, NULL,
            "Profile id is being used.");
    }

    hinic3_list_remove(&profile->node);
    g_hinic3_mtr_profile_list.length--;
    hinic3_free(profile);
    hinic3_mtr_profile_list_unlock();
    return 0;
}

static int hinic3_copy_profile(struct qos_single_value *new_profile,
    struct rte_mtr_meter_profile *old_profile)
{
    int packet_mode = old_profile->packet_mode;
    if (packet_mode != QOS_BW_TYPE && packet_mode != QOS_PPS_TYPE) {
        HINIC3_LOG(ERR, QOS, "Profile add: Meter profile packet mode is invalid.");
        return -1;
    }
    /* The unit is converted from large B to kb */
    switch (old_profile->alg) {
        case RTE_MTR_SRTCM_RFC2697:
            new_profile->min_rate = (old_profile->alg == RTE_MTR_SRTCM_RFC2697) ? (old_profile->srtcm_rfc2697.cir)
                                                                                : (old_profile->trtcm_rfc2698.cir);
            new_profile->min_burst = (old_profile->alg == RTE_MTR_SRTCM_RFC2697) ? (old_profile->srtcm_rfc2697.cbs)
                                                                                 : (old_profile->trtcm_rfc2698.cbs);
            new_profile->max_rate = (old_profile->alg == RTE_MTR_SRTCM_RFC2697) ? (old_profile->srtcm_rfc2697.cir)
                                                                                : (old_profile->trtcm_rfc2698.cir);
            new_profile->max_burst = old_profile->srtcm_rfc2697.ebs;
            new_profile->is_RFC2697 = true;
            break;
        case RTE_MTR_TRTCM_RFC2698:
            new_profile->min_rate = (old_profile->alg == RTE_MTR_SRTCM_RFC2697) ? (old_profile->srtcm_rfc2697.cir)
                                                                                : (old_profile->trtcm_rfc2698.cir);
            new_profile->min_burst = (old_profile->alg == RTE_MTR_SRTCM_RFC2697) ? (old_profile->srtcm_rfc2697.cbs)
                                                                                 : (old_profile->trtcm_rfc2698.cbs);
            new_profile->max_rate = old_profile->trtcm_rfc2698.pir;
            new_profile->max_burst = old_profile->trtcm_rfc2698.pbs;
            new_profile->is_RFC2697 = false;
            break;
        default:
            HINIC3_LOG(ERR, QOS, "Profile add: Meter profile alg type is invalid.");
            return -1;
    }
    new_profile->packet_mode = packet_mode;
    switch (new_profile->packet_mode) {
        case QOS_BW_TYPE:
            new_profile->max_rate = (new_profile->max_rate * BYTE_TO_BIT) / KB_TO_B;
            new_profile->max_burst = (new_profile->max_burst * BYTE_TO_BIT) / KB_TO_B;
            new_profile->min_rate = (new_profile->min_rate * BYTE_TO_BIT) / KB_TO_B;
            new_profile->min_burst = (new_profile->min_burst * BYTE_TO_BIT) / KB_TO_B;
            return check_vf_bw_args(*new_profile);
        case QOS_PPS_TYPE:
            return check_vf_pps_args(*new_profile);
        default:
            return -1;
    }
}

int hinic3_multi_meter_profile_add(struct rte_eth_dev *dev, uint32_t meter_profile_id,
    struct rte_mtr_meter_profile *profile, struct rte_mtr_error *error)
{
    if (dev == NULL || profile == NULL || error == NULL) {
        HINIC3_LOG(ERR, QOS, "Profile add: Meter profile parameter is empty.");
        return -rte_mtr_error_set(error, EPERM, RTE_MTR_ERROR_TYPE_MTR_PARAMS, NULL,
            "Meter profile parameter is empty.");
    }

    if (g_hinic3_mtr_profile_list.length > HINIC3_METER_PROFILE_MAX) {
        HINIC3_LOG(ERR, QOS, "Profile add: Meter profile reaches the upper limit.(2048)");
        return -rte_mtr_error_set(error, ENOMEM, RTE_MTR_ERROR_TYPE_METER_PROFILE, NULL,
            "Meter profile reaches the upper limit.");
    }

    int ret;
    struct hinic3_mtr_profile_node *profile_node = NULL;

    hinic3_mtr_profile_list_lock();
    profile_node = hinic3_mtr_profile_find(meter_profile_id);
    if (profile_node != NULL) {
        hinic3_mtr_profile_list_unlock();
        HINIC3_LOG(ERR, QOS, "Profile add: Profile already exist.");
        return -rte_mtr_error_set(error, EEXIST, RTE_MTR_ERROR_TYPE_METER_PROFILE_ID, NULL,
            "Profile already exist.");
    }

    profile_node = hinic3_calloc(1, sizeof(struct hinic3_mtr_profile_node), HINIC3_QOS);
    if (profile_node == NULL) {
        hinic3_mtr_profile_list_unlock();
        HINIC3_LOG(ERR, QOS, "Profile add: Profile memory alloc failed.");
        return -rte_mtr_error_set(error, ENOMEM, RTE_MTR_ERROR_TYPE_UNSPECIFIED, NULL,
            "Profile memory alloc failed.");
    }

    ret = hinic3_copy_profile(&profile_node->profile, profile);
    if (ret != 0) {
        hinic3_free(profile_node);
        hinic3_mtr_profile_list_unlock();
        return -rte_mtr_error_set(error, EINVAL, RTE_MTR_ERROR_TYPE_METER_PROFILE, NULL,
            "Meter profile parameter is invalid.");
    }
    profile_node->profile_id = meter_profile_id;
    profile_node->used_num = 0;
    hinic3_list_init(&profile_node->node);
    hinic3_list_insert(&g_hinic3_mtr_profile_list.node, &profile_node->node);
    g_hinic3_mtr_profile_list.length++;
    hinic3_mtr_profile_list_unlock();
    return 0;
}

void hinic3_meter_profile_list_init(void)
{
    hinic3_list_init(&g_hinic3_mtr_profile_list.node);
    hinic3_pthread_mutex_init(&g_hinic3_mtr_profile_list.mutex);
    g_hinic3_mtr_profile_list.length = 0;
}
