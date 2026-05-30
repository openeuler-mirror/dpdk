/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2025-2026 Huawei Technologies Co., Ltd
 */

#include <rte_os.h>
#include <rte_telemetry.h>
#include "base/hinic3_compat.h"
#include "base/hinic3_pmd_mgmt.h"
#include "base/hinic3_pmd_hwdev.h"
#include "base/hinic3_pmd_mbox.h"
#include "hinic3_pmd_ethdev.h"
#include "hinic3_pmd_telemetry.h"

static int
hinic3_get_mbox_info(struct rte_eth_dev *eth_dev, struct mbox_cnt_info *mbox_cnt)
{
	u32 i;

	struct rte_pci_device *pci_dev = RTE_ETH_DEV_TO_PCI(eth_dev);
	if (pci_dev == NULL) {
		PMD_DRV_LOG(ERR, "Get pci device failed.");
		return -1;
	}

	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(eth_dev);
	if (nic_dev == NULL) {
		PMD_DRV_LOG(ERR, "Get nic device failed.");
		return -1;
	}

	i = mbox_cnt->func_num;
	mbox_cnt->func_num++;
	snprintf(mbox_cnt->func_info[i].bus_info, sizeof(mbox_cnt->func_info[i].bus_info), "%.4x_%.2x_%.2x_%x",
		 pci_dev->addr.domain, pci_dev->addr.bus,
		 pci_dev->addr.devid, pci_dev->addr.function);

	mbox_cnt->func_info[i].send_cnt = nic_dev->hwdev->func_to_func->mbox_send_cnt;
	mbox_cnt->func_info[i].ack_cnt = nic_dev->hwdev->func_to_func->mbox_ack_cnt;

	return 0;
}

static int
hinic3_telemetry_info(const char *cmd, const char *params, struct rte_tel_data *d)
{
	struct rte_eth_dev *eth_dev = NULL;
	struct rte_tel_data *i_data = NULL;
	struct rte_tel_data *func_data = NULL;
	struct hinic3_nic_dev *nic_dev = NULL;
	struct mbox_cnt_info mbox_cnt;
	uint32_t i;
	uint8_t n_ports, err;

	memset(&mbox_cnt, 0, sizeof(struct mbox_cnt_info));

	for (i = 0; i < RTE_MAX_ETHPORTS; i++) {
		/* Skip if port is unused */
		if (!rte_eth_dev_is_valid_port(i))
			continue;

		eth_dev = &rte_eth_devices[i];
		if (eth_dev) {
			err = hinic3_get_mbox_info(eth_dev, &mbox_cnt);
			if (err) {
				PMD_DRV_LOG(ERR, "Get mbox info failed.");
				return -1;
			}
		}
	}

	rte_tel_data_start_dict(d);
	rte_tel_data_add_dict_int(d, "func_num", mbox_cnt.func_num);

	i_data = rte_tel_data_alloc();
	if (i_data == NULL)
		return -ENOMEM;

	rte_tel_data_start_dict(i_data);

	for (i = 0; i < mbox_cnt.func_num; i++) {
		func_data = rte_tel_data_alloc();
		if (func_data == NULL)
			return -ENOMEM;

		rte_tel_data_start_dict(func_data);
		rte_tel_data_add_dict_int(func_data, "send_cnt", mbox_cnt.func_info[i].send_cnt);
		rte_tel_data_add_dict_int(func_data, "ack_cnt", mbox_cnt.func_info[i].ack_cnt);
		rte_tel_data_add_dict_container(i_data, mbox_cnt.func_info[i].bus_info, func_data, 0);
	}

	rte_tel_data_add_dict_container(d, "func_info", i_data, 0);

	return 0;
}

RTE_INIT(hinic3_init_telemetry)
{
	rte_telemetry_register_cmd("/hinic3/mbox_cnt", hinic3_telemetry_info,
				   "Returns mbox send and ack count information");
}
