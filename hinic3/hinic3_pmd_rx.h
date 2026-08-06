/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2025-2026 Huawei Technologies Co., Ltd
 */

#ifndef _HINIC3_PMD_RX_H_
#define _HINIC3_PMD_RX_H_

#include "hinic3_pmd_wq.h"

#define RQ_CQE_OFFOLAD_TYPE_PTYPE_OFFLOAD_SHIFT		0
#define RQ_CQE_OFFOLAD_TYPE_PKT_TYPE_SHIFT		0
#define RQ_CQE_OFFOLAD_TYPE_IP_TYPE_SHIFT		5
#define RQ_CQE_OFFOLAD_TYPE_ENC_L3_TYPE_SHIFT		7
#define RQ_CQE_OFFOLAD_TYPE_PKT_FORMAT_SHIFT		8
#define RQ_CQE_OFFOLAD_TYPE_PKT_UMBCAST_SHIFT		19
#define RQ_CQE_OFFOLAD_TYPE_VLAN_EN_SHIFT		21
#define RQ_CQE_OFFOLAD_TYPE_RSS_TYPE_SHIFT		24

#define RQ_CQE_OFFOLAD_TYPE_PTYPE_OFFLOAD_MASK		0xFFFU
#define RQ_CQE_OFFOLAD_TYPE_PKT_TYPE_MASK		0x1FU
#define RQ_CQE_OFFOLAD_TYPE_IP_TYPE_MASK		0x3U
#define RQ_CQE_OFFOLAD_TYPE_ENC_L3_TYPE_MASK		0X1U
#define RQ_CQE_OFFOLAD_TYPE_PKT_FORMAT_MASK		0xFU
#define RQ_CQE_OFFOLAD_TYPE_PKT_UMBCAST_MASK		0x3U
#define RQ_CQE_OFFOLAD_TYPE_VLAN_EN_MASK		0x1U
#define RQ_CQE_OFFOLAD_TYPE_RSS_TYPE_MASK		0xFFU

#define DPI_EXT_ACTION_FILED		(1ULL << 32)

#define HINIC3_GET_RX_PTYPE_OFFLOAD(offload_type)	\
		RQ_CQE_OFFOLAD_TYPE_GET(offload_type, PTYPE_OFFLOAD)

#define RQ_CQE_OFFOLAD_TYPE_GET(val, member)		(((val) >> \
				RQ_CQE_OFFOLAD_TYPE_##member##_SHIFT) & \
				RQ_CQE_OFFOLAD_TYPE_##member##_MASK)

#define HINIC3_GET_RX_PKT_TYPE(offload_type)	\
		RQ_CQE_OFFOLAD_TYPE_GET(offload_type, PKT_TYPE)

#define HINIC3_GET_RX_IP_TYPE(offload_type) \
	RQ_CQE_OFFOLAD_TYPE_GET(offload_type, IP_TYPE)

#define HINIC3_GET_RX_ENC_L3_TYPE(offload_type) \
	RQ_CQE_OFFOLAD_TYPE_GET(offload_type, ENC_L3_TYPE)

#define HINIC3_GET_RX_PKT_FORMAT(offload_type) \
	RQ_CQE_OFFOLAD_TYPE_GET(offload_type, PKT_FORMAT)

#define HINIC3_GET_RX_PKT_UMBCAST(offload_type)	\
		RQ_CQE_OFFOLAD_TYPE_GET(offload_type, PKT_UMBCAST)

#define HINIC3_GET_RX_VLAN_OFFLOAD_EN(offload_type)	\
		RQ_CQE_OFFOLAD_TYPE_GET(offload_type, VLAN_EN)

#define HINIC3_GET_RSS_TYPES(offload_type)	\
		RQ_CQE_OFFOLAD_TYPE_GET(offload_type, RSS_TYPE)

#define RQ_CQE_SGE_VLAN_SHIFT				0
#define RQ_CQE_SGE_LEN_SHIFT				16

#define RQ_CQE_SGE_VLAN_MASK				0xFFFFU
#define RQ_CQE_SGE_LEN_MASK				0xFFFFU

#define RQ_CQE_SGE_GET(val, member)			(((val) >> \
					RQ_CQE_SGE_##member##_SHIFT) & \
					RQ_CQE_SGE_##member##_MASK)

#define HINIC3_GET_RX_VLAN_TAG(vlan_len)	RQ_CQE_SGE_GET(vlan_len, VLAN)

#define HINIC3_GET_RX_PKT_LEN(vlan_len)		RQ_CQE_SGE_GET(vlan_len, LEN)

#define RQ_CQE_STATUS_CSUM_ERR_SHIFT		0
#define RQ_CQE_STATUS_NUM_LRO_SHIFT		16
#define RQ_CQE_STATUS_LRO_PUSH_SHIFT		25
#define RQ_CQE_STATUS_LRO_ENTER_SHIFT		26
#define RQ_CQE_STATUS_LRO_INTR_SHIFT		27

#define RQ_CQE_STATUS_BP_EN_SHIFT		30
#define RQ_CQE_STATUS_RXDONE_SHIFT		31
#define RQ_CQE_STATUS_DECRY_PKT_SHIFT		29
#define RQ_CQE_STATUS_FLUSH_SHIFT		28

#define RQ_CQE_STATUS_CSUM_ERR_MASK		0xFFFFU
#define RQ_CQE_STATUS_NUM_LRO_MASK		0xFFU
#define RQ_CQE_STATUS_LRO_PUSH_MASK		0X1U
#define RQ_CQE_STATUS_LRO_ENTER_MASK		0X1U
#define RQ_CQE_STATUS_LRO_INTR_MASK		0X1U
#define RQ_CQE_STATUS_BP_EN_MASK		0X1U
#define RQ_CQE_STATUS_RXDONE_MASK		0x1U
#define RQ_CQE_STATUS_FLUSH_MASK		0x1U
#define RQ_CQE_STATUS_DECRY_PKT_MASK		0x1U

#define RQ_CQE_STATUS_GET(val, member)			(((val) >> \
					RQ_CQE_STATUS_##member##_SHIFT) & \
					RQ_CQE_STATUS_##member##_MASK)

#define HINIC3_GET_RX_CSUM_ERR(status)	RQ_CQE_STATUS_GET(status, CSUM_ERR)

#define HINIC3_GET_RX_DONE(status)	RQ_CQE_STATUS_GET(status, RXDONE)

#define HINIC3_GET_RX_FLUSH(status)	RQ_CQE_STATUS_GET(status, FLUSH)

#define HINIC3_GET_RX_BP_EN(status)	RQ_CQE_STATUS_GET(status, BP_EN)

#define HINIC3_GET_RX_NUM_LRO(status)	RQ_CQE_STATUS_GET(status, NUM_LRO)

#define HINIC3_RX_IS_DECRY_PKT(status)	RQ_CQE_STATUS_GET(status, DECRY_PKT)

#define RQ_CQE_SUPER_CQE_EN_SHIFT			0
#define RQ_CQE_PKT_NUM_SHIFT				1
#define RQ_CQE_PKT_LAST_LEN_SHIFT			6
#define RQ_CQE_PKT_FIRST_LEN_SHIFT			19

#define RQ_CQE_SUPER_CQE_EN_MASK			0x1
#define RQ_CQE_PKT_NUM_MASK				0x1FU
#define RQ_CQE_PKT_FIRST_LEN_MASK			0x1FFFU
#define RQ_CQE_PKT_LAST_LEN_MASK			0x1FFFU

#define RQ_CQE_PKT_NUM_GET(val, member)			(((val) >> \
					RQ_CQE_PKT_##member##_SHIFT) & \
					RQ_CQE_PKT_##member##_MASK)
#define HINIC3_GET_RQ_CQE_PKT_NUM(pkt_info) RQ_CQE_PKT_NUM_GET(pkt_info, NUM)

#define RQ_CQE_SUPER_CQE_EN_GET(val, member)		(((val) >> \
					RQ_CQE_##member##_SHIFT) & \
					RQ_CQE_##member##_MASK)
#define HINIC3_GET_SUPER_CQE_EN(pkt_info)	\
	RQ_CQE_SUPER_CQE_EN_GET(pkt_info, SUPER_CQE_EN)

#define RQ_CQE_PKT_LEN_GET(val, member)			(((val) >> \
						RQ_CQE_PKT_##member##_SHIFT) & \
						RQ_CQE_PKT_##member##_MASK)

#define RQ_CQE_DECRY_INFO_DECRY_STATUS_SHIFT	8
#define RQ_CQE_DECRY_INFO_ESP_NEXT_HEAD_SHIFT	0

#define RQ_CQE_DECRY_INFO_DECRY_STATUS_MASK	0xFFU
#define RQ_CQE_DECRY_INFO_ESP_NEXT_HEAD_MASK	0xFFU

#define RQ_CQE_DECRY_INFO_GET(val, member)		(((val) >> \
				RQ_CQE_DECRY_INFO_##member##_SHIFT) & \
				RQ_CQE_DECRY_INFO_##member##_MASK)

#define HINIC3_GET_DECRYPT_STATUS(decry_info)	\
	RQ_CQE_DECRY_INFO_GET(decry_info, DECRY_STATUS)

#define HINIC3_GET_ESP_NEXT_HEAD(decry_info)	\
	RQ_CQE_DECRY_INFO_GET(decry_info, ESP_NEXT_HEAD)

/* compact cqe field */
/* cqe dw0 */
#define RQ_COMPACT_CQE_STATUS_RXDONE_SHIFT		31
#define RQ_COMPACT_CQE_STATUS_CQE_TYPE_SHIFT		30
#define RQ_COMPACT_CQE_STATUS_TS_FLAG_SHIFT		29
#define RQ_COMPACT_CQE_STATUS_VLAN_EN_SHIFT		28
#define RQ_COMPACT_CQE_STATUS_PKT_FORMAT_SHIFT		25
#define RQ_COMPACT_CQE_STATUS_IP_TYPE_SHIFT		24
#define RQ_COMPACT_CQE_STATUS_CQE_LEN_SHIFT		23
#define RQ_COMPACT_CQE_STATUS_PKT_MC_SHIFT		21
#define RQ_COMPACT_CQE_STATUS_CSUM_ERR_SHIFT		19
#define RQ_COMPACT_CQE_STATUS_PKT_TYPE_SHIFT		16
#define RQ_COMPACT_CQE_STATUS_PTYPE_SHIFT		16
#define RQ_COMPACT_CQE_STATUS_PKT_LEN_SHIFT		0

#define RQ_COMPACT_CQE_STATUS_RXDONE_MASK		0x1U
#define RQ_COMPACT_CQE_STATUS_CQE_TYPE_MASK		0x1U
#define RQ_COMPACT_CQE_STATUS_TS_FLAG_MASK		0x1U
#define RQ_COMPACT_CQE_STATUS_VLAN_EN_MASK		0x1U
#define RQ_COMPACT_CQE_STATUS_PKT_FORMAT_MASK		0x7U
#define RQ_COMPACT_CQE_STATUS_IP_TYPE_MASK		0x1U
#define RQ_COMPACT_CQE_STATUS_CQE_LEN_MASK		0x1U
#define RQ_COMPACT_CQE_STATUS_PKT_MC_MASK		0x3U
#define RQ_COMPACT_CQE_STATUS_CSUM_ERR_MASK		0x3U
#define RQ_COMPACT_CQE_STATUS_PKT_TYPE_MASK		0x7U
#define RQ_COMPACT_CQE_STATUS_PTYPE_MASK		0xFFFU
#define RQ_COMPACT_CQE_STATUS_PKT_LEN_MASK		0xFFFFU

#define HINIC3_RQ_COMPACT_CQE_STATUS_GET(val, member) \
	((((val) >> RQ_COMPACT_CQE_STATUS_##member##_SHIFT) & \
	 	RQ_COMPACT_CQE_STATUS_##member##_MASK))

#define HINIC3_RQ_CQE_SEPARATE 	0
#define HINIC3_RQ_CQE_INTEGRATE 1

/* cqe dw2 */
#define RQ_COMPACT_CQE_OFFLOAD_NUM_LRO_SHIFT		24
#define RQ_COMPACT_CQE_OFFLOAD_VLAN_SHIFT		8

#define RQ_COMPACT_CQE_OFFLOAD_NUM_LRO_MASK		0xFFU
#define RQ_COMPACT_CQE_OFFLOAD_VLAN_MASK		0xFFFFU

#define HINIC3_RQ_COMPACT_CQE_OFFLOAD_GET(val, member) \
	(((val) >> RQ_COMPACT_CQE_OFFLOAD_##member##_SHIFT) & RQ_COMPACT_CQE_OFFLOAD_##member##_MASK)

#define HINIC3_RQ_COMPACT_CQE_16BYTE	0
#define HINIC3_RQ_COMPACT_CQE_8BYTE 	1

/* Rx cqe checksum err */
#define HINIC3_RX_CSUM_IP_CSUM_ERR	BIT(0)
#define HINIC3_RX_CSUM_TCP_CSUM_ERR	BIT(1)
#define HINIC3_RX_CSUM_UDP_CSUM_ERR	BIT(2)
#define HINIC3_RX_CSUM_IGMP_CSUM_ERR	BIT(3)
#define HINIC3_RX_CSUM_ICMPv4_CSUM_ERR	BIT(4)
#define HINIC3_RX_CSUM_ICMPv6_CSUM_ERR	BIT(5)
#define HINIC3_RX_CSUM_SCTP_CRC_ERR	BIT(6)
#define HINIC3_RX_CSUM_HW_CHECK_NONE	BIT(7)
#define HINIC3_RX_CSUM_IPSU_OTHER_ERR	BIT(8)

enum hinic3_compact_cqe_csum_err_type {
	HINIC3_RX_COMPACT_CSUM_NO_ERROR = 0,
	HINIC3_RX_COMPACT_L3_L4_CSUM_ERROR,
	HINIC3_RX_COMPACT_CSUM_OTHER_ERROR,
	HINIC3_RX_COMPACT_HW_BYPASS_ERROR
};

#define HINIC3_DEFAULT_RX_CSUM_OFFLOAD	0xFFF
#define HINIC3_CQE_LEN 32

#define HINIC3_RSS_OFFLOAD_ALL ( \
	ETH_RSS_IPV4 | \
	ETH_RSS_FRAG_IPV4 | \
	ETH_RSS_NONFRAG_IPV4_TCP | \
	ETH_RSS_NONFRAG_IPV4_UDP | \
	ETH_RSS_NONFRAG_IPV4_OTHER | \
	ETH_RSS_IPV6 | \
	ETH_RSS_FRAG_IPV6 | \
	ETH_RSS_NONFRAG_IPV6_TCP | \
	ETH_RSS_NONFRAG_IPV6_UDP | \
	ETH_RSS_NONFRAG_IPV6_OTHER | \
	ETH_RSS_IPV6_EX | \
	ETH_RSS_IPV6_TCP_EX | \
	ETH_RSS_IPV6_UDP_EX)

#define HINIC3_L4_PYTPE_SHIFT	16
#define HINIC3_COMPACT_CQE_PTYPE_SHIFT 16

#define HINIC3_DEFAULT_DESCS_PER_LOOP 4
#define HINIC3_DEFAULT_RX_BURST 64

/* keep same with IPSU_METADATA_L3_TP_E */
enum HINIC3_RX_CQE_PT_L3 {
	HINIC3_RX_CQE_L3_IPV4 = 0u,
	HINIC3_RX_CQE_L3_IPV6 = 1u,
};

/* keep same with IPSU_PKT_TYPE_L45FINAL_E */
enum HINIC3_RX_CQE_PT_L4 {
	HINIC3_RX_CQE_L4_TCP = 3,
	HINIC3_RX_CQE_L4_UDP = 4,
};

enum IPSU_METADATA_L3_TP_E {
	IPSU_METADATA_L3_TP_IPV4 = 0u,
	IPSU_METADATA_L3_TP_IPV6 = 1u,
};

enum IPSU_PKT_TYPE_L45FINAL_E {
	IPSU_PKT_TYPE_NULL = 0,
	IPSU_PKT_TYPE_ROCEV2,
	IPSU_PKT_TYPE_TCPCOCO,
	IPSU_PKT_TYPE_TCP,
	IPSU_PKT_TYPE_UDP,
	IPSU_PKT_TYPE_ICMP,
	IPSU_PKT_TYPE_IGMP,
	IPSU_PKT_TYPE_SCTP,
	IPSU_PKT_TYPE_DHCP,
	IPSU_PKT_TYPE_IPV4_FRAG,
	IPSU_PKT_TYPE_IPV6_MC,
	IPSU_PKT_TYPE_1588,
	IPSU_PKT_TYPE_AH_OVER_IP,
	IPSU_PKT_TYPE_ESP_OVER_IP,
	IPSU_PKT_TYPE_NATT,
	IPSU_PKT_TYPE_AH_OVER_VXLAN_GPE,
	IPSU_PKT_TYPE_ESP_OVER_VXLAN_GPE,
	IPSU_PKT_TYPE_TSO,
	IPSU_PKT_TYPE_UFO,
	IPSU_PKT_TYPE_INT,
	IPSU_PKT_TYPE_IOAM,
	IPSU_PKT_TYPE_VXLAN_GPE,
	IPSU_PKT_TYPE_GENEVE,
	IPSU_PKT_TYPE_NSH_OVER_GENEVE,
	IPSU_PKT_TYPE_NSH_OVER_VXLAN_GPE,
	IPSU_PKT_TYPE_ESP_OVER_GENEVE,
	IPSU_PKT_TYPE_PPOP_OVER_GENEVE,
	IPSU_PKT_TYPE_OSPF,
	IPSU_PKT_TYPE_VRRP,
	IPSU_PKT_TYPE_BGP,
	IPSU_PKT_TYPE_GRE,
	IPSU_ERR_RSVD,
};

enum IPSU_METADATA_FMT_E {
	IPSU_METADATA_FMT_NO_ENC	= 0u,
	IPSU_METADATA_FMT_VXLAN		= 1u,
	IPSU_METADATA_FMT_NVGRE		= 2u,
	IPSU_METADATA_FMT_FC		= 3u,
	IPSU_METADATA_FMT_GPE		= 4u,
	IPSU_METADATA_FMT_GENEVE	= 5u,
	IPSU_METADATA_FMT_NSH		= 6u,
	IPSU_METADATA_FMT_IPIP		= 7U,
};

struct hinic3_rxq_stats {
	u64 packets;
	u64 bytes;
	u64 errors;
	u64 csum_errors;
	u64 other_errors;
	u64 unlock_bp;
	u64 dropped;

	u64 rx_nombuf;
	u64 rx_discards;
	u64 burst_pkts;
	u64 empty;
	u64 tsc;
#ifdef HINIC3_XSTAT_MBUF_USE
	u64 alloc_mbuf;
	u64 free_mbuf;
	u64 left_mbuf;
#endif

#ifdef HINIC3_XSTAT_RXBUF_INFO
	u64 rx_mbuf;
	u64 rx_avail;
	u64 rx_hole;
#endif

#ifdef HINIC3_XSTAT_PROF_RX
	u64 app_tsc;
	u64 pmd_tsc;
#endif
};

struct hinic3_rq_cqe {
	u32 status;
	u32 vlan_len;

	u32 offload_type;
	u32 hash_val;
	u32 mark_id_0;
	u32 mark_id_1;
	u32 mark_id_2;
	u32 pkt_info;
} __rte_cache_aligned;

struct hinic3_cqe_info {
	u8 data_offset;
	u8 lro_num;
	u8 vlan_offload;
	u8 cqe_len;
	u8 cqe_type;
	u8 ts_flag;

	u16 csum_err;
	u16 vlan_tag;
	u16 ptype;
	u16 pkt_len;
	u16 rss_type;

	u32 rss_hash_value;
};

/*
 * Attention: please do not add any member in hinic3_rx_info because rxq bulk
 * rearm mode will write mbuf in rx_info
 */
struct hinic3_rx_info {
	struct rte_mbuf *mbuf;
};

struct hinic3_sge_sect {
	struct hinic3_sge sge;
	u32 rsvd;
};

struct hinic3_rq_extend_wqe {
	struct hinic3_sge_sect buf_desc;
	struct hinic3_sge_sect cqe_sect;
};

struct hinic3_rq_normal_wqe {
	u32 buf_hi_addr;
	u32 buf_lo_addr;
	u32 cqe_hi_addr;
	u32 cqe_lo_addr;
};

struct hinic3_rq_compact_wqe {
	u32 buf_hi_addr;
	u32 buf_lo_addr;
};

struct hinic3_rq_wqe {
	union {
		struct hinic3_rq_compact_wqe compact_wqe;
		struct hinic3_rq_normal_wqe normal_wqe;
		struct hinic3_rq_extend_wqe extend_wqe;
	};
};

struct hinic3_rq_ci_wb {
	union {
		struct {
			u16 cqe_num;
			u16 hw_ci;
		} bs;
		u32 value;
	} dw1;

	u32 rsvd[3];
};

struct hinic3_rxq {
	/* Cache Line 0 : RX Fast Path Hot Data */
	struct hinic3_nic_dev *nic_dev;

	struct hinic3_rx_info *rx_info;
	struct hinic3_rq_cqe *rx_cqe;
	struct rte_mempool *mb_pool;

	u64 wait_time_cycle;

	u16 cons_idx;
	u16 prod_idx;

	u16 q_mask;
	u16 buf_len;

	u16 next_to_update;
	u16 delta;

	u16 port_id;
	u8  is_scattered_rx;
	u8  dp_intr_en;

	u16 hw_cons_idx;
	bool prefetch_flag;
	/* Cache Line 1 : Queue Configuration */
	u16 q_id;
	u16 local_qid;

	u16 q_depth;
	u16 rx_free_thresh;

	u16 rxinfo_align_end;

	u16 wqebb_shift;
	u16 wqebb_size;

	u16 wqe_type;

	u16 msix_entry_idx;

	u32 rx_buff_shift;

	unsigned long status;

	bool is_hairpin;
	struct rte_eth_hairpin_conf hairpin_conf;

	/* Cache Line 2+ : DMA Resources / Setup Only */
	const struct rte_memzone *rq_mz;
	void *queue_buf_vaddr; /* Rq dma info */
	rte_iova_t queue_buf_paddr;

	const struct rte_memzone *pi_mz;
	u16 *pi_virt_addr;
	void *db_addr;
	rte_iova_t pi_dma_addr;

	const struct rte_memzone *ci_mz;
	struct hinic3_rq_ci_wb *rq_ci;
	rte_iova_t rq_ci_paddr;

	const struct rte_memzone *cqe_mz;
	void *cqe_start_vaddr;
	rte_iova_t cqe_start_paddr;

	/* Statistics (cold write path) */
	struct hinic3_rxq_stats rxq_stats;

#ifdef HINIC3_XSTAT_PROF_RX
	/* performance profiling */
	uint64_t prof_rx_end_tsc;
#endif
} __rte_cache_aligned;

int hinic3_rx_fill_wqe(struct hinic3_rxq *rxq);

u32 hinic3_rx_fill_buffers(struct hinic3_rxq *rxq);

void hinic3_free_rxq_mbufs(struct hinic3_rxq *rxq);

void hinic3_free_all_rxq_mbufs(struct hinic3_nic_dev *nic_dev);

int hinic3_update_rss_config(struct rte_eth_dev *dev,
			     struct rte_eth_rss_conf *rss_conf);

int hinic3_init_rx_ptype_table(struct rte_eth_dev *dev);

int hinic3_stop_rq(struct rte_eth_dev *eth_dev, struct hinic3_rxq *rxq);

int hinic3_start_rq(struct rte_eth_dev *eth_dev, struct hinic3_rxq *rxq);

u16 hinic3_recv_pkts_compact_cqe(void *rx_queue, struct rte_mbuf **rx_pkts, u16 nb_pkts);

u16 hinic3_recv_pkts(void *rx_queue, struct rte_mbuf **rx_pkts, u16 nb_pkts);

u16 hinic3_recv_pkts_vec(void *rx_queue, struct rte_mbuf **rx_pkts, u16 nb_pkts);

void hinic3_add_rq_to_rx_queue_list(struct hinic3_nic_dev *nic_dev,
				    u16 queue_id);

int hinic3_refill_indir_rqid(struct hinic3_rxq *rxq);

void hinic3_init_rx_queue_list(struct hinic3_nic_dev *nic_dev);

void hinic3_remove_rq_from_rx_queue_list(struct hinic3_nic_dev *nic_dev,
					 u16 queue_id);
int hinic3_start_all_rqs(struct rte_eth_dev *eth_dev);

#ifdef HINIC3_XSTAT_RXBUF_INFO
void hinic3_get_stats(struct hinic3_rxq *rxq);
#endif

#endif /* _HINIC3_PMD_RX_H_ */

