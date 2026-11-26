/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026-2027 Huawei Technologies Co., Ltd
 */

#include <stdlib.h>
#include <string.h>

#include <rte_mbuf.h>
#include <rte_mbuf_ptype.h>
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
#include <rte_cycles.h>
#include <rte_alarm.h>
#include <rte_bitops.h>
#include <rte_atomic.h>
#include <rte_ethdev_vdev.h>

#include "hidrv_subctrl_api.h"
#include "ndrv_ppe_bd_type.h"
#include "ndrv_ppe_pf_api.h"
#include "ndrv_poe_pf_api.h"
#include "ndrv_poe_vf_api.h"
#include "ndrv_ppe_ppp_pf_api.h"
#include "ndrv_ppe_ppp_vf_api.h"
#include "ndrv_ppe_rpu_pf_api.h"
#include "ndrv_ppe_ssu_pf_api.h"
#include "ndrv_ppe_tpu_pf_api.h"
#include "ndrv_ppe_tpu_vf_api.h"
#include "ndrv_ppe_vf_api.h"
#include "ndrv_ppe_mam_pf_api.h"
#include "ndrv_ppe_mam_vf_api.h"
#include "ndrv_ppe_lsw_pf_api.h"
#include "ppe_lsw_type_api.h"
#include "ndrv_ppe_pa_pf_api.h"
#include "node_code_api.h"
#include "hidrv_udev.h"
#include "hidrv_map.h"
#include "hidrv_tm_pf_api.h"
#include "ndrv_ppe_debug_api.h"
#include "hidrv_mag_api.h"
#include "hidrv_poe_pf_api.h"
#include "hidrv_poe_vf_api.h"
#include "hidrv_log_api.h"
#include "hidrv_ppe_pf_api.h"
#include "hidrv_ppe_vf_api.h"
#include "hidrv_mag_xtp.h"
#include "hidrv_ppe_xtp.h"

#include "hns5_eth.h"
#include "hns5_stats.h"
#include "hns5_mbuf.h"
#include "hns5_eth_rxtx.h"
#include "hns5_ppe.h"
#include "hns5_poe.h"
#include "hns5_mag.h"
#include "hns5_log.h"
#include "hns5_mempool.h"
#include "hns5_flow.h"
#include "hns5_tm.h"
#include "hns5_rss.h"
#include "hns5_lsw.h"
#include "hns5_hairpin.h"

#define HNS5_ETH_DEV_INFO_NAME_LEN     64
#define HNS5_ETH_DEFAULT_HW_TXQ_NUM    8
#define HNS5_ETH_POOL_MAX              64      // global max pool count
#define HNS5_ETH_MAX_RX_MEMPOOLS       8       // max mempools per rxq
#define HNS5_LSC_POLL_INTERVAL_US      250000  // lsc polling interval: 250ms, also the only source reporting link up
#define HNS5_LSC_CB_UNREG_MAX_RETRIES  5       // lsc callback unregister retry count
#define HNS5_LSC_CB_UNREG_RETRY_MS     100     // lsc callback unregister retry interval
#define HNS5_RXQ_DRAIN_MAX_POLLS       1024

static const char *g_mag_mode_maps[] = {
    [HIDRV_MAG_PORT_MODE_100BASE_FX] = "100BASE_FX",
    [HIDRV_MAG_PORT_MODE_1000BASE_X] = "1000BASE_X",
    [HIDRV_MAG_PORT_MODE_SGMII] = "SGMII",
    [HIDRV_MAG_PORT_MODE_QSGMII] = "QSGMII",
    [HIDRV_MAG_PORT_MODE_2G5BASE_X] = "2G5BASE_X",
    [HIDRV_MAG_PORT_MODE_10GBASE_R] = "10GBASE_R",
    [HIDRV_MAG_PORT_MODE_FLEXMII_5G2P] = "FLEXMII_5G2P",
    [HIDRV_MAG_PORT_MODE_FLEXMII_10G2P] = "FLEXMII_10G2P",
    [HIDRV_MAG_PORT_MODE_FLEXMII_20G2P] = "FLEXMII_20G2P",
    [HIDRV_MAG_PORT_MODE_FLEXMII_10G4P] = "FLEXMII_10G4P",
    [HIDRV_MAG_PORT_MODE_FLEXMII_20G4P] = "FLEXMII_20G4P",
    [HIDRV_MAG_PORT_MODE_5GBASE_R] = "5GBASE_R",
    [HIDRV_MAG_PORT_MODE_5GBASE_KR] = "5GBASE_KR",
    [HIDRV_MAG_PORT_MODE_XAUI] = "XAUI",
    [HIDRV_MAG_PORT_MODE_25GBASE_R] = "25GBASE_R",
    [HIDRV_MAG_PORT_MODE_40GBASE_R] = "40GBASE_R",
    [HIDRV_MAG_PORT_MODE_50GBASE_R] = "50GBASE_R",
    [HIDRV_MAG_PORT_MODE_100GBASE_R] = "100GBASE_R"
};

static uint32_t g_mag_speed_hidrv_to_rte[] = {
    [HIDRV_MAG_PORT_SPEED_10M] = ETH_SPEED_NUM_10M,
    [HIDRV_MAG_PORT_SPEED_100M] = ETH_SPEED_NUM_100M,
    [HIDRV_MAG_PORT_SPEED_1000M] = ETH_SPEED_NUM_1G,
    [HIDRV_MAG_PORT_SPEED_2G5] = ETH_SPEED_NUM_2_5G,
    [HIDRV_MAG_PORT_SPEED_5G] = ETH_SPEED_NUM_5G,
    [HIDRV_MAG_PORT_SPEED_10G] = ETH_SPEED_NUM_10G,
    [HIDRV_MAG_PORT_SPEED_12G5] = ETH_SPEED_NUM_NONE,
    [HIDRV_MAG_PORT_SPEED_25G] = ETH_SPEED_NUM_25G,
    [HIDRV_MAG_PORT_SPEED_40G] = ETH_SPEED_NUM_40G,
    [HIDRV_MAG_PORT_SPEED_50G] = ETH_SPEED_NUM_50G,
    [HIDRV_MAG_PORT_SPEED_100G] = ETH_SPEED_NUM_100G,
    [HIDRV_MAG_PORT_SPEED_NO_USE] = ETH_SPEED_NUM_UNKNOWN,
};

static uint32_t g_mag_mode_support_speeds[] = {
    [HIDRV_MAG_PORT_MODE_100BASE_FX] = ETH_LINK_SPEED_100M | ETH_LINK_SPEED_100M_HD,
    [HIDRV_MAG_PORT_MODE_1000BASE_X] = ETH_LINK_SPEED_1G,
    [HIDRV_MAG_PORT_MODE_SGMII] = ETH_LINK_SPEED_10M | ETH_LINK_SPEED_10M_HD |
                                    ETH_LINK_SPEED_100M | ETH_LINK_SPEED_100M_HD | ETH_LINK_SPEED_1G,
    [HIDRV_MAG_PORT_MODE_QSGMII] = ETH_LINK_SPEED_10M | ETH_LINK_SPEED_10M_HD |
                                    ETH_LINK_SPEED_100M | ETH_LINK_SPEED_100M_HD | ETH_LINK_SPEED_1G,
    [HIDRV_MAG_PORT_MODE_2G5BASE_X] = ETH_LINK_SPEED_2_5G,
    [HIDRV_MAG_PORT_MODE_10GBASE_R] = ETH_LINK_SPEED_10G,
    [HIDRV_MAG_PORT_MODE_FLEXMII_5G2P] = ETH_LINK_SPEED_2_5G,
    [HIDRV_MAG_PORT_MODE_FLEXMII_10G2P] = ETH_LINK_SPEED_5G,
    [HIDRV_MAG_PORT_MODE_FLEXMII_20G2P] = ETH_LINK_SPEED_10G,
    [HIDRV_MAG_PORT_MODE_FLEXMII_10G4P] = ETH_LINK_SPEED_2_5G,
    [HIDRV_MAG_PORT_MODE_FLEXMII_20G4P] = ETH_LINK_SPEED_5G,
    [HIDRV_MAG_PORT_MODE_5GBASE_R] = ETH_LINK_SPEED_5G,
    [HIDRV_MAG_PORT_MODE_5GBASE_KR] = ETH_LINK_SPEED_5G,
    [HIDRV_MAG_PORT_MODE_XAUI] = ETH_LINK_SPEED_2_5G,
    [HIDRV_MAG_PORT_MODE_25GBASE_R] = ETH_LINK_SPEED_25G,
    [HIDRV_MAG_PORT_MODE_40GBASE_R] = ETH_LINK_SPEED_40G,
    [HIDRV_MAG_PORT_MODE_50GBASE_R] = ETH_LINK_SPEED_50G,
    [HIDRV_MAG_PORT_MODE_100GBASE_R] = ETH_LINK_SPEED_100G
};

static enum hidrv_mag_port_speed g_mag_mode_default_speed[] = {
    [HIDRV_MAG_PORT_MODE_100BASE_FX] = HIDRV_MAG_PORT_SPEED_100M,
    [HIDRV_MAG_PORT_MODE_1000BASE_X] = HIDRV_MAG_PORT_SPEED_1000M,
    [HIDRV_MAG_PORT_MODE_SGMII] = HIDRV_MAG_PORT_SPEED_1000M,
    [HIDRV_MAG_PORT_MODE_QSGMII] = HIDRV_MAG_PORT_SPEED_1000M,
    [HIDRV_MAG_PORT_MODE_2G5BASE_X] = HIDRV_MAG_PORT_SPEED_2G5,
    [HIDRV_MAG_PORT_MODE_10GBASE_R] = HIDRV_MAG_PORT_SPEED_10G,
    [HIDRV_MAG_PORT_MODE_FLEXMII_5G2P] = HIDRV_MAG_PORT_SPEED_2G5,
    [HIDRV_MAG_PORT_MODE_FLEXMII_10G2P] = HIDRV_MAG_PORT_SPEED_5G,
    [HIDRV_MAG_PORT_MODE_FLEXMII_20G2P] = HIDRV_MAG_PORT_SPEED_10G,
    [HIDRV_MAG_PORT_MODE_FLEXMII_10G4P] = HIDRV_MAG_PORT_SPEED_2G5,
    [HIDRV_MAG_PORT_MODE_FLEXMII_20G4P] = HIDRV_MAG_PORT_SPEED_5G,
    [HIDRV_MAG_PORT_MODE_5GBASE_R] = HIDRV_MAG_PORT_SPEED_5G,
    [HIDRV_MAG_PORT_MODE_5GBASE_KR] = HIDRV_MAG_PORT_SPEED_5G,
    [HIDRV_MAG_PORT_MODE_XAUI] = HIDRV_MAG_PORT_SPEED_2G5,
    [HIDRV_MAG_PORT_MODE_25GBASE_R] = HIDRV_MAG_PORT_SPEED_25G,
    [HIDRV_MAG_PORT_MODE_40GBASE_R] = HIDRV_MAG_PORT_SPEED_40G,
    [HIDRV_MAG_PORT_MODE_50GBASE_R] = HIDRV_MAG_PORT_SPEED_50G,
    [HIDRV_MAG_PORT_MODE_100GBASE_R] = HIDRV_MAG_PORT_SPEED_100G
};

static const struct rte_eth_fec_capa g_speed_fec_capa_tbl[] = {
    { ETH_SPEED_NUM_5G, RTE_ETH_FEC_MODE_CAPA_MASK(NOFEC) |
                RTE_ETH_FEC_MODE_CAPA_MASK(BASER) |
                RTE_ETH_FEC_MODE_CAPA_MASK(RS)},

    { ETH_SPEED_NUM_10G, RTE_ETH_FEC_MODE_CAPA_MASK(NOFEC) |
                RTE_ETH_FEC_MODE_CAPA_MASK(BASER) |
                RTE_ETH_FEC_MODE_CAPA_MASK(RS)},

    { ETH_SPEED_NUM_25G, RTE_ETH_FEC_MODE_CAPA_MASK(NOFEC) |
                RTE_ETH_FEC_MODE_CAPA_MASK(BASER) |
                RTE_ETH_FEC_MODE_CAPA_MASK(RS)},

    { ETH_SPEED_NUM_40G, RTE_ETH_FEC_MODE_CAPA_MASK(NOFEC) |
                RTE_ETH_FEC_MODE_CAPA_MASK(BASER) |
                RTE_ETH_FEC_MODE_CAPA_MASK(RS)},

    { ETH_SPEED_NUM_100G, RTE_ETH_FEC_MODE_CAPA_MASK(NOFEC) |
                RTE_ETH_FEC_MODE_CAPA_MASK(BASER) |
                RTE_ETH_FEC_MODE_CAPA_MASK(RS)},
};

static const uint32_t g_mag_fec_mode_hidrv_to_rte[] = {
    [HIDRV_MAG_NO_FEC] = RTE_ETH_FEC_NOFEC,
    [HIDRV_MAG_FEC_BRFEC] = RTE_ETH_FEC_BASER,
    [HIDRV_MAG_FEC_RSFEC_544] = RTE_ETH_FEC_RS
};

static int hns5_eth_dev_mtu_set(struct rte_eth_dev *dev, uint16_t mtu)
{
    int ret = 0;
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);
    uint32_t max_frame_len = HNS5_MAG_MTU_TO_FRAME(mtu);

    if (mtu > HNS5_MTU_MAX) {
        hns5_warn(dev, "mtu:%u is out [%u-%u], use max val\n", mtu, HNS5_MTU_MIN, HNS5_MTU_MAX);
        max_frame_len = HNS5_FRAME_MAX;
    }

    if (priv->mag_handle == NULL) {
        hns5_info(dev, "mag not ready, save mtu %u (frame:%u), apply at dev_start\n",
            mtu, max_frame_len);
        priv->param.max_frame_len = max_frame_len;
        return 0;
    }

    ret = hidrv_mag_mac_set_max_frame_len(priv->mag_pf_devid, priv->param.mport_id, max_frame_len);
    if (ret != 0) {
        hns5_err(dev, "failed set frame, mtu:%u frame:%u\n", mtu, max_frame_len);
        return ret;
    }
    priv->param.max_frame_len = max_frame_len;
    hns5_info(dev, "succeed set mtu %u\n", HNS5_MAG_FRAME_TO_MTU(max_frame_len));
    return 0;
}

static int hns5_eth_dev_set_link_down(struct rte_eth_dev *dev)
{
    int ret = hns5_mag_txrx_enable(dev, 0);

    if (ret != 0)
        return ret;

    dev->data->dev_link.link_status = ETH_LINK_DOWN;
    dev->data->dev_link.link_speed = ETH_SPEED_NUM_UNKNOWN;
    return 0;
}

static int hns5_eth_dev_set_link_up(struct rte_eth_dev *dev)
{
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);
    int ret = hns5_mag_txrx_enable(dev, 1);

    if (ret != 0)
        return ret;

    dev->data->dev_link.link_status = ETH_LINK_UP;
    if (priv->param.port_speed < RTE_DIM(g_mag_speed_hidrv_to_rte)) {
        dev->data->dev_link.link_speed = g_mag_speed_hidrv_to_rte[priv->param.port_speed];
    } else {
        dev->data->dev_link.link_speed = ETH_SPEED_NUM_NONE;
    }
    dev->data->dev_link.link_duplex = (priv->param.duplex_mode == HIDRV_MAG_DUPLEX_MODE_FULL) ?
        ETH_LINK_FULL_DUPLEX : ETH_LINK_HALF_DUPLEX;
    dev->data->dev_link.link_autoneg = (priv->param.autoneg == 0) ? ETH_LINK_SPEED_FIXED : ETH_LINK_SPEED_AUTONEG;
    return 0;
}

static void hns5_stop_rx_queues(struct rte_eth_dev *dev, uint32_t vcpu)
{
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);
    uint32_t cleared_grp_mask = 0;
    int ret;

    for (uint32_t i = 0; i < dev->data->nb_rx_queues; i++) {
        struct hns5_eth_rx_queue *rxq = dev->data->rx_queues[i];
		if (rxq != NULL && rxq->is_hairpin) {
			/* hairpin queue owns no POE group and must keep HAIRPIN state */
			continue;
		}
		dev->data->rx_queue_state[i] = RTE_ETH_QUEUE_STATE_STOPPED;
        if (rxq != NULL)
            rxq->poe_enabled = 0; /* group cleared below disables the input */
        if (rxq != NULL && (cleared_grp_mask & (1U << rxq->group)) == 0) {
            ret = hidrv_poe_clear_group(priv->poe_vf_devid, vcpu, rxq->group);
            if (ret != 0)
                hns5_warn(dev, "failed to clear group:%u, ret:%d\n", rxq->group, ret);
            cleared_grp_mask |= (1U << rxq->group);
        }
    }
}

/*
 * A link in local/remote fault cannot carry traffic even if the mac reports
 * it up, which happens when the link keeps flapping during a fault.
 */
static bool hns5_eth_link_faulted(struct hns5_eth_priv *priv)
{
    uint32_t local = 0;
    uint32_t remote = 0;

    if (hidrv_mag_mac_get_local_fault_status(priv->mag_pf_devid, priv->param.mport_id, &local) != 0)
        return false;
    if (hidrv_mag_mac_get_remote_fault_status(priv->mag_pf_devid, priv->param.mport_id, &remote) != 0)
        return false;

    return (local != 0 || remote != 0);
}

static int hns5_eth_link_update(struct rte_eth_dev *dev, int wait_to_complete __rte_unused)
{
    int ret = 0;
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);
    struct rte_eth_link *link = &(dev->data->dev_link);
    enum hidrv_mag_link_status link_status = HIDRV_MAG_LINK_INVALID;

    ret = hidrv_mag_mac_get_link_status(priv->mag_pf_devid, priv->param.mport_id, &link_status);
    if (ret != 0) {
        hns5_err(dev, "failed to hidrv_mag_mac_get_link_status, ret:%d\n", ret);
        return ret;
    }
    if (hns5_eth_link_faulted(priv))
        link_status = HIDRV_MAG_LINK_DOWN;
    link->link_status = (link_status == HIDRV_MAG_LINK_UP) ? ETH_LINK_UP : ETH_LINK_DOWN;
    return 0;
}

/* Fill and report the link info after the new status survived its confirm window. */
static void hns5_lsc_report_link(struct rte_eth_dev *dev, uint32_t cur_status)
{
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);

    /*
     * Only refresh the reported link info here. Do not toggle the MAC rx/tx
     * enable in the lsc path, otherwise a persistent fault irq may trigger
     * repeated enable/disable and make the link flap.
     */
    if (cur_status == ETH_LINK_UP) {
        if (priv->param.port_speed < RTE_DIM(g_mag_speed_hidrv_to_rte))
            dev->data->dev_link.link_speed = g_mag_speed_hidrv_to_rte[priv->param.port_speed];
        else
            dev->data->dev_link.link_speed = ETH_SPEED_NUM_NONE;
        dev->data->dev_link.link_duplex = (priv->param.duplex_mode == HIDRV_MAG_DUPLEX_MODE_FULL) ?
            ETH_LINK_FULL_DUPLEX : ETH_LINK_HALF_DUPLEX;
        dev->data->dev_link.link_autoneg = (priv->param.autoneg == 0) ?
            ETH_LINK_FIXED : ETH_LINK_AUTONEG;
    } else {
        dev->data->dev_link.link_speed = ETH_SPEED_NUM_UNKNOWN;
    }

    hns5_info(dev, "port:%u link status changed to %s\n", dev->data->port_id,
        (cur_status == ETH_LINK_UP) ? "up" : "down");
    rte_eth_dev_callback_process(dev, RTE_ETH_EVENT_INTR_LSC, NULL);
}

/*
 * The mac only raises an irq on link down, polling reports link up, so both
 * paths call in here to sample the status and report it as soon as it changes.
 */
static void hns5_lsc_update_link_report(struct rte_eth_dev *dev)
{
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);
    uint32_t status;

    if (hns5_eth_link_update(dev, 0) != 0) {
        hns5_warn(dev, "failed to update link status\n");
        return;
    }

    status = dev->data->dev_link.link_status;

    /* only report a real change */
    if (status == priv->last_link_status)
        return;

    priv->last_link_status = status;
    hns5_lsc_report_link(dev, status);
}

static void hns5_intr_lsc_handler(void *cb_arg)
{
    struct rte_eth_dev *dev = (struct rte_eth_dev *)cb_arg;
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);
    union hidrv_mag_mac_int_status int_status = {0};
    int ret;

    /* a stale callback may still fire after teardown, just bail out */
    if (priv == NULL || priv->intr_fd < 0 ||
        __atomic_load_n(&priv->alarm_cancel, __ATOMIC_ACQUIRE))
        return;

    /* mask during handling, unmasked before return only if still active */
    (void)hidrv_mag_mac_enable_interrupt(priv->mag_pf_devid, priv->intr_fd, 0);

    /* teardown may start after the entry check, stop touching the hw here */
    if (__atomic_load_n(&priv->alarm_cancel, __ATOMIC_ACQUIRE))
        return;

    /* eal has read the uio fd, only the mac status needs clearing here */
    ret = hidrv_mag_mac_get_int_status(priv->mag_pf_devid, priv->param.mport_id, &int_status);
    if (ret != 0) {
        hns5_warn(dev, "failed to get mac int status, ret:%d\n", ret);
    } else if (int_status.u64 != 0) {
        ret = hidrv_mag_mac_clr_int_status(priv->mag_pf_devid, priv->param.mport_id, &int_status);
        if (ret != 0) {
            hns5_warn(dev, "failed to clear mac int status:0x%llx, ret:%d\n",
                (unsigned long long)int_status.u64, ret);
        }
    }

    hns5_lsc_update_link_report(dev);

    /* never re-enable during teardown, otherwise unregister keeps seeing a busy irq;
     * the app callback may also have stopped or released the port */
    priv = hns5_eth_priv_get(dev);
    if (priv == NULL || __atomic_load_n(&priv->alarm_cancel, __ATOMIC_ACQUIRE) ||
        priv->intr_fd < 0)
        return;
    ret = hidrv_mag_mac_enable_interrupt(priv->mag_pf_devid, priv->intr_fd, 1);
    if (ret != 0)
        hns5_err(dev, "failed to re-enable lsc interrupt, ret:%d\n", ret);
}

static void hns5_intr_lsc_poll(void *arg)
{
    struct rte_eth_dev *dev = (struct rte_eth_dev *)arg;
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);

    if (priv == NULL || __atomic_load_n(&priv->alarm_cancel, __ATOMIC_ACQUIRE))
        return;

    hns5_lsc_update_link_report(dev);

    /* the lsc callback may have stopped or released the port, re-arm only if alive */
    priv = hns5_eth_priv_get(dev);
    if (priv == NULL || __atomic_load_n(&priv->alarm_cancel, __ATOMIC_ACQUIRE))
        return;

    if (rte_eal_alarm_set(HNS5_LSC_POLL_INTERVAL_US, hns5_intr_lsc_poll, dev) != 0)
        hns5_err(dev, "failed to re-arm lsc poll alarm\n");
}

static void hns5_intr_lsc_teardown(struct rte_eth_dev *dev)
{
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);
    int ret = 0;
    int retries = 0;

    __atomic_store_n(&priv->alarm_cancel, 1, __ATOMIC_RELEASE);
    (void)rte_eal_alarm_cancel(hns5_intr_lsc_poll, dev);

    if (priv->intr_fd < 0)
        return;

    (void)hidrv_mag_mac_enable_interrupt(priv->mag_pf_devid, priv->intr_fd, 0);

    do {
        ret = rte_intr_callback_unregister(&priv->intr_handle, hns5_intr_lsc_handler, dev);
        if (ret >= 0) {
            break;
        } else if (ret == -EAGAIN) {
            rte_delay_ms(HNS5_LSC_CB_UNREG_RETRY_MS);
            retries++;
        } else {
            hns5_warn(dev, "failed to unregister lsc callback, ret:%d\n", ret);
            break;
        }
    } while (retries < HNS5_LSC_CB_UNREG_MAX_RETRIES);
    if (retries == HNS5_LSC_CB_UNREG_MAX_RETRIES)
        hns5_warn(dev, "unregister lsc callback timeout after %d retries\n", retries);

    /* a handler that was already running may have re-enabled the irq */
    (void)hidrv_mag_mac_enable_interrupt(priv->mag_pf_devid, priv->intr_fd, 0);

    (void)hidrv_mag_mac_free_irq(priv->mag_pf_devid, priv->intr_fd);
    priv->intr_fd = -1;
    memset(&priv->intr_handle, 0, sizeof(priv->intr_handle));
}

static int hns5_intr_lsc_setup(struct rte_eth_dev *dev)
{
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);
    int ret;

    /* release the leftover irq from a previous start, keep setup idempotent */
    hns5_intr_lsc_teardown(dev);
    __atomic_store_n(&priv->alarm_cancel, 0, __ATOMIC_RELEASE);

    priv->intr_fd = hidrv_mag_mac_request_port_irq(priv->mag_pf_devid, priv->param.mport_id);
    if (priv->intr_fd < 0) {
        hns5_warn(dev, "failed to request lsc irq, ret:%d, fallback to polling\n", priv->intr_fd);
        priv->intr_fd = -1;
        goto start_poll;
    }

    memset(&priv->intr_handle, 0, sizeof(priv->intr_handle));
    priv->intr_handle.type = RTE_INTR_HANDLE_UIO;
    priv->intr_handle.fd = priv->intr_fd;
    priv->intr_handle.uio_cfg_fd = -1;

    ret = rte_intr_callback_register(&priv->intr_handle, hns5_intr_lsc_handler, dev);
    if (ret != 0) {
        hns5_warn(dev, "failed to register lsc callback, ret:%d, fallback to polling\n", ret);
        goto free_irq;
    }

    /* request_port_irq has enabled the interrupt, enable again to make the state explicit */
    ret = hidrv_mag_mac_enable_interrupt(priv->mag_pf_devid, priv->intr_fd, 1);
    if (ret != 0) {
        hns5_warn(dev, "failed to enable lsc interrupt, ret:%d, fallback to polling\n", ret);
        (void)rte_intr_callback_unregister(&priv->intr_handle, hns5_intr_lsc_handler, dev);
        goto free_irq;
    }

    hns5_info(dev, "port:%u lsc interrupt registered, fd:%d\n", dev->data->port_id, priv->intr_fd);
    goto start_poll;

free_irq:
    (void)hidrv_mag_mac_free_irq(priv->mag_pf_devid, priv->intr_fd);
    priv->intr_fd = -1;
    memset(&priv->intr_handle, 0, sizeof(priv->intr_handle));

start_poll:
    /* sync the current status so the first poll only reports a real change */
    (void)hns5_eth_link_update(dev, 0);
    priv->last_link_status = dev->data->dev_link.link_status;

    /*
     * the mac only raises an irq on link down, polling is what reports link up,
     * it also keeps the link status fresh for the application
     */
    ret = rte_eal_alarm_set(HNS5_LSC_POLL_INTERVAL_US, hns5_intr_lsc_poll, dev);
    if (ret != 0) {
        hns5_err(dev, "failed to start lsc poll alarm, ret:%d\n", ret);
        return ret;
    }
    return 0;
}

static int hns5_eth_dev_stop(struct rte_eth_dev *dev)
{
    uint32_t lcore_id = hns5_get_lcore_id();
    uint32_t vcpu = lcore_id * 2 + 1;
    int ret;

    /* hw is going down: the lsc irq would otherwise report on stale state */
    hns5_intr_lsc_teardown(dev);

    ret = hns5_eth_dev_set_link_down(dev);
    if (ret != 0) {
        hns5_err(dev, "failed to linkdown, ret:%d\n", ret);
        return ret;
    }

    hns5_stop_rx_queues(dev, vcpu);

    for (uint32_t i = 0; i < dev->data->nb_tx_queues; i++) {
        if (dev->data->tx_queue_state[i] != RTE_ETH_QUEUE_STATE_HAIRPIN)
            dev->data->tx_queue_state[i] = RTE_ETH_QUEUE_STATE_STOPPED;
    }
    dev->data->scattered_rx = false;
    dev->rx_pkt_burst = NULL;
    dev->tx_pkt_burst = NULL;

    return 0;
}

static int hns5_eth_dev_close(struct rte_eth_dev *dev)
{
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);
    handle vfhandle;
    int ret;

    vfhandle = hidrv_ppe_vf_get_ndrv_handle(priv->ppe_vf_devid);
    if (vfhandle != NULL) {
        ret = ndrv_ppe_mam_vf_disable_all_vlp(vfhandle);
        if (ret != 0)
            hns5_warn(dev, "failed to disable all vlp, ret:%d\n", ret);
    }

    for (int i = 0; i < HNS5_ETH_POOL_MAX; i++) {
        if ((priv->pool_inited & (1 << i)) != 0) {
            ret = hidrv_ppe_vf_close_pool(priv->ppe_vf_devid, i);
            if (ret != 0)
                hns5_warn(dev, "failed to close pool:%d, ret:%d\n", i, ret);
            priv->pool_inited &= ~(1 << i);
        }
    }

    // hns5_mp_clean_addr_trans_tbl(); // clean bmu blocks, has issues

    /* Clean RSS config (write to hardware) */
    hns5_rss_uninit(dev);

    /* hand back the bp_ppe slot and clear per-queue watermarks/source drop */
    (void)hns5_poe_bp_grp_release(dev);

    /* Clean mcast mode ingress flow (matches DMAC I/G bit), avoid residual hw flow table after close */
    if (priv->mcast_mode_flow_added)
        (void)hns5_ppe_lsw_mcast_mode_config(dev, 0);

    /* Delete the L2 MAC entry added at probe (random MAC) */
    (void)hns5_l2_del_entry(dev, priv->address.addr_bytes, 0);

    dev->data->mac_addrs = NULL;
    return 0;
}

static int hns5_eth_rxq_drain(struct hns5_eth_rx_queue *rxq)
{
    struct rte_mbuf *pkts[HNS5_ETH_RX_BURST_CNT];
    uint32_t polls = 0;

    while (polls++ < HNS5_RXQ_DRAIN_MAX_POLLS) {
        uint16_t nb_rx = hns5_eth_rx_burst(rxq, pkts, HNS5_ETH_RX_BURST_CNT);
        uint16_t i;

        if (nb_rx == 0)
            return 0;
        for (i = 0; i < nb_rx; i++)
            rte_pktmbuf_free(pkts[i]);
    }
    return -EBUSY;
}

/* True when @peer is another hns5 port sharing @dev's PPE, hence its RSS IDT. */
static bool hns5_eth_same_ppe(struct rte_eth_dev *dev, struct rte_eth_dev *peer)
{
    struct hns5_eth_priv *priv;
    struct hns5_eth_priv *peer_priv;

    if (dev == NULL || dev->data == NULL || peer == NULL || peer->data == NULL)
        return false;
    if (dev->device == NULL || peer->device == NULL ||
        peer->device->driver != dev->device->driver)
        return false;

    priv = hns5_eth_priv_get(dev);
    peer_priv = hns5_eth_priv_get(peer);
    return priv != NULL && peer_priv != NULL &&
        peer_priv->ppe_pf_devid == priv->ppe_pf_devid;
}

/* The RSS IDT is shared by all hns5 ports on one PPE, so queue start/stop on a
 * port changes steering for its peers; mirror the state and RETA copy. */
static void hns5_eth_rxq_state_sync_peers(struct rte_eth_dev *dev, uint16_t rx_queue_id,
    uint8_t state)
{
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);
    struct hns5_eth_priv *peer_priv;
    struct hns5_eth_rx_queue *peer_rxq;
    struct rte_eth_dev *peer;
    uint16_t port_id;

    for (port_id = 0; port_id < RTE_MAX_ETHPORTS; port_id++) {
        if (port_id == dev->data->port_id || !rte_eth_dev_is_valid_port(port_id))
            continue;

        peer = &rte_eth_devices[port_id];
        if (!hns5_eth_same_ppe(dev, peer))
            continue;
        if (rx_queue_id >= peer->data->nb_rx_queues)
            continue;

        peer_rxq = peer->data->rx_queues[rx_queue_id];
        if (peer_rxq == NULL || peer_rxq->type != HNS5_ETH_RXQ_TYPE_QUEUE ||
            peer_rxq->is_hairpin)
            continue;

        /* Mirror the RETA copy the caller just wrote to hardware. */
        peer_priv = hns5_eth_priv_get(peer);
        memcpy(peer_priv->rss.reta, priv->rss.reta, sizeof(priv->rss.reta));
        peer_priv->rss.reta_size = priv->rss.reta_size;

        /* Keep a peer with its POE input still disabled stopped, so its own
         * start call re-enables the input. */
        if (state == RTE_ETH_QUEUE_STATE_STARTED && peer_rxq->poe_enabled == 0)
            continue;

        peer->data->rx_queue_state[rx_queue_id] = state;
        hns5_dbg(peer, "sync rxq:%u state:%u from port:%u\n",
            rx_queue_id, state, dev->data->port_id);
    }
}

static int hns5_eth_rxq_acquire(struct rte_eth_dev *dev, uint16_t rx_queue_id,
    struct hns5_eth_rx_queue **rxq_out)
{
    struct hns5_eth_rx_queue *rxq;

    if (rx_queue_id >= dev->data->nb_rx_queues)
        return -EINVAL;
    rxq = dev->data->rx_queues[rx_queue_id];
    if (rxq == NULL)
        return -EINVAL;
    if (rxq->type != HNS5_ETH_RXQ_TYPE_QUEUE || rxq->is_hairpin)
        return -ENOTSUP;

    *rxq_out = rxq;
    return 0;
}

static int hns5_eth_rx_queue_stop(struct rte_eth_dev *dev, uint16_t rx_queue_id)
{
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);
    struct hns5_eth_rx_queue *rxq;
    uint16_t old_reta[HNS5_RSS_RETA_SIZE];
    int ret;

    ret = hns5_eth_rxq_acquire(dev, rx_queue_id, &rxq);
    if (ret != 0)
        return ret;

    /* RETA is shared by all ports on the PPE: the process-wide lock keeps the
     * RETA write and peer sync below serialized across ports. */
    hns5_rss_lock();
    if (dev->data->rx_queue_state[rx_queue_id] == RTE_ETH_QUEUE_STATE_STOPPED) {
        hns5_rss_unlock();
        return 0;
    }
    memcpy(old_reta, priv->rss.reta, sizeof(old_reta));

    /* Move future RSS traffic away before disabling the hardware queue. */
    ret = hns5_rss_queue_detach(dev, rx_queue_id);
    if (ret != 0)
        goto out;

    /* Arm the fw_tnl drop before closing POE, or earlier FD rules would hold
     * packets in POE forever. */
    hns5_poe_rxq_fw_tnl_drop_set(dev, rxq, true);

    /* Keep the input enabled when a fw_tnl drop exists: dropping at q_in_en
     * leaks the BMU buffer, and the last queue (RETA is left untouched once no
     * queue is active) would drain the pool. The MAM drop above discards before
     * allocation, so fall back to gating the input only without fw_tnl. */
    if (priv->fwl_tnl_inited != 1) {
        ret = hns5_poe_rxq_enable(rxq, false);
        if (ret != 0) {
            hns5_err(dev, "failed to disable rxq:%u, ret:%d\n", rx_queue_id, ret);
            goto rollback;
        }
    }

    /* The caller must have stopped its polling thread before this drain. */
    ret = hns5_eth_rxq_drain(rxq);
    if (ret != 0) {
        hns5_err(dev, "failed to drain rxq:%u, ret:%d\n", rx_queue_id, ret);
        if (priv->fwl_tnl_inited != 1)
            (void)hns5_poe_rxq_enable(rxq, true);
        goto rollback;
    }

    dev->data->rx_queue_state[rx_queue_id] = RTE_ETH_QUEUE_STATE_STOPPED;

out:
    /* Sync peers under the lock so detach + peer update stays atomic per PPE. */
    if (ret == 0 && priv->rss.reta_size != 0)
        hns5_eth_rxq_state_sync_peers(dev, rx_queue_id, RTE_ETH_QUEUE_STATE_STOPPED);
    hns5_rss_unlock();
    return ret;

rollback:
    (void)hns5_poe_rxq_fw_tnl_drop_set(dev, rxq, false);
    (void)hns5_rss_reta_restore(dev, old_reta);
    goto out;
}

static int hns5_eth_rx_queue_start(struct rte_eth_dev *dev, uint16_t rx_queue_id)
{
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);
    struct hns5_eth_rx_queue *rxq;
    int ret;

    ret = hns5_eth_rxq_acquire(dev, rx_queue_id, &rxq);
    if (ret != 0)
        return ret;

    hns5_rss_lock();
    if (dev->data->rx_queue_state[rx_queue_id] == RTE_ETH_QUEUE_STATE_STARTED) {
        hns5_rss_unlock();
        return 0;
    }

    /* Clear the fw_tnl drop first so the restored watermark is in effect
     * before the queue becomes visible to RSS/FD steering again. */
    hns5_poe_rxq_fw_tnl_drop_set(dev, rxq, false);

    /* Enable first so the queue is ready before new RETA entries point to it. */
    ret = hns5_poe_rxq_enable(rxq, true);
    if (ret != 0) {
        hns5_err(dev, "failed to enable rxq:%u, ret:%d\n", rx_queue_id, ret);
        (void)hns5_poe_rxq_fw_tnl_drop_set(dev, rxq, true);
        goto out;
    }

    ret = hns5_rss_queue_attach(dev, rx_queue_id);
    if (ret != 0) {
        hns5_err(dev, "failed to attach rxq:%u to RSS, ret:%d\n", rx_queue_id, ret);
        /* attach already rolled back its RETA change on failure. */
        (void)hns5_poe_rxq_enable(rxq, false);
        (void)hns5_eth_rxq_drain(rxq);
        (void)hns5_poe_rxq_fw_tnl_drop_set(dev, rxq, true);
        goto out;
    }

    dev->data->rx_queue_state[rx_queue_id] = RTE_ETH_QUEUE_STATE_STARTED;

out:
    /* Sync peers under the shared lock, see hns5_eth_rx_queue_stop(). */
    if (ret == 0 && priv->rss.reta_size != 0)
        hns5_eth_rxq_state_sync_peers(dev, rx_queue_id, RTE_ETH_QUEUE_STATE_STARTED);
    hns5_rss_unlock();
    return ret;
}

static struct hns5_eth_tx_queue *hns5_eth_txq_get(struct rte_eth_dev *dev, uint16_t tx_queue_id)
{
    if (tx_queue_id >= dev->data->nb_tx_queues || dev->data->tx_queues[tx_queue_id] == NULL) {
        hns5_err(dev, "%s, port:%u txq:%u nb_tx_queues:%u\n", strerror(EINVAL),
            dev->data->port_id, tx_queue_id, dev->data->nb_tx_queues);
        return NULL;
    }
    return dev->data->tx_queues[tx_queue_id];
}

static int hns5_eth_tx_queue_stop(struct rte_eth_dev *dev, uint16_t tx_queue_id)
{
    struct hns5_eth_tx_queue *txq = hns5_eth_txq_get(dev, tx_queue_id);

    if (txq == NULL)
        return -EINVAL;
    /* hns5 relies on hardware to release mbuf, without the need for software to perform mbuf release. */
    txq->stopped = 1;
    dev->data->tx_queue_state[tx_queue_id] = RTE_ETH_QUEUE_STATE_STOPPED;
    return 0;
}

static int hns5_eth_tx_queue_start(struct rte_eth_dev *dev, uint16_t tx_queue_id)
{
    struct hns5_eth_tx_queue *txq = hns5_eth_txq_get(dev, tx_queue_id);

    if (txq == NULL)
        return -EINVAL;
    /* hns5 relies on hardware to release mbuf, without the need for software to perform mbuf release. */
    txq->stopped = 0;
    dev->data->tx_queue_state[tx_queue_id] = RTE_ETH_QUEUE_STATE_STARTED;
    return 0;
}

static int hns5_eth_dev_start(struct rte_eth_dev *dev)
{
    int ret = 0;
    uint16_t real_rxq_num;
    uint32_t i;
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);
    struct rte_eth_conf *dev_conf = &dev->data->dev_conf;

    /* dev_start restores every queue to STARTED and hns5_config_rss() then
     * rebuilds the default equal-share RETA over all of them (queue start/stop
     * does the same), overwriting a customized weighting; apps must re-download
     * RETA after such changes. */
    for (i = 0; i < dev->data->nb_rx_queues; i++) {
        struct hns5_eth_rx_queue *rxq = dev->data->rx_queues[i];

        if (dev->data->rx_queue_state[i] == RTE_ETH_QUEUE_STATE_HAIRPIN)
            continue;
        if (rxq == NULL) {
            hns5_err(dev, "invalid rxq:%u\n", i);
            ret = -EINVAL;
            goto err_undo_rxq;
        }

        hns5_poe_rxq_fw_tnl_drop_set(dev, rxq, false);
        /* Both queue and group mode need every group queue input enabled. */
        ret = hns5_poe_rxq_enable(rxq, true);
        if (ret != 0) {
            hns5_err(dev, "failed to initialize rxq:%u state, ret:%d\n", i, ret);
            /* The input stayed disabled, keep steering to it dropped. */
            hns5_poe_rxq_fw_tnl_drop_set(dev, rxq, true);
            goto err_undo_rxq;
        }
        dev->data->rx_queue_state[i] = RTE_ETH_QUEUE_STATE_STARTED;
    }
    /* Below this index every non-hairpin RX queue was enabled above. */
    i = dev->data->nb_rx_queues;

    for (uint32_t j = 0; j < dev->data->nb_tx_queues; j++) {
        struct hns5_eth_tx_queue *txq = dev->data->tx_queues[j];

        if (txq != NULL)
            txq->stopped = 0;
        if (dev->data->tx_queue_state[j] != RTE_ETH_QUEUE_STATE_HAIRPIN)
            dev->data->tx_queue_state[j] = RTE_ETH_QUEUE_STATE_STARTED;
    }

    ret = hns5_eth_mag_init(dev);
    if (ret != 0) {
        hns5_err(dev, "failed to hns5_eth_mag_init, ret:%d\n", ret);
        goto err_undo_txq;
    }
    ret = hns5_eth_dev_mtu_set(dev, dev->data->mtu);
    if (ret != 0) {
        hns5_err(dev, "failed to hns5_eth_dev_mtu_set, ret:%d\n", ret);
        goto err_undo_txq;
    }

    real_rxq_num = hns5_rss_real_rxq_num(dev);
    if (real_rxq_num == 0) {
        hns5_warn(dev, "no hardware rx queue, skip rss config\n");
    } else if ((dev_conf->rxmode.mq_mode & ETH_MQ_RX_RSS_FLAG) ||
        (real_rxq_num == 1)) {
        ret = hns5_config_rss(dev);
        if (ret != 0) {
            hns5_err(dev, "failed to config RSS, ret:%d\n", ret);
            goto err_undo_txq;
        }
    }

    ret = hns5_eth_set_crc_offload(dev, priv->crc_offload);
    if (ret != 0) {
        hns5_err(dev, "failed to set CRC offload, ret:%d\n", ret);
        goto err_undo_txq;
    }

    ret = hns5_eth_dev_set_link_up(dev);
    if (ret != 0) {
        hns5_err(dev, "failed to linkup, ret:%d\n", ret);
        (void)hns5_eth_dev_set_link_down(dev);
        goto err_undo_txq;
    }

    if (dev_conf->rxmode.offloads & DEV_RX_OFFLOAD_SCATTER)
        dev->data->scattered_rx = true;
    /* Publish the data path only after every fallible step succeeded. */
    dev->rx_pkt_burst = hns5_eth_rx_burst;
    dev->tx_pkt_burst = hns5_eth_tx_burst;

    /* hw is up: register the link status change interrupt (polling fallback inside) */
    ret = hns5_intr_lsc_setup(dev);
    if (ret != 0)
        hns5_warn(dev, "failed to setup lsc, link status will not be notified, ret:%d\n", ret);

    hns5_info(dev, "port:%u succeed to start\n", dev->data->port_id);
    return 0;

    /* Unwind in reverse order. ethdev marks the port started only after this
     * returns 0, so rte_eth_dev_stop() can't be reused for rollback here. */
err_undo_txq:
    for (uint32_t j = 0; j < dev->data->nb_tx_queues; j++) {
        struct hns5_eth_tx_queue *txq = dev->data->tx_queues[j];

        if (txq != NULL)
            txq->stopped = 1;
        if (dev->data->tx_queue_state[j] != RTE_ETH_QUEUE_STATE_HAIRPIN)
            dev->data->tx_queue_state[j] = RTE_ETH_QUEUE_STATE_STOPPED;
    }
    dev->data->scattered_rx = false;

err_undo_rxq:
    /* Only RX queues below @i were enabled; the failed one re-armed its drop. */
    for (uint32_t j = 0; j < i; j++) {
        struct hns5_eth_rx_queue *rxq = dev->data->rx_queues[j];

        if (rxq == NULL || rxq->is_hairpin)
            continue;
        (void)hns5_poe_rxq_enable(rxq, false);
        hns5_poe_rxq_fw_tnl_drop_set(dev, rxq, true);
        dev->data->rx_queue_state[j] = RTE_ETH_QUEUE_STATE_STOPPED;
    }
    return ret;
}

static int hns5_rte_to_hidrv_port_speed(struct hns5_eth_priv *priv, uint32_t input_speed)
{
    switch (input_speed) {
        case ETH_LINK_SPEED_10M_HD:
            priv->param.port_speed = HIDRV_MAG_PORT_SPEED_10M;
            priv->param.duplex_mode = HIDRV_MAG_DUPLEX_MODE_HALF;
            break;
        case ETH_LINK_SPEED_10M:
            priv->param.port_speed = HIDRV_MAG_PORT_SPEED_10M;
            break;
        case ETH_LINK_SPEED_100M_HD:
            priv->param.port_speed = HIDRV_MAG_PORT_SPEED_100M;
            priv->param.duplex_mode = HIDRV_MAG_DUPLEX_MODE_HALF;
            break;
        case ETH_LINK_SPEED_100M:
            priv->param.port_speed = HIDRV_MAG_PORT_SPEED_100M;
            break;
        case ETH_LINK_SPEED_1G:
            priv->param.port_speed = HIDRV_MAG_PORT_SPEED_1000M;
            break;
        case ETH_LINK_SPEED_2_5G:
            priv->param.port_speed = HIDRV_MAG_PORT_SPEED_2G5;
            break;
        case ETH_LINK_SPEED_5G:
            priv->param.port_speed = HIDRV_MAG_PORT_SPEED_5G;
            break;
        case ETH_LINK_SPEED_10G:
            priv->param.port_speed = HIDRV_MAG_PORT_SPEED_10G;
            break;
        case ETH_LINK_SPEED_25G:
            priv->param.port_speed = HIDRV_MAG_PORT_SPEED_25G;
            break;
        case ETH_LINK_SPEED_100G:
            priv->param.port_speed = HIDRV_MAG_PORT_SPEED_100G;
            break;
        default:
            return -EINVAL;
    }
    return 0;
}

/* Set VLAN offload: configure VLAN strip per rxmode.offloads */
static int hns5_vlan_offload_set(struct rte_eth_dev *dev, int mask)
{
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);
    struct ndrv_ppe_lsw_port_egr_attr egr_attr = {0};
    unsigned int ret;

    if (mask == 0)
        return 0;

    if (mask & DEV_RX_OFFLOAD_VLAN_STRIP) {
        uint8_t strip = (dev->data->dev_conf.rxmode.offloads & DEV_RX_OFFLOAD_VLAN_STRIP) ? 1 : 0;

        /* Use MAC port egress attr as template to configure CPU port VLAN strip */
        ret = ndrv_ppe_lsw_pf_get_port_egress_attr(priv->ppe_pfhandle,
            NDRV_PPE_MAC_PORT_TYPE, priv->param.mport_id, &egr_attr);
        if (ret != 0) {
            hns5_err(dev, "get port egress attr failed, ret=%u\n", ret);
            return -EIO;
        }

        egr_attr.vlan_in_strip_en = strip;
        egr_attr.vlan_out_strip_en = strip;
        egr_attr.vlan_in_showtag = !strip;
        egr_attr.vlan_out_showtag = !strip;

        ret = ndrv_ppe_lsw_pf_set_port_egress_attr(priv->ppe_pfhandle,
            NDRV_PPE_CPU_PORT_TYPE, priv->param.cport_id, &egr_attr);
        if (ret != 0) {
            hns5_err(dev, "set port egress attr failed, ret=%u\n", ret);
            return -EIO;
        }
    }

    return 0;
}

/**
 * @brief Set port default VLAN (dev_ops.vlan_pvid_set callback, also reused when vlan_insert_config enabled)
 * @param dev eth device
 * @param vlan_id VLAN ID (ignored when on=0)
 * @param on 1-enable VLAN insert and set port default VLAN, 0-disable VLAN insert
 * @return int 0 on success, non-zero on failure
 */
int hns5_vlan_pvid_set(struct rte_eth_dev *dev, uint16_t vlan_id, int on)
{
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);
    struct ndrv_ppe_lsw_port_ing_vlan_attr vlan_attr = {0};
    unsigned int ret;

    if (priv->ppe_pfhandle == NULL) {
        hns5_err(dev, "ppe_pfhandle is NULL\n");
        return -EINVAL;
    }

    if (on) {
        priv->port_def_vlan = vlan_id;

        vlan_attr.def_vlan = vlan_id;
        vlan_attr.vlan_ad.vld1_zero0_pvid0 = NDRV_PPE_VLAN_ACTN_RPLC_VLAN;
        vlan_attr.vlan_ad.vld1_zero0_pvid1 = NDRV_PPE_VLAN_ACTN_NA;
        vlan_attr.vlan_ad.vld1_zero1_pvid0 = NDRV_PPE_VLAN_ACTN_RPLC_VLAN;
        vlan_attr.vlan_ad.vld1_zero1_pvid1 = NDRV_PPE_VLAN_ACTN_RPLC_VLAN;
        vlan_attr.vlan_ad.vld0 = NDRV_PPE_VLAN_ACTN_INS;

        ret = ndrv_ppe_lsw_pf_set_port_ingress_vlan_attr(priv->ppe_pfhandle,
            NDRV_PPE_CPU_PORT_TYPE, priv->param.cport_id, 1, &vlan_attr);
        if (ret != 0) {
            hns5_err(dev, "failed to set port ingress vlan attr, ret=%d\n", ret);
            return ret;
        }
        priv->vlan_pvid_en = 1;
    } else {
        priv->vlan_pvid_en = 0;
    }

    return 0;
}

/**
 * @brief User configures NIC parameters
 * @note Default input is 0 if not specified, init per mode
 * @note Only one speed can be passed for special config
 * @attention Autoneg is supported only by some modes
 */
static int hns5_eth_dev_configure(struct rte_eth_dev *dev)
{
    int ret = 0;
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);
    struct rte_eth_conf *dev_conf = &dev->data->dev_conf;
    uint32_t input_speed = 0;

    /* default single TC config */
    priv->num_tc = 1;
    priv->hw_tc_map = 0x01;  /* TC0 only */

    if (priv->param.port_mode >= HIDRV_MAG_PORT_MODE_INVALID) {
        hns5_err(dev, "invalid port mode:%u\n", priv->param.port_mode);
        return -EINVAL;
    }
    priv->param.duplex_mode = HIDRV_MAG_DUPLEX_MODE_FULL;
    if (dev_conf->link_speeds == ETH_LINK_SPEED_AUTONEG) {
        if (priv->param.port_mode == HIDRV_MAG_PORT_MODE_100BASE_FX || priv->param.port_mode == HIDRV_MAG_PORT_MODE_1000BASE_X ||
            priv->param.port_mode == HIDRV_MAG_PORT_MODE_SGMII || priv->param.port_mode == HIDRV_MAG_PORT_MODE_QSGMII)  {
            priv->param.autoneg = 1;
        }
        priv->param.port_speed = g_mag_mode_default_speed[priv->param.port_mode];
    } else {
        // invalid input: speed and autoneg passed together
        if (!(dev_conf->link_speeds & ETH_LINK_SPEED_FIXED)) {
            hns5_err(dev, "can not input speed in autoneg mode\n");
            return -EINVAL;
        }
        // invalid input: multiple speeds passed
        input_speed = dev_conf->link_speeds & ~ETH_LINK_SPEED_FIXED;
        if (input_speed & (input_speed - 1)) {
            hns5_err(dev, "can only input one speed\n");
            return -EINVAL;
        }
        // invalid input: speed not supported by mode
        if ((input_speed & g_mag_mode_support_speeds[priv->param.port_mode]) == 0) {
            hns5_err(dev, "unsupport port speed:0x%x for port mode:%u\n", input_speed, priv->param.port_mode);
            return -EINVAL;
        }
        ret = hns5_rte_to_hidrv_port_speed(priv, input_speed);
        if (ret != 0) {
            hns5_err(dev, "failed to get hidrv speed form 0x%x\n", input_speed);
            return ret;
        }
    }
    hns5_info(dev, "mode:%u speed:%u autoneg:%u duplex_mode:%u\n", priv->param.port_mode, priv->param.port_speed,
        priv->param.autoneg, priv->param.duplex_mode);

    if (dev_conf->rxmode.mq_mode & ETH_MQ_RX_RSS_FLAG) {
        ret = hns5_rss_apply_conf(dev);
        if (ret != 0) {
            hns5_err(dev, "failed to apply RSS conf, ret:%d\n", ret);
            return ret;
        }
    }

    priv->crc_offload = (dev_conf->rxmode.offloads & DEV_RX_OFFLOAD_KEEP_CRC) ? 0 : 1;

    /* Apply VLAN offload: keep hw consistent with dev_conf when app skips vlan_offload_set */
    ret = hns5_vlan_offload_set(dev, DEV_RX_OFFLOAD_VLAN_STRIP);
    if (ret != 0) {
        hns5_err(dev, "failed to apply VLAN offload config, ret:%d\n", ret);
        return ret;
    }

    /* Configure VLAN insert per txmode offloads, diff against flow table add state */
    uint8_t vlan_insert_en = (dev_conf->txmode.offloads & DEV_TX_OFFLOAD_VLAN_INSERT) ? 1 : 0;
    if (vlan_insert_en != priv->vlan_insert_flow_added) {
        ret = hns5_ppe_vlan_insert_config(dev, vlan_insert_en);
        if (ret != 0) {
            hns5_err(dev, "failed to apply VLAN insert config, ret:%d\n", ret);
            return ret;
        }
    }

    return 0;
}

static int hns5_eth_dev_infos_get(struct rte_eth_dev *dev, struct rte_eth_dev_info *dev_info)
{
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);

    dev_info->min_mtu = HNS5_MTU_MIN;
    dev_info->max_mtu = HNS5_MTU_MAX;
    dev_info->max_mac_addrs = 1;
    dev_info->max_rx_pktlen = PPE_SUPPORT_FRAME_MAX;
    /* RX queue limit set by POE CPU group queue count, see hns5_poe.h:HNS5_POE_CPU_GRP_QUE_NUM */
    dev_info->max_rx_queues = HNS5_POE_CPU_GRP_QUE_NUM;
    dev_info->max_tx_queues = (uint16_t)HNS5_ETH_MAX_TXQ_NUM;
    dev_info->min_rx_bufsize = 256;     // bmu buf granule 256 bytes
    dev_info->rx_offload_capa = DEV_RX_OFFLOAD_SCATTER | DEV_RX_OFFLOAD_RSS_HASH |
                                DEV_RX_OFFLOAD_KEEP_CRC | DEV_RX_OFFLOAD_VLAN_STRIP;
    dev_info->tx_offload_capa = DEV_TX_OFFLOAD_IPV4_CKSUM | DEV_TX_OFFLOAD_UDP_CKSUM |
                                DEV_TX_OFFLOAD_TCP_CKSUM | DEV_TX_OFFLOAD_VLAN_INSERT;
    dev_info->dev_capa = 0;
    dev_info->speed_capa = g_mag_mode_support_speeds[priv->param.port_mode];
    /* RSS capability */
    dev_info->flow_type_rss_offloads = HNS5_ETH_RSS_SUPPORT;
    dev_info->hash_key_size = HNS5_RSS_KEY_SIZE;
    dev_info->reta_size = HNS5_RSS_RETA_SIZE;
    return 0;
}

/* Hardware RX buffer size derived from the mempool layout. */
static uint32_t hns5_eth_rx_buf_size(struct rte_mempool *mp)
{
    return rte_pktmbuf_data_room_size(mp) - RTE_PKTMBUF_HEADROOM +
           rte_pktmbuf_ppe_skip(mp) + BMU_BD_LEN;
}

/**
 * @brief Init ppe pool
 * @attention Reentrant, guarded against duplicate init
 * @return ret: 0:succeed, < 0:failed
*/
static int hns5_eth_ppe_pool_init(struct rte_eth_dev *dev, struct rte_mempool *mp)
{
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);
    int ret = 0;
    struct hidrv_ppe_pool_conf pool_cnf = { 0 };

    if (dev == NULL || mp == NULL) {
        hns5_err(dev, "%s\n", strerror(EINVAL));
        return -EINVAL;
    }
    if ((priv->pool_inited & (1 << mp->pool_id)) != 0)
        return 0;
    pool_cnf.align_size = 0;
    pool_cnf.buffer_size = hns5_eth_rx_buf_size(mp);
    pool_cnf.sop_skip = rte_pktmbuf_ppe_skip(mp);
    pool_cnf.not_sop_skip = rte_pktmbuf_ppe_skip(mp);
    ret = hidrv_ppe_vf_open_pool(priv->ppe_vf_devid, mp->pool_id, &pool_cnf);
    if (ret != 0) {
        hns5_err(dev, "failed to hidrv_ppe_vf_open_pool, pool_id:%lu ret:%d\n", mp->pool_id, ret);
        return ret;
    }
    priv->pool_inited |= (1 << mp->pool_id);
    return 0;
}

/**
 * @brief Init pool bound to rxq
 * @return pool_id: >= 0:succeed, < 0:failed
 */
static int hns5_eth_rxq_pool_init(struct rte_eth_dev *dev, uint16_t rx_queue_id,
                                    struct rte_mempool *mp)
{
    int ret = 0;
    uint16_t pool_id = 0;

    if (mp == NULL)
        return 0;
    if (mp->pool_id >= HNS5_ETH_POOL_MAX) {
        hns5_err(dev, "%s, pool_id is more than max:%u\n", strerror(EINVAL), HNS5_ETH_POOL_MAX - 1);
        return -EINVAL;
    }
    pool_id = (uint16_t)mp->pool_id;
    ret = hns5_eth_ppe_pool_init(dev, mp);
    if (ret != 0) {
        hns5_err(dev, "failed to hns5_eth_ppe_pool_init, port:%d rxq:%u pool_id:%u\n",
            dev->data->port_id, rx_queue_id, pool_id);
        return ret;
    }
    return (int)pool_id;
}

/**
 * @brief Init RX queue, init poe queue and program fw_tnl
 * @attention fw_tnl/group etc. are derived inside driver from vdev devargs: group feeds
 *            poe group config via internal var, fw_tnl goes to a local var then to hns5_ppe_fwl_tnl_config
*/
static int hns5_eth_rx_queue_setup(struct rte_eth_dev *dev, uint16_t rx_queue_id,
                                    uint16_t nb_rx_desc,
                                    unsigned int socket_id __rte_unused,
                                    const struct rte_eth_rxconf *rx_conf,
                                    struct rte_mempool *mp)
{
    int ret = 0;
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);
    struct hns5_eth_rx_queue *rxq = NULL;
    int pool_id = 0;
    uint32_t tnl_num = 0;
    /* share_group derived from devargs; rx_conf saved for rxq info */
    uint16_t group = priv->param.share_group;
    /* fw_tnl derived from devargs into local var, then sent via hns5_ppe_fwl_tnl_config */
    struct rte_fw_tnl_conf fw_tnl_local = { 0 };

    if (rx_queue_id >= HNS5_POE_CPU_GRP_QUE_NUM) {
        hns5_err(dev, "%s, port:%u rx_queue_id:%u exceeds max:%u\n",
            strerror(EINVAL), dev->data->port_id, rx_queue_id,
            HNS5_POE_CPU_GRP_QUE_NUM - 1);
        return -EINVAL;
    }

    /* fw_tnl params derived from devargs into local vars:
     * tnl_num takes nb_rx_queues (valid after configure, safe at first rx_queue_setup),
     * bitfield caps at 4 bits (max 15), clamped; base_vqi=share_group*8; qos_sel/def_qos/def_qos_inc fixed */
    fw_tnl_local.enable = priv->param.fw_tnl_enable;
    fw_tnl_local.base_tnl = priv->param.base_tnl;
    tnl_num = dev->data->nb_rx_queues;
    if (tnl_num > 15) {
        hns5_warn(dev, "nb_rx_queues:%u exceeds fw_tnl tnl_num bitfield max:15, clamp to 15\n", tnl_num);
        tnl_num = 15;
    }
    fw_tnl_local.tnl_num = tnl_num;
    fw_tnl_local.base_vqi = priv->param.share_group * HNS5_POE_CPU_GRP_QUE_NUM;
    fw_tnl_local.qos_sel = 0;
    fw_tnl_local.def_qos = 0;
    fw_tnl_local.def_qos_inc = 1;

    if (group > HNS5_ETH_GROUP_MAX) {
        hns5_err(dev, "%s, port:%u rxq:%u share_group:0x%u\n",
            strerror(EINVAL), dev->data->port_id, rx_queue_id, group);
        return -EINVAL;
    }
    pool_id = hns5_eth_rxq_pool_init(dev, rx_queue_id, mp);
    if (pool_id < 0)
        return pool_id;

    rxq = priv->rxq + rx_queue_id;
    dev->data->rx_queues[rx_queue_id] = rxq;
    rxq->is_hairpin = false;
    rxq->pool_id = (uint16_t)pool_id;
    rxq->port_id = dev->data->port_id;
    rxq->mp = mp;
    rxq->nb_rx_desc = nb_rx_desc;
    rxq->conf = *rx_conf;
    rxq->group = group;
    rxq->queue = rx_queue_id;
    rxq->poe_vfhandle = priv->poe_vfhandle;
    rxq->type = (priv->param.rxq_mode == HNS5_ETH_RXQ_MODE_GROUP) ?
        HNS5_ETH_RXQ_TYPE_GROUP : HNS5_ETH_RXQ_TYPE_QUEUE;

    ret = hns5_eth_rx_queue_setup_poe(dev, rxq->group, nb_rx_desc);
    if (ret != 0) {
        hns5_err(dev, "failed to hns5_eth_rx_queue_setup_poe, port:%d rxq:%u group:%u ret:%d\n",
            dev->data->port_id, rx_queue_id, rxq->group, ret);
        goto err;
    }
    ret = hns5_ppe_fwl_tnl_config(dev, &fw_tnl_local, rxq->pool_id);
    if (ret != 0) {
        hns5_err(dev, "failed to hns5_ppe_fwl_tnl_config, port:%d rxq:%u pool_id:%u ret:%d\n",
            dev->data->port_id, rx_queue_id, rxq->pool_id, ret);
        goto err;
    }

    /* per-queue share of the group bp budget; excess is dropped before buffer alloc */
    hns5_poe_rxq_bp_quota_apply(dev, rxq);

    return 0;

err:
    /* Don't leave a half-initialized queue visible to dev_start/stop. */
    dev->data->rx_queues[rx_queue_id] = NULL;
    rxq->type = HNS5_ETH_RXQ_TYPE_INVALID;
    return ret;
}

static int hns5_eth_tx_queue_setup(struct rte_eth_dev *dev, uint16_t tx_queue_id,
    uint16_t nb_tx_desc, unsigned int socket_id __rte_unused,
    const struct rte_eth_txconf *tx_conf)
{
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);
    struct hns5_eth_tx_queue *txq = NULL;
    // queue spec check
    if (tx_conf == NULL || tx_queue_id >= HNS5_ETH_MAX_TXQ_NUM) {
        hns5_err(dev, "%s\n", strerror(EINVAL));
        return -EINVAL;
    }
    txq = priv->txq + tx_queue_id;
    dev->data->tx_queues[tx_queue_id] = (void *)txq;

    txq->is_hairpin = false;
    txq->port_id = dev->data->port_id;
    txq->tm_port = priv->param.cport_id;
    txq->tm_queue_id = priv->param.hw_txq_start + (tx_queue_id % priv->param.hw_txq_num);
    txq->poe_vfhandle = priv->poe_vfhandle;
    txq->priv = priv;
    txq->nb_tx_desc = nb_tx_desc;
    txq->conf = *tx_conf;

    hns5_eth_set_default_tx_msg(txq);
    hns5_dbg(dev, "portid:%u txq:%u tm_port:%u tm_queue:%u\n",
        txq->port_id, tx_queue_id, txq->tm_port, txq->tm_queue_id);
    return 0;
}

static void hns5_eth_rxq_info_get(struct rte_eth_dev *dev, uint16_t rx_queue_id,
                                  struct rte_eth_rxq_info *qinfo)
{
    struct hns5_eth_rx_queue *rxq = dev->data->rx_queues[rx_queue_id];

    qinfo->mp = rxq->mp;
    qinfo->nb_desc = rxq->nb_rx_desc;
    qinfo->conf = rxq->conf;
    qinfo->scattered_rx = dev->data->scattered_rx;
    if (rxq->mp != NULL)
        qinfo->rx_buf_size = (uint16_t)hns5_eth_rx_buf_size(rxq->mp);
}

static void hns5_eth_txq_info_get(struct rte_eth_dev *dev, uint16_t tx_queue_id,
                                  struct rte_eth_txq_info *qinfo)
{
    struct hns5_eth_tx_queue *txq = dev->data->tx_queues[tx_queue_id];

    qinfo->nb_desc = txq->nb_tx_desc;
    qinfo->conf = txq->conf;
}

static int hns5_eth_fec_get_capability(struct rte_eth_dev *dev, struct rte_eth_fec_capa *speed_fec_capa,
    unsigned int num)
{
    if (dev == NULL)
        return -EINVAL;
    if (speed_fec_capa == NULL)
        return RTE_DIM(g_speed_fec_capa_tbl);
    if (num < RTE_DIM(g_speed_fec_capa_tbl))
        return -EINVAL;
    memcpy(speed_fec_capa, g_speed_fec_capa_tbl, sizeof(g_speed_fec_capa_tbl));

    return 0;
}

static int hns5_eth_fec_get(struct rte_eth_dev *dev, uint32_t *fec_capa)
{
    int ret = 0;
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);
    enum hidrv_mag_fec_mode mag_fec_mode_hidrv = HIDRV_MAG_FEC_INVALID;

    ret = hidrv_mag_mac_get_fec_mode(priv->mag_pf_devid, priv->param.mport_id, &mag_fec_mode_hidrv);
    if (ret != 0 || mag_fec_mode_hidrv >= HIDRV_MAG_FEC_INVALID) {
        hns5_err(dev, "failed to hidrv_mag_mac_get_fec_mode, ret:%d fec_mode:%d\n", ret, mag_fec_mode_hidrv);
        return ret;
    }
    *fec_capa = g_mag_fec_mode_hidrv_to_rte[mag_fec_mode_hidrv];

    return 0;
}

static int hns5_eth_fec_set(struct rte_eth_dev *dev, uint32_t fec_capa)
{
    int ret = 0;
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);
    enum hidrv_mag_fec_mode mag_fec_mode_hidrv = HIDRV_MAG_FEC_INVALID;

    for (uint32_t i = 0; i < RTE_DIM(g_mag_fec_mode_hidrv_to_rte); i++) {
        if (g_mag_fec_mode_hidrv_to_rte[i] == fec_capa) {
            mag_fec_mode_hidrv = i;
            break;
        }
    }
    if (mag_fec_mode_hidrv == HIDRV_MAG_FEC_INVALID) {
        hns5_err(dev, "failed to find hidrv_mag_fec_mode, fec_capa:%u\n", fec_capa);
        return -EINVAL;
    }
    ret = hidrv_mag_mac_set_fec_mode(priv->mag_pf_devid, priv->param.mport_id, mag_fec_mode_hidrv);
    if (ret != 0) {
        hns5_err(dev, "failed to hidrv_mag_mac_set_fec_mode, ret:%d\n", ret);
        return ret;
    }

    return 0;
}

/**
 * @brief Get flow control status, only on/off supported, no parameter config
*/
static int hns5_eth_flow_ctrl_get(struct rte_eth_dev *dev, struct rte_eth_fc_conf *fc_conf)
{
    int ret = 0;
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);
    unsigned int pause_rx_enable = 0;
    unsigned int pause_tx_enable = 0;

    ret = hidrv_mag_get_pause_enable(priv->mag_pf_devid, priv->param.mport_id, HIDRV_MAG_DIR_RX, &pause_rx_enable);
    if (ret != 0) {
        hns5_err(dev, "failed to hidrv_mag_get_pause_enable, mport:%u dir:rx ret:%d\n", priv->param.mport_id, ret);
        return ret;
    }
    ret = hidrv_mag_get_pause_enable(priv->mag_pf_devid, priv->param.mport_id, HIDRV_MAG_DIR_TX, &pause_tx_enable);
    if (ret != 0) {
        hns5_err(dev, "failed to hidrv_mag_get_pause_enable, mport:%u dir:tx ret:%d\n", priv->param.mport_id, ret);
        return ret;
    }
    if (pause_rx_enable == 0 && pause_tx_enable == 0) {
        fc_conf->mode = RTE_FC_NONE;
    } else if (pause_rx_enable == 1 && pause_tx_enable == 0) {
        fc_conf->mode = RTE_FC_RX_PAUSE;
    } else if (pause_rx_enable == 0 && pause_tx_enable == 1) {
        fc_conf->mode = RTE_FC_TX_PAUSE;
    } else {
        fc_conf->mode = RTE_FC_FULL;
    }

    return 0;
}

static int hns5_eth_flow_ctrl_set(struct rte_eth_dev *dev, struct rte_eth_fc_conf *fc_conf)
{
    int ret = 0;
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);
    unsigned int rx_enable_get = 0;
    unsigned int tx_enable_get = 0;
    unsigned int rx_enable_set = 0;
    unsigned int tx_enable_set = 0;

    ret = hidrv_mag_get_pause_enable(priv->mag_pf_devid, priv->param.mport_id, HIDRV_MAG_DIR_RX, &rx_enable_get);
    if (ret != 0) {
        hns5_err(dev, "failed to hidrv_mag_get_pause_enable, mport:%u dir:rx ret:%d\n", priv->param.mport_id, ret);
        return ret;
    }
    ret = hidrv_mag_get_pause_enable(priv->mag_pf_devid, priv->param.mport_id, HIDRV_MAG_DIR_TX, &tx_enable_get);
    if (ret != 0) {
        hns5_err(dev, "failed to hidrv_mag_get_pause_enable, mport:%u dir:tx ret:%d\n", priv->param.mport_id, ret);
        return ret;
    }
    if (fc_conf->mode == RTE_FC_FULL) {
        rx_enable_set = 1;
        tx_enable_set = 1;
    } else if (fc_conf->mode == RTE_FC_RX_PAUSE) {
        rx_enable_set = 1;
    } else if (fc_conf->mode == RTE_FC_TX_PAUSE) {
        tx_enable_set = 1;
    }

    if (rx_enable_get != rx_enable_set) {
        ret = hidrv_mag_set_pause_enable(priv->mag_pf_devid, priv->param.mport_id, HIDRV_MAG_DIR_RX, rx_enable_set);
        if (ret != 0) {
            hns5_err(dev, "failed to hidrv_mag_set_pause_enable, mport:%u dir:rx ret:%d\n", priv->param.mport_id, ret);
            return ret;
        }
    }
    if (tx_enable_get != tx_enable_set) {
        ret = hidrv_mag_set_pause_enable(priv->mag_pf_devid, priv->param.mport_id, HIDRV_MAG_DIR_TX, tx_enable_set);
        if (ret != 0) {
            hns5_err(dev, "failed to hidrv_mag_set_pause_enable, mport:%u dir:tx ret:%d\n", priv->param.mport_id, ret);
            return ret;
        }
    }

    return 0;
}


static const char *MZ_RTE_ETH_DEV_RX_QUEUE = "rte_eth_dev_rx_queue";
static const char *MZ_RTE_ETH_DEV_TX_QUEUE = "rte_eth_dev_tx_queue";

/**
 * @brief Release RX queue
 * @param rxq  queue ptr from framework, unused
 */
static void hns5_eth_rx_queue_release(void *rxq)
{
    (void)rxq;
}

/**
 * @brief Release TX queue
 * @param txq  queue ptr from framework, unused
 */
static void hns5_eth_tx_queue_release(void *txq)
{
    (void)txq;
}

/* Packet types recognized on the driver RX path (aligned with hns5_l3_l4_to_ptype L3/L4 maps) */
static const uint32_t hns5_supported_ptypes[] = {
    RTE_PTYPE_L2_ETHER_ARP,
    RTE_PTYPE_L2_ETHER_LLDP,
    RTE_PTYPE_L2_ETHER_TIMESYNC,
    RTE_PTYPE_L3_IPV4,
    RTE_PTYPE_L3_IPV4_EXT,
    RTE_PTYPE_L3_IPV6,
    RTE_PTYPE_L3_IPV6_EXT,
    RTE_PTYPE_L4_ICMP,
    RTE_PTYPE_L4_IGMP,
    RTE_PTYPE_L4_SCTP,
    RTE_PTYPE_L4_TCP,
    RTE_PTYPE_L4_UDP,
    RTE_PTYPE_TUNNEL_ESP,
    RTE_PTYPE_TUNNEL_GRE,
    RTE_PTYPE_UNKNOWN,
};

static const uint32_t *
hns5_dev_supported_ptypes_get(__rte_unused struct rte_eth_dev *dev)
{
    return hns5_supported_ptypes;
}

static const struct eth_dev_ops hns5_eth_dev_ops = {
    .dev_configure = hns5_eth_dev_configure,
    .dev_infos_get = hns5_eth_dev_infos_get,
    .dev_supported_ptypes_get = hns5_dev_supported_ptypes_get,
    .dev_start = hns5_eth_dev_start,
    .dev_stop = hns5_eth_dev_stop,
    .dev_close = hns5_eth_dev_close,
    .dev_set_link_up = hns5_eth_dev_set_link_up,
    .dev_set_link_down = hns5_eth_dev_set_link_down,
    .rx_queue_setup = hns5_eth_rx_queue_setup,
    .rx_queue_release = hns5_eth_rx_queue_release,
    .rx_queue_start = hns5_eth_rx_queue_start,
    .rx_queue_stop = hns5_eth_rx_queue_stop,
    .tx_queue_setup = hns5_eth_tx_queue_setup,
    .tx_queue_start = hns5_eth_tx_queue_start,
    .tx_queue_stop = hns5_eth_tx_queue_stop,
    .tx_queue_release = hns5_eth_tx_queue_release,
    .rxq_info_get = hns5_eth_rxq_info_get,
    .txq_info_get = hns5_eth_txq_info_get,
    .link_update = hns5_eth_link_update,
    .stats_get = hns5_eth_stats_get,
    .stats_reset = hns5_eth_stats_reset,
    .xstats_get = hns5_eth_xstats_get,
    .xstats_reset = hns5_eth_xstats_reset,
    .xstats_get_names = hns5_eth_xstats_get_names,
    .flow_ctrl_get = hns5_eth_flow_ctrl_get,  // flow control
    .flow_ctrl_set = hns5_eth_flow_ctrl_set,
    .fec_get_capability = hns5_eth_fec_get_capability, // FEC
    .fec_get = hns5_eth_fec_get,
    .fec_set = hns5_eth_fec_set,
    .mtu_set = hns5_eth_dev_mtu_set,
    .filter_ctrl = hns5_dev_filter_ctrl,   // flow table (rte_flow)
    /* VLAN offload */
    .vlan_offload_set = hns5_vlan_offload_set,
    .vlan_pvid_set = hns5_vlan_pvid_set,
    /* RSS */
    .rss_hash_update = hns5_rss_hash_update,
    .rss_hash_conf_get = hns5_rss_hash_conf_get,
    .reta_update = hns5_rss_reta_update,
    .reta_query = hns5_rss_reta_query,
    .promiscuous_enable = hns5_promiscuous_enable,
    .promiscuous_disable = hns5_promiscuous_disable,
    .allmulticast_enable = hns5_allmulticast_enable,
    .allmulticast_disable = hns5_allmulticast_disable,
    .mac_addr_set = hns5_mac_addr_set,
	.hairpin_cap_get = hns5_eth_hairpin_cap_get,
	.rx_hairpin_queue_setup = hns5_eth_rx_hairpin_queue_setup,
	.tx_hairpin_queue_setup = hns5_eth_tx_hairpin_queue_setup,
	.hairpin_get_peer_ports = hns5_eth_hairpin_get_peer_ports,
	.hairpin_bind = hns5_eth_hairpin_bind,
	.hairpin_unbind = hns5_eth_hairpin_unbind,
};

bool hns5_eth_dev_is_hns5(uint16_t port_id)
{
	struct rte_eth_dev *dev;

	if (!rte_eth_dev_is_valid_port(port_id))
		return false;

	dev = &rte_eth_devices[port_id];
	if (dev->data == NULL || dev->data->dev_private == NULL)
		return false;

	return dev->dev_ops == &hns5_eth_dev_ops;
}

bool hns5_eth_has_ppe_peer(struct rte_eth_dev *dev)
{
    struct rte_eth_dev *peer;
    uint16_t port_id;

    if (dev == NULL || dev->data == NULL)
        return false;

    for (port_id = 0; port_id < RTE_MAX_ETHPORTS; port_id++) {
        if (port_id == dev->data->port_id || !rte_eth_dev_is_valid_port(port_id))
            continue;

        peer = &rte_eth_devices[port_id];
        if (hns5_eth_same_ppe(dev, peer))
            return true;
    }

    return false;
}

static struct rte_eth_dev *hns5_eth_dev_create(struct rte_vdev_device *vdev)
{
    struct rte_eth_dev_data *data = NULL;
    struct rte_eth_dev *dev = NULL;
    struct hns5_eth_priv *priv = NULL;
    const unsigned flags = 0;
    const struct rte_memzone *mz_rx = NULL;
    const struct rte_memzone *mz_tx = NULL;
    char dev_name[HNS5_ETH_DEV_INFO_NAME_LEN] = {0};
    const char *name = NULL;

    name = rte_vdev_device_name(vdev);
    if (name == NULL)
        return NULL;
    // private data mounted on eth_dev->data->dev_private
    dev = rte_eth_vdev_allocate(vdev, sizeof(struct hns5_eth_priv));
    if (dev == NULL)
        return NULL;
    data = dev->data;
    priv = data->dev_private;
    priv->intr_fd = -1; /* lsc irq not requested yet */

    data->mac_addrs = &priv->address;
    snprintf(dev_name, sizeof(dev_name), "%s_%d", MZ_RTE_ETH_DEV_RX_QUEUE, dev->data->port_id);
    mz_rx = rte_memzone_reserve(dev_name, sizeof(void *) * HNS5_ETH_MAX_RXQ_NUM, rte_socket_id(), flags);
    if (mz_rx == NULL) {
        rte_errno = ENOMEM;
        return NULL;
    }
    memset(mz_rx->addr, 0, sizeof(void *) * HNS5_ETH_MAX_RXQ_NUM);
    data->rx_queues = mz_rx->addr;

    snprintf(dev_name, sizeof(dev_name), "%s_%d", MZ_RTE_ETH_DEV_TX_QUEUE, dev->data->port_id);
    mz_tx = rte_memzone_reserve(dev_name, sizeof(void *) * HNS5_ETH_MAX_TXQ_NUM, rte_socket_id(), flags);
    if (mz_tx == NULL) {
        rte_errno = ENOMEM;
        (void)rte_memzone_free(mz_rx);
        return NULL;
    }
    memset(mz_tx->addr, 0, sizeof(void *) * HNS5_ETH_MAX_TXQ_NUM);
    data->tx_queues = mz_tx->addr;

    data->nb_rx_queues = (uint16_t)HNS5_ETH_MAX_RXQ_NUM;
    data->nb_tx_queues = (uint16_t)HNS5_ETH_MAX_TXQ_NUM;
    data->numa_node = rte_socket_id();

    priv->bp_grp = (uint8_t)HNS5_POE_BP_SLOT_INVALID;
    hns5_rss_set_default_args(priv);

    dev->dev_ops = &hns5_eth_dev_ops;

    return dev;
}

static int vdev_args_parse_uint32(__rte_unused const char *key, const char *value, void *opaque)
{
    uint32_t *val = opaque;
    char *endptr = NULL;
    unsigned long tmp = 0;

    if (value == NULL || opaque == NULL)
        return -EINVAL;
    errno = 0;
    // empty third arg auto-detects number base
    tmp = strtoul(value, &endptr, 0);

    if (errno != 0 || endptr == value || tmp > UINT32_MAX)
        return -EINVAL;

    *val = (uint32_t)tmp;
    return 0;
}

/* Read a u32 devarg; a missing optional key leaves *val untouched. */
static int hns5_kvargs_read_u32(const struct rte_kvargs *kvlist, const char *key,
                                uint32_t *val, bool required)
{
    int count = rte_kvargs_count(kvlist, key);

    if (count == 0)
        return required ? -EINVAL : 0;
    if (count != 1)
        return -EINVAL;
    return rte_kvargs_process(kvlist, key, &vdev_args_parse_uint32, val);
}

static const char *
hns5_kvargs_get(const struct rte_kvargs *kvlist, const char *key)
{
	unsigned int i;

	if (kvlist == NULL)
		return NULL;
	for (i = 0; i < kvlist->count; ++i) {
		if (key != NULL && strcmp(kvlist->pairs[i].key, key) != 0)
			continue;
		return kvlist->pairs[i].value;
	}
	return NULL;
}

/**
 * @attention Mismatched mode/speed errors at mac init, not checked here
*/
static int hns5_eth_mac_params_parse(struct rte_eth_dev *dev, struct rte_kvargs *kvlist,
    struct hns5_eth_priv *priv)
{
    const char *args_mode = NULL;

    args_mode = hns5_kvargs_get(kvlist, "mode");
    if (args_mode == NULL) {
        hns5_err(dev, "no args:mode\n");
        return -EINVAL;
    }
    priv->param.port_mode = HIDRV_MAG_PORT_MODE_INVALID;
    for (uint32_t i = 0; i < RTE_DIM(g_mag_mode_maps); i++) {
        if (strcmp(g_mag_mode_maps[i], args_mode) == 0) {
            priv->param.port_mode = i;
            break;
        }
    }
    if (priv->param.port_mode == HIDRV_MAG_PORT_MODE_INVALID) {
        hns5_err(dev, "invalid mode:%s\n", args_mode);
        return -EINVAL;
    }

    priv->param.port_speed = HIDRV_MAG_PORT_SPEED_NO_USE;

    return 0;
}

/**
* @brief Parse vdev args
*/
static int hns5_eth_params_parse(struct rte_vdev_device *vdev, struct rte_eth_dev *dev)
{
    int ret = 0;
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);
    const char *args = NULL;
    struct rte_kvargs *kvlist = NULL;

    args = rte_vdev_device_args(vdev);
    if (args == NULL) {
        hns5_err(dev, "failed to rte_vdev_device_args\n");
        return -EINVAL;
    }
    kvlist = rte_kvargs_parse(args, NULL);
    if (kvlist == NULL) {
        hns5_err(dev, "failed to rte_kvargs_parse\n");
        return -EINVAL;
    }
    if (hns5_kvargs_read_u32(kvlist, "port", &priv->param.cport_id, true) != 0)
        goto free_kvargs;

    /* share_group required, assigned externally to avoid group conflicts across ports */
    if (rte_kvargs_count(kvlist, "share_group") != 1) {
        hns5_err(dev, "share_group must be specified in devargs, assigned externally to avoid group conflicts\n");
        goto free_kvargs;
    }
    if (hns5_kvargs_read_u32(kvlist, "share_group", &priv->param.share_group, true) != 0)
        goto free_kvargs;
    if (priv->param.share_group > HNS5_ETH_GROUP_MAX) {
        hns5_err(dev, "invalid share_group:%u, max:%u\n", priv->param.share_group, HNS5_ETH_GROUP_MAX);
        goto free_kvargs;
    }

    /* rxq_mode optional, default queue mode (supports per-queue start/stop) */
    priv->param.rxq_mode = HNS5_ETH_RXQ_MODE_QUEUE;
    if (hns5_kvargs_read_u32(kvlist, "rxq_mode", &priv->param.rxq_mode, false) != 0)
        goto free_kvargs;
    if (priv->param.rxq_mode != HNS5_ETH_RXQ_MODE_QUEUE &&
        priv->param.rxq_mode != HNS5_ETH_RXQ_MODE_GROUP) {
        hns5_err(dev, "invalid rxq_mode:%u, expect 0(queue) or 1(group)\n",
            priv->param.rxq_mode);
        goto free_kvargs;
    }

    ret = hns5_eth_mac_params_parse(dev, kvlist, priv);
    if (ret != 0)
        goto free_kvargs;

    /* fw_tnl optional, enabled by default, devargs fw_tnl=0 disables */
    uint32_t fw_tnl_enable = 1;
    if (hns5_kvargs_read_u32(kvlist, "fw_tnl", &fw_tnl_enable, false) != 0)
        goto free_kvargs;
    if (fw_tnl_enable != 0 && fw_tnl_enable != 1) {
        hns5_err(dev, "invalid fw_tnl:%u, expect 0 or 1\n", fw_tnl_enable);
        goto free_kvargs;
    }
    priv->param.fw_tnl_enable = (uint8_t)fw_tnl_enable;
    /* base_tnl optional, default=cport_id*8 (step HNS5_POE_CPU_GRP_QUE_NUM, 8 per cport), devargs override */
    priv->param.base_tnl = priv->param.cport_id * HNS5_POE_CPU_GRP_QUE_NUM;
    if (hns5_kvargs_read_u32(kvlist, "base_tnl", &priv->param.base_tnl, false) != 0)
        goto free_kvargs;

    /* optional args, defaults used if absent */
    priv->param.mport_id = priv->param.cport_id;
    if (hns5_kvargs_read_u32(kvlist, "macport", &priv->param.mport_id, false) != 0)
        goto free_kvargs;
    /* cport/mport used as port bitmap index in (1U << id) masks, must be < 32 to avoid shift UB */
    if (priv->param.cport_id >= 32 || priv->param.mport_id >= 32) {
        hns5_err(dev, "invalid cport_id:%u/mport_id:%u, must be < 32\n",
            priv->param.cport_id, priv->param.mport_id);
        goto free_kvargs;
    }
    priv->param.hw_txq_num = HNS5_ETH_DEFAULT_HW_TXQ_NUM;
    if (hns5_kvargs_read_u32(kvlist, "hw_txq_num", &priv->param.hw_txq_num, false) != 0)
        goto free_kvargs;
    priv->param.hw_txq_start = priv->param.cport_id * 16; /* TM queue maps 16x port, matches default hw_txq_num=16 */
    if (hns5_kvargs_read_u32(kvlist, "hw_txq_id", &priv->param.hw_txq_start, false) != 0)
        goto free_kvargs;

    rte_kvargs_free(kvlist);

    hns5_info(dev, "cport:%u macport:%u mode:%u speed:%u share_group:%u rxq_mode:%s base_tnl:%u fw_tnl_enable:%u hw_txq_start:%u hw_txq_num:%u\n",
        priv->param.cport_id, priv->param.mport_id, priv->param.port_mode, priv->param.port_speed,
        priv->param.share_group,
        priv->param.rxq_mode == HNS5_ETH_RXQ_MODE_GROUP ? "group" : "queue",
        priv->param.base_tnl, priv->param.fw_tnl_enable,
        priv->param.hw_txq_start, priv->param.hw_txq_num);
    return 0;

free_kvargs:
    rte_kvargs_free(kvlist);
    return -EINVAL;
}

static int net_hns5_vdev_probe(struct rte_vdev_device *vdev)
{
    int ret = 0;
    struct rte_eth_dev *dev = NULL;

    dev = hns5_eth_dev_create(vdev);
    if (dev == NULL)
        return -EINVAL;
    ret = hns5_eth_params_parse(vdev, dev);
    if (ret != 0) {
        hns5_err(dev, "failed to hns5_eth_params_parse\n");
        return ret;
    }
    /* accelerator hardware init */
    ret = hns5_eth_poe_init(dev);
    if (ret != 0) {
        hns5_err(dev, "failed to hns5_eth_poe_init, ret:%d\n", ret);
        return ret;
    }
    ret = hns5_eth_ppe_init_lsw(dev);
    if (ret != 0) {
        hns5_err(dev, "failed to hns5_eth_ppe_init, ret:%d\n", ret);
        goto err_bp_grp;
    }
    ret = hns5_flow_init(dev);
    if (ret != 0) {
        hns5_err(dev, "failed to hns5_flow_init, ret:%d\n", ret);
        goto err_bp_grp;
    }
    if (hns5_get_tm_init_status() != true) {
        ret = hns5_tm_platform_dev_probe();
        if (ret != 0) {
            hns5_err(dev, "failed to hns5_tm_platform_dev_probe, ret:%d\n", ret);
            goto err_bp_grp;
        }
    }

    /* Generate random MAC and init L2 entry */
    ret = hns5_l2_mac_init(dev);
    if (ret != 0) {
        hns5_err(dev, "failed to hns5_l2_mac_init, ret:%d\n", ret);
        goto err_bp_grp;
    }

    rte_eth_dev_probing_finish(dev);
    return 0;

err_bp_grp:
    /* hns5_eth_poe_init() already claimed a bp slot; give it back on failure. */
    hns5_poe_bp_grp_release(dev);
    return ret;
}

static int net_hns5_vdev_remove(struct rte_vdev_device *vdev)
{
    const char *name = rte_vdev_device_name(vdev);
    struct rte_eth_dev *dev = (struct rte_eth_dev *)rte_eth_dev_allocated(name);
    char dev_name[HNS5_ETH_DEV_INFO_NAME_LEN] = {0};
    const struct rte_memzone *mz_rx = NULL;
    const struct rte_memzone *mz_tx = NULL;

    if (name == NULL)
        return -EINVAL;

    if (dev == NULL)
        return -EINVAL;

    (void)hns5_eth_dev_stop(dev);

    /* remove may happen without a prior close; return the bp_ppe slot here too */
    (void)hns5_poe_bp_grp_release(dev);

    rte_eth_dev_release_port(dev);
    snprintf(dev_name, sizeof(dev_name), "%s_%d", MZ_RTE_ETH_DEV_RX_QUEUE, dev->data->port_id);
    mz_rx = rte_memzone_lookup(dev_name);
    if (mz_rx != NULL)
        rte_memzone_free(mz_rx);
    snprintf(dev_name, sizeof(dev_name), "%s_%d", MZ_RTE_ETH_DEV_TX_QUEUE, dev->data->port_id);
    mz_tx = rte_memzone_lookup(dev_name);
    if (mz_tx != NULL)
        rte_memzone_free(mz_tx);
    return 0;
}

int hns5_set_ppe_bypass_mode(uint8_t port_id, enum hidrv_ppe_bypass_mode mode)
{
    struct rte_eth_dev *dev = &rte_eth_devices[port_id];
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);
    int ret;

    if (priv == NULL)
        return -EINVAL;

    if (mode != HIDRV_PPE_LSW_MODE && mode != HIDRV_PPE_BYPASS_MODE)
        return -EINVAL;

    ret = hidrv_ppe_pf_set_bypass(priv->ppe_pf_devid, priv->param.mport_id, HIDRV_PPE_DIR_RX,
        priv->param.cport_id, mode);
    if (ret != 0) {
        hns5_err(dev, "failed to hidrv_ppe_pf_set_bypass rx, ret:%d\n", ret);
        return ret;
    }

    ret = hidrv_ppe_pf_set_bypass(priv->ppe_pf_devid, priv->param.cport_id, HIDRV_PPE_DIR_TX,
        priv->param.mport_id, mode);
    if (ret != 0) {
        hns5_err(dev, "failed to hidrv_ppe_pf_set_bypass tx, ret:%d\n", ret);
        return ret;
    }

    return 0;
}

int hns5_lsw_mac_table_write(uint8_t port_id, struct rte_ether_addr *mac)
{
    struct rte_eth_dev *dev = &rte_eth_devices[port_id];
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);
    struct hidrv_lsw_l2_key hidrv_key = {0};
    struct hidrv_lsw_l2_actn hidrv_actn = {0};
    int ret;

    if (priv == NULL || mac == NULL)
        return -EINVAL;

    (void)memcpy(hidrv_key.mac, mac->addr_bytes, HNS5_ETH_ALEN);
    hidrv_key.vlan_id = 0;

    hidrv_actn.des_type = HIDRV_LSW_L2_DES_CPORT_PORT;
    hidrv_actn.des_id.des_cpu_port.port_id = priv->param.cport_id;
    hidrv_actn.static_item = 1;
    hidrv_actn.aging_en = 0;

    ret = hidrv_ppe_lsw_add_l2_entry(priv->ppe_pf_devid, &hidrv_key, &hidrv_actn);
    if (ret != 0) {
        hns5_err(dev, "failed to hidrv_ppe_lsw_add_l2_entry, ret:%d\n", ret);
        return ret;
    }

    return 0;
}

int hns5_promiscuous_enable(struct rte_eth_dev *dev)
{
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);
    int ret;

    if (priv == NULL)
        return -EINVAL;

    if (priv->promisc_mode == 1)
        return 0;

    priv->promisc_mode = 1;

    ret = hns5_set_ppe_bypass_mode(dev->data->port_id, HIDRV_PPE_BYPASS_MODE);
    if (ret != 0) {
        hns5_err(dev, "failed to set bypass mode, ret:%d\n", ret);
        priv->promisc_mode = 0;
        return ret;
    }

    hns5_dbg(dev, "port:%u promiscuous mode enabled\n", dev->data->port_id);

    return 0;
}

int hns5_promiscuous_disable(struct rte_eth_dev *dev)
{
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);
    int ret;

    if (priv == NULL)
        return -EINVAL;

    if (priv->promisc_mode == 0)
        return 0;

    priv->promisc_mode = 0;

    ret = hns5_set_ppe_bypass_mode(dev->data->port_id, HIDRV_PPE_LSW_MODE);
    if (ret != 0) {
        hns5_err(dev, "failed to set lsw mode, ret:%d\n", ret);
        priv->promisc_mode = 1;
        return ret;
    }

    hns5_dbg(dev, "port:%u promiscuous mode disabled\n", dev->data->port_id);
    return 0;
}

/* Enable mcast mode: add ingress flow matching DMAC I/G bit, send mcast to CPU/RX */
int hns5_allmulticast_enable(struct rte_eth_dev *dev)
{
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);
    int ret;

    if (priv == NULL)
        return -EINVAL;

    ret = hns5_ppe_lsw_mcast_mode_config(dev, 1);
    if (ret != 0) {
        hns5_err(dev, "failed to enable mcast mode, ret:%d\n", ret);
        return ret;
    }

    hns5_dbg(dev, "port:%u allmulticast mode enabled\n", dev->data->port_id);

    return 0;
}

/* Disable mcast mode: delete ingress flow matching DMAC I/G bit, drop mcast */
int hns5_allmulticast_disable(struct rte_eth_dev *dev)
{
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);
    int ret;

    if (priv == NULL)
        return -EINVAL;

    ret = hns5_ppe_lsw_mcast_mode_config(dev, 0);
    if (ret != 0) {
        hns5_err(dev, "failed to disable mcast mode, ret:%d\n", ret);
        return ret;
    }

    hns5_dbg(dev, "port:%u allmulticast mode disabled\n", dev->data->port_id);

    return 0;
}

static struct rte_vdev_driver net_hns5_pmd = {
    .probe =  net_hns5_vdev_probe,
    .remove = net_hns5_vdev_remove,
};

RTE_PMD_REGISTER_VDEV(net_hns5, net_hns5_pmd);
RTE_PMD_REGISTER_PARAM_STRING(net_hns5,
    "port=<0-31>"
    "macport=<0-31>"
    "share_group=<0-31>"
    "rxq_mode=<0(queue)|1(group)>"
    "fw_tnl=<0|1>"
    "base_tnl=<0-4095>"
    "hw_txq_id=<N>"
    "hw_txq_num=<N>"
    "mode=<100BASE_FX|1000BASE_X|SGMII|QSGMII|2G5BASE_X|10GBASE_R|FLEXMII_5G2P|FLEXMII_10G2P|FLEXMII_20G2P|\
    FLEXMII_10G4P|FLEXMII_20G4P|5GBASE_R|5GBASE_KR|XAUI|25GBASE_R|40GBASE_R|50GBASE_R|100GBASE_R>"
);
