/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2025 Huawei Technologies Co., Ltd
 */

#include "rte_lcore.h"
#include "hinic3_util.h"
#include "hinic3_capture_main.h"
#include "hinic3_vf_controller.h"
#include "hinic3_offload_flow_port.h"
#include "hinic3_vxlan.h"
#include "hinic3_flow_dump.h"
#include "hinic3_tlv_key.h"
#include "hinic3_bond_detect.h"
#include "hinic3_bond_controller.h"

#define PRIORITY_UPCALL_MAX_NUM  2
#define HINIC3_BOND_MODE_ANY      0
#define ETH_XSTATS_NAME_SIZE     64
#define STATUS_NAME              4
#define STATUS_LEN (STATUS_NAME * 2)
#define SPEED_LEN                10
#define MODE_MAX_LEN             32
#define BOND_SLAVES_MAX_LEN      128
#define SYSFS_CLASS_NET_PATH     "/sys/class/net/"

static rte_atomic32_t g_bond_active = RTE_ATOMIC16_INIT(0);

static struct rte_flow_ops *g_hinic3_bond_flow_ops = NULL;

void
hinic3_bond_flow_ops_construct(void)
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
    g_hinic3_bond_flow_ops = flow_ops;
}

void
hinic3_bond_flow_ops_destroy(void)
{
    if (g_hinic3_bond_flow_ops == NULL)
        return;

    hinic3_free(g_hinic3_bond_flow_ops);
    g_hinic3_bond_flow_ops = NULL;
}

static int
hinic3_bond_update_numa_node(struct rte_eth_dev *eth_dev)
{
    struct rte_eth_dev_data *data = eth_dev->data;
    int numa_node = rte_socket_id();

    if (data->numa_node != numa_node) {
        HINIC3_LOG(DEBUG, VPORT, "update numa node %d to %d!", data->numa_node, numa_node);
        data->numa_node = numa_node;
    }

    return 0;
}

static int
hinic3_bond_uplink_pci_get(uint16_t bond_id, struct rte_pci_addr *uplink_pci)
{
    int ret = 0;
    struct smap bond_info = {0};
    const char *uplink_pci_addr = NULL;

    if (uplink_pci == NULL) {
        HINIC3_LOG(ERR, VPORT, "point of uplink_pci is NULL!");
        return -EINVAL;
    }

    if (bond_id == HINIC3_PORT_ID_INVALID) {
        HINIC3_LOG(ERR, VPORT, "invalid bond id!");
        return -EINVAL;
    }

    hinic3_smap_init(&bond_info);
    ret = hinic3_bond_mgmt_get(bond_id, &bond_info);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "failed to get bond info by id!");
        goto end;
    }

    uplink_pci_addr = hinic3_smap_get(&bond_info, HINIC3_BOND_UPLINK_PCI_ID);
    if (uplink_pci_addr == NULL) {
        HINIC3_LOG(ERR, VPORT, "failed to get uplink pci from bond info!");
        ret = -EPERM;
        goto end;
    }

    if (hinic3_support_bond_detect_get()) {
        ret = hinic3_init_bond_slave_info(&bond_info);
        if (ret != 0) {
            HINIC3_LOG(ERR, VPORT, "failed to init bond slave info!");
            ret = -EPERM;
            goto end;
        }
    }

    ret = rte_pci_addr_parse(uplink_pci_addr, uplink_pci);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "failed to parse uplink pci from str!");
        ret = -EPERM;
        goto end;
    }
end:
    hinic3_smap_destroy(&bond_info);
    return ret;
}

static int
hinic3_bond_add(struct rte_eth_dev *eth_dev)
{
    int ret = 0;
    struct hinic3_bond_dev *bond_dev = hinic3_ethdev_get_bond_private(eth_dev);

    if (bond_dev->bond_id != HINIC3_PORT_ID_INVALID) {
        HINIC3_LOG(ERR, VPORT, "bond already created!");
        return 0;
    }

    ret = hinic3_bond_mgmt_create(HINIC3_BOND_MODE_ANY, bond_dev->bond_name, &bond_dev->bond_id);
    if (ret != 0 || bond_dev->bond_id == HINIC3_PORT_ID_INVALID) {
        HINIC3_LOG(ERR, VPORT, "failed to create bond, return: %d!", ret);
        if (ret == 0)
            return -EPERM;

        return ret;
    }
    return 0;
}

static int
hinic3_bond_port_add(struct rte_eth_dev *eth_dev)
{
    int ret = 0;
    struct hinic3_bond_dev *bond_dev = hinic3_ethdev_get_bond_private(eth_dev);

    if (bond_dev->vport_id != HINIC3_PORT_ID_INVALID) {
        HINIC3_LOG(ERR, VPORT, "bond vport already created!");
        return 0;
    }

    ret = hinic3_bond_uplink_pci_get(bond_dev->bond_id, &bond_dev->uplink_pci_addr);
    if (ret != 0)
        return ret;

    ret = hinic3_bond_update_numa_node(eth_dev);
    if (ret != 0)
        return ret;

    bond_dev->vport_id = bond_dev->n_upcall_queue;
    ret = hinic3_port_mgmt_add(&bond_dev->vport_id, &bond_dev->uplink_pci_addr);
    if (ret != 0) {
        bond_dev->vport_id = HINIC3_PORT_ID_INVALID;
        HINIC3_LOG(ERR, VPORT, "failed to create vport, return: %d", ret);
        return ret;
    }

    return ret;
}

static int
hinic3_bond_dev_add(struct rte_eth_dev *dev)
{
    int ret = 0;
    uint32_t bond_alive = 0;
    struct hinic3_bond_dev *bond_dev = hinic3_ethdev_get_bond_private(dev);

    hinic3_list_init(&bond_dev->aged_flow_list.list_head);
    bond_alive = rte_atomic32_read(&g_bond_active);
    if (bond_alive != 0) {
        HINIC3_LOG(ERR, VPORT, "more than one bond is not supported by hinic3!");
        ret = -EINVAL;
        goto end;
    }

    ret = hinic3_bond_add(dev);
    if (ret != 0)
        goto end;

    ret = hinic3_bond_port_add(dev);
    if (ret != 0)
        goto clean_bond;

    ret = hinic3_eth_get_dpdk_port_id(bond_dev->vport_id, &bond_dev->dpdk_port_id);
    if (ret != 0)
        goto clean_port;

    HINIC3_LOG(INFO, VPORT, "hinic3 bond add success, port_id: %u, upcall_queue: %u",
        bond_dev->vport_id, bond_dev->n_upcall_queue);
    HINIC3_LOG(INFO, VPORT, "bond pci is %.4x:%.2x:%.2x.%x", bond_dev->uplink_pci_addr.domain,
        bond_dev->uplink_pci_addr.bus, bond_dev->uplink_pci_addr.devid, bond_dev->uplink_pci_addr.function);
    rte_atomic32_set(&g_bond_active, 1);
    return ret;

clean_port:
    hinic3_port_mgmt_del(bond_dev->vport_id);
    bond_dev->vport_id = HINIC3_PORT_ID_INVALID;

clean_bond:
    hinic3_bond_mgmt_delete(bond_dev->bond_id);
    bond_dev->bond_id = HINIC3_PORT_ID_INVALID;

end:
    return ret;
}

static void
hinic3_bond_dev_del(struct rte_eth_dev *dev)
{
    if (hinic3_is_ethdev_valid(dev) == false)
        return;

    struct rte_eth_dev_data *data = dev->data;
    struct hinic3_bond_dev *bond_dev = hinic3_ethdev_get_bond_private(dev);
    uint16_t nb_rx_queues = data->nb_rx_queues;
    uint16_t nb_tx_queues = data->nb_tx_queues;
    uint32_t bond_alive = 0;

    for (uint16_t index = 0; index < nb_rx_queues; index++)
        hinic3_bond_rx_queue_release(dev, index);

    for (uint16_t index = 0; index < nb_tx_queues; index++)
        hinic3_bond_tx_queue_release(dev, index);

    if (bond_dev->vport_id != HINIC3_PORT_ID_INVALID) {
        /* 端口删除前，清理抓包信息 */
        pcap_task_stop_as_eth_port_del(bond_dev->vport_id);
        hinic3_port_mgmt_del(bond_dev->vport_id);
        hinic3_ifindex_port_remove(bond_dev->vport_id);
        bond_dev->vport_id = HINIC3_PORT_ID_INVALID;
    }

    if (bond_dev->bond_id != HINIC3_PORT_ID_INVALID) {
        hinic3_bond_mgmt_delete(bond_dev->bond_id);
        bond_dev->bond_id = HINIC3_PORT_ID_INVALID;
        bond_alive = rte_atomic32_read(&g_bond_active);
        if (bond_alive == 1)
            rte_atomic32_set(&g_bond_active, 0);
    }

    if (bond_dev->dpdk_port_id != INVALID_DPDK_PORT_ID)
        bond_dev->dpdk_port_id = INVALID_DPDK_PORT_ID;
}

int
hinic3_bond_set_link_up(struct rte_eth_dev *dev)
{
    if (hinic3_is_ethdev_valid(dev) == false)
        return -EINVAL;

    struct rte_eth_dev_data *data = dev->data;
    struct rte_eth_link *dev_link = &data->dev_link;

    dev_link->link_status = RTE_ETH_LINK_UP;

    return 0;
}

int
hinic3_bond_set_link_down(struct rte_eth_dev *dev)
{
    if (hinic3_is_ethdev_valid(dev) == false)
        return -EINVAL;

    struct rte_eth_dev_data *data = dev->data;
    struct rte_eth_link *dev_link = &data->dev_link;

    dev_link->link_status = RTE_ETH_LINK_DOWN;

    return 0;
}

int
hinic3_bond_dev_start(struct rte_eth_dev *dev)
{
    if (hinic3_is_ethdev_valid(dev) == false)
        return -EINVAL;

    struct hinic3_bond_dev *bond_dev = hinic3_ethdev_get_bond_private(dev);
    struct rte_eth_dev_data *data = dev->data;
    struct rte_eth_link *dev_link = &data->dev_link;

    if (bond_dev->bond_id == HINIC3_PORT_ID_INVALID || bond_dev->vport_id == HINIC3_PORT_ID_INVALID)
        return -1;

    dev_link->link_status = RTE_ETH_LINK_UP;
    data->dev_started = HINIC3_DEV_STATE_START;

    return 0;
}

int
hinic3_bond_dev_stop(struct rte_eth_dev *dev)
{
    if (hinic3_is_ethdev_valid(dev) == false)
        return -EINVAL;

    struct rte_eth_dev_data *data = dev->data;
    struct rte_eth_link *dev_link = &data->dev_link;

    dev_link->link_status = RTE_ETH_LINK_DOWN;
    data->dev_started = HINIC3_DEV_STATE_STOP;

    return 0;
}

int
hinic3_bond_dev_close(struct rte_eth_dev *dev)
{
    if (hinic3_is_ethdev_valid(dev) == false)
        return -EINVAL;

    struct rte_eth_dev_data *data = dev->data;
    if (data->mac_addrs != NULL) {
        hinic3_rte_free(data->mac_addrs);
        data->mac_addrs = NULL;
    }
    hinic3_bond_dev_del(dev);
    return 0;
}

int
hinic3_bond_dev_infos_get(struct rte_eth_dev *dev, struct rte_eth_dev_info *info)
{
    if (hinic3_is_ethdev_valid(dev) == false)
        return -EINVAL;

    if (info == NULL) {
        HINIC3_LOG(ERR, VPORT, "eth dev info is NULL!");
        return -EINVAL;
    }

    int ret = 0;
    struct hinic3_port_capability cap = {0};

    struct hinic3_bond_dev *bond_dev = hinic3_ethdev_get_bond_private(dev);
    struct rte_device *device = dev->device;
    const struct rte_driver *driver = device->driver;

    info->driver_name = driver->name;
    ret = hinic3_port_mgmt_get_capability(&cap);
    if (ret != 0)
        return ret;

    if (cap.supported_upcall_qnum != MAX_RX_QUEUE_PER_VPORT)
        info->max_rx_queues = HINIC3_BOND_DEFAULT_NB_QUEUES;
    else
        info->max_rx_queues = MAX_RX_QUEUE_PER_VPORT;

    info->min_mtu = HINIC3_BOND_MTU_MIN;
    info->max_mtu = HINIC3_BOND_MTU_MAX;
    info->max_rx_pktlen = HINIC3_BOND_MTU_MAX + HINIC3_BOND_SIZE_OFFSET;
    info->max_tx_queues = MAX_RX_QUEUE_PER_VPORT;
    info->if_index = bond_dev->vport_id;
    if (hinic3_card_mod_get() != PROG_MODE) {
        info->tx_offload_capa = HINIC3_ETH_TX_OFFLOAD_CAPA;
    }
    return 0;
}

static int
hinic3_bond_rx_queue_setup_check(struct rte_eth_dev *dev, uint16_t idx, uint16_t desc, struct rte_mempool *mp)
{
    if (hinic3_is_ethdev_valid(dev) == false)
        return -EINVAL;

    if (mp == NULL) {
        HINIC3_LOG(ERR, VPORT, "mp is NULL!");
        return -EINVAL;
    }

    uint16_t bond_rx_depth = hinic3_bond_rx_depth_get();
    struct rte_eth_dev_data *data = dev->data;
    bool is_open_ovs = hinic3_check_masked_to_exact_switch();
    if (bond_rx_depth != desc && !is_open_ovs) {
        HINIC3_LOG(ERR, VPORT, "bond_rx_depth differences. desc is %" PRIu16 ", bond_rx_depth is %" PRIu16 "!",
            desc, bond_rx_depth);
        return -EINVAL;
    }

    if (idx >= MAX_RX_QUEUE_PER_VPORT) {
        HINIC3_LOG(ERR, VPORT, "bond upcall index(%" PRIu16 ") greater than max(%" PRIu16 ")!",
            idx, MAX_RX_QUEUE_PER_VPORT);
        return -EINVAL;
    }

    if (idx >= data->nb_rx_queues) {
        HINIC3_LOG(ERR, VPORT, "bond rx queue setup check, queue index invalid, max queue is %" PRIu16 "!",
            data->nb_rx_queues);
        return -EINVAL;
    }

    return 0;
}

static int
hinic3_setup_standard_queue(struct hinic3_queue *rxq, uint16_t upcall_queue_id, unsigned int socket_id,
    struct rte_mempool *mp)
{
    struct hinic3_standard_queue *stdqueue =
        hinic3_rte_zmalloc_socket(HINIC3_PORTS, sizeof(struct hinic3_standard_queue), CACHE_LINE_SIZE, socket_id);
    if (stdqueue == NULL) {
        HINIC3_LOG(ERR, VPORT, "alloc bond queue rx%" PRIu16 " failed, no enough memory!", rxq->queue_id);
        return -ENOMEM;
    }

    if (hinic3_port_mgmt_setup_upcall_queue(rxq->vport_id, rxq->queue_id, socket_id, mp) != 0) {
        HINIC3_LOG(ERR, VPORT, "setup standard queue, port setup upcall queue error!");
        hinic3_rte_free(stdqueue);
        return -EPERM;
    }

    stdqueue->queue_info = rxq;
    stdqueue->hiovs_queue_id = upcall_queue_id;
    stdqueue->mp = mp;
    rxq->variant.stdqueue = stdqueue;

    return 0;
}

int
hinic3_bond_rx_queue_setup(struct rte_eth_dev *dev, uint16_t idx, uint16_t desc,
            unsigned int socket_id, const struct rte_eth_rxconf *conf __rte_unused,
            struct rte_mempool *mp)
{
    int ret = hinic3_bond_rx_queue_setup_check(dev, idx, desc, mp);
    if (ret != 0)
        return ret;

    struct rte_eth_dev_data *data = dev->data;
    struct hinic3_bond_dev *bond_dev = hinic3_ethdev_get_bond_private(dev);
    struct hinic3_queue *rxq = &bond_dev->upcall_queue.rx_queues[idx];
    uint16_t dpdk_index_id = 0;
    ret = hinic3_get_port_index_by_dev(dev, &dpdk_index_id);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "bond rx queue setup, get port index by dev error!");
        return -EPERM;
    }

    rxq->queue_id = idx;
    rxq->vport_id = bond_dev->vport_id;
    rxq->dpdk_port_id = bond_dev->dpdk_port_id;
    rxq->dpdk_index_id = dpdk_index_id;
    rxq->type = HINIC3_STANDARD_RX_QUEUE;
    rte_atomic64_set(&rxq->pkt_stats, 0);
    ret = hinic3_setup_standard_queue(rxq, bond_dev->upcall_queue.upcall_queue_id[idx], socket_id, mp);
    if (ret != 0)
        return ret;

    bond_dev->upcall_queue.is_queue_valid[idx] = true;
    data->rx_queues[idx] = rxq;
    HINIC3_LOG(INFO, VPORT,
        "bond queue rx%" PRIu16 " setup success, vport id: %" PRIu16 ", hiovs queue id: %" PRIu16 ".",
        rxq->queue_id, rxq->vport_id, rxq->variant.stdqueue->hiovs_queue_id);
    return 0;
}

void
hinic3_bond_rx_queue_release(struct rte_eth_dev *dev, uint16_t queue_id)
{
    if (hinic3_is_ethdev_valid(dev) == false)
        return;

    struct rte_eth_dev_data *data = dev->data;
    struct hinic3_bond_dev *bond_dev = hinic3_ethdev_get_bond_private(dev);
    struct hinic3_queue *rxq = NULL;

    if (queue_id >= data->nb_rx_queues) {
        HINIC3_LOG(ERR, VPORT, "queue index invalid, max queue is:%" PRIu16 "!",
            data->nb_rx_queues);
        return;
    }

    rxq = data->rx_queues[queue_id];
    if (rxq != NULL) {
        if (bond_dev->upcall_queue.is_queue_valid[queue_id] == true) {
            if (hinic3_port_mgmt_release_upcall_queue(bond_dev->vport_id, queue_id) != 0)
                HINIC3_LOG(ERR, VPORT, "bond rx queue release, release upcall queue error!");

            hinic3_rte_free(rxq->variant.stdqueue);
            hinic3_reset_queue_info(rxq);
            bond_dev->upcall_queue.is_queue_valid[queue_id] = false;
        }
        data->rx_queues[queue_id] = NULL;
    }
}

static int
hinic3_bond_tx_queue_setup_check(struct rte_eth_dev *dev, uint16_t idx, uint16_t desc)
{
    if (hinic3_is_ethdev_valid(dev) == false)
        return -EINVAL;

    uint16_t bond_tx_depth = hinic3_bond_tx_depth_get();
    struct rte_eth_dev_data *data = dev->data;
    bool is_open_ovs = hinic3_check_masked_to_exact_switch();
    if (bond_tx_depth != desc && !is_open_ovs) {
        HINIC3_LOG(ERR, VPORT, "bond_tx_depth differences. desc is %" PRIu16 ", bond_tx_depth is %" PRIu16 "!",
            desc, bond_tx_depth);
        return -EINVAL;
    }

    if (idx >= MAX_TX_QUEUE_PER_VPORT) {
        HINIC3_LOG(ERR, VPORT, "bond reinject index(%" PRIu16 ") greater than max(%" PRIu16 ")!",
            idx, MAX_TX_QUEUE_PER_VPORT);
        return -EINVAL;
    }

    if (idx >= data->nb_tx_queues) {
        HINIC3_LOG(ERR, VPORT, "bond tx queue setup check, queue index invalid, max queue is %" PRIu16 "!",
            data->nb_tx_queues);
        return -EINVAL;
    }

    return 0;
}

int
hinic3_bond_tx_queue_setup(struct rte_eth_dev *dev, uint16_t idx, uint16_t desc,
            unsigned int socket_id __rte_unused, const struct rte_eth_txconf *conf __rte_unused)
{
    int ret = hinic3_bond_tx_queue_setup_check(dev, idx, desc);
    if (ret != 0)
        return ret;

    struct rte_eth_dev_data *data = dev->data;
    struct hinic3_bond_dev *bond_dev = hinic3_ethdev_get_bond_private(dev);
    struct hinic3_queue *txq = &bond_dev->reinject_queue.tx_queues[idx];
    uint16_t dpdk_index_id = 0;
    ret = hinic3_get_port_index_by_dev(dev, &dpdk_index_id);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "bond tx queue setup, get port index by dev error!");
        return -EPERM;
    }

    txq->queue_id = idx;
    txq->vport_id = bond_dev->vport_id;
    txq->dpdk_port_id = bond_dev->dpdk_port_id;
    txq->dpdk_index_id = dpdk_index_id;
    txq->type = HINIC3_TX_QUEUE;
    rte_atomic64_set(&txq->pkt_stats, 0);

    bond_dev->reinject_queue.is_queue_valid[idx] = true;
    data->tx_queues[idx]= txq;
    HINIC3_LOG(INFO, VPORT, "bond queue tx%" PRIu16 " setup success, vport id: %" PRIu16 ".",
        txq->queue_id, txq->vport_id);
    return 0;
}

void
hinic3_bond_tx_queue_release(struct rte_eth_dev *dev, uint16_t queue_id)
{
    if (hinic3_is_ethdev_valid(dev) == false)
        return;

    struct rte_eth_dev_data *data = dev->data;
    struct hinic3_bond_dev *bond_dev = hinic3_ethdev_get_bond_private(dev);
    struct hinic3_queue *txq = NULL;

    if (queue_id >= data->nb_tx_queues) {
        HINIC3_LOG(ERR, VPORT, "bond tx queue release, queue index invalid, max queue is %" PRIu16 "!",
            data->nb_tx_queues);
        return;
    }

    txq = data->tx_queues[queue_id];
    if (txq != NULL) {
        if (bond_dev->reinject_queue.is_queue_valid[queue_id] == true) {
            hinic3_reset_queue_info(txq);
            bond_dev->reinject_queue.is_queue_valid[queue_id] = false;
        }
        data->tx_queues[queue_id] = NULL;
    }
}

static void
hinic3_construct_priority_upcall_cfgs(struct hinic3_high_priority_cfgs *cfgs)
{
    cfgs->cfgs_len = PRIORITY_UPCALL_MAX_NUM;

    cfgs->hovs_cfgs[0].protocol_l2 = ETH_TYPE_IPV6;
    cfgs->hovs_cfgs[0].protocol_l3 = HINIC3_NEXTHDR_ICMP;
    cfgs->hovs_cfgs[0].reserve0 = 0;
    cfgs->hovs_cfgs[0].reserve1 = 0;

    cfgs->hovs_cfgs[1].protocol_l2 = ETH_TYPE_ARP;
    cfgs->hovs_cfgs[1].protocol_l3 = 0;
    cfgs->hovs_cfgs[1].reserve0 = 0;
    cfgs->hovs_cfgs[1].reserve1 = 0;
}

static int
hinic3_bond_dev_configure_sub(struct hinic3_bond_dev *bond_dev, struct rte_eth_dev *dev,
    struct rte_eth_dev_data *data)
{
    int ret = hinic3_bond_dev_add(dev);
    if (ret != 0)
        return -EPERM;

    if (bond_dev->priority_upcall == true) {
        struct hinic3_high_priority_cfgs cfgs = {0};
        hinic3_construct_priority_upcall_cfgs(&cfgs);
        ret = hinic3_iface_high_priority_upcall_set(bond_dev->vport_id, &cfgs);
        if (ret != 0) {
            hinic3_bond_dev_del(dev);
            return -EPERM;
        }
    }

    ret = hinic3_eth_get_upcall_queue_map(bond_dev->vport_id, bond_dev->n_upcall_queue,
        bond_dev->upcall_queue.upcall_queue_id);
    if (ret != 0) {
        hinic3_bond_dev_del(dev);
        return -EPERM;
    }

    bond_dev->port_ifindex = data->port_id;
    ret = hinic3_set_port_map_by_ifindex(bond_dev->vport_id, bond_dev->port_ifindex);
    if (ret != 0) {
        hinic3_bond_dev_del(dev);
        return -EPERM;
    }
    return 0;
}

int
hinic3_bond_dev_configure(struct rte_eth_dev *dev)
{
    if (hinic3_is_ethdev_valid(dev) == false)
        return -EINVAL;

    int ret = 0;
    struct hinic3_bond_dev *bond_dev = hinic3_ethdev_get_bond_private(dev);
    struct rte_eth_dev_data *data = dev->data;

    if (data->dev_started != HINIC3_DEV_STATE_STOP) {
        HINIC3_LOG(ERR, VPORT, "the device must be stopped when the port is configured!");
        return -EPERM;
    }

    // rebuild the port only when the rx queue is changed.
    if (data->nb_rx_queues != bond_dev->n_upcall_queue) {
        if (data->nb_rx_queues == 0) {
            HINIC3_LOG(ERR, VPORT, "number of rx queue can't be zero!");
            return -EPERM;
        }
        hinic3_bond_dev_del(dev);
        bond_dev->n_upcall_queue = data->nb_rx_queues;
    }

    bond_dev->n_txq = data->nb_tx_queues;
    // create hwbond
    if (bond_dev->bond_id == HINIC3_PORT_ID_INVALID && bond_dev->vport_id == HINIC3_PORT_ID_INVALID) {
        ret = hinic3_bond_dev_configure_sub(bond_dev, dev, data);
        if (ret != 0) {
            HINIC3_LOG(ERR, VPORT, "pf dev configure failed!");
            return ret;
        }
    }

    if (data->mtu != bond_dev->mtu) {
        ret = hinic3_bond_dev_set_mtu(dev, bond_dev->mtu);
        if (ret != 0) {
            HINIC3_LOG(ERR, VPORT, "failed to set dev mtu to %u", bond_dev->mtu);
            hinic3_bond_dev_del(dev);
            return -EPERM;
        }
    }

    return 0;
}

static int
hinic3_read_file(const char *path, char *file_data, int date_len)
{
    int ret = 0;
    int ch = 0;
    int i = 0;
    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        HINIC3_LOG(ERR, VPORT, "Can not open file.");
        return -EPERM;
    }

    for (i = 0; i < date_len - 1; i++) {
        ch = fgetc(fp);
        if ((ch != '\n') && (ch != EOF))
            file_data[i] = ch;
        else
            break;
    }

    file_data[i] = '\0';
    ret = fclose(fp);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "Can not close file.");
        return -EPERM;
    }
    return 0;
}

int
hinic3_get_bond_link_status(const char *bond_name, uint32_t *status)
{
    int ret = 0;
    char bond_status[STATUS_LEN] = {0};
    char bond_status_path[PATH_MAX] = {0};
    char relative_path[PATH_MAX] = {0};

    if (bond_name == NULL)
        return -EINVAL;

    ret = snprintf(relative_path, PATH_MAX, "%s%s%s", SYSFS_CLASS_NET_PATH, bond_name, "/bonding/mii_status");
    if (ret < 0 || ret >= PATH_MAX) {
        HINIC3_LOG(ERR, VPORT, "bond status file name error");
        return -EPERM;
    }

    if (realpath(relative_path, bond_status_path) == NULL) {
        HINIC3_LOG(ERR, VPORT, "file not exist!");
        return -1;
    }

    ret = hinic3_read_file(bond_status_path, bond_status, STATUS_LEN);
    if (ret != 0)
        return -EPERM;

    if (strcmp(bond_status, "up") == 0)
        *status = RTE_ETH_LINK_UP;
    else if (strcmp(bond_status, "down") == 0)
        *status = RTE_ETH_LINK_DOWN;
    else {
        HINIC3_LOG(ERR, VPORT, "Failed to obtain the bond status");
        return -EPERM;
    }
    return 0;
}

static int
hinic3_set_bond_link_speed(uint32_t *speed, const char *bond_name)
{
    int ret = 0;
    char bond_speed[SPEED_LEN] = {0};
    char bond_speed_path[PATH_MAX] = {0};
    char slaves[BOND_SLAVES_MAX_LEN] = {0};
    char slaves_path[PATH_MAX] = {0};
    char slaves_relative_path[PATH_MAX] = {0};
    char delim[] = " ";
    char *slave = NULL;
    char *saveptr = NULL;
    char *end_ptr = NULL;
    ret = snprintf(slaves_relative_path, PATH_MAX, "%s%s%s", SYSFS_CLASS_NET_PATH, bond_name, "/bonding/slaves");
    if (ret < 0 || ret >= PATH_MAX)
        return -EPERM;

    if (realpath(slaves_relative_path, slaves_path) == NULL) {
        HINIC3_LOG(ERR, VPORT, "file not exist!");
        return -1;
    }

    ret = hinic3_read_file(slaves_path, slaves, BOND_SLAVES_MAX_LEN);
    if (ret != 0)
        return -EPERM;

    slave = strtok_r(slaves, delim, &saveptr);
    while (slave != NULL) {
        ret = snprintf(bond_speed_path, PATH_MAX, "%s%s%s%s%s", SYSFS_CLASS_NET_PATH,
            bond_name, "/lower_", slave, "/speed");
        if (ret < 0 || ret >= PATH_MAX)
            return -EPERM;

        ret = hinic3_read_file(bond_speed_path, bond_speed, SPEED_LEN);
        if (ret != 0)
            return -EPERM;

        uint32_t slave_speed = strtoul(bond_speed, &end_ptr, HWPT_DEC_BASE);
        if (end_ptr == NULL || *end_ptr != '\0') {
            HINIC3_LOG(ERR, VPORT, "Speed outof range. speed is %u!", slave_speed);
            return -EPERM;
        }
        *speed += slave_speed;
        slave = strtok_r(NULL, delim, &saveptr);
    }
    return 0;
}

static int
hinic3_set_active_backup_speed(uint32_t *speed, const char *bond_name)
{
    int ret = 0;
    char active_slave[BOND_SLAVES_MAX_LEN] = {0};
    char active_slave_path[PATH_MAX] = {0};
    char slaves_relative_path[PATH_MAX] = {0};
    char active_slave_speed_path[PATH_MAX] = {0};
    char active_slave_speed[SPEED_LEN] = {0};
    char *end_ptr = NULL;
    ret = snprintf(slaves_relative_path, PATH_MAX, "%s%s%s", SYSFS_CLASS_NET_PATH, bond_name, "/bonding/active_slave");
    if (ret < 0 || ret >= PATH_MAX)
        return -EPERM;

    if (realpath(slaves_relative_path, active_slave_path) == NULL) {
        HINIC3_LOG(ERR, VPORT, "file not exist!");
        return -1;
    }

    ret = hinic3_read_file(active_slave_path, active_slave, BOND_SLAVES_MAX_LEN);
    if (ret != 0)
        return -EPERM;

    ret = snprintf(active_slave_speed_path, PATH_MAX, "%s%s%s%s%s", SYSFS_CLASS_NET_PATH,
        bond_name, "/lower_", active_slave, "/speed");
    if (ret < 0 || ret >= PATH_MAX)
        return -EPERM;

    ret = hinic3_read_file(active_slave_speed_path, active_slave_speed, SPEED_LEN);
    if (ret != 0)
        return -EPERM;

    *speed = strtoul(active_slave_speed, &end_ptr, HWPT_DEC_BASE);
    if (end_ptr == NULL || *end_ptr != '\0') {
        HINIC3_LOG(ERR, VPORT, "Speed outof range. speed is %u!", *speed);
        *speed = 0;
        return -EPERM;
    }
    return 0;
}

static int
hinic3_get_bond_link_speed(const struct hinic3_bond_dev *bond_dev, uint32_t *speed)
{
    int ret = 0;
    char bond_mode[MODE_MAX_LEN] = {0};
    char bond_mode_path[PATH_MAX] = {0};
    char relative_path[PATH_MAX] = {0};

    ret = snprintf(relative_path, PATH_MAX, "%s%s%s", SYSFS_CLASS_NET_PATH, bond_dev->bond_name, "/bonding/mode");
    if (ret < 0 || ret >= PATH_MAX)
        return -EPERM;

    if (realpath(relative_path, bond_mode_path) == NULL) {
        HINIC3_LOG(ERR, VPORT, "file not exist!");
        return -1;
    }

    ret = hinic3_read_file(bond_mode_path, bond_mode, MODE_MAX_LEN);
    if (ret != 0)
        return -EPERM;

    if (strcmp(bond_mode, "balance-rr 0") == 0 || strcmp(bond_mode, "802.3ad 4") == 0 ||
        strcmp(bond_mode, "balance-xor 2") == 0)
        ret = hinic3_set_bond_link_speed(speed, bond_dev->bond_name);
    else if (strcmp(bond_mode, "active-backup 1") == 0)
        ret = hinic3_set_active_backup_speed(speed, bond_dev->bond_name);
    else {
        HINIC3_LOG(WARNING, VPORT, "Get bond link speed failed, unknow bond mode!");
        *speed = 0;
    }
    return ret;
}

int
hinic3_bond_link_update(struct rte_eth_dev *dev, int wait_to_complete HINIC3_UNUSED)
{
    if (hinic3_is_ethdev_valid(dev) == false)
        return -EINVAL;

    int ret = 0;
    uint32_t status = 0;
    uint32_t speed = 0;
    struct rte_eth_link bond_dev_link = {
        .link_speed = RTE_ETH_SPEED_NUM_NONE,
        .link_duplex = RTE_ETH_LINK_FULL_DUPLEX,
        .link_status = RTE_ETH_LINK_DOWN,
        .link_autoneg = RTE_ETH_LINK_FIXED,
    };

    const struct hinic3_bond_dev *bond_dev = hinic3_ethdev_get_bond_private(dev);
    struct rte_eth_dev_data *data = dev->data;

    ret = hinic3_get_bond_link_status(bond_dev->bond_name, &status);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "Failed to obtain the link status");
        return ret;
    }

    bond_dev_link.link_status = status;
    if (status == RTE_ETH_LINK_DOWN) {
        data->dev_link = bond_dev_link;
        return 0;
    }

    ret = hinic3_get_bond_link_speed(bond_dev, &speed);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "Failed to obtain the bond speed");
        data->dev_link = bond_dev_link;
        return ret;
    }

    bond_dev_link.link_speed = speed;
    data->dev_link = bond_dev_link;
    return 0;
}

int
hinic3_bond_dev_set_mtu(struct rte_eth_dev *dev, uint16_t mtu)
{
    if (hinic3_is_ethdev_valid(dev) == false)
        return -EINVAL;

    int ret = 0;
    struct hinic3_bond_dev *bond_dev = hinic3_ethdev_get_bond_private(dev);
    struct rte_eth_dev_data *data = dev->data;

    if (bond_dev->mtu == mtu)
        return 0;

    if (mtu > HINIC3_BOND_MTU_MAX || mtu < HINIC3_BOND_MTU_MIN) {
        HINIC3_LOG(ERR, VPORT, "the MTU value range is incorrect!");
        return -ERANGE;
    }

    if (bond_dev->vport_id == HINIC3_PORT_ID_INVALID) {
        HINIC3_LOG(ERR, VPORT, "invalid vport id!");
        return -EPERM;
    }

    ret = hinic3_port_upcall_mtu_set(OVS_VPORT_TYPE_BOND, mtu);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "hinic3 pf set mtu failed, vport:" PRIu16 "!", bond_dev->vport_id);
        return ret;
    }

    bond_dev->mtu = mtu;
    data->mtu = mtu;

    return ret;
}

int
hinic3_bond_dev_flow_ops_get(struct rte_eth_dev *dev __rte_unused, const struct rte_flow_ops **ops)
{
    *ops = g_hinic3_bond_flow_ops;
    return 0;
}
