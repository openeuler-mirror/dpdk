/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2025-2026 Huawei Technologies Co., Ltd
 */

#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <rte_pci.h>
#include <rte_bus_pci.h>
#include <rte_kvargs.h>
#include <rte_mbuf.h>
#include <rte_malloc.h>
#include <rte_mempool.h>
#include <rte_errno.h>
#include <rte_ether.h>

#include "base/hinic3_compat.h"
#include "base/hinic3_pmd_csr.h"
#include "base/hinic3_pmd_wq.h"
#include "base/hinic3_pmd_eqs.h"
#include "base/hinic3_pmd_cmd.h"
#include "base/hinic3_pmd_cmdq.h"
#include "base/hinic3_pmd_hwdev.h"
#include "base/hinic3_pmd_hwif.h"
#include "base/hinic3_pmd_hw_cfg.h"
#include "base/hinic3_pmd_hw_comm.h"
#include "base/hinic3_pmd_mbox.h"
#include "base/hinic3_pmd_nic_event.h"
#include "base/hinic3_pmd_nic_cfg.h"
#include "mml/hinic3_pmd_mml_lib.h"
#include "stn/hinic3_stn_cmdq.h"
#include "hinic3_pmd_nic_io.h"
#include "hinic3_pmd_tx.h"
#include "hinic3_pmd_rx.h"
#include "hinic3_pmd_ethdev.h"
#include "hinic3_pmd_dcb.h"
#include "hinic3_pmd_tm.h"
#include "hinic3_pmd_hairpin.h"
#include "hinic3_pmd_bifur.h"

#define HINIC3_MIN_RX_BUF_SIZE		1024

#define HINIC3_DEFAULT_BURST_SIZE	32
#define HINIC3_DEFAULT_NB_QUEUES	1
#define HINIC3_DEFAULT_RING_SIZE	1024
#define HINIC3_MAX_LRO_SIZE		65536

#define HINIC3_RX_EMPTY_THRESHOLD 3
#define HINIC3_MAX_TX_FREE_LOOP   1000

#define HINIC3_DEFAULT_RX_FREE_THRESH	32
#define HINIC3_DEFAULT_TX_FREE_THRESH	32

#define HINIC3_RX_WAIT_CYCLE_THRESH	150

#define HINIC3_FEC_CAPA_NUM_PER_SPEED	1

#define RQ_WQE_TYPE_PATH "/sys/module/hinic5/parameters/rq_wqe_type"

/*
 * Vlan_id is a 12 bit number. The VFTA array is actually a 4096 bit array,
 * 128 of 32bit elements. 2^5 = 32. The val of lower 5 bits specifies the bit
 * in the 32bit element. The higher 7 bit val specifies VFTA array index.
 */
#define HINIC3_VFTA_BIT(vlan_id)    (1 << ((vlan_id) & 0x1F))
#define HINIC3_VFTA_IDX(vlan_id)    ((vlan_id) >> 5)

#define HINIC3_LRO_DEFAULT_TIME_LIMIT			16
#define HINIC3_LRO_UNIT_WQE_SIZE			1024 /* Bytes */

#ifdef DPDK_21_11
#define HINIC3_MAX_RX_PKT_LEN(rxmod) ((rxmod).mtu)
#else
#define HINIC3_MAX_RX_PKT_LEN(rxmod) ((rxmod).max_rx_pkt_len)
#endif
/* Driver-specific log messages type */
int hinic3_logtype;
enum hinic3_rx_mod {
	HINIC3_RX_MODE_UC = 1 << 0,
	HINIC3_RX_MODE_MC = 1 << 1,
	HINIC3_RX_MODE_BC = 1 << 2,
	HINIC3_RX_MODE_MC_ALL = 1 << 3,
	HINIC3_RX_MODE_PROMISC = 1 << 4,
};

#define HINIC3_DEFAULT_RX_MODE	(HINIC3_RX_MODE_UC | HINIC3_RX_MODE_MC | \
				HINIC3_RX_MODE_BC)

static const struct rte_pci_id pci_id_hinic3_map[] = {
	{RTE_PCI_DEVICE(PCI_VENDOR_ID_HUAWEI, HINIC3_DEV_ID_SP620)},
	{RTE_PCI_DEVICE(PCI_VENDOR_ID_HUAWEI, HINIC3_DEV_ID_VF_SP620)},
	{RTE_PCI_DEVICE(PCI_VENDOR_ID_HUAWEI, HINIC3_DEV_ID_SP920)},

	{RTE_PCI_DEVICE(PCI_VENDOR_ID_HUAWEI, HINIC3_DEV_ID_SP560)},
	{RTE_PCI_DEVICE(PCI_VENDOR_ID_HUAWEI, HINIC3_DEV_ID_VF_SP560)},
	{RTE_PCI_DEVICE(PCI_VENDOR_ID_HUAWEI, HINIC3_DEV_ID_HYPER_VF_SP560)},

	{RTE_PCI_DEVICE(PCI_VENDOR_ID_BP1, HINIC3_DEV_ID_SP620)},
	{RTE_PCI_DEVICE(PCI_VENDOR_ID_BP1, HINIC3_DEV_ID_VF_SP620)},
	{RTE_PCI_DEVICE(PCI_VENDOR_ID_BP1, HINIC3_DEV_ID_SP920)},

	{RTE_PCI_DEVICE(PCI_VENDOR_ID_BP2, HINIC3_DEV_ID_BP2_620)},
	{RTE_PCI_DEVICE(PCI_VENDOR_ID_BP2, HINIC3_DEV_ID_VF_BP2_620)},

	{RTE_PCI_DEVICE(PCI_VENDOR_ID_BP3, HINIC3_DEV_ID_BP3_620)},
	{RTE_PCI_DEVICE(PCI_VENDOR_ID_BP3, HINIC3_DEV_ID_VF_BP3_620)},

	{.vendor_id = 0},
};

struct hinic3_xstats_name_off {
	char name[RTE_ETH_XSTATS_NAME_SIZE];
	u32  offset;
};

#define HINIC3_CIR_DROP_STAT(_stat_item) { \
	.name = #_stat_item, \
	.offset = offsetof(struct hinic3_cir_drop, _stat_item) \
}

#define HINIC3_FUNC_STAT(_stat_item) {	\
	.name = #_stat_item, \
	.offset = offsetof(struct hinic3_vport_stats, _stat_item) \
}

#define HINIC3_PORT_STAT(_stat_item) { \
	.name = #_stat_item, \
	.offset = offsetof(struct mag_phy_port_stats, _stat_item) \
}

static struct hinic3_xstats_name_off hinic3_cir_drop_stats_strings[] = {
	HINIC3_CIR_DROP_STAT(rx_discard_phy),
};

#define HINIC3_CIR_DROP_XSTATS_NUM (sizeof(hinic3_cir_drop_stats_strings) / \
		sizeof(hinic3_cir_drop_stats_strings[0]))

static const struct hinic3_xstats_name_off hinic3_vport_stats_strings[] = {
	HINIC3_FUNC_STAT(tx_unicast_pkts_vport),
	HINIC3_FUNC_STAT(tx_unicast_bytes_vport),
	HINIC3_FUNC_STAT(tx_multicast_pkts_vport),
	HINIC3_FUNC_STAT(tx_multicast_bytes_vport),
	HINIC3_FUNC_STAT(tx_broadcast_pkts_vport),
	HINIC3_FUNC_STAT(tx_broadcast_bytes_vport),

	HINIC3_FUNC_STAT(rx_unicast_pkts_vport),
	HINIC3_FUNC_STAT(rx_unicast_bytes_vport),
	HINIC3_FUNC_STAT(rx_multicast_pkts_vport),
	HINIC3_FUNC_STAT(rx_multicast_bytes_vport),
	HINIC3_FUNC_STAT(rx_broadcast_pkts_vport),
	HINIC3_FUNC_STAT(rx_broadcast_bytes_vport),

	HINIC3_FUNC_STAT(tx_discard_vport),
	HINIC3_FUNC_STAT(rx_discard_vport),
	HINIC3_FUNC_STAT(tx_err_vport),
	HINIC3_FUNC_STAT(rx_err_vport),
	HINIC3_FUNC_STAT(rx_mtu_err_vport),
	HINIC3_FUNC_STAT(rx_out_of_buffer),
};

#define HINIC3_VPORT_XSTATS_NUM (sizeof(hinic3_vport_stats_strings) / \
		sizeof(hinic3_vport_stats_strings[0]))

static const struct hinic3_xstats_name_off hinic3_phyport_stats_strings[] = {
	HINIC3_PORT_STAT(mac_tx_fragment_pkt_num),
	HINIC3_PORT_STAT(mac_tx_undersize_pkt_num),
	HINIC3_PORT_STAT(mac_tx_undermin_pkt_num),
	HINIC3_PORT_STAT(mac_tx_64_oct_pkt_num),
	HINIC3_PORT_STAT(mac_tx_65_127_oct_pkt_num),
	HINIC3_PORT_STAT(mac_tx_128_255_oct_pkt_num),
	HINIC3_PORT_STAT(mac_tx_256_511_oct_pkt_num),
	HINIC3_PORT_STAT(mac_tx_512_1023_oct_pkt_num),
	HINIC3_PORT_STAT(mac_tx_1024_1518_oct_pkt_num),
	HINIC3_PORT_STAT(mac_tx_1519_2047_oct_pkt_num),
	HINIC3_PORT_STAT(mac_tx_2048_4095_oct_pkt_num),
	HINIC3_PORT_STAT(mac_tx_4096_8191_oct_pkt_num),
	HINIC3_PORT_STAT(mac_tx_8192_9216_oct_pkt_num),
	HINIC3_PORT_STAT(mac_tx_9217_12287_oct_pkt_num),
	HINIC3_PORT_STAT(mac_tx_12288_16383_oct_pkt_num),
	HINIC3_PORT_STAT(mac_tx_1519_max_bad_pkt_num),
	HINIC3_PORT_STAT(mac_tx_1519_max_good_pkt_num),
	HINIC3_PORT_STAT(mac_tx_oversize_pkt_num),
	HINIC3_PORT_STAT(mac_tx_jabber_pkt_num),
	HINIC3_PORT_STAT(mac_tx_bad_pkt_num),
	HINIC3_PORT_STAT(mac_tx_bad_oct_num),
	HINIC3_PORT_STAT(mac_tx_good_pkt_num),
	HINIC3_PORT_STAT(mac_tx_good_oct_num),
	HINIC3_PORT_STAT(mac_tx_total_pkt_num),
	HINIC3_PORT_STAT(mac_tx_total_oct_num),
	HINIC3_PORT_STAT(mac_tx_uni_pkt_num),
	HINIC3_PORT_STAT(mac_tx_multi_pkt_num),
	HINIC3_PORT_STAT(mac_tx_broad_pkt_num),
	HINIC3_PORT_STAT(mac_tx_pause_num),
	HINIC3_PORT_STAT(mac_tx_pfc_pkt_num),
	HINIC3_PORT_STAT(mac_tx_pfc_pri0_pkt_num),
	HINIC3_PORT_STAT(mac_tx_pfc_pri1_pkt_num),
	HINIC3_PORT_STAT(mac_tx_pfc_pri2_pkt_num),
	HINIC3_PORT_STAT(mac_tx_pfc_pri3_pkt_num),
	HINIC3_PORT_STAT(mac_tx_pfc_pri4_pkt_num),
	HINIC3_PORT_STAT(mac_tx_pfc_pri5_pkt_num),
	HINIC3_PORT_STAT(mac_tx_pfc_pri6_pkt_num),
	HINIC3_PORT_STAT(mac_tx_pfc_pri7_pkt_num),
	HINIC3_PORT_STAT(mac_tx_control_pkt_num),
	HINIC3_PORT_STAT(mac_tx_err_all_pkt_num),
	HINIC3_PORT_STAT(mac_tx_from_app_good_pkt_num),
	HINIC3_PORT_STAT(mac_tx_from_app_bad_pkt_num),

	HINIC3_PORT_STAT(mac_rx_fragment_pkt_num),
	HINIC3_PORT_STAT(mac_rx_undersize_pkt_num),
	HINIC3_PORT_STAT(mac_rx_undermin_pkt_num),
	HINIC3_PORT_STAT(mac_rx_64_oct_pkt_num),
	HINIC3_PORT_STAT(mac_rx_65_127_oct_pkt_num),
	HINIC3_PORT_STAT(mac_rx_128_255_oct_pkt_num),
	HINIC3_PORT_STAT(mac_rx_256_511_oct_pkt_num),
	HINIC3_PORT_STAT(mac_rx_512_1023_oct_pkt_num),
	HINIC3_PORT_STAT(mac_rx_1024_1518_oct_pkt_num),
	HINIC3_PORT_STAT(mac_rx_1519_2047_oct_pkt_num),
	HINIC3_PORT_STAT(mac_rx_2048_4095_oct_pkt_num),
	HINIC3_PORT_STAT(mac_rx_4096_8191_oct_pkt_num),
	HINIC3_PORT_STAT(mac_rx_8192_9216_oct_pkt_num),
	HINIC3_PORT_STAT(mac_rx_9217_12287_oct_pkt_num),
	HINIC3_PORT_STAT(mac_rx_12288_16383_oct_pkt_num),
	HINIC3_PORT_STAT(mac_rx_1519_max_bad_pkt_num),
	HINIC3_PORT_STAT(mac_rx_1519_max_good_pkt_num),
	HINIC3_PORT_STAT(mac_rx_oversize_pkt_num),
	HINIC3_PORT_STAT(mac_rx_jabber_pkt_num),
	HINIC3_PORT_STAT(mac_rx_bad_pkt_num),
	HINIC3_PORT_STAT(mac_rx_bad_oct_num),
	HINIC3_PORT_STAT(mac_rx_good_pkt_num),
	HINIC3_PORT_STAT(mac_rx_good_oct_num),
	HINIC3_PORT_STAT(mac_rx_total_pkt_num),
	HINIC3_PORT_STAT(mac_rx_total_oct_num),
	HINIC3_PORT_STAT(mac_rx_uni_pkt_num),
	HINIC3_PORT_STAT(mac_rx_multi_pkt_num),
	HINIC3_PORT_STAT(mac_rx_broad_pkt_num),
	HINIC3_PORT_STAT(mac_rx_pause_num),
	HINIC3_PORT_STAT(mac_rx_pfc_pkt_num),
	HINIC3_PORT_STAT(mac_rx_pfc_pri0_pkt_num),
	HINIC3_PORT_STAT(mac_rx_pfc_pri1_pkt_num),
	HINIC3_PORT_STAT(mac_rx_pfc_pri2_pkt_num),
	HINIC3_PORT_STAT(mac_rx_pfc_pri3_pkt_num),
	HINIC3_PORT_STAT(mac_rx_pfc_pri4_pkt_num),
	HINIC3_PORT_STAT(mac_rx_pfc_pri5_pkt_num),
	HINIC3_PORT_STAT(mac_rx_pfc_pri6_pkt_num),
	HINIC3_PORT_STAT(mac_rx_pfc_pri7_pkt_num),
	HINIC3_PORT_STAT(mac_rx_control_pkt_num),
	HINIC3_PORT_STAT(mac_rx_sym_err_pkt_num),
	HINIC3_PORT_STAT(rx_crc_errors),
	HINIC3_PORT_STAT(mac_rx_send_app_good_pkt_num),
	HINIC3_PORT_STAT(mac_rx_send_app_bad_pkt_num),
	HINIC3_PORT_STAT(mac_rx_unfilter_pkt_num)
};

#define HINIC3_PHYPORT_XSTATS_NUM (sizeof(hinic3_phyport_stats_strings) / \
		sizeof(hinic3_phyport_stats_strings[0]))

static const struct hinic3_xstats_name_off hinic3_rxq_stats_strings[] = {
	{"rx_nombuf", offsetof(struct hinic3_rxq_stats, rx_nombuf)},
	{"burst_pkt", offsetof(struct hinic3_rxq_stats, burst_pkts)},
	{"errors", offsetof(struct hinic3_rxq_stats, errors)},
	{"csum_errors", offsetof(struct hinic3_rxq_stats, csum_errors)},
	{"other_errors", offsetof(struct hinic3_rxq_stats, other_errors)},
	{"empty", offsetof(struct hinic3_rxq_stats, empty)},

#ifdef HINIC3_XSTAT_RXBUF_INFO
	{"rxmbuf", offsetof(struct hinic3_rxq_stats, rx_mbuf)},
	{"avail", offsetof(struct hinic3_rxq_stats, rx_avail)},
	{"hole", offsetof(struct hinic3_rxq_stats, rx_hole)},
#endif

#ifdef HINIC3_XSTAT_PROF_RX
	{"app_tsc", offsetof(struct hinic3_rxq_stats, app_tsc)},
	{"pmd_tsc", offsetof(struct hinic3_rxq_stats, pmd_tsc)},
#endif

#ifdef HINIC3_XSTAT_MBUF_USE
	{"rx_alloc_mbuf", offsetof(struct hinic3_rxq_stats, alloc_mbuf)},
	{"rx_free_mbuf", offsetof(struct hinic3_rxq_stats, free_mbuf)},
	{"rx_left_mbuf", offsetof(struct hinic3_rxq_stats, left_mbuf)},
#endif
};

#define HINIC3_RXQ_XSTATS_NUM (sizeof(hinic3_rxq_stats_strings) / \
		sizeof(hinic3_rxq_stats_strings[0]))

static const struct hinic3_xstats_name_off hinic3_txq_stats_strings[] = {
	{"tx_busy", offsetof(struct hinic3_txq_stats, tx_busy)},
	{"offload_errors", offsetof(struct hinic3_txq_stats, off_errs)},
	{"burst_pkts", offsetof(struct hinic3_txq_stats, burst_pkts)},
	{"sge_len0", offsetof(struct hinic3_txq_stats, sge_len0)},
	{"mbuf_null", offsetof(struct hinic3_txq_stats, mbuf_null)},

#ifdef HINIC3_XSTAT_PROF_TX
	{"app_tsc", offsetof(struct hinic3_txq_stats, app_tsc)},
	{"pmd_tsc", offsetof(struct hinic3_txq_stats, pmd_tsc)},
#endif

#ifdef HINIC3_XSTAT_MBUF_USE
	{"tx_left_mbuf_bytes", offsetof(struct hinic3_txq_stats, left_mbuf)},
#endif

};

#define HINIC3_TXQ_XSTATS_NUM (sizeof(hinic3_txq_stats_strings) / \
		sizeof(hinic3_txq_stats_strings[0]))

static int hinic3_xstats_calc_num(struct hinic3_nic_dev *nic_dev)
{
	if (HINIC3_IS_VF(nic_dev->hwdev)) {
		return (HINIC3_VPORT_XSTATS_NUM +
			HINIC3_CIR_DROP_XSTATS_NUM +
			HINIC3_RXQ_XSTATS_NUM * nic_dev->num_rqs +
			HINIC3_TXQ_XSTATS_NUM * nic_dev->num_sqs);
	} else {
		return (HINIC3_VPORT_XSTATS_NUM +
			HINIC3_CIR_DROP_XSTATS_NUM +
			HINIC3_PHYPORT_XSTATS_NUM +
			HINIC3_RXQ_XSTATS_NUM * nic_dev->num_rqs +
			HINIC3_TXQ_XSTATS_NUM * nic_dev->num_sqs);
	}
}

#define HINIC3_TXD_ALIGN		1
#define HINIC3_RXD_ALIGN		1

static const struct rte_eth_desc_lim hinic3_rx_desc_lim = {
	.nb_max = HINIC3_MAX_QUEUE_DEPTH,
	.nb_min = HINIC3_MIN_QUEUE_DEPTH,
	.nb_align = HINIC3_RXD_ALIGN,
};

static const struct rte_eth_desc_lim hinic3_tx_desc_lim = {
	.nb_max = HINIC3_MAX_QUEUE_DEPTH,
	.nb_min = HINIC3_MIN_QUEUE_DEPTH,
	.nb_align = HINIC3_TXD_ALIGN,
};

static void hinic3_deinit_mac_addr(struct rte_eth_dev *eth_dev);

static int hinic3_copy_mempool_init(struct hinic3_nic_dev *nic_dev);

static void hinic3_copy_mempool_uninit(struct hinic3_nic_dev *nic_dev);

bool
is_sp620_nic(struct hinic3_nic_dev *nic_dev)
{
	struct rte_pci_device *pci_dev = NULL;
	struct hinic3_hwdev *hwdev = nic_dev->hwdev;
	struct rte_eth_dev *eth_dev = &rte_eth_devices[hwdev->port_id];

	pci_dev = RTE_ETH_DEV_TO_PCI(eth_dev);

	switch (pci_dev->id.device_id) {
	case HINIC3_DEV_ID_SP620:
	case HINIC3_DEV_ID_VF_SP620:
	case HINIC3_DEV_ID_BP2_620:
	case HINIC3_DEV_ID_VF_BP2_620:
	case HINIC3_DEV_ID_BP3_620:
	case HINIC3_DEV_ID_VF_BP3_620:
		return true;
	default:
		return false;
	}
}

bool
is_sp560_nic(struct hinic3_nic_dev *nic_dev)
{
	struct rte_pci_device *pci_dev = NULL;
	struct hinic3_hwdev *hwdev = nic_dev->hwdev;
	struct rte_eth_dev *eth_dev = &rte_eth_devices[hwdev->port_id];

	pci_dev = RTE_ETH_DEV_TO_PCI(eth_dev);

	switch (pci_dev->id.device_id) {
	case HINIC3_DEV_ID_SP560:
	case HINIC3_DEV_ID_VF_SP560:
	case HINIC3_DEV_ID_HYPER_VF_SP560:
		return true;
	default:
		return false;
	}
}

/**
 * Interrupt handler triggered by NIC for handling specific event
 *
 * @param[in] param
 *   The address of parameter (struct rte_eth_dev *) regsitered before
 */

static void hinic3_dev_interrupt_handler_qpool(void *param)
{
	struct rte_eth_dev *dev = param;
 	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
 	struct rte_intr_handle *intr_handle = dev->intr_handle;
 	struct netdev_event event;
 	struct rte_eth_link link;
 	ssize_t bytes_read;
 	u8 link_state = 0;

 	if (!hinic3_get_bit(HINIC3_DEV_INTR_EN, &nic_dev->dev_status)) {
 		PMD_DRV_LOG(WARNING,
 			    "Intr is disabled, ignore intr event, dev_name: %s, port_id: %d",
 			    nic_dev->dev_name, dev->data->port_id);
 		return;
 	}

	while ((bytes_read = read(intr_handle->fd, &event, sizeof(event))) == sizeof(event)) {
		if (event.type == NETDEV_UP) {
			link_state = 1;
			get_port_info(nic_dev->hwdev, link_state, &link);
			rte_eth_linkstatus_set(dev, &link);
		} else if (event.type == NETDEV_DOWN) {
			link_state = 0;
			get_port_info(nic_dev->hwdev, link_state, &link);
			rte_eth_linkstatus_set(dev, &link);
		} else if (event.type == NETDEV_CHANGEADDR) {
			u8 addr_bytes[RTE_ETHER_ADDR_LEN];
			memmove(addr_bytes, event.data, RTE_ETHER_ADDR_LEN);
			rte_ether_addr_copy((struct rte_ether_addr *)addr_bytes,
				&dev->data->mac_addrs[0]);
			if (rte_is_zero_ether_addr(&dev->data->mac_addrs[0]))
				PMD_DRV_LOG(INFO, "mac addr is zero");
		} else if (event.type == NETDEV_CHANGEMTU) {
			PMD_DRV_LOG(INFO, "Set new mtu address");
			nic_dev->mtu_size = event.data_mtu;
			dev->data->mtu = event.data_mtu;
		} else {
			PMD_DRV_LOG(INFO, "event type not support");
		}
	}

	if (bytes_read < 0 && errno != EAGAIN) {
		PMD_DRV_LOG(ERR, "interrupt handler fd read error: %d.", errno);
	}
}

static void hinic3_dev_interrupt_handler(void *param)
{
	struct rte_eth_dev *dev = param;
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);

	if (!hinic3_get_bit(HINIC3_DEV_INTR_EN, &nic_dev->dev_status)) {
		PMD_DRV_LOG(WARNING,
			    "Intr is disabled, ignore intr event, dev_name: %s, port_id: %d",
			    nic_dev->dev_name, dev->data->port_id);
		return;
	}

	/* Aeq0 msg handler */
	hinic3_dev_handle_aeq_event(nic_dev->hwdev, param);
}

/**
 * Ethernet device configuration.
 *
 * Prepare the driver for a given number of TX and RX queues, mtu size
 * and configure RSS.
 *
 * @param[in] dev
 *   Pointer to ethernet device structure.
 *
 * @retval zero : Success
 * @retval non-zero : Failure.
 */
static int hinic3_dev_configure(struct rte_eth_dev *dev)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);

	nic_dev->num_sqs = dev->data->nb_tx_queues;
	nic_dev->num_rqs = dev->data->nb_rx_queues;

	if (nic_dev->num_sqs > nic_dev->max_sqs ||
		nic_dev->num_rqs > nic_dev->max_rqs) {
		PMD_DRV_LOG(ERR, "num_sqs: %d or num_rqs: %d larger than max_sqs: %d or max_rqs: %d",
			    nic_dev->num_sqs, nic_dev->num_rqs,
			    nic_dev->max_sqs, nic_dev->max_rqs);
		return -EINVAL;
	}

	/* The range of mtu is 384~9600 */

	if (HINIC3_MAX_RX_PKT_LEN(dev->data->dev_conf.rxmode) < HINIC3_MIN_FRAME_SIZE ||
	    HINIC3_MAX_RX_PKT_LEN(dev->data->dev_conf.rxmode) >
	    HINIC3_MAX_JUMBO_FRAME_SIZE) {
		PMD_DRV_LOG(ERR, "Max rx pkt len out of range, max_rx_pkt_len: %d, "
			    "expect between %d and %d",
			    HINIC3_MAX_RX_PKT_LEN(dev->data->dev_conf.rxmode),
			    HINIC3_MIN_FRAME_SIZE, HINIC3_MAX_JUMBO_FRAME_SIZE);
		return -EINVAL;
	}

	if (!IS_QPOOL_MODE())
		nic_dev->mtu_size = (u16)HINIC3_PKTLEN_TO_MTU(HINIC3_MAX_RX_PKT_LEN(dev->data->dev_conf.rxmode));

	if (dev->data->dev_conf.rxmode.mq_mode & ETH_MQ_RX_RSS_FLAG)
		dev->data->dev_conf.rxmode.offloads |= DEV_RX_OFFLOAD_RSS_HASH;

	if (!nic_dev->hinic3_offload_initialized) {
		dev->data->dev_conf.rxmode.offloads |= DEV_RX_OFFLOAD_SCATTER;
		dev->data->dev_conf.txmode.offloads |= DEV_TX_OFFLOAD_MULTI_SEGS;
		nic_dev->hinic3_offload_initialized = true;
	}

	/* Clear fdir filter */
	hinic3_free_fdir_filter(dev);

	if (dev->data->dev_conf.txmode.mq_mode == ETH_MQ_TX_DCB) {
		int err;
		u8 cos_num = hinic3_get_dev_user_cos_num(nic_dev);
		err = hinic3_setup_cos(nic_dev, cos_num);
		if (err) {
			PMD_DRV_LOG(ERR, "hinic3 setup failed, errno %d", err);
			return err;
		}

		err = hinic3_tm_conf_update(dev);
		if (err) {
			PMD_DRV_LOG(ERR, "failed to update tm conf, errno %d",
				    err);
			return err;
		}
	}

	return 0;
}

void hinic3_dev_info_get(struct rte_eth_dev_info *info, struct hinic3_nic_dev *nic_dev)
{
	if (nic_dev->dcb->dcb_on) {
		info->max_rx_queues = nic_dev->num_rqs;
		info->max_tx_queues = nic_dev->num_sqs;
	} else {
		info->max_rx_queues = nic_dev->max_rqs;
		info->max_tx_queues = nic_dev->max_sqs;
	}

	info->min_rx_bufsize = HINIC3_MIN_RX_BUF_SIZE;
	info->max_rx_pktlen  = HINIC3_MAX_JUMBO_FRAME_SIZE;
	info->max_mac_addrs  = HINIC3_MAX_UC_MAC_ADDRS;
	info->min_mtu = HINIC3_MIN_MTU_SIZE;
	info->max_mtu = HINIC3_MAX_MTU_SIZE;
	info->max_lro_pkt_size = HINIC3_MAX_LRO_SIZE;

	info->rx_queue_offload_capa = 0;
	info->rx_offload_capa = DEV_RX_OFFLOAD_VLAN_STRIP |
				DEV_RX_OFFLOAD_IPV4_CKSUM |
				DEV_RX_OFFLOAD_UDP_CKSUM |
				DEV_RX_OFFLOAD_TCP_CKSUM |
				DEV_RX_OFFLOAD_SCTP_CKSUM |
				DEV_RX_OFFLOAD_VLAN_FILTER |
				DEV_RX_OFFLOAD_SCATTER |
#ifndef DPDK_21_11
				DEV_RX_OFFLOAD_JUMBO_FRAME |
#endif
				DEV_RX_OFFLOAD_TCP_LRO |
				DEV_RX_OFFLOAD_RSS_HASH |
				DEV_RX_OFFLOAD_QINQ_STRIP;

	info->tx_queue_offload_capa = 0;
	info->tx_offload_capa = DEV_TX_OFFLOAD_VLAN_INSERT |
				DEV_TX_OFFLOAD_IPV4_CKSUM |
				DEV_TX_OFFLOAD_UDP_CKSUM |
				DEV_TX_OFFLOAD_TCP_CKSUM |
				DEV_TX_OFFLOAD_SCTP_CKSUM |
				DEV_TX_OFFLOAD_OUTER_IPV4_CKSUM |
				DEV_TX_OFFLOAD_OUTER_UDP_CKSUM |
				DEV_TX_OFFLOAD_VXLAN_TNL_TSO |
				DEV_TX_OFFLOAD_TCP_TSO |
				DEV_TX_OFFLOAD_MULTI_SEGS |
				DEV_TX_OFFLOAD_QINQ_INSERT;

	info->hash_key_size = HINIC3_RSS_KEY_SIZE;
	info->reta_size = HINIC3_RSS_INDIR_SIZE;
	info->flow_type_rss_offloads = HINIC3_RSS_OFFLOAD_ALL;

	info->rx_desc_lim = hinic3_rx_desc_lim;
	info->tx_desc_lim = hinic3_tx_desc_lim;

	/* Driver-preferred rx/tx parameters */
	info->default_rxportconf.burst_size = HINIC3_DEFAULT_BURST_SIZE;
	info->default_txportconf.burst_size = HINIC3_DEFAULT_BURST_SIZE;
	info->default_rxportconf.nb_queues = HINIC3_DEFAULT_NB_QUEUES;
	info->default_txportconf.nb_queues = HINIC3_DEFAULT_NB_QUEUES;
	info->default_rxportconf.ring_size = HINIC3_DEFAULT_RING_SIZE;
	info->default_txportconf.ring_size = HINIC3_DEFAULT_RING_SIZE;
}

static int hinic3_get_link_state_qpool(struct hinic3_nic_dev *nic_dev)
{
	struct drv_cmd_kernel_nic_data cfg_kernel_data;
 	struct msg_module msg_to_kernel;
 	int in_size, out_size, err;

 	(void)memset(&msg_to_kernel, 0, sizeof(msg_to_kernel));
 	in_size = sizeof(cfg_kernel_data);
 	out_size = sizeof(cfg_kernel_data);
 	fill_ioctl_msg(&msg_to_kernel, SEND_TO_NIC_DRIVER, GET_KERN_DEV_DATA,
 			in_size, out_size,
 			&cfg_kernel_data, &cfg_kernel_data);

 	err = ioctl(nic_dev->fd, 0, &msg_to_kernel);
 	if (err < 0)
 		PMD_DRV_LOG(ERR, "Get kernel netdev state failed, err: %d.", err);

 	if (cfg_kernel_data.netdev_state == 0)
 		err = -EIO;

 	return err;
}

static int hinic3_get_kernel_mtu(struct rte_eth_dev *eth_dev)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(eth_dev);
 	struct drv_cmd_kernel_nic_data cfg_kernel_data  = { 0 };
 	struct msg_module msg_to_kernel = { 0 };
 	int err = 0;

 	fill_ioctl_msg(&msg_to_kernel, SEND_TO_NIC_DRIVER, GET_KERN_DEV_DATA,
 		       sizeof(cfg_kernel_data), sizeof(cfg_kernel_data),
 		       &cfg_kernel_data, &cfg_kernel_data);

 	err = ioctl(nic_dev->fd, 0, &msg_to_kernel);
 	if (err < 0) {
 		PMD_DRV_LOG(WARNING, "Get kernel mtu failed");
 		return err;
 	}

 	eth_dev->data->mtu = cfg_kernel_data.mtu;

 	return err;
}

static int hinic3_verify_queue_depth(struct hinic3_nic_dev *nic_dev, u16 *q_depth, u16 type)
{
	struct drv_cmd_kernel_nic_data cfg_kernel_data  = { 0 };
 	struct msg_module msg_to_kernel = { 0 };
 	int err = 0;

 	fill_ioctl_msg(&msg_to_kernel, SEND_TO_NIC_DRIVER, GET_KERN_DEV_DATA,
 		       sizeof(cfg_kernel_data), sizeof(cfg_kernel_data),
 		       &cfg_kernel_data, &cfg_kernel_data);

 	err = ioctl(nic_dev->fd, 0, &msg_to_kernel);
 	if (err < 0)
 		return err;

 	if (type == HINIC3_VERIFY_RX_DEPTH && *q_depth != cfg_kernel_data.rx_q_depth) {
 		*q_depth = cfg_kernel_data.rx_q_depth;
 		PMD_DRV_LOG(WARNING, "[WARNING] Rxq depth adjusted to %d to match kernel", *q_depth);
 	}

 	if (type == HINIC3_VERIFY_TX_DEPTH && *q_depth != cfg_kernel_data.tx_q_depth) {
 		*q_depth = cfg_kernel_data.tx_q_depth;
 		PMD_DRV_LOG(WARNING, "[WARNING] Txq depth adjusted to %d to match kernel", *q_depth);
 	}

 	return err;
}

static int hinic3_get_kernel_addr(struct rte_eth_dev *eth_dev)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(eth_dev);
 	struct drv_cmd_kernel_nic_data cfg_kernel_data  = { 0 };
 	struct msg_module msg_to_kernel = { 0 };
 	int err = 0;

 	fill_ioctl_msg(&msg_to_kernel, SEND_TO_NIC_DRIVER, GET_KERN_DEV_DATA,
 		       sizeof(cfg_kernel_data), sizeof(cfg_kernel_data),
 		       &cfg_kernel_data, &cfg_kernel_data);

 	err = ioctl(nic_dev->fd, 0, &msg_to_kernel);
 	if (err < 0) {
 		PMD_DRV_LOG(WARNING, "Get kernel mtu failed");
 		return err;
 	}

	rte_ether_addr_copy((struct rte_ether_addr *)cfg_kernel_data.dev_addr,
 				&eth_dev->data->mac_addrs[0]);

 	return err;
}

/**
 * Get information about the device.
 *
 * @param[in] dev
 *   Pointer to ethernet device structure.
 * @param[out] info
 *   Info structure for ethernet device.
 *
 * @retval zero : Success
 * @retval non-zero : Failure.
 */
static int hinic3_dev_infos_get(struct rte_eth_dev *dev,
				struct rte_eth_dev_info *info)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	hinic3_dev_info_get(info, nic_dev);

	return 0;
}

static int hinic3_fw_version_get(struct rte_eth_dev *dev, char *fw_version,
				 size_t fw_size)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	char mgmt_ver[MGMT_VERSION_MAX_LEN] = { 0 };
	int err;

	err = hinic3_get_mgmt_version(nic_dev->hwdev, mgmt_ver,
			HINIC3_MGMT_VERSION_MAX_LEN);
	if (err) {
		PMD_DRV_LOG(ERR, "Get fw version failed");
		return -EIO;
	}

	if (fw_size < strlen((char *)mgmt_ver) + 1)
		return (strlen((char *)mgmt_ver) + 1);

	(void)snprintf(fw_version, fw_size, "%s", mgmt_ver);

	return 0;
}

/**
 * Set ethernet device link state up.
 *
 * @param[in] dev
 *   Pointer to ethernet device structure.
 *
 * @retval zero : Success
 * @retval non-zero : Failure.
 */
static int hinic3_dev_set_link_up(struct rte_eth_dev *dev)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct rte_eth_link link = {0};
	int err;

	if (IS_QPOOL_MODE()) {
 		PMD_DRV_LOG(WARNING, "Qpool mode not support set link up.");
 		return -EAGAIN;
 	}

	/* Vport enable will set function valid in mpu.
	   So dev start status need to be checked before vport enable.*/
	if (hinic3_get_bit(HINIC3_DEV_START, &nic_dev->dev_status)) {
		err = hinic3_set_vport_enable(nic_dev->hwdev, true);
		if (err) {
			PMD_DRV_LOG(ERR, "Enable vport failed, dev_name: %s", nic_dev->dev_name);
			return err;
		}
	}

	/* Link status follow phy port status, mpu will open pma */
	err = hinic3_set_port_enable(nic_dev->hwdev, true);
	if (err) {
		PMD_DRV_LOG(ERR, "Set MAC link up failed, dev_name: %s, port_id: %d",
			    nic_dev->dev_name, dev->data->port_id);
		return err;
	}

	if(HINIC3_IS_VF(nic_dev->hwdev)) {
		link = dev->data->dev_link;
		link.link_status = nic_dev->hwdev->link_status & nic_dev->hwdev->vf_valid_status;
		if (link.link_status == ETH_LINK_DOWN) {
			PMD_DRV_LOG(ERR,
				"Set VF link up failed, dev_name: %s, port_id: %d, link_status: %d, vf_valid_status: %d",
				nic_dev->dev_name, dev->data->port_id,
				nic_dev->hwdev->link_status,
				nic_dev->hwdev->vf_valid_status);
			return -EAGAIN;
		}

		(void)rte_eth_linkstatus_set(dev, &link);
	}

	return 0;
}

/**
 * Set ethernet device link state down.
 *
 * @param[in] dev
 *   Pointer to ethernet device structure.
 *
 * @retval zero : Success
 * @retval non-zero : Failure.
 */
static int hinic3_dev_set_link_down(struct rte_eth_dev *dev)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct rte_eth_link link = {0};
	int err;

	if (IS_QPOOL_MODE()) {
 	 	PMD_DRV_LOG(WARNING, "Qpool mode not support set link down.");
 	 	return -EAGAIN;
 	}

	err = hinic3_set_vport_enable(nic_dev->hwdev, false);
	if (err) {
		PMD_DRV_LOG(ERR, "Disable vport failed, dev_name: %s", nic_dev->dev_name);
		return err;
	}

	/* Link status follow phy port status, mpu will close pma */
	err = hinic3_set_port_enable(nic_dev->hwdev, false);
	if (err) {
		PMD_DRV_LOG(ERR, "Set MAC link down failed, dev_name: %s, port_id: %d",
			    nic_dev->dev_name, dev->data->port_id);
		return err;
	}

	if(HINIC3_IS_VF(nic_dev->hwdev)) {
		link = dev->data->dev_link;
		link.link_status = nic_dev->hwdev->link_status & nic_dev->hwdev->vf_valid_status;

		(void)rte_eth_linkstatus_set(dev, &link);
	}

	return 0;
}

/**
 * Get device physical link information.
 *
 * @param[in] dev
 *   Pointer to ethernet device structure.
 * @param[in] wait_to_complete
 *   Wait for request completion.
 *
 * @retval 0 : Link status changed
 * @retval -1 : Link status not changed.
 */
static int hinic3_link_update(struct rte_eth_dev *dev, int wait_to_complete)
{
#define CHECK_INTERVAL 10  /* 10ms */
#define MAX_REPEAT_TIME 100  /* 1s (100 * 10ms) in total */
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct rte_eth_link link;
	u8 link_state;
	unsigned int rep_cnt = MAX_REPEAT_TIME;
	int ret;

	memset(&link, 0, sizeof(link));
	do {
		/* Get link status information from hardware */
		ret = hinic3_get_link_state(nic_dev->hwdev, &link_state);
		if (ret) {
			link.link_status = ETH_LINK_DOWN;
			link.link_speed = ETH_SPEED_NUM_NONE;
			link.link_duplex = ETH_LINK_HALF_DUPLEX;
			link.link_autoneg = ETH_LINK_FIXED;
			goto out;
		}

		get_port_info(nic_dev->hwdev, link_state, &link);

		if (!wait_to_complete || link.link_status)
			break;

		rte_delay_ms(CHECK_INTERVAL);
	} while (rep_cnt--);

out:
	if(HINIC3_IS_VF(nic_dev->hwdev) && !IS_QPOOL_MODE()) {
		nic_dev->hwdev->link_status = link.link_status;
		link.link_status = nic_dev->hwdev->link_status & nic_dev->hwdev->vf_valid_status;
	}

	return rte_eth_linkstatus_set(dev, &link);
}

static void hinic3_reset_rx_queue(struct rte_eth_dev *dev)
{
	struct hinic3_rxq *rxq = NULL;
	struct hinic3_nic_dev *nic_dev;
	int q_id = 0;

	nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);

	for (q_id = 0; q_id < nic_dev->num_rqs; q_id++) {
		rxq = nic_dev->rxqs[q_id];
		if (rxq == NULL)
			continue;
		rxq->cons_idx = 0;
		rxq->prod_idx = 0;
		rxq->delta = rxq->q_depth;
		rxq->next_to_update = 0;
	}
}

static void hinic3_reset_tx_queue(struct rte_eth_dev *dev)
{
	struct hinic3_nic_dev *nic_dev;
	struct hinic3_txq *txq = NULL;
	int q_id = 0;

	nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);

	for (q_id = 0; q_id < nic_dev->num_sqs; q_id++) {
		txq = nic_dev->txqs[q_id];
		if (txq == NULL)
			continue;
		txq->cons_idx = 0;
		txq->prod_idx = 0;
		txq->owner = 1;

		if (!txq->is_hairpin)
			/* Clear hardware ci */
			*(txq->ci_vaddr_base) = 0;
	}
}
static int hinic3_alloc_template(struct hinic3_nic_dev *nic_dev)
{
	struct drv_cmd_cfg_rss_temp cfg_rss_temp;
	struct msg_module msg_to_kernel;
	int in_size, out_size, err;

	(void)memset(&cfg_rss_temp, 0, sizeof(cfg_rss_temp));
	cfg_rss_temp.opcode = NIC_RSS_CMD_TEMP_ALLOC;

	(void)memset(&msg_to_kernel, 0, sizeof(msg_to_kernel));
	in_size = sizeof(cfg_rss_temp);
	out_size = sizeof(cfg_rss_temp);
	fill_ioctl_msg(&msg_to_kernel, SEND_TO_NIC_DRIVER, CFG_RSS_TEMPLATE,
			in_size, out_size,
			&cfg_rss_temp, &cfg_rss_temp);
	err = ioctl(nic_dev->fd, 0, &msg_to_kernel);
	if (err < 0)
		PMD_DRV_LOG(ERR, "Alloc template error: %d", err);

	nic_dev->hwdev->qpool_qgrp_id = cfg_rss_temp.q_grp_id;

	return err;
}

static int hinic3_release_template(struct hinic3_nic_dev *nic_dev)
{
	int err;
	struct msg_module msg_to_kernel;
	struct drv_cmd_cfg_rss_temp cfg_rss_temp;

	(void)memset(&cfg_rss_temp, 0, sizeof(cfg_rss_temp));
	cfg_rss_temp.opcode = NIC_RSS_CMD_TEMP_QPOOL_FREE;

	(void)memset(&msg_to_kernel, 0, sizeof(msg_to_kernel));
	fill_ioctl_msg(&msg_to_kernel, SEND_TO_NIC_DRIVER, CFG_RSS_TEMPLATE,
			sizeof(cfg_rss_temp), sizeof(cfg_rss_temp),
			&cfg_rss_temp, &cfg_rss_temp);
	err = ioctl(nic_dev->fd, 0, &msg_to_kernel);
	if (err < 0)
		PMD_DRV_LOG(ERR, "Release template error: %d.", errno);

	nic_dev->hwdev->qpool_qgrp_id = 0;

	return err;
}

static int hinic3_get_rx_user_queue(struct hinic3_nic_dev *nic_dev, struct hinic3_rxq *rxq)
{
	struct drv_cmd_user_queue_get queueinfo;
	struct msg_module msg_to_kernel;
	int in_size, out_size, err;

	queueinfo.qid = rxq->q_id;

	(void)memset(&msg_to_kernel, 0, sizeof(msg_to_kernel));
	in_size = sizeof(queueinfo);
	out_size = sizeof(queueinfo);

	fill_ioctl_msg(&msg_to_kernel, SEND_TO_NIC_DRIVER, GET_USER_QUEUE_ID, in_size, out_size,
		&queueinfo, &queueinfo);
	err = ioctl(nic_dev->fd, 0, &msg_to_kernel);
	if (err < 0)
		PMD_DRV_LOG(ERR, "Get rx user queue err: %d", err);

	rxq->local_qid = queueinfo.local_qid;
	return err;
}

static int hinic3_release_user_queue(struct hinic3_nic_dev *nic_dev, int queue_id)
{
	int err;
	struct msg_module msg_to_kernel;
	struct drv_cmd_user_queue_get queueinfo;

	queueinfo.qid = queue_id;
	(void)memset(&msg_to_kernel, 0, sizeof(msg_to_kernel));
	fill_ioctl_msg(&msg_to_kernel, SEND_TO_NIC_DRIVER, DEL_USER_QUEUE_ID,
			sizeof(queueinfo), sizeof(queueinfo),
			&queueinfo, &queueinfo);
	err = ioctl(nic_dev->fd, 0, &msg_to_kernel);
	if (err < 0)
		PMD_DRV_LOG(ERR, "Release user queue error: %d.", err);

	return err;
}

static int
hinic3_rx_queue_dma_create(struct rte_eth_dev *dev, struct hinic3_rxq *rxq,
			   uint16_t qid, unsigned int socket_id)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct hinic3_hwdev *hwdev = nic_dev->hwdev;
	const struct rte_memzone *rq_mz = NULL;
	const struct rte_memzone *cqe_mz = NULL; /**< normal cqe */
	const struct rte_memzone *ci_mz = NULL; /**< compact cqe */
	const struct rte_memzone *pi_mz = NULL;
	u32 queue_buf_size;
	u16 vector_len;
	void *db_addr = NULL;
	int wqe_count;
	int err;
	int ci_mz_size = sizeof(*rxq->rq_ci), ci_mz_align = RTE_CACHE_LINE_SIZE;

	if (IS_QPOOL_MODE()) {
		/* alloc template if not */
		if (hwdev->qpool_qgrp_id == 0) { 
			err = hinic3_alloc_template(nic_dev); 
			if (err < 0) 
				goto alloc_template_fail;
		}

		/* Get user queue */
		err = hinic3_get_rx_user_queue(nic_dev, rxq);
		if (err < 0)
			goto get_rx_user_queue_fail;
	} else {
		rxq->local_qid = rxq->q_id;
	}

	if (nic_dev->vec_allowed)
		vector_len = rxq->q_depth + HINIC3_DEFAULT_RX_BURST;
	else
		vector_len = rxq->q_depth;

	pi_mz = hinic3_dma_zone_reserve(dev, "hinic3_rq_pi", qid,
					 RTE_PGSIZE_4K, RTE_CACHE_LINE_SIZE,
					 (int)socket_id);
	if (!pi_mz) {
		PMD_DRV_LOG(ERR, "Allocate rxq[%d] pi_mz failed, dev_name: %s",
			    qid, dev->data->name);
		err = -ENOMEM;
		goto alloc_pi_mz_fail;
	}
	rxq->pi_mz = pi_mz;
	rxq->pi_dma_addr = pi_mz->iova;
	rxq->pi_virt_addr = pi_mz->addr;

	err = hinic3_alloc_db_addr(hwdev, &db_addr, HINIC3_DB_TYPE_RQ);
	if (err) {
		PMD_DRV_LOG(ERR, "Alloc rq doorbell addr failed.");
		goto alloc_db_err_fail;
	}
	rxq->db_addr = db_addr;

	queue_buf_size = BIT(rxq->wqebb_shift) * vector_len;
	rq_mz = hinic3_dma_zone_reserve(dev, "hinic3_rq_mz", qid,
					queue_buf_size, RTE_PGSIZE_256K,
					(int)socket_id);
	if (!rq_mz) {
		PMD_DRV_LOG(ERR, "Allocate rxq[%d] rq_mz failed, dev_name: %s",
			    qid, dev->data->name);
		err = -ENOMEM;
		goto alloc_rq_mz_fail;
	}
	memset(rq_mz->addr, 0, queue_buf_size);
	rxq->rq_mz = rq_mz;
	rxq->queue_buf_paddr = rq_mz->iova;
	rxq->queue_buf_vaddr = rq_mz->addr;

	rxq->rx_info = rte_zmalloc_socket("rx_info",
					  vector_len * sizeof(*rxq->rx_info),
					  RTE_CACHE_LINE_SIZE, (int)socket_id);
	if (!rxq->rx_info) {
		PMD_DRV_LOG(ERR, "Allocate rx_info failed, dev_name: %s",
			dev->data->name);
		err = -ENOMEM;
		goto alloc_rx_info_fail;
	}

	if (HINIC3_SUPPORT_RX_HW_COMPACT_CQE(nic_dev) ||
	    HINIC3_SUPPORT_RX_SW_COMPACT_CQE(nic_dev)) {
		ci_mz = hinic3_dma_zone_reserve(dev, "hinic3_ci_mz", qid,
						ci_mz_size, ci_mz_align, (int)socket_id);

		if (!ci_mz) {
			PMD_DRV_LOG(ERR, "Allocate ci mem zone failed, dev_name: %s", dev->data->name);
			err = -ENOMEM;
			goto alloc_cqe_ci_mz_fail;
		}

		memset(ci_mz->addr, 0, sizeof(*rxq->rq_ci));
		rxq->rq_ci = (struct hinic3_rq_ci_wb *)ci_mz->addr;
		rxq->rq_ci_paddr = ci_mz->iova;
	} else {
		cqe_mz = hinic3_dma_zone_reserve(dev, "hinic3_cqe_mz", qid,
						vector_len * sizeof(*rxq->rx_cqe),
						RTE_CACHE_LINE_SIZE, (int)socket_id);
		if (!cqe_mz) {
			PMD_DRV_LOG(ERR, "Allocate cqe mem zone failed, dev_name: %s",
				dev->data->name);
			err = -ENOMEM;
			goto alloc_cqe_ci_mz_fail;
		}
		memset(cqe_mz->addr, 0, vector_len * sizeof(*rxq->rx_cqe));
		rxq->cqe_mz = cqe_mz;
		rxq->cqe_start_paddr = cqe_mz->iova;
		rxq->cqe_start_vaddr = cqe_mz->addr;
		rxq->rx_cqe = (struct hinic3_rq_cqe *)rxq->cqe_start_vaddr;

		/* step5 fill cqe dma addr*/
		wqe_count = hinic3_rx_fill_wqe(rxq);
		if (wqe_count != rxq->q_depth) {
			PMD_DRV_LOG(ERR, "Fill rx wqe failed, wqe_count: %d, dev_name: %s",
				wqe_count, dev->data->name);
			err = -ENOMEM;
			goto fill_rx_wqe_fail;
		}
	}

	return 0;

fill_rx_wqe_fail:
	hinic3_memzone_free(rxq->cqe_mz);

alloc_cqe_ci_mz_fail:
	rte_free(rxq->rx_info);

alloc_rx_info_fail:
	hinic3_memzone_free(rxq->rq_mz);

alloc_rq_mz_fail:
	rxq->db_addr = NULL;
alloc_db_err_fail:
	hinic3_memzone_free(rxq->pi_mz);

alloc_pi_mz_fail:
	if (IS_QPOOL_MODE())
		hinic3_release_user_queue(nic_dev, qid);

get_rx_user_queue_fail:
	if (IS_QPOOL_MODE())
		hinic3_release_template(nic_dev);

alloc_template_fail:
	rte_free(rxq);
	nic_dev->rxqs[qid] = NULL;

	return err;
}

/**
 * Create the receive queue.
 *
 * @param[in] dev
 *   Pointer to ethernet device structure.
 * @param[in] qid
 *   Receive queue index.
 * @param[in] nb_desc
 *   Number of descriptors for receive queue.
 * @param[in] socket_id
 *   Socket index on which memory must be allocated.
 * @param rx_conf
 *   Thresholds parameters (unused_).
 * @param mp
 *   Memory pool for buffer allocations.
 *
 * @retval zero : Success
 * @retval non-zero : Failure
 */
static int hinic3_rx_queue_setup(struct rte_eth_dev *dev, uint16_t qid,
			uint16_t nb_desc, unsigned int socket_id,
			__rte_unused const struct rte_eth_rxconf *rx_conf,
			struct rte_mempool *mp)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct hinic3_rxq *rxq = NULL;
	u16 rq_depth, rx_free_thresh, vec_len;
	u32 buf_size;
	int err;

	if (!IS_QPOOL_MODE()) {
		/* Queue depth must be equal to queue 0 */
		if (qid != 0 && (nb_desc != nic_dev->rxqs[0]->q_depth)) {
			PMD_DRV_LOG(WARNING, "rxq%u depth:%u is not equal to queue0 depth:%u.\n",
				qid, nb_desc, nic_dev->rxqs[0]->q_depth);
			nb_desc = nic_dev->rxqs[0]->q_depth;
		}
	} else {
		err = hinic3_verify_queue_depth(nic_dev, &nb_desc, HINIC3_VERIFY_RX_DEPTH);
 		if (err) {
 			PMD_DRV_LOG(ERR, "Get queue depth failed");
 			return -EINVAL;
 		}
	}

	/* Queue depth must be power of 2, otherwise will be aligned up */
	rq_depth = (nb_desc & (nb_desc - 1)) ?
		((u16)(1U << (ilog2(nb_desc) + 1))) : nb_desc;

	/*
	 * Validate number of receive descriptors.
	 * It must not exceed hardware maximum and minimum.
	 */
	if (rq_depth > HINIC3_MAX_QUEUE_DEPTH ||
		rq_depth < HINIC3_MIN_QUEUE_DEPTH) {
		PMD_DRV_LOG(ERR, "RX queue depth is out of range from %d to %d,"
			    "(nb_desc: %d, q_depth: %d, port: %d queue: %d)",
			    HINIC3_MIN_QUEUE_DEPTH, HINIC3_MAX_QUEUE_DEPTH,
			    (int)nb_desc, (int)rq_depth,
			    (int)dev->data->port_id, (int)qid);
		return -EINVAL;
	}

	/*
	 * The RX descriptor ring will be cleaned after rxq->rx_free_thresh
	 * descriptors are used or if the number of descriptors required
	 * to transmit a packet is greater than the number of free RX
	 * descriptors.
	 * The following constraints must be satisfied:
	 *  -rx_free_thresh must be greater than 0.
	 *  -rx_free_thresh must be less than the size of the ring minus 1.
	 * When set to zero use default values.
	 */
	rx_free_thresh = (u16)((rx_conf->rx_free_thresh) ?
		rx_conf->rx_free_thresh : HINIC3_DEFAULT_RX_FREE_THRESH);
	if (rx_free_thresh >= (rq_depth - 1)) {
		PMD_DRV_LOG(ERR, "rx_free_thresh must be less than the number "
			    "of RX descriptors minus 1, rx_free_thresh: %u port: %d queue: %d)",
			    (unsigned int)rx_free_thresh,
			    (int)dev->data->port_id, (int)qid);

		return -EINVAL;
	}

	rxq = rte_zmalloc_socket("hinic3_rq", sizeof(struct hinic3_rxq),
				 RTE_CACHE_LINE_SIZE, (int)socket_id);
	if (!rxq) {
		PMD_DRV_LOG(ERR, "Allocate rxq[%d] failed, dev_name: %s",
			    qid, dev->data->name);

		return -ENOMEM;
	}

	/* Init rq parameters */
	rxq->nic_dev = nic_dev;
	nic_dev->rxqs[qid] = rxq;
	rxq->mb_pool = mp;
	rxq->q_id = qid;
	rxq->next_to_update = 0;
	rxq->q_depth = rq_depth;
	rxq->q_mask = rq_depth - 1;
	rxq->delta = rq_depth;
	rxq->cons_idx = 0;
	rxq->prod_idx = 0;
	rxq->rx_free_thresh = rx_free_thresh;
	rxq->rxinfo_align_end = rxq->q_depth - rxq->rx_free_thresh;
	rxq->port_id = dev->data->port_id;
	rxq->wait_time_cycle = HINIC3_RX_WAIT_CYCLE_THRESH;

	/* If buf_len used for function table, need to translated */
	err = hinic3_convert_rx_buf_size(
				rte_pktmbuf_data_room_size(rxq->mb_pool) -
				RTE_PKTMBUF_HEADROOM, &buf_size);
	if (err) {
		PMD_DRV_LOG(ERR, "Adjust buf size failed, dev_name: %s",
			    dev->data->name);
		goto adjust_bufsize_fail;
	}


	if (HINIC3_SUPPORT_RX_HW_COMPACT_CQE(nic_dev) ||
	    HINIC3_SUPPORT_RX_SW_COMPACT_CQE(nic_dev)) {
		/* Default rx wqe type set to compact wqe if NIC supports compact rx CQE */
		rxq->wqe_type = HINIC3_COMPACT_RQ_WQE;
	} else {
		if ((buf_size >= HINIC3_RX_BUF_SIZE_4K) && (buf_size < HINIC3_RX_BUF_SIZE_16K))
			rxq->wqe_type = HINIC3_EXTEND_RQ_WQE;
		else
			rxq->wqe_type = HINIC3_NORMAL_RQ_WQE;
	}

	rxq->wqebb_shift = HINIC3_RQ_WQEBB_SHIFT + rxq->wqe_type;
	rxq->wqebb_size = (u16)BIT(rxq->wqebb_shift);

	rxq->buf_len = (u16)buf_size;
	rxq->rx_buff_shift = ilog2(rxq->buf_len);

	err = hinic3_rx_queue_dma_create(dev, rxq, qid, socket_id);
 	if (err) {
		goto rx_queue_dma_fail;
	}

	/* Record rxq pointer in rte_eth rx_queues */
	dev->data->rx_queues[qid] = rxq;

	return 0;

adjust_bufsize_fail:
	rte_free(rxq);
	nic_dev->rxqs[qid] = NULL;
rx_queue_dma_fail:
	return err;
}

static int hinic3_get_tx_user_queue(struct hinic3_nic_dev *nic_dev, struct hinic3_txq *txq)
{
	struct drv_cmd_user_queue_get queueinfo;
	struct msg_module msg_to_kernel;
	int in_size, out_size, err;

	queueinfo.qid = txq->q_id;

	(void)memset(&msg_to_kernel, 0, sizeof(msg_to_kernel));
	in_size = sizeof(queueinfo);
	out_size = sizeof(queueinfo);

	fill_ioctl_msg(&msg_to_kernel, SEND_TO_NIC_DRIVER, GET_USER_QUEUE_ID, in_size, out_size,
		&queueinfo, &queueinfo);
	err = ioctl(nic_dev->fd, 0, &msg_to_kernel);
	if (err < 0)
		PMD_DRV_LOG(ERR, "Get tx user queue error: %d.", errno);

	txq->local_qid = queueinfo.local_qid;

	hinic3_indir_set_qid_mmap(txq->q_id, txq->local_qid);

	return err;
}


static int
hinic3_tx_queue_dma_create(struct rte_eth_dev *dev, struct hinic3_txq *txq,
			   uint16_t qid, unsigned int socket_id)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct hinic3_hwdev *hwdev = nic_dev->hwdev;
	const struct rte_memzone *sq_mz = NULL;
	const struct rte_memzone *ci_mz = NULL;
	void *db_addr = NULL;
	u32 queue_buf_size;
	int err;

	if(IS_QPOOL_MODE()){
		/* alloc template if not */ 
		if (hwdev->qpool_qgrp_id == 0) { 
			err = hinic3_alloc_template(nic_dev); 
			if (err < 0) 
				goto alloc_template_fail;
		}

		err = hinic3_get_tx_user_queue(nic_dev, txq);
		if (err < 0)
			goto close_fd;
	} else {
		txq->local_qid = txq->q_id;
	}

	ci_mz = hinic3_dma_zone_reserve(dev, "hinic3_sq_ci", qid,
					HINIC3_CI_Q_ADDR_SIZE,
					HINIC3_CI_Q_ADDR_SIZE, (int)socket_id);
	if (!ci_mz) {
		PMD_DRV_LOG(ERR, "Allocate txq[%d] ci_mz failed, dev_name: %s",
			    qid, dev->data->name);
		err = -ENOMEM;
		goto alloc_ci_mz_fail;
	}
	txq->ci_mz = ci_mz;
	txq->ci_dma_base = ci_mz->iova;
	txq->ci_vaddr_base = (volatile u16 *)ci_mz->addr;

	queue_buf_size = BIT(txq->wqebb_shift) * txq->q_depth;
	sq_mz = hinic3_dma_zone_reserve(dev, "hinic3_sq_mz", qid,
					queue_buf_size, RTE_PGSIZE_256K,
					(int)socket_id);
	if (!sq_mz) {
		PMD_DRV_LOG(ERR, "Allocate txq[%d] sq_mz failed, dev_name: %s",
			    qid, dev->data->name);
		err = -ENOMEM;
		goto alloc_sq_mz_fail;
	}

	memset(sq_mz->addr, 0, queue_buf_size);
	txq->sq_mz = sq_mz;
	txq->queue_buf_paddr = sq_mz->iova;
	txq->queue_buf_vaddr = sq_mz->addr;
	txq->sq_head_addr = (u64)txq->queue_buf_vaddr;
	txq->sq_bot_sge_addr = txq->sq_head_addr + queue_buf_size;
	txq->multi_segs = (dev->data->dev_conf.txmode.offloads & DEV_TX_OFFLOAD_MULTI_SEGS) ? true : false;

	err = hinic3_alloc_db_addr(hwdev, &db_addr, HINIC3_DB_TYPE_SQ);
	if (err) {
		PMD_DRV_LOG(ERR, "Alloc sq doorbell addr failed");
		goto alloc_db_err_fail;
	}
	txq->db_addr = db_addr;

	txq->tx_info = rte_zmalloc_socket("tx_info",
					  txq->q_depth * sizeof(*txq->tx_info),
					  RTE_CACHE_LINE_SIZE, (int)socket_id);
	if (!txq->tx_info) {
		PMD_DRV_LOG(ERR, "Allocate tx_info failed, dev_name: %s",
			    dev->data->name);
		err = -ENOMEM;
		goto alloc_tx_info_fail;
	}

	return 0;

alloc_tx_info_fail:
	rte_free(txq->tx_info);

alloc_db_err_fail:
	hinic3_memzone_free(txq->sq_mz);

alloc_sq_mz_fail:
	hinic3_memzone_free(txq->ci_mz);

alloc_ci_mz_fail:
close_fd:
alloc_template_fail:
	return err;
}

/**
 * Create the transmit queue.
 *
 * @param[in] dev
 *   Pointer to ethernet device structure.
 * @param[in] queue_idx
 *   Transmit queue index.
 * @param[in] nb_desc
 *   Number of descriptors for transmit queue.
 * @param[in] socket_id
 *   Socket index on which memory must be allocated.
 * @param[in] tx_conf
 *   Tx queue configuration parameters (unused_).
 *
 * @retval zero : Success
 * @retval non-zero : Failure
 */
static int hinic3_tx_queue_setup(struct rte_eth_dev *dev, uint16_t qid,
			 uint16_t nb_desc, unsigned int socket_id,
			 __rte_unused const struct rte_eth_txconf *tx_conf)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct hinic3_txq *txq = NULL;
	u16 sq_depth, tx_free_thresh;
	int err;

	if (!IS_QPOOL_MODE()) {
		/* Queue depth must be equal to queue 0 */
		if (qid != 0 && (nb_desc != nic_dev->txqs[0]->q_depth)) {
			PMD_DRV_LOG(WARNING, "txq%u depth:%u is not equal to queue0 depth:%u.\n",
				qid, nb_desc, nic_dev->txqs[0]->q_depth);
			nb_desc = nic_dev->txqs[0]->q_depth;
		}
	} else {
		err = hinic3_verify_queue_depth(nic_dev, &nb_desc, HINIC3_VERIFY_TX_DEPTH);
 		if (err) {
 			PMD_DRV_LOG(ERR, "Get queue depth failed");
 			return -EINVAL;
 		}	
	}

	/* Queue depth must be power of 2, otherwise will be aligned up */
	sq_depth = (nb_desc & (nb_desc - 1)) ?
		   ((u16)(1U << (ilog2(nb_desc) + 1))) : nb_desc;

	/*
	 * Validate number of transmit descriptors.
	 * It must not exceed hardware maximum and minimum.
	 */
	if (sq_depth > HINIC3_MAX_QUEUE_DEPTH ||
		sq_depth < HINIC3_MIN_QUEUE_DEPTH) {
		PMD_DRV_LOG(ERR, "TX queue depth is out of range from %d to %d,"
			    "(nb_desc: %d, q_depth: %d, port: %d queue: %d)",
			    HINIC3_MIN_QUEUE_DEPTH, HINIC3_MAX_QUEUE_DEPTH,
			    (int)nb_desc, (int)sq_depth,
			    (int)dev->data->port_id, (int)qid);
		return -EINVAL;
	}

	/*
	 * The TX descriptor ring will be cleaned after txq->tx_free_thresh
	 * descriptors are used or if the number of descriptors required
	 * to transmit a packet is greater than the number of free TX
	 * descriptors.
	 * The following constraints must be satisfied:
	 *  -tx_free_thresh must be greater than 0.
	 *  -tx_free_thresh must be less than the size of the ring minus 1.
	 * When set to zero use default values.
	 */
	tx_free_thresh = (u16)((tx_conf->tx_free_thresh) ?
		tx_conf->tx_free_thresh : HINIC3_DEFAULT_TX_FREE_THRESH);
	if (tx_free_thresh >= (sq_depth - 1)) {
		PMD_DRV_LOG(ERR, "tx_free_thresh must be less than the number of tx "
			    "descriptors minus 1, tx_free_thresh: %u port: %d queue: %d",
			    (unsigned int)tx_free_thresh,
			    (int)dev->data->port_id, (int)qid);
		return -EINVAL;
	}

	txq = rte_zmalloc_socket("hinic3_tx_queue", sizeof(struct hinic3_txq),
				 RTE_CACHE_LINE_SIZE, (int)socket_id);
	if (!txq) {
		PMD_DRV_LOG(ERR, "Allocate txq[%d] failed, dev_name: %s",
			    qid, dev->data->name);
		return -ENOMEM;
	}
	nic_dev->txqs[qid] = txq;
	txq->nic_dev = nic_dev;
	txq->q_id = qid;
	txq->q_depth = sq_depth;
	txq->q_mask = sq_depth - 1;
	txq->cons_idx = 0;
	txq->prod_idx = 0;
	txq->wqebb_shift = HINIC3_SQ_WQEBB_SHIFT;
	txq->wqebb_size = (u16)BIT(txq->wqebb_shift);
	txq->tx_free_thresh = tx_free_thresh;
	txq->owner = 1;
	txq->is_sp620_nic = is_sp620_nic(nic_dev);
	txq->tx_free_loop = nic_dev->config.tx_free_loop;
	if (nic_dev->dcb->dcb_on)
		txq->cos = nic_dev->dcb->txq_cos[qid];
	else {
		if (!ODD_NUMBER_QUEUE_ID(qid) &&
		    hinic3_cmd_vf_lag(nic_dev->hwdev, hinic3_global_func_id(nic_dev->hwdev), HINIC3_CMD_OPCODE_GET) == 1)
			txq->cos = SELECT_OTHER_COS_ID(nic_dev->default_cos);
		else
			txq->cos = nic_dev->default_cos;
	}

	txq->tx_wqe_compact_task = HINIC3_SUPPORT_TX_WQE_COMPACT_TASK(nic_dev);

	err = hinic3_tx_queue_dma_create(dev, txq, qid, socket_id);
	if (err) {
		nic_dev->txqs[qid] = NULL;
		rte_free(txq);
		return -ENOMEM;
	}

	/* Record txq pointer in rte_eth tx_queues */
	dev->data->tx_queues[qid] = txq;

	return 0;
}

#ifndef DPDK_21_11
static void hinic3_rx_queue_release(void *queue)
{
	struct hinic3_rxq *rxq = queue;
#else
static void hinic3_rx_queue_release(struct rte_eth_dev *dev, uint16_t queue_id)
{
	if (dev == NULL || dev->data == NULL || dev->data->rx_queues == NULL) {
		PMD_DRV_LOG(WARNING, "rx queue is null when release");
		return;
	}
	if (queue_id >= dev->data->nb_rx_queues) {
		PMD_DRV_LOG(WARNING, "eth_dev: %s, rx queue id: %u is illegal",
			dev->data->name, queue_id);
		return;
	}
	struct hinic3_rxq *rxq = dev->data->rx_queues[queue_id];
#endif
	struct hinic3_nic_dev *nic_dev = NULL;
	int err;

	if (!rxq) {
		PMD_DRV_LOG(WARNING, "Rxq is null when release");
		return;
	}

	nic_dev = rxq->nic_dev;

	hinic3_free_rxq_mbufs(rxq);

	if (IS_QPOOL_MODE()) {
		if (nic_dev->fd < 0) {
			PMD_DRV_LOG(WARNING, "NIC device queue release fd < 0. fd = %d", nic_dev->fd);
			goto release_resources;
		}

		if (rxq->q_id == 0) {
			err = hinic3_release_template(nic_dev);
			if (err < 0) {
				PMD_DRV_LOG(WARNING, "NIC device queue release template err, err = %d", err);
				goto release_resources;
			}
		}

		err = hinic3_release_user_queue(nic_dev, rxq->q_id);
		if (err < 0) {
			PMD_DRV_LOG(WARNING, "NIC device queue release user queue err, err = %d", err);
			goto release_resources;
		}

		u32 rqcqe_buf_size = RQCQE_BUF_SIZE(rxq->q_depth);
		munmap(rxq->cqe_start_vaddr, rqcqe_buf_size);

		rte_free(rxq->rx_info);
		rxq->rx_info = NULL;

		u32 queue_buf_size = BIT(rxq->wqebb_shift) * rxq->q_depth;
		munmap(rxq->queue_buf_vaddr, queue_buf_size);
	}

release_resources:
	if (!rxq->is_hairpin) {
		hinic3_memzone_free(rxq->cqe_mz);
		hinic3_memzone_free(rxq->rq_mz);
		hinic3_memzone_free(rxq->pi_mz);

		rte_free(rxq->rx_info);
		rxq->rx_info = NULL;
	}

	nic_dev->rxqs[rxq->q_id] = NULL;
	rte_free(rxq);
#ifdef DPDK_21_11
	dev->data->rx_queues[queue_id] = NULL;
#endif
}

#ifndef DPDK_21_11
static void hinic3_tx_queue_release(void *queue)
{
	struct hinic3_txq *txq = queue;
#else
static void hinic3_tx_queue_release(struct rte_eth_dev *dev, uint16_t queue_id)
{
	if (dev == NULL || dev->data == NULL || dev->data->tx_queues == NULL) {
		PMD_DRV_LOG(WARNING, "tx queue is null when release");
		return;
	}
	if (queue_id >= dev->data->nb_tx_queues) {
		PMD_DRV_LOG(WARNING, "eth_dev: %s, tx queue id: %u is illegal",
			dev->data->name, queue_id);
		return;
	}
	struct hinic3_txq *txq = dev->data->tx_queues[queue_id];
#endif
	struct hinic3_nic_dev *nic_dev = NULL;

	if (!txq) {
		PMD_DRV_LOG(WARNING, "Txq is null when release");
		return;
	}
	PMD_DRV_LOG(INFO, "%s txq_idx:%d queue release.\n", txq->nic_dev->dev_name, txq->q_id);
	nic_dev = txq->nic_dev;

	hinic3_free_txq_mbufs(txq);

	rte_free(txq->tx_info);
	txq->tx_info = NULL;

	hinic3_memzone_free(txq->sq_mz);

	hinic3_memzone_free(txq->ci_mz);

	nic_dev->txqs[txq->q_id] = NULL;
	rte_free(txq);
#ifdef DPDK_21_11
	dev->data->tx_queues[queue_id] = NULL;
#endif
}

static int hinic3_dev_rx_queue_start(__rte_unused struct rte_eth_dev *dev,
				     __rte_unused uint16_t rq_id)
{
	struct hinic3_rxq *rxq = NULL;
	int rc;

	if (rq_id < dev->data->nb_rx_queues) {
		rxq = dev->data->rx_queues[rq_id];

		rc = hinic3_start_rq(dev, rxq);
		if (rc) {
			PMD_DRV_LOG(ERR, "Start rx queue failed, eth_dev:%s, queue_idx:%d",
					dev->data->name, rq_id);
			return rc;
		}

		dev->data->rx_queue_state[rq_id] = RTE_ETH_QUEUE_STATE_STARTED;
	}

	if (!IS_QPOOL_MODE()) {
 		rc = hinic3_enable_rxq_fdir_filter(dev, (u32)rq_id, (u32)true);
 		if (rc) {
 			PMD_DRV_LOG(ERR, "Failed to enable rq : %d fdir filter.", rq_id);
 			return rc;
 		}
 	}

	return 0;
}

static int hinic3_dev_rx_queue_stop(__rte_unused struct rte_eth_dev *dev,
				    __rte_unused uint16_t rq_id)
{
	struct hinic3_rxq *rxq = NULL;
	int rc;

	if (rq_id < dev->data->nb_rx_queues) {
		rxq = dev->data->rx_queues[rq_id];

		rc = hinic3_stop_rq(dev, rxq);
		if (rc) {
			PMD_DRV_LOG(ERR, "Stop rx queue failed, eth_dev:%s, queue_idx:%d",
					dev->data->name, rq_id);
			return rc;
		}

		dev->data->rx_queue_state[rq_id] = RTE_ETH_QUEUE_STATE_STOPPED;
	}

	if (!IS_QPOOL_MODE()) {
 		rc = hinic3_enable_rxq_fdir_filter(dev, (u32)rq_id, (u32)false);
 		if (rc) {
 			PMD_DRV_LOG(ERR, "Failed to disable rq : %d fdir filter.", rq_id);
 			return rc;
 		}
 	}

	return 0;
}

static int hinic3_dev_tx_queue_start(__rte_unused struct rte_eth_dev *dev,
				     __rte_unused uint16_t sq_id)
{
	struct hinic3_txq *txq = NULL;

	PMD_DRV_LOG(INFO, "Start tx queue, eth_dev:%s, queue_idx:%d",
		   dev->data->name, sq_id);

	if (sq_id >= dev->data->nb_tx_queues) {
		PMD_DRV_LOG(ERR, "sq_id %u exceeds nb_tx_queues %u",
			    sq_id, dev->data->nb_tx_queues);
		return -EINVAL;
	}

	txq = dev->data->tx_queues[sq_id];
	if (txq == NULL) {
		PMD_DRV_LOG(ERR, "txq[%u] is NULL", sq_id);
		return -EINVAL;
	}
	HINIC3_SET_TXQ_STARTED(txq);
	dev->data->tx_queue_state[sq_id] = RTE_ETH_QUEUE_STATE_STARTED;
	return 0;
}

static int hinic3_dev_tx_queue_stop(__rte_unused struct rte_eth_dev *dev,
				    __rte_unused uint16_t sq_id)
{
	struct hinic3_txq *txq = NULL;
	int rc;

	if (sq_id < dev->data->nb_tx_queues) {
		txq = dev->data->tx_queues[sq_id];
		rc = hinic3_stop_sq(txq);
		if (rc) {
			PMD_DRV_LOG(ERR, "Stop tx queue failed, eth_dev:%s, queue_idx:%d",
				   dev->data->name, sq_id);
			return rc;
		}

		HINIC3_SET_TXQ_STOPPED(txq);
		dev->data->tx_queue_state[sq_id] = RTE_ETH_QUEUE_STATE_STOPPED;
	}

	return 0;
}

int hinic3_dev_rx_queue_intr_enable(struct rte_eth_dev *dev,
				    uint16_t queue_id)
{
	struct rte_pci_device *pci_dev = RTE_ETH_DEV_TO_PCI(dev);
	struct rte_intr_handle *intr_handle = PCI_DEV_TO_INTR_HANDLE(pci_dev);
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	u16 msix_intr;

	if (!rte_intr_dp_is_en(intr_handle) || !intr_handle->intr_vec)
		return 0;

	if (queue_id >= dev->data->nb_rx_queues)
		return -EINVAL;

	msix_intr = (u16)intr_handle->intr_vec[queue_id];
	hinic3_set_msix_auto_mask_state(nic_dev->hwdev, msix_intr,
					HINIC3_SET_MSIX_AUTO_MASK);
	hinic3_set_msix_state(nic_dev->hwdev, msix_intr, HINIC3_MSIX_ENABLE);

	return 0;
}

int hinic3_dev_rx_queue_intr_disable(struct rte_eth_dev *dev, uint16_t queue_id)
{
	struct rte_pci_device *pci_dev = RTE_ETH_DEV_TO_PCI(dev);
	struct rte_intr_handle *intr_handle = PCI_DEV_TO_INTR_HANDLE(pci_dev);
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	u16 msix_intr;

	if (!rte_intr_dp_is_en(intr_handle) || !intr_handle->intr_vec)
		return 0;

	if (queue_id >= dev->data->nb_rx_queues)
		return -EINVAL;

	msix_intr = (u16)intr_handle->intr_vec[queue_id];
	hinic3_set_msix_auto_mask_state(nic_dev->hwdev, msix_intr,
					HINIC3_CLR_MSIX_AUTO_MASK);
	hinic3_set_msix_state(nic_dev->hwdev, msix_intr, HINIC3_MSIX_DISABLE);
	hinic3_misx_intr_clear_resend_bit(nic_dev->hwdev, msix_intr, MSIX_RESEND_TIMER_CLEAR);

	return 0;
}

#ifndef DPDK_20_11
static uint32_t hinic3_dev_rx_queue_count(__rte_unused struct rte_eth_dev *dev,
					  __rte_unused uint16_t rq_id)
{
	return 0;
}

static int hinic3_dev_rx_descriptor_done(__rte_unused void *rq,
					 __rte_unused uint16_t offset)
{
	return 0;
}

static int hinic3_dev_rx_descriptor_status(__rte_unused void *rx_queue,
					   __rte_unused uint16_t offset)
{
	return 0;
}

static int hinic3_dev_tx_descriptor_status(__rte_unused void *tx_queue,
					   __rte_unused uint16_t offset)
{
	return 0;
}
#endif

static int hinic3_set_lro(struct hinic3_nic_dev *nic_dev, struct rte_eth_conf *dev_conf)
{
	bool lro_en;
	int max_lro_size, lro_max_pkt_len;
	int err;

	/* Config lro */
	lro_en = dev_conf->rxmode.offloads & DEV_RX_OFFLOAD_TCP_LRO ?
		 true : false;
	max_lro_size = (int)(dev_conf->rxmode.max_lro_pkt_size);
	lro_max_pkt_len = max_lro_size / HINIC3_LRO_UNIT_WQE_SIZE ?
		      max_lro_size / HINIC3_LRO_UNIT_WQE_SIZE : 1;

	PMD_DRV_LOG(INFO, "max_lro_size: %d, rx_buff_len: %d, lro_max_pkt_len: %d",
		    max_lro_size, nic_dev->rx_buff_len, lro_max_pkt_len);
	PMD_DRV_LOG(INFO, "max_rx_pkt_len: %d", HINIC3_MAX_RX_PKT_LEN(dev_conf->rxmode));
	err = hinic3_set_rx_lro_state(nic_dev->hwdev, lro_en,
				      HINIC3_LRO_DEFAULT_TIME_LIMIT,
				      lro_max_pkt_len);
	if (err) {
		PMD_DRV_LOG(ERR, "Set lro state failed, err: %d", err);
	}
	return err;
}

static int hinic3_set_vlan(struct rte_eth_dev *dev, struct rte_eth_conf *dev_conf)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	bool vlan_filter, vlan_strip;
	int err;

	/* Config vlan filter */
	vlan_filter = dev_conf->rxmode.offloads & DEV_RX_OFFLOAD_VLAN_FILTER ?
		      true : false;

	err = hinic3_set_vlan_fliter(nic_dev->hwdev, vlan_filter);
	if (err) {
		PMD_DRV_LOG(ERR, "Config vlan filter failed, device: %s, port_id: %d, err: %d",
			    nic_dev->dev_name, dev->data->port_id, err);
		return err;
	}

	/* Config vlan stripping */
	vlan_strip = dev_conf->rxmode.offloads & DEV_RX_OFFLOAD_VLAN_STRIP ?
		     true : false;

	err = hinic3_set_rx_vlan_offload(nic_dev->hwdev, vlan_strip);
	if (err) {
		PMD_DRV_LOG(ERR, "Config vlan strip failed, device: %s, port_id: %d, err: %d",
			    nic_dev->dev_name, dev->data->port_id, err);
	}

	return err;
}

static int hinic3_set_rxtx_configure(struct rte_eth_dev *dev)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct rte_eth_conf *dev_conf = &dev->data->dev_conf;
	struct rte_eth_rss_conf *rss_conf = NULL;
	int err;

	/* Config rx mode */
	err = hinic3_set_rx_mode(nic_dev->hwdev, HINIC3_DEFAULT_RX_MODE);
	if (err) {
		PMD_DRV_LOG(ERR, "Set rx_mode: 0x%x failed",
			    HINIC3_DEFAULT_RX_MODE);
		return err;
	}
	nic_dev->rx_mode = HINIC3_DEFAULT_RX_MODE;

	/* Config rx checksum offload */
	if (dev_conf->rxmode.offloads & DEV_RX_OFFLOAD_CHECKSUM)
		nic_dev->rx_csum_en = HINIC3_DEFAULT_RX_CSUM_OFFLOAD;

	err = hinic3_set_lro(nic_dev, dev_conf);
	if (err) {
		PMD_DRV_LOG(ERR, "Set lro failed");
		return err;
	}
	/* Config RSS */
	if ((dev_conf->rxmode.mq_mode & ETH_MQ_RX_RSS_FLAG) &&
		nic_dev->num_rqs > 1) {
		rss_conf = &(dev_conf->rx_adv_conf.rss_conf);
		err = hinic3_update_rss_config(dev, rss_conf);
		if (err) {
			PMD_DRV_LOG(ERR, "Set rss config failed, err: %d", err);
			return err;
		}

		if (nic_dev->dcb->dcb_on) {
			err = hinic3_dcb_rss_init(nic_dev, nic_dev->dcb->dcb_on);
			if (err) {
				PMD_DRV_LOG(ERR, "Set dcb rss config failed, err: %d", err);
				return err;
			}
		}
	}

	err = hinic3_set_vlan(dev, dev_conf);
	if (err) {
		PMD_DRV_LOG(ERR, "Set vlan failed, err: %d", err);
		return err;
	}

	hinic3_init_rx_queue_list(nic_dev);

	return 0;
}

static void hinic3_remove_rxtx_configure(struct rte_eth_dev *dev)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	u8 prio_tc[HINIC3_DCB_UP_MAX] = {0};

	hinic3_set_rx_mode(nic_dev->hwdev, 0);

	if (nic_dev->rss_state == HINIC3_RSS_ENABLE) {
		hinic3_rss_cfg(nic_dev->hwdev, HINIC3_RSS_DISABLE, 0, prio_tc);
		hinic3_rss_template_free(nic_dev->hwdev, 0);
		nic_dev->rss_state = HINIC3_RSS_DISABLE;
	}
}

static bool hinic3_find_vlan_filter(struct hinic3_nic_dev *nic_dev,
				    uint16_t vlan_id)
{
	u32 vid_idx, vid_bit;

	vid_idx = HINIC3_VFTA_IDX(vlan_id);
	vid_bit = HINIC3_VFTA_BIT(vlan_id);

	return (nic_dev->vfta[vid_idx] & vid_bit) ? true : false;
}

static void hinic3_store_vlan_filter(struct hinic3_nic_dev *nic_dev,
					u16 vlan_id, bool on)
{
	u32 vid_idx, vid_bit;

	vid_idx = HINIC3_VFTA_IDX(vlan_id);
	vid_bit = HINIC3_VFTA_BIT(vlan_id);

	if (on)
		nic_dev->vfta[vid_idx] |= vid_bit;
	else
		nic_dev->vfta[vid_idx] &= ~vid_bit;
}

static void hinic3_remove_all_vlanid(struct rte_eth_dev *dev)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	int vlan_id;
	u16 func_id;

	func_id = hinic3_global_func_id(nic_dev->hwdev);

	for (vlan_id = 1; vlan_id < RTE_ETHER_MAX_VLAN_ID; vlan_id++) {
		if (hinic3_find_vlan_filter(nic_dev, vlan_id)) {
			hinic3_del_vlan(nic_dev->hwdev, vlan_id, func_id);
			hinic3_store_vlan_filter(nic_dev, vlan_id, false);
		}
	}
}

static void hinic3_disable_interrupt(struct rte_eth_dev *dev)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct rte_pci_device *pci_dev = RTE_ETH_DEV_TO_PCI(dev);

	if (!hinic3_get_bit(HINIC3_DEV_INIT, &nic_dev->dev_status))
		return;

	if (IS_QPOOL_MODE()) {
 		rte_intr_callback_unregister(PCI_DEV_TO_INTR_HANDLE(pci_dev),
 					hinic3_dev_interrupt_handler_qpool, (void *)dev);
 	} else {
 		/* disable rte interrupt */
 		rte_intr_disable(PCI_DEV_TO_INTR_HANDLE(pci_dev));
 		rte_intr_callback_unregister(PCI_DEV_TO_INTR_HANDLE(pci_dev),
 					hinic3_dev_interrupt_handler, (void *)dev);
 	}
}

static void hinic3_enable_interrupt(struct rte_eth_dev *dev)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct rte_pci_device *pci_dev = RTE_ETH_DEV_TO_PCI(dev);

	if (!hinic3_get_bit(HINIC3_DEV_INIT, &nic_dev->dev_status))
		return;

	if (IS_QPOOL_MODE()) {
 		rte_intr_callback_register(PCI_DEV_TO_INTR_HANDLE(pci_dev),
 					hinic3_dev_interrupt_handler_qpool, (void *)dev);
 	} else {
 		/* enable rte interrupt */
 		rte_intr_enable(PCI_DEV_TO_INTR_HANDLE(pci_dev));
 		rte_intr_callback_register(PCI_DEV_TO_INTR_HANDLE(pci_dev),
 					hinic3_dev_interrupt_handler, (void *)dev);
 	}
}

#define HINIC3_RX_VEC_START RTE_INTR_VEC_RXTX_OFFSET

/* dp interrupt msix attribute */
#define HINIC3_TXRX_MSIX_PENDING_LIMIT       2
#define HINIC3_TXRX_MSIX_COALESC_TIMER       2
#define HINIC3_TXRX_MSIX_RESEND_TIMER_CFG    7

static int hinic3_init_rxq_msix_attr(void *hwdev, u16 msix_index)
{
	struct interrupt_info info = { 0 };
	int err;

	info.lli_set = 0;
	info.interrupt_coalesc_set = 1;
	info.pending_limt = HINIC3_TXRX_MSIX_PENDING_LIMIT;
	info.coalesc_timer_cfg = HINIC3_TXRX_MSIX_COALESC_TIMER;
	info.resend_timer_cfg = HINIC3_TXRX_MSIX_RESEND_TIMER_CFG;

	info.msix_index = msix_index;
	err = hinic3_set_interrupt_cfg(hwdev, info);
	if (err) {
		PMD_DRV_LOG(ERR, "Set msix attr failed, msix_index %d\n",
			    msix_index);
		return -EFAULT;
	}

	return 0;
}

static void hinic3_deinit_rxq_intr(struct rte_eth_dev *dev)
{
	struct rte_intr_handle *intr_handle = dev->intr_handle;

	rte_intr_efd_disable(intr_handle);
	if (intr_handle->intr_vec) {
		rte_free(intr_handle->intr_vec);
		intr_handle->intr_vec = NULL;
	}
}

static int hinic3_init_rxq_intr(struct rte_eth_dev *dev)
{
	struct rte_intr_handle *intr_handle = NULL;
	struct hinic3_nic_dev *nic_dev = NULL;
	struct hinic3_rxq *rxq = NULL;
	u32 nb_rx_queues, i;
	int err;

	intr_handle = dev->intr_handle;
	nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	if (!dev->data->dev_conf.intr_conf.rxq)
		return 0;

	if (!rte_intr_cap_multiple(intr_handle)) {
		PMD_DRV_LOG(ERR, "Rx queue interrupts require MSI-X interrupts"
			" (vfio-pci driver)\n");
		return -ENOTSUP;
	}

	nb_rx_queues = dev->data->nb_rx_queues;
	err = rte_intr_efd_enable(intr_handle, nb_rx_queues);
	if (err) {
		PMD_DRV_LOG(ERR, "Failed to enable event fds for Rx queue interrupts\n");
		return err;
	}

	intr_handle->intr_vec = rte_zmalloc("hinic_intr_vec",
					    nb_rx_queues * sizeof(int), 0);
	if (intr_handle->intr_vec == NULL) {
		PMD_DRV_LOG(ERR, "Failed to allocate intr_vec\n");
		rte_intr_efd_disable(intr_handle);
		return -ENOMEM;
	}
#ifdef DPDK_21_11
	intr_handle->vec_list_size = nb_rx_queues;
#endif
	for (i = 0; i < nb_rx_queues; i++)
		intr_handle->intr_vec[i] = (int)(i + HINIC3_RX_VEC_START);

	for (i = 0; i < dev->data->nb_rx_queues; i++) {
		rxq = dev->data->rx_queues[i];
		rxq->dp_intr_en = 1;
		rxq->msix_entry_idx = (u16)intr_handle->intr_vec[i];

		err = hinic3_init_rxq_msix_attr(nic_dev->hwdev,
						rxq->msix_entry_idx);
		if (err) {
			hinic3_deinit_rxq_intr(dev);
			return err;
		}
	}

	return 0;
}

static int hinic3_init_sw_rxtxqs(struct hinic3_nic_dev *nic_dev)
{
	u32 txq_size;
	u32 rxq_size;

	/* Allocate software txq array */
	txq_size = nic_dev->max_sqs * sizeof(*nic_dev->txqs);
	nic_dev->txqs = rte_zmalloc("hinic3_txqs", txq_size,
				    RTE_CACHE_LINE_SIZE);
	if (!nic_dev->txqs) {
		PMD_DRV_LOG(ERR, "Allocate txqs failed");
		return -ENOMEM;
	}

	/* Allocate software rxq array */
	rxq_size = nic_dev->max_rqs * sizeof(*nic_dev->rxqs);
	nic_dev->rxqs = rte_zmalloc("hinic3_rxqs", rxq_size,
				    RTE_CACHE_LINE_SIZE);
	if (!nic_dev->rxqs) {
		/* Free txqs */
		rte_free(nic_dev->txqs);
		nic_dev->txqs = NULL;

		PMD_DRV_LOG(ERR, "Allocate rxqs failed");
		return -ENOMEM;
	}

	return 0;
}

static void hinic3_deinit_sw_rxtxqs(struct hinic3_nic_dev *nic_dev)
{
	rte_free(nic_dev->txqs);
	nic_dev->txqs = NULL;

	rte_free(nic_dev->rxqs);
	nic_dev->rxqs = NULL;
}

static void hinic3_disable_queue_intr(struct rte_eth_dev *dev)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct rte_intr_handle *intr_handle = dev->intr_handle;
	int msix_intr;
	int i;
	u16 nb_rx_queues = dev->data->nb_rx_queues;

	if (intr_handle->intr_vec == NULL) {
		return;
	}

	for (i = 0; i < nic_dev->num_rqs; i++) {
		msix_intr = intr_handle->intr_vec[i];
		hinic3_set_msix_state(nic_dev->hwdev, (u16)msix_intr, HINIC3_MSIX_DISABLE);
		hinic3_misx_intr_clear_resend_bit(nic_dev->hwdev, (u16)msix_intr, MSIX_RESEND_TIMER_CLEAR);
	}

	return;
}

static int hinic3_refill_hairpinq(struct rte_eth_dev *dev)
{
	struct hinic3_rxq *rxq = NULL;
	struct hinic3_txq *txq = NULL;
	struct rte_eth_hairpin_conf conf;
	int ret;
	int i;
#ifdef DPDK_20_11
	conf.manual_bind = 1;
	conf.tx_explicit = 1;
#endif
#ifdef DPDK_22_11
	conf.use_locked_device_memory = 0;
	conf.use_rte_memory = 0;
	conf.force_memory = 0;
#endif
	conf.peer_count = 0;
	for (i=0; i < dev->data->nb_rx_queues; i++) {
		rxq = dev->data->rx_queues[i];
		if (rxq == NULL) {
			ret = hinic3_rx_hairpin_queue_setup(dev, i, HINIC3_MIN_QUEUE_DEPTH, &conf);
			if (ret != 0)
				return ret;
		}
	}
	for (i=0; i < dev->data->nb_tx_queues; i++) {
		txq = dev->data->tx_queues[i];
		if (txq == NULL) {
			ret = hinic3_tx_hairpin_queue_setup(dev, i, HINIC3_MIN_QUEUE_DEPTH, &conf);
			if (ret != 0)
				return ret;
		}
	}
	return 0;
}

static void hinic3_print_hairpin_map(struct rte_eth_dev *dev)
{
	struct hinic3_rxq *rxq = NULL;
	struct rte_eth_hairpin_conf *conf;
	int i;
	for (i=0; i < dev->data->nb_rx_queues; i++) {
		rxq = dev->data->rx_queues[i];
		if (rxq == NULL || !rxq->is_hairpin)
			continue;
		conf = &rxq->hairpin_conf;
		if (conf->peer_count == 0)
			PMD_DRV_LOG(INFO, "Port %u rxq %u usable", dev->data->port_id, i);
		else
			PMD_DRV_LOG(INFO, "Port %u rxq %u -> Port %u txq %u",
						dev->data->port_id, i, conf->peers[0].port, conf->peers[0].queue);
	}
}

/**
 * Start the device.
 *
 * Initialize function table, rxq and txq context, config rx offload, and enable
 * vport and port to prepare receiving packets.
 *
 * @param[in] eth_dev
 *   Pointer to ethernet device structure.
 *
 * @retval zero : Success
 * @retval non-zero : Failure
 */

static int hinic3_dev_start_qpool(struct rte_eth_dev *eth_dev)
{
	struct hinic3_nic_dev *nic_dev = NULL;
	struct hinic3_rxq *rxq = NULL;
	int i;
	int err;

	nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(eth_dev);
	hinic3_get_func_rx_buf_size(nic_dev);
	err = hinic3_get_kernel_mtu(eth_dev);
	if (err)
		return err;

	err = hinic3_get_kernel_addr(eth_dev);
	if (err)
		return err;

	err = hinic3_set_feature_to_hw(nic_dev->hwdev, &nic_dev->feature_cap, 1);
	if (err) {
		PMD_DRV_LOG(ERR, "Failed to set nic features to hardware, err %d",
			    err);
		goto get_feature_err;
	}

	/* reset rx and tx queue */
	hinic3_reset_rx_queue(eth_dev);
	hinic3_reset_tx_queue(eth_dev);

	/* Init txq and rxq context */
	hinic3_flush_assign_qps_res(nic_dev->hwdev);

	err = hinic3_init_qp_ctxts(nic_dev);
	if (err) {
		PMD_DRV_LOG(ERR, "Init qp context failed, dev_name: %s",
			    eth_dev->data->name);
		goto handle_err;
	}

	/* Set rx configuration: rss/checksum/rxmode/lro */
	err = hinic3_set_rxtx_configure(eth_dev);
	if (err) {
		PMD_DRV_LOG(ERR, "Set rx config failed, dev_name: %s",
			    eth_dev->data->name);
	}

	/* Add scatter support if scatter mode should be enabled */
	if (eth_dev->data->dev_conf.rxmode.offloads & DEV_RX_OFFLOAD_SCATTER)
		eth_dev->data->scattered_rx = true;
	else
		eth_dev->data->scattered_rx = false;

	err = hinic3_start_all_rqs(eth_dev);
	if (err) {
		PMD_DRV_LOG(ERR, "Set rx config failed, dev_name: %s",
			    eth_dev->data->name);
		goto handle_err;
	}

	hinic3_start_all_sqs(eth_dev);

	/* Update eth_dev link status */
	if (eth_dev->data->dev_conf.intr_conf.lsc != 0)
		(void)hinic3_link_update(eth_dev, 0);

	hinic3_set_bit(HINIC3_DEV_START, &nic_dev->dev_status);

	return 0;

handle_err:
	/* Flush tx && rx chip resources in case of setting vport fake fail */
	(void)hinic3_flush_qps_res(nic_dev->hwdev);
	rte_delay_ms(DEV_START_DELAY_MS);
	for (i = 0; i < nic_dev->num_rqs; i++) {
		rxq = nic_dev->rxqs[i];
		hinic3_remove_rq_from_rx_queue_list(nic_dev, rxq->q_id);
		hinic3_free_rxq_mbufs(rxq);
		hinic3_dev_rx_queue_intr_disable(eth_dev, rxq->q_id);
		eth_dev->data->rx_queue_state[i] = RTE_ETH_QUEUE_STATE_STOPPED;
		eth_dev->data->tx_queue_state[i] = RTE_ETH_QUEUE_STATE_STOPPED;
	}
	hinic3_remove_rxtx_configure(eth_dev);

	hinic3_free_qp_ctxts(nic_dev->hwdev);

get_feature_err:
	hinic3_deinit_rxq_intr(eth_dev);
	hinic3_copy_mempool_uninit(nic_dev);

	return err;
}

static int hinic3_dev_start(struct rte_eth_dev *eth_dev)
{
	struct hinic3_nic_dev *nic_dev = NULL;
	u64 nic_features;
	struct hinic3_rxq *rxq = NULL;
	int i;
	int err;

	nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(eth_dev);
	err = hinic3_copy_mempool_init(nic_dev);
	if (err) {
		PMD_DRV_LOG(ERR, "Create copy mempool failed, dev_name: %s",
			 eth_dev->data->name);
		goto init_mpool_fail;
	}
	hinic3_update_msix_info(nic_dev->hwdev->hwif);

	nic_features = hinic3_get_driver_feature(nic_dev);
	/* You can update the features supported by the driver according to the
	 * scenario here
	 */
	nic_features &= DEFAULT_DRV_FEATURE;
	hinic3_update_driver_feature(nic_dev, nic_features);

	if (IS_QPOOL_MODE())
 		return hinic3_dev_start_qpool(eth_dev);

	hinic3_disable_interrupt(eth_dev);

	err = hinic3_refill_hairpinq(eth_dev);
	if (err) {
		PMD_DRV_LOG(ERR, "Refill hairpinq fail, dev_name: %s",
			 eth_dev->data->name);
		goto refill_hairpin_fail;
	}
	hinic3_print_hairpin_map(eth_dev);
	err = hinic3_init_rxq_intr(eth_dev);
	if (err) {
		PMD_DRV_LOG(ERR, "Init rxq intr fail, eth_dev:%s",
			    eth_dev->data->name);
		goto init_rxq_intr_fail;
	}

	hinic3_get_func_rx_buf_size(nic_dev);
	err = hinic3_init_function_table(nic_dev->hwdev, nic_dev->rx_buff_len);
	if (err) {
		PMD_DRV_LOG(ERR, "Init function table failed, dev_name: %s",
			    eth_dev->data->name);
		goto init_func_tbl_fail;
	}

	err = hinic3_set_feature_to_hw(nic_dev->hwdev, &nic_dev->feature_cap, 1);
	if (err) {
		PMD_DRV_LOG(ERR, "Failed to set nic features to hardware, err %d\n",
			    err);
		goto get_feature_err;
	}

	/* reset rx and tx queue */
	hinic3_reset_rx_queue(eth_dev);
	hinic3_reset_tx_queue(eth_dev);

	/* Init txq and rxq context */
	err = hinic3_init_qp_ctxts(nic_dev);
	if (err) {
		PMD_DRV_LOG(ERR, "Init qp context failed, dev_name: %s",
			    eth_dev->data->name);
		goto init_qp_fail;
	}

	/* Set default mtu */
	err = hinic3_set_port_mtu(nic_dev->hwdev, nic_dev->mtu_size);
	if (err) {
		PMD_DRV_LOG(ERR, "Set mtu_size[%d] failed, dev_name: %s",
			    nic_dev->mtu_size, eth_dev->data->name);
		goto set_mtu_fail;
	}
	eth_dev->data->mtu = nic_dev->mtu_size;

	/* Set rx configuration: rss/checksum/rxmode/lro */
	err = hinic3_set_rxtx_configure(eth_dev);
	if (err) {
		PMD_DRV_LOG(ERR, "Set rx config failed, dev_name: %s",
			    eth_dev->data->name);
		goto set_rxtx_config_fail;
	}

	/* Add scatter support if scatter mode should be enabled */
	if (eth_dev->data->dev_conf.rxmode.offloads & DEV_RX_OFFLOAD_SCATTER )
		eth_dev->data->scattered_rx = true;
	else
		eth_dev->data->scattered_rx = false;

	/* enable dev interrupt */
	hinic3_enable_interrupt(eth_dev);
	err = hinic3_start_all_rqs(eth_dev);
	if (err) {
		PMD_DRV_LOG(ERR, "Set rx config failed, dev_name: %s",
			    eth_dev->data->name);
		goto start_rqs_fail;
	}

	hinic3_start_all_sqs(eth_dev);

	/* Open virtual port and ready to start packet receiving */
	err = hinic3_set_vport_enable(nic_dev->hwdev, true);
	if (err) {
		PMD_DRV_LOG(ERR, "Enable vport failed, dev_name: %s",
			    eth_dev->data->name);
		goto en_vport_fail;
	}

	/* Open physical port and start packet receiving */
	err = hinic3_set_port_enable(nic_dev->hwdev, true);
	if (err) {
		PMD_DRV_LOG(ERR, "Enable physical port failed, dev_name: %s",
			    eth_dev->data->name);
		goto en_port_fail;
	}

	hinic3_tm_dev_start_proc(eth_dev);

	/* Update eth_dev link status */
	if (eth_dev->data->dev_conf.intr_conf.lsc != 0)
		(void)hinic3_link_update(eth_dev, 0);

	hinic3_set_bit(HINIC3_DEV_START, &nic_dev->dev_status);

	return 0;

en_port_fail:
	(void)hinic3_set_vport_enable(nic_dev->hwdev, false);

en_vport_fail:
	/* Flush tx && rx chip resources in case of setting vport fake fail */
	(void)hinic3_flush_qps_res(nic_dev->hwdev);
	rte_delay_ms(DEV_START_DELAY_MS);
	for (i = 0; i < nic_dev->num_rqs; i++) {
		rxq = nic_dev->rxqs[i];
		hinic3_remove_rq_from_rx_queue_list(nic_dev, rxq->q_id);
		hinic3_free_rxq_mbufs(rxq);
		hinic3_dev_rx_queue_intr_disable(eth_dev, rxq->q_id);
		eth_dev->data->rx_queue_state[i] = RTE_ETH_QUEUE_STATE_STOPPED;
		eth_dev->data->tx_queue_state[i] = RTE_ETH_QUEUE_STATE_STOPPED;
	}
start_rqs_fail:
	hinic3_remove_rxtx_configure(eth_dev);

set_rxtx_config_fail:
set_mtu_fail:
	hinic3_free_qp_ctxts(nic_dev->hwdev);

init_qp_fail:
get_feature_err:
init_func_tbl_fail:
    hinic3_deinit_rxq_intr(eth_dev);
init_rxq_intr_fail:
refill_hairpin_fail:
	hinic3_copy_mempool_uninit(nic_dev);
init_mpool_fail:
	return err;
}

static int hinic3_copy_mempool_init(struct hinic3_nic_dev *nic_dev)
{
	nic_dev->cpy_mpool = rte_mempool_lookup(HINCI3_CPY_MEMPOOL_NAME);
	if (nic_dev->cpy_mpool == NULL) {
		nic_dev->cpy_mpool =
		rte_pktmbuf_pool_create(HINCI3_CPY_MEMPOOL_NAME,
					HINIC3_COPY_MEMPOOL_DEPTH, HINIC3_COPY_MEMPOOL_CACHE, 0,
					HINIC3_COPY_MBUF_SIZE,
					(int)rte_socket_id());
		if (nic_dev->cpy_mpool == NULL) {
			PMD_DRV_LOG(ERR, "Create copy mempool failed, errno: %d, dev_name: %s",
				    rte_errno, HINCI3_CPY_MEMPOOL_NAME);
			return -ENOMEM;
		}
	}

	return 0;
}

static void hinic3_copy_mempool_uninit(struct hinic3_nic_dev *nic_dev)
{
	nic_dev->cpy_mpool = NULL;
}

/**
 * Stop the device.
 *
 * Stop phy port and vport, flush pending io request, clean context configure
 * and free io resourece.
 *
 * @param[in] dev
 *   Pointer to ethernet device structure.
 */
#ifdef DPDK_20_11
static int hinic3_dev_stop(struct rte_eth_dev *dev)
#else
static void hinic3_dev_stop(struct rte_eth_dev *dev)
#endif
{
	struct hinic3_nic_dev *nic_dev;
	struct rte_eth_link link;
	int err;
	uint16_t i;
	u8 sec_tcam_en = 0;

	nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	if (!hinic3_test_and_clear_bit(HINIC3_DEV_START,
				       &nic_dev->dev_status)) {
		PMD_DRV_LOG(INFO, "Device %s already stopped",
			    nic_dev->dev_name);
#ifdef DPDK_20_11
		return 0;
#endif
	}

	if (nic_dev->dcb->dcb_on) {
		if (is_sp620_nic(nic_dev) ||
		    !HINIC3_IS_VF(nic_dev->hwdev))
			hinic3_sync_dcb_state(nic_dev->hwdev, 1, 0);
	}

	hinic3_tm_dev_stop_proc(dev);

	/* Clear recorded link status */
	memset(&link, 0, sizeof(link));
	(void)rte_eth_linkstatus_set(dev, &link);

	/* Stop phy port and vport */
	if (!IS_QPOOL_MODE()) {
 		err = hinic3_set_port_enable(nic_dev->hwdev, false);
 		if (err)
 			PMD_DRV_LOG(WARNING, "Disable phy port failed, error: %d, "
 					"dev_name: %s, port_id: %d", err, dev->data->name,
 					dev->data->port_id);

 		err = hinic3_set_vport_enable(nic_dev->hwdev, false);
 		if (err)
 			PMD_DRV_LOG(WARNING, "Disable vport failed, error: %d, "
 					"dev_name: %s, port_id: %d", err, dev->data->name,
 					dev->data->port_id);

 		/* disable dp interrupt */
 		hinic3_disable_queue_intr(dev);
 		hinic3_deinit_rxq_intr(dev);
 	}

	/* Flush pending io request */
	hinic3_flush_txqs(nic_dev);

	/* After set vport disable 100ms,
	 * no packets will be send to host
	 */
	rte_delay_ms(DEV_STOP_DELAY_MS);

	if (!IS_QPOOL_MODE()) {
 		hinic3_flush_qps_res(nic_dev->hwdev);

 		/* Clean root context */
 		hinic3_free_qp_ctxts(nic_dev->hwdev);
 	} else {
 		hinic3_flush_assign_qps_res(nic_dev->hwdev);
 	}

	/* Clean RSS table and rx_mode */
	hinic3_remove_rxtx_configure(dev);

	/* Free all tx and rx mbufs */
	hinic3_free_all_txq_mbufs(nic_dev);
	hinic3_free_all_rxq_mbufs(nic_dev);

	/* Free mempool */
	hinic3_copy_mempool_uninit(nic_dev);

	for (i = 0; i < dev->data->nb_rx_queues; i++)
		dev->data->rx_queue_state[i] = RTE_ETH_QUEUE_STATE_STOPPED;
	for (i = 0; i < dev->data->nb_tx_queues; i++)
		dev->data->tx_queue_state[i] = RTE_ETH_QUEUE_STATE_STOPPED;

	/* Clear scatter rx flag */
	dev->data->scattered_rx = false;

	(void)hinic3_fdir_cfg_sec_tcam(nic_dev->hwdev, &sec_tcam_en);

	(void)hinic3_flush_tcam_rule(nic_dev->hwdev);
	if (sec_tcam_en == 1)
		(void)hinic3_fdir_flush_sec_tcam_rule(nic_dev->hwdev);

#ifdef DPDK_20_11
	return 0;
#endif
}

static void hinic3_dev_release(struct rte_eth_dev *eth_dev)
{
	struct hinic3_nic_dev *nic_dev =
		HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(eth_dev);
	struct rte_pci_device *pci_dev = RTE_ETH_DEV_TO_PCI(eth_dev);
	int qid;

	/* Release io resource */
	for (qid = 0; qid < nic_dev->num_sqs; qid++)
#ifdef DPDK_21_11
		hinic3_tx_queue_release(eth_dev, qid);
#else
		hinic3_tx_queue_release(nic_dev->txqs[qid]);
#endif

	for (qid = 0; qid < nic_dev->num_rqs; qid++)
#ifdef DPDK_21_11
		hinic3_rx_queue_release(eth_dev, qid);
#else
		hinic3_rx_queue_release(nic_dev->rxqs[qid]);
#endif

	hinic3_deinit_sw_rxtxqs(nic_dev);

	hinic3_deinit_mac_addr(eth_dev);
	rte_free(nic_dev->mc_list);

	hinic3_remove_all_vlanid(eth_dev);

	hinic3_clear_bit(HINIC3_DEV_INTR_EN, &nic_dev->dev_status);

	if (IS_QPOOL_MODE()) {
 		(void)rte_intr_callback_unregister(PCI_DEV_TO_INTR_HANDLE(pci_dev),
 					   hinic3_dev_interrupt_handler_qpool,
 					   (void *)eth_dev);
 	} else {
 		hinic3_set_msix_state(nic_dev->hwdev, 0, HINIC3_MSIX_DISABLE);
 		rte_intr_disable(PCI_DEV_TO_INTR_HANDLE(pci_dev));
 		(void)rte_intr_callback_unregister(PCI_DEV_TO_INTR_HANDLE(pci_dev),
 					   hinic3_dev_interrupt_handler,
 					   (void *)eth_dev);
 	}


	/* Destroy rx mode mutex */
	hinic3_mutex_destroy(&nic_dev->rx_mode_mutex);

	hinic3_free_nic_hwdev(nic_dev->hwdev);
	hinic3_free_hwdev(nic_dev->hwdev);

	if (IS_QPOOL_MODE())
		close(nic_dev->fd);

	eth_dev->rx_pkt_burst = NULL;
	eth_dev->tx_pkt_burst = NULL;
	eth_dev->dev_ops = NULL;

	rte_free(nic_dev->hwdev);
	nic_dev->hwdev = NULL;
	rte_free(nic_dev->ptype_tbl);
	nic_dev->ptype_tbl = NULL;
}

/**
 * Close the device.
 *
 * @param[in] dev
 *   Pointer to ethernet device structure.
 */
#ifdef DPDK_20_11
static int hinic3_dev_close(struct rte_eth_dev *eth_dev)
#else
static void hinic3_dev_close(struct rte_eth_dev *eth_dev)
#endif
{
	struct hinic3_nic_dev *nic_dev =
		HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(eth_dev);

	if (rte_eal_process_type() != RTE_PROC_PRIMARY) {
#ifdef DPDK_20_11
	return 0;
#else
	return;
#endif
	}

#ifdef DPDK_20_11
	int ret;
#endif

	if (!IS_QPOOL_MODE()) {
		if (hinic3_test_and_set_bit(HINIC3_DEV_CLOSE, &nic_dev->dev_status)) {
			PMD_DRV_LOG(WARNING, "Device %s already closed",
			    	nic_dev->dev_name);
#ifdef DPDK_20_11
		return 0;
#endif
		}
	}
#ifdef DPDK_20_11
	ret = hinic3_dev_stop(eth_dev);
#else
	hinic3_dev_stop(eth_dev);
#endif

	hinic3_dev_release(eth_dev);
#ifdef DPDK_20_11
	return ret;
#endif
}

static int hinic3_dev_reset(__rte_unused struct rte_eth_dev *dev)
{
	return 0;
}

#define MIN_RX_BUFFER_SIZE 256
#define MIN_RX_BUFFER_SIZE_SMALL_MODE 1518

static int hinic3_dev_set_mtu(struct rte_eth_dev *dev, uint16_t mtu)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	uint32_t frame_size = mtu + HINIC3_ETH_OVERHEAD;
	int err = 0;

	if (IS_QPOOL_MODE()) {
 	 	PMD_DRV_LOG(WARNING, "Qpool mode not support set mtu.");
 	 	return -EINVAL;
 	}

	PMD_DRV_LOG(INFO, "Set port mtu, port_id: %d, mtu: %d, max_pkt_len: %d",
		    dev->data->port_id, mtu, HINIC3_MTU_TO_PKTLEN(mtu));

	if (mtu < HINIC3_MIN_MTU_SIZE || mtu > HINIC3_MAX_MTU_SIZE) {
		PMD_DRV_LOG(ERR, "Invalid mtu: %d, must between %d and %d",
			    mtu, HINIC3_MIN_MTU_SIZE, HINIC3_MAX_MTU_SIZE);
		return -EINVAL;
	}

	if (dev->data->dev_started && !dev->data->scattered_rx &&
		frame_size > nic_dev->rx_buff_len) {
			PMD_DRV_LOG(ERR, "failed to set mtu because current is"
					"not scattered rx mode, frame_size: %u, rx_buff_len: %u",
					frame_size, nic_dev->rx_buff_len);
			return -EOPNOTSUPP;
		}

	err = hinic3_set_port_mtu(nic_dev->hwdev, mtu);
	if (err) {
		PMD_DRV_LOG(ERR, "Set port mtu failed, err: %d", err);
		return err;
	}

	/* Update max frame size */
	HINIC3_MAX_RX_PKT_LEN(dev->data->dev_conf.rxmode) = HINIC3_MTU_TO_PKTLEN(mtu);
	nic_dev->mtu_size = mtu;
	return err;
}

/**
 * Add or delete vlan id.
 *
 * @param[in] dev
 *   Pointer to ethernet device structure.
 * @param[in] vlan_id
 *   Vlan id is used to filter vlan packets
 * @param[in] enable
 *   Disable or enable vlan filter function
 *
 * @retval zero: Success
 * @retval non-zero: Failure
 */
static int hinic3_vlan_filter_set(struct rte_eth_dev *dev,
				  uint16_t vlan_id, int enable)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	int err = 0;
	u16 func_id;

	if (vlan_id > RTE_ETHER_MAX_VLAN_ID)
		return -EINVAL;

	if (vlan_id == 0)
		return 0;

	func_id = hinic3_global_func_id(nic_dev->hwdev);

	if (enable) {
		/* If vlanid is already set, just return */
		if (hinic3_find_vlan_filter(nic_dev, vlan_id)) {
			PMD_DRV_LOG(INFO, "Vlan %u has been added, device: %s",
				    vlan_id, nic_dev->dev_name);
			return 0;
		}

		err = hinic3_add_vlan(nic_dev->hwdev, vlan_id, func_id);
	} else {
		/* If vlanid can't be found, just return */
		if (!hinic3_find_vlan_filter(nic_dev, vlan_id)) {
			PMD_DRV_LOG(INFO, "Vlan %u is not in the vlan filter list, device: %s",
				    vlan_id, nic_dev->dev_name);
			return 0;
		}

		err = hinic3_del_vlan(nic_dev->hwdev, vlan_id, func_id);
	}

	if (err) {
		PMD_DRV_LOG(ERR, "%s vlan failed, func_id: %d, vlan_id: %d, err: %d",
			    enable ? "Add" : "Remove", func_id, vlan_id, err);
		return err;
	}

	hinic3_store_vlan_filter(nic_dev, vlan_id, enable);

	PMD_DRV_LOG(INFO, "%s vlan %u succeed, device: %s",
		    enable ? "Add" : "Remove", vlan_id, nic_dev->dev_name);

	return 0;
}

/**
 * Enable or disable vlan offload.
 *
 * @param[in] dev
 *   Pointer to ethernet device structure.
 * @param[in] mask
 *   Definitions used for VLAN setting, vlan filter of vlan strip
 *
 * @retval zero: Success
 * @retval non-zero: Failure
 */
static int hinic3_vlan_offload_set(struct rte_eth_dev *dev, int mask)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct rte_eth_rxmode *rxmode = &dev->data->dev_conf.rxmode;
	bool on;
	int err;

	/* Enable or disable VLAN filter */
	if (mask & ETH_VLAN_FILTER_MASK) {
		on = (rxmode->offloads & DEV_RX_OFFLOAD_VLAN_FILTER) ?
			true : false;
		err = hinic3_set_vlan_fliter(nic_dev->hwdev, on);
		if (err) {
			PMD_DRV_LOG(ERR, "%s vlan filter failed, device: %s, port_id: %d, err: %d",
				    on ? "Enable" : "Disable",
				    nic_dev->dev_name, dev->data->port_id, err);
			return err;
		}

		PMD_DRV_LOG(INFO, "%s vlan filter succeed, device: %s, port_id: %d",
			    on ? "Enable" : "Disable",
			    nic_dev->dev_name, dev->data->port_id);
	}

	/* Enable or disable VLAN stripping */
	if (mask & ETH_VLAN_STRIP_MASK) {
		on = (rxmode->offloads & DEV_RX_OFFLOAD_VLAN_STRIP) ?
		     true : false;
		err = hinic3_set_rx_vlan_offload(nic_dev->hwdev, on);
		if (err) {
			PMD_DRV_LOG(ERR, "%s vlan strip failed, device: %s, port_id: %d, err: %d",
				    on ? "Enable" : "Disable",
				    nic_dev->dev_name, dev->data->port_id, err);
			return err;
		}

		PMD_DRV_LOG(INFO, "%s vlan strip succeed, device: %s, port_id: %d",
			    on ? "Enable" : "Disable",
			    nic_dev->dev_name, dev->data->port_id);
	}
	return 0;
}

/**
 * Enable allmulticast mode.
 *
 * @param[in] dev
 *   Pointer to ethernet device structure.
 *
 * @retval zero: Success
 * @retval non-zero: Failure
 */
static int hinic3_dev_allmulticast_enable(struct rte_eth_dev *dev)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	u32 rx_mode;
	int err;

	if (IS_QPOOL_MODE()) {
 		PMD_DRV_LOG(WARNING, "Qpool mode not support set allmulticast enable.");
 		return -ENOTSUP;
 	}

	err = hinic3_mutex_lock(&nic_dev->rx_mode_mutex);
	if (err)
		return err;

	rx_mode = nic_dev->rx_mode | HINIC3_RX_MODE_MC_ALL;

	err = hinic3_set_rx_mode(nic_dev->hwdev, rx_mode);
	if (err) {
		(void)hinic3_mutex_unlock(&nic_dev->rx_mode_mutex);
		PMD_DRV_LOG(ERR, "Enable allmulticast failed, error: %d", err);
		return err;
	}

	nic_dev->rx_mode = rx_mode;

	(void)hinic3_mutex_unlock(&nic_dev->rx_mode_mutex);

	PMD_DRV_LOG(INFO, "Enable allmulticast succeed, nic_dev: %s, port_id: %d",
		    nic_dev->dev_name, dev->data->port_id);
	return 0;
}

/**
 * Disable allmulticast mode.
 *
 * @param[in] dev
 *   Pointer to ethernet device structure.
 *
 * @retval zero: Success
 * @retval non-zero: Failure
 */
static int hinic3_dev_allmulticast_disable(struct rte_eth_dev *dev)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	u32 rx_mode;
	int err;

	if (IS_QPOOL_MODE()) {
 		PMD_DRV_LOG(WARNING, "Qpool mode not support set allmulticast disable.");
 		return -ENOTSUP;
 	}

	err = hinic3_mutex_lock(&nic_dev->rx_mode_mutex);
	if (err)
		return err;

	rx_mode = nic_dev->rx_mode & (~HINIC3_RX_MODE_MC_ALL);

	err = hinic3_set_rx_mode(nic_dev->hwdev, rx_mode);
	if (err) {
		(void)hinic3_mutex_unlock(&nic_dev->rx_mode_mutex);
		PMD_DRV_LOG(ERR, "Disable allmulticast failed, error: %d", err);
		return err;
	}

	nic_dev->rx_mode = rx_mode;

	(void)hinic3_mutex_unlock(&nic_dev->rx_mode_mutex);

	PMD_DRV_LOG(INFO, "Disable allmulticast succeed, nic_dev: %s, port_id: %d",
		    nic_dev->dev_name, dev->data->port_id);
	return 0;
}

/**
 * Enable promiscuous mode.
 *
 * @param[in] dev
 *   Pointer to ethernet device structure.
 *
 * @retval zero: Success
 * @retval non-zero: Failure
 */
static int hinic3_dev_promiscuous_enable(struct rte_eth_dev *dev)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	u32 rx_mode;
	int err;

	if (IS_QPOOL_MODE()) {
 	 	PMD_DRV_LOG(WARNING, "Qpool mode not support set promiscuous enable.");
 	 	return -ENOTSUP;
 	}

	if (!(nic_dev->feature_cap & NIC_F_PROMISC)) {
		PMD_DRV_LOG(ERR, "nic_dev: %s, port_id: %d, do not support vf promisc: %" PRIu64 "",
			nic_dev->dev_name, dev->data->port_id, nic_dev->feature_cap);
		return -ENOTSUP;
	}

	err = hinic3_mutex_lock(&nic_dev->rx_mode_mutex);
	if (err)
		return err;

	rx_mode = nic_dev->rx_mode | HINIC3_RX_MODE_PROMISC;

	err = hinic3_set_rx_mode(nic_dev->hwdev, rx_mode);
	if (err) {
		(void)hinic3_mutex_unlock(&nic_dev->rx_mode_mutex);
		PMD_DRV_LOG(ERR, "Enable promiscuous failed");
		return err;
	}

	nic_dev->rx_mode = rx_mode;

	(void)hinic3_mutex_unlock(&nic_dev->rx_mode_mutex);

	PMD_DRV_LOG(INFO, "Enable promiscuous, nic_dev: %s, port_id: %d, promisc: %d",
		    nic_dev->dev_name, dev->data->port_id,
		    dev->data->promiscuous);
	return 0;
}

/**
 * Disable promiscuous mode.
 *
 * @param[in] dev
 *   Pointer to ethernet device structure.
 *
 * @retval zero: Success
 * @retval non-zero: Failure
 */
static int hinic3_dev_promiscuous_disable(struct rte_eth_dev *dev)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	u32 rx_mode;
	int err;

	if (IS_QPOOL_MODE()) {
 	 	PMD_DRV_LOG(WARNING, "Qpool mode not support set promiscuous disable.");
 	 	return -ENOTSUP;
 	}

	if (!(nic_dev->feature_cap & NIC_F_PROMISC)) {
		PMD_DRV_LOG(ERR, "nic_dev: %s, port_id: %d, do not support vf promisc: %" PRIu64 "",
			nic_dev->dev_name, dev->data->port_id, nic_dev->feature_cap);
		return -ENOTSUP;
	}

	err = hinic3_mutex_lock(&nic_dev->rx_mode_mutex);
	if (err)
		return err;

	rx_mode = nic_dev->rx_mode & (~HINIC3_RX_MODE_PROMISC);

	err = hinic3_set_rx_mode(nic_dev->hwdev, rx_mode);
	if (err) {
		(void)hinic3_mutex_unlock(&nic_dev->rx_mode_mutex);
		PMD_DRV_LOG(ERR, "Disable promiscuous failed");
		return err;
	}

	nic_dev->rx_mode = rx_mode;

	(void)hinic3_mutex_unlock(&nic_dev->rx_mode_mutex);

	PMD_DRV_LOG(INFO, "Disable promiscuous, nic_dev: %s, port_id: %d, promisc: %d",
		    nic_dev->dev_name, dev->data->port_id,
		    dev->data->promiscuous);
	return 0;
}

static int hinic3_dev_flow_ctrl_get(struct rte_eth_dev *dev,
				    struct rte_eth_fc_conf *fc_conf)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct nic_pause_config nic_pause;
	int err;

	err = hinic3_mutex_lock(&nic_dev->pause_mutuex);
	if (err)
		return err;

	memset(&nic_pause, 0, sizeof(nic_pause));
	err = hinic3_get_pause_info(nic_dev->hwdev, &nic_pause);
	if (err) {
		(void)hinic3_mutex_unlock(&nic_dev->pause_mutuex);
		return err;
	}

	if (nic_dev->pause_set || !nic_pause.auto_neg) {
		nic_pause.rx_pause = nic_dev->nic_pause.rx_pause;
		nic_pause.tx_pause = nic_dev->nic_pause.tx_pause;
	}

	fc_conf->autoneg = nic_pause.auto_neg;

	if (nic_pause.tx_pause && nic_pause.rx_pause)
		fc_conf->mode = RTE_FC_FULL;
	else if (nic_pause.tx_pause)
		fc_conf->mode = RTE_FC_TX_PAUSE;
	else if (nic_pause.rx_pause)
		fc_conf->mode = RTE_FC_RX_PAUSE;
	else
		fc_conf->mode = RTE_FC_NONE;

	(void)hinic3_mutex_unlock(&nic_dev->pause_mutuex);
	return 0;
}

static int hinic3_dev_flow_ctrl_set(struct rte_eth_dev *dev,
				    struct rte_eth_fc_conf *fc_conf)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct nic_pause_config nic_pause;
	int err;

	if (IS_QPOOL_MODE()) {
 	 	PMD_DRV_LOG(WARNING, "Qpool mode not support ctrl set.");
 	 	return -ENOTSUP;
 	}

	err = hinic3_mutex_lock(&nic_dev->pause_mutuex);
	if (err)
		return err;

	memset(&nic_pause, 0, sizeof(nic_pause));
	if (((fc_conf->mode & RTE_FC_FULL) == RTE_FC_FULL) ||
	    (fc_conf->mode & RTE_FC_TX_PAUSE))
		nic_pause.tx_pause = true;

	if (((fc_conf->mode & RTE_FC_FULL) == RTE_FC_FULL) ||
	    (fc_conf->mode & RTE_FC_RX_PAUSE))
		nic_pause.rx_pause = true;

	err = hinic3_set_pause_info(nic_dev->hwdev, nic_pause);
	if (err) {
		(void)hinic3_mutex_unlock(&nic_dev->pause_mutuex);
		return err;
	}

	nic_dev->pause_set = true;
	nic_dev->nic_pause.rx_pause = nic_pause.rx_pause;
	nic_dev->nic_pause.tx_pause = nic_pause.tx_pause;

	PMD_DRV_LOG(INFO, "Just support set tx or rx pause info, tx: %s, rx: %s\n",
		    nic_pause.tx_pause ? "on" : "off",
		    nic_pause.rx_pause ? "on" : "off");

	(void)hinic3_mutex_unlock(&nic_dev->pause_mutuex);
	return 0;
}

/**
 * Update the RSS hash key and RSS hash type.
 *
 * @param[in] dev
 *   Pointer to ethernet device structure.
 * @param[in] rss_conf
 *   RSS configuration data.
 *
 * @retval zero: Success
 * @retval non-zero: Failure
 */
static int hinic3_rss_hash_update(struct rte_eth_dev *dev,
				  struct rte_eth_rss_conf *rss_conf)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct hinic3_rss_type rss_type = {0};
	u64 rss_hf = rss_conf->rss_hf;
	int err = 0;

	if (nic_dev->rss_state == HINIC3_RSS_DISABLE) {
		if (rss_hf != 0)
			return -EINVAL;

		PMD_DRV_LOG(INFO, "RSS is not enabled");
		return 0;
	}

	if (rss_conf->rss_key_len > HINIC3_RSS_KEY_SIZE) {
		PMD_DRV_LOG(ERR, "Invalid RSS key, rss_key_len: %d",
			    rss_conf->rss_key_len);
		return -EINVAL;
	}

	if (rss_conf->rss_key) {
		err = hinic3_rss_set_hash_key(nic_dev->hwdev, rss_conf->rss_key,
					      HINIC3_RSS_KEY_SIZE);
		if (err) {
			PMD_DRV_LOG(ERR, "Set RSS hash key failed");
			return err;
		}
		memcpy((void *)nic_dev->rss_key, (void *)rss_conf->rss_key,
		       (size_t)rss_conf->rss_key_len);
	}

	rss_type.ipv4 = (rss_hf & (ETH_RSS_IPV4 | ETH_RSS_FRAG_IPV4 |
		ETH_RSS_NONFRAG_IPV4_OTHER)) ? 1 : 0;
	rss_type.tcp_ipv4 = (rss_hf & ETH_RSS_NONFRAG_IPV4_TCP) ? 1 : 0;
	rss_type.ipv6 = (rss_hf & (ETH_RSS_IPV6 | ETH_RSS_FRAG_IPV6 |
		ETH_RSS_NONFRAG_IPV6_OTHER)) ? 1 : 0;
	rss_type.tcp_ipv6 = (rss_hf & ETH_RSS_NONFRAG_IPV6_TCP) ? 1 : 0;
	rss_type.udp_ipv4 = (rss_hf & ETH_RSS_NONFRAG_IPV4_UDP) ? 1 : 0;
	rss_type.udp_ipv6 = (rss_hf & ETH_RSS_NONFRAG_IPV6_UDP) ? 1 : 0;

	err = hinic3_set_rss_type(nic_dev->hwdev, rss_type);
	if (err)
		PMD_DRV_LOG(ERR, "Set RSS type failed");

	return err;
}

/**
 * Get the RSS hash configuration.
 *
 * @param[in] dev
 *   Pointer to ethernet device structure.
 * @param[out] rss_conf
 *   RSS configuration data.
 *
 * @retval zero: Success
 * @retval non-zero: Failure
 */
static int hinic3_rss_conf_get(struct rte_eth_dev *dev,
			       struct rte_eth_rss_conf *rss_conf)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct hinic3_rss_type rss_type = {0};
	int err;

	if (!rss_conf)
		return -EINVAL;

	if (nic_dev->rss_state == HINIC3_RSS_DISABLE &&
	    nic_dev->dcb->dcb_on == 0) {
		rss_conf->rss_hf = 0;
		PMD_DRV_LOG(INFO, "RSS is not enabled");
		return 0;
	}

	if (rss_conf->rss_key &&
	    rss_conf->rss_key_len >= HINIC3_RSS_KEY_SIZE) {
		/*
		 * Get RSS key from driver to reduce the frequency of the MPU
		 * accessing the RSS memory.
		 */
		rss_conf->rss_key_len = sizeof(nic_dev->rss_key);
		memcpy((void *)rss_conf->rss_key, (void *)nic_dev->rss_key,
		       (size_t)rss_conf->rss_key_len);
	}

	err = hinic3_get_rss_type(nic_dev->hwdev, &rss_type);
	if (err)
		return err;

	rss_conf->rss_hf = 0;
	rss_conf->rss_hf |=  rss_type.ipv4 ? (ETH_RSS_IPV4 |
		ETH_RSS_FRAG_IPV4 | ETH_RSS_NONFRAG_IPV4_OTHER) : 0;
	rss_conf->rss_hf |=  rss_type.tcp_ipv4 ? ETH_RSS_NONFRAG_IPV4_TCP : 0;
	rss_conf->rss_hf |=  rss_type.ipv6 ? (ETH_RSS_IPV6 |
		ETH_RSS_FRAG_IPV6 | ETH_RSS_NONFRAG_IPV6_OTHER) : 0;
	rss_conf->rss_hf |=  rss_type.tcp_ipv6 ? ETH_RSS_NONFRAG_IPV6_TCP : 0;
	rss_conf->rss_hf |=  rss_type.udp_ipv4 ? ETH_RSS_NONFRAG_IPV4_UDP : 0;
	rss_conf->rss_hf |=  rss_type.udp_ipv6 ? ETH_RSS_NONFRAG_IPV6_UDP : 0;

	return 0;
}

/**
 * Get the RETA indirection table.
 *
 * @param[in] dev
 *   Pointer to ethernet device structure.
 * @param[out] reta_conf
 *   Pointer to RETA configuration structure array.
 * @param[in] reta_size
 *   Size of the RETA table.
 *
 * @retval zero: Success
 * @retval non-zero: Failure
 */
static int hinic3_rss_reta_query(struct rte_eth_dev *dev,
				 struct rte_eth_rss_reta_entry64 *reta_conf,
				 uint16_t reta_size)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	u32 indirtbl[HINIC3_RSS_INDIR_SIZE] = {0};
	u16 idx, shift;
	u16 i;
	int err;

	if (nic_dev->rss_state == HINIC3_RSS_DISABLE) {
		PMD_DRV_LOG(INFO, "RSS is not enabled");
		return 0;
	}

	if (reta_size != HINIC3_RSS_INDIR_SIZE) {
		PMD_DRV_LOG(ERR, "Invalid reta size, reta_size: %d", reta_size);
		return -EINVAL;
	}

	err = hinic3_rss_get_indir_tbl(nic_dev->hwdev, indirtbl, HINIC3_RSS_INDIR_SIZE);
	if (err) {
		PMD_DRV_LOG(ERR, "Get RSS retas table failed, error: %d",
			    err);
		return err;
	}

	for (i = 0; i < reta_size; i++) {
		idx = i / RTE_RETA_GROUP_SIZE;
		shift = i % RTE_RETA_GROUP_SIZE;
		if (reta_conf[idx].mask & (1ULL << shift))
			reta_conf[idx].reta[shift] = (uint16_t)indirtbl[i];
	}

	return 0;
}

static int hinic3_get_eeprom(__rte_unused struct rte_eth_dev *dev, struct rte_dev_eeprom_info *info)
{
#define MAX_BUF_OUT_LEN 2048

	return hinic3_pmd_mml_lib(info->data, info->offset, info->data,
		&info->length, MAX_BUF_OUT_LEN);
}

/**
 * Update the RETA indirection table.
 *
 * @param[in] dev
 *   Pointer to ethernet device structure.
 * @param[in] reta_conf
 *   Pointer to RETA configuration structure array.
 * @param[in] reta_size
 *   Size of the RETA table.
 *
 * @retval zero: Success
 * @retval non-zero: Failure
 */
static int hinic3_rss_reta_update(struct rte_eth_dev *dev,
				  struct rte_eth_rss_reta_entry64 *reta_conf,
				  uint16_t reta_size)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	u32 indirtbl[HINIC3_RSS_INDIR_SIZE] = {0};
	u16 idx, shift;
	u16 i;
	int err;

	if (nic_dev->rss_state == HINIC3_RSS_DISABLE)
		return 0;

	if (reta_size != HINIC3_RSS_INDIR_SIZE) {
		PMD_DRV_LOG(ERR, "Invalid reta size, reta_size: %d", reta_size);
		return -EINVAL;
	}

	err = hinic3_rss_get_indir_tbl(nic_dev->hwdev, indirtbl, HINIC3_RSS_INDIR_SIZE);
	if (err)
		return err;

	/* Update RSS reta table */
	for (i = 0; i < reta_size; i++) {
		idx = i / RTE_RETA_GROUP_SIZE;
		shift = i % RTE_RETA_GROUP_SIZE;
		if (reta_conf[idx].mask & (1ULL << shift))
			indirtbl[i] = reta_conf[idx].reta[shift];
	}

	for (i = 0 ; i < reta_size; i++) {
		if (indirtbl[i] >= nic_dev->num_rqs) {
			PMD_DRV_LOG(ERR, "Invalid reta entry, index: %d, num_rqs: %d",
				    indirtbl[i], nic_dev->num_rqs);
			return -EFAULT;
		}
	}

	if (IS_QPOOL_MODE())
 		err = hinic3_rss_set_indir_tbl_qpool(nic_dev->hwdev, indirtbl, HINIC3_RSS_INDIR_SIZE);
 	else
 		err = hinic3_rss_set_indir_tbl(nic_dev->hwdev, indirtbl, HINIC3_RSS_INDIR_SIZE);

	if (err)
		PMD_DRV_LOG(ERR, "Set RSS reta table failed");

	return err;
}

/**
 * Get device generic statistics.
 *
 * @param[in] dev
 *   Pointer to ethernet device structure.
 * @param[out] stats
 *   Stats structure output buffer.
 *
 * @retval zero: Success
 * @retval non-zero: Failure
 */
static int
#ifdef DPDK_25_11
hinic3_dev_stats_get(struct rte_eth_dev *dev, struct rte_eth_stats *stats,
		     struct eth_queue_stats *qstats)
#else
hinic3_dev_stats_get(struct rte_eth_dev *dev, struct rte_eth_stats *stats)
#endif
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct hinic3_vport_stats vport_stats;
	struct hinic3_rxq *rxq = NULL;
	struct hinic3_txq *txq = NULL;
	int i, err, q_num;
	u64 rx_discards_pmd = 0;

	err = hinic3_get_vport_stats(nic_dev->hwdev, &vport_stats);
	if (err) {
		PMD_DRV_LOG(ERR, "Get vport stats from fw failed, nic_dev: %s",
			    nic_dev->dev_name);
		return err;
	}

	dev->data->rx_mbuf_alloc_failed = 0;

	/* Rx queue stats */
	q_num = (nic_dev->num_rqs < RTE_ETHDEV_QUEUE_STAT_CNTRS) ?
			nic_dev->num_rqs : RTE_ETHDEV_QUEUE_STAT_CNTRS;
	for (i = 0; i < q_num; i++) {
		rxq = nic_dev->rxqs[i];
		if (rxq == NULL)
			continue;
#ifdef HINIC3_XSTAT_MBUF_USE
		rxq->rxq_stats.left_mbuf = rxq->rxq_stats.alloc_mbuf - rxq->rxq_stats.free_mbuf;
#endif
		rxq->rxq_stats.errors = rxq->rxq_stats.other_errors;

#ifdef DPDK_25_11
		if (qstats) {
			qstats->q_ipackets[i] = rxq->rxq_stats.packets;
			qstats->q_ibytes[i] = rxq->rxq_stats.bytes;
			qstats->q_errors[i] = rxq->rxq_stats.errors;
		}
#else
		stats->q_ipackets[i] = rxq->rxq_stats.packets;
		stats->q_ibytes[i] = rxq->rxq_stats.bytes;
		stats->q_errors[i] = rxq->rxq_stats.errors;
#endif

		stats->ierrors += rxq->rxq_stats.errors;
		rx_discards_pmd += rxq->rxq_stats.dropped;
		dev->data->rx_mbuf_alloc_failed += rxq->rxq_stats.rx_nombuf;
	}

	/* Tx queue stats */
	q_num = (nic_dev->num_sqs < RTE_ETHDEV_QUEUE_STAT_CNTRS) ?
		nic_dev->num_sqs : RTE_ETHDEV_QUEUE_STAT_CNTRS;
	for (i = 0; i < q_num; i++) {
		txq = nic_dev->txqs[i];
		if (txq == NULL)
			continue;
#ifdef DPDK_25_11
		if (qstats) {
			qstats->q_opackets[i] = txq->txq_stats.packets;
			qstats->q_obytes[i] = txq->txq_stats.bytes;
		}
#else
		stats->q_opackets[i] = txq->txq_stats.packets;
		stats->q_obytes[i] = txq->txq_stats.bytes;
#endif
		stats->oerrors += (txq->txq_stats.tx_busy +
				  txq->txq_stats.off_errs);
	}

	if (IS_QPOOL_MODE()) {
		q_num = (nic_dev->num_rqs < HINIC3_QUEUE_STAT_CNTRS) ?
			nic_dev->num_rqs : HINIC3_QUEUE_STAT_CNTRS;

		for (i = 0; i < q_num; i++) {
			rxq = nic_dev->rxqs[i];
			if (rxq == NULL)
				continue;
			stats->ipackets += rxq->rxq_stats.packets;
			stats->ibytes += rxq->rxq_stats.bytes;
			stats->imissed += rxq->rxq_stats.dropped;
		}

		q_num = (nic_dev->num_sqs < HINIC3_QUEUE_STAT_CNTRS) ?
			nic_dev->num_sqs :  HINIC3_QUEUE_STAT_CNTRS;
		for (i = 0; i < q_num; i++) {
			txq = nic_dev->txqs[i];
			if (txq == NULL)
				continue;
			stats->opackets += txq->txq_stats.packets;
			stats->obytes += txq->txq_stats.bytes;
		}

		return 0;
	}

	/* Vport stats */
	stats->oerrors += vport_stats.tx_discard_vport;

	stats->imissed = vport_stats.rx_discard_vport + rx_discards_pmd;

	stats->ipackets = (vport_stats.rx_unicast_pkts_vport +
			  vport_stats.rx_multicast_pkts_vport +
			  vport_stats.rx_broadcast_pkts_vport -
			  rx_discards_pmd);

	stats->opackets = (vport_stats.tx_unicast_pkts_vport +
			  vport_stats.tx_multicast_pkts_vport +
			  vport_stats.tx_broadcast_pkts_vport);

	stats->ibytes = (vport_stats.rx_unicast_bytes_vport +
			vport_stats.rx_multicast_bytes_vport +
			vport_stats.rx_broadcast_bytes_vport);

	stats->obytes = (vport_stats.tx_unicast_bytes_vport +
			vport_stats.tx_multicast_bytes_vport +
			vport_stats.tx_broadcast_bytes_vport);
	return 0;
}

/**
 * Clear device generic statistics.
 *
 * @param[in] dev
 *   Pointer to ethernet device structure.
 *
 * @retval zero: Success
 * @retval non-zero: Failure
 */
static int hinic3_dev_stats_reset(struct rte_eth_dev *dev)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct hinic3_rxq *rxq = NULL;
	struct hinic3_txq *txq = NULL;
	int qid;
	int err;

	err = hinic3_clear_vport_stats(nic_dev->hwdev);
	if (err)
		return err;

	for (qid = 0; qid < nic_dev->num_rqs; qid++) {
		rxq = nic_dev->rxqs[qid];
		if (rxq == NULL)
			continue;
		memset(&rxq->rxq_stats, 0, sizeof(struct hinic3_rxq_stats));
	}

	for (qid = 0; qid < nic_dev->num_sqs; qid++) {
		txq = nic_dev->txqs[qid];
		if (txq == NULL)
			continue;
		memset(&txq->txq_stats, 0, sizeof(struct hinic3_txq_stats));
	}

	return 0;
}

static u16
get_port_cir_drop(struct hinic3_nic_dev *nic_dev,
		  struct rte_eth_xstat *xstats)
{
	struct hinic3_cir_drop port_stats;
	u16 i;
	int err = 0;

	memset(&port_stats, 0, sizeof(port_stats));
	
	if (is_sp620_nic(nic_dev)) {
		err = hinic3_get_cir_drop(nic_dev->hwdev, &port_stats);
		if (err) {
			PMD_DRV_LOG(ERR, "Failed to get CPB cir drops from fw.");

			for (i = 0; i < ARRAY_LEN(hinic3_cir_drop_stats_strings); i++)
				xstats[i].value = 0;

			return ARRAY_LEN(hinic3_cir_drop_stats_strings);
		}
	}

	for (i = 0; i < ARRAY_LEN(hinic3_cir_drop_stats_strings); i++) {
		memcpy(&xstats[i].value,
		       (const char *)&port_stats + hinic3_cir_drop_stats_strings[i].offset,
		       sizeof(u64));
	}

	return ARRAY_LEN(hinic3_cir_drop_stats_strings);
}

/**
 * Get device extended statistics.
 *
 * @param[in] dev
 *   Pointer to ethernet device structure.
 * @param[out] xstats
 *   Pointer to rte extended stats table.
 * @param[in] n
 *   The size of the stats table.
 *
 * @retval positive: Number of extended stats on success and stats is filled
 * @retval negative: Failure
 */
static int hinic3_dev_xstats_get(struct rte_eth_dev *dev,
				 struct rte_eth_xstat *xstats, unsigned int n)
{
	struct hinic3_nic_dev *nic_dev;
	struct mag_phy_port_stats port_stats;
	struct hinic3_vport_stats vport_stats;
	struct hinic3_rxq *rxq = NULL;
	struct hinic3_rxq_stats rxq_stats;
	struct hinic3_txq *txq = NULL;
	struct hinic3_txq_stats txq_stats;
	u16 qid;
	u32 i;
	int err, count;

	nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	count = hinic3_xstats_calc_num(nic_dev);
	if ((int)n < count)
		return count;

	count = 0;

	/* Get stats from rxq stats structure */
	for (qid = 0; qid < nic_dev->num_rqs; qid++) {
		rxq = nic_dev->rxqs[qid];
		if (rxq == NULL)
			continue;

#ifdef HINIC3_XSTAT_RXBUF_INFO
		hinic3_get_stats(rxq);
#endif

#ifdef HINIC3_XSTAT_MBUF_USE
		rxq->rxq_stats.left_mbuf = rxq->rxq_stats.alloc_mbuf - rxq->rxq_stats.free_mbuf;
#endif
		rxq->rxq_stats.errors = rxq->rxq_stats.csum_errors + rxq->rxq_stats.other_errors;

		memcpy((void *)&rxq_stats, (void *)&rxq->rxq_stats,
		       sizeof(rxq->rxq_stats));

		for (i = 0; i < HINIC3_RXQ_XSTATS_NUM; i++) {
			xstats[count].value =
				*(uint64_t *)(((char *)&rxq_stats) +
				hinic3_rxq_stats_strings[i].offset);
			xstats[count].id = count;
			count++;
		}
	}

	/* Get stats from txq stats structure */
	for (qid = 0; qid < nic_dev->num_sqs; qid++) {
		txq = nic_dev->txqs[qid];
		if (txq == NULL)
			continue;
		memcpy((void *)&txq_stats, (void *)&txq->txq_stats,
		       sizeof(txq->txq_stats));

		for (i = 0; i < HINIC3_TXQ_XSTATS_NUM; i++) {
			xstats[count].value =
				*(uint64_t *)(((char *)&txq_stats) +
				hinic3_txq_stats_strings[i].offset);
			xstats[count].id = count;
			count++;
		}
	}

	/* Get stats from vport stats structure */
	err = hinic3_get_vport_stats(nic_dev->hwdev, &vport_stats);
	if (err)
		return err;

	for (i = 0; i < HINIC3_VPORT_XSTATS_NUM; i++) {
		xstats[count].value =
			*(uint64_t *)(((char *)&vport_stats) +
			hinic3_vport_stats_strings[i].offset);
		xstats[count].id = count;
		count++;
	}

	/* Get stats from phy CPB stats structure */
	count += get_port_cir_drop(nic_dev, &xstats[count]);

	if (HINIC3_IS_VF(nic_dev->hwdev))
		return count;

	/* Get stats from phy port stats structure */
	err = hinic3_get_phy_port_stats(nic_dev->hwdev, &port_stats);
	if (err)
		return err;

	for (i = 0; i < HINIC3_PHYPORT_XSTATS_NUM; i++) {
		xstats[count].value = *(uint64_t *)(((char *)&port_stats) +
				hinic3_phyport_stats_strings[i].offset);
		xstats[count].id = count;
		count++;
	}

	return count;
}

/**
 * Clear device extended statistics.
 *
 * @param[in] dev
 *   Pointer to ethernet device structure.
 *
 * @retval zero: Success
 * @retval non-zero: Failure
 */
static int hinic3_dev_xstats_reset(struct rte_eth_dev *dev)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	int err;

	err = hinic3_dev_stats_reset(dev);
	if (err)
		return err;

	if (hinic3_func_type(nic_dev->hwdev) != TYPE_VF) {
		err = hinic3_clear_phy_port_stats(nic_dev->hwdev);
		if (err)
			return err;
	}

	return 0;
}

/**
 * Retrieve names of extended device statistics
 *
 * @param[in] dev
 *   Pointer to ethernet device structure.
 * @param[out] xstats_names
 *   Buffer to insert names into.
 *
 * @return
 *   Number of xstats names.
 */
static int hinic3_dev_xstats_get_names(struct rte_eth_dev *dev,
				       struct rte_eth_xstat_name *xstats_names,
				       __rte_unused unsigned int limit)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	int count = 0;
	u16 i, q_num;

	if (xstats_names == NULL)
		return hinic3_xstats_calc_num(nic_dev);

	/* Get pmd rxq stats name */
	for (q_num = 0; q_num < nic_dev->num_rqs; q_num++) {
		for (i = 0; i < HINIC3_RXQ_XSTATS_NUM; i++) {
			snprintf(xstats_names[count].name,
				 sizeof(xstats_names[count].name),
				 "rxq%d_%s_pmd", q_num,
				 hinic3_rxq_stats_strings[i].name);
			count++;
		}
	}

	/* Get pmd txq stats name */
	for (q_num = 0; q_num < nic_dev->num_sqs; q_num++) {
		for (i = 0; i < HINIC3_TXQ_XSTATS_NUM; i++) {
			snprintf(xstats_names[count].name,
				 sizeof(xstats_names[count].name),
				 "txq%d_%s_pmd", q_num,
				 hinic3_txq_stats_strings[i].name);
			count++;
		}
	}

	/* Get vport stats name */
	for (i = 0; i < HINIC3_VPORT_XSTATS_NUM; i++) {
		snprintf(xstats_names[count].name,
			 sizeof(xstats_names[count].name),
			 "%s", hinic3_vport_stats_strings[i].name);
		count++;
	}

	/* Get phy CPB stats name */
	for (i = 0; i < HINIC3_CIR_DROP_XSTATS_NUM; i++) {
		snprintf(xstats_names[count].name,
			 sizeof(xstats_names[count].name),
			 "%s", hinic3_cir_drop_stats_strings[i].name);
		count++;
	}

	if (HINIC3_IS_VF(nic_dev->hwdev))
		return count;

	/* Get phy port stats name */
	for (i = 0; i < HINIC3_PHYPORT_XSTATS_NUM; i++) {
		snprintf(xstats_names[count].name,
			 sizeof(xstats_names[count].name),
			 "%s", hinic3_phyport_stats_strings[i].name);
		count++;
	}

	return count;
}
#ifdef DPDK_24_11
static const uint32_t *
hinic3_dev_supported_ptypes_get(__rte_unused struct rte_eth_dev *dev, size_t *no_of_elements)
#else
static const uint32_t *
hinic3_dev_supported_ptypes_get(__rte_unused struct rte_eth_dev *dev)
#endif
{
	static const uint32_t ptypes[] = {
		RTE_PTYPE_L3_IPV4_EXT_UNKNOWN,
		RTE_PTYPE_L3_IPV6_EXT_UNKNOWN,
		RTE_PTYPE_L4_TCP,
		RTE_PTYPE_L4_UDP,
		RTE_PTYPE_L4_SCTP,
		RTE_PTYPE_L4_NONFRAG,
		RTE_PTYPE_TUNNEL_IP,
		RTE_PTYPE_TUNNEL_GRE,
		RTE_PTYPE_TUNNEL_VXLAN,
		RTE_PTYPE_TUNNEL_VXLAN_GPE,
		RTE_PTYPE_TUNNEL_GENEVE,
		RTE_PTYPE_INNER_L3_IPV4_EXT_UNKNOWN,
		RTE_PTYPE_INNER_L3_IPV6_EXT_UNKNOWN,
		RTE_PTYPE_INNER_L4_TCP,
		RTE_PTYPE_INNER_L4_UDP,
		RTE_PTYPE_INNER_L4_SCTP,
		RTE_PTYPE_INNER_L4_NONFRAG,
	};
#ifdef DPDK_24_11
	*no_of_elements = RTE_DIM(ptypes);
#endif
	return ptypes;
}

static void hinic3_rxq_info_get(struct rte_eth_dev *dev, uint16_t queue_id,
				struct rte_eth_rxq_info *rxq_info)
{
	struct hinic3_rxq *rxq = dev->data->rx_queues[queue_id];

	rxq_info->mp = rxq->mb_pool;
	rxq_info->nb_desc = rxq->q_depth;
	rxq_info->scattered_rx = dev->data->scattered_rx;
}

static void hinic3_txq_info_get(struct rte_eth_dev *dev, uint16_t queue_id,
				struct rte_eth_txq_info *txq_qinfo)
{
	struct hinic3_txq *txq = dev->data->tx_queues[queue_id];

	txq_qinfo->nb_desc = txq->q_depth;
}

/**
 * Update MAC address
 *
 * @param[in] dev
 *   Pointer to ethernet device structure.
 * @param[in] addr
 *   Pointer to MAC address
 *
 * @retval zero: Success
 * @retval non-zero: Failure
 */
static int hinic3_set_mac_addr(struct rte_eth_dev *dev,
			       struct rte_ether_addr *addr)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	char mac_addr[RTE_ETHER_ADDR_FMT_SIZE];
	u16 func_id;
	int err;

	if (IS_QPOOL_MODE()) {
		PMD_DRV_LOG(WARNING, "Qpool mode not support set mac addr.");
		return 0;
	}

	if (IS_BIFUR_MODE()) {
		if (hinic3_bifur_is_shared_dev(nic_dev->hwdev->pci_dev)) {
			PMD_DRV_LOG(INFO, "The current mode not support set mac.");
			return -EPERM;
		}
	}

	if (!rte_is_valid_assigned_ether_addr(addr)) {
		rte_ether_format_addr(mac_addr, RTE_ETHER_ADDR_FMT_SIZE, addr);
		PMD_DRV_LOG(ERR, "Set invalid MAC address %s", mac_addr);
		return -EINVAL;
	}

	func_id = hinic3_global_func_id(nic_dev->hwdev);
	err = hinic3_update_mac(nic_dev->hwdev,
				nic_dev->default_addr.addr_bytes,
				addr->addr_bytes, 0, func_id);
	if (err)
		return err;

	rte_ether_addr_copy(addr, &nic_dev->default_addr);
	rte_ether_format_addr(mac_addr, RTE_ETHER_ADDR_FMT_SIZE,
			      &nic_dev->default_addr);

	PMD_DRV_LOG(INFO, "Set new MAC address %s", mac_addr);
	return 0;
}

/**
 * Remove a MAC address.
 *
 * @param[in] dev
 *   Pointer to ethernet device structure.
 * @param[in] index
 *   MAC address index.
 */
static void hinic3_mac_addr_remove(struct rte_eth_dev *dev, uint32_t index)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	u16 func_id;
	int err;

	if (IS_QPOOL_MODE()) {
 	 	PMD_DRV_LOG(WARNING, "Qpool mode not support remove mac addr.");
 	 	return;
 	}

	if (index >= HINIC3_MAX_UC_MAC_ADDRS) {
		PMD_DRV_LOG(INFO, "Remove MAC index(%u) is out of range",
			    index);
		return;
	}

	func_id = hinic3_global_func_id(nic_dev->hwdev);
	err = hinic3_del_mac(nic_dev->hwdev,
			     dev->data->mac_addrs[index].addr_bytes,
			     0, func_id);
	if (err)
		PMD_DRV_LOG(ERR, "Remove MAC index(%u) failed", index);
}

/**
 * Add a MAC address.
 *
 * @param[in] dev
 *   Pointer to ethernet device structure.
 * @param[in] mac_addr
 *   MAC address to register.
 * @param[in] index
 *   MAC address index.
 * @param[in] vmdq
 *   VMDq pool index to associate address with (unused_).
 *
 * @retval zero: Success
 * @retval non-zero: Failure
 */
static int hinic3_mac_addr_add(struct rte_eth_dev *dev,
			       struct rte_ether_addr *mac_addr, uint32_t index,
			       __rte_unused uint32_t vmdq)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	unsigned int i;
	u16 func_id;
	int err;

	if (IS_QPOOL_MODE()) {
 	 	PMD_DRV_LOG(WARNING, "Qpool mode not support add mac addr.");
 	 	return -EINVAL;
 	}

	if (!rte_is_valid_assigned_ether_addr(mac_addr)) {
		PMD_DRV_LOG(ERR, "Add invalid MAC address");
		return -EINVAL;
	}

	if (index >= HINIC3_MAX_UC_MAC_ADDRS) {
		PMD_DRV_LOG(ERR, "Add MAC index(%u) is out of range", index);
		return -EINVAL;
	}

	/* Make sure this address doesn't already be configured */
	for (i = 0; i < HINIC3_MAX_UC_MAC_ADDRS; i++) {
		if (rte_is_same_ether_addr(mac_addr,
				&dev->data->mac_addrs[i])) {
			PMD_DRV_LOG(ERR, "MAC address is already configured");
			return -EADDRINUSE;
		}
	}

	func_id = hinic3_global_func_id(nic_dev->hwdev);
	err = hinic3_set_mac(nic_dev->hwdev, mac_addr->addr_bytes, 0, func_id);
	if (err)
		return err;

	return 0;
}

static void hinic3_delete_mc_addr_list(struct hinic3_nic_dev *nic_dev)
{
	u16 func_id;
	u32 i;

	if (IS_QPOOL_MODE()) {
 	 	PMD_DRV_LOG(WARNING, "Qpool mode not support set mac addr list.");
 	 	return;
 	}

	func_id = hinic3_global_func_id(nic_dev->hwdev);

	for (i = 0; i < HINIC3_MAX_MC_MAC_ADDRS; i++) {
		if (rte_is_zero_ether_addr(&nic_dev->mc_list[i]))
			break;

		hinic3_del_mac(nic_dev->hwdev, nic_dev->mc_list[i].addr_bytes,
			       0, func_id);
		memset(&nic_dev->mc_list[i], 0, sizeof(struct rte_ether_addr));
	}
}

/**
 * Set multicast MAC address
 *
 * @param[in] dev
 *   Pointer to ethernet device structure.
 * @param[in] mc_addr_set
 *   Pointer to multicast MAC address
 * @param[in] nb_mc_addr
 *   The number of multicast MAC address to set
 *
 * @retval zero: Success
 * @retval non-zero: Failure
 */
static int hinic3_set_mc_addr_list(struct rte_eth_dev *dev,
				   struct rte_ether_addr *mc_addr_set,
				   uint32_t nb_mc_addr)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	char mac_addr[RTE_ETHER_ADDR_FMT_SIZE];
	u16 func_id;
	int err;
	u32 i;

	func_id = hinic3_global_func_id(nic_dev->hwdev);

	/* Delete old multi_cast addrs firstly */
	hinic3_delete_mc_addr_list(nic_dev);

	if (nb_mc_addr > HINIC3_MAX_MC_MAC_ADDRS)
		return -EINVAL;

	for (i = 0; i < nb_mc_addr; i++) {
		if (!rte_is_multicast_ether_addr(&mc_addr_set[i])) {
			rte_ether_format_addr(mac_addr, RTE_ETHER_ADDR_FMT_SIZE,
					      &mc_addr_set[i]);
			PMD_DRV_LOG(ERR, "Set mc MAC addr failed, addr(%s) invalid",
				    mac_addr);
			return -EINVAL;
		}
	}

	for (i = 0; i < nb_mc_addr; i++) {
		err = hinic3_set_mac(nic_dev->hwdev, mc_addr_set[i].addr_bytes,
				     0, func_id);
		if (err) {
			hinic3_delete_mc_addr_list(nic_dev);
			return err;
		}

		rte_ether_addr_copy(&mc_addr_set[i], &nic_dev->mc_list[i]);
	}

	return 0;
}

/**
 * Manage flow director filter operations
 *
 * @param[in] dev
 *   Pointer to ethernet device structure.
 * @param[in] filter_type
 *   Filter type.
 * @param[in] filter_op
 *   Operation to perform.
 * @param[in] arg
 *   Pointer to operation-specific structure.
 *
 * @retval zero: Success
 * @retval non-zero: Failure
 */
#ifdef DPDK_21_11
static int hinic3_dev_filter_ctrl(struct rte_eth_dev *dev, const struct rte_flow_ops **arg)
{
	RTE_SET_USED(dev);
	*arg = &hinic3_flow_ops;
	return 0;
}
#else
static int hinic3_dev_filter_ctrl(struct rte_eth_dev *dev,
				  enum rte_filter_type filter_type,
				  enum rte_filter_op filter_op,
				  void *arg)
{
	RTE_SET_USED(dev);
	switch (filter_type) {
	case RTE_ETH_FILTER_GENERIC:
		if (filter_op != RTE_ETH_FILTER_GET)
			return -EINVAL;
		*(const void **)arg = &hinic3_flow_ops;
		break;
	default:
		PMD_DRV_LOG(INFO, "Filter type (%d) not supported",
			filter_type);
		return -EINVAL;
	}
	return 0;
}
#endif
static int hinic3_get_reg(__rte_unused struct rte_eth_dev *dev,
			  __rte_unused struct rte_dev_reg_info *regs)
{
	return 0;
}

#ifdef DPDK_20_11
static bool hinic3_fec_param_valid(uint32_t fec_param)
{
	if ((fec_param == HINIC3_FEC_MODE_LLRS)  ||
	    (fec_param == HINIC3_FEC_MODE_RS)    ||
	    (fec_param == HINIC3_FEC_MODE_BASER) ||
	    (fec_param == HINIC3_FEC_MODE_OFF)) {
		return true;
	}

	return false;
}

static int hinic3_fec_set(struct rte_eth_dev *dev, uint32_t fec_capa)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	int err;

	if (hinic3_fec_param_valid(fec_capa) == false) {
		PMD_DRV_LOG(ERR, "Fec param is valid, failed to set fec param.");
		return -EINVAL;
	}

	err = hinic3_set_fec_mode(nic_dev->hwdev, (u8)fec_capa);
	if (err) {
		PMD_DRV_LOG(ERR, "Set fec param failed: %d.", err);
		return err;
	}

	nic_dev->fec_mode = fec_capa;

	return 0;
}

static int hinic3_fec_get(struct rte_eth_dev *dev, uint32_t *fec_capa)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	u8 advertised_fec = 0;
	int err;

	err = hinic3_get_fec_mode(nic_dev->hwdev, &advertised_fec, 0);
	if (err) {
		PMD_DRV_LOG(ERR, "Get fec parma failed: %d.", err);
		return err;
	}

	*fec_capa = (u32)advertised_fec;

	return 0;
}

static int hinic3_fec_capability_get(struct rte_eth_dev *dev,
			struct rte_eth_fec_capa *speed_fec_capa,
			unsigned int num)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	u8 supported_fec = 0;
	int err;

	if (speed_fec_capa == NULL)
		return HINIC3_FEC_CAPA_NUM_PER_SPEED;

	if (num < HINIC3_FEC_CAPA_NUM_PER_SPEED) {
		PMD_DRV_LOG(ERR, "Not enough array size(%u) to store FEC capabilities, should not be less than %u.",
			    num, HINIC3_FEC_CAPA_NUM_PER_SPEED);
		return -EINVAL;
	}

	err = hinic3_get_fec_mode(nic_dev->hwdev, 0, &supported_fec);
	if (err) {
		PMD_DRV_LOG(ERR, "Failed to get fec capability, err: %d.", err);
		return err;
	}

	speed_fec_capa->speed = nic_dev->hwdev->speed;
	speed_fec_capa->capa = (u32)supported_fec;

	if (speed_fec_capa->speed == ETH_SPEED_NUM_NONE ||
		speed_fec_capa->capa == 0)
		return -ENOTSUP;

	return HINIC3_FEC_CAPA_NUM_PER_SPEED;
}

#endif

static const struct eth_dev_ops hinic3_pmd_ops = {
	.dev_configure                 = hinic3_dev_configure,
	.dev_infos_get                 = hinic3_dev_infos_get,
	.dev_supported_ptypes_get      = hinic3_dev_supported_ptypes_get,
	.fw_version_get                = hinic3_fw_version_get,
	.dev_set_link_up               = hinic3_dev_set_link_up,
	.dev_set_link_down             = hinic3_dev_set_link_down,
	.link_update                   = hinic3_link_update,
	.rx_queue_setup                = hinic3_rx_queue_setup,
	.tx_queue_setup                = hinic3_tx_queue_setup,
	.rx_queue_release              = hinic3_rx_queue_release,
	.tx_queue_release              = hinic3_tx_queue_release,
	.rx_queue_start                = hinic3_dev_rx_queue_start,
	.rx_queue_stop                 = hinic3_dev_rx_queue_stop,
	.tx_queue_start                = hinic3_dev_tx_queue_start,
	.tx_queue_stop                 = hinic3_dev_tx_queue_stop,
	.rx_queue_intr_enable          = hinic3_dev_rx_queue_intr_enable,
	.rx_queue_intr_disable         = hinic3_dev_rx_queue_intr_disable,
#ifndef DPDK_20_11
	.rx_queue_count                = hinic3_dev_rx_queue_count,
#ifndef DPDK_21_11
	.rx_descriptor_done            = hinic3_dev_rx_descriptor_done,
#else
	.rx_descriptor_status          = hinic3_dev_rx_descriptor_done,
#endif
	.rx_descriptor_status          = hinic3_dev_rx_descriptor_status,
	.tx_descriptor_status          = hinic3_dev_tx_descriptor_status,
#endif
	.dev_start                     = hinic3_dev_start,
	.dev_stop                      = hinic3_dev_stop,
	.dev_close                     = hinic3_dev_close,
	.dev_reset                     = hinic3_dev_reset,
	.mtu_set                       = hinic3_dev_set_mtu,
	.vlan_filter_set               = hinic3_vlan_filter_set,
	.vlan_offload_set              = hinic3_vlan_offload_set,
	.allmulticast_enable           = hinic3_dev_allmulticast_enable,
	.allmulticast_disable          = hinic3_dev_allmulticast_disable,
	.promiscuous_enable            = hinic3_dev_promiscuous_enable,
	.promiscuous_disable           = hinic3_dev_promiscuous_disable,
	.flow_ctrl_get                 = hinic3_dev_flow_ctrl_get,
	.flow_ctrl_set                 = hinic3_dev_flow_ctrl_set,
	.rss_hash_update               = hinic3_rss_hash_update,
	.rss_hash_conf_get             = hinic3_rss_conf_get,
	.reta_update                   = hinic3_rss_reta_update,
	.reta_query                    = hinic3_rss_reta_query,
	.get_eeprom                    = hinic3_get_eeprom,
	.stats_get                     = hinic3_dev_stats_get,
	.stats_reset                   = hinic3_dev_stats_reset,
	.xstats_get                    = hinic3_dev_xstats_get,
	.xstats_reset                  = hinic3_dev_xstats_reset,
	.xstats_get_names              = hinic3_dev_xstats_get_names,
	.rxq_info_get                  = hinic3_rxq_info_get,
	.txq_info_get                  = hinic3_txq_info_get,
	.mac_addr_set                  = hinic3_set_mac_addr,
	.mac_addr_remove               = hinic3_mac_addr_remove,
	.mac_addr_add                  = hinic3_mac_addr_add,
	.set_mc_addr_list              = hinic3_set_mc_addr_list,
#ifndef DPDK_21_11
	.filter_ctrl                   = hinic3_dev_filter_ctrl,
#else
	.flow_ops_get                  = hinic3_dev_filter_ctrl,
#endif
	.get_reg                       = hinic3_get_reg,
	.get_dcb_info                  = hinic3_get_dcb_info,
	.tm_ops_get                    = hinic3_tm_ops_get,
	.hairpin_cap_get			   = hinic3_hairpin_cap_get,
#ifdef DPDK_20_11
	.hairpin_get_peer_ports		   = hinic3_hairpin_get_peer_ports,
	.hairpin_bind				   = hinic3_hairpin_bind,
	.hairpin_unbind				   = hinic3_hairpin_unbind,
#endif
	.rx_hairpin_queue_setup		   = hinic3_rx_hairpin_queue_setup,
	.tx_hairpin_queue_setup		   = hinic3_tx_hairpin_queue_setup,
	.tx_burst_mode_get             = hinic3_tx_burst_mode_get,
#ifdef DPDK_20_11
	.fec_get_capability	       = hinic3_fec_capability_get,
	.fec_get                       = hinic3_fec_get,
	.fec_set               	       = hinic3_fec_set,
#endif
};

static const struct eth_dev_ops hinic3_pmd_vf_ops = {
	.dev_configure                 = hinic3_dev_configure,
	.dev_infos_get                 = hinic3_dev_infos_get,
	.dev_supported_ptypes_get      = hinic3_dev_supported_ptypes_get,
	.fw_version_get                = hinic3_fw_version_get,
	.dev_set_link_up               = hinic3_dev_set_link_up,
	.dev_set_link_down             = hinic3_dev_set_link_down,
	.rx_queue_setup                = hinic3_rx_queue_setup,
	.tx_queue_setup                = hinic3_tx_queue_setup,
	.rx_queue_intr_enable          = hinic3_dev_rx_queue_intr_enable,
	.rx_queue_intr_disable         = hinic3_dev_rx_queue_intr_disable,

	.rx_queue_start                = hinic3_dev_rx_queue_start,
	.rx_queue_stop                 = hinic3_dev_rx_queue_stop,
	.tx_queue_start                = hinic3_dev_tx_queue_start,
	.tx_queue_stop                 = hinic3_dev_tx_queue_stop,

	.dev_start                     = hinic3_dev_start,
	.link_update                   = hinic3_link_update,
	.rx_queue_release              = hinic3_rx_queue_release,
	.tx_queue_release              = hinic3_tx_queue_release,
	.dev_stop                      = hinic3_dev_stop,
	.dev_close                     = hinic3_dev_close,
	.mtu_set                       = hinic3_dev_set_mtu,
	.vlan_filter_set               = hinic3_vlan_filter_set,
	.vlan_offload_set              = hinic3_vlan_offload_set,
	.allmulticast_enable           = hinic3_dev_allmulticast_enable,
	.allmulticast_disable          = hinic3_dev_allmulticast_disable,
	.promiscuous_enable            = hinic3_dev_promiscuous_enable,
	.promiscuous_disable           = hinic3_dev_promiscuous_disable,
	.rss_hash_update               = hinic3_rss_hash_update,
	.rss_hash_conf_get             = hinic3_rss_conf_get,
	.reta_update                   = hinic3_rss_reta_update,
	.reta_query                    = hinic3_rss_reta_query,
	.get_eeprom                    = hinic3_get_eeprom,
	.stats_get                     = hinic3_dev_stats_get,
	.stats_reset                   = hinic3_dev_stats_reset,
	.xstats_get                    = hinic3_dev_xstats_get,
	.xstats_reset                  = hinic3_dev_xstats_reset,
	.xstats_get_names              = hinic3_dev_xstats_get_names,
	.rxq_info_get                  = hinic3_rxq_info_get,
	.txq_info_get                  = hinic3_txq_info_get,
	.mac_addr_set                  = hinic3_set_mac_addr,
	.mac_addr_remove               = hinic3_mac_addr_remove,
	.mac_addr_add                  = hinic3_mac_addr_add,
	.set_mc_addr_list              = hinic3_set_mc_addr_list,
#ifndef DPDK_21_11
	.filter_ctrl                   = hinic3_dev_filter_ctrl,
#else
	.flow_ops_get                  = hinic3_dev_filter_ctrl,
#endif
	.get_dcb_info                  = hinic3_get_dcb_info,
	.tm_ops_get                    = hinic3_tm_ops_get,
	.hairpin_cap_get			   = hinic3_hairpin_cap_get,
#ifdef DPDK_20_11
	.hairpin_get_peer_ports		   = hinic3_hairpin_get_peer_ports,
	.hairpin_bind				   = hinic3_hairpin_bind,
	.hairpin_unbind				   = hinic3_hairpin_unbind,
#endif
	.rx_hairpin_queue_setup		   = hinic3_rx_hairpin_queue_setup,
	.tx_hairpin_queue_setup		   = hinic3_tx_hairpin_queue_setup,
	.tx_burst_mode_get             = hinic3_tx_burst_mode_get,
};

/**
 * Init mac_vlan table in hardwares.
 *
 * @param[in] eth_dev
 *   Pointer to ethernet device structure.
 *
 * @retval zero: Success
 * @retval non-zero: Failure
 */
static int hinic3_init_mac_table(struct rte_eth_dev *eth_dev)
{
	struct hinic3_nic_dev *nic_dev =
				HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(eth_dev);
	u8 addr_bytes[RTE_ETHER_ADDR_LEN];
	u16 func_id = 0;
	int err = 0;

	err = hinic3_get_default_mac(nic_dev->hwdev, addr_bytes,
				     RTE_ETHER_ADDR_LEN);
	if (err)
		return err;

	rte_ether_addr_copy((struct rte_ether_addr *)addr_bytes,
			    &eth_dev->data->mac_addrs[0]);
	if (rte_is_zero_ether_addr(&eth_dev->data->mac_addrs[0]) ||
		rte_is_broadcast_ether_addr(&eth_dev->data->mac_addrs[0]))
		rte_eth_random_addr(eth_dev->data->mac_addrs[0].addr_bytes);

	func_id = hinic3_global_func_id(nic_dev->hwdev);
	err = hinic3_set_mac(nic_dev->hwdev,
			     eth_dev->data->mac_addrs[0].addr_bytes,
			     0, func_id);
	if (err && err != HINIC3_PF_SET_VF_ALREADY)
		return err;

	rte_ether_addr_copy(&eth_dev->data->mac_addrs[0],
			    &nic_dev->default_addr);

	return 0;
}

/**
 * Deinit mac_vlan table in hardware.
 *
 * @param[in] eth_dev
 *   Pointer to ethernet device structure.
 */
static void hinic3_deinit_mac_addr(struct rte_eth_dev *eth_dev)
{
	struct hinic3_nic_dev *nic_dev =
				HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(eth_dev);
	u16 func_id = 0;
	int err;
	int i;

	func_id = hinic3_global_func_id(nic_dev->hwdev);

	for (i = 0; i < HINIC3_MAX_UC_MAC_ADDRS; i++) {
		if (rte_is_zero_ether_addr(&eth_dev->data->mac_addrs[i]))
			continue;

		err = hinic3_del_mac(nic_dev->hwdev,
				     eth_dev->data->mac_addrs[i].addr_bytes,
				     0, func_id);
		if (err && err != HINIC3_PF_SET_VF_ALREADY)
			PMD_DRV_LOG(ERR, "Delete mac table failed, dev_name: %s",
				    eth_dev->data->name);

		memset(&eth_dev->data->mac_addrs[i], 0,
		       sizeof(struct rte_ether_addr));
	}

	/* Delete multicast mac addrs */
	hinic3_delete_mc_addr_list(nic_dev);
}

static int hinic3_pf_get_default_cos(struct hinic3_hwdev *hwdev, u8 *cos_id)
{
	u8 default_cos = 0;
	u8 valid_cos_bitmap;
	u8 i;

	valid_cos_bitmap = hwdev->cfg_mgmt->svc_cap.cos_valid_bitmap;
	if (!valid_cos_bitmap) {
		PMD_DRV_LOG(ERR, "PF has none cos to support\n");
		return -EFAULT;
	}

	for (i = 0; i < HINIC3_COS_NUM_MAX; i++) {
		if (valid_cos_bitmap & BIT(i))
			 /* Find max cos id as default cos */
			default_cos = i;
	}

	*cos_id = default_cos;

	return 0;
}

static int hinic3_init_default_cos(struct hinic3_nic_dev *nic_dev)
{
	u8 cos_id = 0;
	int err;

	if (!HINIC3_IS_VF(nic_dev->hwdev)) {
		err = hinic3_pf_get_default_cos(nic_dev->hwdev, &cos_id);
		if (err) {
			PMD_DRV_LOG(ERR, "Get PF default cos failed, err: %d",
				    err);
			return err;
		}
	} else {
		err = hinic3_vf_get_default_cos(nic_dev->hwdev, &cos_id);
		if (err) {
			PMD_DRV_LOG(ERR, "Get VF default cos failed, err: %d",
				    err);
			return err;
		}
	}

	nic_dev->default_cos = cos_id;
	PMD_DRV_LOG(INFO, "Default cos %d", nic_dev->default_cos);
	return 0;
}

static int hinic3_set_default_hw_feature(struct hinic3_nic_dev *nic_dev)
{
	int err;

	err = hinic3_init_default_cos(nic_dev);
	if (err)
		return err;

	if (hinic3_func_type(nic_dev->hwdev) == TYPE_VF)
        return 0;

	err = hinic3_set_link_status_follow(nic_dev->hwdev,
					   HINIC3_LINK_FOLLOW_PORT);
	if (err == HINIC3_MGMT_CMD_UNSUPPORTED)
		PMD_DRV_LOG(WARNING, "Don't support to set link status follow phy port status");
	else if (err)
		return err;

	return 0;
}

static int hinic3_parse_version(const char *str, uint8_t *ver) {
    if (str == NULL || ver == NULL)
		return -EINVAL;

    uint8_t idx = 0;
    uint8_t val = 0;
    uint8_t char_cnt = 0;

    for (const char *p = str; *p; ++p) {
        if (++char_cnt > HINIC3_FW_VERSION_LEN)
			return -EINVAL;

        unsigned char c = *p;
        if (c >= '0' && c <= '9')
            val = val * 10 + (c - '0');
        else if (c == '.') {
			if (idx >= HINIC3_FW_VERSION_NUM - 1)
				return -EINVAL;
			ver[idx++] = val;
			val = 0;
        } else
            return -EINVAL;
    }
	if (idx != HINIC3_FW_VERSION_NUM - 1)
		return -EINVAL;

	ver[idx++] = val;
    return 0;
}

static int hinic3_check_fw_version(struct rte_eth_dev *eth_dev)
{
	char fw_version[HINIC3_FW_VERSION_LEN];
	int fw_size = HINIC3_FW_VERSION_LEN;
	uint8_t version[HINIC3_FW_VERSION_NUM];
	int ret = hinic3_fw_version_get(eth_dev, fw_version, fw_size);
	if (ret != 0) {
		PMD_DRV_LOG(ERR, "Get firmware version failed, err: %d", ret);
		return ret;
	}
	ret = hinic3_parse_version(fw_version, version);
	if(ret) {
		PMD_DRV_LOG(ERR, "Parse firmware version failed, err: %d", ret);
		return ret;
	}
	if (!strcmp(fw_version, "15.19.2.9")) {
		PMD_DRV_LOG(ERR, "Unsupported firmware version:%s", fw_version);
		return -ENOTSUP;
	}
	return 0;
}

static int hinic3_func_init(struct rte_eth_dev *eth_dev)
{
	struct hinic3_tcam_info *tcam_info = NULL;
	struct hinic3_nic_dev *nic_dev = NULL;
	struct rte_pci_device *pci_dev = NULL;
	unsigned long compact_cqe = 0;
	int err;

	pci_dev = RTE_ETH_DEV_TO_PCI(eth_dev);

	/* EAL is secondary and eth_dev is already created */
	if (rte_eal_process_type() != RTE_PROC_PRIMARY) {
		PMD_DRV_LOG(INFO, "Initialize %s in secondary process",
			    eth_dev->data->name);

		char name[RTE_ETH_NAME_MAX_LEN];
		snprintf(name, sizeof(name), "%s", eth_dev->data->name);
		eth_dev = rte_eth_dev_attach_secondary(name);
		if (eth_dev == NULL) {
			PMD_DRV_LOG(ERR, "can not attach rte ethdev, dev_name: %s", name);
			return -ENOMEM;
		}

		nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(eth_dev);
		if (nic_dev == NULL) {
			PMD_DRV_LOG(ERR, "nic_dev hwdev is NULL, dev_name: %s", name);
			return -ENOMEM;
		}

		if (HINIC3_FUNC_TYPE(nic_dev->hwdev) == TYPE_VF) {
			eth_dev->dev_ops = &hinic3_pmd_vf_ops;
		} else {
			eth_dev->dev_ops = &hinic3_pmd_ops;
		}
		return 0;
	}

	nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(eth_dev);
	memset(nic_dev, 0, sizeof(*nic_dev));

	if (IS_BIFUR_MODE()) {
		if (hinic3_bifur_is_shared_dev(pci_dev)) {
			PMD_DRV_LOG(INFO, "It`s a shared vf for flow bifurcation");
			/* HINIC3_FUNC_EXCLUSIVE is default value. */
			nic_dev->hinic3_function_mode = HINIC3_FUNC_SHARED;
		}
	}

	nic_dev->id_table = pci_id_hinic3_map;

	(void)snprintf(nic_dev->dev_name, sizeof(nic_dev->dev_name),
		 "dbdf-%.4x:%.2x:%.2x.%x",
		 pci_dev->addr.domain, pci_dev->addr.bus,
		 pci_dev->addr.devid, pci_dev->addr.function);

	/* Alloc mac_addrs */
	eth_dev->data->mac_addrs = rte_zmalloc("hinic3_mac",
		HINIC3_MAX_UC_MAC_ADDRS * sizeof(struct rte_ether_addr), 0);
	if (!eth_dev->data->mac_addrs) {
		PMD_DRV_LOG(ERR, "Allocate %zx bytes to store MAC addresses "
			    "failed, dev_name: %s",
			    HINIC3_MAX_UC_MAC_ADDRS *
			    sizeof(struct rte_ether_addr),
			    eth_dev->data->name);
		err = -ENOMEM;
		goto alloc_eth_addr_fail;
	}

	nic_dev->mc_list = rte_zmalloc("hinic3_mc",
		HINIC3_MAX_MC_MAC_ADDRS * sizeof(struct rte_ether_addr), 0);
	if (!nic_dev->mc_list) {
		PMD_DRV_LOG(ERR, "Allocate %zx bytes to store multicast "
			    "addresses failed, dev_name: %s",
			    HINIC3_MAX_MC_MAC_ADDRS *
			    sizeof(struct rte_ether_addr),
			    eth_dev->data->name);
		err = -ENOMEM;
		goto alloc_mc_list_fail;
	}

#ifndef DPDK_20_11
	/*
	 * Pass the information to the rte_eth_dev_close() that it should also
	 * release the private port resources.
	 */
	eth_dev->data->dev_flags |= RTE_ETH_DEV_CLOSE_REMOVE;
#endif

	/* Create hardware device */
	nic_dev->hwdev = rte_zmalloc("hinic3_hwdev", sizeof(*(nic_dev->hwdev)),
				     RTE_CACHE_LINE_SIZE);
	if (!nic_dev->hwdev) {
		PMD_DRV_LOG(ERR, "Allocate hwdev memory failed, dev_name: %s",
			    eth_dev->data->name);
		err = -ENOMEM;
		goto alloc_hwdev_mem_fail;
	}

	nic_dev->hwdev->pci_dev = pci_dev;
	nic_dev->hwdev->dev_handle = nic_dev;
	nic_dev->hwdev->eth_dev = eth_dev;
	nic_dev->hwdev->port_id = eth_dev->data->port_id;

	err = hinic3_init_hwdev(nic_dev->hwdev);
	if (err) {
		PMD_DRV_LOG(ERR, "Init chip hwdev failed, dev_name: %s",
			    eth_dev->data->name);
		goto init_hwdev_fail;
	}

	nic_dev->max_sqs = hinic3_func_max_sqs(nic_dev->hwdev);
	nic_dev->max_rqs = hinic3_func_max_rqs(nic_dev->hwdev);

	if (HINIC3_FUNC_TYPE(nic_dev->hwdev) == TYPE_VF)

		eth_dev->dev_ops = &hinic3_pmd_vf_ops;
	else
		eth_dev->dev_ops = &hinic3_pmd_ops;

	err = hinic3_init_nic_hwdev(nic_dev->hwdev);
	if (err) {
		PMD_DRV_LOG(ERR, "Init nic hwdev failed, dev_name: %s",
			    eth_dev->data->name);
		goto init_nic_hwdev_fail;
	}

	err = hinic3_check_fw_version(eth_dev);
	if (err) {
		PMD_DRV_LOG(ERR, "Check firmware version failed, err: %d", err);
		goto check_fw_version_fail;
	}
	err = hinic3_get_feature_from_hw(nic_dev->hwdev, &nic_dev->feature_cap, 1);
	if (err) {
		PMD_DRV_LOG(ERR, "Get nic feature from hardware failed, dev_name: %s",
			    eth_dev->data->name);
		goto get_cap_fail;
	}

	if (!is_sp620_nic(nic_dev)) {
		if (hinic3_parse_sysfs_value(RQ_WQE_TYPE_PATH, &compact_cqe) != 0) {
			err = -EINVAL;
			goto get_cap_fail;
		}

		if (compact_cqe == 1)
			nic_dev->feature_cap &= ~(NIC_F_RX_SW_COMPACT_CQE | NIC_F_RX_HW_COMPACT_CQE);
	}

	err = hinic3_init_sw_rxtxqs(nic_dev);
	if (err) {
		PMD_DRV_LOG(ERR, "Init sw rxqs or txqs failed, dev_name: %s",
			    eth_dev->data->name);
		goto init_sw_rxtxqs_fail;
	}

	err = hinic3_init_mac_table(eth_dev);
	if (err) {
		PMD_DRV_LOG(ERR, "Init mac table failed, dev_name: %s",
			    eth_dev->data->name);
		goto init_mac_table_fail;
	}

	/* Set hardware feature to default status */
	err = hinic3_set_default_hw_feature(nic_dev);
	if (err) {
		PMD_DRV_LOG(ERR, "Set hw default features failed, dev_name: %s",
			    eth_dev->data->name);
		goto set_default_feature_fail;
	}

	/* Register callback func to eal lib */
	err = rte_intr_callback_register(PCI_DEV_TO_INTR_HANDLE(pci_dev),
					 hinic3_dev_interrupt_handler,
					 (void *)eth_dev);
	if (err) {
		PMD_DRV_LOG(ERR, "Register intr callback failed, dev_name: %s",
			    eth_dev->data->name);
		goto reg_intr_cb_fail;
	}

	/* Enable uio/vfio intr/eventfd mapping */
	err = rte_intr_enable(PCI_DEV_TO_INTR_HANDLE(pci_dev));
	if (err) {
		PMD_DRV_LOG(ERR, "Enable rte interrupt failed, dev_name: %s",
			    eth_dev->data->name);
		goto enable_intr_fail;
	}

	err = hinic3_init_rx_ptype_table(eth_dev);
	if (err)
		PMD_DRV_LOG(ERR, "Failed to create ptype_table.");

	tcam_info = &nic_dev->tcam;
	memset(tcam_info, 0, sizeof(struct hinic3_tcam_info));
	TAILQ_INIT(&tcam_info->tcam_list);
	TAILQ_INIT(&tcam_info->tcam_dynamic_info.tcam_dynamic_list);
	TAILQ_INIT(&nic_dev->filter_ethertype_list);
	TAILQ_INIT(&nic_dev->filter_fdir_rule_list);
	TAILQ_INIT(&nic_dev->rss_template_list);

	err = hinic3_mutex_init_shared(&nic_dev->rx_mode_mutex);
	if (err) {
		PMD_DRV_LOG(ERR, "Mutex init failed.");
		goto mutex_init_fail;
	}

	hinic3_set_bit(HINIC3_DEV_INTR_EN, &nic_dev->dev_status);

	hinic3_set_bit(HINIC3_DEV_INIT, &nic_dev->dev_status);
	PMD_DRV_LOG(INFO, "Initialize %s in primary succeed",
		    eth_dev->data->name);

#ifdef DPDK_20_11
	/**
	 * Queue xstats filled automatically by ethdev layer.
	 */
	eth_dev->data->dev_flags |= RTE_ETH_DEV_AUTOFILL_QUEUE_XSTATS;
#endif
	err = hinic3_dcb_init(nic_dev);
	if (err) {
		PMD_DRV_LOG(ERR, "Failed to init dcb: %d", err);
		goto dcb_init_fail;
	}

	hinic3_tm_conf_init(eth_dev);

	return 0;

dcb_init_fail:
	hinic3_mutex_destroy(&nic_dev->rx_mode_mutex);
mutex_init_fail:
enable_intr_fail:
	(void)rte_intr_callback_unregister(PCI_DEV_TO_INTR_HANDLE(pci_dev),
					   hinic3_dev_interrupt_handler,
					   (void *)eth_dev);

reg_intr_cb_fail:
set_default_feature_fail:
	hinic3_deinit_mac_addr(eth_dev);

init_mac_table_fail:
	hinic3_deinit_sw_rxtxqs(nic_dev);

init_sw_rxtxqs_fail:
get_cap_fail:
check_fw_version_fail:
	hinic3_free_nic_hwdev(nic_dev->hwdev);

init_nic_hwdev_fail:
	hinic3_free_hwdev(nic_dev->hwdev);
	eth_dev->dev_ops = NULL;

init_hwdev_fail:
	rte_free(nic_dev->hwdev);
	nic_dev->hwdev = NULL;

alloc_hwdev_mem_fail:
	rte_free(nic_dev->mc_list);
	nic_dev->mc_list = NULL;

alloc_mc_list_fail:
	rte_free(eth_dev->data->mac_addrs);
	eth_dev->data->mac_addrs = NULL;

alloc_eth_addr_fail:
	PMD_DRV_LOG(ERR, "Initialize %s in primary failed",
		    eth_dev->data->name);
	return err;
}

static int hinic3_get_nic_fd(struct rte_eth_dev *eth_dev)
{
	struct rte_pci_device *pci_dev = RTE_ETH_DEV_TO_PCI(eth_dev);
	char dev_file[PATH_MAX];
	int fd;

	snprintf(dev_file, sizeof(dev_file), "/dev/nic_cdev/" PCI_PRI_FMT, pci_dev->addr.domain,
			pci_dev->addr.bus, pci_dev->addr.devid, pci_dev->addr.function);

	fd = open(dev_file, O_RDWR | O_TRUNC, 777);
	if (fd < 0) {
		PMD_DRV_LOG(ERR, "Open nic_cdev file failed.\n");
		return -1;
	}

	return fd;
}

static int
hinic3_nic_common_args_check_handler(const char *key, const char *val,
				     void *opaque)
{
	struct hinic3_nic_common_dev_config *config = opaque;
	signed long tmp;

	if (val == NULL || *val == '\0') {
		PMD_DRV_LOG(ERR, "Key %s is missing value.", key);
		return -EINVAL;
	}

	errno = 0;
	tmp = strtol(val, NULL, 0);
	if (errno) {
		rte_errno = errno;
		PMD_DRV_LOG(WARNING, "%s: \"%s\" is an invalid integer.", key, val);
		return -rte_errno;
	}

	if (strcmp(key, "rx_empty_threshold") == 0)
		config->rx_empty_threshold = tmp;
	else if (strcmp(key, "tx_free_loop") == 0)
		config->tx_free_loop = tmp;

	return 0;
}

static int
hinic3_nic_common_config_get(struct rte_pci_device *pci_dev,
			     struct hinic3_nic_common_dev_config *config)
{
	int ret = 0;
	struct rte_kvargs *kvlist;
	struct rte_device *eal_dev = &pci_dev->device;

	/* Set private param defaults. */
	config->rx_empty_threshold = HINIC3_RX_EMPTY_THRESHOLD;
	config->tx_free_loop = HINIC3_MAX_TX_FREE_LOOP;

	if (eal_dev->devargs == NULL)
		return 0;

	kvlist = rte_kvargs_parse(eal_dev->devargs->args, NULL);
	if (kvlist == NULL) {
		PMD_DRV_LOG(ERR, "nic private parameter err, the format must be '-a dev,[key]=[value]'.");
		return -EINVAL;
	}

	ret = rte_kvargs_process(kvlist, NULL, hinic3_nic_common_args_check_handler, config);
	if (ret)
		ret = -rte_errno;

	rte_kvargs_free(kvlist);

	return ret;
}

static int hinic3_func_init_qpool(struct rte_eth_dev *eth_dev)
{
	struct hinic3_tcam_info *tcam_info = NULL;
	struct hinic3_nic_dev *nic_dev = NULL;
	struct rte_pci_device *pci_dev = NULL;
	unsigned long compact_cqe = 0;
	int err;

	pci_dev = RTE_ETH_DEV_TO_PCI(eth_dev);

	/* EAL is secondary and eth_dev is already created */
	if (rte_eal_process_type() != RTE_PROC_PRIMARY) {
		PMD_DRV_LOG(INFO, "Initialize %s in secondary process",
			    eth_dev->data->name);

		char name[RTE_ETH_NAME_MAX_LEN];
		snprintf(name, sizeof(name), "%s", eth_dev->data->name);
		eth_dev = rte_eth_dev_attach_secondary(name);
		if (eth_dev == NULL) {
			PMD_DRV_LOG(ERR, "can not attach rte ethdev, dev_name: %s", name);
			return -ENOMEM;
		}

		nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(eth_dev);
		if (nic_dev == NULL) {
			PMD_DRV_LOG(ERR, "nic_dev hwdev is NULL, dev_name: %s", name);
			return -ENOMEM;
		}

		if (HINIC3_FUNC_TYPE(nic_dev->hwdev) == TYPE_VF)
			eth_dev->dev_ops = &hinic3_pmd_vf_ops;
		else
			eth_dev->dev_ops = &hinic3_pmd_ops;

		return 0;
	}

	nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(eth_dev);
	memset(nic_dev, 0, sizeof(*nic_dev));
	nic_dev->id_table = pci_id_hinic3_map;

	(void)snprintf(nic_dev->dev_name, sizeof(nic_dev->dev_name),
		"dbdf-%.4x:%.2x:%.2x.%x",
		pci_dev->addr.domain, pci_dev->addr.bus,
		pci_dev->addr.devid, pci_dev->addr.function);

	/* Alloc mac_addrs */
	eth_dev->data->mac_addrs = rte_zmalloc("hinic3_mac",
		HINIC3_MAX_UC_MAC_ADDRS * sizeof(struct rte_ether_addr), 0);
	if (!eth_dev->data->mac_addrs) {
		PMD_DRV_LOG(ERR, "Allocate %zx bytes to store MAC addresses "
			    "failed, dev_name: %s",
			    HINIC3_MAX_UC_MAC_ADDRS *
			    sizeof(struct rte_ether_addr),
			    eth_dev->data->name);
		err = -ENOMEM;
		goto alloc_eth_addr_fail;
	}

	nic_dev->mc_list = rte_zmalloc("hinic3_mc",
		HINIC3_MAX_MC_MAC_ADDRS * sizeof(struct rte_ether_addr), 0);
	if (!nic_dev->mc_list) {
		PMD_DRV_LOG(ERR, "Allocate %zx bytes to store multicast "
			    "addresses failed, dev_name: %s",
			    HINIC3_MAX_MC_MAC_ADDRS *
			    sizeof(struct rte_ether_addr),
			    eth_dev->data->name);
		err = -ENOMEM;
		goto alloc_mc_list_fail;
	}

#ifndef DPDK_20_11
	/*
	 * Pass the information to the rte_eth_dev_close() that it should also
	 * release the private port resources.
	 */
	eth_dev->data->dev_flags |= RTE_ETH_DEV_CLOSE_REMOVE;
#endif

	nic_dev->hwdev = rte_zmalloc("hinic3_hwdev", sizeof(*(nic_dev->hwdev)),
				     RTE_CACHE_LINE_SIZE);
	if (!nic_dev->hwdev) {
		PMD_DRV_LOG(ERR, "Allocate hwdev memory failed, dev_name: %s",
			    eth_dev->data->name);
		err = -ENOMEM;
		goto alloc_hwdev_mem_fail;
	}

	nic_dev->hwdev->pci_dev = pci_dev;
	nic_dev->hwdev->dev_handle = nic_dev;
	nic_dev->hwdev->eth_dev = eth_dev;
	nic_dev->hwdev->port_id = eth_dev->data->port_id;

	nic_dev->fd = hinic3_get_nic_fd(eth_dev);
	if (nic_dev->fd < 0) {
		PMD_DRV_LOG(ERR, "Qpool func init get nic fd failed, fd: %d",
			nic_dev->fd);
		err = nic_dev->fd;
		goto get_nic_fd_fail;
	}

	err = hinic3_get_link_state_qpool(nic_dev);
	if (err) {
		PMD_DRV_LOG(ERR, "Qpool not support start when netdev is down");
		goto link_state_err;
	}

	err = hinic3_init_hwdev(nic_dev->hwdev);
	if (err) {
		PMD_DRV_LOG(ERR, "Init chip hwdev failed, dev_name: %s",
			    eth_dev->data->name);
		goto init_hwdev_fail;
	}

	nic_dev->max_sqs = hinic3_func_max_sqs(nic_dev->hwdev);
	nic_dev->max_rqs = hinic3_func_max_rqs(nic_dev->hwdev);

	if (HINIC3_FUNC_TYPE(nic_dev->hwdev) == TYPE_VF)
		eth_dev->dev_ops = &hinic3_pmd_vf_ops;
	else
		eth_dev->dev_ops = &hinic3_pmd_ops;

	err = hinic3_get_feature_from_hw(nic_dev->hwdev, &nic_dev->feature_cap, 1);
	if (err) {
		PMD_DRV_LOG(ERR, "Get nic feature from hardware failed, dev_name: %s",
			    eth_dev->data->name);
		goto get_cap_fail;
	}

	if (!is_sp620_nic(nic_dev)) {
		if (hinic3_parse_sysfs_value(RQ_WQE_TYPE_PATH, &compact_cqe) != 0)
			goto get_cap_fail;

		if (compact_cqe == 1)
			nic_dev->feature_cap &= ~(NIC_F_RX_SW_COMPACT_CQE | NIC_F_RX_HW_COMPACT_CQE);
	}

	err = hinic3_init_sw_rxtxqs(nic_dev);
	if (err) {
		PMD_DRV_LOG(ERR, "Init sw rxqs or txqs failed, dev_name: %s",
			    eth_dev->data->name);
		goto init_sw_rxtxqs_fail;
	}

#ifdef DPDK_21_11
	err = rte_intr_fd_set(pci_dev->intr_handle, nic_dev->fd);
	if (err) {
		PMD_DRV_LOG(ERR, "intr fd set failed, err = %d", err);
		goto set_default_feature_fail;
	}
	err = rte_intr_type_set(pci_dev->intr_handle, RTE_INTR_HANDLE_EXT);
	if (err) {
		PMD_DRV_LOG(ERR, "intr type set failed, err = %d", err);
		goto set_default_feature_fail;
	}
#else
	pci_dev->intr_handle.fd = nic_dev->fd;
	pci_dev->intr_handle.type = RTE_INTR_HANDLE_EXT;
#endif

	/* Register callback func to eal lib */
	err = rte_intr_callback_register(PCI_DEV_TO_INTR_HANDLE(pci_dev),
					 hinic3_dev_interrupt_handler_qpool,
						 (void *)eth_dev);
	if (err) {
		PMD_DRV_LOG(ERR, "Register intr callback failed, dev_name: %s",
			    eth_dev->data->name);
		goto reg_intr_cb_fail;
	}

	err = hinic3_init_rx_ptype_table(eth_dev);
	if (err) {
		PMD_DRV_LOG(ERR, "Failed to create ptype_table.");
		goto init_rx_ptype_table_fail;
	}

	tcam_info = &nic_dev->tcam;
	memset(tcam_info, 0, sizeof(struct hinic3_tcam_info));
	TAILQ_INIT(&tcam_info->tcam_list);
	TAILQ_INIT(&tcam_info->tcam_dynamic_info.tcam_dynamic_list);
	TAILQ_INIT(&nic_dev->filter_ethertype_list);
	TAILQ_INIT(&nic_dev->filter_fdir_rule_list);
	TAILQ_INIT(&nic_dev->rss_template_list);

	hinic3_mutex_init_shared(&nic_dev->rx_mode_mutex);

	hinic3_set_bit(HINIC3_DEV_INTR_EN, &nic_dev->dev_status);

	hinic3_set_bit(HINIC3_DEV_INIT, &nic_dev->dev_status);
	PMD_DRV_LOG(INFO, "Initialize %s in primary succeed",
		    eth_dev->data->name);

#ifdef DPDK_20_11
	/**
	 * Queue xstats filled automatically by ethdev layer.
	 */
	eth_dev->data->dev_flags |= RTE_ETH_DEV_AUTOFILL_QUEUE_XSTATS;
#endif
	err = hinic3_dcb_init(nic_dev);
	if (err) {
		PMD_DRV_LOG(ERR, "Failed to init dcb: %d", err);
		goto dcb_init_fail;
	}

	return 0;

dcb_init_fail:
init_rx_ptype_table_fail:
reg_intr_cb_fail:
#ifdef DPDK_21_11
set_default_feature_fail:
#endif
	hinic3_deinit_mac_addr(eth_dev);

	hinic3_deinit_sw_rxtxqs(nic_dev);

init_sw_rxtxqs_fail:
	hinic3_free_nic_hwdev(nic_dev->hwdev);

get_cap_fail:
init_hwdev_fail:
link_state_err:
	close(nic_dev->fd);
get_nic_fd_fail:
	rte_free(nic_dev->hwdev);
	nic_dev->hwdev = NULL;

alloc_hwdev_mem_fail:
	rte_free(nic_dev->mc_list);
	nic_dev->mc_list = NULL;

alloc_mc_list_fail:
	rte_free(eth_dev->data->mac_addrs);
	eth_dev->data->mac_addrs = NULL;

alloc_eth_addr_fail:
	PMD_DRV_LOG(ERR, "Initialize %s in primary failed",
		    eth_dev->data->name);
	return err;
}

static int hinic3_dev_init(struct rte_eth_dev *eth_dev)
{
	struct rte_pci_device *pci_dev;
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(eth_dev);
	int err;

	pci_dev = RTE_ETH_DEV_TO_PCI(eth_dev);

	PMD_DRV_LOG(INFO, "Initializing %.4x:%.2x:%.2x.%x in %s process",
		    pci_dev->addr.domain, pci_dev->addr.bus,
		    pci_dev->addr.devid, pci_dev->addr.function,
		    (rte_eal_process_type() == RTE_PROC_PRIMARY) ?
		    "primary" : "secondary");

	PMD_DRV_LOG(INFO, "Network Interface pmd driver version: %s", HINIC3_PMD_DRV_VERSION);

	if (IS_QPOOL_MODE())
 		err = hinic3_func_init_qpool(eth_dev);
	else
		err = hinic3_func_init(eth_dev);
	if (err < 0)
		return err;

	err = hinic3_nic_common_config_get(pci_dev, &nic_dev->config);
	if (err < 0) {
		PMD_DRV_LOG(ERR, "Failed to get nic device arguments: %s",
			strerror(rte_errno));
		return err;
	}

	if (is_sp620_nic(nic_dev) || !HINIC3_SUPPORT_RX_SW_COMPACT_CQE(nic_dev)) {
#ifdef RTE_ARCH_ARM
		if (nic_dev->vec_allowed == 1)
			eth_dev->rx_pkt_burst = hinic3_recv_pkts_vec;
		else
#endif
			eth_dev->rx_pkt_burst = hinic3_recv_pkts;
		eth_dev->tx_pkt_burst = hinic3_xmit_pkts;
	} else {
		eth_dev->rx_pkt_burst = hinic3_recv_pkts_compact_cqe;
		eth_dev->tx_pkt_burst = hinic3_xmit_pkts_compact_cqe;
	}

	return err;
}

static int hinic3_dev_uninit(struct rte_eth_dev *dev)
{
	struct hinic3_nic_dev *nic_dev;

	nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	hinic3_clear_bit(HINIC3_DEV_INIT, &nic_dev->dev_status);

	if (rte_eal_process_type() != RTE_PROC_PRIMARY)
		return 0;

#ifdef DPDK_20_11
	return hinic3_dev_close(dev);
#else
	hinic3_dev_close(dev);

	return 0;
#endif
}

static int hinic3_pci_probe(struct rte_pci_driver *pci_drv,
			    struct rte_pci_device *pci_dev)
{
	struct rte_pci_device *work_pci_dev = pci_dev;
	char dev_file[PATH_MAX];
	int ret = 0;

	snprintf(dev_file, sizeof(dev_file), "/sys/class/nic_cdev/nic_cdev!" PCI_PRI_FMT "/qinfo_mode",
		 pci_dev->addr.domain,
		 pci_dev->addr.bus,
		 pci_dev->addr.devid,
		 pci_dev->addr.function);

	ret = hinic3_qinfo_type_init(dev_file);
	if (ret != 0) {
		PMD_DRV_LOG(ERR, "Qinfo type init failed: %d, unable to know mode used.", ret);
		return ret;
	}

	if (IS_NORMAL_MODE()) {
		ret = rte_pci_map_device(pci_dev);
		pci_drv->drv_flags |= RTE_PCI_DRV_NEED_MAPPING;
		if (ret != 0) {
			PMD_DRV_LOG(ERR, "Rte pci map device failed: %d", ret);
			return ret;
		}
	} else if (IS_BIFUR_MODE()) {
		enum BIFUR_ACTION bifur_action = BIFUR_CONTINUE;
		ret = hinic3_bifur_pre_probe(pci_drv, pci_dev, &work_pci_dev, &bifur_action);
		if (ret != 0 || bifur_action == BIFUR_DONE) {
			PMD_DRV_LOG(ERR, "Bifur pre probe failed: %d", ret);
			return ret;
		}
	}

	ret = rte_eth_dev_pci_generic_probe(work_pci_dev,
		sizeof(struct hinic3_nic_dev), hinic3_dev_init);
	return ret;
}

static int hinic3_pci_remove(struct rte_pci_device *pci_dev)
{
	int ret;
	ret = rte_eth_dev_pci_generic_remove(pci_dev, hinic3_dev_uninit);
	if (ret != 0) {
		PMD_DRV_LOG(ERR, "hinic3_pci_remove: rte remove failed!");
	}

	if (IS_BIFUR_MODE())
		hinic3_bifur_post_remove(pci_dev);

	return ret;
}

static struct rte_pci_driver rte_hinic3_pmd = {
	.id_table = pci_id_hinic3_map,
	.drv_flags = RTE_PCI_DRV_INTR_LSC,
	.probe = hinic3_pci_probe,
	.remove = hinic3_pci_remove,
};

RTE_PMD_REGISTER_PCI(net_hinic3, rte_hinic3_pmd);
RTE_PMD_REGISTER_PCI_TABLE(net_hinic3, pci_id_hinic3_map);

RTE_INIT(hinic3_init_log)
{
	hinic3_logtype = rte_log_register("pmd.net.hinic3");
	if (hinic3_logtype >= 0)
		rte_log_set_level(hinic3_logtype, RTE_LOG_INFO);
}
