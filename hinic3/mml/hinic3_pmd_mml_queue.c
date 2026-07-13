/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
 */

#include "hinic3_pmd_mml_lib.h"
#include "hinic3_pmd_mml_cmd.h"
#include "hinic3_pmd_mml_queue.h"

#define ADDR_HI_BIT 32

static int
hinic3_pmd_mml_log_ret(char *show_str, int *show_len, const char *fmt, ...)
{
	va_list args;
	int ret = 0;
	int remaining_len;

	if (*show_len >= MAX_SHOW_STR_LEN)
		return UDA_SUCCESS;

	remaining_len = MAX_SHOW_STR_LEN - *show_len;

	va_start(args, fmt);
	ret = vsnprintf(show_str + *show_len, remaining_len, fmt, args);
	va_end(args);

	if (ret > 0 && ret < remaining_len) {
		*show_len += ret;
	} else {
		PMD_DRV_LOG(ERR, "MML show string snprintf failed, err: %d\n", ret);
		return -UDA_EINVAL;
	}

	return UDA_SUCCESS;
}

static void
rx_show_rq_info(major_cmd_t *self, struct nic_rq_info *rq_info)
{
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "Receive queue information:\n");
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "queue_id:%u\n", rq_info->q_id);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "ci:%u\n", rq_info->ci);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "sw_pi:%u\n", rq_info->sw_pi);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "rq_depth:%u\n", rq_info->rq_depth);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "rq_wqebb_size:%u\n", rq_info->rq_wqebb_size);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "buf_len:%u\n", rq_info->buf_len);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "int_num:%u\n", rq_info->int_num);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "msix_vector:%u\n", rq_info->msix_vector);
}

static void
rx_show_wqe(major_cmd_t *self, nic_rq_wqe *wqe)
{
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "Rx buffer section information:\n");
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "buf_addr:0x%llx\n",
			   (((uint64_t)wqe->buf_desc.pkt_buf_addr_high) << ADDR_HI_BIT) | wqe->buf_desc.pkt_buf_addr_low);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "buf_len:%u\n", wqe->buf_desc.len);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "rsvd0:%u\n", wqe->rsvd0);

	hinic3_pmd_mml_log(self->show_str, &self->show_len, "Cqe buffer section information:\n");
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "buf_hi:0x%llx\n",
			   (((uint64_t)wqe->cqe_sect.pkt_buf_addr_high) << ADDR_HI_BIT) | wqe->cqe_sect.pkt_buf_addr_low);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "buf_len:%u\n", wqe->cqe_sect.len);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "rsvd1:%u\n", wqe->rsvd1);
}

static void
rx_show_cqe_info(major_cmd_t *self, struct tag_l2nic_rx_cqe *wqe_cs)
{
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "Rx cqe info:\n");

	hinic3_pmd_mml_log(self->show_str, &self->show_len, "cs_dw0:0x%08x\n", wqe_cs->dw0.value);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "rx_done:0x%x\n", wqe_cs->dw0.bs.rx_done);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "bp_en:0x%x\n", wqe_cs->dw0.bs.bp_en);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "decry_pkt:0x%x\n", wqe_cs->dw0.bs.decry_pkt);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "flush:0x%x\n", wqe_cs->dw0.bs.flush);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "spec_flags:0x%x\n", wqe_cs->dw0.bs.spec_flags);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "rsvd0:0x%x\n", wqe_cs->dw0.bs.rsvd0);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "lro_num:0x%x\n", wqe_cs->dw0.bs.lro_num);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "checksum_err:0x%x\n", wqe_cs->dw0.bs.checksum_err);

	hinic3_pmd_mml_log(self->show_str, &self->show_len, "cs_dw1:0x%08x\n", wqe_cs->dw1.value);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "length:%u\n", wqe_cs->dw1.bs.length);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "vlan:0x%x\n", wqe_cs->dw1.bs.vlan);

	hinic3_pmd_mml_log(self->show_str, &self->show_len, "cs_dw2:0x%08x\n", wqe_cs->dw2.value);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "rss_type:0x%x\n", wqe_cs->dw2.bs.rss_type);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "rsvd0:0x%x\n", wqe_cs->dw2.bs.rsvd0);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "vlan_offload_en:0x%x\n", wqe_cs->dw2.bs.vlan_offload_en);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "umbcast:0x%x\n", wqe_cs->dw2.bs.umbcast);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "rsvd1:0x%x\n", wqe_cs->dw2.bs.rsvd1);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "pkt_types:0x%x\n", wqe_cs->dw2.bs.pkt_types);

	hinic3_pmd_mml_log(self->show_str, &self->show_len, "rss_hash_value:0x%08x\n", wqe_cs->dw3.bs.rss_hash_value);

	hinic3_pmd_mml_log(self->show_str, &self->show_len, "cs_dw4:0x%08x\n", wqe_cs->dw4.value);

	hinic3_pmd_mml_log(self->show_str, &self->show_len, "cs_dw5:0x%08x\n", wqe_cs->dw5.value);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "mac_type:0x%x\n", wqe_cs->dw5.ovs_bs.mac_type);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "l3_type:0x%x\n", wqe_cs->dw5.ovs_bs.l3_type);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "l4_type:0x%x\n", wqe_cs->dw5.ovs_bs.l4_type);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "rsvd0:0x%x\n", wqe_cs->dw5.ovs_bs.rsvd0);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "traffic_type:0x%x\n", wqe_cs->dw5.ovs_bs.traffic_type);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "traffic_from:0x%x\n", wqe_cs->dw5.ovs_bs.traffic_from);

	hinic3_pmd_mml_log(self->show_str, &self->show_len, "cs_dw6:0x%08x\n", wqe_cs->dw6.value);

	hinic3_pmd_mml_log(self->show_str, &self->show_len, "localtag:0x%08x\n", wqe_cs->dw7.ovs_bs.localtag);
}

static int
cmd_queue_help(major_cmd_t *self, __rte_unused char *argc)
{
	int ret;
	ret = hinic3_pmd_mml_log_ret(self->show_str, &self->show_len, "\n");
	HINIC3_PMD_MML_RET(ret);
	ret = hinic3_pmd_mml_log_ret(self->show_str, &self->show_len, " Usage: %s %s\n",
				     self->name, "-i <device> -d <tx or rx> -t <type> -q <queue id> [-w <wqe id>]");
	HINIC3_PMD_MML_RET(ret);
	ret = hinic3_pmd_mml_log_ret(self->show_str, &self->show_len, "\n %s\n", self->description);
	HINIC3_PMD_MML_RET(ret);
	ret = hinic3_pmd_mml_log_ret(self->show_str, &self->show_len, "\n Options:\n\n");
	HINIC3_PMD_MML_RET(ret);
	ret = hinic3_pmd_mml_log_ret(self->show_str, &self->show_len, "	%s, %-25s %s\n",
				     "-h", "--help", "display this help and exit");
	HINIC3_PMD_MML_RET(ret);
	ret = hinic3_pmd_mml_log_ret(self->show_str, &self->show_len, "	%s, %-25s %s\n",
				     "-i", "--device=<device>", "device target, e.g. 08:00.0");
	HINIC3_PMD_MML_RET(ret);
	ret = hinic3_pmd_mml_log_ret(self->show_str, &self->show_len, "	%s, %-25s %s\n", "-d", "--direction", "tx or rx");
	HINIC3_PMD_MML_RET(ret);
	ret = hinic3_pmd_mml_log_ret(self->show_str, &self->show_len, "	%s, %-25s %s\n", "  ", "", "0: tx");
	HINIC3_PMD_MML_RET(ret);
	ret = hinic3_pmd_mml_log_ret(self->show_str, &self->show_len, "	%s, %-25s %s\n", "  ", "", "1: rx");
	HINIC3_PMD_MML_RET(ret);

	ret = hinic3_pmd_mml_log_ret(self->show_str, &self->show_len, "	%s, %-25s %s\n", "-t", "--type", "");
	HINIC3_PMD_MML_RET(ret);
	ret = hinic3_pmd_mml_log_ret(self->show_str, &self->show_len, "	%s, %-25s %s\n", "  ", "", "0: queue info");
	HINIC3_PMD_MML_RET(ret);
	ret = hinic3_pmd_mml_log_ret(self->show_str, &self->show_len, "	%s, %-25s %s\n", "  ", "", "1: wqe info");
	HINIC3_PMD_MML_RET(ret);
	ret = hinic3_pmd_mml_log_ret(self->show_str, &self->show_len, "	%s, %-25s %s\n", "  ", "",
				     "2: cqe info(only for rx)");
	HINIC3_PMD_MML_RET(ret);

	ret = hinic3_pmd_mml_log_ret(self->show_str, &self->show_len, "	%s, %-25s %s\n", "-q", "--queue_id", "");
	HINIC3_PMD_MML_RET(ret);
	ret = hinic3_pmd_mml_log_ret(self->show_str, &self->show_len, "	%s, %-25s %s\n", "-w", "--wqe_id", "");
	HINIC3_PMD_MML_RET(ret);
	ret = hinic3_pmd_mml_log_ret(self->show_str, &self->show_len, "\n");
	HINIC3_PMD_MML_RET(ret);

	return -UDA_EINVAL;
}

static void
tx_show_sq_info(major_cmd_t *self, struct nic_sq_info *sq_info)
{
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "Send queue information:\n");
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "queue_id:%u\n", sq_info->q_id);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "pi:%u\n", sq_info->pi);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "ci:%u\n", sq_info->ci);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "fi:%u\n", sq_info->fi);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "sq_depth:%u\n", sq_info->sq_depth);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "sq_wqebb_size:%u\n", sq_info->sq_wqebb_size);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "cla_addr:0x%llx\n", sq_info->cla_addr);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "doorbell phy_addr:0x%lx\n",
			   (uintptr_t)sq_info->doorbell.phy_addr);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "page_idx:%u\n", sq_info->page_idx);
}

static void
tx_show_wqe(major_cmd_t *self, struct nic_tx_wqe_desc *wqe)
{
	struct nic_tx_ctrl_section *control = NULL;
	struct nic_tx_task_section *task = NULL;
	unsigned int *val = (unsigned int *)wqe;

	hinic3_pmd_mml_log(self->show_str, &self->show_len, "dw0:0x%08x\n", *(val++));
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "dw1:0x%08x\n", *(val++));
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "dw2:0x%08x\n", *(val++));
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "dw3:0x%08x\n", *(val++));
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "dw4:0x%08x\n", *(val++));
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "dw5:0x%08x\n", *(val++));
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "dw6:0x%08x\n", *(val++));
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "dw7:0x%08x\n", *(val++));

	hinic3_pmd_mml_log(self->show_str, &self->show_len, "\nWqe may analyse as follows:\n");
	control = &(wqe->control);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "\nInformation about wqe control section:\n");
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "ctrl_format:0x%08x\n", control->ctrl_format);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "owner:%u\n", control->ctrl_sec.o);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "extended_compact:%u\n", control->ctrl_sec.ec);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "direct_normal:%u\n", control->ctrl_sec.dn);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "inline_sgl:%u\n", control->ctrl_sec.df);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "ts_size:%u\n", control->ctrl_sec.tss);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "bds_len:%u\n", control->ctrl_sec.bdsl);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "rsvd:%u\n", control->ctrl_sec.r);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "1st_buf_len:%u\n", control->ctrl_sec.len);

	hinic3_pmd_mml_log(self->show_str, &self->show_len, "queue_info:0x%08x\n", control->queue_info);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "pri:%u\n", control->qsf.pri);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "uc:%u\n", control->qsf.uc);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "sctp:%u\n", control->qsf.sctp);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "mss:%u\n", control->qsf.mss);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "tcp_udp_cs:%u\n", control->qsf.tcp_udp_cs);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "tso:%u\n", control->qsf.tso);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "ufo:%u\n", control->qsf.ufo);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "payload_offset:%u\n", control->qsf.payload_offset);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "pkt_type:%u\n", control->qsf.pkt_type);

	/* first buffer section */
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "bd0_hi_addr:0x%08x\n", wqe->bd0_hi_addr);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "bd0_lo_addr:0x%08x\n", wqe->bd0_lo_addr);

	/* show the task section */
	task = &(wqe->task);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "\nInformation about wqe task section:\n");
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "vport_id:%u\n", task->bs2.vport_id);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "vport_type:%u\n", task->bs2.vport_type);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "traffic_type:%u\n", task->bs2.traffic_type);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "slave_port_id:%u\n", task->bs2.slave_port_id);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "rsvd0:%u\n", task->bs2.rsvd0);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "crypto_en:%u\n", task->bs2.crypto_en);
	hinic3_pmd_mml_log(self->show_str, &self->show_len, "pkt_type:%u\n", task->bs2.pkt_type);
}

static int
cmd_queue_target(major_cmd_t *self, char *argc)
{
	struct cmd_show_q_st *show_q = self->cmd_st;
	int ret;

	if (tool_get_valid_target(argc, &(show_q->target)) != UDA_SUCCESS) {
		self->err_no = -UDA_EINVAL;
		ret = snprintf(self->err_str, COMMANDER_ERR_MAX_STRING - 1, "Unknown device %s.", argc);
		if (ret <= 0) {
			PMD_DRV_LOG(ERR, "snprintf queue err msg failed, ret: %d", ret);
		}
		return -UDA_EINVAL;
	}

	return UDA_SUCCESS;
}

static int
get_queue_type(major_cmd_t *self, char *argc)
{
	struct cmd_show_q_st *show_q = self->cmd_st;
	unsigned int num = 0;

	if (string_toui(argc, BASE_10, &num) != UDA_SUCCESS) {
		self->err_no = -UDA_EINVAL;
		(void)snprintf(self->err_str, COMMANDER_ERR_MAX_STRING - 1, "Unknown queuetype %u.", num);
		return -UDA_EINVAL;
	}

	show_q->qobj = (int)num;
	return UDA_SUCCESS;
}

static int
get_queue_id(major_cmd_t *self, char *argc)
{
	struct cmd_show_q_st *show_q = self->cmd_st;
	unsigned int num = 0;

	if (string_toui(argc, BASE_10, &num) != UDA_SUCCESS) {
		self->err_no = -UDA_EINVAL;
		(void)snprintf(self->err_str, COMMANDER_ERR_MAX_STRING - 1, "Invalid queue id.");
		return -UDA_EINVAL;
	}

	show_q->q_id = (int)num;
	return UDA_SUCCESS;
}

static int
get_q_wqe_id(major_cmd_t *self, char *argc)
{
	struct cmd_show_q_st *show_q = self->cmd_st;
	unsigned int num = 0;

	if (string_toui(argc, BASE_10, &num) != UDA_SUCCESS) {
		self->err_no = -UDA_EINVAL;
		(void)snprintf(self->err_str, COMMANDER_ERR_MAX_STRING - 1, "Invalid wqe id.");
		return -UDA_EINVAL;
	}

	show_q->wqe_id = (int)num;
	return UDA_SUCCESS;
}

static int
get_direction(major_cmd_t *self, char *argc)
{
	struct cmd_show_q_st *show_q = self->cmd_st;
	unsigned int num = 0;

	if (string_toui(argc, BASE_10, &num) != UDA_SUCCESS || num > 1) {
		self->err_no = -UDA_EINVAL;
		(void)snprintf(self->err_str, COMMANDER_ERR_MAX_STRING - 1, "Unknown mode.");
		return -UDA_EINVAL;
	}

	show_q->direction = (int)num;
	return UDA_SUCCESS;
}

static int
rx_param_check(major_cmd_t *self, struct cmd_show_q_st *rx_param)
{
	struct cmd_show_q_st *show_q = self->cmd_st;

	if (rx_param->target.bus_num == TRGET_UNKNOWN_BUS_NUM) {
		self->err_no = -UDA_EINVAL;
		(void)snprintf(self->err_str, COMMANDER_ERR_MAX_STRING - 1, "Need device name.");
		return self->err_no;
	}

	if (show_q->qobj > OBJ_CQE_INFO || show_q->qobj < OBJ_Q_INFO) {
		self->err_no = -UDA_EINVAL;
		(void)snprintf(self->err_str, COMMANDER_ERR_MAX_STRING - 1, "Unknown queue type.");
		return self->err_no;
	}

	if (show_q->q_id == -1) {
		self->err_no = -UDA_EINVAL;
		(void)snprintf(self->err_str, COMMANDER_ERR_MAX_STRING - 1, "Need queue id.");
		return self->err_no;
	}

	if (show_q->qobj != OBJ_Q_INFO && show_q->wqe_id == -1) {
		self->err_no = -UDA_FAIL;
		(void)snprintf(self->err_str, COMMANDER_ERR_MAX_STRING - 1, "Get cqe_info or wqe_info, must set wqeid.\n");
		return -UDA_FAIL;
	}

	if (show_q->qobj == OBJ_Q_INFO && show_q->wqe_id != -1) {
		self->err_no = -UDA_FAIL;
		(void)snprintf(self->err_str, COMMANDER_ERR_MAX_STRING - 1, "Get queue info, need not set wqeid.\n");
		return -UDA_FAIL;
	}

	return UDA_SUCCESS;
}

static int
tx_param_check(major_cmd_t *self, struct cmd_show_q_st *tx_param)
{
	struct cmd_show_q_st *show_q = self->cmd_st;

	if (tx_param->target.bus_num == TRGET_UNKNOWN_BUS_NUM) {
		self->err_no = -UDA_EINVAL;
		(void)snprintf(self->err_str, COMMANDER_ERR_MAX_STRING - 1, "Need device name.");
		return self->err_no;
	}

	if (show_q->qobj > OBJ_WQE_INFO || show_q->qobj < OBJ_Q_INFO) {
		self->err_no = -UDA_EINVAL;
		(void)snprintf(self->err_str, COMMANDER_ERR_MAX_STRING - 1, "Unknown queue type.");
		return self->err_no;
	}

	if (show_q->q_id == -1) {
		self->err_no = -UDA_EINVAL;
		(void)snprintf(self->err_str, COMMANDER_ERR_MAX_STRING - 1, "Need queue id.");
		return self->err_no;
	}

	if (show_q->qobj == OBJ_WQE_INFO && show_q->wqe_id == -1) {
		self->err_no = -UDA_EINVAL;
		(void)snprintf(self->err_str, COMMANDER_ERR_MAX_STRING - 1, "Get wqe_info, must set wqeid.");
		return self->err_no;
	}

	if (show_q->qobj != OBJ_WQE_INFO && show_q->wqe_id != -1) {
		self->err_no = -UDA_EINVAL;
		(void)snprintf(self->err_str, COMMANDER_ERR_MAX_STRING - 1, "Get queue info, need not set wqeid.");
		return self->err_no;
	}

	return UDA_SUCCESS;
}

static void
cmd_tx_execute(major_cmd_t *self)
{
	struct cmd_show_q_st *show_q = self->cmd_st;
	int ret;
	struct nic_sq_info sq_info = {0};
	struct nic_tx_wqe_desc nwqe = {0};

	if (tx_param_check(self, show_q) != UDA_SUCCESS) {
		return;
	}

	if (show_q->qobj == OBJ_Q_INFO || show_q->qobj == OBJ_WQE_INFO) {
		ret = lib_tx_sq_info_get(show_q->target, (void *)&sq_info, show_q->q_id);
		if (ret != UDA_SUCCESS) {
			self->err_no = ret;
			(void)snprintf(self->err_str, COMMANDER_ERR_MAX_STRING - 1, "Get tx sq_info failed.");
			return;
		}

		if (show_q->qobj == OBJ_Q_INFO) {
			tx_show_sq_info(self, &sq_info);
			return;
		}

		if (show_q->wqe_id >= (int)sq_info.sq_depth) {
			self->err_no = -UDA_EINVAL;
			(void)snprintf(self->err_str, COMMANDER_ERR_MAX_STRING - 1, "Max wqe id is %u.", sq_info.sq_depth - 1);
			return;
		}

		ret = lib_tx_wqe_info_get(show_q->target, &sq_info, show_q->q_id, show_q->wqe_id, (void *)&nwqe, sizeof(nwqe));
		if (ret != UDA_SUCCESS) {
			self->err_no = ret;
			(void)snprintf(self->err_str, COMMANDER_ERR_MAX_STRING - 1, "Get tx wqe_info failed.");
			return;
		}

		tx_show_wqe(self, &nwqe);
		return;
	}

	return;
}

static void
cmd_rx_execute(major_cmd_t *self)
{
	int ret;
	struct nic_rq_info rq_info = {0};
	struct tag_l2nic_rx_cqe cqe = {0};
	nic_rq_wqe wqe = {0};
	struct cmd_show_q_st *show_q = self->cmd_st;

	if (rx_param_check(self, show_q) != UDA_SUCCESS) {
		return;
	}

	ret = lib_rx_rq_info_get(show_q->target, &rq_info, show_q->q_id);
	if (ret != UDA_SUCCESS) {
		self->err_no = ret;
		(void)snprintf(self->err_str, COMMANDER_ERR_MAX_STRING - 1, "Get rx rq_info failed.");
		return;
	}

	if (show_q->qobj == OBJ_Q_INFO) {
		rx_show_rq_info(self, &rq_info);
		return;
	}

	if ((uint32_t)show_q->wqe_id >= rq_info.rq_depth) {
		self->err_no = -UDA_EINVAL;
		(void)snprintf(self->err_str, COMMANDER_ERR_MAX_STRING - 1, "Max wqe id is %u.", rq_info.rq_depth - 1);
		return;
	}

	if (show_q->qobj == OBJ_WQE_INFO) {
		ret = lib_rx_wqe_info_get(show_q->target, &rq_info, show_q->q_id, show_q->wqe_id, (void *)&wqe, sizeof(wqe));
		if (ret != UDA_SUCCESS) {
			self->err_no = ret;
			(void)snprintf(self->err_str, COMMANDER_ERR_MAX_STRING - 1, "Get rx wqe_info failed.");
			return;
		}

		rx_show_wqe(self, &wqe);
		return;
	}

	/* OBJ_CQE_INFO */
	ret = lib_rx_cqe_info_get(show_q->target, &rq_info, show_q->q_id, show_q->wqe_id, (void *)&cqe, sizeof(cqe));
	if (ret != UDA_SUCCESS) {
		self->err_no = ret;
		(void)snprintf(self->err_str, COMMANDER_ERR_MAX_STRING - 1, "Get rx cqe_info failed.");
		return;
	}

	rx_show_cqe_info(self, &cqe);

	return;
}

static void
cmd_nic_queue_execute(major_cmd_t *self)
{
	struct cmd_show_q_st *show_q = self->cmd_st;

	if (show_q->direction == -1) {
		hinic3_pmd_mml_log(self->show_str, &self->show_len, "Need -d parameter.\n");
		return;
	}

	if (show_q->direction == 0) {
		cmd_tx_execute(self);
	} else {
		cmd_rx_execute(self);
	}

	return;
}

int
cmd_show_q_init(cmd_adapter_t *adapter)
{
	struct cmd_show_q_st *show_q = NULL;
	major_cmd_t *show_q_cmd;

	show_q_cmd = calloc(1, sizeof(*show_q_cmd));
	if (!show_q_cmd) {
		PMD_DRV_LOG(ERR, "Failed to allocate queue cmd\n");
		return -UDA_ENONMEM;
	}

	(void)snprintf(show_q_cmd->name, MAX_NAME_LEN - 1, "%s", "nic_queue");
	(void)snprintf(show_q_cmd->description, MAX_DES_LEN - 1, "%s",
		       "Query the rx/tx queue information of a specified pci_addr");

	show_q_cmd->option_count = 0;
	show_q_cmd->execute = cmd_nic_queue_execute;

	show_q = calloc(1, sizeof(*show_q));
	if (!show_q) {
		free(show_q_cmd);
		PMD_DRV_LOG(ERR, "Failed to allocate show queue\n");
		return -UDA_ENONMEM;
	}

	show_q->qobj = -1;
	show_q->q_id = -1;
	show_q->wqe_id = -1;
	show_q->direction = -1;

	show_q_cmd->cmd_st = show_q;

	tool_target_init(&(show_q->target.bus_num), show_q->target.dev_name, MAX_DEV_LEN);

	major_command_option(show_q_cmd, "-h", "--help", PARAM_NOT_NEED, cmd_queue_help);
	major_command_option(show_q_cmd, "-i", "--device", PARAM_NEED, cmd_queue_target);

	major_command_option(show_q_cmd, "-t", "--type", PARAM_NEED, get_queue_type);
	major_command_option(show_q_cmd, "-q", "--queue_id", PARAM_NEED, get_queue_id);
	major_command_option(show_q_cmd, "-w", "--wqe_id", PARAM_NEED, get_q_wqe_id);
	major_command_option(show_q_cmd, "-d", "--direction", PARAM_NEED, get_direction);

	major_command_register(adapter, show_q_cmd);

	return UDA_SUCCESS;
}
