/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2025 Huawei Technologies Co., Ltd
 */

#include <unistd.h>
#include "rte_dev.h"
#include "rte_devargs.h"
#include <rte_atomic.h>
#include "hinic3_tlv_key.h"
#include "hinic3_bond_controller.h"
#include "hinic3_util.h"
#include "hinic3_capture_main.h"
#include "hinic3_agent.h"
#include "hinic3_vf_port_qos_public.h"
#include "hinic3_flow_agent.h"
#include "hinic3_offload_flow_port.h"
#include "hinic3_qos.h"
#include "hinic3_mtr.h"
#include "hinic3_smap.h"
#include "hinic3_vxlan.h"
#include "hinic3_flow_dump.h"
#include "hinic3_hugepage_meminfo.h"
#include "hinic3_offload_flow_port.h"
#include "hinic3_vf_controller.h"

#define BITMAP_NUM                       64
#define HINIC3_USED_DRIVER_NAME          "vfio-pci"
#define HINIC3_DRIVER_STR                "driver"
#define DEFAULT_MAX_QUEUE_NUM            4
#define DEFAULT_UPCALL_QUEUE_NUM         4
#define PORT_SECTION                     "hinic3_port"

static struct share_upcall_ref {
    uint8_t share_upcall_num;
    uint16_t ref_cnt; // single thread call
} g_shared_upcall_cnt = {
    .share_upcall_num = 0,
    .ref_cnt = 0
};

static struct rte_flow_ops *g_hinic3_vf_flow_ops = NULL;

int
hinic3_share_upcall_set(uint8_t queue_num)
{
    if (g_shared_upcall_cnt.ref_cnt != 0)
        return -1;

    g_shared_upcall_cnt.share_upcall_num = queue_num;
    return 0;
}

void
hinic3_share_upcall_incress(void)
{
    g_shared_upcall_cnt.ref_cnt += 1;
}

void
hinic3_share_upcall_decress(void)
{
    if (g_shared_upcall_cnt.ref_cnt == 0)
        return;
    
    g_shared_upcall_cnt.ref_cnt -= 1;
    if (g_shared_upcall_cnt.ref_cnt == 0)
        g_shared_upcall_cnt.share_upcall_num = 0;
}

uint8_t
hinic3_get_vf_share_upcall_num(void)
{
    return g_shared_upcall_cnt.share_upcall_num;
}

uint16_t
hinic3_get_vf_share_upcall_ref_cnt(void)
{
    return g_shared_upcall_cnt.ref_cnt;
}

void
hinic3_vf_flow_ops_construct(void)
{
    struct rte_flow_ops *flow_ops = hinic3_calloc(1, sizeof(struct rte_flow_ops), HINIC3_PORTS);
    if (flow_ops == NULL)
        return;

    flow_ops->validate = hinic3_eth_flow_validate;
    flow_ops->create = hinic3_eth_flow_create;
    flow_ops->destroy = hinic3_eth_flow_destroy;
    flow_ops->flush = hinic3_eth_flow_flush;
    flow_ops->query = hinic3_eth_flow_query;
    flow_ops->dev_dump = hinic3_eth_flow_dev_dump;
    flow_ops->get_aged_flows = hinic3_eth_get_aged_flow;
    flow_ops->tunnel_decap_set = hinic3_flow_tunnel_decap_set;
    g_hinic3_vf_flow_ops = flow_ops;
}

void
hinic3_vf_flow_ops_destroy(void)
{
    if (g_hinic3_vf_flow_ops == NULL)
        return;

    hinic3_free(g_hinic3_vf_flow_ops);
    g_hinic3_vf_flow_ops = NULL;
}

static int
hinic3_vf_get_used_status(const struct hinic3_vf_dev *vf_dev, bool *is_used)
{
    int err;
    ssize_t len;
    char *device_driver = NULL;
    char filename[PATH_MAX] = {0};
    char link_buf[PATH_MAX] = {0};

    if (vf_dev == NULL) {
        HINIC3_LOG(ERR, VPORT, "device is NULL");
        return -1;
    }

    err = snprintf(filename, sizeof(filename), "%s/%.4x:%.2x:%.2x.%x/%s", SYSFS_PCI_DEVICES,
        vf_dev->pci_addr.domain, vf_dev->pci_addr.bus, vf_dev->pci_addr.devid, vf_dev->pci_addr.function,
        HINIC3_DRIVER_STR);
    if (err <= 0 || err >= PATH_MAX) {
        HINIC3_LOG(ERR, VPORT, "Get device driver path err");
        return -1;
    }

    HINIC3_LOG(DEBUG, VPORT, "Device dirver path: %s", filename);
    len = readlink(filename, link_buf, PATH_MAX);
    if (len < 0 || len >= PATH_MAX) {
        HINIC3_LOG(INFO, VPORT, "Device driver soft link not exist");
        return 0;
    }
    link_buf[len] = '\0';
    HINIC3_LOG(DEBUG, VPORT, "Device driver soft link: %s", link_buf);

    device_driver = strrchr(link_buf, '/');
    if (device_driver == NULL) {
        HINIC3_LOG(ERR, VPORT, "Get device driver err");
        return -1;
    }

    if (strcmp(device_driver + 1, HINIC3_USED_DRIVER_NAME) == 0) {
        *is_used = true;
        HINIC3_LOG(INFO, VPORT, " :The current device %.4x:%.2x:%.2x.%x is occupied by a virtual machine",
            vf_dev->pci_addr.domain, vf_dev->pci_addr.bus, vf_dev->pci_addr.devid, vf_dev->pci_addr.function);
    }
    return 0;
}

static int
hinic3_vf_vni_update(struct hinic3_vf_dev *vf_dev)
{
    int ret = 0;
    struct smap smap_args, unset_args;
    hinic3_smap_init(&smap_args);
    hinic3_smap_init(&unset_args);
    switch (vf_dev->state[HINIC3_VF_CONFIG_VNI]) {
        case HINIC3_VF_CONFIG_ADD:
            hinic3_smap_add_format(HINIC3_PORTS, &smap_args, HINIC3_PORT_VNI, "%u", vf_dev->vni);
            ret = hinic3_port_mgmt_set(vf_dev->vport_id, &smap_args, &unset_args);
            if (ret != 0) {
                HINIC3_LOG(ERR, VPORT, "hinic3_port_mgmt_set VNI failed, port %u", vf_dev->vport_id);
                vf_dev->vni = VNI_INVALID;
            }
            break;
        case HINIC3_VF_CONFIG_DEL:
            if (vf_dev->vni == VNI_INVALID) {
                break;
            }
            hinic3_smap_add_format(HINIC3_PORTS, &smap_args, HINIC3_PORT_VNI, "%u", HINIC3_VF_VNI_DEFAULT);
            ret = hinic3_port_mgmt_set(vf_dev->vport_id, &smap_args, &unset_args);
            if (ret != 0) {
                HINIC3_LOG(ERR, VPORT, "hinic3_port_mgmt_set VNI failed, port %u", vf_dev->vport_id);
            }
            vf_dev->vni = VNI_INVALID;
            break;
        case HINIC3_VF_CONFIG_STABLE:
            break;
        default:
            break;
    }
    hinic3_smap_destroy(&smap_args);
    hinic3_smap_destroy(&unset_args);
    vf_dev->state[HINIC3_VF_CONFIG_VNI] = (vf_dev->vni == VNI_INVALID ?
        HINIC3_VF_CONFIG_NONE : HINIC3_VF_CONFIG_DEL);
    return ret;
}

int
hinic3_vf_dynamic_port_add(struct hinic3_vf_dev *vf_dev)
{
    int ret;
    struct smap args;
    uint16_t virtio_queue_depth;

    if (vf_dev->vport_id != HINIC3_PORT_ID_INVALID)
        return 0;

    ret = hinic3_alloc_queues_to_port(&vf_dev->upcall_queue, vf_dev->n_upcall_queue);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "failed to alloc queues to vf in dynamic port add!");
        return ret;
    }

    hinic3_smap_init(&args);
    if (hinic3_user_scenario_get() == COM_BD && vf_dev->function_id != 0) {
        HINIC3_LOG(INFO, VPORT, "add port by function id.");
        hinic3_smap_add_format(HINIC3_PORTS, &args, HINIC3_PORT_FUNCTION_ID, "%u", vf_dev->function_id);
    } else {
        HINIC3_LOG(INFO, VPORT, "add port by pci addr.");
        hinic3_smap_add_format(HINIC3_PORTS, &args, HINIC3_PORT_PCI_ADDR, "%" PRIuPTR, (uintptr_t)&vf_dev->pci_addr);
    }
    hinic3_smap_add_format(HINIC3_PORTS, &args, HINIC3_PORT_BLOCK_SIZE, "%u", vf_dev->max_queue_num);
    hinic3_smap_add_format(HINIC3_PORTS, &args, HINIC3_PORT_UPCALL_QUEUE_NUM, "%u", vf_dev->n_upcall_queue);
    if (hinic3_get_virtual_queue_mode_enabled() == true) {
        hinic3_smap_add_format(
            HINIC3_PORTS, &args, HINIC3_PORT_UPCALL_QUEUE_MAP, "%" PRIuPTR, (uintptr_t)vf_dev->upcall_queue.upcall_queue_id);
    } else if (vf_dev->share_upcall == true) {
        hinic3_smap_add_format(HINIC3_PORTS, &args, HINIC3_PORT_UPCALL_REUSE, "%u", vf_dev->share_upcall);
        HINIC3_LOG(INFO, VPORT, "vf_dev->share_upcall is %u", vf_dev->share_upcall);
    }

    virtio_queue_depth = hinic3_virtio_queue_depth();
    if (vf_dev->virtio_queue_depth != 0)
        hinic3_smap_add_format(HINIC3_PORTS, &args, HINIC3_PORT_VIRTIO_QUEUE_DEPTH, "%u", vf_dev->virtio_queue_depth);
    else
        hinic3_smap_add_format(HINIC3_PORTS, &args, HINIC3_PORT_VIRTIO_QUEUE_DEPTH, "%u", virtio_queue_depth);

    hinic3_pthread_mutex_init(&vf_dev->aged_flow_list.mutex);
    ret = hinic3_port_mgmt_add_dynamic(&vf_dev->vport_id, &args);
    if (ret != 0) {
        hinic3_free_queues_from_port(&vf_dev->upcall_queue, vf_dev->n_upcall_queue);
        hinic3_pthread_mutex_destroy(&vf_dev->aged_flow_list.mutex);
        HINIC3_LOG(ERR, VPORT, "failed to add dynamic port of the vf!");
    }

    hinic3_smap_destroy(&args);

    return ret;
}

static void
hinic3_clear_vf_port_info(struct hinic3_vf_dev *vf_dev)
{
    int ret = hinic3_mtu_set(vf_dev->vport_id, HINIC3_DEFAULT_MTU);
    if (ret != 0)
        HINIC3_LOG(ERR, VPORT, "hinic3 vf set mtu failed, vport:%u!", vf_dev->vport_id);

    if (hinic3_support_multi_qos_get() == true) {
        if (vf_dev->qos_type == VF_PORT_QOS_FLAG)
            hinic3_port_meter_clear(vf_dev, vf_dev->vport_id);

        if (vf_dev->group_qos_id != 0) {
            ret = hinic3_group_meter_remove(vf_dev->vport_id, vf_dev->group_qos_id);
            if (ret != 0)
                HINIC3_LOG(ERR, VPORT, "hinic3 vf clear group meter failed, vport:%u!", vf_dev->vport_id);
        }
    }

    ret = hinic3_port_statistics_flush(vf_dev->vport_id);
    if (ret != 0)
        HINIC3_LOG(ERR, VPORT, "hinic3 vf clear port statistics info failed, vport:%u!", vf_dev->vport_id);

    rte_atomic64_set(&vf_dev->vf_upcall_pk_num, 0);
    rte_atomic64_set(&vf_dev->vf_upcall_pk_byt, 0);
    rte_atomic64_set(&vf_dev->vf_reinject_pk_num, 0);
    rte_atomic64_set(&vf_dev->vf_reinject_pk_byt, 0);

    ret = hinic3_port_mgmt_set_usage_state(vf_dev->vport_id, HINIC3_SET_VF_STATE_DOWN);
    if (ret != 0)
        HINIC3_LOG(ERR, VPORT, "port mgmt set vf state down err, ret is %d", ret);

    return;
}

static void
hinic3_vf_dev_del(struct rte_eth_dev *dev)
{
    struct hinic3_vf_dev *vf_dev = hinic3_ethdev_get_vf_private(dev);

    if (vf_dev->vport_id != HINIC3_PORT_ID_INVALID) {
        /* 端口删除前，返还队列资源 */
        hinic3_free_queues_from_port(&vf_dev->upcall_queue, vf_dev->n_upcall_queue);
        /* 端口删除前，清理端口信息 */
        hinic3_clear_vf_port_info(vf_dev);
        /* 端口删除前，清理抓包信息 */
        pcap_task_stop_as_eth_port_del(vf_dev->vport_id);

        if (hinic3_support_port_hot_plug()) {
            if (hinic3_hotplug_del(vf_dev->vport_id) != 0)
                HINIC3_LOG(ERR, VPORT, "hinic3_hotplug_del failed!");
        }

        if (hinic3_is_preload_dev(vf_dev) == false)
            hinic3_port_mgmt_del(vf_dev->vport_id);
        mgmt_vf_virtio_queue(vf_dev->max_queue_num, true);
        hinic3_ifindex_port_remove(vf_dev->vport_id);
        vf_dev->vport_id = HINIC3_PORT_ID_INVALID;
        vf_dev->dpdk_port_id = INVALID_DPDK_PORT_ID;
        int ret = hinic3_hwpt_flavor_set_phy_dev_refcnt(&vf_dev->pci_addr, vf_dev->function_id, HWPT_FLAVOR_REFCNT_UNUSED);
        if (ret != 0)
            HINIC3_LOG(ERR, VPORT, "failed to reset flavor of the vport!");
    }
}

static int
hinic3_set_mac_to_vf(struct hinic3_vf_dev *vf_dev)
{
    int ret = 0;

    if (!eth_addr_is_zero(vf_dev->mac_addr)) {
        ret = hinic3_etheraddr_set(vf_dev->vport_id, vf_dev->mac_addr);
        if (ret != 0)
            HINIC3_LOG(ERR, VPORT, "failed to set mac for vf!");
    } else {
        ret = hinic3_etheraddr_get(vf_dev->vport_id, (struct eth_address*)&vf_dev->mac_addr);
        if (ret != 0)
            HINIC3_LOG(ERR, VPORT, "failed to get vf's default mac!");
    }
    return ret;
}

static int
hinic3_vf_dev_update(struct hinic3_vf_dev *vf_dev, struct rte_eth_dev_data *data)
{
    int ret;
    struct rte_ether_addr* mac_addrs = NULL;

    mgmt_vf_virtio_queue(vf_dev->max_queue_num, false);
    /* set mac to vf */
    ret = hinic3_set_mac_to_vf(vf_dev);
    if (ret != 0)
        return ret;

    mac_addrs = data->mac_addrs;
    if (mac_addrs == NULL)
        return -1;

    rte_ether_addr_copy((struct rte_ether_addr *)&vf_dev->mac_addr.ea, mac_addrs);

    ret = hinic3_eth_get_dpdk_port_id(vf_dev->vport_id, &vf_dev->dpdk_port_id);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "failed to get dpdk port id of vport:%u!", vf_dev->vport_id);
        return ret;
    }

    ret = hinic3_hwpt_flavor_set_phy_dev_refcnt(&vf_dev->pci_addr, vf_dev->function_id, HWPT_FLAVOR_REFCNT_USED);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "failed to set flavor of the vport:%u!", vf_dev->vport_id);
        return -EPERM;
    }

    if (vf_dev->share_upcall == 1)
        hinic3_share_upcall_incress();

    return 0;
}

static int
hinic3_vf_dev_add(struct rte_eth_dev *dev)
{
    int ret;
    struct hinic3_vf_dev *vf_dev = hinic3_ethdev_get_vf_private(dev);
    bool is_used = false;
    enum hinic3_work_mode work_mode = hinic3_device_mode_get();
    struct rte_eth_dev_data *data = dev->data;

    hinic3_list_init(&vf_dev->aged_flow_list.list_head);
    
    if (work_mode == SMART_NIC_MODE)
        (void)hinic3_vf_get_used_status((const struct hinic3_vf_dev*)vf_dev, &is_used);

    ret = hinic3_vf_dynamic_port_add(vf_dev);
    if (ret != 0) {
        if ((ret == ENOMEM) && is_used) {
            HINIC3_LOG(ERR, VPORT,
                "The port is occupied by a virtual machine, please shut down the virtual machine and re-add the port!");
        }
        goto end;
    }

    ret = hinic3_vf_dev_update(vf_dev, data);
    if (ret != 0)
        goto clean;

    HINIC3_LOG(INFO, VPORT, "id:%" PRIu16 "vport pci is %.4x:%.2x:%.2x.%x, upcall queue:%" PRIu8 ", virtio queue:%" PRIu32 ", vdpa feature:%#llx",
        vf_dev->vport_id, vf_dev->pci_addr.domain, vf_dev->pci_addr.bus, vf_dev->pci_addr.devid, vf_dev->pci_addr.function,
        vf_dev->n_upcall_queue, vf_dev->max_queue_num, hinic3_vdpa_feature_get());
    return 0;

clean:
    hinic3_vf_dev_del(dev);
end:
    return ret;
}

int
hinic3_vf_dev_infos_get(struct rte_eth_dev *dev, struct rte_eth_dev_info *info)
{
    if (hinic3_is_ethdev_valid(dev) == false)
        return -EINVAL;

    if (info == NULL) {
        HINIC3_LOG(ERR, VPORT, "The dev info is NULL.");
        return -EINVAL;
    }

    int ret;
    uint16_t max_rx_tx_queue = 0;
    struct hinic3_port_capability cap = {0};
    struct hinic3_vf_dev *vf_dev = hinic3_ethdev_get_vf_private(dev);
    struct rte_device *device = dev->device;
    const struct rte_driver *driver = device->driver;

    
    ret = hinic3_port_mgmt_get_capability(&cap);
    if (ret != 0)
        return ret;

    if (hinic3_is_preload_dev(vf_dev) == true) {
        max_rx_tx_queue = hinic3_upcall_queue_num_get();
        info->max_rx_queues = max_rx_tx_queue;
        info->max_tx_queues = max_rx_tx_queue;
    } else {
        info->max_tx_queues = MAX_RX_QUEUE_PER_VPORT;
        if (cap.supported_upcall_qnum != MAX_RX_QUEUE_PER_VPORT)
            info->max_rx_queues = HINIC3_VF_DEFAULT_NB_QUEUES;
        else
            info->max_rx_queues = MAX_RX_QUEUE_PER_VPORT;
    }
    info->driver_name = driver->name;
    info->max_rx_pktlen = HINIC3_VF_MTU_MAX + HINIC3_VF_SIZE_OFFSET;
    info->min_mtu = HINIC3_VF_MTU_MIN;
    info->max_mtu = HINIC3_VF_MTU_MAX;
    info->if_index = vf_dev->vport_id;
    info->max_mac_addrs = HINIC3_MAX_MAC_ADDRS;
    info->max_hash_mac_addrs = HINIC3_MAX_HASH_MAC_ADDRS;
    info->max_vfs = HINIC3_MAX_VFS;
    info->max_vmdq_pools = HINIC3_MAX_VMDQ_POOLS;
    info->rx_offload_capa = HINIC3_ETH_RX_OFFLOAD_SCATTER;
    if (hinic3_card_mod_get() == PROG_MODE) {
        info->tx_offload_capa = HINIC3_ETH_TX_OFFLOAD_MULTI_SEGS;
    } else {
        info->tx_offload_capa = HINIC3_ETH_TX_OFFLOAD_CAPA;
    }
    info->flow_type_rss_offloads = HINIC3_ETH_SUPPORT_RSS;

    return 0;
}

int
hinic3_vf_set_link_up(struct rte_eth_dev *dev)
{
    if (hinic3_is_ethdev_valid(dev) == false)
        return -EINVAL;

    struct rte_eth_dev_data *data = dev->data;
    struct rte_eth_link *dev_link = &data->dev_link;
    dev_link->link_status = RTE_ETH_LINK_UP;

    return 0;
}

int
hinic3_vf_set_link_down(struct rte_eth_dev *dev)
{
    if (hinic3_is_ethdev_valid(dev) == false)
        return -EINVAL;

    struct rte_eth_dev_data *data = dev->data;
    struct rte_eth_link *dev_link = &data->dev_link;
    dev_link->link_status = RTE_ETH_LINK_DOWN;

    return 0;
}

int hinic3_vf_dev_start(struct rte_eth_dev *dev)
{
    if (hinic3_is_ethdev_valid(dev) == false)
        return -EINVAL;

    struct hinic3_vf_dev *vf_dev = hinic3_ethdev_get_vf_private(dev);
    struct rte_eth_dev_data *data = dev->data;
    struct rte_eth_link *dev_link = &data->dev_link;

    if (vf_dev == NULL) {
        HINIC3_LOG(ERR, VPORT, "The current device private_data is NULL!");
        return -1;
    }

    if (vf_dev->vport_id == HINIC3_PORT_ID_INVALID) {
        HINIC3_LOG(ERR, VPORT, "can't start device: invalid vport id!");
        return -1;
    }

    dev_link->link_status = RTE_ETH_LINK_UP;
    data->dev_started = HINIC3_DEV_STATE_START;

    /* vf热插拔场景中，下发mtu后，需要进行热拔后热擦 */
    if (hinic3_support_port_hot_plug()) {
        int ret = hinic3_hotplug_add(vf_dev->vport_id);
        if (ret != 0) {
            HINIC3_LOG(ERR, VPORT, "hinic3_hotplug_add failed!");
            return ret;
        }
    }
    return 0;
}

int hinic3_vf_dev_stop(struct rte_eth_dev *dev)
{
    if (hinic3_is_ethdev_valid(dev) == false)
        return -EINVAL;

    struct rte_eth_dev_data *data = dev->data;
    struct rte_eth_link *dev_link = &data->dev_link;
    dev_link->link_status = RTE_ETH_LINK_DOWN;
    data->dev_started = HINIC3_DEV_STATE_STOP;

    return 0;
}

int hinic3_vf_dev_close(struct rte_eth_dev *dev)
{
    if (hinic3_is_ethdev_valid(dev) == false)
        return -EINVAL;

    struct rte_eth_dev_data *data = dev->data;
    struct hinic3_vf_dev *vf_dev = hinic3_ethdev_get_vf_private(dev);

    if (vf_dev->share_upcall == 1)
        hinic3_share_upcall_decress();

    if (vf_dev->share_upcall == 0 || g_shared_upcall_cnt.ref_cnt == 0) {
        for (uint16_t index = 0; index < data->nb_rx_queues; index++)
            hinic3_vf_rx_queue_release(dev, index);
    }

    for (uint16_t index = 0; index < data->nb_tx_queues; ++index)
        hinic3_vf_tx_queue_release(dev, index);

    if (data->mac_addrs != NULL) {
        hinic3_rte_free(data->mac_addrs);
        data->mac_addrs = NULL;
    }

    hinic3_vf_dev_del(dev);
    return 0;
}

static int
hinic3_vf_rx_queue_setup_check(struct rte_eth_dev *dev, uint16_t idx, uint16_t desc, struct rte_mempool *mp)
{
    if (hinic3_is_ethdev_valid(dev) == false)
        return -EINVAL;

    if (mp == NULL) {
        HINIC3_LOG(ERR, VPORT, "mp is NULL!");
        return -EINVAL;
    }

    uint16_t vport_rx_depth = hinic3_vport_rx_depth();
    struct rte_eth_dev_data *data = dev->data;
    bool is_open_ovs = hinic3_check_masked_to_exact_switch();
    if (vport_rx_depth != desc && !is_open_ovs) {
        HINIC3_LOG(ERR, VPORT, "vport_rx_depth differences. desc is %" PRIu16 ", vport_rx_depth is %" PRIu16 "!",
            desc, vport_rx_depth);
        return -EINVAL;
    }

    if (idx >= MAX_RX_QUEUE_PER_VPORT) {
        HINIC3_LOG(ERR, VPORT, "vport upcall index(%" PRIu16 ") greater than max(%" PRIu16 ")!",
            idx, MAX_RX_QUEUE_PER_VPORT);
        return -EINVAL;
    }

    if (idx >= data->nb_rx_queues) {
        HINIC3_LOG(ERR, VPORT, "vf rx queue setup check, queue index invalid, max queue is %" PRIu16 "!",
            data->nb_rx_queues);
        return -EINVAL;
    }

    return 0;
}

static int
hinic3_standard_queue_setup(struct hinic3_queue *rxq, uint16_t upcall_queue_id, unsigned int socket_id,
    struct rte_mempool *mp, uint8_t share_upcall)
{
    struct hinic3_standard_queue *stdqueue =
        hinic3_rte_zmalloc_socket(HINIC3_PORTS, sizeof(struct hinic3_standard_queue), CACHE_LINE_SIZE, socket_id);
    if (stdqueue == NULL) {
        HINIC3_LOG(ERR, VPORT, "alloc vport queue rx%" PRIu16 " failed, no enough memory!", rxq->queue_id);
        return -ENOMEM;
    }

    if (hinic3_port_mgmt_setup_upcall_queue(rxq->vport_id, rxq->queue_id, socket_id, mp) != 0) {
        HINIC3_LOG(ERR, VPORT, "standard queue setup, port setup upcall queue error!");
        hinic3_rte_free(stdqueue);
        return -EPERM;
    }

    stdqueue->queue_info = rxq;
    stdqueue->hiovs_queue_id = upcall_queue_id;
    stdqueue->mp = mp;
    stdqueue->share_upcall = share_upcall;
    rxq->variant.stdqueue = stdqueue;

    return 0;
}

static int
hinic3_vf_standard_rx_queue_setup(struct rte_eth_dev *dev, uint16_t idx, uint16_t desc,
    unsigned int socket_id, const struct rte_eth_rxconf *conf __rte_unused, struct rte_mempool *mp)
{
    int ret = hinic3_vf_rx_queue_setup_check(dev, idx, desc, mp);
    if (ret != 0)
        return ret;

    struct rte_eth_dev_data *data = dev->data;
    struct hinic3_vf_dev *vf_dev = hinic3_ethdev_get_vf_private(dev);
    struct hinic3_queue *rxq = &vf_dev->upcall_queue.rx_queues[idx];
    uint16_t dpdk_index_id = 0;
    ret = hinic3_get_port_index_by_dev(dev, &dpdk_index_id);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "vf standard rx queue setup, hinic3_get_port_index_by_dev error!");
        return -EPERM;
    }

    rxq->queue_id = idx;
    rxq->vport_id = vf_dev->vport_id;
    rxq->dpdk_port_id = vf_dev->dpdk_port_id;
    rxq->dpdk_index_id = dpdk_index_id;
    rxq->type = HINIC3_STANDARD_RX_QUEUE;
    rte_atomic64_set(&rxq->pkt_stats, 0);
    ret = hinic3_standard_queue_setup(
        rxq, vf_dev->upcall_queue.upcall_queue_id[idx], socket_id, mp, vf_dev->share_upcall);
    if (ret != 0)
        return ret;

    vf_dev->upcall_queue.is_queue_valid[idx] = true;
    data->rx_queues[idx] = rxq;
    HINIC3_LOG(INFO, VPORT,
        "vport queue rx%" PRIu16 " setup success, vport id: %" PRIu16 ", hiovs queue id: %" PRIu16 ".",
        rxq->queue_id, rxq->vport_id, hinic3_queue_get_hiovs_queue_id(rxq));
    return 0;
}

static void
hinic3_vf_standard_rx_queue_release(struct rte_eth_dev *dev, uint16_t queue_id)
{
    if (hinic3_is_ethdev_valid(dev) == false)
        return;

    struct rte_eth_dev_data *data = dev->data;
    struct hinic3_vf_dev *vf_dev = hinic3_ethdev_get_vf_private(dev);
    struct hinic3_queue *rxq = NULL;

    if (queue_id >= data->nb_rx_queues) {
        HINIC3_LOG(ERR, VPORT, "vf standard rx queue release, queue index invalid, max queue is %" PRIu16 "!",
            data->nb_rx_queues);
        return;
    }

    rxq = data->rx_queues[queue_id];
    if (rxq != NULL) {
        if (vf_dev->upcall_queue.is_queue_valid[queue_id] == true) {
            if (hinic3_port_mgmt_release_upcall_queue(vf_dev->vport_id, queue_id) != 0)
                HINIC3_LOG(ERR, VPORT, "vf standard rx queue release, release upcall queue error!");

            hinic3_rte_free(rxq->variant.stdqueue);
            hinic3_reset_queue_info(rxq);
            vf_dev->upcall_queue.is_queue_valid[queue_id] = false;
        }
        data->rx_queues[queue_id] = NULL;
    }
}

static int
hinic3_vf_tx_queue_setup_check(struct rte_eth_dev *dev, uint16_t idx, uint16_t desc)
{
    if (hinic3_is_ethdev_valid(dev) == false)
        return -EINVAL;

    uint16_t vport_tx_depth = hinic3_vport_tx_depth();
    struct rte_eth_dev_data *data = dev->data;
    bool is_open_ovs = hinic3_check_masked_to_exact_switch();
    if (vport_tx_depth != desc && !is_open_ovs) {
        HINIC3_LOG(ERR, VPORT, "vport_tx_depth differences. desc is %" PRIu16 ", vport_tx_depth is %" PRIu16 "!",
            desc, vport_tx_depth);
        return -EINVAL;
    }

    if (idx >= MAX_TX_QUEUE_PER_VPORT) {
        HINIC3_LOG(ERR, VPORT, "vport reinject index(%" PRIu16 ") greater than max(%" PRIu16 ")!",
            idx, MAX_TX_QUEUE_PER_VPORT);
        return -EINVAL;
    }

    if (idx >= data->nb_tx_queues) {
        HINIC3_LOG(ERR,  VPORT, "vf tx queue setup check, queue index invalid, max queue is %" PRIu16 "!",
            data->nb_tx_queues);
        return -EINVAL;
    }

    return 0;
}

int
hinic3_vf_tx_queue_setup(struct rte_eth_dev *dev, uint16_t idx, uint16_t desc,
    unsigned int socket_id __rte_unused, const struct rte_eth_txconf *conf __rte_unused)
{
    int ret = hinic3_vf_tx_queue_setup_check(dev, idx, desc);
    if (ret != 0)
        return ret;

    struct rte_eth_dev_data *data = dev->data;
    struct hinic3_vf_dev *vf_dev = hinic3_ethdev_get_vf_private(dev);
    struct hinic3_queue *txq = &vf_dev->reinject_queue.tx_queues[idx];
    uint16_t dpdk_index_id = 0;
    ret = hinic3_get_port_index_by_dev(dev, &dpdk_index_id);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "vf tx queue setup, hinic3_get_port_index_by_dev error!");
        return -EPERM;
    }

    txq->queue_id = idx;
    txq->vport_id = vf_dev->vport_id;
    txq->dpdk_port_id = vf_dev->dpdk_port_id;
    txq->dpdk_index_id = dpdk_index_id;
    txq->type = HINIC3_TX_QUEUE;
    rte_atomic64_set(&txq->pkt_stats, 0);
    
    vf_dev->reinject_queue.is_queue_valid[idx] = true;
    data->tx_queues[idx] = txq;
    HINIC3_LOG(INFO, VPORT, "vport queue tx%" PRIu16 " setup success, vport id: %" PRIu16 ".",
        txq->queue_id, txq->vport_id);
    return 0;
}

void
hinic3_vf_tx_queue_release(struct rte_eth_dev *dev, uint16_t queue_id)
{
    if (hinic3_is_ethdev_valid(dev) == false)
        return;

    struct rte_eth_dev_data *data = dev->data;
    struct hinic3_vf_dev *vf_dev = hinic3_ethdev_get_vf_private(dev);
    struct hinic3_queue *txq = NULL;

    if (queue_id >= data->nb_tx_queues) {
        HINIC3_LOG(ERR, VPORT, "vf tx queue release, queue index invalid, max queue is %" PRIu16 "!",
            data->nb_tx_queues);
        return;
    }

    txq = data->tx_queues[queue_id];
    if (txq != NULL) {
        if (vf_dev->reinject_queue.is_queue_valid[queue_id] == true) {
            hinic3_reset_queue_info(txq);
            vf_dev->reinject_queue.is_queue_valid[queue_id] = false;
        }
        data->tx_queues[queue_id] = NULL;
    }
}

static int
hinic3_vf_info_update(struct hinic3_vf_dev *vf_dev)
{
    int ret = hinic3_eth_get_upcall_queue_map(vf_dev->vport_id, vf_dev->n_upcall_queue,
        vf_dev->upcall_queue.upcall_queue_id);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "hinic3_eth_get_upcall_queue_map error!");
        return -EPERM;
    }

    ret = hinic3_port_mgmt_set_usage_state(vf_dev->vport_id, HINIC3_SET_VF_STATE_UP);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "port mgmt set vf state up err, ret is %d!", ret);
        return -EPERM;
    }

    ret = hinic3_vf_vni_update(vf_dev);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "vni set failed!");
        return -EPERM;
    }

    return 0;
}

static int
hinic3_vf_dev_set_ifindex(struct hinic3_vf_dev *dev, struct rte_eth_dev_data *data)
{
    int ret;

    dev->port_ifindex = data->port_id;
    ret = hinic3_set_port_map_by_ifindex(dev->vport_id, dev->port_ifindex);
    return ret;
}

static int
hinic3_vf_dev_configure_check(struct rte_eth_dev *dev)
{
    if (hinic3_is_ethdev_valid(dev) == false)
        return -EINVAL;

    struct rte_eth_dev_data *data = dev->data;
    if (data->dev_started != HINIC3_DEV_STATE_STOP) {
        HINIC3_LOG(ERR, VPORT, "the device must be stopped when the port is configured!");
        return -EPERM;
    }

    if (data->nb_rx_queues == 0) {
        HINIC3_LOG(ERR, VPORT, "vf dev configure check, number of rx queue can't be zero!");
        return -EPERM;
    }

    return 0;
}

static int
hinic3_check_vf_upcall_queue(struct hinic3_vf_dev *vf_dev)
{
    /* 裸金属场景需要判断是否与配置文件一致 */
    if (hinic3_is_preload_dev(vf_dev) == true) {
        if (vf_dev->n_upcall_queue != hinic3_upcall_queue_num_get()) {
            HINIC3_LOG(ERR, VPORT, "vf upcall queue(%u) is not equal to configuration file num(%u)!",
                vf_dev->n_upcall_queue, hinic3_upcall_queue_num_get());
            return -EPERM;
        }
        vf_dev->n_upcall_queue = hinic3_upcall_queue_num_get();
    }

    /* 软件模拟upcall队列模式下不可使用共享upcall队列 */
    if (hinic3_get_virtual_queue_mode_enabled() == true) {
        vf_dev->share_upcall = false;
        if (vf_dev->n_upcall_queue > MAX_RX_QUEUE_PER_VPORT) {
            HINIC3_LOG(ERR, VPORT, "vf upcall queue(%u) cant be greater than (%u)!",
                vf_dev->n_upcall_queue, MAX_RX_QUEUE_PER_VPORT);
            return -EPERM;
        }
    }

    /* 虚拟化场景需要判断share_upcall数量是否与第一次设置一致 */
    if (vf_dev->share_upcall == true) {
        if (hinic3_get_vf_share_upcall_ref_cnt() == 0) {
            hinic3_share_upcall_set(vf_dev->n_upcall_queue);
            HINIC3_LOG(INFO, VPORT, "The size of the shared upcall queue is set to %d.",
                g_shared_upcall_cnt.share_upcall_num);
        } else if (g_shared_upcall_cnt.share_upcall_num != vf_dev->n_upcall_queue) {
            HINIC3_LOG(ERR, VPORT, "The shared upcall queue is inconsistent with the first setting!");
            return -EPERM;
        }
    }

    return 0;
}

int
hinic3_vf_dev_configure(struct rte_eth_dev *dev)
{
    int ret = hinic3_vf_dev_configure_check(dev);
    if (ret != 0)
        return ret;

    struct rte_eth_dev_data *data = dev->data;
    struct hinic3_vf_dev *vf_dev = hinic3_ethdev_get_vf_private(dev);

    vf_dev->dev = dev;
    if (data->nb_rx_queues != vf_dev->n_upcall_queue)
        hinic3_vf_dev_del(dev);

    vf_dev->n_upcall_queue = data->nb_rx_queues;
    vf_dev->n_txq = data->nb_tx_queues;
    ret = hinic3_check_vf_upcall_queue(vf_dev);
    if (ret != 0) 
        return -EPERM;

    if (vf_dev->vport_id == HINIC3_PORT_ID_INVALID) {
        ret = hinic3_vf_dev_add(dev);
        if (ret != 0)
            return ret;

        ret = hinic3_vf_dev_set_ifindex(vf_dev, data);
        if (ret != 0) {
            HINIC3_LOG(ERR, VPORT, "hinic3_vf_dev_set_ifindex error");
            goto clean;
        }
    }

    ret = hinic3_vf_info_update(vf_dev);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "vf port info set update err, ret is %d", ret);
        goto clean;
    }

    return 0;
clean:
    hinic3_vf_dev_del(dev);
    return ret;
}

int
hinic3_vf_link_update(struct rte_eth_dev *dev, int wait_to_complete HINIC3_UNUSED)
{
    if (hinic3_is_ethdev_valid(dev) == false)
        return -EINVAL;

    int ret;
    uint32_t status = 0;
    bool is_ovs = 0;
    struct rte_eth_dev_data *data = NULL;
    struct hinic3_vf_dev *vf_dev = NULL;
    struct rte_eth_link hinic3_vf_dev_link = {
        .link_speed = RTE_ETH_SPEED_NUM_100G,
        .link_duplex = RTE_ETH_LINK_FULL_DUPLEX,
        .link_status = RTE_ETH_LINK_UP,
        .link_autoneg = RTE_ETH_LINK_FIXED,
    };

    is_ovs = hinic3_check_masked_to_exact_switch();
    data = dev->data;
    vf_dev = hinic3_ethdev_get_vf_private(dev);

    if (is_ovs == false) {
        ret = hinic3_port_mgmt_get_usage_state(vf_dev->vport_id, &status);
        if (ret != 0) {
            HINIC3_LOG(ERR, VPORT, "port mgmt get vf state err, ret is %d!", ret);
            return ret;
        }
    } else {
        status = HW_ETH_DEVICE_STATUS;
    }

    if ((status & HW_ETH_DEVICE_STATUS) != 0)
        hinic3_vf_dev_link.link_status = RTE_ETH_LINK_UP;
    else
        hinic3_vf_dev_link.link_status = RTE_ETH_LINK_DOWN;

    data->dev_link = hinic3_vf_dev_link;
    return 0;
}

int
hinic3_vf_dev_set_mtu(struct rte_eth_dev *dev, uint16_t mtu)
{
    if (hinic3_is_ethdev_valid(dev) == false)
        return -EINVAL;

    int ret;
    struct rte_eth_dev_data *data = dev->data;
    struct hinic3_vf_dev *vf_dev = hinic3_ethdev_get_vf_private(dev);
    uint16_t mtu_set = mtu;
    // testpmd attach端口下发有mtu则使用attach下发的mtu
    if (vf_dev->attach_mtu !=0)
        mtu_set = vf_dev->mtu;

    if (mtu_set > HINIC3_VF_MTU_MAX || mtu_set < HINIC3_VF_MTU_MIN) {
        HINIC3_LOG(ERR, VPORT, "the MTU value range is incorrect!");
        return -ERANGE;
    }

    if (vf_dev->vport_id == HINIC3_PORT_ID_INVALID) {
        HINIC3_LOG(ERR, VPORT, "invalid vport id!");
        return -EPERM;
    }

    ret = hinic3_mtu_set(vf_dev->vport_id, mtu_set);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "hinic3 vf set mtu failed, vport:%u!", vf_dev->vport_id);
        return ret;
    }

    vf_dev->mtu = mtu_set;
    vf_dev->attach_mtu = 0;
    data->mtu = mtu_set;

    return ret;
}

int
hinic3_vf_dev_flow_ops_get(struct rte_eth_dev *dev __rte_unused, const struct rte_flow_ops **ops)
{
    *ops = g_hinic3_vf_flow_ops;
    return 0;
}

int
hinic3_vf_dev_mac_addr_set(struct rte_eth_dev *dev, struct rte_ether_addr *mac_addr)
{
    if (hinic3_is_ethdev_valid(dev) == false)
        return -EINVAL;

    int ret = 0;
    struct hinic3_vf_dev *vf_dev = hinic3_ethdev_get_vf_private(dev);

    if (mac_addr == NULL) {
        HINIC3_LOG(ERR, VPORT, "the mac_addr is NULL!");
        return -EINVAL;
    }

    if (eth_addr_is_zero(*(struct eth_addr *)mac_addr) == true) {
        HINIC3_LOG(WARNING, VPORT, "mac_addr is zero, last config mac(" ETH_ADDR_FMT ")!",
            ETH_ADDR_ARGS(vf_dev->mac_addr));
        return ret;
    }

    memcpy(&vf_dev->mac_addr, (struct eth_addr *)mac_addr, sizeof(struct eth_addr));
    ret = hinic3_etheraddr_set(vf_dev->vport_id, vf_dev->mac_addr);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "failed to set mac for vf!");
        return -EPERM;
    }

    return 0;
}

static int
hinic3_virtual_queue_setup(struct hinic3_queue *rxq, uint16_t upcall_queue_id, uint16_t desc,
    unsigned int socket_id, struct rte_mempool *mp)
{
    int ret = 0;
    struct hinic3_virtual_queue *virtual_rxq = rxq->variant.virtqueue;
    if (virtual_rxq == NULL || virtual_rxq->physical_queue == NULL) {
        HINIC3_LOG(ERR, VPORT, "virtual_rxq is NULL!");
        return -ENOMEM;
    }
    struct hinic3_physical_queue *physical_rxq = virtual_rxq->physical_queue;

    virtual_rxq->ring = hinic3_ring_create(virtual_rxq->ring_name, desc, socket_id, 0, HINIC3_PORTS);
    if (virtual_rxq->ring == NULL) {
        HINIC3_LOG(ERR, VPORT, "virtual queue ring create failed!");
        return -ENOMEM;
    }
    virtual_rxq->queue_info = rxq;

    if (physical_rxq->virtual_queue_valid_cnt == 0) {
        ret = hinic3_port_mgmt_setup_upcall_queue(rxq->vport_id, rxq->queue_id, socket_id, mp);
        if (ret != 0) {
            hinic3_ring_free(virtual_rxq->ring, HINIC3_PORTS);
            virtual_rxq->ring = NULL;
            HINIC3_LOG(ERR, VPORT, "port mgmt setup upcall queue error!");
            return ret;
        }
        physical_rxq->hiovs_queue_id = upcall_queue_id;
        physical_rxq->mp = mp;
    }
    physical_rxq->is_virtual_queue_valids[virtual_rxq->virtual_queue_group_index] = true;
    physical_rxq->virtual_queue_valid_cnt += 1;

    HINIC3_LOG(INFO, VPORT,
        "virtual rxq %s setup success, vport id: %u, hiovs queue id: %u, physical rxq shared cnt: %u.",
        virtual_rxq->ring_name, rxq->vport_id,
        physical_rxq->hiovs_queue_id, physical_rxq->virtual_queue_valid_cnt);
    return 0;
}

static int
hinic3_vf_virtual_rx_queue_setup(struct rte_eth_dev *dev, uint16_t idx, uint16_t desc,
    unsigned int socket_id, const struct rte_eth_rxconf *conf __rte_unused, struct rte_mempool *mp)
{
    int ret = hinic3_vf_rx_queue_setup_check(dev, idx, desc, mp);
    if (ret != 0)
        return ret;

    struct rte_eth_dev_data *data = dev->data;
    struct hinic3_vf_dev *vf_dev = hinic3_ethdev_get_vf_private(dev);
    struct hinic3_queue *rxq = &vf_dev->upcall_queue.rx_queues[idx];
    uint16_t dpdk_index_id = 0;
    ret = hinic3_get_port_index_by_dev(dev, &dpdk_index_id);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "vf virtual rx queue setup, get port index by dev error!");
        return ret;
    }

    rxq->queue_id = idx;
    rxq->vport_id = vf_dev->vport_id;
    rxq->dpdk_port_id = vf_dev->dpdk_port_id;
    rxq->dpdk_index_id = dpdk_index_id;
    rxq->type = HINIC3_VIRTUAL_RX_QUEUE;
    rte_atomic64_set(&rxq->pkt_stats, 0);
    ret = hinic3_virtual_queue_setup(rxq, vf_dev->upcall_queue.upcall_queue_id[idx], desc, socket_id, mp);
    if (ret != 0) {
        return ret;
    }

    vf_dev->upcall_queue.is_queue_valid[idx] = true;
    data->rx_queues[idx] = rxq;

    return 0;
}

static void
hinic3_vf_virtual_rx_queue_release(struct rte_eth_dev *dev, uint16_t queue_id)
{
    if (hinic3_is_ethdev_valid(dev) == false)
        return;

    struct rte_eth_dev_data *data = dev->data;
    struct hinic3_vf_dev *vf_dev = hinic3_ethdev_get_vf_private(dev);
    struct hinic3_queue *rxq = NULL;
    struct hinic3_virtual_queue *virtual_rxq = NULL;
    struct hinic3_physical_queue *physical_rxq = NULL;
    struct rte_mbuf *pkt = NULL;

    if (queue_id >= data->nb_rx_queues) {
        HINIC3_LOG(ERR, VPORT, "vf virtual rx queue release, queue index invalid, max queue is %" PRIu16 "!",
            data->nb_rx_queues);
        return;
    }

    rxq = data->rx_queues[queue_id];
    if (rxq == NULL)
        return;
    virtual_rxq = rxq->variant.virtqueue;
    if (virtual_rxq == NULL)
        return;
    physical_rxq = virtual_rxq->physical_queue;
    if (physical_rxq == NULL || physical_rxq->virtual_queue_valid_cnt == 0)
        return;

    rte_spinlock_lock(&physical_rxq->physical_queue_lock);
    physical_rxq->is_virtual_queue_valids[virtual_rxq->virtual_queue_group_index] = false;
    physical_rxq->virtual_queue_valid_cnt -= 1;
    if (vf_dev->upcall_queue.is_queue_valid[queue_id] == true && physical_rxq->virtual_queue_valid_cnt == 0) {
        if (hinic3_port_mgmt_release_upcall_queue(vf_dev->vport_id, queue_id) != 0) {
            HINIC3_LOG(ERR, VPORT, "port mgmt release upcall queue error!");
        }
        physical_rxq->hiovs_queue_id = INVALID_UPCALL_QUEUE_ID;
        physical_rxq->mp = NULL;
    }
    /* 加锁防止软件队列环释放过程中被共享的端口分发入队 */
    if (virtual_rxq->ring != NULL) {
        while (rte_ring_count(virtual_rxq->ring) != 0)   {
            rte_ring_dequeue_burst(virtual_rxq->ring, (void **)&pkt, 1, NULL);
            rte_pktmbuf_free(pkt);
            pkt = NULL;
        }
        hinic3_ring_free(virtual_rxq->ring, HINIC3_PORTS);
        virtual_rxq->ring = NULL;
    }
    rte_spinlock_unlock(&physical_rxq->physical_queue_lock);

    vf_dev->upcall_queue.is_queue_valid[queue_id] = false;
    data->rx_queues[queue_id] = NULL;
}

int hinic3_vf_rx_queue_setup(struct rte_eth_dev *dev, uint16_t idx, uint16_t desc, unsigned int socket_id,
    const struct rte_eth_rxconf *conf __rte_unused, struct rte_mempool *mp)
{
    if (hinic3_get_virtual_queue_mode_enabled() == false)
        return hinic3_vf_standard_rx_queue_setup(dev, idx, desc, socket_id, conf, mp);
    else
        return hinic3_vf_virtual_rx_queue_setup(dev, idx, desc, socket_id, conf, mp);
}

void hinic3_vf_rx_queue_release(struct rte_eth_dev *dev, uint16_t queue_id)
{
    if (hinic3_get_virtual_queue_mode_enabled() == false)
        return hinic3_vf_standard_rx_queue_release(dev, queue_id);
    else
        return hinic3_vf_virtual_rx_queue_release(dev, queue_id);
}
