/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2019 Huawei Technologies Co., Ltd
 */

#ifndef _HINIC3_PMD_NIC_IO_H_
#define _HINIC3_PMD_NIC_IO_H_

#include "hinic3_pmd_cmdq.h"
#include "hinic3_pmd_ethdev.h"

#define HINIC3_SQ_WQEBB_SHIFT			4
#define HINIC3_RQ_WQEBB_SHIFT			3

#define HINIC3_SQ_WQEBB_SIZE	BIT(HINIC3_SQ_WQEBB_SHIFT)
#define HINIC3_CQE_SIZE_SHIFT			4

/* Ci addr should RTE_CACHE_SIZE(64B) alignment for performance */
#define HINIC3_CI_Q_ADDR_SIZE			64

#define CI_TABLE_SIZE(num_qps, pg_sz)	\
			(RTE_ALIGN((num_qps) * HINIC3_CI_Q_ADDR_SIZE, pg_sz))

#define HINIC3_CI_VADDR(base_addr, q_id)	((u8 *)(base_addr) + \
						(q_id) * HINIC3_CI_Q_ADDR_SIZE)

#define HINIC3_CI_PADDR(base_paddr, q_id)	((base_paddr) + \
						(q_id) * HINIC3_CI_Q_ADDR_SIZE)

#define HINIC3_Q_CTXT_MAX	((u16)(((HINIC3_CMDQ_BUF_SIZE - 8) - RTE_PKTMBUF_HEADROOM) / 64))

#define HINIC3_FLUSH_QUEUE_TIMEOUT	3000

#define HINIC3_DEAULT_DROP_THD_ON		0xFFFF

#define SQ_CTXT_PKT_DROP_THD_ON_SHIFT			0
#define SQ_CTXT_PKT_DROP_THD_OFF_SHIFT			16

#define SQ_CTXT_PKT_DROP_THD_ON_MASK			0xFFFFU
#define SQ_CTXT_PKT_DROP_THD_OFF_MASK			0xFFFFU

#define SQ_CTXT_PKT_DROP_THD_SET(val, member)		(((val) & \
					SQ_CTXT_PKT_DROP_##member##_MASK) \
					<< SQ_CTXT_PKT_DROP_##member##_SHIFT)
#define SQ_CTXT_PREF_CACHE_THRESHOLD_SHIFT		0
#define SQ_CTXT_PREF_CACHE_MAX_SHIFT			14
#define SQ_CTXT_PREF_CACHE_MIN_SHIFT			25

#define SQ_CTXT_PREF_CACHE_THRESHOLD_MASK		0x3FFFU
#define SQ_CTXT_PREF_CACHE_MAX_MASK			0x7FFU
#define SQ_CTXT_PREF_CACHE_MIN_MASK			0x7FU

#define SQ_CTXT_PREF_SET(val, member)			(((val) & \
					SQ_CTXT_PREF_##member##_MASK) \
					<< SQ_CTXT_PREF_##member##_SHIFT)
#define RQ_CTXT_PREF_CACHE_THRESHOLD_SHIFT		0
#define RQ_CTXT_PREF_CACHE_MAX_SHIFT			14
#define RQ_CTXT_PREF_CACHE_MIN_SHIFT			25

#define RQ_CTXT_PREF_CACHE_THRESHOLD_MASK		0x3FFFU
#define RQ_CTXT_PREF_CACHE_MAX_MASK			0x7FFU
#define RQ_CTXT_PREF_CACHE_MIN_MASK			0x7FU

#define RQ_CTXT_PREF_SET(val, member)			(((val) & \
					RQ_CTXT_PREF_##member##_MASK) << \
					RQ_CTXT_PREF_##member##_SHIFT)

#define RQ_CTXT_CEQ_ATTR_SET(val, member)		(((val) & \
					RQ_CTXT_CEQ_ATTR_##member##_MASK) \
					<< RQ_CTXT_CEQ_ATTR_##member##_SHIFT)

enum hinic3_qp_ctxt_type {
	HINIC3_QP_CTXT_TYPE_SQ,
	HINIC3_QP_CTXT_TYPE_RQ,
};

enum hinic3_rq_wqe_type {
	HINIC3_COMPACT_RQ_WQE,
	HINIC3_NORMAL_RQ_WQE,
	HINIC3_EXTEND_RQ_WQE
};

enum hinic3_queue_type {
	HINIC3_SQ,
	HINIC3_RQ,
	HINIC3_MAX_QUEUE_TYPE
};

/* Doorbell info */
struct hinic3_db {
	u32 db_info;
	u32 pi_hi;
};

struct hinic3_sq_ctxt {
	u32 ci_pi;
	u32 drop_mode_sp;
	u32 wq_pfn_hi_owner;
	u32 wq_pfn_lo;

	u32 rsvd0;
	u32 pkt_drop_thd;
	u32 global_sq_id;
	u32 vlan_ceq_attr;

	u32 pref_cache;
	u32 pref_ci_owner;
	u32 pref_wq_pfn_hi_ci;
	u32 pref_wq_pfn_lo;

	u32 rsvd8;
	u32 rsvd9;
	u32 wq_block_pfn_hi;
	u32 wq_block_pfn_lo;
};

struct hinic3_rq_ctxt {
	u32 ci_pi;
	u32 ceq_attr;
	u32 wq_pfn_hi_type_owner;
	u32 wq_pfn_lo;

	u32 rsvd[3];
	u32 cqe_sge_len;

	u32 pref_cache;
	u32 pref_ci_owner;
	u32 pref_wq_pfn_hi_ci;
	u32 pref_wq_pfn_lo;

	u32 pi_paddr_hi;
	u32 pi_paddr_lo;
	u32 wq_block_pfn_hi;
	u32 wq_block_pfn_lo;
};

struct hinic3_rq_cqe_ctx {
	struct mgmt_msg_head msg_head;

	u8 cqe_type;
	u8 rq_id;
	u8 threshold_cqe_num;
	u8 rsvd1;

	u16 msix_entry_idx;
	u16 rsvd2;

	u32 ci_addr_hi;
	u32 ci_addr_lo;

	u16 timer_loop;
	u16 rsvd3;
};

struct hinic3_rq_enable {
	struct mgmt_msg_head msg_head;

	u32 rq_id;
	u8 rq_enable;
	u8 rsvd[3];
};

/* Prepare cmd to clean tso/lro space */
typedef uint8_t  (*prepare_cmd_buf_clean_tso_lro_space_t)(struct hinic3_nic_dev *nic_dev,
							struct hinic3_cmd_buf *cmd_buf,
							enum hinic3_qp_ctxt_type ctxt_type);
/* Prepare cmd to store RQ and TQ ctxt */
typedef uint8_t  (*prepare_cmd_buf_qp_context_multi_store_t)(struct hinic3_nic_dev *nic_dev,
							struct hinic3_cmd_buf *cmd_buf,
							enum hinic3_qp_ctxt_type ctxt_type,
							uint16_t start_qid, uint16_t max_ctxts);
/* Prepare cmd to modify vlan tag */
typedef uint8_t  (*prepare_cmd_buf_modify_svlan_t)(struct hinic3_cmd_buf *cmd_buf, 
						   uint16_t func_id,
						   uint16_t vlan_tag,
						   uint16_t q_id,
						   uint8_t vlan_mode);
/* Prepare cmd to set RSS indir table */
typedef uint8_t  (*prepare_cmd_buf_set_rss_indir_table_t)(struct hinic3_nic_dev *nic_dev,
							  const uint32_t *indir_table,
							  struct hinic3_cmd_buf *cmd_buf);
/* Prepare cmd to get RSS indir table */
typedef uint8_t  (*prepare_cmd_buf_get_rss_indir_table_t)(struct hinic3_nic_dev *nic_dev,
							  struct hinic3_cmd_buf *cmd_buf);
/* Configure RSS indir table */
typedef void     (*cmd_buf_to_rss_indir_table_t)(const struct hinic3_cmd_buf *cmd_buf,
						 uint32_t *indir_table);
typedef void     (*cmd_buf_to_rss_indir_table_qpool_t)(const struct hinic3_cmd_buf *cmd_buf,
						 uint32_t *indir_table);
typedef void	 (*prepare_rq_ctxt_ceq_and_prefetch_t)(struct hinic3_rq_ctxt *rq_ctxt,
						       u16 wqe_type,
						       u16 msix_entry_idx,
						       bool support_rq_sw_compact_cqe,
						       u8 intr_disable);

typedef void	 (*prepare_sq_ctxt_drop_and_prefetch_t)(struct hinic3_sq_ctxt *sq_ctxt);

struct hinic3_nic_cmdq_ops {
	prepare_cmd_buf_clean_tso_lro_space_t		prepare_cmd_buf_clean_tso_lro_space;
	prepare_cmd_buf_qp_context_multi_store_t	prepare_cmd_buf_qp_context_multi_store;
	prepare_cmd_buf_modify_svlan_t			prepare_cmd_buf_modify_svlan;
	prepare_cmd_buf_set_rss_indir_table_t		prepare_cmd_buf_set_rss_indir_table;
	prepare_cmd_buf_get_rss_indir_table_t		prepare_cmd_buf_get_rss_indir_table;
	cmd_buf_to_rss_indir_table_t			cmd_buf_to_rss_indir_table;
	cmd_buf_to_rss_indir_table_qpool_t		cmd_buf_to_rss_indir_table_qpool;
	prepare_rq_ctxt_ceq_and_prefetch_t		prepare_rq_ctxt_ceq_and_prefetch;
	prepare_sq_ctxt_drop_and_prefetch_t		prepare_sq_ctxt_drop_and_prefetch;
};

#define DB_INFO_QID_SHIFT			0
#define DB_INFO_NON_FILTER_SHIFT		22
#define DB_INFO_CFLAG_SHIFT			23
#define DB_INFO_COS_SHIFT			24
#define DB_INFO_TYPE_SHIFT			27

#define DB_INFO_QID_MASK			0x1FFFU
#define DB_INFO_NON_FILTER_MASK			0x1U
#define DB_INFO_CFLAG_MASK			0x1U
#define DB_INFO_COS_MASK			0x7U
#define DB_INFO_TYPE_MASK			0x1FU
#define DB_INFO_SET(val, member)		(((u32)(val) & \
					DB_INFO_##member##_MASK) << \
					DB_INFO_##member##_SHIFT)

#define DB_PI_LOW_MASK	0xFFU
#define DB_PI_HIGH_MASK	0xFFU
#define DB_PI_LOW(pi)	((pi) & DB_PI_LOW_MASK)
#define DB_PI_HI_SHIFT	8
#define DB_PI_HIGH(pi)	(((pi) >> DB_PI_HI_SHIFT) & DB_PI_HIGH_MASK)
#define DB_INFO_UPPER_32(val) (((u64)(val)) << 32)

#define DB_ADDR(db_addr, pi)	((u64 *)(db_addr) + DB_PI_LOW(pi))
#define SRC_TYPE		1

/* Cflag data path */
#define SQ_CFLAG_DP		0
#define RQ_CFLAG_DP		1

#define MASKED_QUEUE_IDX(queue, idx) ((idx) & (queue)->q_mask)

#define	NIC_WQE_ADDR(queue, idx) ((void *)((u64)((queue)->queue_buf_vaddr) + \
				       ((idx) << (queue)->wqebb_shift)))

/**
 * Write send queue doorbell
 *
 * @param[in] db_addr
 *   Doorbell address
 * @param[in] q_id
 *   Send queue id
 * @param[in] cos
 *   Send queue cos
 * @param[in] cflag
 *   Cflag data path
 * @param[in] pi
 *   Send queue pi
 */
static inline void hinic3_write_db(void *db_addr, u16 q_id, int cos, u8 cflag,
				   u16 pi)
{
	u64 db;

	/* Hardware will do endianness coverting */
	db = DB_PI_HIGH(pi);
	db = DB_INFO_UPPER_32(db) | DB_INFO_SET(SRC_TYPE, TYPE) |
	     DB_INFO_SET(cflag, CFLAG) | DB_INFO_SET(cos, COS) |
	     DB_INFO_SET(q_id, QID);

	rte_wmb(); /* Write all before the doorbell */

	rte_write64(*((u64 *)&db), DB_ADDR(db_addr, pi));
}

void hinic3_get_func_rx_buf_size(void *dev);

/**
 * Init queue pair context
 *
 * @param[in] dev
 *   Device pointer to nic device
 *
 * @retval zero: Success
 * @retval non-zero: Failure
 */
int hinic3_init_qp_ctxts(void *dev);

/**
 * Initialize RQ integrated CQE context
 *
 * @param[in] nic_dev
 * Pointer to ethernet device structure.
 *
 * @return
 * 0 on success, non-zero on failure.
 */
int hinic3_init_rq_cqe_ctxts(struct hinic3_nic_dev *nic_dev);

/**
 * Free queue pair context
 *
 * @param[in] hwdev
 * Device pointer to hwdev
 */
void hinic3_free_qp_ctxts(void *hwdev);

/**
 * Set RQ disable or enable
 *
 * @param[in] nic_dev
 * Pointer to ethernet device structure.
 * @param[in] q_id
 * Receive queue id.
 * @param[in] enable
 *   1: enable  0: disable
 * @return
 * 0 on success, non-zero on failure.
 */
int hinic3_set_rq_enable(struct hinic3_nic_dev *nic_dev, u16 q_id, bool enable);

/**
 * Update service feature driver supported
 *
 * @param[in] dev
 *   Device pointer to nic device
 * @param[out] s_feature
 *   s_feature driver supported
 * @retval zero: Success
 * @retval non-zero: Failure
 */
void hinic3_update_driver_feature(void *dev, u64 s_feature);

/**
 * Get service feature driver supported
 *
 * @param[in] dev
 *   Device pointer to nic device
 * @param[out] s_feature
 *   s_feature driver supported
 * @retval zero: Success
 * @retval non-zero: Failure
 */
u64 hinic3_get_driver_feature(void *dev);

/**
 * Prepare sq context
 *
 * @param[in] sq
 *   Pointer to sq
 * @param[in] sq_id
 *   Specific sq id
 * @param[out] sq_ctxt
 *   Pointer to sq context
 */
void hinic3_sq_prepare_ctxt(struct hinic3_txq *sq, u16 sq_id, struct hinic3_sq_ctxt *sq_ctxt);

/**
 * Prepare rq context
 *
 * @param[in] rq
 *   Pointer to rq
 * @param[out] rq_ctxt
 *   Pointer to rq context
 */
void hinic3_rq_prepare_ctxt(struct hinic3_rxq *rq, struct hinic3_rq_ctxt *rq_ctxt);

/**
 * Get cmdq ops software tile NIC(stn) supported.
 *
 * @return
 * Pointer to ops.
 */
struct hinic3_nic_cmdq_ops *hinic3_nic_cmdq_get_stn_ops(void);

/**
 * Get cmdq ops hardware tile NIC(htn) supported.
 *
 * @retval Pointer to ops.
 */
struct hinic3_nic_cmdq_ops *hinic3_nic_cmdq_get_htn_ops(void);

/**
 * Determine whether specific service is set
 *
 * @param[in] dev
 *   Device pointer to nic device
 * @param[in] feature_bit
 *   Specific service bit
 * @retval Zero: disabled
 * @retval non-zero: enabled
 */
u8 hinic3_get_driver_feature_bit(void *dev, u64 feature_bit);

#endif /* _HINIC3_PMD_NIC_IO_H_ */

