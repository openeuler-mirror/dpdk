/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2025 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_VF_MGMT_H
#define HINIC3_VF_MGMT_H

#include <limits.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include "rte_pci.h"
#include "hinic3_list.h"
#include "hinic3_iface_port.h"

#define HWPT_FLAVOR_TYPE_NONE                   0
#define HWPT_FLAVOR_TYPE_MIN                    1
#define HWPT_FLAVOR_TYPE_MAX                    32
#define HWPT_PCI_ADDR_LEN_MAX                   32
#define VF_FLAVOR_QUEUE_NUM_INVALID           0xff

#define HWPT_DIR_PARRENT_LEV                    2
#define HWPT_NO_PARRENT_LEV                     1

#define HWPT_HEX_BASE                           16
#define HWPT_DEC_BASE                           10

#define SYSFS_PCI_DEVICES                       "/sys/bus/pci/devices"
#define QUEUE_PCI_FMT_NVAL                      (PCI_FMT_NVAL + 1)

#define HWPT_HWTYPE_PF_STR                      "PF"
#define HWPT_HWTYPE_VF_STR                      "VF"

#define HWPT_HWTYPE_VF_BAR_NUM                  3
#define HWPT_REPRESENTOR_DEV_MAX_CNT            256
#define PORT_CONF_NULL                          0
#define PORT_CONF_PCI                           1
#define PORT_CONF_FUNC                          2

#define HWPT_FLAVOR_REFCNT_UNUSED               0
#define HWPT_FLAVOR_REFCNT_USED                 1
#define HWPT_FLAVOR_REFCNT_INVALID              2
#define HWPT_FLAVOR_REFCNT_NULL                 3

struct hwpt_phy_dev {
    /* phy dev type, pf or vf */
    uint8_t type;
    /* flag of pci is assign */
    uint8_t refcnt;
    /* corresponding phy dev's pci address */
    struct rte_pci_addr pci_addr;
    /* corresponding phy dev's function id */
    int function_id;
};

struct hinic3_dev_node {
    struct hinic3_list node;
    struct hwpt_phy_dev phy_dev;
};

struct dev_mgmt {
    struct hinic3_list vf_list;
    struct hinic3_list pf_list;
};

struct dir_info_input {
    char name_buf[PATH_MAX];
    char parent_name_buf[PATH_MAX];
    char pci_addr_buf[HWPT_PCI_ADDR_LEN_MAX];
};

struct dir_info_output {
    char *parent;
    char *parent_parent;
};

union splitaddr {
    struct {
        char *queue_num;
        char *domain;
        char *bus;
        char *devid;
        char *function;
    };
    char *str[QUEUE_PCI_FMT_NVAL]; /* last element-separator is "." not ":" */
};

enum hinic3_bdf_status {
    HINIC3_VF_BDF_NOT_USED,
    HINIC3_VF_BDF_USED,
    HINIC3_VF_BDF_INVALID,
    HINIC3_VF_BDF_LIST_NULL,
};

int hinic3_hwpt_flavor_mgmt_init(void);
enum hinic3_bdf_status hinic3_vf_flavor_get_phy_dev_refcnt(const struct rte_pci_addr *pci_addr, uint16_t function_id);
struct hwpt_phy_dev *hinic3_flavor_get_phy_dev(const struct rte_pci_addr *pci_addr);
int hinic3_hwpt_flavor_set_phy_dev_refcnt(const struct rte_pci_addr *pci_addr, uint16_t function_id, uint8_t refcnt);
struct dev_mgmt *get_netdev_hwpt_flavor(void);
struct dev_mgmt *hinic3_updata_netdev_hwpt_flavor(void);
bool hinic3_check_flavor_phy_dev(const struct rte_pci_addr *pci_addr);
void mgmt_vf_virtio_queue(uint32_t max_queue_num, bool is_del);
int hinic3_get_pci_by_function_id(uint16_t func_id, struct rte_pci_addr *pci_addr);
int hinic3_get_function_id_by_pci(struct rte_pci_addr *pci_addr);
void hinic3_hwpt_flavor_phy_dev_construct_one(const struct rte_pci_addr *pci_addr, int function_id,
    enum hiovs_hwpt_phy_dev_type dev_type);
int hinic3_get_port_conf_type(void);
int hinic3_set_port_conf_type(int conf_type);
void hinic3_dev_init(void);
void hinic3_dev_uninit(void);
bool hinic3_get_port_list_init(void);
void hinic3_set_port_list_init(bool value);
int hinic3_dpu_vf_flavor_add(void);
void hinic3_vf_dev_mutex_lock(void);
void hinic3_vf_dev_mutex_unlock(void);
#endif
