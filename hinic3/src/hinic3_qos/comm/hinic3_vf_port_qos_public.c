/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2022 Huawei Technologies Co., Ltd
 */

#include "hinic3_vf_port_qos_public.h"
#include "hinic3_flow_qos.h"
#include <ethdev_pci.h>
#include "rte_kvargs.h"
#include "rte_devargs.h"
#include "rte_ethdev.h"
#include "rte_string_fns.h"
#include "hinic3_util.h"
#include "hinic3_tlv_key.h"
#include "hinic3_meminfo.h"
#include "hinic3_flow_qos.h"
#include "hinic3_mtr.h"
#include "hinic3_smap.h"

static const char *const hinic3_bond_valid_arguments[] = {
    HINIC3_BOND_NAME,
    HINIC3_PORT_PCI_ID,
    HINIC3_PORT_MAC,
    HINIC3_MAX_QUEUE_NUM,
    HINIC3_REPRESENTOR_ID,
    HINIC3_SHARE_UPCALL,
    HINIC3_PRIORITY_UPCALL,
    HINIC3_PORT_VIRTIO_QUEUE_DEPTH,
    HINIC3_BOND_ARG_SLAVE_MTU_STR,
    NULL
};

group_qos_type_key g_group_qos_type_key = {
    .qos_type_key = {
        HINIC3_GROUP_QOS_BW_INGRESS,
        HINIC3_GROUP_QOS_PPS_INGRESS,
        HINIC3_GROUP_QOS_BW_EGRESS,
        HINIC3_GROUP_QOS_PPS_EGRESS,
    }
};

group_qos_type_key *hinic3_get_qos_type_key(void)
{
    return &g_group_qos_type_key;
}

void hinic3_qos_init(void)
{
    hinic3_mtr_map_init();
    hinic3_multi_meter_init();
    hinic3_qos_array_init();
}

int check_vf_bw_args(struct qos_single_value vm_qos)
{
    uint64_t max_rate = vm_qos.max_rate;
    uint64_t max_burst = vm_qos.max_burst;
    uint64_t min_rate = vm_qos.min_rate;
    uint64_t min_burst = vm_qos.min_burst;

    if (vm_qos.is_RFC2697 == true) {
        if ((min_rate < MIN_BW_RATE_QOS) || (min_rate >= MAX_BW_RATE_QOS)) {
            HINIC3_LOG(ERR, QOS, "cir range is incorrect.");
            return -1;
        }

        if ((min_burst < MIN_BW_RATE_QOS) || (min_burst > MAX_BW_BURST_QOS)) {
            HINIC3_LOG(ERR, QOS, "cbs range is incorrect.");
            return -1;
        }

        if ((max_burst + min_burst) > MAX_BW_BURST_QOS) {
            HINIC3_LOG(ERR, QOS, "cbs and ebs range is incorrect.");
            return -1;
        }
    } else {
        if ((min_rate < MIN_BW_RATE_QOS) || (min_burst < MIN_BW_RATE_QOS)) {
            HINIC3_LOG(ERR, QOS, "cir or cbs range is incorrect.");
            return -1;
        }

        if ((max_rate < min_rate) || (max_rate >= MAX_BW_RATE_QOS)) {
            HINIC3_LOG(ERR, QOS, "cir or pir range is incorrect.");
            return -1;
        }

        if ((max_burst < min_burst) || (max_burst > MAX_BW_BURST_QOS)) {
            HINIC3_LOG(ERR, QOS, "cbs or pbs range is incorrect.");
            return -1;
        }
    }

    return 0;
}

int check_vf_pps_args(struct qos_single_value vm_qos)
{
    uint64_t max_rate = vm_qos.max_rate;
    uint64_t max_burst = vm_qos.max_burst;
    uint64_t min_rate = vm_qos.min_rate;
    uint64_t min_burst = vm_qos.min_burst;

    if ((min_rate < MIN_PPS_RATE_QOS) || (min_burst < MIN_PPS_RATE_QOS)) {
        HINIC3_LOG(ERR, QOS, "cir or cbs range is incorrect.");
        return -1;
    }

    if ((max_rate < min_rate) || (max_rate >= MAX_PPS_RATE_QOS)) {
        HINIC3_LOG(ERR, QOS, "cir or pir range is incorrect.");
        return -1;
    }

    if ((max_burst < min_burst) || (max_burst > MAX_PPS_BURST_QOS)) {
        HINIC3_LOG(ERR, QOS, "cbs or pbs range is incorrect.");
        return -1;
    }

    return 0;
}

int hinic3_port_mgmt_set_qos_id(uint16_t vport_id, uint16_t bucket_id)
{
    int ret;
    struct smap args;
    struct smap unset_args;

    if (bucket_id == UNBIND_QOS_ID) {
        HINIC3_LOG(DEBUG, QOS, "remove vport_id %u qos_id", vport_id);
    } else {
        HINIC3_LOG(DEBUG, QOS, "set vport_id %u qos_id %u", vport_id, bucket_id);
    }

    hinic3_smap_init(&args);
    hinic3_smap_init(&unset_args);

    hinic3_smap_add_format(HINIC3_QOS, &args, HINIC3_PORT_BUCKET_ID, "%u", bucket_id);
    ret = hinic3_port_mgmt_set(vport_id, &args, &unset_args);
    if (ret != 0) {
        HINIC3_LOG(ERR, QOS, "failed to set qos id for vport_id (%u)", vport_id);
    }

    hinic3_smap_destroy(&args);
    hinic3_smap_destroy(&unset_args);

    return ret;
}

bool is_hinic3_vf_dev(uint16_t port_id)
{
    struct rte_kvargs *kvlist = NULL;
    struct rte_eth_dev_info info = {0};

    int ret = rte_eth_dev_info_get(port_id, &info);
    if (ret != 0) {
        HINIC3_LOG(ERR, QOS, "when check hinic3 vf dev, get eth dev info fail!");
        return false;
    }
    const char *driver_name = info.driver_name;
    if (driver_name == NULL || strlen(driver_name) == 0) {
        HINIC3_LOG(ERR, QOS, "when check hinic3 vf dev, get driver name fail!");
        return false;
    }

    struct rte_devargs *devargs = info.device->devargs;
    kvlist = rte_kvargs_parse((devargs == NULL ? NULL : devargs->args), hinic3_bond_valid_arguments);
    if (kvlist == NULL) {
        HINIC3_LOG(ERR, QOS, "kvargs parse failed!");
        return false;
    }

    if (strcmp(driver_name, HINIC3_ETH_VDEV_DRV_NAME) == 0 &&
        rte_kvargs_count(kvlist, HINIC3_BOND_NAME) == 0) {
        rte_kvargs_free(kvlist);
        return true;
    }

    rte_kvargs_free(kvlist);
    return false;
}

int hinic3_port_qos_limit_set(uint16_t port_id, uint16_t dir, uint16_t type, const struct qos_single_value *qos_value)
{
    int ret;
    struct hinic3_drv_ops *ops = NULL;

    if (qos_value->max_rate == 0) {
        HINIC3_LOG(INFO, QOS, "remove config port_id dir %u type %u port_id %u.", dir, type, port_id);
    } else {
        HINIC3_LOG(INFO, QOS, "set config port_id dir %u type %u port_id %u value.", dir, type, port_id);
    }

    ops = hinic3_get_drv_ops();
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_qos_vport_limit_set, HINIC3_DRV_FUNC_NO_PTR);
    ret = ops->hovs_qos_vport_limit_set(port_id, dir, type, qos_value->max_rate, qos_value->max_burst,
        qos_value->min_rate, qos_value->min_burst);
    return ret;
}

int hinic3_port_qos_limit_get(uint16_t port_id, uint16_t dir, uint16_t type, struct qos_single_value *qos_value)
{
    int ret;
    struct hinic3_drv_ops *ops = NULL;

    ops = hinic3_get_drv_ops();
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_qos_vport_limit_get, HINIC3_DRV_FUNC_NO_PTR);
    ret = ops->hovs_qos_vport_limit_get(port_id, dir, type, &qos_value->max_rate, &qos_value->max_burst,
        &qos_value->min_rate, &qos_value->min_burst);
    return ret;
}

int hinic3_vm_qos_limit_set(uint16_t qos_id, uint16_t dir, uint16_t type, struct qos_single_value qos_value)
{
    int ret;
    struct hinic3_drv_ops *ops = NULL;

    if (qos_value.max_rate == 0) {
        HINIC3_LOG(DEBUG, QOS, "remove config qos_id dir %u type %u qos_id %u.", dir, type, qos_id);
    } else {
        HINIC3_LOG(DEBUG, QOS, "set config qos_id dir %u type %u qos_id %u value.", dir, type, qos_id);
    }

    ops = hinic3_get_drv_ops();
    if (qos_value.is_RFC2697 == true) {
        HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_qos_vm_srtcm_limit_set, HINIC3_DRV_FUNC_NO_PTR);
        ret = ops->hovs_qos_vm_srtcm_limit_set(qos_id, dir, type, qos_value.min_rate, qos_value.min_burst,
            qos_value.max_burst);
    } else {
        HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_qos_vm_limit_set, HINIC3_DRV_FUNC_NO_PTR);
        ret = ops->hovs_qos_vm_limit_set(qos_id, dir, type, qos_value.max_rate, qos_value.max_burst, qos_value.min_rate,
            qos_value.min_burst);
    }
    return ret;
}

int hinic3_vm_qos_limit_get(uint16_t qos_id, uint16_t dir, uint16_t type, struct qos_single_value *qos_value)
{
    int ret;
    struct hinic3_drv_ops *ops = NULL;

    if (qos_value == NULL) {
        return -1;
    }

    ops = hinic3_get_drv_ops();
    if (qos_value->is_RFC2697 == true) {
        HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_qos_vm_srtcm_limit_get, HINIC3_DRV_FUNC_NO_PTR);
        ret = ops->hovs_qos_vm_srtcm_limit_get(qos_id, dir, type, &qos_value->min_rate, &qos_value->min_burst,
            &qos_value->max_burst);
    } else {
        HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_qos_vm_limit_get, HINIC3_DRV_FUNC_NO_PTR);
        ret = ops->hovs_qos_vm_limit_get(qos_id, dir, type, &qos_value->max_rate, &qos_value->max_burst,
            &qos_value->min_rate, &qos_value->min_burst);
    }
    return ret;
}

int
hinic3_net_qos_limit_set(uint16_t host_id HINIC3_UNUSED, uint16_t dir, uint16_t type,
    uint64_t max_rate, uint64_t max_burst)
{
    int ret;
    struct hinic3_drv_ops *ops = NULL;
    if (max_rate == 0) {
        HINIC3_LOG(INFO, QOS, "remove config qos_id dir %u type %u.", dir, type);
    } else {
        HINIC3_LOG(INFO, QOS, "set config qos_id dir %u type %u value.", dir, type);
    }

    ops = hinic3_get_drv_ops();
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_qos_net_limit_set, HINIC3_DRV_FUNC_NO_PTR);
    ret = ops->hovs_qos_net_limit_set(0, dir, type, max_rate, max_burst);
    return ret;
}

int
hinic3_net_qos_limit_get(uint16_t host_id HINIC3_UNUSED, uint16_t dir, uint16_t type,
    uint64_t *max_rate, uint64_t *max_burst)

{
    int ret;
    struct hinic3_drv_ops *ops = NULL;
    ops = hinic3_get_drv_ops();
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_qos_net_limit_get, HINIC3_DRV_FUNC_NO_PTR);
    ret = ops->hovs_qos_net_limit_get(0, dir, type, max_rate, max_burst);
    return ret;
}

int hinic3_flow_qos_limit_get(uint16_t qos_id, uint16_t type, uint64_t *max_rate, uint64_t *max_burst,
                             uint64_t *min_rate, uint64_t *min_burst)
{
    int ret;
    struct hinic3_drv_ops *ops = NULL;
    ops = hinic3_get_drv_ops();
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_qos_flow_limit_get, HINIC3_DRV_FUNC_NO_PTR);
    ret = ops->hovs_qos_flow_limit_get(qos_id, type, max_rate, max_burst, min_rate, min_burst);
    return ret;
}

int hinic3_flow_qos_limit_set(uint16_t qos_id, uint16_t type, uint64_t max_rate, uint64_t max_burst,
                             uint64_t min_rate, uint64_t min_burst)
{
    int ret;
    struct hinic3_drv_ops *ops = NULL;
    ops = hinic3_get_drv_ops();
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_qos_flow_limit_set, HINIC3_DRV_FUNC_NO_PTR);
    ret = hinic3_hqos_statistics_clear(QOS_TYPE_FLOW_LIMIT, 0, &qos_id, 1); // 下发限速前，先清除统计信息，如果下发失败，不影响限速，只打印日志
    if (ret != 0)
    {
        HINIC3_LOG(ERR, QOS, "Failed to clear the flow qos statistics. qos id is %u.", qos_id);
    }
    ret = ops->hovs_qos_flow_limit_set(qos_id, type, max_rate, max_burst, min_rate, min_burst);
    return ret;
}

int hinic3_vf_qos_statistics_get_all_batch(uint16_t *qos_array, struct hovs_qos_stats_batch_all *stats, size_t cnt)
{
    int ret;
    struct hinic3_drv_ops *ops = NULL;

    if ((qos_array == NULL) || (stats == NULL)) {
        return -1;
    }

    ops = hinic3_get_drv_ops();
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_qos_statistics_get_all_batch, HINIC3_DRV_FUNC_NO_PTR);
    ret = ops->hovs_qos_statistics_get_all_batch(VM_LEVEL, qos_array, stats, cnt);
    return ret;
}

int hinic3_vf_qos_statistics_clear_batch(uint16_t *qos_array, size_t cnt)
{
    int ret;
    struct hinic3_drv_ops *ops = NULL;

    if (qos_array == NULL) {
        return -1;
    }

    ops = hinic3_get_drv_ops();
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_qos_statistics_clear_batch, HINIC3_DRV_FUNC_NO_PTR);
    ret = ops->hovs_qos_statistics_clear_batch(VM_LEVEL, qos_array, cnt);
    return ret;
}

int hinic3_vf_qos_statistics_clear(uint16_t *vport_id)
{
    int ret;
    struct hinic3_drv_ops *ops = NULL;

    ops = hinic3_get_drv_ops();
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_qos_statistics_clear_batch, HINIC3_DRV_FUNC_NO_PTR);
    ret = ops->hovs_qos_statistics_clear_batch(QOS_TYPE_FUNC_LIMIT, vport_id, 1);
    return ret;
}

/* net级，dis参数传0 */
int hinic3_hqos_statistics_clear(enum qos_type_limit type, uint16_t dir, uint16_t *ids, size_t cnt)
{
    int ret;
    struct hinic3_drv_ops *ops = NULL;
    ops = hinic3_get_drv_ops();
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_hqos_statistics_clear_batch, HINIC3_DRV_FUNC_NO_PTR);
    ret = ops->hovs_hqos_statistics_clear_batch(type, dir, ids, cnt);
    return ret;
}

int hinic3_hqos_statistics_get_all_batch(enum qos_type_limit type, uint16_t *ids,
    struct hovs_hqos_stats_batch_all *stats, size_t cnt)
{
    int ret;
    struct hinic3_drv_ops *ops = NULL;
    ops = hinic3_get_drv_ops();
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_hqos_statistics_get_all_batch, HINIC3_DRV_FUNC_NO_PTR);
    ret = ops->hovs_hqos_statistics_get_all_batch(type, ids, stats, cnt);
    return ret;
}
