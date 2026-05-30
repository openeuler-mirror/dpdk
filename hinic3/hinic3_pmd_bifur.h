/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2025-2026 Huawei Technologies Co., Ltd
 */
#ifndef HINIC3_PMD_BIFUR_H_
#define HINIC3_PMD_BIFUR_H_

#include <rte_bus_pci.h>

/* bifur kenerl define. */
#define BIFUR_GDEV_PATH "/dev/bifur_gdev"
#define BIFUR_PROC_PATH "/proc/net/bifur"
#define BIFUR_DEV_NAME	"bifur_vdev"

enum BIFUR_ACTION {
	BIFUR_DONE,
	BIFUR_CONTINUE,
};

int hinic3_bifur_pre_probe(struct rte_pci_driver *pci_drv, struct rte_pci_device *origin_pci_dev,
			   struct rte_pci_device **work_pci_dev, enum BIFUR_ACTION *bifur_flag);
void hinic3_bifur_post_remove(struct rte_pci_device *origin_pci_dev);
bool hinic3_bifur_is_shared_dev(struct rte_pci_device *pci_dev);
int hinic3_bifur_get_default_mac(struct rte_pci_device *pci_dev, u8 *mac_addr, int ether_len);
int hinic3_parse_sysfs_value(const char *filename, unsigned long *val);

#endif /* _HINIC3_PMD_BIFUR_H_ */