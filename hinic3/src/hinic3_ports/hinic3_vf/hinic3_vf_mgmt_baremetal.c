/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2025 Huawei Technologies Co., Ltd
 */

#include "hinic3_vf_mgmt_baremetal.h"
#include <unistd.h>
#include "hinic3_util.h"
#include "hinic3_vf_controller.h"
#include "hinic3_vf_mgmt.h"
#include "rte_pci.h"
#include "rte_bus_pci.h"
#include "rte_dev.h"
#include "rte_string_fns.h"

inline static bool
hinic3_check_dev_vf(uint8_t type)
{
    return hinic3_is_support_vf_port() &&
    ((type & HINIC3_DEVICE_DRIVER_TYPE) == HINIC3_DEVICE_DRIVER_TYPE) &&
    ((type & HINIC3_DEVICE_TYPE) == HWPT_PHY_DEV_TYPE_VF);
}

inline static bool
hinic3_check_dev_pf(uint8_t type)
{
    return hinic3_is_support_pf_port() &&
    ((type & HINIC3_DEVICE_DRIVER_TYPE) == HINIC3_DEVICE_DRIVER_TYPE) &&
    ((type & HINIC3_DEVICE_TYPE) == HWPT_PHY_DEV_TYPE_PF);
}

static int
hinic3_port_set_mac(struct hinic3_vf_dev *vf_dev, struct hovs_pci_addr_info *info)
{
    struct rte_cfgfile *inifile = NULL;
    int ret = 0;
    char port_name[HINIC3_MAX_PORT_NAME_LEN] = {0};
    const char *mac_addr = NULL;

    inifile = hinic3_cfgfile_load(AGENT_CFG_FILE);
    if (inifile == NULL) {
        HINIC3_LOG(ERR, VPORT, "Faild to load agent config file: %s!", AGENT_CFG_FILE);
        return -1;
    }

    ret = snprintf(port_name, HINIC3_MAX_PORT_NAME_LEN, "representor%d_mac", info->glb_func_inx);
    if (ret < 0 || ret >= HINIC3_MAX_PORT_NAME_LEN) {
        HINIC3_LOG(ERR, VPORT, "sprintf_s fail, ret is %d!", ret);
        rte_cfgfile_close(inifile);
        return -1;
    }

    mac_addr = rte_cfgfile_get_entry(inifile, AGENT_SECTION, port_name);
    if (mac_addr == NULL) {
        HINIC3_LOG(INFO, VPORT, "%s mac addr not set.", port_name);
        rte_cfgfile_close(inifile);
        return 0;
    }

    ret = rte_ether_unformat_addr(mac_addr, (struct rte_ether_addr *)&vf_dev->mac_addr);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "port mac unformat fail, ret is %d!", ret);
        rte_cfgfile_close(inifile);
        return -1;
    }

    if (!eth_addr_is_zero(vf_dev->mac_addr)) {
        ret = hinic3_etheraddr_set(vf_dev->vport_id, vf_dev->mac_addr);
        if (ret != 0) {
            HINIC3_LOG(ERR, VPORT, "failed to set mac for vf! ret is %d!", ret);
            rte_cfgfile_close(inifile);
            return -1;
        }
    }

    rte_cfgfile_close(inifile);
    return 0;
}

static int
hinic3_init_one_port(struct hovs_pci_addr_info *info)
{
    int ret;
    struct hinic3_vf_dev vf_dev = {0};

    if ((info->type & HINIC3_DEVICE_DRIVER_TYPE) != HINIC3_DEVICE_DRIVER_TYPE) {
        HINIC3_LOG(ERR, VPORT, "port type error!");
        return -1;
    }
    vf_dev.max_queue_num = hinic3_max_queue_num_get();
    vf_dev.n_upcall_queue = hinic3_upcall_queue_num_get();
    vf_dev.vport_id = HINIC3_PORT_ID_INVALID;
    memcpy(&vf_dev.pci_addr, (struct rte_pci_addr *)&info->pci_addr, sizeof(struct rte_pci_addr));
    ret = hinic3_vf_dynamic_port_add(&vf_dev);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "hinic3 add dynamic port err, ret is %d!", ret);
        return -1;
    }

    ret = hinic3_port_mgmt_set_usage_state(vf_dev.vport_id, HINIC3_SET_VF_STATE_DOWN);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "init one port, port mgmt set vf state down err, ret is %d!", ret);
        return -1;
    }

    ret = hinic3_port_set_mac(&vf_dev, info);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "vf_dev set mac err, ret is %d!", ret);
        return -1;
    }

    return 0;
}

int
hinic3_dpu_bare_metal_flavor_mgmt_init(void)
{
    int ret = 0;
    uint32_t dev_num;
    struct hovs_pci_addr_info *info = NULL;
    struct hovs_phy_dev_info dev = {0};
    union bdf_info_u bdf_info;
    const char *err_str = NULL;
    enum hiovs_hwpt_phy_dev_type dev_type;
    bool should_init_port = false;

    bdf_info.bs.bdf_type = BDF_TYPE_FAKE;
    bdf_info.bs.func_id_flag = 1;

    ret = hinic3_global_pcie_list_query(1, bdf_info.value, &dev);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "dpu bare metal flavor mgmt init, get global pcie list err!");
        return ret;
    }

    dev_num = MIN(dev.phy_dev_num, HWPT_REPRESENTOR_DEV_MAX_CNT);
    for (uint32_t i = 0; i < dev_num; i++) {
        info = &dev.pci_info[i];
        should_init_port = false;
        if (hinic3_check_dev_vf(info->type)) {
            dev_type = HWPT_PHY_DEV_TYPE_VF;
            should_init_port = hinic3_is_preload_vf_port();
            err_str = "vf";
        } else if (hinic3_check_dev_pf(info->type)) {
            dev_type = HWPT_PHY_DEV_TYPE_PF;
            should_init_port = hinic3_is_preload_pf_port();
            err_str = "pf";
        }

        if (should_init_port == false)
            continue;

        hinic3_hwpt_flavor_phy_dev_construct_one((struct rte_pci_addr *)&info->pci_addr, info->glb_func_inx, dev_type);
        if (hinic3_init_one_port(info) != 0) {
            HINIC3_LOG(ERR, VPORT, "hinic3 init one %s port fail!", err_str);
            return -1;
        }
    }

    return ret;
}