/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2019 Huawei Technologies Co., Ltd
 */

#include <sys/ioctl.h>

#include <rte_ether.h>

#include "hinic3_compat.h"
#include "hinic3_pmd_cmd.h"
#include "hinic3_pmd_mgmt.h"
#include "hinic3_pmd_hwif.h"
#include "hinic3_pmd_mbox.h"
#include "hinic3_pmd_hwdev.h"
#include "hinic3_pmd_wq.h"
#include "hinic3_pmd_cmdq.h"
#include "hinic3_pmd_nic_cfg.h"
#include "hinic3_pmd_hw_cfg.h"
#include "hinic3_pmd_ethdev.h"
#include "hinic3_pmd_tx.h"
#ifdef HINIC3_TRAFFIC_BIFUR
#include "hinic3_pmd_bifur.h"
#endif

#include "mml/hinic3_pmd_mml_lib.h"

#define HAIRPIN_FLAG (1 << 1)

struct vf_msg_handler {
	u16 cmd;
};

static const struct vf_msg_handler vf_cmd_handler[] = {
	{
		.cmd = HINIC3_NIC_CMD_VF_REGISTER,
	},

	{
		.cmd = HINIC3_NIC_CMD_GET_MAC,
	},

	{
		.cmd = HINIC3_NIC_CMD_SET_MAC,
	},

	{
		.cmd = HINIC3_NIC_CMD_DEL_MAC,
	},

	{
		.cmd = HINIC3_NIC_CMD_UPDATE_MAC,
	},

	{
		.cmd = HINIC3_NIC_CMD_VF_COS,
	},
};

static const struct vf_msg_handler vf_mag_cmd_handler[] = {
	{
		.cmd = MAG_CMD_GET_LINK_STATUS,
	},
};

static int mag_msg_to_mgmt_sync(void *hwdev, u16 cmd, void *buf_in, u16 in_size,
				void *buf_out, u16 *out_size);

int l2nic_msg_to_mgmt_sync(void *hwdev, u16 cmd, void *buf_in, u16 in_size,
			   void *buf_out, u16 *out_size)
{
	u32 i, cmd_cnt = ARRAY_LEN(vf_cmd_handler);
	bool cmd_to_pf = false;
	struct hinic3_nic_dev *nic_dev = ((struct hinic3_hwdev *)hwdev)->dev_handle;

	if (hinic3_func_type(hwdev) == TYPE_VF) {
		for (i = 0; i < cmd_cnt; i++) {
			if (cmd == vf_cmd_handler[i].cmd)
				cmd_to_pf = true;
		}
	}

	if (nic_dev->hwdev->qinfo_type != HINIC3_QINFO_TYPE_QPOOL && cmd_to_pf) {
		return hinic3_mbox_to_pf(hwdev, HINIC3_MOD_L2NIC, cmd,
					 buf_in, in_size,
					 buf_out, out_size, 0);
	}

	return hinic3_msg_to_mgmt_sync(hwdev, HINIC3_MOD_L2NIC, cmd,
				       buf_in, in_size,
				       buf_out, out_size, 0);
}

int hinic3_set_ci_table(void *hwdev, struct hinic3_sq_attr *attr)
{
	struct hinic3_cmd_cons_idx_attr cons_idx_attr;
	u16 out_size = sizeof(cons_idx_attr);
	int err;

	if (!hwdev || !attr)
		return -EINVAL;

	memset(&cons_idx_attr, 0, sizeof(cons_idx_attr));
	cons_idx_attr.func_idx = hinic3_global_func_id(hwdev);
	cons_idx_attr.dma_attr_off  = attr->dma_attr_off;
	cons_idx_attr.pending_limit = attr->pending_limit;
	cons_idx_attr.coalescing_time  = attr->coalescing_time;

	if (attr->intr_en) {
		cons_idx_attr.intr_en = attr->intr_en;
		cons_idx_attr.intr_idx = attr->intr_idx;
	}

	cons_idx_attr.l2nic_sqn = attr->l2nic_sqn;
	cons_idx_attr.ci_addr = attr->ci_dma_base;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC3_NIC_CMD_SQ_CI_ATTR_SET,
				     &cons_idx_attr, sizeof(cons_idx_attr),
				     &cons_idx_attr, &out_size);
	if (err || !out_size || cons_idx_attr.msg_head.status) {
		PMD_DRV_LOG(ERR, "Set ci attribute table failed, err: %d, "
			    "status: 0x%x, out_size: 0x%x",
			    err, cons_idx_attr.msg_head.status, out_size);
		return -EFAULT;
	}

	return 0;
}

#define PF_SET_VF_MAC(hwdev, status)	\
		(hinic3_func_type(hwdev) == TYPE_VF && \
		(status) == HINIC3_PF_SET_VF_ALREADY)

static int hinic3_check_mac_info(void *hwdev, u8 status, u16 vlan_id)
{
	if ((status && status != HINIC3_MGMT_STATUS_EXIST) ||
	    ((vlan_id & CHECK_IPSU_15BIT) &&
	     status == HINIC3_MGMT_STATUS_EXIST)) {
		if (PF_SET_VF_MAC(hwdev, status))
			return 0;

		return -EINVAL;
	}

	return 0;
}

#define VLAN_N_VID		4096

int hinic3_set_mac(void *hwdev, const u8 *mac_addr, u16 vlan_id, u16 func_id)
{
	struct hinic3_port_mac_set mac_info;
	u16 out_size = sizeof(mac_info);
	int err;

	if (!hwdev || !mac_addr)
		return -EINVAL;

#ifdef HINIC3_TRAFFIC_BIFUR
	if (hinic3_bifur_is_shared_dev(((struct hinic3_hwdev *)hwdev)->pci_dev)) {
		PMD_DRV_LOG(WARNING, "Share mode vf do not support change mac");
		return 0;
	}
#endif

	memset(&mac_info, 0, sizeof(mac_info));

	if (vlan_id >= VLAN_N_VID) {
		PMD_DRV_LOG(ERR, "Invalid VLAN number: %d", vlan_id);
		return -EINVAL;
	}

	mac_info.func_id = func_id;
	mac_info.vlan_id = vlan_id;
	memmove(mac_info.mac, mac_addr, ETH_ALEN);

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC3_NIC_CMD_SET_MAC, &mac_info,
				     sizeof(mac_info), &mac_info, &out_size);
	if (err || !out_size ||
	    hinic3_check_mac_info(hwdev, mac_info.msg_head.status, mac_info.vlan_id)) {
		PMD_DRV_LOG(ERR, "Update MAC failed, err: %d, status: 0x%x, out size: 0x%x",
			    err, mac_info.msg_head.status, out_size);
		return -EIO;
	}

	if (PF_SET_VF_MAC(hwdev, mac_info.msg_head.status)) {
		PMD_DRV_LOG(WARNING, "PF has already set VF mac, Ignore set operation");
		return HINIC3_PF_SET_VF_ALREADY;
	}

	if (mac_info.msg_head.status == HINIC3_MGMT_STATUS_EXIST) {
		PMD_DRV_LOG(WARNING, "MAC is repeated. Ignore update operation");
		return 0;
	}

	return 0;
}

int hinic3_del_mac(void *hwdev, const u8 *mac_addr, u16 vlan_id, u16 func_id)
{
	struct hinic3_port_mac_set mac_info;
	u16 out_size = sizeof(mac_info);
	int err;

	if (!hwdev || !mac_addr)
		return -EINVAL;

#ifdef HINIC3_TRAFFIC_BIFUR
	if (hinic3_bifur_is_shared_dev(((struct hinic3_hwdev *)hwdev)->pci_dev)) {
		PMD_DRV_LOG(WARNING, "Share mode vf do not support change mac");
		return 0;
	}
#endif

	if (vlan_id >= VLAN_N_VID) {
		PMD_DRV_LOG(ERR, "Invalid VLAN number: %d", vlan_id);
		return -EINVAL;
	}

	memset(&mac_info, 0, sizeof(mac_info));
	mac_info.func_id = func_id;
	mac_info.vlan_id = vlan_id;
	memmove(mac_info.mac, mac_addr, ETH_ALEN);

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC3_NIC_CMD_DEL_MAC, &mac_info,
				     sizeof(mac_info), &mac_info, &out_size);
	if (err || !out_size || (mac_info.msg_head.status &&
	    !PF_SET_VF_MAC(hwdev, mac_info.msg_head.status))) {
		PMD_DRV_LOG(ERR, "Delete MAC failed, err: %d, status: 0x%x, out size: 0x%x",
			    err, mac_info.msg_head.status, out_size);
		return -EIO;
	}

	if (PF_SET_VF_MAC(hwdev, mac_info.msg_head.status)) {
		PMD_DRV_LOG(WARNING, "PF has already set VF mac, Ignore delete operation");
		return HINIC3_PF_SET_VF_ALREADY;
	}

	return 0;
}

int hinic3_update_mac(void *hwdev, u8 *old_mac, u8 *new_mac, u16 vlan_id,
		      u16 func_id)
{
	struct hinic3_port_mac_update mac_info;
	u16 out_size = sizeof(mac_info);
	int err;

	if (!hwdev || !old_mac || !new_mac)
		return -EINVAL;

#ifdef HINIC3_TRAFFIC_BIFUR
	if (hinic3_bifur_is_shared_dev(((struct hinic3_hwdev *)hwdev)->pci_dev)) {
		PMD_DRV_LOG(WARNING, "Share mode vf do not support change mac");
		return 0;
	}
#endif

	if (vlan_id >= VLAN_N_VID) {
		PMD_DRV_LOG(ERR, "Invalid VLAN number: %d", vlan_id);
		return -EINVAL;
	}

	memset(&mac_info, 0, sizeof(mac_info));
	mac_info.func_id = func_id;
	mac_info.vlan_id = vlan_id;
	memcpy(mac_info.old_mac, old_mac, ETH_ALEN);
	memcpy(mac_info.new_mac, new_mac, ETH_ALEN);

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC3_NIC_CMD_UPDATE_MAC,
				     &mac_info, sizeof(mac_info),
				     &mac_info, &out_size);
	if (err || !out_size ||
	    hinic3_check_mac_info(hwdev, mac_info.msg_head.status, mac_info.vlan_id)) {
		PMD_DRV_LOG(ERR, "Update MAC failed, err: %d, status: 0x%x, out size: 0x%x",
			    err, mac_info.msg_head.status, out_size);
		return -EIO;
	}

	if (PF_SET_VF_MAC(hwdev, mac_info.msg_head.status)) {
		PMD_DRV_LOG(WARNING, "PF has already set VF MAC. Ignore update operation");
		return HINIC3_PF_SET_VF_ALREADY;
	}

	if (mac_info.msg_head.status == HINIC3_MGMT_STATUS_EXIST) {
		PMD_DRV_LOG(INFO, "MAC is repeated. Ignore update operation");
		return 0;
	}

	return 0;
}

int hinic3_get_default_mac(void *hwdev, u8 *mac_addr, int ether_len)
{
	struct hinic3_port_mac_set mac_info;
	u16 out_size = sizeof(mac_info);
	int err;

	if (!hwdev || !mac_addr)
		return -EINVAL;

#ifdef HINIC3_TRAFFIC_BIFUR
	if (hinic3_bifur_is_shared_dev(((struct hinic3_hwdev *)hwdev)->pci_dev)) {
		return hinic3_bifur_get_default_mac(((struct hinic3_hwdev *)hwdev)->pci_dev,
			mac_addr, ether_len);
	}
#endif

	memset(&mac_info, 0, sizeof(mac_info));
	mac_info.func_id = hinic3_global_func_id(hwdev);

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC3_NIC_CMD_GET_MAC,
				     &mac_info, sizeof(mac_info),
		&mac_info, &out_size);
	if (err || !out_size || mac_info.msg_head.status) {
		PMD_DRV_LOG(ERR, "Get MAC failed, err: %d, status: 0x%x, out size: 0x%x",
			    err, mac_info.msg_head.status, out_size);
		return -EINVAL;
	}

	memmove(mac_addr, mac_info.mac, ether_len);

	return 0;
}

static int hinic3_config_vlan(void *hwdev, u8 opcode, u16 vlan_id, u16 func_id)
{
	struct hinic3_cmd_vlan_config vlan_info;
	u16 out_size = sizeof(vlan_info);
	int err;

	memset(&vlan_info, 0, sizeof(vlan_info));
	vlan_info.opcode = opcode;
	vlan_info.func_id = func_id;
	vlan_info.vlan_id = vlan_id;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC3_NIC_CMD_CFG_FUNC_VLAN,
				     &vlan_info, sizeof(vlan_info),
				     &vlan_info, &out_size);
	if (err || !out_size || vlan_info.msg_head.status) {
		PMD_DRV_LOG(ERR, "%s vlan failed, err: %d, status: 0x%x, out size: 0x%x",
			    opcode == HINIC3_CMD_OP_ADD ? "Add" : "Delete",
			    err, vlan_info.msg_head.status, out_size);
		return -EINVAL;
	}

	return 0;
}

int hinic3_add_vlan(void *hwdev, u16 vlan_id, u16 func_id)
{
	if (!hwdev)
		return -EINVAL;

	return hinic3_config_vlan(hwdev, HINIC3_CMD_OP_ADD, vlan_id, func_id);
}

int hinic3_del_vlan(void *hwdev, u16 vlan_id, u16 func_id)
{
	if (!hwdev)
		return -EINVAL;

	return hinic3_config_vlan(hwdev, HINIC3_CMD_OP_DEL, vlan_id, func_id);
}

int hinic3_get_port_info(void *hwdev, struct nic_port_info *port_info)
{
	struct hinic3_cmd_port_info port_msg;
	u16 out_size = sizeof(port_msg);
	int err;

	if (!hwdev || !port_info)
		return -EINVAL;

	memset(&port_msg, 0, sizeof(port_msg));
	port_msg.port_id = hinic3_physical_port_id(hwdev);

	err = mag_msg_to_mgmt_sync(hwdev, MAG_CMD_GET_PORT_INFO, &port_msg,
				   sizeof(port_msg), &port_msg, &out_size);
	if (err || !out_size || port_msg.msg_head.status) {
		PMD_DRV_LOG(ERR, "Get port info failed, err: %d, status: 0x%x, out size: 0x%x",
			    err, port_msg.msg_head.status, out_size);
		return -EINVAL;
	}

	port_info->autoneg_cap = port_msg.autoneg_cap;
	port_info->autoneg_state = port_msg.autoneg_state;
	port_info->duplex = port_msg.duplex;
	port_info->port_type = port_msg.port_type;
	port_info->speed = port_msg.speed;
	port_info->fec = port_msg.fec;

	return 0;
}

int hinic3_get_link_state(void *hwdev, u8 *link_state)
{
	struct hinic3_cmd_link_state get_link;
	u16 out_size = sizeof(get_link);
	int err;

	if (!hwdev || !link_state)
		return -EINVAL;

	memset(&get_link, 0, sizeof(get_link));
	get_link.port_id = hinic3_physical_port_id(hwdev);
	err = mag_msg_to_mgmt_sync(hwdev, MAG_CMD_GET_LINK_STATUS,
				     &get_link, sizeof(get_link),
				     &get_link, &out_size);
	if (err || !out_size || get_link.msg_head.status) {
		PMD_DRV_LOG(ERR, "Get link state failed, err: %d, status: 0x%x, out size: 0x%x",
			    err, get_link.msg_head.status, out_size);
		return -EIO;
	}

	*link_state = get_link.state;

	return 0;
}

int hinic3_set_vport_enable(void *hwdev, bool enable)
{
	struct hinic3_vport_state en_state;
	u16 out_size = sizeof(en_state);
	int err;
	struct hinic3_nic_dev *nic_dev = (struct hinic3_nic_dev*)((struct hinic3_hwdev *)hwdev)->dev_handle;

	if (!hwdev)
		return -EINVAL;

	if (IS_QPOOL_MODE(nic_dev)) {
		PMD_DRV_LOG(WARNING, "Qpool not support set vport enable");
		return 0;
	}

	memset(&en_state, 0, sizeof(en_state));
	en_state.func_id = hinic3_global_func_id(hwdev);
	en_state.state = enable ? 1 : 0;
	en_state.num_qps = nic_dev->num_rqs;
	en_state.rx_compact_wqe_en = HINIC3_SUPPORT_RX_SW_COMPACT_CQE(nic_dev);

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC3_NIC_CMD_SET_VPORT_ENABLE,
				     &en_state, sizeof(en_state),
				     &en_state, &out_size);
	if (err || !out_size || en_state.msg_head.status) {
		PMD_DRV_LOG(ERR, "Set vport state failed, err: %d, status: 0x%x, out size: 0x%x",
			    err, en_state.msg_head.status, out_size);
		return -EIO;
	}

	((struct hinic3_hwdev *)hwdev)->vf_valid_status = enable;

	return 0;
}

int hinic3_set_port_enable(void *hwdev, bool enable)
{
	struct mag_cmd_set_port_enable en_state;
	u16 out_size = sizeof(en_state);
	int err;

	if (!hwdev)
		return -EINVAL;

	if (hinic3_func_type(hwdev) == TYPE_VF)
		return 0;

	memset(&en_state, 0, sizeof(en_state));
	en_state.function_id = hinic3_global_func_id(hwdev);
	en_state.state = enable ? MAG_CMD_TX_ENABLE | MAG_CMD_RX_ENABLE :
				MAG_CMD_PORT_DISABLE;

	err = mag_msg_to_mgmt_sync(hwdev, MAG_CMD_SET_PORT_ENABLE,
				     &en_state, sizeof(en_state), &en_state,
				     &out_size);
	if (err || !out_size || en_state.head.status) {
		PMD_DRV_LOG(ERR, "Set port state failed, err: %d, status: 0x%x, out size: 0x%x",
			    err, en_state.head.status, out_size);
		return -EIO;
	}

	return 0;
}

int hinic3_flush_qps_res(void *hwdev)
{
	struct hinic3_cmd_clear_qp_resource sq_res;
	u16 out_size = sizeof(sq_res);
	int err;

	if (!hwdev)
		return -EINVAL;

	memset(&sq_res, 0, sizeof(sq_res));
	sq_res.func_id = hinic3_global_func_id(hwdev);

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC3_NIC_CMD_CLEAR_QP_RESOURCE,
				     &sq_res, sizeof(sq_res), &sq_res,
				     &out_size);
	if (err || !out_size || sq_res.msg_head.status) {
		PMD_DRV_LOG(ERR, "Clear sq resources failed, err: %d, status: 0x%x, out size: 0x%x",
			    err, sq_res.msg_head.status, out_size);
		return -EIO;
	}

	return 0;
}

int hinic3_flush_assign_qps_res(void *hwdev)
{
	struct hinic3_cmd_clear_assign_qp_res sq_res = {0};
	struct hinic3_nic_dev *nic_dev = NULL;
	u16 out_size = sizeof(sq_res), q_id;
	int err;

	if (!hwdev)
		return -EINVAL;

	memset(&sq_res, 0, sizeof(sq_res));

	nic_dev = ((struct hinic3_hwdev *)hwdev)->dev_handle;
	sq_res.func_id = hinic3_global_func_id(hwdev);
	sq_res.qp_num = nic_dev->num_sqs;
	for (q_id = 0; q_id < nic_dev->num_sqs; q_id++) {
		sq_res.qp[q_id] = nic_dev->txqs[q_id]->local_qid;
	}

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC3_NIC_CMD_CLEAR_ASSIGN_QP_RES,
				     &sq_res, sizeof(sq_res), &sq_res,
				     &out_size);
	if (err || !out_size || sq_res.msg_head.status) {
		PMD_DRV_LOG(ERR, "Clear sq resources failed, err: %d, status: 0x%x, out size: 0x%x",
			    errno, sq_res.msg_head.status, out_size);
		return -EIO;
	}

	return 0;
}

static int hinic3_cfg_hw_pause(void *hwdev, u8 opcode,
			       struct nic_pause_config *nic_pause)
{
	struct hinic3_cmd_pause_config pause_info;
	u16 out_size = sizeof(pause_info);
	int err;

	memset(&pause_info, 0, sizeof(pause_info));

	pause_info.port_id = hinic3_physical_port_id(hwdev);
	pause_info.opcode = opcode;
	if (opcode == HINIC3_CMD_OP_SET) {
		pause_info.auto_neg = nic_pause->auto_neg;
		pause_info.rx_pause = nic_pause->rx_pause;
		pause_info.tx_pause = nic_pause->tx_pause;
	}

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC3_NIC_CMD_CFG_PAUSE_INFO,
				     &pause_info, sizeof(pause_info),
				     &pause_info, &out_size);
	if (err || !out_size || pause_info.msg_head.status) {
		PMD_DRV_LOG(ERR, "%s pause info failed, err: %d, status: 0x%x, out size: 0x%x\n",
			    opcode == HINIC3_CMD_OP_SET ? "Set" : "Get",
			    err, pause_info.msg_head.status, out_size);
		return -EIO;
	}

	if (opcode == HINIC3_CMD_OP_GET) {
		nic_pause->auto_neg = pause_info.auto_neg;
		nic_pause->rx_pause = pause_info.rx_pause;
		nic_pause->tx_pause = pause_info.tx_pause;
	}

	return 0;
}

int hinic3_set_pause_info(void *hwdev, struct nic_pause_config nic_pause)
{
	if (!hwdev)
		return -EINVAL;

	return hinic3_cfg_hw_pause(hwdev, HINIC3_CMD_OP_SET, &nic_pause);
}

int hinic3_get_pause_info(void *hwdev, struct nic_pause_config *nic_pause)
{
	if (!hwdev || !nic_pause)
		return -EINVAL;

	return hinic3_cfg_hw_pause(hwdev, HINIC3_CMD_OP_GET, nic_pause);
}

int hinic3_get_vport_stats(void *hwdev, struct hinic3_vport_stats *stats)
{
	struct hinic3_port_stats_info stats_info;
	struct hinic3_cmd_vport_stats vport_stats;
	u16 out_size = sizeof(vport_stats);
	int err;

	if (!hwdev || !stats)
		return -EINVAL;

	memset(&stats_info, 0, sizeof(stats_info));
	memset(&vport_stats, 0, sizeof(vport_stats));

	stats_info.func_id = hinic3_global_func_id(hwdev);

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC3_NIC_CMD_GET_VPORT_STAT,
				     &stats_info, sizeof(stats_info),
				     &vport_stats, &out_size);
	if (err || !out_size || vport_stats.msg_head.status) {
		PMD_DRV_LOG(ERR, "Get function stats failed, err: %d, status: 0x%x, out size: 0x%x",
			    err, vport_stats.msg_head.status, out_size);
		return -EIO;
	}

	memcpy(stats, &vport_stats.stats, sizeof(*stats));

	return 0;
}

int hinic3_get_phy_port_stats(void *hwdev, struct mag_phy_port_stats *stats)
{
	struct mag_cmd_get_port_stat *port_stats = NULL;
	struct mag_cmd_port_stats_info stats_info;
	u16 out_size = sizeof(*port_stats);
	int err;

	port_stats = rte_zmalloc("port_stats", sizeof(*port_stats), 0);
	if (!port_stats)
		return -ENOMEM;

	memset(&stats_info, 0, sizeof(stats_info));
	stats_info.port_id = hinic3_physical_port_id(hwdev);

	err = mag_msg_to_mgmt_sync(hwdev, MAG_CMD_GET_PORT_STAT,
				   &stats_info, sizeof(stats_info),
				   port_stats, &out_size);
	if (err || !out_size || port_stats->head.status) {
		PMD_DRV_LOG(ERR,
			"Failed to get port statistics, err: %d, status: 0x%x, out size: 0x%x\n",
			err, port_stats->head.status, out_size);
		err = -EIO;
		goto out;
	}

	memcpy(stats, &port_stats->counter, sizeof(*stats));

out:
	rte_free(port_stats);

	return err;
}

int hinic3_clear_vport_stats(void *hwdev)
{
	struct hinic3_cmd_clear_vport_stats clear_vport_stats;
	u16 out_size = sizeof(clear_vport_stats);
	int err;

	if (!hwdev) {
		PMD_DRV_LOG(ERR, "Hwdev is NULL");
		return -EINVAL;
	}

	memset(&clear_vport_stats, 0, sizeof(clear_vport_stats));
	clear_vport_stats.func_id = hinic3_global_func_id(hwdev);

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC3_NIC_CMD_CLEAN_VPORT_STAT,
				     &clear_vport_stats,
				     sizeof(clear_vport_stats),
				     &clear_vport_stats, &out_size);
	if (err || !out_size || clear_vport_stats.msg_head.status) {
		PMD_DRV_LOG(ERR, "Clear vport stats failed, err: %d, status: 0x%x, out size: 0x%x",
			    err, clear_vport_stats.msg_head.status, out_size);
		return -EIO;
	}

	return 0;
}

int hinic3_clear_phy_port_stats(void *hwdev)
{
	struct mag_cmd_port_stats_info port_stats;
	u16 out_size = sizeof(port_stats);
	int err;

	port_stats.port_id = hinic3_physical_port_id(hwdev);

	err = mag_msg_to_mgmt_sync(hwdev, MAG_CMD_CLR_PORT_STAT,
				   &port_stats, sizeof(port_stats),
				   &port_stats, &out_size);
	if (err || !out_size || port_stats.head.status) {
		PMD_DRV_LOG(ERR,
			"Failed to get port statistics, err: %d, status: 0x%x, out size: 0x%x\n",
			err, port_stats.head.status, out_size);
		err = -EIO;
	}

	return err;
}

static int hinic3_set_function_table(void *hwdev, u32 cfg_bitmap,
				     struct hinic3_func_tbl_cfg *cfg)
{
	struct hinic3_cmd_set_func_tbl cmd_func_tbl;
	u16 out_size = sizeof(cmd_func_tbl);
	int err;

	memset(&cmd_func_tbl, 0, sizeof(cmd_func_tbl));
	cmd_func_tbl.func_id = hinic3_global_func_id(hwdev);
	cmd_func_tbl.cfg_bitmap = cfg_bitmap;
	cmd_func_tbl.tbl_cfg = *cfg;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC3_NIC_CMD_SET_FUNC_TBL,
				     &cmd_func_tbl, sizeof(cmd_func_tbl),
				     &cmd_func_tbl, &out_size);
	if (err || cmd_func_tbl.msg_head.status || !out_size) {
		PMD_DRV_LOG(ERR, "Set func table failed, bitmap: 0x%x, err: %d, "
			    "status: 0x%x, out size: 0x%x\n", cfg_bitmap, err,
			    cmd_func_tbl.msg_head.status, out_size);
		return -EFAULT;
	}

	return 0;
}

int hinic3_init_function_table(void *hwdev, u16 rx_buff_len)
{
	struct hinic3_func_tbl_cfg func_tbl_cfg;
	u32 cfg_bitmap = BIT(FUNC_CFG_INIT) | BIT(FUNC_CFG_MTU) |
			 BIT(FUNC_CFG_RX_BUF_SIZE);

	memset(&func_tbl_cfg, 0, sizeof(func_tbl_cfg));
	func_tbl_cfg.mtu = 0x3FFF; /* Default, max mtu */
	func_tbl_cfg.rx_wqe_buf_size = rx_buff_len;

	return hinic3_set_function_table(hwdev, cfg_bitmap, &func_tbl_cfg);
}

int hinic3_set_port_mtu(void *hwdev, u16 new_mtu)
{
	struct hinic3_func_tbl_cfg func_tbl_cfg;

	if (!hwdev)
		return -EINVAL;

	if (new_mtu < HINIC3_MIN_MTU_SIZE) {
		PMD_DRV_LOG(ERR, "Invalid mtu size: %ubytes, mtu size < %ubytes",
			    new_mtu, HINIC3_MIN_MTU_SIZE);
		return -EINVAL;
	}

	if (new_mtu > HINIC3_MAX_JUMBO_FRAME_SIZE) {
		PMD_DRV_LOG(ERR, "Invalid mtu size: %ubytes, mtu size > %ubytes",
			    new_mtu, HINIC3_MAX_JUMBO_FRAME_SIZE);
		return -EINVAL;
	}

	memset(&func_tbl_cfg, 0, sizeof(func_tbl_cfg));
	func_tbl_cfg.mtu = new_mtu;
	return hinic3_set_function_table(hwdev, BIT(FUNC_CFG_MTU),
					 &func_tbl_cfg);
}

static int nic_feature_nego(void *hwdev, u8 opcode, u64 *s_feature, u16 size)
{
	struct hinic3_cmd_feature_nego feature_nego;
	u16 out_size = sizeof(feature_nego);
	int err;

	if (!hwdev || !s_feature || size > MAX_FEATURE_QWORD)
		return -EINVAL;

	memset(&feature_nego, 0, sizeof(feature_nego));
	feature_nego.func_id = hinic3_global_func_id(hwdev);
	feature_nego.opcode = opcode;
	if (opcode == HINIC3_CMD_OP_SET)
		memcpy(feature_nego.s_feature, s_feature, size * sizeof(u64));

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC3_NIC_CMD_FEATURE_NEGO,
				     &feature_nego, sizeof(feature_nego),
				     &feature_nego, &out_size);
	if (err || !out_size || feature_nego.msg_head.status) {
		PMD_DRV_LOG(ERR, "Failed to negotiate nic feature, err:%d, status: 0x%x, out_size: 0x%x\n",
			    err, feature_nego.msg_head.status, out_size);
		return -EFAULT;
	}

	if (opcode == HINIC3_CMD_OP_GET)
		memcpy(s_feature, feature_nego.s_feature, size * sizeof(u64));

	return 0;
}

int hinic3_get_feature_from_hw(void *hwdev, u64 *s_feature, u16 size)
{
	return nic_feature_nego(hwdev, HINIC3_CMD_OP_GET, s_feature, size);
}

int hinic3_set_feature_to_hw(void *hwdev, u64 *s_feature, u16 size)
{
	return nic_feature_nego(hwdev, HINIC3_CMD_OP_SET, s_feature, size);
}

static int hinic3_vf_func_init(void *hwdev)
{
	struct hinic3_cmd_register_vf register_info;
	u16 out_size = sizeof(register_info);
	int err;

	if (hinic3_func_type(hwdev) != TYPE_VF)
		return 0;

	memset(&register_info, 0, sizeof(register_info));
	register_info.op_register = 1;
	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC3_NIC_CMD_VF_REGISTER,
				     &register_info, sizeof(register_info),
				     &register_info, &out_size);
	if (err || register_info.msg_head.status || !out_size) {
		PMD_DRV_LOG(ERR, "Register VF failed, err: %d, status: 0x%x, out size: 0x%x",
			    err, register_info.msg_head.status, out_size);
		return -EFAULT;
	}

	return 0;
}

static int hinic3_vf_func_free(void *hwdev)
{
	struct hinic3_cmd_register_vf unregister;
	u16 out_size = sizeof(unregister);
	int err;

	if (hinic3_func_type(hwdev) != TYPE_VF)
		return 0;

	memset(&unregister, 0, sizeof(unregister));
	unregister.op_register = 0;
	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC3_NIC_CMD_VF_REGISTER,
				     &unregister, sizeof(unregister),
				     &unregister, &out_size);
	if (err || unregister.msg_head.status || !out_size) {
		PMD_DRV_LOG(ERR, "Unregister VF failed, err: %d, status: 0x%x, out size: 0x%x",
			    err, unregister.msg_head.status, out_size);
		return -EFAULT;
	}

	return 0;
}

int hinic3_init_nic_hwdev(void *hwdev)
{
	return hinic3_vf_func_init(hwdev);
}

void hinic3_free_nic_hwdev(void *hwdev)
{
	if (!hwdev)
		return;

	if (hinic3_func_type(hwdev) != TYPE_VF)
        (void)hinic3_set_link_status_follow(hwdev, HINIC3_LINK_FOLLOW_DEFAULT);

	hinic3_vf_func_free(hwdev);
}

int hinic3_set_rx_mode(void *hwdev, u32 enable)
{
	struct hinic3_rx_mode_config rx_mode_cfg;
	u16 out_size = sizeof(rx_mode_cfg);
	int err;

	if (!hwdev)
		return -EINVAL;

	if (((struct hinic3_hwdev *)hwdev)->qinfo_type == HINIC3_QINFO_TYPE_QPOOL)
		return 0;

	memset(&rx_mode_cfg, 0, sizeof(rx_mode_cfg));
	rx_mode_cfg.func_id = hinic3_global_func_id(hwdev);
	rx_mode_cfg.rx_mode = enable;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC3_NIC_CMD_SET_RX_MODE,
				     &rx_mode_cfg, sizeof(rx_mode_cfg),
				     &rx_mode_cfg, &out_size);
	if (err || !out_size || rx_mode_cfg.msg_head.status) {
		PMD_DRV_LOG(ERR, "Set rx mode failed, err: %d, status: 0x%x, out size: 0x%x",
			    err, rx_mode_cfg.msg_head.status, out_size);
		return -EIO;
	}
	return 0;
}

int hinic3_set_rx_vlan_offload(void *hwdev, u8 en)
{
	struct hinic3_cmd_vlan_offload vlan_cfg;
	u16 out_size = sizeof(vlan_cfg);
	int err;

	if (!hwdev)
		return -EINVAL;

	memset(&vlan_cfg, 0, sizeof(vlan_cfg));
	vlan_cfg.func_id = hinic3_global_func_id(hwdev);
	vlan_cfg.vlan_offload = en;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC3_NIC_CMD_SET_RX_VLAN_OFFLOAD,
				     &vlan_cfg, sizeof(vlan_cfg),
				     &vlan_cfg, &out_size);
	if (err || !out_size || vlan_cfg.msg_head.status) {
		PMD_DRV_LOG(ERR, "Set rx vlan offload failed, err: %d, status: 0x%x, out size: 0x%x",
			    err, vlan_cfg.msg_head.status, out_size);
		return -EIO;
	}
	return 0;
}

int hinic3_set_vlan_fliter(void *hwdev, u32 vlan_filter_ctrl)
{
	struct hinic3_cmd_set_vlan_filter vlan_filter;
	u16 out_size = sizeof(vlan_filter);
	int err;

	if (!hwdev)
		return -EINVAL;

	memset(&vlan_filter, 0, sizeof(vlan_filter));
	vlan_filter.func_id = hinic3_global_func_id(hwdev);
	vlan_filter.vlan_filter_ctrl = vlan_filter_ctrl;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC3_NIC_CMD_SET_VLAN_FILTER_EN,
				     &vlan_filter, sizeof(vlan_filter),
				     &vlan_filter, &out_size);
	if (err || !out_size || vlan_filter.msg_head.status) {
		PMD_DRV_LOG(ERR, "Failed to set vlan filter, err: %d, status: 0x%x, out size: 0x%x",
			    err, vlan_filter.msg_head.status, out_size);
		return -EIO;
	}
	return 0;
}

static int hinic3_set_rx_lro(void *hwdev, u8 ipv4_en, u8 ipv6_en,
			     u8 lro_max_pkt_len)
{
	struct hinic3_cmd_lro_config lro_cfg;
	u16 out_size = sizeof(lro_cfg);
	int err;

	if (!hwdev)
		return -EINVAL;

	memset(&lro_cfg, 0, sizeof(lro_cfg));
	lro_cfg.func_id = hinic3_global_func_id(hwdev);
	lro_cfg.opcode = HINIC3_CMD_OP_SET;
	lro_cfg.lro_ipv4_en = ipv4_en;
	lro_cfg.lro_ipv6_en = ipv6_en;
	lro_cfg.lro_max_pkt_len = lro_max_pkt_len;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC3_NIC_CMD_CFG_RX_LRO,
				     &lro_cfg, sizeof(lro_cfg),
				     &lro_cfg, &out_size);
	if (err || !out_size || lro_cfg.msg_head.status) {
		PMD_DRV_LOG(ERR, "Set lro offload failed, err: %d, status: 0x%x, out size: 0x%x",
			    err, lro_cfg.msg_head.status, out_size);
		return -EIO;
	}

	return 0;
}

static int hinic3_set_rx_lro_timer(void *hwdev, u32 timer_value)
{
	struct hinic3_cmd_lro_timer lro_timer;
	u16 out_size = sizeof(lro_timer);
	int err;

	if (!hwdev)
		return -EINVAL;

	memset(&lro_timer, 0, sizeof(lro_timer));
	lro_timer.opcode = HINIC3_CMD_OP_SET;
	lro_timer.timer = timer_value;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC3_NIC_CMD_CFG_LRO_TIMER,
				     &lro_timer, sizeof(lro_timer),
				     &lro_timer, &out_size);
	if (err || !out_size || lro_timer.msg_head.status) {
		PMD_DRV_LOG(ERR, "Set lro timer failed, err: %d, status: 0x%x, out size: 0x%x",
			    err, lro_timer.msg_head.status, out_size);

		return -EIO;
	}

	return 0;
}

int hinic3_set_rx_lro_state(void *hwdev, u8 lro_en, u32 lro_timer,
			    u32 lro_max_pkt_len)
{
	u8 ipv4_en = 0, ipv6_en = 0;
	int err;

	if (!hwdev)
		return -EINVAL;

	ipv4_en = lro_en ? 1 : 0;
	ipv6_en = lro_en ? 1 : 0;

	PMD_DRV_LOG(INFO, "Set LRO max coalesce packet size to %uK",
		    lro_max_pkt_len);

	err = hinic3_set_rx_lro(hwdev, ipv4_en, ipv6_en, (u8)lro_max_pkt_len);
	if (err)
		return err;

	/* We don't set LRO timer for VF */
	if (hinic3_func_type(hwdev) == TYPE_VF)
		return 0;

	PMD_DRV_LOG(INFO, "Set LRO timer to %u", lro_timer);

	return hinic3_set_rx_lro_timer(hwdev, lro_timer);
}

/* RSS config */
int hinic3_rss_template_alloc(void *hwdev, u16 q_grp_id)
{
	struct hinic3_rss_template_mgmt template_mgmt;
	u16 out_size = sizeof(template_mgmt);
	int err;

	if (!hwdev)
		return -EINVAL;

	if (((struct hinic3_hwdev *)hwdev)->qinfo_type == HINIC3_QINFO_TYPE_QPOOL)
		return 0;

	memset(&template_mgmt, 0, sizeof(struct hinic3_rss_template_mgmt));
	if (q_grp_id == 0)
		template_mgmt.func_id = hinic3_global_func_id(hwdev);
	else
		template_mgmt.func_id = q_grp_id;
	template_mgmt.cmd = NIC_RSS_CMD_TEMP_ALLOC;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC3_NIC_CMD_RSS_TEMP_MGR,
				     &template_mgmt, sizeof(template_mgmt),
				     &template_mgmt, &out_size);
	if (err || !out_size || template_mgmt.msg_head.status) {
		if (template_mgmt.msg_head.status ==
		    HINIC3_MGMT_STATUS_TABLE_FULL) {
			PMD_DRV_LOG(ERR, "There is no more template available");
			return -ENOSPC;
		}
		PMD_DRV_LOG(ERR, "Alloc rss template failed, err: %d, "
			    "status: 0x%x, out size: 0x%x",
			    err, template_mgmt.msg_head.status, out_size);
		return -EFAULT;
	}

	return 0;
}

int hinic3_rss_template_free(void *hwdev, u16 q_grp_id)
{
	struct hinic3_rss_template_mgmt template_mgmt;
	u16 out_size = sizeof(template_mgmt);
	int err;

	if (!hwdev)
		return -EINVAL;

	if (((struct hinic3_hwdev *)hwdev)->qinfo_type == HINIC3_QINFO_TYPE_QPOOL)
		return 0;

	memset(&template_mgmt, 0, sizeof(struct hinic3_rss_template_mgmt));
	if (q_grp_id == 0)
		template_mgmt.func_id = hinic3_global_func_id(hwdev);
	else
		template_mgmt.func_id = q_grp_id;
	template_mgmt.cmd = NIC_RSS_CMD_TEMP_FREE;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC3_NIC_CMD_RSS_TEMP_MGR,
				     &template_mgmt, sizeof(template_mgmt),
				     &template_mgmt, &out_size);
	if (err || !out_size || template_mgmt.msg_head.status) {
		PMD_DRV_LOG(ERR, "Free rss template failed, err: %d, "
			    "status: 0x%x, out size: 0x%x",
			    err, template_mgmt.msg_head.status, out_size);
		return -EFAULT;
	}

	return 0;
}

static int hinic3_rss_cfg_hash_key(void *hwdev, u8 opcode, u8 *key, u16 key_size)
{
	struct hinic3_cmd_rss_hash_key hash_key;
	u16 out_size = sizeof(hash_key);
	int err;

	if (!hwdev || !key)
		return -EINVAL;

	memset(&hash_key, 0, sizeof(struct hinic3_cmd_rss_hash_key));
	if (((struct hinic3_hwdev *)hwdev)->qinfo_type == HINIC3_QINFO_TYPE_QPOOL)
		hash_key.func_id = ((struct hinic3_hwdev *)hwdev)->qpool_qgrp_id;
	else
		hash_key.func_id = hinic3_global_func_id(hwdev);

	hash_key.opcode = opcode;
	if (opcode == HINIC3_CMD_OP_SET)
		memcpy(hash_key.key, key, key_size);

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC3_NIC_CMD_CFG_RSS_HASH_KEY,
				     &hash_key, sizeof(hash_key),
				     &hash_key, &out_size);
	if (err || !out_size || hash_key.msg_head.status) {
		PMD_DRV_LOG(ERR, "%s hash key failed, err: %d, "
			    "status: 0x%x, out size: 0x%x",
			    opcode == HINIC3_CMD_OP_SET ? "Set" : "Get",
			    err, hash_key.msg_head.status, out_size);
		return -EFAULT;
	}

	if (opcode == HINIC3_CMD_OP_GET)
		memcpy(key, hash_key.key, key_size);

	return 0;
}

int hinic3_rss_set_hash_key(void *hwdev, u8 *key, u16 key_size)
{
	if (!hwdev || !key)
		return -EINVAL;

	return hinic3_rss_cfg_hash_key(hwdev, HINIC3_CMD_OP_SET, key, key_size);
}

static int hinic3_rss_get_indir_tbl_qpool(struct nic_rss_indirect_tbl *nic_indir_tbl, int fd)
{
	struct msg_module msg_to_kernel = {0};
	int err;

	fill_ioctl_msg(&msg_to_kernel, SEND_TO_NPU, 0,
			sizeof(struct nic_rss_indirect_tbl),
			sizeof(struct nic_rss_indirect_tbl),
			nic_indir_tbl, nic_indir_tbl);
	msg_to_kernel.npu_cmd.direct_resp = 0;
	msg_to_kernel.npu_cmd.mod = HINIC3_MOD_L2NIC;
	msg_to_kernel.npu_cmd.cmd = HINIC3_UCODE_CMD_GET_RSS_INDIR_TABLE;
	msg_to_kernel.npu_cmd.ack_type = HINIC3_ACK_TYPE_CMDQ;

	err = ioctl(fd, 0, &msg_to_kernel);
	if (err < 0)
		PMD_DRV_LOG(ERR, "Get qpool indir tbl err: %d.", errno);
	return err;
}

int hinic3_indir_set_qid_mmap(u16 q_id, u16 local_qid)
{
	struct hinic3_indir_tbl_qid_lqid *entry = NULL;

	TAILQ_FOREACH(entry, &g_qid_lqid_list, entries) {
		if (entry->q_id == q_id) {
			return -1;
		}
	}

	entry = rte_zmalloc("indir_entry", sizeof(struct hinic3_indir_tbl_qid_lqid), 0);

	entry->q_id = q_id;
	entry->local_qid = local_qid;

	TAILQ_INSERT_TAIL(&g_qid_lqid_list, entry, entries);

	return 0;
}

static struct hinic3_indir_tbl_qid_lqid *hinic3_find_by_local_qid(u16 local_qid)
{
	struct hinic3_indir_tbl_qid_lqid *entry = NULL;

	TAILQ_FOREACH(entry, &g_qid_lqid_list, entries) {
		if (entry->local_qid == local_qid)
			return entry;
	}

	return NULL;
}

int hinic3_rss_get_indir_tbl(void *hwdev, u32 *indir_table, u32 indir_table_size)
{
	struct hinic3_nic_dev *nic_dev = NULL;
	struct hinic3_cmd_buf *cmd_buf = NULL;
	struct nic_rss_indirect_tbl *nic_indir_tbl = NULL;
	struct nic_rss_indirect_tbl_user_data *indir_tbl_udata = NULL;
	struct hinic3_indir_tbl_qid_lqid *entry = NULL;
	u16 *indir_tbl = NULL;
	int err;
	u32 i;

	if (!hwdev || !indir_table)
		return -EINVAL;

	cmd_buf = hinic3_alloc_cmd_buf(hwdev);
	if (!cmd_buf) {
		PMD_DRV_LOG(ERR, "Allocate cmd buf failed");
		return -ENOMEM;
	}
	cmd_buf->size = sizeof(struct nic_rss_indirect_tbl);

	if (((struct hinic3_hwdev *)hwdev)->qinfo_type == HINIC3_QINFO_TYPE_QPOOL) {
		nic_dev = ((struct hinic3_hwdev *)hwdev)->dev_handle;
		nic_indir_tbl = (struct nic_rss_indirect_tbl *)cmd_buf->buf;
		memset(nic_indir_tbl, 0, sizeof(struct nic_rss_indirect_tbl));

		indir_tbl_udata = (struct nic_rss_indirect_tbl_user_data *)&nic_indir_tbl->dw0.user_data;
		indir_tbl_udata->qgrp_id = nic_dev->hwdev->qpool_qgrp_id - HINIC3_QGRP_START_INDEX;
		indir_tbl_udata->op_code = 1;

		nic_indir_tbl->dw0.user_data = cpu_to_be32(nic_indir_tbl->dw0.user_data);

		err = hinic3_rss_get_indir_tbl_qpool(nic_indir_tbl, nic_dev->fd);
	} else {
		err = hinic3_cmdq_detail_resp(hwdev, HINIC3_MOD_L2NIC,
				      HINIC3_UCODE_CMD_GET_RSS_INDIR_TABLE,
				      cmd_buf, cmd_buf, 0);
	}
	if (err) {
		PMD_DRV_LOG(ERR, "Get rss indir table failed");
		hinic3_free_cmd_buf(cmd_buf);
		return err;
	}

	indir_tbl = (u16 *)cmd_buf->buf;
	if (indir_tbl == NULL) {
		PMD_DRV_LOG(ERR, "Get rss indir table failed, cmd_buf buf is null.");
		hinic3_free_cmd_buf(cmd_buf);
		return err;
	}

	if (((struct hinic3_hwdev *)hwdev)->qinfo_type == HINIC3_QINFO_TYPE_QPOOL) {
		for (i = 0; i < indir_table_size; i++) {
			PMD_DRV_LOG(ERR, "i: %d, *(indir_tbl + i): %d", i, *(indir_tbl + i));
			entry = hinic3_find_by_local_qid(*(indir_tbl + i));
			indir_table[i] = entry ? entry->q_id : 0xFFF;
		}
	} else {
		for (i = 0; i < indir_table_size; i++)
			indir_table[i] = *(indir_tbl + i);
	}

	hinic3_free_cmd_buf(cmd_buf);
	return 0;
}

int hinic3_rss_set_indir_tbl(void *hwdev, const u32 *indir_table, u32 indir_table_size)
{
	struct nic_rss_indirect_tbl *indir_tbl = NULL;
	struct hinic3_cmd_buf *cmd_buf = NULL;
	u32 i, size;
	u32 *temp = NULL;
	u64 out_param = 0;
	int err;

	if (!hwdev || !indir_table)
		return -EINVAL;

	cmd_buf = hinic3_alloc_cmd_buf(hwdev);
	if (!cmd_buf) {
		PMD_DRV_LOG(ERR, "Allocate cmd buf failed");
		return -ENOMEM;
	}

	cmd_buf->size = sizeof(struct nic_rss_indirect_tbl);
	indir_tbl = (struct nic_rss_indirect_tbl *)cmd_buf->buf;
	memset(indir_tbl, 0, sizeof(*indir_tbl));

	for (i = 0; i < indir_table_size; i++)
		indir_tbl->entry[i] = (u16)(*(indir_table + i));

	rte_mb();
	size = sizeof(indir_tbl->entry) / sizeof(u32);
	temp = (u32 *)indir_tbl->entry;
	for (i = 0; i < size; i++)
		temp[i] = cpu_to_be32(temp[i]);

	err = hinic3_cmdq_direct_resp(hwdev, HINIC3_MOD_L2NIC,
				      HINIC3_UCODE_CMD_SET_RSS_INDIR_TABLE,
				      cmd_buf, &out_param, 0);
	if (err || out_param != 0) {
		PMD_DRV_LOG(ERR, "Set rss indir table failed");
		err = -EFAULT;
	}

	hinic3_free_cmd_buf(cmd_buf);
	return err;
}

int hinic3_rss_set_indir_tbl_qpool(void *hwdev, const u32 *indir_table, u32 indir_table_size)
{
	struct drv_cmd_rss_indir_tbl cmd_indir_tbl = {0};
	struct msg_module msg_to_kernel = {0};
	struct nic_rss_indirect_tbl *indir_tbl = &(cmd_indir_tbl.rss_indir);
	u32 i;
	int err, fd;
	int in_size = sizeof(struct drv_cmd_rss_indir_tbl);
	int out_size = sizeof(struct drv_cmd_rss_indir_tbl);

	if (!hwdev || !indir_table)
		return -EINVAL;

	struct rte_eth_dev * eth_dev = (struct rte_eth_dev *)(((struct hinic3_hwdev *)hwdev)->eth_dev);
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(eth_dev);

	for (i = 0; i < indir_table_size; i++)
		indir_tbl->entry[i] = (u16)(*(indir_table + i));

	rte_mb();

	fd = nic_dev->fd;
	fill_ioctl_msg(&msg_to_kernel, SEND_TO_NIC_DRIVER, SET_RSS_INDIR_TBL, in_size, out_size,
		&cmd_indir_tbl, &cmd_indir_tbl);

	err = ioctl(fd, 0, &msg_to_kernel);
	if (err < 0) {
		PMD_DRV_LOG(ERR, "Set qpool indir table error: %d.", err);
		return -1;
	}
	return err;
}

#define NIC_RSS_CONTEXT_CMD_RSS_QUEUE 1

static int hinic3_cmdq_set_rss_type_ioctl(struct hinic3_nic_dev *nic_dev, struct hinic3_rss_type rss_type)
{
	struct msg_module msg_to_kernel = {0};
	int err;
	struct nic_rss_context_tbl *ctx_tbl = NULL;
	struct hinic3_cmd_buf *cmd_buf = NULL;
	struct hinic3_hwdev *hwdev = NULL;
	u32 ctx = 0;

	if (!nic_dev->hwdev)
		return -EINVAL;
	hwdev = nic_dev->hwdev;

	cmd_buf = hinic3_alloc_cmd_buf(hwdev);
	if (!cmd_buf) {
		PMD_DRV_LOG(ERR, "Allocate cmd buf failed");
		return -ENOMEM;
	}

	ctx |= HINIC3_RSS_TYPE_SET(1, VALID) |
	       HINIC3_RSS_TYPE_SET(rss_type.ipv4, IPV4) |
	       HINIC3_RSS_TYPE_SET(rss_type.ipv6, IPV6) |
	       HINIC3_RSS_TYPE_SET(rss_type.ipv6_ext, IPV6_EXT) |
	       HINIC3_RSS_TYPE_SET(rss_type.tcp_ipv4, TCP_IPV4) |
	       HINIC3_RSS_TYPE_SET(rss_type.tcp_ipv6, TCP_IPV6) |
	       HINIC3_RSS_TYPE_SET(rss_type.tcp_ipv6_ext, TCP_IPV6_EXT) |
	       HINIC3_RSS_TYPE_SET(rss_type.udp_ipv4, UDP_IPV4) |
	       HINIC3_RSS_TYPE_SET(rss_type.udp_ipv6, UDP_IPV6);
	cmd_buf->size = sizeof(struct nic_rss_context_tbl);
	ctx_tbl = (struct nic_rss_context_tbl *)cmd_buf->buf;
	memset(ctx_tbl, 0, sizeof(*ctx_tbl));
	rte_mb();

	ctx_tbl->q_grp_id = cpu_to_be16(hwdev->qpool_qgrp_id - 2048);
	ctx_tbl->cmd_type = cpu_to_be16(NIC_RSS_CONTEXT_CMD_RSS_QUEUE);
	ctx_tbl->ctx = cpu_to_be32(ctx);

	fill_ioctl_msg(&msg_to_kernel, SEND_TO_NPU, 0,
			sizeof(struct nic_rss_context_tbl),
			sizeof(struct nic_rss_context_tbl),
			ctx_tbl, ctx_tbl);
	msg_to_kernel.npu_cmd.direct_resp = 1;
	msg_to_kernel.npu_cmd.mod = HINIC3_MOD_L2NIC;
	msg_to_kernel.npu_cmd.cmd = HINIC3_UCODE_CMD_SET_RSS_CONTEXT_TABLE;
	msg_to_kernel.npu_cmd.ack_type = HINIC3_ACK_TYPE_CMDQ;

	err = ioctl(nic_dev->fd, 0, &msg_to_kernel);
	if (err < 0)
		PMD_DRV_LOG(ERR, "Set qpool rss type ctx error: %d.", err);


	hinic3_free_cmd_buf(cmd_buf);

	return err;
}

static int hinic3_cmdq_set_rss_type(void *hwdev, struct hinic3_rss_type rss_type)
{
	struct nic_rss_context_tbl *ctx_tbl = NULL;
	struct hinic3_cmd_buf *cmd_buf = NULL;
	u32 ctx = 0;
	u64 out_param = 0;
	int err;

	if (!hwdev)
		return -EINVAL;

	cmd_buf = hinic3_alloc_cmd_buf(hwdev);
	if (!cmd_buf) {
		PMD_DRV_LOG(ERR, "Allocate cmd buf failed");
		return -ENOMEM;
	}

	ctx |= HINIC3_RSS_TYPE_SET(1, VALID) |
	       HINIC3_RSS_TYPE_SET(rss_type.ipv4, IPV4) |
	       HINIC3_RSS_TYPE_SET(rss_type.ipv6, IPV6) |
	       HINIC3_RSS_TYPE_SET(rss_type.tcp_ipv4, TCP_IPV4) |
	       HINIC3_RSS_TYPE_SET(rss_type.tcp_ipv6, TCP_IPV6) |
	       HINIC3_RSS_TYPE_SET(rss_type.udp_ipv4, UDP_IPV4) |
	       HINIC3_RSS_TYPE_SET(rss_type.udp_ipv6, UDP_IPV6);

	cmd_buf->size = sizeof(struct nic_rss_context_tbl);
	ctx_tbl = (struct nic_rss_context_tbl *)cmd_buf->buf;
	memset(ctx_tbl, 0, sizeof(*ctx_tbl));
	rte_mb();
	ctx_tbl->ctx = cpu_to_be32(ctx);

	/* Cfg the RSS context table by command queue */
	err = hinic3_cmdq_direct_resp(hwdev, HINIC3_MOD_L2NIC,
				      HINIC3_UCODE_CMD_SET_RSS_CONTEXT_TABLE,
				      cmd_buf, &out_param, 0);

	hinic3_free_cmd_buf(cmd_buf);

	if (err || out_param != 0) {
		PMD_DRV_LOG(ERR, "Cmdq set rss context table failed, err: %d", err);
		return -EFAULT;
	}

	return 0;
}

static int hinic3_mgmt_set_rss_type(void *hwdev, struct hinic3_rss_type rss_type)
{
	struct hinic3_rss_context_table ctx_tbl;
	u32 ctx = 0;
	u16 out_size = sizeof(ctx_tbl);
	int err;

	if (!hwdev)
		return -EINVAL;

	memset(&ctx_tbl, 0, sizeof(ctx_tbl));
	ctx_tbl.func_id = hinic3_global_func_id(hwdev);
	ctx |= HINIC3_RSS_TYPE_SET(1, VALID) |
	       HINIC3_RSS_TYPE_SET(rss_type.ipv4, IPV4) |
	       HINIC3_RSS_TYPE_SET(rss_type.ipv6, IPV6) |
	       HINIC3_RSS_TYPE_SET(rss_type.tcp_ipv4, TCP_IPV4) |
	       HINIC3_RSS_TYPE_SET(rss_type.tcp_ipv6, TCP_IPV6) |
	       HINIC3_RSS_TYPE_SET(rss_type.udp_ipv4, UDP_IPV4) |
	       HINIC3_RSS_TYPE_SET(rss_type.udp_ipv6, UDP_IPV6);
	ctx_tbl.context = ctx;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC3_NIC_CMD_SET_RSS_CTX_TBL_INTO_FUNC,
				     &ctx_tbl, sizeof(ctx_tbl),
				     &ctx_tbl, &out_size);
	if (ctx_tbl.msg_head.status == HINIC3_MGMT_CMD_UNSUPPORTED) {
		return HINIC3_MGMT_CMD_UNSUPPORTED;
	} else if (err || !out_size || ctx_tbl.msg_head.status) {
		PMD_DRV_LOG(ERR, "Mgmt set rss context table failed,  err: %d, "
			    "status: 0x%x, out size: 0x%x",
			    err, ctx_tbl.msg_head.status, out_size);
		return -EINVAL;
	}
	return 0;
}

int hinic3_set_rss_type(void *hwdev, struct hinic3_rss_type rss_type)
{
	struct hinic3_nic_dev *nic_dev = ((struct hinic3_hwdev *)hwdev)->dev_handle;
	int err;

	if (nic_dev->hwdev->qinfo_type == HINIC3_QINFO_TYPE_QPOOL) {
		return hinic3_cmdq_set_rss_type_ioctl(nic_dev, rss_type);
	}

	err = hinic3_mgmt_set_rss_type(hwdev, rss_type);
	if (err != HINIC3_MGMT_CMD_UNSUPPORTED)
		return err;

	return hinic3_cmdq_set_rss_type(hwdev, rss_type);
}

int hinic3_get_rss_type(void *hwdev, struct hinic3_rss_type *rss_type)
{
	struct hinic3_rss_context_table ctx_tbl;
	u16 out_size = sizeof(ctx_tbl);
	int err;

	if (!hwdev || !rss_type)
		return -EINVAL;

	memset(&ctx_tbl, 0, sizeof(struct hinic3_rss_context_table));

	if (((struct hinic3_hwdev *)hwdev)->qinfo_type == HINIC3_QINFO_TYPE_QPOOL) {
		ctx_tbl.func_id = ((struct hinic3_hwdev *)hwdev)->qpool_qgrp_id;
	} else {
		ctx_tbl.func_id = hinic3_global_func_id(hwdev);
	}

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC3_NIC_CMD_GET_RSS_CTX_TBL,
				     &ctx_tbl, sizeof(ctx_tbl),
				     &ctx_tbl, &out_size);
	if (err || !out_size || ctx_tbl.msg_head.status) {
		PMD_DRV_LOG(ERR, "Get hash type failed, err: %d, status: 0x%x, out size: 0x%x",
			    err, ctx_tbl.msg_head.status, out_size);
		return -EFAULT;
	}

	rss_type->ipv4	       = HINIC3_RSS_TYPE_GET(ctx_tbl.context, IPV4);
	rss_type->ipv6	       = HINIC3_RSS_TYPE_GET(ctx_tbl.context, IPV6);
	rss_type->tcp_ipv4     = HINIC3_RSS_TYPE_GET(ctx_tbl.context, TCP_IPV4);
	rss_type->tcp_ipv6     = HINIC3_RSS_TYPE_GET(ctx_tbl.context, TCP_IPV6);
	rss_type->udp_ipv4     = HINIC3_RSS_TYPE_GET(ctx_tbl.context, UDP_IPV4);
	rss_type->udp_ipv6     = HINIC3_RSS_TYPE_GET(ctx_tbl.context, UDP_IPV6);

	return 0;
}

static int hinic3_rss_cfg_hash_engine(void *hwdev, u8 opcode, u8 *type)
{
	struct hinic3_cmd_rss_engine_type hash_type;
	u16 out_size = sizeof(hash_type);
	int err;

	if (!hwdev || !type)
		return -EINVAL;

	memset(&hash_type, 0, sizeof(struct hinic3_cmd_rss_engine_type));

	if (((struct hinic3_hwdev *)hwdev)->qinfo_type == HINIC3_QINFO_TYPE_QPOOL) {
		hash_type.func_id = ((struct hinic3_hwdev *)hwdev)->qpool_qgrp_id;
	} else {
		hash_type.func_id = hinic3_global_func_id(hwdev);
	}

	hash_type.opcode = opcode;
	if (opcode == HINIC3_CMD_OP_SET)
		hash_type.hash_engine = *type;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC3_NIC_CMD_CFG_RSS_HASH_ENGINE,
				     &hash_type, sizeof(hash_type),
				     &hash_type, &out_size);
	if (err || !out_size || hash_type.msg_head.status) {
		PMD_DRV_LOG(ERR, "%s hash engine failed, err: %d, "
			    "status: 0x%x, out size: 0x%x",
			    opcode == HINIC3_CMD_OP_SET ? "Set" : "Get",
			    err, hash_type.msg_head.status, out_size);
		return -EFAULT;
	}

	if (opcode == HINIC3_CMD_OP_GET)
		*type = hash_type.hash_engine;

	return 0;
}

int hinic3_rss_get_hash_engine(void *hwdev, u8 *type)
{
	if (!hwdev || !type)
		return -EINVAL;

	return hinic3_rss_cfg_hash_engine(hwdev, HINIC3_CMD_OP_GET, type);
}

int hinic3_rss_set_hash_engine(void *hwdev, u8 type)
{
	if (!hwdev)
		return -EINVAL;

	return hinic3_rss_cfg_hash_engine(hwdev, HINIC3_CMD_OP_SET, &type);
}

int hinic3_rss_cfg(void *hwdev, u8 rss_en, u8 tc_num, u8 *prio_tc)
{
	struct hinic3_cmd_rss_config rss_cfg;
	u16 out_size = sizeof(rss_cfg);
	int err;

	/* Ucode requires number of TC should be power of 2 */
	if (!hwdev || !prio_tc || (tc_num & (tc_num - 1)))
		return -EINVAL;

	if (((struct hinic3_hwdev *)hwdev)->qinfo_type == HINIC3_QINFO_TYPE_QPOOL)
		return 0;

	memset(&rss_cfg, 0, sizeof(struct hinic3_cmd_rss_config));
	rss_cfg.func_id = hinic3_global_func_id(hwdev);
	rss_cfg.rss_en = rss_en;
	rss_cfg.rq_priority_number = tc_num ? (u8)ilog2(tc_num) : 0;

	memcpy(rss_cfg.prio_tc, prio_tc, HINIC3_DCB_UP_MAX);
	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC3_NIC_CMD_RSS_CFG,
				     &rss_cfg, sizeof(rss_cfg),
				     &rss_cfg, &out_size);
	if (err || !out_size || rss_cfg.msg_head.status) {
		PMD_DRV_LOG(ERR, "Set rss cfg failed, err: %d, "
			    "status: 0x%x, out size: 0x%x",
			    err, rss_cfg.msg_head.status, out_size);
		return -EFAULT;
	}

	return 0;
}

int hinic3_vf_get_default_cos(void *hwdev, u8 *cos_id)
{
	struct hinic3_cmd_vf_dcb_state vf_dcb;
	u16 out_size = sizeof(vf_dcb);
	int err;

	memset(&vf_dcb, 0, sizeof(vf_dcb));

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC3_NIC_CMD_VF_COS, &vf_dcb,
				     sizeof(vf_dcb), &vf_dcb, &out_size);
	if (err || !out_size || vf_dcb.msg_head.status) {
		PMD_DRV_LOG(ERR, "Get VF default cos failed, err: %d, status: 0x%x, out size: 0x%x",
			    err, vf_dcb.msg_head.status, out_size);
		return -EIO;
	}

	*cos_id = vf_dcb.state.default_cos;

	return 0;
}

int hinic3_set_fdir_ethertype_filter(void *hwdev, u8 pkt_type, struct rte_eth_ethertype_filter *ethertype_filter, u8 en)
{
	struct hinic3_set_fdir_ethertype_rule ethertype_cmd;
	u16 out_size = sizeof(ethertype_cmd);
	int err;

	if (!hwdev)
		return -EINVAL;

	memset(&ethertype_cmd, 0, sizeof(struct hinic3_set_fdir_ethertype_rule));
	ethertype_cmd.func_id = hinic3_global_func_id(hwdev);
	ethertype_cmd.pkt_type = pkt_type;
	ethertype_cmd.pkt_type_en = en;
	ethertype_cmd.qid = (u8)ethertype_filter->queue;
	if (en == 0)
		ethertype_cmd.flags = 0;
	else
 		ethertype_cmd.flags = (u8)ethertype_filter->flags;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC3_NIC_CMD_SET_FDIR_STATUS,
				     &ethertype_cmd, sizeof(ethertype_cmd),
				     &ethertype_cmd, &out_size);
	if (err || ethertype_cmd.head.status || !out_size) {
		PMD_DRV_LOG(ERR,
			    "set fdir ethertype rule failed, err: %d, status: 0x%x, out size: 0x%x func_id %d",
			    err, ethertype_cmd.head.status, out_size, ethertype_cmd.func_id);
		return -EIO;
	}

	return 0;
}

int hinic3_add_tcam_rule(void *hwdev, struct hinic3_tcam_cfg_rule *tcam_rule, u8 tcam_rule_type)
{
	struct hinic3_fdir_add_rule tcam_cmd;
	u16 out_size = sizeof(tcam_cmd);
	int err;

	if (!hwdev || !tcam_rule)
		return -EINVAL;

	if (tcam_rule->index >= HINIC3_MAX_TCAM_RULES_NUM) {
		PMD_DRV_LOG(ERR, "Tcam rules num to add is invalid");
		return -EINVAL;
	}

	memset(&tcam_cmd, 0, sizeof(struct hinic3_fdir_add_rule));
	tcam_cmd.func_id = hinic3_global_func_id(hwdev);

#ifdef HINIC3_TRAFFIC_BIFUR
	/* Process of enabling group ext_info in the MPU */
	u8 bifur_en, iso_en;

	if (hinic3_get_bifur_enable(hwdev, &bifur_en, &iso_en, 0) != 0)
		PMD_DRV_LOG(ERR, "hinic3 get port table bifur enable status failed.");

	if (bifur_en)
		tcam_cmd.bifur_rss_en = 1;
#endif
	memcpy((void *)&tcam_cmd.rule, (void *)tcam_rule,
		sizeof(struct hinic3_tcam_cfg_rule));
	tcam_cmd.type = tcam_rule_type;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC3_NIC_CMD_ADD_TC_FLOW,
				     &tcam_cmd, sizeof(tcam_cmd),
				     &tcam_cmd, &out_size);
	if (err || tcam_cmd.msg_head.status || !out_size) {
		PMD_DRV_LOG(ERR,
			    "Add tcam rule failed, err: %d, status: 0x%x, out size: 0x%x",
			    err, tcam_cmd.msg_head.status, out_size);
		return -EIO;
	}

	return 0;
}

int hinic3_del_tcam_rule(void *hwdev, u32 index, u8 tcam_rule_type)
{
	struct hinic3_fdir_del_rule tcam_cmd = {0};
	u16 out_size = sizeof(tcam_cmd);
	int err;

	if (!hwdev)
		return -EINVAL;

	if (index >= HINIC3_MAX_TCAM_RULES_NUM) {
		PMD_DRV_LOG(ERR, "Tcam rules num to del is invalid");
		return -EINVAL;
	}

	if (((struct hinic3_hwdev *)hwdev)->qinfo_type == HINIC3_QINFO_TYPE_QPOOL)
		tcam_cmd.func_id = ((struct hinic3_hwdev *)hwdev)->qpool_qgrp_id;
	else
		tcam_cmd.func_id = hinic3_global_func_id(hwdev);

	tcam_cmd.index_start = index;
	tcam_cmd.index_num = 1;
	tcam_cmd.type = tcam_rule_type;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC3_NIC_CMD_DEL_TC_FLOW,
				     &tcam_cmd, sizeof(tcam_cmd),
				     &tcam_cmd, &out_size);
	if (err || tcam_cmd.msg_head.status || !out_size) {
		PMD_DRV_LOG(ERR,
			    "Del tcam rule failed, err: %d, status: 0x%x, out size: 0x%x",
			    err, tcam_cmd.msg_head.status, out_size);
		return -EIO;
	}

	return 0;
}

static int hinic3_cfg_tcam_block(void *hwdev, u8 alloc_en, u16 *index)
{
	struct hinic3_tcam_block tcam_block_info = {0};
	u16 out_size = sizeof(tcam_block_info);
	int err;

	if (!hwdev)
		return -EINVAL;

	if (((struct hinic3_hwdev *)hwdev)->qinfo_type == HINIC3_QINFO_TYPE_QPOOL)
		tcam_block_info.func_id = ((struct hinic3_hwdev *)hwdev)->qpool_qgrp_id;
	else
		tcam_block_info.func_id = hinic3_global_func_id(hwdev);
	tcam_block_info.alloc_en = alloc_en;
	tcam_block_info.tcam_type = HINIC3_TCAM_BLOCK_NORMAL_TYPE;
	tcam_block_info.tcam_block_index = *index;

	err = l2nic_msg_to_mgmt_sync(hwdev,
				     HINIC3_NIC_CMD_CFG_TCAM_BLOCK,
				     &tcam_block_info, sizeof(tcam_block_info),
				     &tcam_block_info, &out_size);
	if (err || (!out_size) || tcam_block_info.msg_head.status) {
		PMD_DRV_LOG(ERR,
			    "Set tcam block failed, err: %d, status: 0x%x, out size: 0x%x",
			    err, tcam_block_info.msg_head.status,
			    out_size);
		return -EIO;
	}

	if (alloc_en)
		*index = tcam_block_info.tcam_block_index;

	return 0;
}

int hinic3_alloc_tcam_block(void *hwdev, u16 *index)
{
	return hinic3_cfg_tcam_block(hwdev, HINIC3_TCAM_BLOCK_ENABLE, index);
}

int hinic3_free_tcam_block(void *hwdev, u16 *index)
{
	return hinic3_cfg_tcam_block(hwdev, HINIC3_TCAM_BLOCK_DISABLE, index);
}

int hinic3_flush_tcam_rule(void *hwdev)
{
	struct hinic3_flush_tcam_rules tcam_flush = {0};
	u16 out_size = sizeof(tcam_flush);
	int err;

	if (!hwdev)
		return -EINVAL;

	if (((struct hinic3_hwdev *)hwdev)->qinfo_type == HINIC3_QINFO_TYPE_QPOOL)
		tcam_flush.func_id = ((struct hinic3_hwdev*)hwdev)->qpool_qgrp_id;
	else
		tcam_flush.func_id = hinic3_global_func_id(hwdev);

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC3_NIC_CMD_FLUSH_TCAM,
				     &tcam_flush,
				     sizeof(struct hinic3_flush_tcam_rules),
				     &tcam_flush, &out_size);
	if (tcam_flush.msg_head.status == HINIC3_MGMT_CMD_UNSUPPORTED) {
		err = HINIC3_MGMT_CMD_UNSUPPORTED;
		PMD_DRV_LOG(INFO, "Firmware/uP doesn't support flush tcam fdir");
	} else if (err || (!out_size) || tcam_flush.msg_head.status) {
		PMD_DRV_LOG(ERR,
			    "Flush tcam fdir rules failed, err: %d, status: 0x%x, out size: 0x%x",
			    err, tcam_flush.msg_head.status, out_size);
		err = -EIO;
	}

	return err;
}

int hinic3_set_fdir_tcam_rule_filter(void *hwdev, bool enable)
{
	struct hinic3_port_tcam_info port_tcam_cmd;
	u16 out_size = sizeof(port_tcam_cmd);
	int err;

	if (!hwdev)
		return -EINVAL;

	memset(&port_tcam_cmd, 0, sizeof(port_tcam_cmd));
	port_tcam_cmd.func_id = hinic3_global_func_id(hwdev);

	if (((struct hinic3_hwdev *)hwdev)->qinfo_type == HINIC3_QINFO_TYPE_QPOOL)
		port_tcam_cmd.tcam_enable = 1;
	else
		port_tcam_cmd.tcam_enable = (u8)enable;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC3_NIC_CMD_ENABLE_TCAM,
				     &port_tcam_cmd, sizeof(port_tcam_cmd),
				     &port_tcam_cmd, &out_size);
	if ((port_tcam_cmd.msg_head.status !=
		HINIC3_MGMT_CMD_UNSUPPORTED &&
		port_tcam_cmd.msg_head.status) || err || !out_size) {
		PMD_DRV_LOG(ERR, "Set fdir tcam filter failed, err: %d, "
			    "status: 0x%x, out size: 0x%x, enable: 0x%x",
			    err, port_tcam_cmd.msg_head.status, out_size,
			    enable);
		return -EIO;
	}

	if (port_tcam_cmd.msg_head.status == HINIC3_MGMT_CMD_UNSUPPORTED) {
		err = HINIC3_MGMT_CMD_UNSUPPORTED;
		PMD_DRV_LOG(WARNING, "Fw doesn't support setting fdir tcam filter");
	}

	return err;
}

static int hinic3_set_rq_flush_qpool(struct hinic3_cmd_set_rq_flush *rq_flush_msg, int fd)
{
	struct msg_module msg_to_kernel = {0};
	int err;

	fill_ioctl_msg(&msg_to_kernel, SEND_TO_NPU, 0,
			sizeof(struct hinic3_cmd_set_rq_flush),
			sizeof(struct hinic3_cmd_set_rq_flush),
			rq_flush_msg, rq_flush_msg);
	msg_to_kernel.npu_cmd.direct_resp = 1;
	msg_to_kernel.npu_cmd.mod = HINIC3_MOD_L2NIC;
	msg_to_kernel.npu_cmd.cmd = HINIC3_UCODE_CMD_SET_RQ_FLUSH;
	msg_to_kernel.npu_cmd.ack_type = HINIC3_ACK_TYPE_CMDQ;

	err = ioctl(fd, 0, &msg_to_kernel);
	if (err < 0)
		PMD_DRV_LOG(ERR, "Set qpool rx flush err : %d.", errno);
	
	return err;
}

int hinic3_set_rq_flush(void *hwdev, u16 q_id)
{
	struct hinic3_cmd_set_rq_flush *rq_flush_msg = NULL;
	struct hinic3_cmd_buf *cmd_buf = NULL;
	struct hinic3_nic_dev *nic_dev = NULL;
	u64 out_param = EIO;
	int err;

	cmd_buf = hinic3_alloc_cmd_buf(hwdev);
	if (!cmd_buf) {
		PMD_DRV_LOG(ERR, "Failed to allocate cmd buf\n");
		return -ENOMEM;
	}

	cmd_buf->size = sizeof(*rq_flush_msg);

	rq_flush_msg = cmd_buf->buf;
	rq_flush_msg->local_rq_id = q_id; //lint !e40 !e63
	rte_mb();
	rq_flush_msg->value = cpu_to_be32(rq_flush_msg->value);

	if (((struct hinic3_hwdev *)hwdev)->qinfo_type == HINIC3_QINFO_TYPE_QPOOL) {
		out_param = 0;
		nic_dev = ((struct hinic3_hwdev *)hwdev)->dev_handle;
		err = hinic3_set_rq_flush_qpool(rq_flush_msg, nic_dev->fd);
	} else {
		err = hinic3_cmdq_direct_resp(hwdev, HINIC3_MOD_L2NIC,
				      HINIC3_UCODE_CMD_SET_RQ_FLUSH, cmd_buf,
				      &out_param, 0);
	}
	if ((err) || (out_param != 0)) {
		PMD_DRV_LOG(ERR, "Failed to set rq flush, err:%d, out_param:0x%lx\n",
			    err, out_param);
		err = -EFAULT;
	}

	hinic3_free_cmd_buf(cmd_buf);

	return err;
}

static int _mag_msg_to_mgmt_sync(void *hwdev, u16 cmd, void *buf_in,
				 u16 in_size, void *buf_out, u16 *out_size)
{
	u32 i, cmd_cnt = ARRAY_LEN(vf_mag_cmd_handler);
	struct hinic3_nic_dev *nic_dev = ((struct hinic3_hwdev *)hwdev)->dev_handle;

	if (nic_dev->hwdev->qinfo_type != HINIC3_QINFO_TYPE_QPOOL &&
	    hinic3_func_type(hwdev) == TYPE_VF) {
		for (i = 0; i < cmd_cnt; i++) {
			if (cmd == vf_mag_cmd_handler[i].cmd)
				return hinic3_mbox_to_pf(hwdev, HINIC3_MOD_HILINK,
							cmd, buf_in, in_size,
							buf_out, out_size, 0);
		}
	}

	return hinic3_msg_to_mgmt_sync(hwdev, HINIC3_MOD_HILINK, cmd, buf_in,
				      in_size, buf_out, out_size, 0);
}

static int mag_msg_to_mgmt_sync(void *hwdev, u16 cmd, void *buf_in, u16 in_size,
				void *buf_out, u16 *out_size)
{
	return _mag_msg_to_mgmt_sync(hwdev, cmd, buf_in, in_size, buf_out,
				     out_size);
}

int hinic3_set_link_status_follow(void *hwdev, enum hinic3_link_follow_status status)
{
	struct mag_cmd_set_link_follow follow;
	u16 out_size = sizeof(follow);
	int err;

	if (!hwdev)
		return -EINVAL;

	if (((struct hinic3_hwdev *)hwdev)->qinfo_type == HINIC3_QINFO_TYPE_QPOOL) {
		PMD_DRV_LOG(WARNING, "Qpool mode not support set link status flow.");
		return 0;
	}

	if (status >= HINIC3_LINK_FOLLOW_STATUS_MAX) {
		PMD_DRV_LOG(ERR, "Invalid link follow status: %d\n", status);
		return -EINVAL;
	}

	memset(&follow, 0, sizeof(follow));
	follow.function_id = hinic3_global_func_id(hwdev);
	follow.follow = status;

	err = mag_msg_to_mgmt_sync(hwdev, MAG_CMD_SET_LINK_FOLLOW, &follow,
				   sizeof(follow), &follow, &out_size);
	if ((follow.head.status != HINIC3_MGMT_CMD_UNSUPPORTED && follow.head.status) || err || !out_size) {
		PMD_DRV_LOG(ERR, "Failed to set link status follow port status, err: %d, status: 0x%x, out size: 0x%x\n",
			    err, follow.head.status, out_size);
		return -EFAULT;
	}

	return follow.head.status;
}

int
hinic3_sync_dcb_state(void *hwdev, u8 op_code, u8 state)
{
	struct hinic3_cmd_set_dcb_state dcb_state;
	u16 out_size = sizeof(dcb_state);
	int err;

	if (!hwdev)
		return -EINVAL;

	memset(&dcb_state, 0, sizeof(dcb_state));

	dcb_state.op_code = op_code;
	dcb_state.state = state;
	dcb_state.func_id = hinic3_global_func_id(hwdev);

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC3_NIC_CMD_QOS_DCB_STATE,
				     &dcb_state, sizeof(dcb_state), &dcb_state,
				     &out_size);
	if (err || dcb_state.head.status || !out_size) {
		PMD_DRV_LOG(ERR,
			    "Failed to set dcb state, err: %d, status: 0x%x, "
			    "out size: 0x%x",
			    err, dcb_state.head.status, out_size);
		return -EFAULT;
	}
	return 0;
}

int
hinic3_sync_qos_map(void *hwdev, struct hinic3_dcb_config *dcb_cfg)
{
	struct hinic3_cmd_qos_map_cfg qos_cfg;
	u16 out_size = sizeof(qos_cfg);
	u8 i;
	int err;

	if (!hwdev)
		return -EINVAL;

	memset(&qos_cfg, 0, sizeof(qos_cfg));
	qos_cfg.op_code = CMD_QOS_OP_GET;
	qos_cfg.cfg_bitmap |= CMD_QOS_MAP_PCP2COS;
	qos_cfg.cfg_bitmap |= CMD_QOS_MAP_DSCP2COS;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC3_NIC_CMD_QOS_MAP_CFG,
				     &qos_cfg, sizeof(qos_cfg), &qos_cfg,
				     &out_size);
	if (err || qos_cfg.head.status || !out_size) {
		PMD_DRV_LOG(ERR,
			    "Failed to set qos, err: %d, status: 0x%x, "
			    "out size: 0x%x",
			    err, qos_cfg.head.status, out_size);
		return -EFAULT;
	}

	for (i = 0; i < NIC_DCB_UP_MAX; i++)
		dcb_cfg->pcp2cos[i] = qos_cfg.pcp2cos[i];
	for (i = 0; i < NIC_DCB_IP_PRI_MAX; i++)
		dcb_cfg->dscp2cos[i] = qos_cfg.dscp2cos[i];

	return 0;
}

int
hinic3_set_qos_port_trust(void *hwdev, u8 trust)
{
	struct hinic3_cmd_qos_port_cfg port_cfg;
	u16 out_size = sizeof(port_cfg);
	int err;

	if (!hwdev)
		return -EINVAL;

	memset(&port_cfg, 0, sizeof(port_cfg));
	port_cfg.port_id = hinic3_physical_port_id(hwdev);
	port_cfg.op_code = CMD_QOS_OP_SET;
	port_cfg.cfg_bitmap |= CMD_QOS_PORT_TRUST;
	port_cfg.trust = trust;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC3_NIC_CMD_QOS_PORT_CFG, &port_cfg,
				     sizeof(port_cfg), &port_cfg, &out_size);

	if (err || port_cfg.head.status || !out_size) {
		PMD_DRV_LOG(
			ERR,
			"Failed to set qos port trust, err: %d, status: 0x%x, "
			"out size: 0x%x",
			err, port_cfg.head.status, out_size);
		return -EFAULT;
	}

	return 0;
}

int
hinic3_set_tm_config_tc_rate(void *hwdev, u8 tc_no, u8 rate)
{
	struct hinic3_cmd_ets_cfg ets;
	u16 out_size = sizeof(ets);
	int err;

	if (!hwdev)
		return -EINVAL;

	memset(&ets, 0, sizeof(ets));
	ets.port_id = hinic3_physical_port_id(hwdev);
	ets.op_code = CMD_QOS_OP_GET;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC3_NIC_CMD_QOS_ETS, &ets,
				     sizeof(ets), &ets, &out_size);
	if (err || ets.head.status || !out_size) {
		PMD_DRV_LOG(ERR,
			    "Failed to get tc rate, err: %d, status: 0x%x, "
			    "out size: 0x%x",
			    err, ets.head.status, out_size);
		return err;
	}

	ets.op_code = CMD_QOS_OP_SET;
	ets.cfg_bitmap |= CMD_QOS_ETS_TC_RATELIMIT;
	ets.rate_limit[tc_no] = rate;
	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC3_NIC_CMD_QOS_ETS, &ets,
				     sizeof(ets), &ets, &out_size);
	if (err || ets.head.status || !out_size)
		PMD_DRV_LOG(ERR,
			    "Failed to set tc rate, err: %d, status: 0x%x, "
			    "out size: 0x%x",
			    err, ets.head.status, out_size);

	return err;
}

int
hinic3_set_tm_hierarchy_do_commit(void *hwdev, u8 *cos_tc, u8 *tc_bw,
			       u8 *rate_limit)
{
	struct hinic3_cmd_ets_cfg ets;
	u16 out_size = sizeof(ets);
	int err;

	if (!hwdev)
		return -EINVAL;

	memset(&ets, 0, sizeof(ets));
	ets.port_id = hinic3_physical_port_id(hwdev);
	ets.op_code = CMD_QOS_OP_SET;
	ets.cfg_bitmap |= CMD_QOS_ETS_COS_TC | CMD_QOS_ETS_TC_BW |
			  CMD_QOS_ETS_TC_RATELIMIT;

	memcpy(ets.cos_tc, cos_tc, NIC_DCB_COS_MAX);
	memcpy(ets.tc_bw, tc_bw, NIC_DCB_TC_MAX);
	memcpy(ets.rate_limit, rate_limit, NIC_DCB_TC_MAX);

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC3_NIC_CMD_QOS_ETS, &ets,
				     sizeof(ets), &ets, &out_size);
	if (err || ets.head.status || !out_size)
		PMD_DRV_LOG(ERR,
			    "Failed to config ets, err: %d, status: 0x%x, "
			    "out size: 0x%x",
			    err, ets.head.status, out_size);

	return err;
}

int
hinic3_get_bifur_enable(void *hwdev, u8 *bifur_en, u8 *iso_en, u8 *bifur_type)
{
	struct hinic3_port_flow_bifur_en_cmd bifur_cmd;
	u16 out_size = sizeof(bifur_cmd);
	int err;

	if (!hwdev)
		return -EINVAL;

	memset(&bifur_cmd, 0, sizeof(struct hinic3_port_flow_bifur_en_cmd));
	bifur_cmd.port_id     = hinic3_physical_port_id(hwdev);
	bifur_cmd.config_flag = PORT_BIFUR_CMD_GET;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC3_NIC_CMD_SET_PORT_FLOW_BIFUR_ENABLE, &bifur_cmd,
				     sizeof(bifur_cmd), &bifur_cmd, &out_size);
	if (err || bifur_cmd.msg_head.status || !out_size) {
		PMD_DRV_LOG(ERR, "get bifur status failed, err: %d, status: 0x%x, out size: 0x%x",
			    err, bifur_cmd.msg_head.status, out_size);
		return -EIO;
	}

	if (bifur_en != NULL)
		*bifur_en = bifur_cmd.flow_bifur_en;

	if (iso_en != NULL)
		*iso_en	  = bifur_cmd.iso_en;

	if (bifur_type != NULL)
		*bifur_type	  = bifur_cmd.flow_bifur_type;
	
	return 0;
}

static int hinic3_cmdq_qpool(struct nic_rss_context_tbl *ctx_tbl, int fd)
{
	struct msg_module msg_to_kernel = {0};
	int err;

	fill_ioctl_msg(&msg_to_kernel, SEND_TO_NPU, 0,
			sizeof(struct nic_rss_context_tbl),
			sizeof(struct nic_rss_context_tbl),
			ctx_tbl, ctx_tbl);
	msg_to_kernel.npu_cmd.direct_resp = 1;
	msg_to_kernel.npu_cmd.mod = HINIC3_MOD_L2NIC;
	msg_to_kernel.npu_cmd.cmd = HINIC3_UCODE_CMD_SET_RSS_CONTEXT_TABLE;
	msg_to_kernel.npu_cmd.ack_type = HINIC3_ACK_TYPE_CMDQ;

	err = ioctl(fd, 0, &msg_to_kernel);
	if (err < 0)
		PMD_DRV_LOG(ERR, "Set qpool rss queue type err: %d.", err);

	return err;
}

int
hinic3_cmdq_set_rss_queue_type(void *hwdev, struct hinic3_rss_type rss_type, u16 q_grp_id, u16 cmd_type)
{
	struct nic_rss_context_tbl *ctx_tbl = NULL;
	struct hinic3_cmd_buf *cmd_buf = NULL;
	struct hinic3_nic_dev *nic_dev = NULL;
	u32 ctx = 0;
	u64 out_param = 0;
	int err = 0;

	if (!hwdev)
		return -EINVAL;

	cmd_buf = hinic3_alloc_cmd_buf(hwdev);
	if (!cmd_buf) {
		PMD_DRV_LOG(ERR, "Allocate cmd buf failed");
		return -ENOMEM;
	}

	ctx |= HINIC3_RSS_TYPE_SET(1, VALID) |
			HINIC3_RSS_TYPE_SET(rss_type.ipv4, IPV4) |
			HINIC3_RSS_TYPE_SET(rss_type.ipv6, IPV6) |
			HINIC3_RSS_TYPE_SET(rss_type.tcp_ipv4, TCP_IPV4) |
			HINIC3_RSS_TYPE_SET(rss_type.tcp_ipv6, TCP_IPV6) |
			HINIC3_RSS_TYPE_SET(rss_type.udp_ipv4, UDP_IPV4) |
			HINIC3_RSS_TYPE_SET(rss_type.udp_ipv6, UDP_IPV6);
	cmd_buf->size = sizeof(struct nic_rss_context_tbl);
	ctx_tbl = (struct nic_rss_context_tbl *)cmd_buf->buf;
	memset(ctx_tbl, 0, sizeof(*ctx_tbl));
	rte_mb();

	ctx_tbl->ctx = cpu_to_be32(ctx);

	if (((struct hinic3_hwdev *)hwdev)->qinfo_type == HINIC3_QINFO_TYPE_QPOOL) {
		nic_dev = ((struct hinic3_hwdev *)hwdev)->dev_handle;
		ctx_tbl->q_grp_id = nic_dev->hwdev->qpool_qgrp_id;
		ctx_tbl->cmd_type = NIC_RSS_CONTEXT_CMD_RSS_QUEUE;
		err = hinic3_cmdq_qpool(ctx_tbl, nic_dev->fd);
	} else {
		ctx_tbl->q_grp_id = cpu_to_be16(q_grp_id);
		ctx_tbl->cmd_type = cpu_to_be16(cmd_type);
		/* Cfg the RSS context table by command queue */
		err = hinic3_cmdq_direct_resp(hwdev, HINIC3_MOD_L2NIC,
					HINIC3_UCODE_CMD_SET_RSS_CONTEXT_TABLE,
					cmd_buf, &out_param, 0);
	}

	hinic3_free_cmd_buf(cmd_buf);

	if (err || out_param != 0) {
		PMD_DRV_LOG(ERR, "Cmdq set rss context table failed, err: %d", err);
		return -EFAULT;
	}

	return 0;
}

int
hinic3_mgmt_cfg_qgrp_id(void *hwdev, u8 opcode, u16 *q_grp_id)
{
	struct nic_mpu_sub_msg_extend msg_extend = {0};
	struct hinic3_cmd_cfg_qgrp_id cfg_qgrp = {0};
	u16 out_size = sizeof(msg_extend);
	int err = 0;

	if (!hwdev)
		return -EINVAL;

	if (((struct hinic3_hwdev *)hwdev)->qinfo_type == HINIC3_QINFO_TYPE_QPOOL)
		return 0;

	if (opcode == 0)
		cfg_qgrp.q_grp_id = *q_grp_id;
	cfg_qgrp.func_id = hinic3_global_func_id(hwdev);
	cfg_qgrp.opcode = opcode;
	msg_extend.sub_cmd = HINIC3_NIC_QPOOL_CMD_CFG_QGRP_ID;
	msg_extend.sub_msg_len = sizeof(struct hinic3_cmd_cfg_qgrp_id);

	memcpy(&msg_extend.sub_msg, &cfg_qgrp, sizeof(struct hinic3_cmd_cfg_qgrp_id));

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC3_NIC_CMD_QUEUE_GROUP,
						&msg_extend, sizeof(msg_extend),
						&msg_extend, &out_size);
	if (err || msg_extend.head.status || !out_size)
		PMD_DRV_LOG(ERR,
				"Failed to get q_grp_id, err: %d, status: 0x%x, "
				"out size: 0x%x",
				err, msg_extend.head.status, out_size);

	memcpy(&cfg_qgrp, &msg_extend.sub_msg, sizeof(struct hinic3_cmd_cfg_qgrp_id));
	*q_grp_id = cfg_qgrp.q_grp_id;

	return err;
}

void
hinic3_mgmt_get_rss_id(void *hwdev, u16 func_id, u16 *rss_temp_id, u16 *rss_node_id, u16 *rss_inst_id)
{
	struct nic_mpu_sub_msg_extend msg_extend;
	struct nic_cmd_get_rss_id rss_tbl;
	u16 out_size = sizeof(msg_extend);
	int err;

	if (!hwdev) {
		return;
	}

	memset(&msg_extend, 0, sizeof(msg_extend));
	memset(&rss_tbl, 0, sizeof(rss_tbl));
	rss_tbl.func_id = func_id;
	msg_extend.sub_cmd = HINIC3_NIC_QPOOL_CMD_GET_RSS_ID;
	msg_extend.sub_msg_len = sizeof(struct nic_cmd_get_rss_id);
	memcpy(&msg_extend.sub_msg, &rss_tbl, sizeof(struct nic_cmd_get_rss_id));

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC3_NIC_CMD_QUEUE_GROUP,
								&msg_extend, sizeof(msg_extend),
								&msg_extend, &out_size);
	if (err != 0 || out_size == 0 || msg_extend.head.status != 0) {
		PMD_DRV_LOG(ERR, "mgmt Failed to get rss tbl id, err: %d, status: 0x%x, out size: 0x%x\n",
				err, msg_extend.head.status, out_size);
		return;
	}

	memcpy(&rss_tbl, &msg_extend.sub_msg, sizeof(struct nic_cmd_get_rss_id));
	*rss_temp_id = rss_tbl.rss_temp_id;
	*rss_node_id = rss_tbl.rss_node_id;
	*rss_inst_id = rss_tbl.rss_instance_id;

	return;
}

int
hinic3_rss_queue_set_indir_tbl(void *hwdev, const u32 *indir_table, u32 indir_table_size, u16 q_grp_id)
{
	struct nic_rss_indirect_tbl *indir_tbl = NULL;
	struct nic_rss_indirect_tbl_user_data *indir_tbl_udata = NULL;
	struct hinic3_cmd_buf *cmd_buf = NULL;
	u32 i, size;
	u32 *temp = NULL;
	u64 out_param = 0;
	int err;

	if (!hwdev || !indir_table)
		return -EINVAL;

	cmd_buf = hinic3_alloc_cmd_buf(hwdev);
	if (!cmd_buf) {
		PMD_DRV_LOG(ERR, "Allocate cmd buf failed");
		return -ENOMEM;
	}

	cmd_buf->size = sizeof(struct nic_rss_indirect_tbl);
	indir_tbl = (struct nic_rss_indirect_tbl *)cmd_buf->buf;
	memset(indir_tbl, 0, sizeof(*indir_tbl));
	indir_tbl_udata = (struct nic_rss_indirect_tbl_user_data *)&indir_tbl->dw0.user_data;
	indir_tbl_udata->qgrp_id = q_grp_id - HINIC3_QGRP_START_INDEX;
	indir_tbl_udata->op_code = 1;
	indir_tbl->dw0.user_data = cpu_to_be32(indir_tbl->dw0.user_data);

	for (i = 0; i < indir_table_size; i++)
		indir_tbl->entry[i] = (u16)(*(indir_table + i));

	rte_mb();
	size = sizeof(indir_tbl->entry) / sizeof(u32);
	temp = (u32 *)indir_tbl->entry;
	for (i = 0; i < size; i++)
		temp[i] = cpu_to_be32(temp[i]);

	err = hinic3_cmdq_direct_resp(hwdev, HINIC3_MOD_L2NIC,
						HINIC3_UCODE_CMD_SET_RSS_INDIR_TABLE,
						cmd_buf, &out_param, 0);
	if (err || out_param != 0) {
		PMD_DRV_LOG(ERR, "Set rss indir table failed");
		err = -EFAULT;
	}

	hinic3_free_cmd_buf(cmd_buf);

	return err;
}

int hinic3_fdir_alloc_sec_tcam_block(void *hwdev, u8 key_width, u16 *index)
{
	struct nic_cmd_fdir_ext cmd_buf = {0};
	struct hinic3_tcam_block tcam_block_info = {0};
	u16 out_size = sizeof(cmd_buf);
	int err;

	if (!hwdev)
		return -EINVAL;

	tcam_block_info.func_id = hinic3_global_func_id(hwdev);
	tcam_block_info.alloc_en = HINIC3_TCAM_BLOCK_ENABLE;
	tcam_block_info.tcam_type = HINIC3_TCAM_BLOCK_NORMAL_TYPE;

	rte_memcpy(&cmd_buf.data.alloc_block, &tcam_block_info, sizeof(tcam_block_info));
	cmd_buf.key_width = key_width;
	cmd_buf.op_code = TCAM_EXTEND_OPCODE_ALLOC_BLOCK;

	err = l2nic_msg_to_mgmt_sync(hwdev,
				     HINIC3_NIC_CMD_FDIR_EXT,
				     &cmd_buf, sizeof(cmd_buf),
				     &cmd_buf, &out_size);
	if (err || (!out_size) || cmd_buf.msg_head.status) {
		PMD_DRV_LOG(ERR,
			    "Set tcam block failed, err: %d, status: 0x%x, out size: 0x%x",
			    err, cmd_buf.msg_head.status,
			    out_size);
		return -EIO;
	}

	if (index != NULL)
		*index = cmd_buf.data.alloc_block.tcam_block_index;

	return 0;
}

int hinic3_fdir_sec_tcam_block_free(void *hwdev, u8 key_width, u16 *index)
{
	struct nic_cmd_fdir_ext cmd_buf = {0};
	struct hinic3_tcam_block tcam_block_info = {0};
	u16 out_size = sizeof(cmd_buf);
	int err;

	if (!hwdev)
		return -EINVAL;

	tcam_block_info.func_id = hinic3_global_func_id(hwdev);
	tcam_block_info.tcam_type = HINIC3_TCAM_BLOCK_NORMAL_TYPE;
	tcam_block_info.tcam_block_index = *index;
	tcam_block_info.alloc_en = HINIC3_TCAM_BLOCK_DISABLE;

	rte_memcpy(&cmd_buf.data.free_block, &tcam_block_info, sizeof(tcam_block_info));
	cmd_buf.key_width = key_width;
	cmd_buf.op_code = TCAM_EXTEND_OPCODE_FREE_BLOCK;

	err = l2nic_msg_to_mgmt_sync(hwdev,
				     HINIC3_NIC_CMD_FDIR_EXT,
				     &cmd_buf, sizeof(cmd_buf),
				     &cmd_buf, &out_size);
	if (err || (!out_size) || cmd_buf.msg_head.status) {
		PMD_DRV_LOG(ERR,
			    "Set sec tcam block failed, err: %d, status: 0x%x, out size: 0x%x",
			    err, cmd_buf.msg_head.status,
			    out_size);
		return -EIO;
	}

	return 0;
}

int hinic3_fdir_add_sec_tcam_rule(void *hwdev, struct hinic3_ext_tcam_cfg_rule *tcam_rule,
				  u8 tcam_rule_type, bool is_hairpin, u8 key_width)
{
	struct nic_cmd_fdir_ext cmd_buf = {0};
	struct hinic3_ext_fdir_add_rule tcam_cmd = {0};
	u16 out_size = sizeof(cmd_buf);
	int err;

	if (!hwdev || !tcam_rule)
		return -EINVAL;

	if (tcam_rule->index >= HINIC3_MAX_TCAM_RULES_NUM) {
		PMD_DRV_LOG(ERR, "Tcam rules num to add is invalid");
		return -EINVAL;
	}

	tcam_cmd.func_id = hinic3_global_func_id(hwdev);
	if (is_hairpin)
		tcam_cmd.bifur_rss_en |= HAIRPIN_FLAG;

	rte_memcpy((void *)&tcam_cmd.rule, (void *)tcam_rule,
		sizeof(struct hinic3_ext_tcam_cfg_rule));
	tcam_cmd.type = tcam_rule_type;

	rte_memcpy(&cmd_buf.data.tcam_add, &tcam_cmd, sizeof(tcam_cmd));
	cmd_buf.key_width = key_width;
	cmd_buf.op_code = TCAM_EXTEND_OPCODE_ADD_RULE;


	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC3_NIC_CMD_FDIR_EXT,
				     &cmd_buf, sizeof(cmd_buf),
				     &cmd_buf, &out_size);
	if (err || cmd_buf.msg_head.status || !out_size) {
		PMD_DRV_LOG(ERR,
			    "Add tcam rule failed, err: %d, status: 0x%x, out size: 0x%x",
			    err, cmd_buf.msg_head.status, out_size);
		return -EIO;
	}

	return 0;
}

int hinic3_fdir_set_fdir_sec_tcam_rule_filter(void *hwdev, bool enable)
{
	struct nic_cmd_fdir_ext cmd_buf = {0};
	struct hinic3_port_tcam_info port_tcam_cmd = {0};
	u16 out_size = sizeof(cmd_buf);
	int err;

	if (!hwdev)
		return -EINVAL;

	port_tcam_cmd.func_id = hinic3_global_func_id(hwdev);
	port_tcam_cmd.tcam_enable = (u8)enable;

	rte_memcpy(&cmd_buf.data.tcam_en, &port_tcam_cmd, sizeof(port_tcam_cmd));
	cmd_buf.op_code = TCAM_EXTEND_OPCODE_ENABLE_TCAM;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC3_NIC_CMD_FDIR_EXT,
				     &cmd_buf, sizeof(cmd_buf),
				     &cmd_buf, &out_size);
	if ((cmd_buf.msg_head.status !=
		HINIC3_MGMT_CMD_UNSUPPORTED &&
		cmd_buf.msg_head.status) || err || !out_size) {
		PMD_DRV_LOG(ERR, "Set fdir tcam filter failed, err: %d, "
			    "status: 0x%x, out size: 0x%x, enable: 0x%x",
			    err, cmd_buf.msg_head.status, out_size,
			    enable);
		return -EIO;
	}

	if (cmd_buf.msg_head.status == HINIC3_MGMT_CMD_UNSUPPORTED) {
		err = HINIC3_MGMT_CMD_UNSUPPORTED;
		PMD_DRV_LOG(ERR, "Fw doesn't support setting fdir tcam filter");
	}

	return err;
}

int hinic3_fdir_del_sec_tcam_rule(void *hwdev, u32 index, u8 tcam_rule_type,
				  u8 key_width)
{
	struct nic_cmd_fdir_ext cmd_buf = {0};
	struct hinic3_fdir_del_rule tcam_cmd = {0};
	u16 out_size = sizeof(cmd_buf);
	int err;

	if (!hwdev)
		return -EINVAL;

	if (index >= HINIC3_MAX_TCAM_RULES_NUM) {
		PMD_DRV_LOG(ERR, "Tcam rules num to del is invalid");
		return -EINVAL;
	}

	tcam_cmd.func_id = hinic3_global_func_id(hwdev);
	tcam_cmd.index_start = index;
	tcam_cmd.index_num = 1;
	tcam_cmd.type = tcam_rule_type;

	rte_memcpy(&cmd_buf.data.tcam_del, &tcam_cmd, sizeof(tcam_cmd));
	cmd_buf.key_width = key_width;
	cmd_buf.op_code = TCAM_EXTEND_OPCODE_DEL_RULES;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC3_NIC_CMD_FDIR_EXT,
				     &cmd_buf, sizeof(cmd_buf),
				     &cmd_buf, &out_size);
	if (err || cmd_buf.msg_head.status || !out_size) {
		PMD_DRV_LOG(ERR,
			    "Del tcam rule failed, err: %d, status: 0x%x, out size: 0x%x",
			    err, cmd_buf.msg_head.status, out_size);
		return -EIO;
	}

	return 0;
}

int hinic3_fdir_flush_sec_tcam_rule(void *hwdev)
{
	struct nic_cmd_fdir_ext cmd_buf = {0};
	struct hinic3_flush_tcam_rules tcam_flush = {0};
	u16 out_size = sizeof(cmd_buf);
	int err;

	if (!hwdev)
		return -EINVAL;

	tcam_flush.func_id = hinic3_global_func_id(hwdev);

	rte_memcpy(&cmd_buf.data.tcam_flush, &tcam_flush, sizeof(tcam_flush));
	cmd_buf.op_code = TCAM_EXTEND_OPCODE_FLUSH_TCAM;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC3_NIC_CMD_FDIR_EXT,
				     &cmd_buf,
				     sizeof(struct hinic3_flush_tcam_rules),
				     &cmd_buf, &out_size);
	if (cmd_buf.msg_head.status == HINIC3_MGMT_CMD_UNSUPPORTED) {
		err = HINIC3_MGMT_CMD_UNSUPPORTED;
		PMD_DRV_LOG(ERR, "Firmware/uP doesn't support flush tcam fdir");
	} else if (err || (!out_size) || cmd_buf.msg_head.status) {
		PMD_DRV_LOG(ERR,
			    "Flush sec tcam fdir rules failed, err: %d, status: 0x%x, out size: 0x%x",
			    err, cmd_buf.msg_head.status, out_size);
		err = -EIO;
	}

	return err;
}

int hinic3_fdir_cfg_sec_tcam(void *hwdev, u8 *en)
{
	struct hinic3_nic_dev *nic_dev = ((struct hinic3_hwdev *)hwdev)->dev_handle;
	struct nic_cmd_fdir_ext cmd_buf = {0};
	u16 out_size = sizeof(cmd_buf);
	int err;

	if (!hwdev)
		return -EINVAL;

	if(nic_dev->feature_cap != SP600_NIC_FEATURE)
		return 0;

	cmd_buf.op_code = TCAM_EXTEND_OPCODE_GET_FLAG;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC3_NIC_CMD_FDIR_EXT,
				     &cmd_buf,
				     sizeof(struct hinic3_flush_tcam_rules),
				     &cmd_buf, &out_size);
	if (err || cmd_buf.msg_head.status || !out_size) {
		PMD_DRV_LOG(ERR,
			    "Config sec tcam fdir rules failed, err: %d, status: 0x%x, out size: 0x%x",
			    err, cmd_buf.msg_head.status, out_size);
		err = -EIO;
	}

	if (en != NULL)
		*en = cmd_buf.data.tcam_cfg.key_mode;

	return err;
}

u64 hinic3_get_driver_feature(void *dev)
{
	struct hinic3_nic_dev *nic_dev = NULL;

	nic_dev = (struct hinic3_nic_dev *)dev;

	return nic_dev->feature_cap;
}
