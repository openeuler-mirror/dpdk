/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2025-2026 Huawei Technologies Co., Ltd
 */

#ifndef _HINIC3_PMD_ETHDEV_H_
#define _HINIC3_PMD_ETHDEV_H_

#include <rte_ethdev.h>
#include <rte_kvargs.h>
#include <rte_devargs.h>
#include <rte_ethdev_core.h>
#include "base/hinic3_pmd_csr.h"
#include "base/hinic3_pmd_nic_cfg.h"
#include "hinic3_pmd_fdir.h"
#include "hinic3_pmd_tm.h"
#include "hinic3_pmd_tx.h"
#include "hinic3_pmd_rx.h"

#ifdef GLOBAL_VERSION_STR
#define HINIC3_PMD_DRV_VERSION GLOBAL_VERSION_STR
#else
#define HINIC3_PMD_DRV_VERSION "hinic3-26.2.rc1-0930.r1"
#endif

#define HINIC3_MAX_QUEUE_DEPTH		16384
#define HINIC3_MIN_QUEUE_DEPTH		128

#define HINIC3_QUEUE_STAT_CNTRS     256

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

#define HINIC3_DEFAULT_TX_CI_PENDING_LIMIT	2
#define HINIC3_DEFAULT_TX_CI_COALESCING_TIME	2
#define HINIC3_RX_CQE_COMPACT_EN 1
#define HINIC3_RX_CQE_TIMER_LOOP 		8
#define HINIC3_RX_CQE_COALESCE_NUM		7

#define HINIC3_CI_PENDING_LIMIT_UNIT 8
#define HINIC3_CI_COALESCING_TIME_UNIT 5

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
	NIC_F_XSFP_REPORT = LBIT(14),
 	NIC_F_VF_MAC = LBIT(15),
 	NIC_F_RATE_LIMIT = LBIT(16),
 	NIC_F_RXQ_RECOVERY = LBIT(17),
 	NIC_F_PTP_1588_V2 = LBIT(18),
 	NIC_F_TX_WQE_COMPACT_TASK = LBIT(19),
 	NIC_F_RX_HW_COMPACT_CQE = LBIT(20),
 	NIC_F_HTN_CMDQ = LBIT(21),
 	NIC_F_GENEVE_OFFLOAD = LBIT(22),
 	NIC_F_IPXIP_OFFLOAD = LBIT(23),
 	NIC_F_TC_FLOWER_OFFLOAD = LBIT(24),
 	NIC_F_HTN_FDIR = LBIT(25),
 	NIC_F_SQ_RQ_CI_COALESCE = LBIT(26),
 	NIC_F_RX_SW_COMPACT_CQE = LBIT(27),
 	NIC_F_HALF_BOND_OFFLOAD = LBIT(28),
 	NIC_F_MACSEC_OFFLOAD = LBIT(29),
 	NIC_F_VEB_OFFLOAD = LBIT(30),
 	NIC_F_GET_COUNTER_BY_CMDQ = LBIT(31),
	NIC_F_HAIRPIN = LBIT(32),
	NIC_F_RX_CSUM_DIS_BIT = LBIT(34),
};

enum hinic3_function_mode {
	HINIC3_FUNC_EXCLUSIVE = 0,
	HINIC3_FUNC_SHARED,
};

#define DEFAULT_DRV_FEATURE 0x40BFC3FFF
#define HINIC3_VERIFY_RX_DEPTH     1
#define HINIC3_VERIFY_TX_DEPTH     0

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

struct hinic3_nic_common_dev_config {
	unsigned int rx_empty_threshold;
	unsigned int rx_empty_loop;	/* max empty CQE spin count; 0 = no limit */
	unsigned int tx_free_loop;
	unsigned int tx_pending_limit; /* TX CI coalescing parameter pending_limit. */
	unsigned int tx_coalescing_time; /* TX CI coalescing parameter coalescing_time. */
	unsigned int rx_cqe_compact_en; /* cqe mode, 0 -- separate cqe, 1 -- compact cqe. */
	unsigned int rx_cqe_coalesce_num; /* RX CQE parameter coalesce_num. */
	unsigned int rx_cqe_timer_loop; /* RX CQE parameter time_loop. */
	unsigned int rx_dma_align; /* RX DMA buffer alignment (0/64/128/256/512). 0 disables. */
	unsigned int rx_sw_rss; /* SW RSS switch, 0 -- disable, non-zero -- enable. */
	unsigned int sw_rss_flush_us; /* SW RSS cache timeout flush threshold (us),
				       * default 10us. A smaller value lowers delivery
				       * latency but causes more small-batch enqueues and
				       * heavier ring CAS contention.
				       */
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
	u16 rss_q_mask;                     /* num_rqs-1, for fast modulo when num_rqs is a power of 2 */
	bool rss_q_pow2;                    /* whether num_rqs is a power of 2 */
	uint64_t sw_rss_flush_tsc;          /* cache timeout flush threshold (tsc cycles), converted from sw_rss_flush_us at init */

 	u16 rxq_depth;
	u16 txq_depth;

	u16 rx_buff_len;
	u16 mtu_size;

	u16 rss_state;
	u16 num_rss;

	struct hinic3_rss_type rss_type;

	u32 rx_mode;
	u8 rx_queue_list[HINIC3_MAX_QUEUE_NUM];
	rte_spinlock_t queue_list_lock;

	pthread_mutex_t rx_mode_mutex;

	u32 cos_mask;
	u32 default_cos;
	u32 rx_csum_en;

	struct hinic3_dcb *dcb;

	u8 rss_key[HINIC3_RSS_KEY_SIZE];
	u16 global_id;
	u16 indir_table_size;
	int fd;

	unsigned long dev_status;

	bool pause_set;
	pthread_mutex_t pause_mutuex;
	struct nic_pause_config nic_pause;
	struct hinic3_nic_common_dev_config config;

	struct rte_ether_addr default_addr;
	struct rte_ether_addr *mc_list;

	char dev_name[HINIC3_DEV_NAME_LEN];
	const struct rte_pci_id *id_table;
	u64 feature_cap;
	u32 vfta[HINIC3_VFTA_SIZE]; /* VLAN bitmap */

	u16 tcam_rule_nums;
	u16 ethertype_rule_nums;
	struct hinic3_tcam_info tcam;
	struct hinic3_ethertype_filter_list filter_ethertype_list;
	struct hinic3_fdir_rule_filter_list filter_fdir_rule_list;
	struct hinic3_rss_template_list rss_template_list;
	u8 cos_map[HINIC3_COS_NUM_MAX];

	struct hinic3_nic_cmdq_ops *cmdq_ops;

	struct hinic3_ptype_table* ptype_tbl;

	u8 hinic3_function_mode;

	struct hinic3_ets *ets;

	uint32_t fec_mode;  /* current FEC mode for ethdev */

	bool hinic3_offload_initialized;
	bool vec_allowed;
		/* ===== VXLAN-GPE SW RSS fields ===== */
	struct rte_ring **sw_rss_rings;        /* array of num_rqs pointers to MP/SC rings */
	bool sw_rss_enabled;  
};

enum netdev_event_type {
	NETDEV_UP	  = 1, /* For now you can't veto a device up/down */
	NETDEV_DOWN	  = 2, 
	NETDEV_REBOOT	  = 3, /* Tell a protocol stack a network interface
				   detected a hardware crash and restarted
				   - we can use this eg to kick tcp sessions
				   once done */
	NETDEV_CHANGE	  = 4,  /* Notify device state change */
	NETDEV_REGISTER	  = 5,
	NETDEV_UNREGISTER = 6,
	NETDEV_CHANGEMTU  = 7,
	NETDEV_CHANGEADDR = 8,
};

#define MAX_PROCESS 64

#define SELECT_OTHER_COS_ID(cos_id) ((cos_id) ^ 4)
#define ODD_NUMBER_QUEUE_ID(q_id) ((q_id) & 1)

struct netdev_event {
	u32 type;
	u8 data[32];
	u16 data_mtu;
	u16 rsvd;
};

/* ===== VXLAN-GPE SW RSS batch cache ===== */
#define HINIC3_SW_RSS_CACHE_THRESH 32
/* Timeout check interval mask: scan for expired caches once every 64 receives
 * so that the scan cost is amortized.
 */
#define HINIC3_SW_RSS_FLUSH_CHK_MASK 0x3F
/* IP version: the high 4 bits of the first IP byte. */
#define HINIC3_SW_RSS_IP_VERSION_SHIFT		4
/* IPv4 IHL: the low 4 bits of version_ihl, expressed in 4-byte words. */
#define HINIC3_SW_RSS_IPV4_IHL_MASK		0x0F
#define HINIC3_SW_RSS_IPV4_IHL_SHIFT		2
/* Byte length of an IPv4 header, derived from its IHL field. */
#define HINIC3_SW_RSS_IPV4_HDR_LEN(ip4) \
	((uint16_t)(((ip4)->version_ihl & HINIC3_SW_RSS_IPV4_IHL_MASK) << \
		    HINIC3_SW_RSS_IPV4_IHL_SHIFT))

/* SW RSS hash key layout: IPv6 src + dst (32B) + L4 ports (4B). */
#define HINIC3_SW_RSS_HASH_KEY_MAX_LEN		36
/* IPv4 src_addr and dst_addr are contiguous, so both are fetched at once. */
#define HINIC3_SW_RSS_IPV4_ADDR_PAIR_LEN	8
#define HINIC3_SW_RSS_IPV6_ADDR_LEN		16
/* IPv6 src + dst address, used as the leading part of the hash key. */
#define HINIC3_SW_RSS_IPV6_ADDR_PAIR_LEN	32
/* TCP/UDP source port + destination port. */
#define HINIC3_SW_RSS_L4_PORT_PAIR_LEN		4

/* VXLAN-GPE header: flags(1) + reserved(1) + next_protocol(1) + vni(3). */
#define HINIC3_VXLAN_GPE_HDR_LEN		8
#define HINIC3_VXLAN_GPE_NEXT_PROTO_OFFSET	3

/* NSH header (RFC 8300): the base header is 8B and the Length field is the low
 * 6 bits of the 16-bit ver_flags_ttl_len, counting 4-byte words for the whole
 * NSH header (base header + SPH + context headers).
 */
#define HINIC3_NSH_BASE_HDR_LEN			8
#define HINIC3_NSH_LEN_OFFSET			1
#define HINIC3_NSH_LEN_MASK			0x3F
#define HINIC3_NSH_LEN_SHIFT			2
/* Minimum valid NSH length, in 4-byte words (base header only). */
#define HINIC3_NSH_MIN_LEN_WORDS		2

/* VXLAN-GPE next_protocol values, describing the encapsulated payload. */
enum hinic3_sw_rss_vxlan_gpe_next_proto {
	HINIC3_SW_RSS_VXLAN_GPE_NEXT_PROTO_IPV4 = 0x01,
	HINIC3_SW_RSS_VXLAN_GPE_NEXT_PROTO_IPV6 = 0x02,
	HINIC3_SW_RSS_VXLAN_GPE_NEXT_PROTO_ETHERNET = 0x03,
	HINIC3_SW_RSS_VXLAN_GPE_NEXT_PROTO_NSH = 0x04,
};

/* Per RX-core batch cache indexed by target queue.
 * cnt is placed first so that it shares the first cache line with the early
 * mbuf pointers; __rte_cache_aligned aligns each array element to 64B so that
 * adjacent entries never overlap across cache lines.
 */
struct hinic3_sw_rss_cache {
	uint16_t cnt;
	uint16_t reserved;
	uint64_t last_tsc;	/* tsc when the first mbuf of this batch was cached, used for the timeout flush decision */
	struct rte_mbuf *mbufs[HINIC3_SW_RSS_CACHE_THRESH];
} __rte_cache_aligned;

/* VXLAN-GPE SW RSS function declarations */
int hinic3_sw_rss_trace_hook(struct rte_mbuf *m,
			      struct hinic3_nic_dev *nic_dev,
			      uint16_t my_qid,
			      struct hinic3_sw_rss_cache *cache);
void hinic3_sw_rss_flush_expired(struct hinic3_nic_dev *nic_dev,
				 struct hinic3_sw_rss_cache *cache);
void hinic3_sw_rss_flush_all(struct hinic3_nic_dev *nic_dev,
			     struct hinic3_sw_rss_cache *cache);

extern const struct rte_flow_ops hinic3_flow_ops;

bool is_sp230_nic(struct hinic3_nic_dev *nic_dev);
bool is_sp620_nic(struct hinic3_nic_dev *nic_dev);
bool is_sp560_nic(struct hinic3_nic_dev *nic_dev);
bool is_sp230_pci_dev(struct rte_pci_device *pci_dev);
int hinic3_dev_rx_queue_intr_enable(struct rte_eth_dev *dev, uint16_t queue_id);
int hinic3_dev_rx_queue_intr_disable(struct rte_eth_dev *dev,
				     uint16_t queue_id);
void hinic3_dev_info_get(struct rte_eth_dev_info *info,
			 struct hinic3_nic_dev *nic_dev);

int hinic3_get_group_num_qpool(struct hinic3_nic_dev *nic_dev, u8 *num_tc);

/**
 * Rate-limited driver log.
 *
 * Emits at most one message per @p interval_tsc cycles for all callers sharing
 * @p last_tsc. The emitted line carries the driver name prefix and a trailing
 * newline, consistent with PMD_DRV_LOG(). The level check is done before the
 * rate-limit accounting, so a disabled level costs almost nothing.
 *
 * @param level
 *   RTE_LOG_* level, passed as a runtime value.
 * @param last_tsc
 *   Caller-owned limiter state, statically initialized to 0.
 * @param interval_tsc
 *   Minimum interval between two messages, in tsc cycles.
 * @param fmt
 *   printf format of the message body (without prefix and newline).
 */
void hinic3_log_limited(uint32_t level, uint64_t *last_tsc,
			uint64_t interval_tsc, const char *fmt, ...)
	__rte_format_printf(4, 5);

/* Convenience wrapper adding the driver name prefix and a trailing newline. */
#define HINIC3_LOG_LIMITED(level, last_tsc, interval_tsc, fmt, args...) \
	hinic3_log_limited((level), (last_tsc), (interval_tsc), \
			   HINIC3_DRIVER_NAME ": " fmt "\n", ##args)

#endif /* _HINIC3_PMD_ETHDEV_H_ */
