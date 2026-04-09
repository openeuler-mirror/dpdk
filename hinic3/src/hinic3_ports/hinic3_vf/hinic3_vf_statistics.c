/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2025 Huawei Technologies Co., Ltd
 */

#include <unistd.h>
#include "rte_dev.h"
#include "rte_devargs.h"
#include <rte_atomic.h>
#include "hinic3_tlv_key.h"
#include "hinic3_bond_controller.h"
#include "hinic3_util.h"
#include "hinic3_capture_main.h"
#include "hinic3_agent.h"
#include "hinic3_vf_port_qos_public.h"
#include "hinic3_flow_agent.h"
#include "hinic3_offload_flow_port.h"
#include "hinic3_qos.h"
#include "hinic3_vf_controller.h"
#include "hinic3_vxlan.h"
#include "hinic3_flow_dump.h"
#include "hinic3_offload_flow_port.h"
#include "hinic3_vf_statistics.h"

int hinic3_vf_dev_stats_get(struct rte_eth_dev *dev, struct rte_eth_stats *stats)
{
    if (dev == NULL || stats == NULL) {
        HINIC3_LOG(ERR, VPORT, "The parameter is NULL");
        return -EINVAL;
    }

    int ret;
    hinic3_port_stats hinic3_stats = { 0 };
    struct hinic3_vf_dev *vf_dev = hinic3_ethdev_get_vf_private(dev);

    if (vf_dev == NULL) {
        HINIC3_LOG(ERR, VPORT, "The current device private_data is NULL.");
        return -EINVAL;
    }

    ret = hinic3_port_statistics_get(vf_dev->vport_id, &hinic3_stats);
    if (ret != 0) {
        return ret;
    }

    // 端口报文统计信息,只统计慢路径的数据
    stats->ipackets = rte_atomic64_read(&vf_dev->vf_upcall_pk_num);
    stats->opackets = rte_atomic64_read(&vf_dev->vf_reinject_pk_num);
    stats->ibytes = rte_atomic64_read(&vf_dev->vf_upcall_pk_byt);
    stats->obytes = rte_atomic64_read(&vf_dev->vf_reinject_pk_byt);
    stats->imissed = hinic3_stats.upcall_wqe_fail_dropped + hinic3_stats.ovs_port_stats.rx_dropped;
    stats->ierrors = hinic3_stats.ovs_port_stats.rx_errors;
    stats->oerrors = hinic3_stats.ovs_port_stats.tx_errors + hinic3_stats.ovs_port_stats.tx_dropped;
    stats->rx_nombuf = hinic3_stats.rx_wqe_fail_dropped;

    return 0;
}

int hinic3_vf_dev_stats_reset(struct rte_eth_dev *dev)
{
    if (dev == NULL) {
        HINIC3_LOG(ERR, VPORT, "The eth_dev is NULL");
        return -EINVAL;
    }

    int ret;
    struct hinic3_vf_dev *vf_dev = hinic3_ethdev_get_vf_private(dev);
    if (vf_dev == NULL) {
        HINIC3_LOG(ERR, VPORT, "The private data is NULL");
        return -EPERM;
    }

    ret = hinic3_port_statistics_flush(vf_dev->vport_id);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "hinic3 vf stats reset failed, vport:%hu!", vf_dev->vport_id);
        return ret;
    }
    return 0;
}

int hinic3_vf_dev_xstats_get(struct rte_eth_dev *dev, struct rte_eth_xstat *xstats, unsigned int n)
{
    if (dev == NULL || xstats == NULL) {
        HINIC3_LOG(ERR, VPORT, "The parameter is NULL");
        return -EINVAL;
    }

    int ret = 0;
    hinic3_port_stats port_xstats = { 0 };
    struct hinic3_vf_dev *vf_dev = hinic3_ethdev_get_vf_private(dev);
    if (vf_dev == NULL) {
        HINIC3_LOG(ERR, VPORT, "The private data is NULL");
        return -EINVAL;
    }

    if (n < HW_NB_XSTATS) {
        HINIC3_LOG(ERR, VPORT, "The maximum number is less than the expected number");
        return -ERANGE;
    }

    ret = hinic3_port_statistics_get(vf_dev->vport_id, &port_xstats);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "Failed to obtain port information from the hovs");
        return ret;
    }

    for (int i = 0; i < HW_NB_XSTATS; i++) {
        xstats[i].id = i;
    }
    xstats[XSTATS_UPCALL_PKT_INDEX].value = rte_atomic64_read(&vf_dev->vf_upcall_pk_num);
    xstats[XSTATS_REINJECT_PKT_INDEX].value = rte_atomic64_read(&vf_dev->vf_reinject_pk_num);
    xstats[XSTATS_UPCALL_DROP_INDEX].value = port_xstats.upcall_wqe_fail_dropped;
    return HW_NB_XSTATS;
}

int hinic3_vf_dev_xstats_reset(struct rte_eth_dev *dev)
{
    if (dev == NULL) {
        HINIC3_LOG(ERR, VPORT, "The eth_dev is NULL");
        return -1;
    }
    int ret;
    struct hinic3_vf_dev *vf_dev = hinic3_ethdev_get_vf_private(dev);
    if (vf_dev == NULL) {
        HINIC3_LOG(ERR, VPORT, "The private data is NULL");
        return -1;
    }

    ret = hinic3_port_statistics_flush(vf_dev->vport_id);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "hinic3 vf stats reset failed, vport:%hu!", vf_dev->vport_id);
        return -1;
    }

    rte_atomic64_set(&vf_dev->vf_upcall_pk_num, 0);
    rte_atomic64_set(&vf_dev->vf_reinject_pk_num, 0);
    return 0;
}

int hinic3_vf_dev_xstats_get_names(struct rte_eth_dev *dev, struct rte_eth_xstat_name *xstats_names,
    unsigned int limit)
{
    return hinic3_port_xstats_get_names(dev, xstats_names, limit);
}
