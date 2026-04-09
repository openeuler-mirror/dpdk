/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2025 Huawei Technologies Co., Ltd
 */

#include "hinic3_vf_mgmt.h"
#include <dirent.h>
#include <unistd.h>
#include "rte_pci.h"
#include "rte_bus_pci.h"
#include "rte_dev.h"
#include "rte_string_fns.h"
#include "hinic3_util.h"
#include "hinic3_tlv_key.h"
#include "hinic3_iface_global.h"
#include "hinic3_iface_port.h"
#include "hinic3_util.h"
#include "hinic3_log.h"
#include "hinic3_flow_agent.h"
#include "hinic3_mutex.h"
#include "hinic3_mutex.h"
#include "hinic3_vf_controller.h"
#include "hinic3_meminfo.h"
#include "hinic3_smap.h"
#include "hinic3_thread.h"
#include "hinic3_mutex.h"
#include "hinic3_vf_mgmt_virtual.h"
#include "hinic3_vf_mgmt_baremetal.h"

static struct hinic3_mutex hwpt_phy_dev_mutex;
static int g_port_conf_type = PORT_CONF_NULL;
static bool g_port_list_init = true;
static struct dev_mgmt g_dev_list = {0};

void
hinic3_vf_dev_mutex_lock(void)
{
   hinic3_pthread_mutex_lock(&hwpt_phy_dev_mutex);
}

void
hinic3_vf_dev_mutex_unlock(void)
{
   hinic3_pthread_mutex_unlock(&hwpt_phy_dev_mutex);
}

bool
hinic3_get_port_list_init(void)
{
    return g_port_list_init;
}

void
hinic3_set_port_list_init(bool value)
{
    g_port_list_init = value;
}

int
hinic3_get_port_conf_type(void)
{
    return g_port_conf_type;
}

int
hinic3_set_port_conf_type(int conf_type)
{
    if ((conf_type != PORT_CONF_PCI) && (conf_type != PORT_CONF_FUNC)) {
        HINIC3_LOG(ERR, VPORT, "hinic3_set_port_conf_type bad type!");
        return -1;
    }

    if (g_port_conf_type == PORT_CONF_NULL) {
        g_port_conf_type = conf_type;
    } else if (g_port_conf_type != conf_type) {
        HINIC3_LOG(ERR, VPORT, "hinic3_set_port_conf_type type conflict!");
        return -1;
    }

    return 0;
}

int
hinic3_hwpt_flavor_set_phy_dev_refcnt(const struct rte_pci_addr *pci_addr, uint16_t function_id, uint8_t refcnt)
{
    struct hinic3_dev_node *iter = NULL;
    if (pci_addr == NULL)
        return -1;

    if (hinic3_list_is_empty(&g_dev_list.pf_list) == true && hinic3_list_is_empty(&g_dev_list.vf_list) == true) {
        HINIC3_LOG(ERR, VPORT, "dev_list is empty!");
        return -1;
    }

    hinic3_pthread_mutex_lock(&hwpt_phy_dev_mutex);
    LIST_FOR_EACH(iter, node, &g_dev_list.pf_list) {
        if (function_id > 0 && iter->phy_dev.function_id == function_id) {
            iter->phy_dev.refcnt = refcnt;
            hinic3_pthread_mutex_unlock(&hwpt_phy_dev_mutex);
            HINIC3_LOG(INFO, VPORT, "Assigned function id is %u", function_id);
            return 0;
        }
        if (rte_pci_addr_cmp(pci_addr, &(iter->phy_dev.pci_addr)) == 0) {
            iter->phy_dev.refcnt = refcnt;
            hinic3_pthread_mutex_unlock(&hwpt_phy_dev_mutex);
            HINIC3_LOG(INFO, VPORT, "Assigned PCI Address is %04x:%02x:%02x.%d choose %d",
                pci_addr->domain, pci_addr->bus, pci_addr->devid, pci_addr->function, refcnt);
            return 0;
        }
    }
    LIST_FOR_EACH(iter, node, &g_dev_list.vf_list) {
        if (function_id > 0 && iter->phy_dev.function_id == function_id) {
            iter->phy_dev.refcnt = refcnt;
            hinic3_pthread_mutex_unlock(&hwpt_phy_dev_mutex);
            HINIC3_LOG(INFO, VPORT, "Assigned function id is %u", function_id);
            return 0;
        }
        if (rte_pci_addr_cmp(pci_addr, &(iter->phy_dev.pci_addr)) == 0) {
            iter->phy_dev.refcnt = refcnt;
            hinic3_pthread_mutex_unlock(&hwpt_phy_dev_mutex);
            HINIC3_LOG(INFO, VPORT, "Assigned PCI Address is %04x:%02x:%02x.%d choose %d",
                pci_addr->domain, pci_addr->bus, pci_addr->devid, pci_addr->function, refcnt);
            return 0;
        }
    }

    hinic3_pthread_mutex_unlock(&hwpt_phy_dev_mutex);
    HINIC3_LOG(ERR, VPORT, "invalid hw_type, cannot reset flavor, pci %04x:%02x:%02x.%u",
        pci_addr->domain, pci_addr->bus, pci_addr->devid, pci_addr->function);
    return -1;
}

enum hinic3_bdf_status
hinic3_vf_flavor_get_phy_dev_refcnt(const struct rte_pci_addr *pci_addr, uint16_t function_id)
{
    enum hinic3_bdf_status bdf_status = HINIC3_VF_BDF_LIST_NULL;
    struct hinic3_dev_node *iter_pf = NULL;
    struct hinic3_dev_node *iter_vf = NULL;

    if (hinic3_device_mode_get() == SMART_NIC_MODE)
        hinic3_updata_netdev_hwpt_flavor();

    hinic3_pthread_mutex_lock(&hwpt_phy_dev_mutex);
    if (hinic3_list_is_empty(&g_dev_list.pf_list) == false) {
        LIST_FOR_EACH(iter_pf, node, &g_dev_list.pf_list) {
             /**
                1. host下电虚机场景：用function_id下发端口时会使用fake pci，否则会报错。但这样引入一个问题
                2. 正常前后端都上电场景：先用function_flavour查询时，会先从host捞pci list保存起来，再创建端口时会根据传入的pci号和之前保存的pci list进行比对，由于host下电场景
                    使用function id下发时使用fake pci，因此这里和之前保存的pci list进行比对时会失败报错，所以这里改成使用function id进行比对
            */
            if (function_id > 0 && iter_pf->phy_dev.function_id == function_id) {
                bdf_status = iter_pf->phy_dev.refcnt;
                hinic3_pthread_mutex_unlock(&hwpt_phy_dev_mutex);
                return bdf_status;
            }
            if (rte_pci_addr_cmp(pci_addr, &(iter_pf->phy_dev.pci_addr)) == 0) {
                bdf_status = iter_pf->phy_dev.refcnt;
                hinic3_pthread_mutex_unlock(&hwpt_phy_dev_mutex);
                return bdf_status;
            }
        }
    }

    if (hinic3_list_is_empty(&g_dev_list.vf_list) == false) {
        LIST_FOR_EACH(iter_vf, node, &g_dev_list.vf_list) {
            if (function_id > 0 && iter_vf->phy_dev.function_id == function_id) {
                bdf_status = iter_vf->phy_dev.refcnt;
                hinic3_pthread_mutex_unlock(&hwpt_phy_dev_mutex);
                return bdf_status;
            }
            if (rte_pci_addr_cmp(pci_addr, &(iter_vf->phy_dev.pci_addr)) == 0) {
                bdf_status = iter_vf->phy_dev.refcnt;
                hinic3_pthread_mutex_unlock(&hwpt_phy_dev_mutex);
                return bdf_status;
            }
        }
    }

    hinic3_pthread_mutex_unlock(&hwpt_phy_dev_mutex);
    return bdf_status;
}

struct hwpt_phy_dev *
hinic3_flavor_get_phy_dev(const struct rte_pci_addr *pci_addr)
{
    struct hinic3_dev_node *iter_pf = NULL;
    struct hinic3_dev_node *iter_vf = NULL;

    hinic3_pthread_mutex_lock(&hwpt_phy_dev_mutex);
    if (hinic3_list_is_empty(&g_dev_list.pf_list) == false) {
        LIST_FOR_EACH(iter_pf, node, &g_dev_list.pf_list) {
            if (rte_pci_addr_cmp(pci_addr, &(iter_pf->phy_dev.pci_addr)) == 0) {
                hinic3_pthread_mutex_unlock(&hwpt_phy_dev_mutex);
                return &iter_pf->phy_dev;
            }
        }
    }

    if (hinic3_list_is_empty(&g_dev_list.vf_list) == false) {
        LIST_FOR_EACH(iter_vf, node, &g_dev_list.vf_list) {
            if (rte_pci_addr_cmp(pci_addr, &(iter_vf->phy_dev.pci_addr)) == 0) {
                hinic3_pthread_mutex_unlock(&hwpt_phy_dev_mutex);
                return &iter_vf->phy_dev;
            }
        }
    }

    hinic3_pthread_mutex_unlock(&hwpt_phy_dev_mutex);
    return NULL;
}

static void
hinic3_hwpt_flavor_destroy_list(struct hinic3_list *list)
{
    struct hinic3_dev_node *iter = NULL;
    struct hinic3_dev_node *next = NULL;

    LIST_FOR_EACH_SAFE(iter, next, node, list) {
        hinic3_list_remove(&(iter->node));
        hinic3_free(iter);
        iter = NULL;
    }
}

static void
hinic3_hwpt_flavor_destroy_flavors(void)
{
    hinic3_pthread_mutex_lock(&hwpt_phy_dev_mutex);
    if (!hinic3_list_is_empty(&g_dev_list.vf_list))
        hinic3_hwpt_flavor_destroy_list(&g_dev_list.vf_list);

    if (!hinic3_list_is_empty(&g_dev_list.pf_list))
        hinic3_hwpt_flavor_destroy_list(&g_dev_list.pf_list);

    hinic3_pthread_mutex_unlock(&hwpt_phy_dev_mutex);
}

static int
get_queue_pci_addr_from_str(const char *pci_addr_str, struct rte_pci_addr *pci_addr, uint8_t *queue_num_max)
{
    int ret = -1;
    union splitaddr hwpt_splitaddr;
    char *endPtr = NULL;
    char *new_pci_addr_str = hinic3_xstrdup(pci_addr_str, HINIC3_PORTS);
    if (new_pci_addr_str == NULL)
        return -1;

    if (rte_strsplit(new_pci_addr_str, BUFSIZE_SHORT, hwpt_splitaddr.str, QUEUE_PCI_FMT_NVAL, ':') !=
        (QUEUE_PCI_FMT_NVAL - 1)) {
        HINIC3_LOG(ERR, VPORT, "queue and pci address error!");
        goto free_exit;
    }

    hwpt_splitaddr.function = strchr(hwpt_splitaddr.devid, '.');
    if (hwpt_splitaddr.function == NULL) {
        HINIC3_LOG(ERR, VPORT, "pci address error! no function found!");
        goto free_exit;
    }

    *hwpt_splitaddr.function++ = '\0';

    *queue_num_max = (uint8_t)strtol(hwpt_splitaddr.queue_num, &endPtr, HWPT_HEX_BASE);
    if (endPtr == NULL || *endPtr != '\0')
        goto free_exit;
    pci_addr->domain = (uint16_t)strtoul(hwpt_splitaddr.domain, &endPtr, HWPT_HEX_BASE);
    if (endPtr == NULL || *endPtr != '\0')
        goto free_exit;
    pci_addr->bus = (uint8_t)strtoul(hwpt_splitaddr.bus, &endPtr, HWPT_HEX_BASE);
    if (endPtr == NULL || *endPtr != '\0')
        goto free_exit;
    pci_addr->devid = (uint8_t)strtoul(hwpt_splitaddr.devid, &endPtr, HWPT_HEX_BASE);
    if (endPtr == NULL || *endPtr != '\0')
        goto free_exit;
    pci_addr->function = (uint8_t)strtoul(hwpt_splitaddr.function, &endPtr, HWPT_DEC_BASE);
    if (endPtr == NULL || *endPtr != '\0')
        goto free_exit;
    ret = 0;

free_exit:
    hinic3_free(new_pci_addr_str);
    return ret;
}

static int
hinic3_vf_check_pci(const struct rte_pci_addr *vf_pci_addr)
{
    int ret;
    char buf[BUFSIZE_SHORT] = {0};
    struct smap cfg;
    struct rte_pci_addr real_pci_addr;
    const char *ret_buf = NULL;
    uint8_t queue_num_max;

    memset(&real_pci_addr, 0, sizeof(struct rte_pci_addr));

    hinic3_smap_init(&cfg);
    ret = pci_addr_format(vf_pci_addr, buf, BUFSIZE_SHORT);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "check pci addr parse failed!");
        goto err;
    }
    hinic3_smap_add(&cfg, HINIC3_GLOBAL_CFG_ARG_VF_INFO_STR, buf, HINIC3_PORTS);

    ret = hinic3_global_cfg_get(&cfg);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "Global cfg get error, errno is %d!", ret);
        goto err;
    }

    ret_buf = hinic3_smap_get(&cfg, HINIC3_GLOBAL_CFG_ARG_VF_INFO_STR);
    if (ret_buf == NULL) {
        HINIC3_LOG(ERR, VPORT, "vf_info is empty for %s!", buf);
        ret = -1;
        goto err;
    }

    ret = get_queue_pci_addr_from_str(ret_buf, &real_pci_addr, &queue_num_max);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "getting pci_addr from hiovs for vm failed, errno is %d!", ret);
        goto err;
    }

err:
    hinic3_smap_destroy(&cfg);
    return ret;
}

void
hinic3_hwpt_flavor_phy_dev_construct_one(const struct rte_pci_addr *pci_addr,
    int function_id, enum hiovs_hwpt_phy_dev_type dev_type)
{
    struct hinic3_dev_node *rc = NULL;

    hinic3_pthread_mutex_lock(&hwpt_phy_dev_mutex);
    rc = (struct hinic3_dev_node *)hinic3_calloc(1, sizeof(struct hinic3_dev_node), HINIC3_PORTS);
    if (rc == NULL) {
        HINIC3_LOG(ERR, VPORT, "failed to aclloc hwpt_phy_dev node");
        goto unlock_exit;
    }

    memcpy(&(rc->phy_dev.pci_addr), pci_addr, sizeof(struct rte_pci_addr));
    rc->phy_dev.type = dev_type;
    rc->phy_dev.refcnt = HWPT_FLAVOR_REFCNT_UNUSED;
    rc->phy_dev.function_id = function_id;

    if (dev_type == HWPT_PHY_DEV_TYPE_PF) {
        hinic3_list_insert(&g_dev_list.pf_list, &(rc->node));
    } else if (dev_type == HWPT_PHY_DEV_TYPE_VF) {
        hinic3_list_insert(&g_dev_list.vf_list, &(rc->node));
    } else {
        HINIC3_LOG(WARNING, VPORT, "unknown phy dev type %u", rc->phy_dev.type);
        hinic3_free(rc);
    }

unlock_exit:
    hinic3_pthread_mutex_unlock(&hwpt_phy_dev_mutex);
}

int
hinic3_get_function_id_by_pci(struct rte_pci_addr *pci_addr)
{
    int ret = 0;
    struct hovs_phy_dev_info dev = {0};
    union bdf_info_u bdf_info;
    bdf_info.bs.func_id_flag = 1;
    if (hinic3_query_bdf_type_get() == QUERY_BDF_TYPE_REAL)
        bdf_info.bs.bdf_type = BDF_TYPE_REAL;
    else
        bdf_info.bs.bdf_type = BDF_TYPE_FAKE;

    ret = hinic3_global_pcie_list_query(1, bdf_info.value, &dev);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "hinic3 get global pcie list err ");
        return -1;
    }

    for (uint32_t i = 0; i < dev.phy_dev_num; i++) {
        struct hovs_pci_addr_info *info = &dev.pci_info[i];
        if (rte_pci_addr_cmp((struct rte_pci_addr *)&info->pci_addr, pci_addr) == 0)
            return info->glb_func_inx;
    }

    return -1;
}

int
hinic3_get_pci_by_function_id(uint16_t func_id, struct rte_pci_addr *pci_addr)
{
    int ret = 0;
    struct hovs_phy_dev_info dev = {0};
    union bdf_info_u bdf_info;

    bdf_info.bs.bdf_type = BDF_TYPE_FAKE;
    bdf_info.bs.func_id_flag = 1;

    ret = hinic3_global_pcie_list_query(HINIC3_FRONT_BACK_FAKE_WITH_FUNC_ID, bdf_info.value, &dev);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "hinic3 get global pcie list err ");
        return -1;
    }

    for (uint32_t i = 0; i < dev.phy_dev_num; i++) {
        struct hovs_pci_addr_info *info = &dev.pci_info[i];
        if (func_id == info->glb_func_inx) {
            memcpy(pci_addr, &info->pci_addr, sizeof(struct rte_pci_addr));
            return 0;
        }
    }

    return HINIC3_PORT_TYPE_NO_QOS;
}

bool
hinic3_check_flavor_phy_dev(const struct rte_pci_addr *pci_addr)
{
    int ret;
    DIR *dir_devices = NULL;
    char pci_addr_buf[HWPT_PCI_ADDR_LEN_MAX] = { 0 };
    char real_path[PATH_MAX] = {0};
    char name_buf[PATH_MAX] = {0};

    ret = hinic3_vf_check_pci(pci_addr);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "failed to exec hinic3_vf_check_pci");
        return false;
    }

    ret = pci_addr_format(pci_addr, pci_addr_buf, HWPT_PCI_ADDR_LEN_MAX);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "pci addr parse failed.");
        return false;
    }

    ret = snprintf(real_path, PATH_MAX, "%s/%s", SYSFS_PCI_DEVICES, pci_addr_buf);
    if ((ret < 0) || (ret >= PATH_MAX)) {
        HINIC3_LOG(ERR, VPORT, "File name too long byte_num(%d)", ret);
        return false;
    }

    if (realpath(real_path, name_buf) == NULL)
        return false;

    /* 检查是否为文件 */
    dir_devices = opendir(name_buf);
    if (dir_devices == NULL) {
        HINIC3_LOG(ERR, VPORT, "Cannot open %s", name_buf);
        return false;
    }
    closedir(dir_devices);
    return true;
}

int
hinic3_dpu_vf_flavor_add(void)
{
    int ret = 0;
    struct hovs_phy_dev_info dev = {0};
    union bdf_info_u bdf_info;
    int query_bdf_type = hinic3_query_bdf_type_get();
    bdf_info.bs.func_id_flag = 1;
    if (query_bdf_type == QUERY_BDF_TYPE_REAL)
        bdf_info.bs.bdf_type = BDF_TYPE_REAL;
    else
        bdf_info.bs.bdf_type = BDF_TYPE_FAKE;

    ret = hinic3_global_pcie_list_query(1, bdf_info.value, &dev);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "hinic3 get global pcie list err");
        return -1;
    }
    hinic3_hwpt_flavor_destroy_flavors();
    for (uint32_t i = 0; i < dev.phy_dev_num; i++) {
        struct hovs_pci_addr_info *info = &dev.pci_info[i];
        if (hinic3_is_support_vf_port() && ((info->type & HINIC3_DEVICE_TYPE) == HWPT_PHY_DEV_TYPE_VF)) {
            hinic3_hwpt_flavor_phy_dev_construct_one((struct rte_pci_addr *)&info->pci_addr, info->glb_func_inx,
                HWPT_PHY_DEV_TYPE_VF);
        } else if (hinic3_is_support_pf_port() && ((info->type & HINIC3_DEVICE_TYPE) == HWPT_PHY_DEV_TYPE_PF)) {
            hinic3_hwpt_flavor_phy_dev_construct_one((struct rte_pci_addr *)&info->pci_addr, info->glb_func_inx,
                HWPT_PHY_DEV_TYPE_PF);
        }
    }

    return 0;
}

int
hinic3_hwpt_flavor_mgmt_init(void)
{
    int ret = 0;

    hinic3_pthread_mutex_lock(&hwpt_phy_dev_mutex);
    hinic3_list_init(&(g_dev_list.pf_list));
    hinic3_list_init(&(g_dev_list.vf_list));
    hinic3_pthread_mutex_unlock(&hwpt_phy_dev_mutex);

    if (hinic3_is_preload_port()) {
        ret = hinic3_dpu_bare_metal_flavor_mgmt_init();
        if (ret != 0) {
            HINIC3_LOG(ERR, VPORT, "hinic3 dpu bare metal flavor mgmt init err, ret is %d", ret);
            return -1;
        }
    } else {
        ret = hinic3_virtual_flavor_mgmt_init();
        if (ret != 0) {
            HINIC3_LOG(ERR, VPORT, "hinic3 virtualized flavor mgmt init err, ret is %d", ret);
            return -1;
        }
    }

    ret = hinic3_port_upcall_mtu_set(OVS_VPORT_TYPE_VF, HINIC3_VF_MTU_MAX);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "hinic3 port upcall mtu set err, ret is %d", ret);
        return -1;
    }

    return 0;
}

void
mgmt_vf_virtio_queue(uint32_t max_queue_num, bool is_del)
{
    struct hinic3_dp_extend_info *dp_info = hinic3_get_offload_extend_info();
    if ((dp_info == NULL) || (dp_info->hw_offload == NULL))
        return;

    struct hinic3_flow_agent_db *hw_offload = dp_info->hw_offload;
    uint32_t real_queue_num = max_queue_num + 1;
    if (!is_del) {
        hw_offload->queue_num.used_now += real_queue_num;
        return;
    }

    if (hw_offload->queue_num.used_now >= real_queue_num)
        hw_offload->queue_num.used_now -= real_queue_num;
    else
        HINIC3_LOG(ERR, VPORT, "An unexpected problem occurred, virtio queue used_now < real_queue_num!");
}

static int
hinic3_insert_port_node(struct hinic3_dev_node *dev_node, struct dev_mgmt *dev)
{
    struct hinic3_dev_node *rc =
        (struct hinic3_dev_node *)hinic3_calloc(1, sizeof(struct hinic3_dev_node), HINIC3_PORTS);
    if (rc == NULL) {
        HINIC3_LOG(ERR, VPORT, "failed to aclloc hwpt_phy_dev node!");
        return -1;
    }

    memcpy(&(rc->phy_dev.pci_addr), &(dev_node->phy_dev.pci_addr), sizeof(struct rte_pci_addr));
    rc->phy_dev.refcnt = dev_node->phy_dev.refcnt;
    rc->phy_dev.type = dev_node->phy_dev.type;

    hinic3_list_insert(&(dev->vf_list), &(rc->node));
    return 0;
}

static int
hinic3_get_used_port_info(struct dev_mgmt *dev)
{
    int ret;
    struct hinic3_dev_node *iter = NULL;
    struct hinic3_list *dev_list = &g_dev_list.vf_list;
    hinic3_list_init(&(dev->vf_list));
    hinic3_list_init(&(dev->pf_list));

    hinic3_pthread_mutex_lock(&hwpt_phy_dev_mutex);
    if (hinic3_list_is_empty(dev_list) == true) {
        HINIC3_LOG(INFO, VPORT, "vf flavor dev list is null");
        hinic3_pthread_mutex_unlock(&hwpt_phy_dev_mutex);
        return 0;
    }

    LIST_FOR_EACH(iter, node, dev_list) {
        uint8_t dev_status = iter->phy_dev.refcnt;
        if (dev_status == HWPT_FLAVOR_REFCNT_USED || dev_status == HWPT_FLAVOR_REFCNT_INVALID) {
            ret = hinic3_insert_port_node(iter, dev);
            if (ret != 0) {
                hinic3_pthread_mutex_unlock(&hwpt_phy_dev_mutex);
                HINIC3_LOG(ERR, VPORT, "hinic3_insert_port_node err, ret is %d", ret);
                return -1;
            }
        }
    }

    hinic3_pthread_mutex_unlock(&hwpt_phy_dev_mutex);
    return 0;
}

static bool
hinic3_updata_vf_flavor_phy_dev_refcnt(const struct rte_pci_addr *pci_addr)
{
    struct hinic3_dev_node *iter_vf = NULL;
    bool is_find_node = false;

    hinic3_pthread_mutex_lock(&hwpt_phy_dev_mutex);
    if (hinic3_list_is_empty(&g_dev_list.vf_list) == false) {
        LIST_FOR_EACH(iter_vf, node, &g_dev_list.vf_list) {
            if (rte_pci_addr_cmp(pci_addr, &(iter_vf->phy_dev.pci_addr)) == 0) {
                iter_vf->phy_dev.refcnt = HWPT_FLAVOR_REFCNT_USED;
                is_find_node = true;
                break;
            }
        }
    }
    hinic3_pthread_mutex_unlock(&hwpt_phy_dev_mutex);

    return is_find_node;
}

static void
hinic3_updata_vf_flavor_dev_info(struct dev_mgmt *dev)
{
    int ret;
    struct hinic3_dev_node *iter = NULL;
    struct hinic3_list *dev_list = &(dev->vf_list);

    if (hinic3_list_is_empty(dev_list) == true) {
        HINIC3_LOG(ERR, VPORT, "vf flavor dev info dev list is null");
        return;
    }

    LIST_FOR_EACH(iter, node, dev_list) {
        bool is_find = hinic3_updata_vf_flavor_phy_dev_refcnt(&(iter->phy_dev.pci_addr));
        if (is_find == false) {
            iter->phy_dev.refcnt = HWPT_FLAVOR_REFCNT_INVALID;
            hinic3_pthread_mutex_lock(&hwpt_phy_dev_mutex);
            ret = hinic3_insert_port_node(iter, &g_dev_list);
            hinic3_pthread_mutex_unlock(&hwpt_phy_dev_mutex);
            if (ret != 0) {
                HINIC3_LOG(ERR, VPORT, "hinic3_insert_port_node err, ret is %d", ret);
            }

            HINIC3_LOG(ERR, VPORT, "This port should be deleted, PCI Address is %04x:%02x:%02x.%d",
                iter->phy_dev.pci_addr.domain, iter->phy_dev.pci_addr.bus, iter->phy_dev.pci_addr.devid,
                iter->phy_dev.pci_addr.function);
        }
    }

    return;
}

static void
hinic3_destroy_used_port_info(struct dev_mgmt *dev)
{
    if (!hinic3_list_is_empty(&(dev->vf_list)))
        hinic3_hwpt_flavor_destroy_list(&(dev->vf_list));

    if (!hinic3_list_is_empty(&(dev->pf_list)))
        hinic3_hwpt_flavor_destroy_list(&(dev->pf_list));
}


struct dev_mgmt *
hinic3_updata_netdev_hwpt_flavor(void)
{
    int ret;
    struct dev_mgmt dev_used_list = {0};

    ret = hinic3_get_used_port_info(&dev_used_list);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "hinic3_get_used_port_info err, ret is %d", ret);
        return NULL;
    }
    /* 清空全局链表，重新获取设备信息 */
    hinic3_hwpt_flavor_destroy_flavors();
    ret = hinic3_smart_vf_flavor_add();
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "hinic3_smart_vf_flavor_add err, ret is %d", ret);
        return NULL;
    }
    /* 更新设备信息 */
    hinic3_updata_vf_flavor_dev_info(&dev_used_list);
    /* 销毁链表 */
    hinic3_destroy_used_port_info(&dev_used_list);
    return &g_dev_list;
}

struct dev_mgmt *
get_netdev_hwpt_flavor(void)
{
    return &g_dev_list;
}

void
hinic3_dev_init(void)
{
    hinic3_pthread_mutex_init(&hwpt_phy_dev_mutex);
}

void
hinic3_dev_uninit(void)
{
    hinic3_pthread_mutex_destroy(&hwpt_phy_dev_mutex);
}
