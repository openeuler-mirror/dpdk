/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2022 Huawei Technologies Co., Ltd
 */

#include "ethdev_driver.h"
#include "hinic3_queue.h"
#include "hinic3_flow_agent_enum.h"
#include "hinic3_port_util.h"


static const struct hinic3_port_stats_name_off {
    const char name[ETH_XSTATS_NAME_SIZE];
} g_hinic3_port_stats_strings[] = {
    {"upcall_tx_pkt_num"},  // 网卡往upcall队列发包给vswitch，发成功的数目
    {"upcall_rx_pkt_num"},  // 网卡从reinject队列发包收到包数
    {"upcall_tx_drop_num"}, // 网卡往upcall队列发包给vswitch，丢包数
};

bool
hinic3_is_ethdev_valid(struct rte_eth_dev *dev)
{
    if (dev == NULL) {
        HINIC3_LOG(ERR, VPORT, "rte eth dev is NULL!");
        return false;
    }

    if (dev->data == NULL) {
        HINIC3_LOG(ERR, VPORT, "rte eth dev data is NULL!");
        return false;
    }

    if (dev->data->dev_private == NULL) {
        HINIC3_LOG(ERR, VPORT, "rte eth dev data private is NULL!");
        return false;
    }

    return true;
}

void *
hinic3_get_rte_eth_dev(uint16_t port_id)
{
    struct rte_eth_dev *dev;
    RTE_ETH_VALID_PORTID_OR_ERR_RET(port_id, NULL);
    dev = &rte_eth_devices[port_id];
    return dev;
}

void *
hinic3_get_private_data(uint16_t port_id)
{
    struct rte_eth_dev *dev;
    RTE_ETH_VALID_PORTID_OR_ERR_RET(port_id, NULL);
    dev = &rte_eth_devices[port_id];
    return dev->data->dev_private;
}

int
hinic3_get_port_ifindex(uint16_t src_port, uint32_t *ifindex)
{
    struct rte_eth_dev_info info = {0};
    int ret;

    ret = rte_eth_dev_info_get(src_port, &info);
    if (ret != 0) {
        return -1;
    }
    *ifindex = info.if_index;

    return 0;
}

int
hinic3_get_id_from_dev(struct rte_eth_dev *dev, uint16_t *port_id)
{
    struct rte_device *device = dev->device;
    if (device == NULL) {
		return -EINVAL;
    }
    const char *name = device->name;

    return rte_eth_dev_get_port_by_name(name, port_id);
}

int
hinic3_port_xstats_get_names(struct rte_eth_dev *dev __rte_unused,
                    struct rte_eth_xstat_name *xstats_names, unsigned int limit)
{
    int count = 0;
    if (xstats_names == NULL)
        return HW_NB_XSTATS;

    if (limit < HW_NB_XSTATS) {
        HINIC3_LOG(ERR, VPORT, "The maximum number is less than the expected number!");
        return -1;
    }

    for (int i = 0; i < HW_NB_XSTATS; i++) {
        strcpy(xstats_names[count].name, g_hinic3_port_stats_strings[i].name);
        count++;
    }
    return count;
}

int
hinic3_get_netdev_name(const char *port_name, char *netdev_name)
{
    int ret;

    if (port_name == NULL || netdev_name == NULL)
        return -1;

    ret = snprintf(netdev_name, HINIC3_NETDEV_NAME_MAX_LENGTH, "%s%s",
        HINIC3_ETH_VDEV_DRV_NAME, port_name);
    if (ret <= 0) {
        HINIC3_LOG(ERR, AGENT, "The port name is misspelled.");
        return -1;
    }

    return 0;
}

static int
hinic3_upcall_queue_parse(const char *queues, uint16_t *upcall_queues, int *n_ques)
{
    int i = 0;
    int cnt = 0;
    const int decimal = 10;
    char *arg = NULL;
    char *next = NULL;
    char *qid = NULL;

    if (queues == NULL || upcall_queues == NULL) {
        HINIC3_LOG(ERR, VPORT, "Invalid que_ids or upcall_queues!");
        return -EINVAL;
    }

    arg = hinic3_strdup(queues, HINIC3_DRIVER_ADAPTER);
    if (arg == NULL) {
        HINIC3_LOG(ERR, VPORT, "Alloc memory error!");
        return -ENOMEM;
    }

    next = arg;
    for (i = 0; i < MAX_RX_QUEUE_PER_VPORT; i++) {
        qid = strsep(&next, ",");
        if (qid == NULL) {
            break;
        }
        char *endPtr = NULL;
        upcall_queues[i] = (uint32_t)strtoul(qid, &endPtr, decimal);
        if (endPtr == NULL || *endPtr != '\0') {
            hinic3_free(arg);
            return -EINVAL;
        }
        ++cnt;
    }

    *n_ques = cnt;
    hinic3_free(arg);
    return 0;
}

int
hinic3_eth_get_upcall_queue_map(uint16_t vport_id, uint8_t n_upcall_queue, uint16_t *upcall_queue_id)
{
    int ret = 0;
    int n_ques = 0;
    struct smap smap_args = {0};
    const char *upcall_queues_str = NULL;

    if (vport_id == HINIC3_PORT_ID_INVALID) {
        HINIC3_LOG(ERR, VPORT, "invalid vport id!");
        return -EINVAL;
    }

    if (upcall_queue_id == NULL) {
        HINIC3_LOG(ERR, VPORT, "pointer of upcall queue id is NULL!");
        return -EINVAL;
    }

    hinic3_smap_init(&smap_args);
    ret = hinic3_port_mgmt_get(vport_id, &smap_args);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "failed to get port mgmt info!");
        goto err;
    }
    upcall_queues_str = hinic3_smap_get(&smap_args, HINIC3_PORT_UPCALL_QUEUE_MAP);
    if (upcall_queues_str != NULL) {
        ret = hinic3_upcall_queue_parse(upcall_queues_str, upcall_queue_id, &n_ques);
        if (ret != 0 || n_upcall_queue > n_ques) {
            HINIC3_LOG(ERR, VPORT, "rxq:%u is greater than max queues: %d!", n_upcall_queue, n_ques);
            ret = -EINVAL;
        }
    } else {
        ret = -ENOENT;
    }

err:
    hinic3_smap_destroy(&smap_args);
    return ret;
}


int
hinic3_eth_get_dpdk_port_id(uint16_t vport_id, uint16_t *dpdk_port_id)
{
    int ret = 0;
    struct smap smap_args = {0};
    const char *dpdk_port_id_str = NULL;
    uintmax_t dpdk_port_id_ul = 0;

    if (vport_id == HINIC3_PORT_ID_INVALID) {
        HINIC3_LOG(ERR, VPORT, "invalid vport id!");
        return -EINVAL;
    }

    if (dpdk_port_id == NULL) {
        HINIC3_LOG(ERR, VPORT, "point of dpdk port id is NULL!");
        return -EINVAL;
    }

    hinic3_smap_init(&smap_args);
    ret = hinic3_port_mgmt_get(vport_id, &smap_args);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "failed to get port mgmt info!");
        goto err;
    }

    dpdk_port_id_str = hinic3_smap_get(&smap_args, HINIC3_DPDK_PORT_ID);
    if (dpdk_port_id_str == NULL) {
        HINIC3_LOG(ERR, VPORT, "failed to get dpdk port id!");
        ret = -ENOENT;
        goto err;
    }

    dpdk_port_id_ul = strtoul(dpdk_port_id_str, NULL, STR_TO_DEC_NUM);
    if (dpdk_port_id_ul > UINT8_MAX) {
        HINIC3_LOG(ERR, VPORT, "dpdk port id out of range!");
        ret = -ERANGE;
        goto err;
    }
    *dpdk_port_id = dpdk_port_id_ul;

err:
    hinic3_smap_destroy(&smap_args);
    return ret;
}

int
hinic3_get_port_index_by_dev(struct rte_eth_dev *dev, uint16_t *port_index)
{
    struct rte_device *device = dev->device;
    const char *name = device->name;
    int ret = rte_eth_dev_get_port_by_name(name, port_index);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "rte_eth_dev_get_port_by_name error!");
        return -1;
    }
    return 0;
}

int hinic3_port_get_tx_queue_count(uint16_t port_id, uint16_t queue_id)
{
    struct rte_eth_dev *dev = hinic3_get_rte_eth_dev(port_id);
    struct rte_eth_dev_data *data = NULL;
    uint16_t nb_tx_queues = 0;
    struct hinic3_queue *txq = NULL;
    int backlog = 0;

    if (hinic3_is_ethdev_valid(dev) == false) {
        return -EINVAL;
    }

    data = dev->data;
    nb_tx_queues = data->nb_tx_queues;
    if (queue_id >= nb_tx_queues) {
        HINIC3_LOG(ERR, VPORT, "port get tx queue count, queue index invalid, max queue is:%" PRIu16 ".", nb_tx_queues);
        return -EINVAL;
    }

    txq = data->tx_queues[queue_id];
    if (txq == NULL) {
        HINIC3_LOG(ERR, VPORT, "port get tx queue count, queue index %u invalid.", queue_id);
        return -EINVAL;
    }
    backlog = (int)hinic3_get_tx_queue_count(txq->dpdk_port_id, txq->queue_id);
    if (backlog < 0) {
        HINIC3_LOG(ERR, VPORT, "txq%" PRIu16 " get reinject backlog failed ret: %d, hiovs queue id: %" PRIu16 ".",
            txq->queue_id, backlog, txq->queue_id);
        return -EPERM;
    }

    return backlog;
}
 
int hinic3_port_get_rx_queue_count(uint16_t port_id, uint16_t queue_id)
{
    struct rte_eth_dev *dev = hinic3_get_rte_eth_dev(port_id);
    struct rte_eth_dev_data *data = NULL;
    uint16_t nb_rx_queues = 0;
    struct hinic3_queue *rxq = NULL;
    int backlog = 0;
 
    if (hinic3_is_ethdev_valid(dev) == false) {
        return -EINVAL;
    }

    data = dev->data;
    nb_rx_queues = data->nb_rx_queues;
    if (queue_id >= nb_rx_queues) {
        HINIC3_LOG(ERR, VPORT, "port get rx queue count, queue index invalid, max queue is:%" PRIu16 ".", nb_rx_queues);
        return -EINVAL;
    }

    rxq = data->rx_queues[queue_id];
    backlog = (int)hinic3_get_rx_queue_count(rxq->dpdk_port_id, hinic3_queue_get_hiovs_queue_id(rxq));
    if (backlog < 0) {
        HINIC3_LOG(ERR, VPORT, "rxq%" PRIu16 " get upcall backlog failed ret: %d, hiovs queue id: %" PRIu16 ".",
            rxq->queue_id, backlog, hinic3_queue_get_hiovs_queue_id(rxq));
        return -EPERM;
    }

    return backlog;
}