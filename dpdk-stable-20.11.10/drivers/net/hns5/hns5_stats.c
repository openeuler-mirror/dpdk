/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026-2027 Huawei Technologies Co., Ltd
 */

#include <rte_mbuf.h>
#include <rte_net.h>
#include <rte_ethdev.h>
#include <rte_malloc.h>
#include <rte_memcpy.h>
#include <rte_memzone.h>
#include <rte_string_fns.h>
#include <rte_bus_vdev.h>
#include <rte_bus_pci.h>

#include "hidrv_tm_pf_api.h"
#include "hidrv_tm_vf_api.h"
#include "hidrv_mag_api.h"
#include "hidrv_ppe_pf_api.h"
#include "hidrv_ppe_vf_api.h"
#include "ndrv_poe_vf_api.h"
#include "ndrv_ppe_ssu_pf_api.h"
#include "ndrv_ppe_rpu_vf_api.h"
#include "ndrv_ppe_ppp_pf_api.h"
#include "ndrv_ppe_lsw_pf_api.h"

#include "limit.h"

#include "hns5_eth.h"
#include "hns5_stats.h"
#include "hns5_ppe.h"
#include "hns5_poe.h"
#include "hns5_tm.h"
#include "hns5_log.h"

#define HNS5_ETH_PORT_OFFSET(x)        (offsetof(struct hns5_eth_port_stats, x))
#define HNS5_MIB_STATS_OFFSET(x)       (HNS5_ETH_PORT_OFFSET(mib_stats) + offsetof(struct hidrv_mag_mac_mib_stats, x))
#define HNS5_RXQ_STATS_OFFSET(x)       (HNS5_ETH_PORT_OFFSET(rxq_stats) + offsetof(struct hns5_eth_rxq_stats, x))
#define HNS5_TXQ_STATS_OFFSET(x)       (HNS5_ETH_PORT_OFFSET(txq_stats) + offsetof(struct hns5_eth_txq_stats, x))
#define HNS5_DROP_STATS_OFFSET(x)      (HNS5_ETH_PORT_OFFSET(drop_stats) + offsetof(struct hns5_eth_drop_stats, x))

struct hns5_xstats_stats_name_offset {
    char name[RTE_ETH_XSTATS_NAME_SIZE];
    unsigned offset;
};

static const struct hns5_xstats_stats_name_offset hns5_xstats_strings[] = {
    { "mac_rx_bytes_total", HNS5_MIB_STATS_OFFSET(rx_bytes_total) },
    { "mac_rx_pkts_total", HNS5_MIB_STATS_OFFSET(rx_pkts_total) },
    { "mac_rx_bytes_good", HNS5_MIB_STATS_OFFSET(rx_bytes_good) },
    { "mac_rx_pkts_good", HNS5_MIB_STATS_OFFSET(rx_pkts_good) },
    { "mac_rx_bytes_bad", HNS5_MIB_STATS_OFFSET(rx_bytes_bad) },
    { "mac_rx_pkts_bad", HNS5_MIB_STATS_OFFSET(rx_pkts_bad) },
    { "mac_rx_pkts_unicast", HNS5_MIB_STATS_OFFSET(rx_pkts_unicast) },
    { "mac_rx_pkts_multicast", HNS5_MIB_STATS_OFFSET(rx_pkts_multicast) },
    { "mac_rx_pkts_broadcast", HNS5_MIB_STATS_OFFSET(rx_pkts_broadcast) },
    { "mac_rx_pkts_64_bytes", HNS5_MIB_STATS_OFFSET(rx_pkts_64_bytes) },
    { "mac_rx_pkts_65to127_bytes", HNS5_MIB_STATS_OFFSET(rx_pkts_65to127_bytes) },
    { "mac_rx_pkts_128to255_bytes", HNS5_MIB_STATS_OFFSET(rx_pkts_128to255_bytes) },
    { "mac_rx_pkts_256to511_bytes", HNS5_MIB_STATS_OFFSET(rx_pkts_256to511_bytes) },
    { "mac_rx_pkts_512to1023_bytes", HNS5_MIB_STATS_OFFSET(rx_pkts_512to1023_bytes) },
    { "mac_rx_pkts_1024to1518_bytes", HNS5_MIB_STATS_OFFSET(rx_pkts_1024to1518_bytes) },
    { "mac_rx_pkts_1519to2047_bytes", HNS5_MIB_STATS_OFFSET(rx_pkts_1519to2047_bytes) },
    { "mac_rx_pkts_2048to4095_bytes", HNS5_MIB_STATS_OFFSET(rx_pkts_2048to4095_bytes) },
    { "mac_rx_pkts_4096to8191_bytes", HNS5_MIB_STATS_OFFSET(rx_pkts_4096to8191_bytes) },
    { "mac_rx_pkts_8192to9216_bytes", HNS5_MIB_STATS_OFFSET(rx_pkts_8192to9216_bytes) },
    { "mac_rx_pkts_9217to12287_bytes", HNS5_MIB_STATS_OFFSET(rx_pkts_9217to12287_bytes) },
    { "mac_rx_pkts_12288to16383_bytes", HNS5_MIB_STATS_OFFSET(rx_pkts_12288to16383_bytes) },
    { "mac_rx_pkts_1519tomax_bytes_bad", HNS5_MIB_STATS_OFFSET(rx_pkts_1519tomax_bytes_bad) },
    { "mac_rx_pkts_1519tomax_bytes_good", HNS5_MIB_STATS_OFFSET(rx_pkts_1519tomax_bytes_good) },
    { "mac_rx_pkts_undermin", HNS5_MIB_STATS_OFFSET(rx_pkts_undermin) },
    { "mac_rx_pkts_undersize", HNS5_MIB_STATS_OFFSET(rx_pkts_undersize) },
    { "mac_rx_pkts_fragment", HNS5_MIB_STATS_OFFSET(rx_pkts_fragment) },
    { "mac_rx_pkts_oversize", HNS5_MIB_STATS_OFFSET(rx_pkts_oversize) },
    { "mac_rx_pkts_jabber", HNS5_MIB_STATS_OFFSET(rx_pkts_jabber) },
    { "mac_rx_pkts_fcs_err", HNS5_MIB_STATS_OFFSET(rx_pkts_fcs_err) },
    { "mac_rx_pkts_align_err", HNS5_MIB_STATS_OFFSET(rx_pkts_align_err) },
    { "mac_rx_pkts_symbol_err", HNS5_MIB_STATS_OFFSET(rx_pkts_symbol_err) },
    { "mac_rx_pkts_unfilter", HNS5_MIB_STATS_OFFSET(rx_pkts_unfilter) },
    { "mac_rx_bytes_segment", HNS5_MIB_STATS_OFFSET(rx_bytes_segment) },
    { "mac_rx_pkts_send_from_app_good", HNS5_MIB_STATS_OFFSET(rx_pkts_send_from_app_good) },
    { "mac_rx_pkts_send_from_app_bad", HNS5_MIB_STATS_OFFSET(rx_pkts_send_from_app_bad) },
    { "mac_rx_pkts_pause", HNS5_MIB_STATS_OFFSET(rx_pkts_pause) },
    { "mac_rx_pkts_pfc", HNS5_MIB_STATS_OFFSET(rx_pkts_pfc) },
    { "mac_rx_pkts_pri0_pause", HNS5_MIB_STATS_OFFSET(rx_pkts_pri0_pause) },
    { "mac_rx_pkts_pri1_pause", HNS5_MIB_STATS_OFFSET(rx_pkts_pri1_pause) },
    { "mac_rx_pkts_pri2_pause", HNS5_MIB_STATS_OFFSET(rx_pkts_pri2_pause) },
    { "mac_rx_pkts_pri3_pause", HNS5_MIB_STATS_OFFSET(rx_pkts_pri3_pause) },
    { "mac_rx_pkts_pri4_pause", HNS5_MIB_STATS_OFFSET(rx_pkts_pri4_pause) },
    { "mac_rx_pkts_pri5_pause", HNS5_MIB_STATS_OFFSET(rx_pkts_pri5_pause) },
    { "mac_rx_pkts_pri6_pause", HNS5_MIB_STATS_OFFSET(rx_pkts_pri6_pause) },
    { "mac_rx_pkts_pri7_pause", HNS5_MIB_STATS_OFFSET(rx_pkts_pri7_pause) },
    { "mac_rx_pkts_macctrl", HNS5_MIB_STATS_OFFSET(rx_pkts_macctrl) },
    { "mac_rx_pkts_1731", HNS5_MIB_STATS_OFFSET(rx_pkts_1731) },
    { "mac_rx_pkts_merge_frame_ass_error", HNS5_MIB_STATS_OFFSET(rx_pkts_merge_frame_ass_error) },
    { "mac_rx_pkts_merge_frame_ass_ok", HNS5_MIB_STATS_OFFSET(rx_pkts_merge_frame_ass_ok) },
    { "mac_rx_pkts_merge_frame_frag_count", HNS5_MIB_STATS_OFFSET(rx_pkts_merge_frame_frag_count) },
    { "mac_rx_pkts_merge_frame_smd_error", HNS5_MIB_STATS_OFFSET(rx_pkts_merge_frame_smd_error) },

    { "mac_tx_bytes_total", HNS5_MIB_STATS_OFFSET(tx_bytes_total) },
    { "mac_tx_pkts_total", HNS5_MIB_STATS_OFFSET(tx_pkts_total) },
    { "mac_tx_bytes_good", HNS5_MIB_STATS_OFFSET(tx_bytes_good) },
    { "mac_tx_pkts_goodpkts", HNS5_MIB_STATS_OFFSET(tx_pkts_goodpkts) },
    { "mac_tx_bytes_bad", HNS5_MIB_STATS_OFFSET(tx_bytes_bad) },
    { "mac_tx_pkts_bad", HNS5_MIB_STATS_OFFSET(tx_pkts_bad) },
    { "mac_tx_pkts_unicast", HNS5_MIB_STATS_OFFSET(tx_pkts_unicast) },
    { "mac_tx_pkts_multicast", HNS5_MIB_STATS_OFFSET(tx_pkts_multicast) },
    { "mac_tx_pkts_broadcast", HNS5_MIB_STATS_OFFSET(tx_pkts_broadcast) },
    { "mac_tx_pkts_64bytes", HNS5_MIB_STATS_OFFSET(tx_pkts_64bytes) },
    { "mac_tx_pkts_65to127bytes", HNS5_MIB_STATS_OFFSET(tx_pkts_65to127bytes) },
    { "mac_tx_pkts_128to255bytes", HNS5_MIB_STATS_OFFSET(tx_pkts_128to255bytes) },
    { "mac_tx_pkts_256to511bytes", HNS5_MIB_STATS_OFFSET(tx_pkts_256to511bytes) },
    { "mac_tx_pkts_512to1023bytes", HNS5_MIB_STATS_OFFSET(tx_pkts_512to1023bytes) },
    { "mac_tx_pkts_1024to1518bytes", HNS5_MIB_STATS_OFFSET(tx_pkts_1024to1518bytes) },
    { "mac_tx_pkts_1519to2047bytes", HNS5_MIB_STATS_OFFSET(tx_pkts_1519to2047bytes) },
    { "mac_tx_pkts_2048to4095bytes", HNS5_MIB_STATS_OFFSET(tx_pkts_2048to4095bytes) },
    { "mac_tx_pkts_4096to8191bytes", HNS5_MIB_STATS_OFFSET(tx_pkts_4096to8191bytes) },
    { "mac_tx_pkts_8192to9216bytes", HNS5_MIB_STATS_OFFSET(tx_pkts_8192to9216bytes) },
    { "mac_tx_pkts_9217to12287bytes", HNS5_MIB_STATS_OFFSET(tx_pkts_9217to12287bytes) },
    { "mac_tx_pkts_12288to16383bytes", HNS5_MIB_STATS_OFFSET(tx_pkts_12288to16383bytes) },
    { "mac_tx_pkts_1519tomaxbytesgood", HNS5_MIB_STATS_OFFSET(tx_pkts_1519tomaxbytesgood) },
    { "mac_tx_pkts_1519tomaxbytesbad", HNS5_MIB_STATS_OFFSET(tx_pkts_1519tomaxbytesbad) },
    { "mac_tx_pkts_undermin", HNS5_MIB_STATS_OFFSET(tx_pkts_undermin) },
    { "mac_tx_pkts_undersize", HNS5_MIB_STATS_OFFSET(tx_pkts_undersize) },
    { "mac_tx_pkts_fragment", HNS5_MIB_STATS_OFFSET(tx_pkts_fragment) },
    { "mac_tx_pkts_oversize", HNS5_MIB_STATS_OFFSET(tx_pkts_oversize) },
    { "mac_tx_pkts_jabber", HNS5_MIB_STATS_OFFSET(tx_pkts_jabber) },
    { "mac_tx_pkts_errall", HNS5_MIB_STATS_OFFSET(tx_pkts_errall) },
    { "mac_tx_pkts_fromappgood", HNS5_MIB_STATS_OFFSET(tx_pkts_fromappgood) },
    { "mac_tx_pkts_fromappbad", HNS5_MIB_STATS_OFFSET(tx_pkts_fromappbad) },
    { "mac_tx_pkts_pause", HNS5_MIB_STATS_OFFSET(tx_pkts_pause) },
    { "mac_tx_pkts_pfc", HNS5_MIB_STATS_OFFSET(tx_pkts_pfc) },
    { "mac_tx_pkts_pri0pause", HNS5_MIB_STATS_OFFSET(tx_pkts_pri0pause) },
    { "mac_tx_pkts_pri1pause", HNS5_MIB_STATS_OFFSET(tx_pkts_pri1pause) },
    { "mac_tx_pkts_pri2pause", HNS5_MIB_STATS_OFFSET(tx_pkts_pri2pause) },
    { "mac_tx_pkts_pri3pause", HNS5_MIB_STATS_OFFSET(tx_pkts_pri3pause) },
    { "mac_tx_pkts_pri4pause", HNS5_MIB_STATS_OFFSET(tx_pkts_pri4pause) },
    { "mac_tx_pkts_pri5pause", HNS5_MIB_STATS_OFFSET(tx_pkts_pri5pause) },
    { "mac_tx_pkts_pri6pause", HNS5_MIB_STATS_OFFSET(tx_pkts_pri6pause) },
    { "mac_tx_pkts_pri7pause", HNS5_MIB_STATS_OFFSET(tx_pkts_pri7pause) },
    { "mac_tx_pkts_macctrl", HNS5_MIB_STATS_OFFSET(tx_pkts_macctrl) },
    { "mac_tx_pkts_1731", HNS5_MIB_STATS_OFFSET(tx_pkts_1731) },
    { "mac_tx_pkts_1588", HNS5_MIB_STATS_OFFSET(tx_pkts_1588) },
    { "mac_tx_pkts_deferral", HNS5_MIB_STATS_OFFSET(tx_pkts_deferral) },
    { "mac_tx_pkts_single_collision", HNS5_MIB_STATS_OFFSET(tx_pkts_single_collision) },
    { "mac_tx_pkts_multiple_collision", HNS5_MIB_STATS_OFFSET(tx_pkts_multiple_collision) },
    { "mac_tx_pkts_excessive_collision", HNS5_MIB_STATS_OFFSET(tx_pkts_excessive_collision) },
    { "mac_tx_pkts_late_collision", HNS5_MIB_STATS_OFFSET(tx_pkts_late_collision) },
    { "mac_tx_pkts_crs_lost", HNS5_MIB_STATS_OFFSET(tx_pkts_crs_lost) },
    { "mac_tx_pkts_merge_frame_ass_error", HNS5_MIB_STATS_OFFSET(tx_pkts_merge_frame_ass_error) },
    { "mac_tx_pkts_merge_frame_ass_ok", HNS5_MIB_STATS_OFFSET(tx_pkts_merge_frame_ass_ok) },
    { "mac_tx_pkts_merge_frame_frag", HNS5_MIB_STATS_OFFSET(tx_pkts_merge_frame_frag) },

    { "ipackets", HNS5_RXQ_STATS_OFFSET(ipackets) },
    { "ibytes", HNS5_RXQ_STATS_OFFSET(ibytes) },
    { "ierrors_msg", HNS5_RXQ_STATS_OFFSET(ierrors_msg) },
    { "ierrors_rxd", HNS5_RXQ_STATS_OFFSET(ierrors_rxd) },

    { "opackets", HNS5_TXQ_STATS_OFFSET(opackets) },
    { "obytes", HNS5_TXQ_STATS_OFFSET(obytes) },
    { "oerrors_txd", HNS5_TXQ_STATS_OFFSET(oerrors_txd) },
    { "oerrors_msg", HNS5_TXQ_STATS_OFFSET(oerrors_msg) },
    { "oerrors_cache_full", HNS5_TXQ_STATS_OFFSET(oerrors_cache_full) },

    /* PPE hardware drop counters, refreshed from the SDK ndrv interfaces. */
    { "ppe_ssu_ppp_send_drop_cmd", HNS5_DROP_STATS_OFFSET(ssu_ppp_send_drop_cmd) },
    { "ppe_ssu_recv_drop_cmd", HNS5_DROP_STATS_OFFSET(ssu_recv_drop_cmd) },
    { "ppe_ssu_rx_pkt_full_drop", HNS5_DROP_STATS_OFFSET(ssu_rx_pkt_full_drop) },

    { "ppe_ppp_fw_fd_stg1_drop", HNS5_DROP_STATS_OFFSET(ppp_fw_fd_stg1_drop) },
    { "ppe_lsw_ing_fd_drop", HNS5_DROP_STATS_OFFSET(lsw_ing_fd_drop) },

    /* PPP LSW L2 table lookup miss counter */
    { "ppe_l2_dmac_miss_drop", HNS5_DROP_STATS_OFFSET(l2_dmac_miss_drop) },

    { "ppe_rpu_buf_drop_int", HNS5_DROP_STATS_OFFSET(rpu_buf_drop_int) },
    { "ppe_rpu_recv_err_pkt", HNS5_DROP_STATS_OFFSET(rpu_recv_err_pkt) },
    { "ppe_rpu_prep_drop_pkt", HNS5_DROP_STATS_OFFSET(rpu_prep_drop_pkt) },

    { "tm_vf_tail_drop", HNS5_DROP_STATS_OFFSET(tm_vf_tail_drop) },
    { "ppe_poe_add_fail_drop", HNS5_DROP_STATS_OFFSET(poe_add_fail_drop) },
};

#define HNS5_NUM_MAC_MIB_STATS (sizeof(struct hidrv_mag_mac_mib_stats) / sizeof(unsigned long long))

static void hns5_eth_xstats_soft_update(struct rte_eth_dev *dev, struct hns5_eth_priv *priv)
{
    memset(&priv->xstats.rxq_stats, 0, sizeof(struct hns5_eth_rxq_stats));
    memset(&priv->xstats.txq_stats, 0, sizeof(struct hns5_eth_txq_stats));

    for (uint32_t i = 0; i < dev->data->nb_rx_queues; i++) {
        priv->xstats.rxq_stats.ipackets += priv->rxq[i].stats.ipackets;
        priv->xstats.rxq_stats.ibytes += priv->rxq[i].stats.ibytes;
        priv->xstats.rxq_stats.ierrors_msg += priv->rxq[i].stats.ierrors_msg;
        priv->xstats.rxq_stats.ierrors_rxd += priv->rxq[i].stats.ierrors_rxd;
    }
    for (uint32_t i = 0; i < dev->data->nb_tx_queues; i++) {
        priv->xstats.txq_stats.opackets += priv->txq[i].stats.opackets;
        priv->xstats.txq_stats.obytes += priv->txq[i].stats.obytes;
        priv->xstats.txq_stats.oerrors_txd += priv->txq[i].stats.oerrors_txd;
        priv->xstats.txq_stats.oerrors_msg += priv->txq[i].stats.oerrors_msg;
        priv->xstats.txq_stats.oerrors_cache_full += priv->txq[i].stats.oerrors_cache_full;
    }
}

static void hns5_eth_add_mib(struct hidrv_mag_mac_mib_stats *dst_mib, struct hidrv_mag_mac_mib_stats *src_mib)
{
    uint16_t i;
    uint64_t *src_value;
    uint64_t *dst_value;

    src_value = (uint64_t *)((char *)src_mib);
    dst_value = (uint64_t *)((char *)dst_mib);
    for (i = 0; i < HNS5_NUM_MAC_MIB_STATS; i++) {
        // hidrv_mag_mac_get_mib results: unsupported fields are all 0xff
        if (*src_value != 0xffffffffffffffff) {
            *dst_value += *src_value;
        }
        src_value++;
        dst_value++;
    }
}

static void hns5_eth_xstats_mib_update(struct rte_eth_dev *dev)
{
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);
    struct hidrv_mag_mac_mib_stats mib = { 0 };
    int ret = 0;

    /* MAG is set up in dev_start; probe-time baseline runs before that. */
    if (priv->mag_handle == NULL)
        return;

    ret = hidrv_mag_mac_get_mib(priv->mag_pf_devid, priv->param.mport_id, &mib);
    if (ret != 0) {
        hns5_err(dev, "failed to hidrv_mag_mac_get_mib, ret:%d", ret);
        return;
    }

    /* MIB counters are read-clear: the first read only consumes init traffic. */
    if (priv->mib_inited == 0) {
        priv->mib_inited = 1;
        return;
    }

    (void)hns5_eth_add_mib(&priv->xstats.mib_stats, &mib);
}

/*
 * Accumulate the PPE drop counters. All of them (SSU/PPP/LSW/L2/RPU) are
 * read-clear: the BSP returns the increment since the previous query, so each
 * successful read is added as-is. A failed read leaves its fields zero and
 * contributes nothing. The first query only consumes the counters.
 */
static void hns5_eth_xstats_drop_update(struct rte_eth_dev *dev)
{
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);
    struct hns5_eth_drop_stats cur = { 0 };
    const uint64_t *curv = (const uint64_t *)&cur;
    uint64_t *total = (uint64_t *)&priv->xstats.drop_stats;
    const uint32_t drop_cnt = sizeof(struct hns5_eth_drop_stats) / sizeof(uint64_t);
    int first = (priv->drop_inited == 0);
    uint32_t i;
    struct ndrv_ppe_ssu_pkt_drop_cnt ssu_drop = { 0 };
    struct ndrv_ppp_pf_fw_fd_hit_cnt ppp_hit = { 0 };
    struct ndrv_ppe_lsw_pf_ing_fd_hit_cnt lsw_hit = { 0 };
    struct ndrv_ppe_lsw_pf_l2_pkt_cnt l2_cnt = { 0 };
    struct ndrv_ppe_rpu_vf_err_cnt rpu_err = { 0 };
    struct hidrv_tm_vf_total_cnt tm_cnt = { 0 };
    struct ndrv_poe_vf_pkt_info poe_info = { 0 };
    handle pfhandle = priv->ppe_pfhandle;
    handle vfhandle = priv->ppe_vfhandle;

    if (pfhandle == NULL || vfhandle == NULL) {
        return;
    }

    /* SSU packet-drop counters */
    if (ndrv_ppe_ssu_pf_get_pkt_drop_cnt(pfhandle, &ssu_drop) != 0) {
        hns5_dbg(dev, "ndrv_ppe_ssu_pf_get_pkt_drop_cnt failed\n");
    } else {
        cur.ssu_ppp_send_drop_cmd = ssu_drop.ppp_send_cmd;
        cur.ssu_recv_drop_cmd = ssu_drop.ssu_recv_cmd;
        cur.ssu_rx_pkt_full_drop = ssu_drop.rx_pkt_full;
    }

    /* PPP stage-1 flow-table drop (rte_flow DROP without ingress attr) */
    if (ndrv_ppe_ppp_pf_get_fw_fd_hit_cnt(pfhandle, &ppp_hit) != 0) {
        hns5_dbg(dev, "ndrv_ppe_ppp_pf_get_fw_fd_hit_cnt failed\n");
    } else {
        cur.ppp_fw_fd_stg1_drop = ppp_hit.stg_1_hit_drop;
    }

    /* LSW ingress flow-table drop (rte_flow DROP with ingress attr) */
    if (ndrv_ppe_lsw_pf_get_ing_fd_hit_cnt(pfhandle, &lsw_hit) != 0) {
        hns5_dbg(dev, "ndrv_ppe_lsw_pf_get_ing_fd_hit_cnt failed\n");
    } else {
        cur.lsw_ing_fd_drop = lsw_hit.drop;
    }

    /* PPP LSW L2 table lookup miss counter */
    if (ndrv_ppe_lsw_pf_get_l2_pkt_cnt(pfhandle, &l2_cnt) != 0) {
        hns5_dbg(dev, "ndrv_ppe_lsw_pf_get_l2_pkt_cnt failed\n");
    } else {
        cur.l2_dmac_miss_drop = l2_cnt.dmac_miss;
    }

    /* RPU drop counters (packets on the way to the CPU) */
    if (ndrv_ppe_rpu_vf_get_err_cnt(vfhandle, &rpu_err) != 0) {
        hns5_dbg(dev, "ndrv_ppe_rpu_vf_get_err_cnt failed\n");
    } else {
        cur.rpu_buf_drop_int = rpu_err.buf_drop_int;
        cur.rpu_recv_err_pkt = rpu_err.recv_err_pkt;
        cur.rpu_prep_drop_pkt = rpu_err.prep_drop_pkt;
    }

    /* POE add-fail: not cleared on read by BSP, treated as wrapping 32-bit. */
    if (priv->poe_vfhandle != NULL &&
        ndrv_poe_vf_get_vf_pkt_info(priv->poe_vfhandle, &poe_info) == 0) {
        if (priv->poe_add_fail_inited != 0)
            priv->xstats.drop_stats.poe_add_fail_drop +=
                (uint32_t)(poe_info.add_fail_cnt - priv->poe_add_fail_last);
        priv->poe_add_fail_last = poe_info.add_fail_cnt;
        priv->poe_add_fail_inited = 1;
    }

    /*
     * TM VF tail drop. The BSP counter is a wrapped 64-bit cumulative value
     * and is not cleared on read, so it keeps its own baseline.
     */
    {
        int tm_devid = hidrv_tm_get_vf_devid(TM_DEV_ID, TM_PF_ID, TM_VF_ID);

        if (tm_devid >= 0 &&
            hidrv_tm_vf_get_total_cnt((uint32_t)tm_devid, &tm_cnt, 0) == 0) {
            uint64_t tm_drop = tm_cnt.wred_tail_drop_cnt;

            if (priv->tm_vf_tail_drop_inited != 0 && tm_drop >= priv->tm_vf_tail_drop_last)
                priv->xstats.drop_stats.tm_vf_tail_drop +=
                    tm_drop - priv->tm_vf_tail_drop_last;
            priv->tm_vf_tail_drop_last = tm_drop;
            priv->tm_vf_tail_drop_inited = 1;
        }
    }

    /* First query only consumes the read-clear values. */
    if (first) {
        priv->drop_inited = 1;
        return;
    }

    /* Every field already holds the increment since the previous query. */
    for (i = 0; i < drop_cnt; i++)
        total[i] += curv[i];
}

/* Consume the hw counters at probe to skip pre-existing counts. */
void hns5_eth_xstats_baseline(struct rte_eth_dev *dev)
{
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);

    priv->drop_inited = 0;
    hns5_eth_xstats_drop_update(dev);
    memset(&priv->xstats.drop_stats, 0, sizeof(priv->xstats.drop_stats));

    /*
     * MIB counters are read-clear but MAG is not up yet at probe; the first
     * read in hns5_eth_xstats_mib_update() consumes the init traffic instead.
     */
    memset(&priv->xstats.mib_stats, 0, sizeof(priv->xstats.mib_stats));
}

int hns5_eth_xstats_get(struct rte_eth_dev *dev, struct rte_eth_xstat *xstats, unsigned n)
{
    uint32_t count, cnt_stats;
    uint16_t i;
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);

    cnt_stats = RTE_DIM(hns5_xstats_strings);
    if (xstats == NULL) {
        return cnt_stats;
    }

    if (n < cnt_stats) {
        return cnt_stats;
    }
    hns5_eth_xstats_soft_update(dev, priv);
    hns5_eth_xstats_mib_update(dev);
    hns5_eth_xstats_drop_update(dev);

    count = 0;
    /* Get xstats from ge_mib_stats struct */
    for (i = 0; i < cnt_stats; i++) {
        xstats[count].value = *(uint64_t *)(((char *)&priv->xstats) + hns5_xstats_strings[i].offset);
        xstats[count].id = count;
        count++;
    }

    return count;
}

int hns5_eth_xstats_get_names(__rte_unused struct rte_eth_dev *dev,struct rte_eth_xstat_name *xstats_names,
    unsigned size)
{
    const uint32_t cnt_stats = RTE_DIM(hns5_xstats_strings);

    if (xstats_names == NULL) {
        return cnt_stats;
    }
    if (size < cnt_stats) {
        return cnt_stats;
    }
    for (uint32_t i = 0; i < cnt_stats; i++) {
        snprintf(xstats_names[i].name, RTE_ETH_XSTATS_NAME_SIZE, "%s", hns5_xstats_strings[i].name);
    }

    return cnt_stats;
}

int hns5_eth_stats_get(struct rte_eth_dev *dev, struct rte_eth_stats *stats)
{
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);
    uint32_t nb_rx_queues = dev->data->nb_rx_queues;
    uint32_t nb_tx_queues = dev->data->nb_tx_queues;

    hns5_eth_xstats_soft_update(dev, priv);
    stats->ibytes = priv->xstats.rxq_stats.ibytes;
    stats->obytes = priv->xstats.txq_stats.obytes;
    stats->ipackets = priv->xstats.rxq_stats.ipackets;
    stats->ierrors = priv->xstats.rxq_stats.ierrors_msg + priv->xstats.rxq_stats.ierrors_rxd;
    stats->opackets = priv->xstats.txq_stats.opackets;
    stats->oerrors = priv->xstats.txq_stats.oerrors_msg + priv->xstats.txq_stats.oerrors_txd +
        priv->xstats.txq_stats.oerrors_cache_full;

    if (nb_rx_queues > RTE_ETHDEV_QUEUE_STAT_CNTRS) {
        nb_rx_queues = RTE_ETHDEV_QUEUE_STAT_CNTRS;
    }
    if (nb_tx_queues > RTE_ETHDEV_QUEUE_STAT_CNTRS) {
        nb_tx_queues = RTE_ETHDEV_QUEUE_STAT_CNTRS;
    }
    for (uint32_t i = 0; i < nb_rx_queues; i++) {
        stats->q_ipackets[i] = priv->rxq[i].stats.ipackets;
        stats->q_errors[i] = priv->rxq[i].stats.ierrors_msg + priv->rxq[i].stats.ierrors_rxd;
    }
    for (uint32_t i = 0; i < nb_tx_queues; i++) {
        stats->q_opackets[i] = priv->txq[i].stats.opackets;
    }
    return 0;
}

int hns5_eth_stats_reset(struct rte_eth_dev *dev)
{
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);

    /*
     * Consume the read-clear counters and refresh the normal-counter baseline,
     * then zero the software accumulated totals below.
     */
    priv->drop_inited = 0;
    hns5_eth_xstats_drop_update(dev);
    hns5_eth_xstats_mib_update(dev);

    memset(&priv->xstats, 0, sizeof(priv->xstats));

    for (uint32_t i = 0; i < dev->data->nb_rx_queues; i++) {
        memset(&priv->rxq[i].stats, 0, sizeof(priv->rxq[i].stats));
    }
    for (uint32_t i = 0; i < dev->data->nb_tx_queues; i++) {
        memset(&priv->txq[i].stats, 0, sizeof(priv->txq[i].stats));
    }

    return 0;
}

int hns5_eth_xstats_reset(struct rte_eth_dev *dev)
{
    return hns5_eth_stats_reset(dev);
}
