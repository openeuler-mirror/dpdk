/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2019 Huawei Technologies Co., Ltd
 */

#ifndef _HINIC3_PMD_HWDEV_H_
#define _HINIC3_PMD_HWDEV_H_

#include <rte_ether.h>
#include <rte_memory.h>
#include <hinic3_pmd_mgmt.h>
struct cfg_mgmt_info;

struct hinic3_hwif;
struct hinic3_aeqs;
struct hinic3_mbox;
struct hinic3_msg_pf_to_mgmt;

#define MGMT_VERSION_MAX_LEN		32

enum hinic3_set_arm_type {
	HINIC3_SET_ARM_CMDQ,
	HINIC3_SET_ARM_SQ,
	HINIC3_SET_ARM_TYPE_NUM
};

enum hinic3_qinfo_type {
	HINIC3_QINFO_TYPE_NORMAL,
	HINIC3_QINFO_TYPE_BIFUR,
	HINIC3_QINFO_TYPE_QPOOL,
	HINIC3_QINFO_TYPE_RSV,
};

enum {
	HINIC3_F_API_CHAIN = 1U << 0,
	HINIC3_F_CLP = 1U << 1,
	HINIC3_F_CHANNEL_DETECT = 1U << 2,
	HINIC3_F_MBOX_SEGMENT = 1U << 3,
	HINIC3_F_CMDQ_NUM = 1U << 4,
	HINIC3_F_VIRTIO_VQ_SIZE = 1U << 5,
	HINIC3_F_EXTEND_CAP = 1U << 6,
	HINIC3_F_SMF_CACHE_INVALID = 1U << 7,
	HINIC3_F_ONLY_ENHANCE_CMDQ = 1U << 8,
	HINIC3_F_USE_REAL_RX_BUF_SIZE = 1U << 9,
};

struct hinic3_page_addr {
	void *virt_addr;
	u64 phys_addr;
};

struct ffm_intr_info {
	u8 node_id;
	/* Error level of the interrupt source */
	u8 err_level;
	/* Classification by interrupt source properties */
	u16 err_type;
	u32 err_csr_addr;
	u32 err_csr_value;
};

struct link_event_stats {
	rte_atomic32_t link_down_stats;
	rte_atomic32_t link_up_stats;
};

enum hinic3_fault_err_level {
	FAULT_LEVEL_FATAL,
	FAULT_LEVEL_SERIOUS_RESET,
	FAULT_LEVEL_SERIOUS_FLR,
	FAULT_LEVEL_GENERAL,
	FAULT_LEVEL_SUGGESTION,
	FAULT_LEVEL_MAX
};

enum hinic3_fault_type {
	FAULT_TYPE_CHIP,
	FAULT_TYPE_UCODE,
	FAULT_TYPE_MEM_RD_TIMEOUT,
	FAULT_TYPE_MEM_WR_TIMEOUT,
	FAULT_TYPE_REG_RD_TIMEOUT,
	FAULT_TYPE_REG_WR_TIMEOUT,
	FAULT_TYPE_PHY_FAULT,
	FAULT_TYPE_MAX
};

struct fault_event_stats {
	rte_atomic32_t chip_fault_stats[22][FAULT_LEVEL_MAX];
	rte_atomic32_t fault_type_stat[FAULT_TYPE_MAX];
	rte_atomic32_t pcie_fault_stats;
};

struct hinic3_hw_stats {
	rte_atomic32_t heart_lost_stats;
	struct link_event_stats link_event_stats;
	struct fault_event_stats fault_event_stats;
};

#define HINIC3_CHIP_FAULT_SIZE		(110 * 1024)
#define MAX_DRV_BUF_SIZE		4096

#define HINIC3_SUPPORT_ONLY_ENHANCE_CMDQ(hwdev) \
	(((struct hinic3_hwdev *)hwdev)->features[0] & HINIC3_F_ONLY_ENHANCE_CMDQ)

struct nic_cmd_chip_fault_stats {
	u32 offset;
	u8 chip_fault_stats[MAX_DRV_BUF_SIZE];
};

struct hinic3_board_info {
	u8 board_type;
	u8 port_num;
	u8 port_speed;
	u8 pcie_width;
	u8 host_num;
	u8 pf_num;
	u16 vf_total_num;
	u8 tile_num;
	u8 qcm_num;
	u8 core_num;
	u8 work_mode;
	u8 service_mode;
	u8 pcie_mode;
	u8 boot_sel;
	u8 board_id;
	u32 cfg_addr;
	u32 service_en_bitmap;
	u8 scenes_id;
	u8 cfg_template_id;
	u16 rsvd0;
};

struct hinic3_hwdev {
	void *dev_handle; /* Pointer to hinic3_nic_dev */
	void *pci_dev; /* Pointer to rte_pci_device */
	void *eth_dev; /* Pointer to rte_eth_dev */

	uint16_t port_id;

	u32 wq_page_size;

	struct hinic3_hwif *hwif;
	struct cfg_mgmt_info *cfg_mgmt;

	struct hinic3_cmdqs *cmdqs;
	struct hinic3_aeqs *aeqs;
	struct hinic3_mbox *func_to_func;
	struct hinic3_msg_pf_to_mgmt *pf_to_mgmt;
	struct hinic3_hw_stats hw_stats;
	u8 *chip_fault_stats;

	struct hinic3_board_info board_info;
	char mgmt_ver[MGMT_VERSION_MAX_LEN];

	u16 max_vfs;
	u16 link_status;
	u8 vf_valid_status; /* vport_enable: 1, vport_disable: 0 */

	u64 features[HINIC3_MAX_FEATURE_QWORD];
	struct rte_mempool *cmd_buf_pool;
	enum hinic3_qinfo_type qinfo_type;
	u16 qpool_qgrp_id;
};

bool hinic3_is_vfio_iommu_enable(const struct rte_eth_dev *rte_dev);

int vf_handle_pf_comm_mbox(void *handle, __rte_unused void *pri_handle,
			   __rte_unused u16 cmd, __rte_unused void *buf_in,
			   __rte_unused u16 in_size, __rte_unused void *buf_out,
			   __rte_unused u16 *out_size);

void pf_handle_mgmt_comm_event(void *handle, __rte_unused void *pri_handle,
			       u16 cmd, void *buf_in, u16 in_size,
			       void *buf_out, u16 *out_size);

int hinic3_init_hwdev(struct hinic3_hwdev *hwdev);

void hinic3_free_hwdev(struct hinic3_hwdev *hwdev);

const struct rte_memzone *hinic3_dma_zone_reserve(const void *dev,
						  const char *ring_name,
						  uint16_t queue_id,
						  size_t size,
						  unsigned int align,
						  int socket_id);
int hinic3_memzone_free(const struct rte_memzone *mz);

#endif /* _HINIC3_PMD_HWDEV_H_ */
