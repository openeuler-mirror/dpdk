/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
 */

#ifndef _HINIC3_PMD_MML_LIB
#define _HINIC3_PMD_MML_LIB

#include <string.h>
#include <stdint.h>
#include <net/if.h>

#include "hinic3_pmd_mml_cmd.h"
#include "hinic3_compat.h"
#include "hinic3_pmd_mgmt.h"
#include "hinic3_pmd_nic_cfg.h"

#define MAX_DEV_LEN	      16
#define MAX_ADDR_LEN	      32
#define TRGET_UNKNOWN_BUS_NUM (-1)
#define HINIC3_PMD_MML_RET(ret)             \
	do {                                \
		if ((ret) != UDA_SUCCESS) { \
			return (ret);       \
		}                           \
	} while (0)

#ifndef DEV_NAME_LEN
#define DEV_NAME_LEN 64
#endif

enum {
	UDA_SUCCESS = 0x0,
	UDA_FAIL,
	UDA_ENXIO,
	UDA_ENONMEM,
	UDA_EBUSY,
	UDA_ECRC,
	UDA_EINVAL,
	UDA_EFAULT,
	UDA_ELEN,
	UDA_ECMD,
	UDA_ENODRIVER,
	UDA_EXIST,
	UDA_EOVERSTEP,
	UDA_ENOOBJ,
	UDA_EOBJ,
	UDA_ENOMATCH,
	UDA_ETIMEOUT,

	UDA_CONTOP,

	UDA_REBOOT = 0xFD,
	UDA_CANCEL = 0xFE,
	UDA_KILLED = 0xFF,
};

#define PARAM_NEED     1
#define PARAM_NOT_NEED 0

#define BASE_ALL 0
#define BASE_8	 8
#define BASE_10	 10
#define BASE_16	 16

enum module_name {
	SEND_TO_NPU = 1,
	SEND_TO_MPU,
	SEND_TO_SM,

	SEND_TO_HW_DRIVER,
	SEND_TO_NIC_DRIVER,
	SEND_TO_OVS_DRIVER,
	SEND_TO_ROCE_DRIVER,
	SEND_TO_TOE_DRIVER,
	SEND_TO_IWAP_DRIVER,
	SEND_TO_FC_DRIVER,
	SEND_FCOE_DRIVER,
	SEND_TO_BIFUR_DRIVER = 23,
};

enum driver_cmd_type {
	TX_INFO = 1,
	Q_NUM,
	TX_WQE_INFO,
	TX_MAPPING,
	RX_INFO,
	RX_WQE_INFO,
	RX_CQE_INFO
};

struct tool_target {
	int bus_num;
	char dev_name[MAX_DEV_LEN];
	void *pri;
};

struct nic_tx_hw_page {
	long long phy_addr;
	long long *map_addr;
};

struct nic_sq_info {
	unsigned short q_id;
	unsigned short pi; /* ring buffer queue producer point */
	unsigned short ci; /* ring buffer queue consumer point */
	unsigned short fi; /* ring buffer queue complete point */
	unsigned int sq_depth;
	unsigned short sq_wqebb_size;
	unsigned short *ci_addr;
	unsigned long long cla_addr;

	struct nic_tx_hw_page doorbell;
	unsigned int page_idx;
};

struct comm_info_l2nic_sq_ci_attr {
	struct mgmt_msg_head msg_head;

	uint16_t func_idx;
	uint8_t dma_attr_off;
	uint8_t pending_limit;

	uint8_t coalescing_time;
	uint8_t int_en;
	uint16_t int_offset;

	uint32_t l2nic_sqn;
	uint32_t rsv;
	uint64_t ci_addr;
};

struct nic_rq_info {
	unsigned short q_id; /* queue id in current function, 0, 1, 2... */

	unsigned short hw_pi;	      /* where pkt buf allocated */
	unsigned short ci;	      /* where hw pkt received, owned by hw */
	unsigned short sw_pi;	      /* where driver begin receive pkt */
	unsigned short rq_wqebb_size; /* wqebb size, default to 32 bytes */

	unsigned short rq_depth;
	unsigned short buf_len; /* 2K */
	void *ci_wqe_page_addr; /* for queue context init */
	void *ci_cla_tbl_addr;
	unsigned short int_num;	  /* when support rss, the int_num should consider */
	unsigned int msix_vector; /* for debug */
};

struct hinic_wqe_info {
	int q_id;
	void *slq_handle;
	uint32_t wqe_id;
	uint32_t wqebb_cnt;
};

struct npu_cmd_st {
	uint32_t mod : 8;
	uint32_t cmd : 8;
	uint32_t ack_type : 3;
	uint32_t direct_resp : 1;
	uint32_t len : 12;
};

struct mpu_cmd_st {
	uint32_t api_type : 8;
	uint32_t mod : 8;
	uint32_t cmd : 16;
};

struct msg_module {
	char device_name[IFNAMSIZ];
	uint32_t module;
	union {
		uint32_t msg_formate; /* for driver */
		struct npu_cmd_st npu_cmd;
		struct mpu_cmd_st mpu_cmd;
	};
	uint32_t timeout; /* for mpu/npu cmd */
	uint32_t func_idx;
	uint32_t buf_in_size;
	uint32_t buf_out_size;
	void *in_buf;
	void *out_buf;
	int bus_num;
	uint32_t lcore_id;
	uint16_t qid;
	uint16_t rsvd1;
	uint32_t rsvd2[3];
};


enum nic_driver_qpool_cmd_type {
	GET_USER_QUEUE_ID,    /**< 队列池化中获取用户态队列ID */
	DEL_USER_QUEUE_ID,    /**< 队列池化中删除用户态队列ID */
	CFG_RSS_TEMPLATE,     /**< 队列池化中申请/释放q group id以及RSS模板 */
	SET_RSS_INDIR_TBL,    /**< 队列池化中设置RSS间接表 */
	GET_KERN_DEV_DATA,    /**< 队列池化中获取rx、tx队列深度、mtu、netdev_state  */
	GET_RSS_INDIR_TBL,    /**< 队列池化中获取RSS间接表  */
};

struct cdev_msg_head {
	int status;
	u32 rsvd;
};

#define HINIC3_RSS_INDIR_SIZE		256
struct nic_rss_indirect_tbl {
	union {
		struct {
#if defined(BYTE_ORDER) && (BYTE_ORDER == BIG_ENDIAN)
			u32 op_code : 8; /* 0:old option 1:new fdir-rss */
			u32 qgrp_id : 8; /* The value of qgrp_id must be 2048 less */
			u32 rsvd : 16;
#else
			u32 rsvd : 16;
			u32 qgrp_id : 8; /* The value of qgrp_id must be 2048 less */
			u32 op_code : 8; /* 0:old option 1:new fdir-rss */
#endif
		} bs;
		u32 value;
	} dw0;
	union {
		struct {
#if defined(BYTE_ORDER) && (BYTE_ORDER == BIG_ENDIAN)
			u32 rss_temp_id : 12;
			u32 rss_instance_id : 6;
			u32 rss_node_id : 5;
			u32 rsvd0 : 1;
			u32 rss_level : 2;
			u32 rsvd1 : 6;
#else
			u32 rsvd1 : 6;
			u32 rss_level : 2;
			u32 rsvd0 : 1;
			u32 rss_node_id : 5;
			u32 rss_instance_id : 6;
			u32 rss_temp_id : 12;
#endif
		} fdir_rss;
		u32 value;
	} dw1;
	u32 rsvd0[2]; /* Make sure that 16B before entry[] */
	u16 entry[HINIC3_RSS_INDIR_SIZE];
};

struct drv_cmd_rss_indir_tbl {
	union {
		struct cdev_msg_head head;
		struct {
			u16 pid;
			u16 func_id;
			u16 indir_table_size;
			u16 rsvd;
		}; /*SP230 QPOOL */
	};
	struct nic_rss_indirect_tbl rss_indir;
};

struct drv_cmd_user_queue_get {
	struct cdev_msg_head head;
	u16 qid;
	u16 local_qid;
	u16 lcore_id;
	u16 func_id;
	u32 rsvd[14];
};

struct drv_cmd_cfg_rss_temp {
	struct cdev_msg_head head;
	u16 opcode;
	u16 q_grp_id;
	u32 rsvd1[15];
};

struct drv_cmd_kernel_nic_data {
	struct cdev_msg_head head;
	u32 rx_q_depth;
	u32 tx_q_depth;
	u16 mtu;
	u16 netdev_state;
	u8 dev_addr[MAX_ADDR_LEN];
	u8 group_num;
	u8 rsvd0;
	u16 rsvd1;
	u32 rsvd[4];
};

/*
 * string_tol - string to uint32_t
 * @nptr: the string
 * @value: the output value
 */
static __inline__ int
string_toui(const char *nptr, int base, uint32_t *value)
{
	char *endptr = NULL;
	long int tmp_value;

	tmp_value = strtol(nptr, &endptr, base);
	if ((*endptr != 0) || tmp_value >= 0x7FFFFFFF || tmp_value < 0) {
		return -UDA_EINVAL;
	}
	*value = (uint32_t)tmp_value;
	return UDA_SUCCESS;
}

#define UDA_TRUE  1
#define UDA_FALSE 0

static inline void
__rte_format_printf(3, 4)
hinic3_pmd_mml_log(char *show_str, int *show_len, const char *fmt, ...)
{
	va_list args;
	int ret = 0;
	int remaining_len;

	if (*show_len >= MAX_SHOW_STR_LEN)
		return;

	remaining_len = MAX_SHOW_STR_LEN - *show_len;

	va_start(args, fmt);
	ret = vsnprintf(show_str + *show_len, remaining_len, fmt, args);
	va_end(args);

	if (ret > 0 && ret < remaining_len) {
		*show_len += ret;
	} else {
		PMD_DRV_LOG(ERR, "MML show string snprintf failed, err: %d\n", ret);
	}
}

int tool_get_valid_target(char *name, struct tool_target *target);
int hinic3_pmd_mml_ioctl(void *msg);
int lib_tx_sq_info_get(struct tool_target target, struct nic_sq_info *sq_info, int sq_id);
int lib_tx_wqe_info_get(struct tool_target target,
			struct nic_sq_info *sq_info, int sq_id, int wqe_id, void *nwqe, int nwqe_size);
int lib_rx_rq_info_get(struct tool_target target, struct nic_rq_info *rq_info, int rq_id);
int lib_rx_wqe_info_get(struct tool_target target,
			struct nic_rq_info *rq_info, int rq_id, int wqe_id, void *nwqe, int nwqe_size);
int lib_rx_cqe_info_get(struct tool_target target,
			struct nic_rq_info *rq_info, int rq_id, int wqe_id, void *nwqe, int nwqe_size);
int hinic3_pmd_mml_lib(const char *buf_in, uint32_t in_size, char *buf_out, uint32_t *out_len,
		       uint32_t max_buf_out_len);
#endif /* HINIC3_PMD_MML_LIB */
