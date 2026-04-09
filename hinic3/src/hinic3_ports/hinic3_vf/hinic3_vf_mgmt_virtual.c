/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2025 Huawei Technologies Co., Ltd
 */

#include "hinic3_vf_mgmt_virtual.h"
#include <dirent.h>
#include <unistd.h>
#include "hinic3_vf_mgmt.h"
#include "hinic3_vf_controller.h"
#include "rte_pci.h"
#include "rte_bus_pci.h"
#include "rte_dev.h"
#include "rte_string_fns.h"

int
hinic3_smart_vf_flavor_add(void)
{
    int ret = 0;
    struct hovs_phy_dev_info dev = { 0 };
    union bdf_info_u bdf_info = {0};

    bdf_info.bs.bdf_type = BDF_TYPE_REAL;
    bdf_info.bs.func_id_flag = 0;

    ret = hinic3_global_pcie_list_query(0, bdf_info.value, &dev);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "hinic3 get global pcie list err");
        return -1;
    }

    for (uint32_t i = 0; i < dev.phy_dev_num; i++) {
        struct hovs_pci_addr_info *info = &dev.pci_info[i];
        /* 检查新增加的PCI有效性 */
        if (hinic3_check_flavor_phy_dev((struct rte_pci_addr *)&info->pci_addr) == false)
            continue;

        /* 添加到全局链表 */
        if ((info->type & HINIC3_DEVICE_TYPE) == HWPT_PHY_DEV_TYPE_VF) {
            hinic3_hwpt_flavor_phy_dev_construct_one((struct rte_pci_addr *)&info->pci_addr, 0,
                HWPT_PHY_DEV_TYPE_VF);
        }
    }

    return 0;
}

int
hinic3_virtual_flavor_mgmt_init(void)
{
    int ret = 0;
    bool nic_type = hinic3_device_mode_get();
    if (nic_type == DPU_MODE) {
        hinic3_set_port_list_init(false);
    } else {
        ret = hinic3_smart_vf_flavor_add();
        if (ret != 0) {
            HINIC3_LOG(ERR, VPORT, "hinic3_smart_vf_flavor_add err, ret is %d", ret);
            return -1;
        }
    }

    return 0;
}
