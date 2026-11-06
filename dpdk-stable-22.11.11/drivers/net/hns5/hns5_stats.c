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
#include "hidrv_mag_api.h"
#include "hidrv_ppe_pf_api.h"
#include "hidrv_ppe_vf_api.h"
#include "ndrv_poe_vf_api.h"

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
#define HNS5_DIAG_STATS_OFFSET(x)      (HNS5_ETH_PORT_OFFSET(diag_stats) + offsetof(struct hns5_eth_diag_stats, x))

#define HNS5_SOFT_STATS_CNT (sizeof(struct hns5_eth_soft_stats) / sizeof(uint64_t))

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
    { "ierrors_cksum", HNS5_RXQ_STATS_OFFSET(ierrors_cksum) },

    { "opackets", HNS5_TXQ_STATS_OFFSET(opackets) },
    { "obytes", HNS5_TXQ_STATS_OFFSET(obytes) },
    { "oerrors_txd", HNS5_TXQ_STATS_OFFSET(oerrors_txd) },
    { "oerrors_msg", HNS5_TXQ_STATS_OFFSET(oerrors_msg) },
    { "oerrors_cache_full", HNS5_TXQ_STATS_OFFSET(oerrors_cache_full) },

    /* POE queue/message diagnostics: used to explain "ppe rxd fail" drops */
    { "diag_drain_start_msgs",    HNS5_DIAG_STATS_OFFSET(drain_start_msgs) },
    { "diag_drain_start_refused", HNS5_DIAG_STATS_OFFSET(drain_start_refused) },
    { "diag_drain_start_rc",      HNS5_DIAG_STATS_OFFSET(drain_start_rc) },
    { "diag_drain_exit_msgs",     HNS5_DIAG_STATS_OFFSET(drain_exit_msgs) },
    { "diag_drain_exit_refused",  HNS5_DIAG_STATS_OFFSET(drain_exit_refused) },
    { "diag_drain_exit_rc",       HNS5_DIAG_STATS_OFFSET(drain_exit_rc) },
    { "diag_poe_msgs_read",       HNS5_DIAG_STATS_OFFSET(poe_msgs_read) },
    { "diag_unmapped_msgs",       HNS5_DIAG_STATS_OFFSET(unmapped_msgs) },
    { "diag_unmapped_first_addr", HNS5_DIAG_STATS_OFFSET(unmapped_first_addr) },
    { "diag_unmapped_last_addr",  HNS5_DIAG_STATS_OFFSET(unmapped_last_addr) },
    { "diag_unmapped_addr0",      HNS5_DIAG_STATS_OFFSET(unmapped_addr0) },
    { "diag_unmapped_addr1",      HNS5_DIAG_STATS_OFFSET(unmapped_addr1) },
    { "diag_unmapped_addr2",      HNS5_DIAG_STATS_OFFSET(unmapped_addr2) },
    { "diag_unmapped_addr3",      HNS5_DIAG_STATS_OFFSET(unmapped_addr3) },
    { "diag_unmapped_w0",         HNS5_DIAG_STATS_OFFSET(unmapped_w0) },
    { "diag_unmapped_w1",         HNS5_DIAG_STATS_OFFSET(unmapped_w1) },
    { "diag_unmapped_w2",         HNS5_DIAG_STATS_OFFSET(unmapped_w2) },
    { "diag_unmapped_w3",         HNS5_DIAG_STATS_OFFSET(unmapped_w3) },
    { "diag_ppe_nobuf_drop",      HNS5_DIAG_STATS_OFFSET(pp_nobuf_drop) },
    { "diag_ppe_lowqos_drop",     HNS5_DIAG_STATS_OFFSET(pp_lowqos_drop) },
    { "diag_mam_vlp_enable_mask", HNS5_DIAG_STATS_OFFSET(mam_vlp_enable_mask) },
    { "diag_mam_dfx_rc",          HNS5_DIAG_STATS_OFFSET(mam_dfx_rc) },

    /* TX side: why packets accepted by the driver did not reach the wire */
    { "diag_tx_accepted_not_sent", HNS5_DIAG_STATS_OFFSET(tx_accepted_not_sent) },
    { "diag_tm_add_ok",           HNS5_DIAG_STATS_OFFSET(tm_add_ok) },
    { "diag_tm_qm2tk",            HNS5_DIAG_STATS_OFFSET(tm_qm2tk) },
    { "diag_tm_acc_add",          HNS5_DIAG_STATS_OFFSET(tm_acc_add) },
    { "diag_tm_acc_ffull_drop",   HNS5_DIAG_STATS_OFFSET(tm_acc_ffull_drop) },
    { "diag_tm_drop_que_in",      HNS5_DIAG_STATS_OFFSET(tm_drop_que_in) },
    { "diag_tm_drop_que_out",     HNS5_DIAG_STATS_OFFSET(tm_drop_que_out) },
    { "diag_tm_drop_loc_in",      HNS5_DIAG_STATS_OFFSET(tm_drop_loc_in) },
    { "diag_tm_drop_loc_out",     HNS5_DIAG_STATS_OFFSET(tm_drop_loc_out) },
    { "diag_tm_drop_port_in",     HNS5_DIAG_STATS_OFFSET(tm_drop_port_in) },
    { "diag_tm_drop_port_out",    HNS5_DIAG_STATS_OFFSET(tm_drop_port_out) },
    { "diag_tm_drop_port_urg",    HNS5_DIAG_STATS_OFFSET(tm_drop_port_urg) },
    { "diag_blk_phys",            HNS5_DIAG_STATS_OFFSET(blk_phys) },
    { "diag_blk_virt",            HNS5_DIAG_STATS_OFFSET(blk_virt) },
    { "diag_blk_size",            HNS5_DIAG_STATS_OFFSET(blk_size) },
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
        priv->xstats.rxq_stats.ierrors_cksum += priv->rxq[i].stats.ierrors_cksum;
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

    ret = hidrv_mag_mac_get_mib(priv->mag_pf_devid, priv->param.mport_id, &mib);
    if (ret != 0) {
        hns5_err(dev, "failed to hidrv_mag_mac_get_mib, ret:%d", ret);
        return;
    }

    (void)hns5_eth_add_mib(&priv->xstats.mib_stats, &mib);
}

int hns5_eth_xstats_get(struct rte_eth_dev *dev, struct rte_eth_xstat *xstats, unsigned n)
{
    uint32_t count, cnt_stats;
    uint16_t i;
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);

    cnt_stats = RTE_DIM(hns5_xstats_strings);
    /* DPDK convention: a NULL buffer asks for the number of entries. The
     * telemetry handler (/ethdev/xstats,<port>) relies on it to size its
     * allocation; returning 0 here made the whole command answer null. */
    if (xstats == NULL) {
        return cnt_stats;
    }
    if (n < cnt_stats) {
        return cnt_stats;
    }
    hns5_eth_xstats_soft_update(dev, priv);
    hns5_eth_xstats_mib_update(dev);
    hns5_ppe_diag_refresh(dev);
    hns5_tm_diag_refresh(dev);
    /* opackets counts what the driver accepted; mac_tx_pkts_total counts what
     * actually left the MAC. The delta is the TX loss that no driver counter
     * can attribute, so expose it directly (saturating at 0). */
    {
        uint64_t accepted = priv->xstats.txq_stats.opackets;
        uint64_t sent = priv->xstats.mib_stats.tx_pkts_total;

        priv->xstats.diag_stats.tx_accepted_not_sent =
            (accepted > sent) ? (accepted - sent) : 0;
    }

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
        return -EINVAL;
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
