/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026-2027 Huawei Technologies Co., Ltd
 */

#include <stdint.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <rte_mbuf.h>
#include <rte_net.h>
#include <rte_ethdev.h>
#include <rte_malloc.h>
#include <rte_memcpy.h>
#include <rte_memzone.h>
#include <rte_string_fns.h>
#include <rte_bus_vdev.h>
#include <rte_bus_pci.h>
#include <rte_kvargs.h>
#include <rte_errno.h>


#include "hidrv_subctrl_api.h"
#include "hidrv_udev.h"
#include "hidrv_map.h"
#include "hidrv_poe_pf_api.h"
#include "hidrv_poe_vf_api.h"
#include "hidrv_poe_xtp.h"
#include "hidrv_ppe_vf_api.h"
#include "hidrv_ppe_xtp.h"
#include "ndrv_poe_pf_api.h"
#include "ndrv_poe_vf_api.h"
#include "ndrv_ppe_mam_vf_api.h"

#include "hns5_poe.h"
#include "hns5_eth.h"
#include "hns5_log.h"
#include "hns5_mempool.h"

#define POE_PF_ID 0
#define POE_VF_ID 0
#define POE_DEV_ID 0
#define POE_DIE_ID  0

#define ISOLATE_DEFAULT_VALUE 0xffffffff

static int hns5_poe_module_init(struct rte_eth_dev *dev)
{
    static bool inited = false;
    int ret = 0;
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);

    if (inited == true)
        return 0;
    ret = hidrv_poe_order_set_timeout(priv->poe_pf_devid, HNS5_POE_ORDER_TMOUT_TIME);
    if (ret != 0) {
        hns5_err(dev, "failed to hidrv_poe_order_set_timeout, ret:%d\n", ret);
        return ret;
    }
    inited = true;
    return 0;
}

static int hns5_poe_port_init(struct rte_eth_dev *dev)
{
    int ret = 0;
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);

    ret = hidrv_poe_get_pf_devid(POE_DIE_ID, POE_PF_ID);
    if (ret < 0) {
        hns5_err(dev, "failed to hidrv_poe_get_pf_devid, ret:%d\n", ret);
        return ret;
    }
    priv->poe_pf_devid = ret;
    priv->poe_pfhandle = hidrv_poe_pf_get_ndrv_handle(priv->poe_pf_devid);
    if (priv->poe_pfhandle == NULL) {
        hns5_err(dev, "failed to hidrv_poe_pf_get_ndrv_handle\n");
        return ret;
    }

    ret = hidrv_poe_get_vf_devid(POE_DIE_ID, POE_PF_ID, POE_VF_ID);
    if (ret < 0) {
        hns5_err(dev, "failed to hidrv_poe_get_vf_devid, ret:%d\n", ret);
        return ret;
    }
    priv->poe_vf_devid = ret;
    priv->poe_vfhandle = hidrv_poe_vf_get_ndrv_handle(priv->poe_vf_devid);
    if (priv->poe_vfhandle == NULL) {
        hns5_err(dev, "failed to hidrv_poe_vf_get_ndrv_handle\n");
        return ret;
    }
    return 0;
}

int hns5_eth_poe_init(struct rte_eth_dev *dev)
{
    int ret = 0;

    ret = hns5_poe_port_init(dev); // init poe pfhandle and vfhandle
    if (ret != 0)
        return ret;
    /* claim the poe->ppe bp slot before any fw_tnl is bound to it */
    ret = hns5_poe_bp_grp_claim(dev);
    if (ret != 0) {
        hns5_err(dev, "failed to claim bp_ppe slot, ret:%d\n", ret);
        return ret;
    }
    ret = hns5_poe_module_init(dev); // only order timeout is set
    if (ret != 0) {
        (void)hns5_poe_bp_grp_release(dev);
        return ret;
    }
    return 0;
}

int hns5_eth_rx_queue_setup_poe(struct rte_eth_dev *dev, uint32_t group, uint16_t depth)
{
    int ret = 0;
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);
    struct hidrv_poe_grp_cfg config = { 0 };
    struct hidrv_poe_q_th_tp_sel_cfg templete_cfg = { 0 };
    static uint64_t inited = 0;

    /* group budget is shared by all queues; remember it so rxq setup can split it */
    priv->bp_ppe_th = depth;

    /* Prevent duplicate initialization */
    if (inited & (uint64_t)(1 << group)) {
        hns5_dbg(dev, "group:%u already init\n", group);
        return 0;
    }
    config.order_en = 0;
    config.bp_ppe_th = depth;
    config.int_th = 0;
    config.flow_en = 1;
    ret = hidrv_poe_open_group(priv->poe_vf_devid, group, &config);
    if (ret != 0) {
        hns5_err(dev, "failed to hidrv_poe_open_group, ret:%d\n", ret);
        return ret;
    }
    /* Template values configured by kernel; identical for every queue. */
    templete_cfg.q_low_th_tpid = HNS5_POE_TH_TPMLTE_ID;
    templete_cfg.q_high_th_tpid = HNS5_POE_TH_TPMLTE_ID;
    templete_cfg.q_indraw_pkt_th_tpid = HNS5_POE_TH_TPMLTE_ID;

    /* Init all queues inside the group at once */
    for (uint32_t i = 0; i < HNS5_POE_CPU_GRP_QUE_NUM; i++) {
        ret = hidrv_poe_set_group_queue_weight(priv->poe_vf_devid, group, i, 255);
        if (ret != 0) {
            hns5_err(dev, "failed to hidrv_poe_set_group_queue_weight, ret:%d group:%u queue:%u\n", ret, group, i);
            return ret;
        }
        ret = hidrv_poe_set_group_queue_priority(priv->poe_vf_devid, group, i, i);
        if (ret != 0) {
            hns5_err(dev, "failed to hidrv_poe_set_group_queue_priority, ret:%d group:%u queue:%u\n", ret, group, i);
            return ret;
        }
        ret = hidrv_poe_set_q_th_tp_sel(priv->poe_vf_devid, group, i, &templete_cfg);
        if (ret != 0) {
            hns5_err(dev, "failed to hidrv_poe_set_q_th_tp_sel, ret:%d group:%u queue:%u\n", ret, group, i);
            return ret;
        }
    }

    inited |= (uint64_t)(1 << group);
    return 0;
}

int hns5_poe_rxq_enable(struct hns5_eth_rx_queue *rxq, bool enable)
{
    unsigned int ret;

    ret = ndrv_poe_vf_set_q_in_enable(rxq->poe_vfhandle, rxq->group,
                                      rxq->queue, enable);
    if (ret != 0)
        return -EIO;
    rxq->poe_enabled = enable;
    return 0;
}

/* POE -> PPE per-queue backpressure/watermark: a CPU group shares one
 * bp_ppe_th budget and each queue gets an equal share so a slow queue cannot
 * stall its siblings; over-watermark packets are MAM-dropped before BMU
 * allocation. The write-only template slots are tracked by share_group. */
struct hns5_bp_slot {
    uint8_t used;
    uint32_t group;
};

static struct hns5_bp_slot hns5_bp_slots[HNS5_POE_BP_GROUP_MAX_NUM];
static rte_spinlock_t hns5_bp_slots_lock = RTE_SPINLOCK_INITIALIZER;

static int hns5_poe_bp_grp_alloc(uint32_t group)
{
    int slot = -ENOSPC;
    uint32_t i;

    rte_spinlock_lock(&hns5_bp_slots_lock);
    for (i = 0; i < HNS5_POE_BP_GROUP_MAX_NUM; i++) {
        if (hns5_bp_slots[i].used != 0 && hns5_bp_slots[i].group == group) {
            slot = (int)i;
            goto out;
        }
    }
    /* scan from high to low so group 0 keeps the low, stable slot */
    for (i = HNS5_POE_BP_GROUP_MAX_NUM; i-- > 0;) {
        if (hns5_bp_slots[i].used == 0) {
            hns5_bp_slots[i].used = 1;
            hns5_bp_slots[i].group = group;
            slot = (int)i;
            break;
        }
    }
out:
    rte_spinlock_unlock(&hns5_bp_slots_lock);
    return slot;
}

static void hns5_poe_bp_grp_free(uint32_t group, uint32_t slot)
{
    rte_spinlock_lock(&hns5_bp_slots_lock);
    if (slot < HNS5_POE_BP_GROUP_MAX_NUM && hns5_bp_slots[slot].used != 0 &&
        hns5_bp_slots[slot].group == group) {
        hns5_bp_slots[slot].used = 0;
        hns5_bp_slots[slot].group = 0;
    }
    rte_spinlock_unlock(&hns5_bp_slots_lock);
}

/* program one queue's high/low watermark and toggle its MAM source drop */
static int hns5_poe_rxq_bp_quota_set(struct rte_eth_dev *dev,
    struct hns5_eth_rx_queue *rxq, uint32_t high, uint32_t low, int poe_on)
{
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);
    struct ndrv_poe_pf_th_cfg th = {0};
    struct ndrv_ppe_mam_qos_drop_cfg drop = {0};
    handle ppe_vfhandle;
    uint32_t fw_tnl;
    int ret;

    if (priv->fwl_tnl_inited != 1)
        return 0;

    fw_tnl = priv->fwl_tnl_conf.base_tnl + rxq->queue;

    th.high_th = high;
    th.low_th = low;
    ret = (int)ndrv_poe_pf_set_bp_ppe_queue_cfg(priv->poe_pfhandle, priv->bp_grp,
                                                rxq->queue, &th);
    if (ret != 0) {
        hns5_err(dev, "failed to set bp queue watermark, grp:%u queue:%u high:%u low:%u ret:%d\n",
                 priv->bp_grp, rxq->queue, high, low, ret);
        return ret < 0 ? ret : -EIO;
    }

    /* once POE backpressures the queue, drop in MAM before buffer allocation */
    ppe_vfhandle = hidrv_ppe_vf_get_ndrv_handle(priv->ppe_vf_devid);
    drop.bmu_en = 1;
    drop.poe_en = poe_on ? 1 : 0;
    ret = (int)ndrv_ppe_mam_vf_set_qos_drop(ppe_vfhandle, fw_tnl, &drop);
    if (ret != 0) {
        hns5_err(dev, "failed to set qos source drop, tnl:%u poe_en:%d ret:%d\n",
                 fw_tnl, drop.poe_en, ret);
        return ret < 0 ? ret : -EIO;
    }

    rxq->bp_drop_on = poe_on ? 1 : 0;
    return 0;
}

/* Equal share of the group budget: high = bp_ppe_th / N, low = high / 2.
 * Packets are admitted atomically against the queue watermark, so the sum
 * never exceeds the group budget: each queue is capped individually rather
 * than relying on group backpressure. */
void hns5_poe_rxq_bp_quota_apply(struct rte_eth_dev *dev,
    struct hns5_eth_rx_queue *rxq)
{
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);
    uint32_t nb = dev->data->nb_rx_queues;
    uint32_t budget = priv->bp_ppe_th;
    uint32_t high;
    uint32_t low;

    if (priv->bp_grp >= HNS5_POE_BP_GROUP_MAX_NUM || nb == 0 || budget == 0)
        return;

    high = budget / nb;
    if (high == 0)
        high = 1;
    low = high / 2;

    if (hns5_poe_rxq_bp_quota_set(dev, rxq, high, low, 1) != 0) {
        hns5_err(dev, "failed to apply rxq:%u watermark, fall back to no queue cap\n",
                 rxq->queue);
        (void)hns5_poe_rxq_bp_quota_set(dev, rxq, HNS5_POE_BP_TH_MAX, HNS5_POE_BP_TH_MAX, 0);
    }
}

/* Explicit drop/pass switch for one queue's fw_tnl. The flow table keeps
 * steering hits to a stopping queue, so we arm a zero watermark and keep the
 * MAM source drop on: POE treats it as always over share and MAM discards
 * before BMU allocation. Clearing it restores the normal budget share. */
void hns5_poe_rxq_fw_tnl_drop_set(struct rte_eth_dev *dev,
    struct hns5_eth_rx_queue *rxq, bool drop)
{
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);

    if (priv->bp_grp >= HNS5_POE_BP_GROUP_MAX_NUM)
        return;

    if (!drop) {
        if (priv->bp_ppe_th != 0 && dev->data->nb_rx_queues != 0) {
            hns5_poe_rxq_bp_quota_apply(dev, rxq);
        } else {
            (void)hns5_poe_rxq_bp_quota_set(dev, rxq, HNS5_POE_BP_TH_MAX,
                                            HNS5_POE_BP_TH_MAX, 0);
        }
        return;
    }

    if (hns5_poe_rxq_bp_quota_set(dev, rxq, 0, 0, 1) != 0)
        hns5_err(dev, "failed to arm fw_tnl drop, tnl:%u\n",
                 priv->fwl_tnl_conf.base_tnl + rxq->queue);
}

int hns5_poe_bp_grp_claim(struct rte_eth_dev *dev)
{
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);
    struct ndrv_poe_pf_bp_ppe_grp_cfg grp_cfg = {0};
    struct ndrv_poe_pf_th_cfg th = {0};
    uint32_t group = priv->param.share_group;
    uint32_t slot_id;
    uint32_t i;
    int slot;
    int ret;

    priv->bp_grp = HNS5_POE_BP_SLOT_INVALID;
    slot = hns5_poe_bp_grp_alloc(group);
    if (slot < 0) {
        hns5_err(dev, "no free bp_ppe slot for poe group:%u\n", group);
        return slot;
    }
    slot_id = (uint32_t)slot;

    /* bind the slot to this group, park all queue watermarks at "never trigger" */
    grp_cfg.add_pkt_group = group;
    grp_cfg.bp_low_th = HNS5_POE_BP_TH_MAX;
    grp_cfg.bp_high_th = HNS5_POE_BP_TH_MAX;
    ret = (int)ndrv_poe_pf_set_bp_ppe_group_cfg(priv->poe_pfhandle, slot_id, 1, &grp_cfg);
    if (ret != 0) {
        hns5_err(dev, "failed to set bp group cfg, slot:%u poe_grp:%u ret:%d\n", slot_id, group, ret);
        hns5_poe_bp_grp_free(group, slot_id);
        return ret < 0 ? ret : -EIO;
    }

    th.high_th = HNS5_POE_BP_TH_MAX;
    th.low_th = HNS5_POE_BP_TH_MAX;
    for (i = 0; i < HNS5_POE_CPU_GRP_QUE_NUM; i++) {
        ret = (int)ndrv_poe_pf_set_bp_ppe_queue_cfg(priv->poe_pfhandle, slot_id, i, &th);
        if (ret != 0) {
            hns5_err(dev, "failed to init bp queue cfg, slot:%u queue:%u ret:%d\n", slot_id, i, ret);
            (void)ndrv_poe_pf_set_bp_ppe_group_cfg(priv->poe_pfhandle, slot_id, 0, NULL);
            hns5_poe_bp_grp_free(group, slot_id);
            return ret < 0 ? ret : -EIO;
        }
    }

    priv->bp_grp = (uint8_t)slot;
    hns5_dbg(dev, "claim bp_ppe slot:%u for poe group:%u\n", slot_id, group);
    return 0;
}

int hns5_poe_bp_grp_release(struct rte_eth_dev *dev)
{
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);
    struct ndrv_poe_pf_th_cfg th = {0};
    uint32_t slot = priv->bp_grp;
    uint32_t group = priv->param.share_group;
    uint32_t i;
    int ret = 0;
    int err;

    if (slot >= HNS5_POE_BP_GROUP_MAX_NUM)
        return 0;   /* not claimed or already released, idempotent */

    /* 1. clear MAM source drop and restore the queues we actually touched */
    for (i = 0; i < dev->data->nb_rx_queues; i++) {
        struct hns5_eth_rx_queue *rxq = dev->data->rx_queues[i];

        if (rxq == NULL || rxq->type != HNS5_ETH_RXQ_TYPE_QUEUE || rxq->bp_drop_on == 0)
            continue;
        err = hns5_poe_rxq_bp_quota_set(dev, rxq, HNS5_POE_BP_TH_MAX, HNS5_POE_BP_TH_MAX, 0);
        if (err != 0 && ret == 0)
            ret = err;
    }

    /* 2. park every queue watermark of the slot as a fallback */
    th.high_th = HNS5_POE_BP_TH_MAX;
    th.low_th = HNS5_POE_BP_TH_MAX;
    for (i = 0; i < HNS5_POE_CPU_GRP_QUE_NUM; i++) {
        err = (int)ndrv_poe_pf_set_bp_ppe_queue_cfg(priv->poe_pfhandle, slot, i, &th);
        if (err != 0 && ret == 0)
            ret = err;
    }

    /* 3. disable the template and hand the slot back */
    err = (int)ndrv_poe_pf_set_bp_ppe_group_cfg(priv->poe_pfhandle, slot, 0, NULL);
    if (err != 0 && ret == 0)
        ret = err;

    hns5_poe_bp_grp_free(group, slot);
    priv->bp_grp = (uint8_t)HNS5_POE_BP_SLOT_INVALID;
    hns5_dbg(dev, "release bp_ppe slot:%u for poe group:%u\n", slot, group);
    return ret;
}
