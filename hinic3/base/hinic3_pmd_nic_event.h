/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2019 Huawei Technologies Co., Ltd
 */

#ifndef _HINIC3_PMD_NIC_EVENT_H_
#define _HINIC3_PMD_NIC_EVENT_H_

void get_port_info(struct hinic3_hwdev *hwdev, u8 link_state,
		   struct rte_eth_link *link);

void hinic3_pf_event_handler(void *hwdev, __rte_unused void *pri_handle,
			     u16 cmd, void *buf_in, u16 in_size,
			     void *buf_out, u16 *out_size);

int hinic3_vf_event_handler(void *hwdev, __rte_unused void *pri_handle,
			    u16 cmd, void *buf_in, u16 in_size,
			    void *buf_out, u16 *out_size);

void hinic3_pf_mag_event_handler(void *hwdev, void *pri_handle, u16 cmd,
				void *buf_in, u16 in_size, void *buf_out,
				u16 *out_size);

int hinic3_vf_mag_event_handler(void *hwdev, void *pri_handle, u16 cmd,
			       void *buf_in, u16 in_size, void *buf_out,
			       u16 *out_size);

u8 hinic3_nic_sw_aeqe_handler(__rte_unused void *hwdev, u8 event, u8 *data);

#endif /* _HINIC3_PMD_NIC_EVENT_H_ */

