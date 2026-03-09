/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2019 Huawei Technologies Co., Ltd
 */

#ifndef _HINIC3_PMD_ETHDEV_H_
#define _HINIC3_PMD_ETHDEV_H_

#include <rte_ethdev.h>
#include <rte_ethdev_core.h>
#include "base/hinic3_pmd_nic_cfg.h"
#include "hinic3_pmd_fdir.h"
#include "hinic3_pmd_tm.h"

#ifdef GLOBAL_VERSION_STR
#define HINIC3_PMD_DRV_VERSION GLOBAL_VERSION_STR
#else
#define HINIC3_PMD_DRV_VERSION "B106"
#endif

#define HINIC3_MAX_QUEUE_DEPTH		16384
#define HINIC3_MIN_QUEUE_DEPTH		128

#ifdef DPDK_21_11
#define PCI_DEV_TO_INTR_HANDLE(pci_dev) (pci_dev->intr_handle)

#define HINIC3_PKT_RX_L4_CKSUM_BAD       RTE_MBUF_F_RX_L4_CKSUM_BAD
#define HINIC3_PKT_RX_IP_CKSUM_BAD       RTE_MBUF_F_RX_IP_CKSUM_BAD
#define HINIC3_PKT_RX_IP_CKSUM_UNKNOWN   RTE_MBUF_F_RX_IP_CKSUM_UNKNOWN
#define HINIC3_PKT_RX_L4_CKSUM_GOOD      RTE_MBUF_F_RX_L4_CKSUM_GOOD
#define HINIC3_PKT_RX_IP_CKSUM_GOOD      RTE_MBUF_F_RX_IP_CKSUM_GOOD
#define HINIC3_PKT_TX_TCP_SEG            RTE_MBUF_F_TX_TCP_SEG
#define HINIC3_PKT_TX_UDP_CKSUM          RTE_MBUF_F_TX_UDP_CKSUM
#define HINIC3_PKT_TX_TCP_CKSUM          RTE_MBUF_F_TX_TCP_CKSUM
#define HINIC3_PKT_TX_IP_CKSUM           RTE_MBUF_F_TX_IP_CKSUM
#define HINIC3_PKT_TX_VLAN_PKT           RTE_MBUF_F_TX_VLAN
#define HINIC3_PKT_TX_QINQ_PKT           RTE_MBUF_F_TX_QINQ
#define HINIC3_PKT_TX_L4_MASK            RTE_MBUF_F_TX_L4_MASK
#define HINIC3_PKT_TX_SCTP_CKSUM         RTE_MBUF_F_TX_SCTP_CKSUM
#define HINIC3_PKT_TX_IPV6               RTE_MBUF_F_TX_IPV6
#define HINIC3_PKT_TX_IPV4               RTE_MBUF_F_TX_IPV4
#define HINIC3_PKT_RX_VLAN               RTE_MBUF_F_RX_VLAN
#define HINIC3_PKT_RX_VLAN_STRIPPED      RTE_MBUF_F_RX_VLAN_STRIPPED
#define HINIC3_PKT_RX_RSS_HASH           RTE_MBUF_F_RX_RSS_HASH
#define HINIC3_PKT_TX_TUNNEL_MASK        RTE_MBUF_F_TX_TUNNEL_MASK
#define HINIC3_PKT_TX_TUNNEL_GRE         RTE_MBUF_F_TX_TUNNEL_GRE
#define HINIC3_PKT_TX_TUNNEL_VXLAN       RTE_MBUF_F_TX_TUNNEL_VXLAN
#define HINIC3_PKT_TX_TUNNEL_VXLAN_GPE   RTE_MBUF_F_TX_TUNNEL_VXLAN_GPE
#define HINIC3_PKT_TX_TUNNEL_GENEVE      RTE_MBUF_F_TX_TUNNEL_GENEVE
#define HINIC3_PKT_TX_TUNNEL_IPIP        RTE_MBUF_F_TX_TUNNEL_IPIP
#define HINIC3_PKT_TX_OUTER_IP_CKSUM     RTE_MBUF_F_TX_OUTER_IP_CKSUM
#define HINIC3_PKT_TX_OUTER_UDP_CKSUM    RTE_MBUF_F_TX_OUTER_UDP_CKSUM
#define HINIC3_PKT_TX_OUTER_IPV6         RTE_MBUF_F_TX_OUTER_IPV6
#define HINIC3_PKT_RX_LRO                RTE_MBUF_F_RX_LRO
#define HINIC3_PKT_TX_L4_NO_CKSUM        RTE_MBUF_F_TX_L4_NO_CKSUM
#else
#define PCI_DEV_TO_INTR_HANDLE(pci_dev) (&((pci_dev)->intr_handle))

#define HINIC3_PKT_RX_L4_CKSUM_BAD       PKT_RX_L4_CKSUM_BAD
#define HINIC3_PKT_RX_IP_CKSUM_BAD       PKT_RX_IP_CKSUM_BAD
#define HINIC3_PKT_RX_IP_CKSUM_UNKNOWN   PKT_RX_IP_CKSUM_UNKNOWN
#define HINIC3_PKT_RX_L4_CKSUM_GOOD      PKT_RX_L4_CKSUM_GOOD
#define HINIC3_PKT_RX_IP_CKSUM_GOOD      PKT_RX_IP_CKSUM_GOOD
#define HINIC3_PKT_TX_TCP_SEG            PKT_TX_TCP_SEG
#define HINIC3_PKT_TX_UDP_CKSUM          PKT_TX_UDP_CKSUM
#define HINIC3_PKT_TX_TCP_CKSUM          PKT_TX_TCP_CKSUM
#define HINIC3_PKT_TX_IP_CKSUM           PKT_TX_IP_CKSUM
#define HINIC3_PKT_TX_VLAN_PKT           PKT_TX_VLAN_PKT
#define HINIC3_PKT_TX_QINQ_PKT           PKT_TX_QINQ_PKT
#define HINIC3_PKT_TX_L4_MASK            PKT_TX_L4_MASK
#define HINIC3_PKT_TX_SCTP_CKSUM         PKT_TX_SCTP_CKSUM
#define HINIC3_PKT_TX_IPV6               PKT_TX_IPV6
#define HINIC3_PKT_TX_IPV4               PKT_TX_IPV4
#define HINIC3_PKT_RX_VLAN               PKT_RX_VLAN
#define HINIC3_PKT_RX_VLAN_STRIPPED      PKT_RX_VLAN_STRIPPED
#define HINIC3_PKT_RX_RSS_HASH           PKT_RX_RSS_HASH
#define HINIC3_PKT_TX_TUNNEL_MASK        PKT_TX_TUNNEL_MASK
#define HINIC3_PKT_TX_TUNNEL_GRE         PKT_TX_TUNNEL_GRE
#define HINIC3_PKT_TX_TUNNEL_VXLAN       PKT_TX_TUNNEL_VXLAN
#define HINIC3_PKT_TX_TUNNEL_VXLAN_GPE   PKT_TX_TUNNEL_VXLAN_GPE
#define HINIC3_PKT_TX_TUNNEL_GENEVE      PKT_TX_TUNNEL_GENEVE
#define HINIC3_PKT_TX_TUNNEL_IPIP        PKT_TX_TUNNEL_IPIP
#define HINIC3_PKT_TX_OUTER_IP_CKSUM     PKT_TX_OUTER_IP_CKSUM
#define HINIC3_PKT_TX_OUTER_UDP_CKSUM    PKT_TX_OUTER_UDP_CKSUM
#define HINIC3_PKT_TX_OUTER_IPV6         PKT_TX_OUTER_IPV6
#define HINIC3_PKT_RX_LRO                PKT_RX_LRO
#define HINIC3_PKT_TX_L4_NO_CKSUM        PKT_TX_L4_NO_CKSUM
#endif

#define HINCI3_CPY_MEMPOOL_NAME "cpy_mempool"
/* mbuf pool for copy invalid mbuf segs */
#define HINIC3_COPY_MEMPOOL_DEPTH	1024
#define HINIC3_COPY_MEMPOOL_CACHE	128
#define HINIC3_COPY_MBUF_SIZE		4096

#define HINIC3_DEV_NAME_LEN              32
#define DEV_STOP_DELAY_MS               100
#define DEV_START_DELAY_MS              100

#define HINIC3_UINT32_BIT_SIZE           (CHAR_BIT * sizeof(uint32_t))
#define HINIC3_VFTA_SIZE                 (4096 / HINIC3_UINT32_BIT_SIZE)
#define HINIC3_MAX_QUEUE_NUM             256

#define HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev) \
	((struct hinic3_nic_dev *)(dev)->data->dev_private)

enum hinic3_dev_status {
	HINIC3_DEV_INIT,
	HINIC3_DEV_CLOSE,
	HINIC3_DEV_START,
	HINIC3_DEV_INTR_EN
};

enum hinic3_tx_cvlan_type {
	HINIC3_TX_TPID0,
};

enum nic_feature_cap {
	NIC_F_CSUM = LBIT(0),
	NIC_F_SCTP_CRC = LBIT(1),
	NIC_F_TSO = LBIT(2),
	NIC_F_LRO = LBIT(3),
	NIC_F_UFO = LBIT(4),
	NIC_F_RSS = LBIT(5),
	NIC_F_RX_VLAN_FILTER = LBIT(6),
	NIC_F_RX_VLAN_STRIP = LBIT(7),
	NIC_F_TX_VLAN_INSERT = LBIT(8),
	NIC_F_VXLAN_OFFLOAD = LBIT(9),
	NIC_F_IPSEC_OFFLOAD = LBIT(10),
	NIC_F_FDIR = LBIT(11),
	NIC_F_PROMISC = LBIT(12),
	NIC_F_ALLMULTI = LBIT(13),
	NIC_F_HAIRPIN = LBIT(32),
};

enum hinic3_function_mode {
	HINIC3_FUNC_EXCLUSIVE = 0,
	HINIC3_FUNC_SHARED,
};

#define DEFAULT_DRV_FEATURE		0x3FFF

TAILQ_HEAD(hinic3_ethertype_filter_list, rte_flow);
TAILQ_HEAD(hinic3_fdir_rule_filter_list, rte_flow);

#define HINIC3_PTYPE_NUM 4096
struct hinic3_ptype_table {
#ifdef DPDK_23_11
	alignas(RTE_CACHE_LINE_SIZE) uint32_t ptype[HINIC3_PTYPE_NUM];
#else
	uint32_t ptype[HINIC3_PTYPE_NUM] __rte_cache_aligned;
#endif
};

struct hinic3_nic_dev {
	struct hinic3_hwdev *hwdev; /* Hardware device */

	struct hinic3_txq **txqs;
	struct hinic3_rxq **rxqs;
	struct rte_mempool *cpy_mpool;

	u16 num_sqs;
	u16 num_rqs;
	u16 max_sqs;
	u16 max_rqs;

	u16 rx_buff_len;
	u16 mtu_size;

	u16 rss_state;
	u16 num_rss;

	struct hinic3_rss_type rss_type;

	u32 rx_mode;
	u8 rx_queue_list[HINIC3_MAX_QUEUE_NUM];
	rte_spinlock_t queue_list_lock;

	pthread_mutex_t rx_mode_mutex;

	u32 default_cos;
	u32 rx_csum_en;

	struct hinic3_dcb *dcb;

	u8 rss_key[HINIC3_RSS_KEY_SIZE];

	unsigned long dev_status;

	bool pause_set;
	pthread_mutex_t pause_mutuex;
	struct nic_pause_config nic_pause;

	struct rte_ether_addr default_addr;
	struct rte_ether_addr *mc_list;

	char dev_name[HINIC3_DEV_NAME_LEN];
	const struct rte_pci_id *id_table;
	u64 feature_cap;
	u32 vfta[HINIC3_VFTA_SIZE]; /* VLAN bitmap */ /*lint !e40*/

	u16 tcam_rule_nums;
	u16 ethertype_rule_nums;
	struct hinic3_tcam_info tcam;
	struct hinic3_ethertype_filter_list filter_ethertype_list;
	struct hinic3_fdir_rule_filter_list filter_fdir_rule_list;
	struct hinic3_rss_template_list rss_template_list;

	struct hinic3_ptype_table* ptype_tbl;
#ifdef HINIC3_TRAFFIC_BIFUR
	u8 hinic3_function_mode;
#endif
	struct hinic3_ets *ets;

	uint32_t fec_mode;  /* current FEC mode for ethdev */
};

extern const struct rte_flow_ops hinic3_flow_ops;

int hinic3_dev_rx_queue_intr_enable(struct rte_eth_dev *dev, uint16_t queue_id);
int hinic3_dev_rx_queue_intr_disable(struct rte_eth_dev *dev,
				     uint16_t queue_id);
void hinic3_dev_info_get(struct rte_eth_dev_info *info,
			 struct hinic3_nic_dev *nic_dev);

#endif /* _HINIC3_PMD_ETHDEV_H_ */
