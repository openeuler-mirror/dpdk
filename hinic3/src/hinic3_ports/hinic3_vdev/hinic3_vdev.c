/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2025 Huawei Technologies Co., Ltd
 */

#include "rte_devargs.h"
#include "hinic3_bond_statistics.h"
#include "hinic3_bond_controller.h"
#include "hinic3_vf_controller.h"
#include "hinic3_vf_statistics.h"
#include "hinic3_offload_flow.h"
#include "hinic3_offload_flow_port.h"
#include "hinic3_qos.h"
#include "hinic3_set_userdata.h"
#include "hinic3_parse_agent_config.h"
#include "hinic3_vdev.h"

#define MAX_NAME                        32
#define PRIORITY_ENABLE_LEN             10
#define HINIC3_VF_MAX_QUEUE_DEFAULT_NUM 1
#define HINIC3_VF_DEFAULT_MTU           1500
#define HINIC3_VF_MIN_FUNCTION_ID       0
#define HINIC3_METER_RTE_MBUF_GET_PTR   8
#define HINIC3_NETDEV_MAX_BURST         32

#define HINIC3_ETH_DEV_FLOW_OPS_THREAD_SAFE  0x0001
#define HINIC3_REPRESENTOR_FLAG              (1 << 4)

static const char * const g_hinic3_vdev_valid_arguments[] = {
    HINIC3_BOND_NAME,
    HINIC3_PORT_PCI_ID,
    HINIC3_PORT_MAC,
    HINIC3_MAX_QUEUE_NUM,
    HINIC3_REPRESENTOR_ID,
    HINIC3_SHARE_UPCALL,
    HINIC3_PRIORITY_UPCALL,
    HINIC3_PORT_VIRTIO_QUEUE_DEPTH,
    HINIC3_BOND_ARG_SLAVE_MTU_STR,
    NULL
};

static struct rte_eth_link g_hinic3_vf_dev_link = {
    .link_speed = RTE_ETH_SPEED_NUM_10G,
    .link_duplex = RTE_ETH_LINK_FULL_DUPLEX,
    .link_status = RTE_ETH_LINK_DOWN,
    .link_autoneg = RTE_ETH_LINK_FIXED,
};

static struct rte_eth_link g_hinic3_bond_vdev_link = {
    .link_speed = RTE_ETH_SPEED_NUM_10G,
    .link_duplex = RTE_ETH_LINK_FULL_DUPLEX,
    .link_status = RTE_ETH_LINK_UP,
    .link_autoneg = RTE_ETH_LINK_FIXED,
};

static struct eth_dev_ops *g_hinic3_vf_vdev_ops = NULL;
static struct eth_dev_ops *g_hinic3_bond_vdev_ops = NULL;

struct open_str_extra_args {
    size_t extra_args_str_len;
    char* extra_args_str;
};

const char * const *hinic3_vdev_valid_arguments_get(void)
{
    return g_hinic3_vdev_valid_arguments;
}

static void
hinic3_vf_dev_ops_construct(void)
{
    struct eth_dev_ops *edev_ops = NULL;

    edev_ops = hinic3_calloc(1, sizeof(struct eth_dev_ops), HINIC3_PORTS);
    if (edev_ops == NULL)
        return;

    edev_ops->dev_configure = hinic3_vf_dev_configure;
    edev_ops->dev_start = hinic3_vf_dev_start;
    edev_ops->dev_stop = hinic3_vf_dev_stop;
    edev_ops->dev_close = hinic3_vf_dev_close;
    edev_ops->dev_set_link_down = hinic3_vf_set_link_down;
    edev_ops->dev_set_link_up = hinic3_vf_set_link_up;
    edev_ops->dev_infos_get = hinic3_vf_dev_infos_get;
    edev_ops->stats_get = hinic3_vf_dev_stats_get;
    edev_ops->stats_reset = hinic3_vf_dev_stats_reset;
    edev_ops->xstats_get = hinic3_vf_dev_xstats_get;
    edev_ops->xstats_reset = hinic3_vf_dev_xstats_reset;
    edev_ops->xstats_get_names = hinic3_vf_dev_xstats_get_names;
    edev_ops->link_update = hinic3_vf_link_update;
    edev_ops->mtu_set = hinic3_vf_dev_set_mtu;
    edev_ops->rx_queue_setup = hinic3_vf_rx_queue_setup;
    edev_ops->rx_queue_release = hinic3_vf_rx_queue_release;
    edev_ops->tx_queue_setup = hinic3_vf_tx_queue_setup;
    edev_ops->tx_queue_release = hinic3_vf_tx_queue_release;
    edev_ops->flow_ops_get = hinic3_vf_dev_flow_ops_get;
    edev_ops->mac_addr_set = hinic3_vf_dev_mac_addr_set;
    edev_ops->mtr_ops_get = hinic3_mtr_ops_get;

    g_hinic3_vf_vdev_ops = edev_ops;
}

static void
hinic3_vf_dev_ops_destroy(void)
{
    if (g_hinic3_vf_vdev_ops == NULL)
        return;

    hinic3_free(g_hinic3_vf_vdev_ops);
    g_hinic3_vf_vdev_ops = NULL;
}

static void
hinic3_bond_dev_ops_construct(void)
{
    struct eth_dev_ops *edev_ops = NULL;

    edev_ops = hinic3_calloc(1, sizeof(struct eth_dev_ops), HINIC3_PORTS);
    if (edev_ops == NULL)
        return;

    edev_ops->dev_configure = hinic3_bond_dev_configure;
    edev_ops->dev_start = hinic3_bond_dev_start;
    edev_ops->dev_stop = hinic3_bond_dev_stop;
    edev_ops->dev_close = hinic3_bond_dev_close;
    edev_ops->dev_set_link_down = hinic3_bond_set_link_down;
    edev_ops->dev_set_link_up = hinic3_bond_set_link_up;
    edev_ops->dev_infos_get = hinic3_bond_dev_infos_get;
    edev_ops->stats_get = hinic3_bond_dev_stats_get;
    edev_ops->stats_reset = hinic3_bond_dev_stats_reset;
    edev_ops->xstats_get = hinic3_bond_dev_xstats_get;
    edev_ops->xstats_reset = hinic3_bond_dev_xstats_reset;
    edev_ops->xstats_get_names = hinic3_bond_dev_xstats_get_names;
    edev_ops->link_update = hinic3_bond_link_update;
    edev_ops->mtu_set = hinic3_bond_dev_set_mtu;
    edev_ops->rx_queue_setup = hinic3_bond_rx_queue_setup;
    edev_ops->tx_queue_setup = hinic3_bond_tx_queue_setup;
    edev_ops->rx_queue_release = hinic3_bond_rx_queue_release;
    edev_ops->tx_queue_release = hinic3_bond_tx_queue_release;
    edev_ops->flow_ops_get = hinic3_bond_dev_flow_ops_get;
    edev_ops->mtr_ops_get = hinic3_mtr_ops_get;

    g_hinic3_bond_vdev_ops = edev_ops;
}

static void
hinic3_bond_dev_ops_destroy(void)
{
    if (g_hinic3_bond_vdev_ops == NULL)
        return;

    hinic3_free(g_hinic3_bond_vdev_ops);
    g_hinic3_bond_vdev_ops = NULL;
}


static int
open_u32(const char *key __rte_unused, const char *value, void *extra_args)
{
    uint32_t *n = extra_args;
    char *endPtr = NULL;

    if (value == NULL || extra_args == NULL)
        return -EINVAL;

    *n = (uint32_t)strtoul(value, &endPtr, 0);
    if (endPtr == NULL || *endPtr != '\0')
        return -1;

    if (*n == USHRT_MAX && errno == ERANGE)
        return -1;

    return 0;
}

static int
open_u16(const char *key __rte_unused, const char *value, void *extra_args)
{
    uint32_t *n = extra_args;
    char *end_ptr = NULL;

    if (value == NULL || extra_args == NULL)
        return -EINVAL;

    *n = (uint16_t)strtoul(value, &end_ptr, 0);
    if (end_ptr == NULL || *end_ptr != '\0')
        return -1;

    if (*n == USHRT_MAX && errno == ERANGE)
        return -1;

    return 0;
}

static int
open_mac(const char *key __rte_unused, const char *value, void *extra_args)
{
    struct eth_addr *n = extra_args;

    if (value == NULL || extra_args == NULL)
        return -EINVAL;

    if (rte_ether_unformat_addr(value, (struct rte_ether_addr *)n) != 0) {
        HINIC3_LOG(ERR, VPORT, "failed to parse mac addr from str!");
        return -1;
    }
    if (eth_addr_is_zero(*n) == true) {
        HINIC3_LOG(ERR, VPORT, "mac addr is zero!");
        return -1;
    }
    if (eth_addr_is_multicast(*n) == true) {
        HINIC3_LOG(ERR, VPORT, "mac addr can't be multicast!");
        return -1;
    }

    return 0;
}

static int
open_pci(const char *key __rte_unused, const char *value, void *extra_args)
{
    struct rte_pci_addr *n = extra_args;

    if (value == NULL || extra_args == NULL)
        return -1;

    if (rte_pci_addr_parse(value, n) != 0) {
        HINIC3_LOG(ERR, VPORT, "failed to parse pci addr from str!");
        return -1;
    }

    return 0;
}

static int
open_function_id(const char *key __rte_unused, const char *value, void *extra_args)
{
    int *id = extra_args;
    char *end_ptr = NULL;

    if (value == NULL || id == NULL)
        return -1;

    *id = strtoul(value, &end_ptr, STR_TO_DEC_NUM);
    if (*id < HINIC3_VF_MIN_FUNCTION_ID || end_ptr == NULL || *end_ptr != '\0') {
        HINIC3_LOG(ERR, VPORT, "failed to parse function id str, id is %d!", *id);
        return -1;
    }

    return 0;
}

static int
open_share_upcall(const char *key __rte_unused, const char *value, void *extra_args)
{
    uint8_t *share_upcall = extra_args;

    if (value == NULL || share_upcall == NULL)
        return -1;

    if (strcmp("true", value) == 0) {
        *share_upcall = true;
    } else if (strcmp("false", value) == 0) {
        *share_upcall = false;
    } else {
        HINIC3_LOG(ERR, VPORT, "failed to parse share upcall str!");
        return -1;
    }

    return 0;
}

static int hinic3_vf_parse_max_queue(struct rte_kvargs *kvlist, uint32_t *max_queue_num)
{
    int ret = 0;
    uint32_t count = 0;
    if (kvlist == NULL)
        return -1;

    count = rte_kvargs_count(kvlist, HINIC3_MAX_QUEUE_NUM);
    if (count > 1) {
        HINIC3_LOG(ERR, VPORT, "multi max queue num found!");
        return -1;
    } else if (count == 1) {
        ret = rte_kvargs_process(kvlist, HINIC3_MAX_QUEUE_NUM, open_u32, max_queue_num);
        if (ret != 0 || *max_queue_num == 0) {
            HINIC3_LOG(ERR, VPORT, "max queue num parse failed!");
            return -1;
        }
        if (*max_queue_num > hinic3_max_queue_num_limit_get()) {
            HINIC3_LOG(ERR, VPORT, "max queue num exceeds the max support value: %u", hinic3_max_queue_num_limit_get());
            return -1;
        }
    } else {
        *max_queue_num = HINIC3_VF_MAX_QUEUE_DEFAULT_NUM;
    }

    return 0;
}

static int
hinic3_vf_parse_mac_addr(struct rte_kvargs *kvlist, struct eth_addr *mac_addr)
{
    int ret = 0;
    uint32_t count = 0;
    if (kvlist == NULL)
        return -1;

    count = rte_kvargs_count(kvlist, HINIC3_PORT_MAC);
    if (count > 1) {
        HINIC3_LOG(ERR, VPORT, "multi vf mac found!");
        return -1;
    } else if (count == 1) {
        ret = rte_kvargs_process(kvlist, HINIC3_PORT_MAC, open_mac, mac_addr);
        if (ret != 0) {
            HINIC3_LOG(ERR, VPORT, "failed to parse mac addr!");
            return -1;
        }
    } else {
        if (rte_ether_unformat_addr(HINIC3_VF_MAC_ADDR_DEFAULT, (struct rte_ether_addr *)mac_addr) != 0) {
            HINIC3_LOG(ERR, VPORT, "failed to set default zero mac addr!");
            return -1;
        }
    }
    return 0;
}

static int
hinic3_vf_parse_share_upcall(struct rte_kvargs *kvlist, uint8_t *share_upcall)
{
    int ret = 0;
    uint32_t count = 0;
    if (kvlist == NULL)
        return -1;

    count = rte_kvargs_count(kvlist, HINIC3_SHARE_UPCALL);
    if (count > 1) {
        HINIC3_LOG(ERR, VPORT, "vf parse share upcall, multi max queue num found!");
        return -1;
    } else if (count == 1) {
        ret = rte_kvargs_process(kvlist, HINIC3_SHARE_UPCALL, open_share_upcall, share_upcall);
        if (ret != 0) {
            HINIC3_LOG(ERR, VPORT, "vf parse share upcall, max queue num parse failed!");
            return -1;
        }
    } else {
        *share_upcall = false;
    }

    return 0;
}

static int hinic3_vf_parse_function_id(struct rte_kvargs *kvlist, uint16_t *function_id)
{
    int ret = 0;
    uint32_t count = 0;
    if (kvlist == NULL)
        return -1;

    count = rte_kvargs_count(kvlist, HINIC3_REPRESENTOR_ID);
    if (count != 1) {
        HINIC3_LOG(ERR, VPORT, "failed to obtain the number of vf function id!");
        return -1;
    }

    ret = rte_kvargs_process(kvlist, HINIC3_REPRESENTOR_ID, open_function_id, function_id);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "failed to parse vf function id!");
        return -1;
    }

    return 0;
}

static int
hinic3_vf_parse_pci_addr(struct rte_kvargs *kvlist, struct rte_pci_addr *pci_addr)
{
    int ret = 0;
    uint32_t count = 0;
    if (kvlist == NULL)
        return -1;

    count = rte_kvargs_count(kvlist, HINIC3_PORT_PCI_ID);
    if (count == 1) {
        ret = rte_kvargs_process(kvlist, HINIC3_PORT_PCI_ID, open_pci, pci_addr);
        if (ret != 0) {
            HINIC3_LOG(ERR, VPORT, "failed to parse pci addr!");
            return -1;
        }
    } else {
        if (count > 1)
            HINIC3_LOG(ERR, VPORT, "The number of VF PCIs is too large!");
        else
            HINIC3_LOG(ERR, VPORT, "vf pci not found!");

        return -1;
    }

    ret = hinic3_set_port_conf_type(PORT_CONF_PCI);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "hinic3_set_port_conf_type error");
        return -1;
    }

    return 0;
}

static int
hinic3_check_vf_args_valid(const struct rte_pci_addr *pci_addr, uint16_t function_id, uint32_t max_queue_num)
{
    int ret = 0;
    struct hinic3_dp_extend_info *dp_info = hinic3_get_offload_extend_info();
    if ((dp_info == NULL) || (dp_info->hw_offload == NULL))
        return -1;

    if (hinic3_device_mode_get() == DPU_MODE && hinic3_get_port_list_init() == false) {
        ret = hinic3_dpu_vf_flavor_add();
        if (ret != 0) {
            HINIC3_LOG(ERR, VPORT, "hinic3_dpu_vf_flavor_add err, ret is %d!", ret);
            return -1;
        }
        hinic3_set_port_list_init(true);
    }

    struct hinic3_flow_agent_db *hw_offload = dp_info->hw_offload;
    /* 检查该pci地址是否已被使用 */
    enum hinic3_bdf_status bdf_status = hinic3_vf_flavor_get_phy_dev_refcnt(pci_addr, function_id);
    if (bdf_status == HINIC3_VF_BDF_USED) {
        HINIC3_LOG(ERR, VPORT, "The BDF number is occupied!");
        return -1;
    } else if (bdf_status == HINIC3_VF_BDF_INVALID) {
        HINIC3_LOG(ERR, VPORT, "The BDF number is invalid!");
        return -1;
    } else if (bdf_status == HINIC3_VF_BDF_LIST_NULL) {
        HINIC3_LOG(ERR, VPORT, "The BDF number is not found!");
        return -1;
    }

    /* 判断将要分配的max_queue_num是否超过总容量,每个VF需要再占用一个控制队列 */
    if (hw_offload->queue_num.used_now + max_queue_num + 1 > hw_offload->queue_num.total_count) {
        HINIC3_LOG(ERR, VPORT, "Queue resources are insufficient!");
        return -1;
    }

    return 0;
}

static void
hinic3_mbuf_set_share_upcall_port(uint16_t upcall_num, uint16_t queue_id, struct rte_mbuf **rx_pkts)
{
    int ret = 0;
    struct hinic3_vf_dev *vf_dev = NULL;
    struct rte_mbuf *mbuf = NULL;
    struct hinic3_pkt_user_data *hinic3_metadata = NULL;
    uint16_t dpdk_index_id = 0;
    uint16_t vport_id = 0;

    for (uint16_t i = 0; i < upcall_num; i++) {
        hinic3_metadata = (struct hinic3_pkt_user_data *)(&rx_pkts[i]->dynfield1[1]);
        vport_id = hinic3_metadata->vport_id;
        ret = hinic3_get_port_id_by_ifindex(vport_id, &dpdk_index_id);
        if (HINIC3_UNLIKELY(ret != 0))
            continue;

        vf_dev = (struct hinic3_vf_dev *)hinic3_get_private_data(dpdk_index_id);
        if (HINIC3_UNLIKELY(vf_dev == NULL)) {
            hinic3_add_error_stats(HINIC3_VPORT_DEV_SHARE_UPCALL_PRIVATE_DATA_NULL, 1);
            continue;
        }

        mbuf = rx_pkts[i];
        if (HINIC3_LIKELY(mbuf != NULL)) {
            mbuf->port = dpdk_index_id;
            rte_atomic64_add(&vf_dev->vf_upcall_pk_byt, rte_pktmbuf_pkt_len(mbuf));
        }
        rte_atomic64_add(&vf_dev->vf_upcall_pk_num, 1);
        rte_atomic64_add(&vf_dev->upcall_queue.rx_queues[queue_id].pkt_stats, 1);
    }
}

static void
hinic3_mbuf_set_common_port(uint16_t upcall_num, struct hinic3_queue *rxq, struct rte_mbuf **rx_pkts)
{
    struct hinic3_vf_dev *vf_dev = NULL;
    struct rte_mbuf *mbuf = NULL;
    int64_t upcall_byt = 0;

    vf_dev = (struct hinic3_vf_dev *)hinic3_get_private_data(rxq->dpdk_index_id);
    if (vf_dev == NULL) {
        hinic3_add_error_stats(HINIC3_VPORT_DEV_COMMOM_PORT_PRIVATE_DATA_NULL, 1);
        return;
    }

    for (uint16_t i = 0; i < upcall_num; i++) {
        mbuf = rx_pkts[i];
        if (HINIC3_LIKELY(mbuf != NULL)) {
            mbuf->port = rxq->dpdk_index_id;
            upcall_byt += rte_pktmbuf_pkt_len(mbuf);
        }
    }
    rte_atomic64_add(&rxq->pkt_stats, upcall_num);
    rte_atomic64_add(&vf_dev->vf_upcall_pk_num, upcall_num);
    rte_atomic64_add(&vf_dev->vf_upcall_pk_byt, upcall_byt);
}

uint16_t
hinic3_vf_recv_pkts(void *rx_queue, struct rte_mbuf **rx_pkts, uint16_t nb_pkts)
{
    struct hinic3_queue *rxq = (struct hinic3_queue *)rx_queue;
    uint16_t upcall_num = 0;

    if (hinic3_queue_valid(rxq) != 0) {
        hinic3_add_error_stats(HINIC3_VPORT_VF_RX_QUEUE_INVALID, 1);
        return 0;
    }

    upcall_num = hinic3_global_rte_eth_rx_burst(
        rxq->dpdk_port_id, hinic3_queue_get_hiovs_queue_id(rxq), rx_pkts, nb_pkts);
    if (upcall_num > 0) {
        if (rxq->variant.stdqueue->share_upcall == false)
            hinic3_mbuf_set_common_port(upcall_num, rxq, rx_pkts);
        else
            hinic3_mbuf_set_share_upcall_port(upcall_num, rxq->queue_id, rx_pkts);
    }

    return upcall_num;
}

static int
hinic3_mbuf_set_virtual_queue_port(uint16_t upcall_num, struct hinic3_queue *rxq, struct rte_mbuf **rx_pkts)
{
    struct hinic3_vf_dev *vf_dev = NULL;
    struct rte_mbuf *mbuf = NULL;
    int64_t upcall_byt = 0;

    vf_dev = (struct hinic3_vf_dev *)hinic3_get_private_data(rxq->dpdk_index_id);
    if (vf_dev == NULL) {
        hinic3_add_error_stats(HINIC3_VPORT_DEV_COMMOM_PORT_PRIVATE_DATA_NULL, 1);
        return 0;
    }

    for (uint16_t i = 0; i < upcall_num; i++) {
        mbuf = rx_pkts[i];
        if (HINIC3_LIKELY(mbuf != NULL)) {
            mbuf->port = rxq->dpdk_index_id;
            upcall_byt += rte_pktmbuf_pkt_len(mbuf);
        }
    }
    rte_atomic64_add(&rxq->pkt_stats, upcall_num);
    rte_atomic64_add(&vf_dev->vf_upcall_pk_num, upcall_num);
    rte_atomic64_add(&vf_dev->vf_upcall_pk_byt, upcall_byt);

    return 0;
}

static uint16_t
hinic3_virtual_queue_recv_pkts_distribute(struct hinic3_queue *rxq,
    struct rte_mbuf **buffer, struct rte_mbuf **rx_pkts, uint16_t nb_pkts, uint16_t upcall_num)
{
    struct hinic3_queue *rxq_neighbor = NULL;
    struct hinic3_virtual_queue *virtual_rxq = rxq->variant.virtqueue;
    struct hinic3_physical_queue *physical_rxq = virtual_rxq->physical_queue;
    struct hinic3_pkt_user_data *hinic3_metadata = NULL;
    uint8_t vgroup_num = hinic3_virtual_queue_multiplex_get();
    uint16_t pkt_vport_id = 0;

    for (uint16_t i = 0; i < nb_pkts; i++) {
        hinic3_metadata = (struct hinic3_pkt_user_data *)(&buffer[i]->dynfield1[1]);
        pkt_vport_id = hinic3_metadata->vport_id;

        for (uint8_t vgroup_id = 0; vgroup_id < vgroup_num; ++vgroup_id) {
            /* 未激活的队列不处理 */
            if (physical_rxq->is_virtual_queue_valids[vgroup_id] == false)
                continue;

            /* 属于当前软件队列的包直接放入返回区 */
            if (pkt_vport_id == rxq->vport_id) {
                rx_pkts[upcall_num] = buffer[i];
                ++upcall_num;
                buffer[i] = NULL;
                break;
            }
            /* 属于相邻软件队列的包放入对应的软件队列环中 */
            rxq_neighbor = physical_rxq->virtual_queues[vgroup_id]->queue_info;
            if (pkt_vport_id == rxq_neighbor->vport_id) {
                rte_ring_enqueue_burst(rxq_neighbor->variant.virtqueue->ring, (void **)&buffer[i], 1, NULL);
                buffer[i] = NULL;
                break;
            }
        }
        /* 未匹配到去向的包丢弃 */
        if (HINIC3_UNLIKELY(buffer[i] != NULL)) {
            rte_pktmbuf_free(buffer[i]);
            buffer[i] = NULL;
            hinic3_add_error_stats(HINIC3_VPORT_VIRTUAL_QUEUE_RECV_PKTS_DISTRIBUTE_DROP, 1);
            break;
        }
    }

    return upcall_num;
}

uint16_t
hinic3_vf_recv_pkts_in_virtual_queue(void *virtual_rx_queue, struct rte_mbuf **rx_pkts, uint16_t nb_pkts)
{
    struct hinic3_queue *rxq = (struct hinic3_queue *)virtual_rx_queue;
    struct hinic3_virtual_queue *virtual_rxq = rxq->variant.virtqueue;
    struct hinic3_physical_queue *physical_rxq = virtual_rxq->physical_queue;
    uint16_t upcall_num = 0;
    uint16_t distribute_num = 0;
    struct rte_mbuf *buffer[HINIC3_NETDEV_MAX_BURST] = {0};

    if (nb_pkts > HINIC3_NETDEV_MAX_BURST)
        nb_pkts = HINIC3_NETDEV_MAX_BURST;

    if (hinic3_queue_valid(rxq) != 0) {
        hinic3_add_error_stats(HINIC3_VPORT_VIRTUAL_RX_QUEUE_INVALID, 1);
        return 0;
    }

    /* 先取软件队列中的包，缓存区不满则取硬件队列，并分发至对应的软件队列 */
    if (rte_ring_count(virtual_rxq->ring) != 0)
        upcall_num = rte_ring_dequeue_burst(virtual_rxq->ring, (void **)rx_pkts, nb_pkts, NULL);

    /* 硬件队列读取分发过程加锁，防止线程竞争导致报文乱序 */
    if (upcall_num != nb_pkts) {
        rte_spinlock_lock(&physical_rxq->physical_queue_lock);
        distribute_num = hinic3_global_rte_eth_rx_burst(
            rxq->dpdk_port_id, physical_rxq->hiovs_queue_id, buffer, nb_pkts - upcall_num);
        upcall_num = hinic3_virtual_queue_recv_pkts_distribute(rxq, buffer, rx_pkts, distribute_num, upcall_num);
        rte_spinlock_unlock(&physical_rxq->physical_queue_lock);
    }

    if (upcall_num > 0) {
        int ret = hinic3_mbuf_set_virtual_queue_port(upcall_num, rxq, rx_pkts);
        if (ret != 0)
            return 0;
    }

    return upcall_num;
}

uint16_t
hinic3_vf_xmit_pkts(void *tx_queue, struct rte_mbuf **tx_pkts, uint16_t nb_pkts)
{
    struct hinic3_queue *txq = tx_queue;
    struct hinic3_vf_dev *vf_dev = NULL;
    struct rte_mbuf *mbuf = NULL;
    uint16_t reinject_num = 0;
    int64_t reinject_byt = 0;
    bool pmd_pktinfo_free = hinic3_check_masked_to_exact_switch() &&
        hinic3_forward_mode_get() != OVS_KEY_EXTRACT_EXTEND_MODE_7TUPLE;

    if (hinic3_queue_valid(txq) != 0) {
        hinic3_add_error_stats(HINIC3_VPORT_VF_TX_QUEUE_INVALID, 1);
        return 0;
    }
    hinic3_set_vport_id_into_userdata(txq->vport_id, tx_pkts, nb_pkts);

    if (pmd_pktinfo_free == true) {
        for (uint16_t i = 0; i < nb_pkts; i++)
            hinic3_free_pmd_pkt_info((struct hinic3_inner_metadata *)(&tx_pkts[i]->dynfield1[1]));
    }

    reinject_num = hinic3_global_rte_eth_tx_burst(txq->dpdk_port_id, txq->queue_id, tx_pkts, nb_pkts);
    if (reinject_num > 0) {
        vf_dev = (struct hinic3_vf_dev*)hinic3_get_private_data(txq->dpdk_index_id);
        if (HINIC3_UNLIKELY(vf_dev == NULL)) {
            hinic3_add_error_stats(HINIC3_VPORT_DEV_VF_XMIT_PRIVATE_DATA_NULL, 1);
            return 0;
        }

        for (uint16_t i = 0; i < reinject_num; i++) {
            mbuf = tx_pkts[i];
            if (mbuf != NULL)
                reinject_byt += rte_pktmbuf_pkt_len(mbuf);
        }

        rte_atomic64_add(&txq->pkt_stats, reinject_num);
        rte_atomic64_add(&vf_dev->vf_reinject_pk_num, reinject_num);
        rte_atomic64_add(&vf_dev->vf_reinject_pk_byt, reinject_byt);
    }

    return reinject_num;
}

static void
hinic3_vf_dev_init(struct rte_eth_dev *eth_dev, struct rte_ether_addr *mac_addr)
{
    struct hinic3_vf_dev *vf_dev = hinic3_ethdev_get_vf_private(eth_dev);
    struct rte_eth_dev_data *data = eth_dev->data;

    data->mac_addrs = mac_addr;
    data->dev_link = g_hinic3_vf_dev_link;
    data->dev_flags |= HINIC3_ETH_DEV_FLOW_OPS_THREAD_SAFE;
    data->dev_started = HINIC3_DEV_STATE_STOP;
    data->nb_rx_queues = HINIC3_INVALID_QUEUE_NUM;
    data->nb_tx_queues = HINIC3_INVALID_QUEUE_NUM;

    vf_dev->n_txq = data->nb_tx_queues;
    vf_dev->n_upcall_queue = data->nb_rx_queues;
    vf_dev->function_id = data->representor_id;
    vf_dev->src_mac_num = HINIC3_VF_SECURITY_SMAC_INVALID;
    vf_dev->eth_type_group_id = HINIC3_VF_SECURITY_ETH_GROUP_INVALID;
    vf_dev->brd_rate_limit = HINIC3_VF_SECURITY_BRD_LIMIT_INVALID;
    memset(vf_dev->src_mac_list, 0, sizeof(vf_dev->src_mac_list));
    vf_dev->vni = VNI_INVALID;
    hinic3_list_init(&vf_dev->profiles);
    for (uint8_t i = 0; i < HINIC3_MTR_NUM; i++) {
        vf_dev->mtr[i].is_used = false;
        vf_dev->mtr[i].mtr_id = INVALID_MTR_ID;
        vf_dev->mtr[i].direction = INVALID_MTR_DIR;
    }
    for (int i = 0; i < HINIC3_VF_CONFIG_MAX; i++)
        vf_dev->state[i] = HINIC3_VF_CONFIG_NONE;

    for (int i = 0; i < MAX_RX_QUEUE_PER_VPORT; i++) {
        vf_dev->upcall_queue.upcall_queue_id[i] = INVALID_UPCALL_QUEUE_ID;
        vf_dev->upcall_queue.is_queue_valid[i] = false;
    }
    rte_atomic64_init(&vf_dev->vf_upcall_pk_num);
    rte_atomic64_init(&vf_dev->vf_upcall_pk_byt);
    rte_atomic64_init(&vf_dev->vf_reinject_pk_num);
    rte_atomic64_init(&vf_dev->vf_reinject_pk_byt);
    hinic3_smap_init(&vf_dev->qos_options);
}

static int
hinic3_vf_pci_by_function_id(uint16_t *function_id, struct rte_pci_addr *pci_addr)
{
    int ret = 0;
    struct hovs_phy_dev_info dev = { 0 };
    struct hovs_pci_addr_info *info = NULL;
    union bdf_info_u bdf_info;

    if (hinic3_query_bdf_type_get() == QUERY_BDF_TYPE_REAL)
        bdf_info.bs.bdf_type = BDF_TYPE_REAL;
    else
        bdf_info.bs.bdf_type = BDF_TYPE_FAKE;

    bdf_info.bs.func_id_flag = 1;

    ret = hinic3_global_pcie_list_query(1, bdf_info.value, &dev);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "hinic3 get global pcie list err");
        return -1;
    }

    for (uint32_t i = 0; i < dev.phy_dev_num; i++) {
        info = &dev.pci_info[i];
        if (*function_id == info->glb_func_inx) {
            memcpy(pci_addr, (struct rte_pci_addr *)&info->pci_addr, sizeof(struct rte_pci_addr));
            return 0;
        }
    }
    return -1;
}

static int
hinic3_vf_parse_representor_id(
    struct rte_kvargs *kvlist, struct rte_pci_addr *pci_addr, uint16_t *function_id, struct rte_eth_dev_data *data)
{
    int ret = 0;

    ret = hinic3_vf_parse_function_id(kvlist, function_id);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "hinic3_vf_parse_function_id error!");
        return -1;
    }

    ret = hinic3_vf_pci_by_function_id(function_id, pci_addr);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "hinic3_vf_pci_by_function_id error!");
        return -1;
    }

    ret = hinic3_set_port_conf_type(PORT_CONF_FUNC);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "hinic3_set_port_conf_type error!");
        return -1;
    }

    data->representor_id = *function_id;
    data->dev_flags |=  HINIC3_REPRESENTOR_FLAG;
    data->backer_port_id = data->port_id;

    return 0;
}

static int
hinic3_port_parse_pci_addr(
    struct rte_kvargs *kvlist, struct rte_pci_addr *pci_addr, uint16_t *function_id, struct rte_eth_dev_data *data)
{
    int pci_cnt = rte_kvargs_count(kvlist, HINIC3_PORT_PCI_ID);
    int func_cnt = rte_kvargs_count(kvlist, HINIC3_REPRESENTOR_ID);
    data->representor_id = 0;
    struct hinic3_init_arg *conf = hinic3_get_init_arg();
    /**
        host下电虚机场景，配置文件参数为REAL。当TYPE为REAL时，使用function_id下发端口，会去前端查询pci list报错，所以这里判断
        虚机场景下如果是使用function_id下发，则将TYPE更改为fake pci查询
    */
    if ((func_cnt == 1) && (pci_cnt == 0)) {
        conf->query_bdf_type = QUERY_BDF_TYPE_FAKE;
        return hinic3_vf_parse_representor_id(kvlist, pci_addr, function_id, data);
    } else if ((func_cnt == 0) && (pci_cnt == 1))
        return hinic3_vf_parse_pci_addr(kvlist, pci_addr);
    else {
        HINIC3_LOG(ERR, VPORT, "hinic3_vf_arg_parse bad pci or representor arg");
        return -1;
    }

    return 0;
}

static int
hinic3_vf_parse_virtio_queue_depth(struct rte_kvargs *kvlist, uint16_t *virtio_queue_depth)
{
    int ret;
    uint32_t count;
    bool is_virtio_queue_depth_set = false;
    if (kvlist == NULL)
        return -1;

    is_virtio_queue_depth_set = hinic3_check_virtio_queue_depth_set();
    count = rte_kvargs_count(kvlist, HINIC3_PORT_VIRTIO_QUEUE_DEPTH);
    if (!is_virtio_queue_depth_set && count >= 1) {
        HINIC3_LOG(ERR, VPORT, "can not use command to set virtio queue depth");
        return -1;
    }

    if (count > 1) {
        HINIC3_LOG(ERR, VPORT, "multi queue depth found!");
        return -1;
    } else if (count == 1) {
        ret = rte_kvargs_process(kvlist, HINIC3_PORT_VIRTIO_QUEUE_DEPTH, open_u16, virtio_queue_depth);
        if (ret != 0 || *virtio_queue_depth == 0) {
            HINIC3_LOG(ERR, VPORT, "virtio queue depth num parse failed");
            return -1;
        }
        if (*virtio_queue_depth < HINIC3_PORT_MIN_QUEUE_DEPTH || *virtio_queue_depth > HINIC3_PORT_MAX_QUEUE_DEPTH) {
            *virtio_queue_depth = 0;
            HINIC3_LOG(ERR, VPORT, "virtio queue_depth is out of the range");
            return -1;
        }
    }
    return 0;
}

static int hinic3_vf_parse_mtu(struct rte_kvargs *kvlist, uint32_t *mtu)
{
    int ret = 0;
    uint32_t count = 0;
    if (kvlist == NULL)
        return -1;

    count = rte_kvargs_count(kvlist, HINIC3_BOND_ARG_SLAVE_MTU_STR);
    if (count > 1) {
        HINIC3_LOG(ERR, VPORT, "mtu num found!");
        return -1;
    } else if (count == 1) {
        ret = rte_kvargs_process(kvlist, HINIC3_BOND_ARG_SLAVE_MTU_STR, open_u32, mtu);
        if (ret != 0 ) {
            HINIC3_LOG(ERR, VPORT, "mtu parse failed!");
            return -1;
        }
    } 
    return 0;
}

static int
hinic3_vf_arg_parse(struct rte_kvargs *kvlist, struct hinic3_vf_dev *vf_dev, struct rte_eth_dev_data *data)
{
    int ret = 0;

    ret = hinic3_vf_parse_max_queue(kvlist, &vf_dev->max_queue_num);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "hinic3_vf_parse_max_queue error!");
        return -1;
    }

    ret = hinic3_vf_parse_mac_addr(kvlist, &vf_dev->mac_addr);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "hinic3_vf_parse_mac_addr error!");
        return -1;
    }

    ret = hinic3_vf_parse_share_upcall(kvlist, &vf_dev->share_upcall);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "hinic3_vf_parse_share_upcall error!");
        return -1;
    }

    ret = hinic3_port_parse_pci_addr(kvlist, &vf_dev->pci_addr, &vf_dev->function_id, data);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "hinic3_port_parse_pci_addr error!");
        return -1;
    }

    ret = hinic3_vf_parse_virtio_queue_depth(kvlist, &vf_dev->virtio_queue_depth);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "hinic3_virtio_queue_depth error!");
        return -1;
    }

    ret = hinic3_vf_parse_mtu(kvlist, &vf_dev->mtu);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "hinic3_vf_parse_mtu error!");
        return -1;
    }

    if (vf_dev->mtu != 0) {
        HINIC3_LOG(INFO, VPORT, "attach port add mtu.");
        vf_dev->attach_mtu = 1;
    }

    return 0;
}

static int hinic3_init_dev_type(struct hinic3_vf_dev *vf_dev)
{
    struct hwpt_phy_dev *dev = hinic3_flavor_get_phy_dev(&vf_dev->pci_addr);
    if (dev == NULL)
        return -1;

    if (dev->type == HWPT_PHY_DEV_TYPE_PF) {
        vf_dev->dev_type = HINIC3_PHY_DEV_TYPE_PF;
    } else if (dev->type == HWPT_PHY_DEV_TYPE_VF) {
        vf_dev->dev_type = HINIC3_PHY_DEV_TYPE_VF;
    } else {
        HINIC3_LOG(ERR, VPORT, "hwoff init dev type invaild type!");
        return -1;
    }
    return 0;
}

static int
hinic3_add_vf_flavors(void)
{
    int ret = 0;
    if (hinic3_device_mode_get() == DPU_MODE && hinic3_get_port_list_init() == false) {
        ret = hinic3_dpu_vf_flavor_add();
        if (ret != 0) {
            HINIC3_LOG(ERR, VPORT, "hinic3_dpu_vf_flavor_add err, ret is %d!", ret);
            return -1;
        }
        hinic3_set_port_list_init(true);
    }

    if (hinic3_device_mode_get() == SMART_NIC_MODE) {
        if (hinic3_updata_netdev_hwpt_flavor() == NULL) {
            HINIC3_LOG(ERR, VPORT, "hinic3 smart nic vf flavors update failed!");
            return -1;
        }
    }

    return 0;
}

int
hinic3_vf_vdev_init(struct rte_eth_dev *eth_dev, struct rte_kvargs *kvlist)
{
    int ret = 0;
    char buff[HWPT_PCI_ADDR_LEN_MAX];
    struct rte_ether_addr *mac_addr = NULL;
    struct hinic3_vf_dev *vf_dev = hinic3_ethdev_get_vf_private(eth_dev);
    struct rte_eth_dev_data *data = eth_dev->data;

    ret = hinic3_vf_arg_parse(kvlist, vf_dev, data);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "hinic3 vf arg parse error!");
        return -EPERM;
    }

    ret = hinic3_add_vf_flavors();
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "hinic3 vf flavors add fail!");
        return -EPERM;
    }

    ret = hinic3_init_dev_type(vf_dev);
    if (ret != 0) {
        pci_addr_format(&vf_dev->pci_addr, buff, HWPT_PCI_ADDR_LEN_MAX);
        HINIC3_LOG(ERR, VPORT, "hinic3 init dev [%s] type fail!", buff);
        return -EPERM;
    }

    if (hinic3_is_preload_dev(vf_dev) && (vf_dev->max_queue_num != hinic3_max_queue_num_get())) {
        vf_dev->max_queue_num = hinic3_max_queue_num_get();
        HINIC3_LOG(INFO, VPORT, "reset vf_dev max_queue_num is %u!", vf_dev->max_queue_num);
    }

    ret = hinic3_check_vf_args_valid(&vf_dev->pci_addr, vf_dev->function_id, vf_dev->max_queue_num);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "hinic3_vf_parse_vf_args error!");
        return -EPERM;
    }

    mac_addr = hinic3_rte_zmalloc(HINIC3_PORTS, HINIC3_MAX_UC_MAC_ADDRS * sizeof(struct rte_ether_addr), 0);
    if (!mac_addr) {
        HINIC3_LOG(ERR, VPORT, "Allocate ethernet addresses' memory failed!");
        return -ENOMEM;
    }
    hinic3_vf_dev_init(eth_dev, mac_addr);
    return ret;
}

static int
hinic3_vf_vdev_create(struct rte_vdev_device *vdev_dev, struct rte_kvargs *kvlist)
{
    int ret = 0;
    struct rte_eth_dev *eth_dev = rte_eth_vdev_allocate(vdev_dev, sizeof(struct hinic3_vf_dev));
    if (eth_dev == NULL) {
        HINIC3_LOG(ERR, VPORT, "Failed to allocate the new eth dev for vf!");
        return -ENOMEM;
    }

    ret = hinic3_vf_vdev_init(eth_dev, kvlist);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "vf_vdev_init ERROR!");
        rte_eth_dev_release_port(eth_dev);
        return ret;
    }

    if (hinic3_get_virtual_queue_mode_enabled() == true)
        eth_dev->rx_pkt_burst = hinic3_vf_recv_pkts_in_virtual_queue;
    else
        eth_dev->rx_pkt_burst = hinic3_vf_recv_pkts;
    eth_dev->tx_pkt_burst = hinic3_vf_xmit_pkts;
    eth_dev->dev_ops = g_hinic3_vf_vdev_ops;
    rte_eth_dev_probing_finish(eth_dev);

    return 0;
}

static int
open_str(const char *key __rte_unused, const char *value, void *extra_args)
{
    if (value == NULL || extra_args == NULL)
        return -1;

    struct open_str_extra_args *op_args = (struct open_str_extra_args *)extra_args;
    char *extra_args_str = op_args->extra_args_str;
    size_t extra_args_str_len = op_args->extra_args_str_len;
    size_t value_len = strlen(value);

    if (value_len + 1 > extra_args_str_len) {
        HINIC3_LOG(ERR, VPORT, "open str failed, extra args buffer overflow!");
        return -1;
    }

    strcpy(extra_args_str, value);
    if (extra_args_str[0] == '\0') {
        HINIC3_LOG(ERR, VPORT, "strcpy error!");
        return -1;
    }
    return 0;
}

uint16_t
hinic3_bond_recv_pkts(void *rx_queue, struct rte_mbuf **rx_pkts, uint16_t nb_pkts)
{
    struct hinic3_queue *rxq = (struct hinic3_queue *)rx_queue;
    struct hinic3_bond_dev *bond_dev = NULL;
    struct rte_mbuf *mbuf = NULL;
    uint16_t upcall_num = 0;
    int64_t upcall_byt = 0;

    if (hinic3_queue_valid(rxq) != 0) {
        hinic3_add_error_stats(HINIC3_VPORT_BOND_RX_QUEUE_INVALID, 1);
        return 0;
    }

    upcall_num = hinic3_global_rte_eth_rx_burst(
        rxq->dpdk_port_id, rxq->variant.stdqueue->hiovs_queue_id, rx_pkts, nb_pkts);
    if (upcall_num > 0) {
        bond_dev = (struct hinic3_bond_dev *)hinic3_get_private_data(rxq->dpdk_index_id);
        if (bond_dev == NULL) {
            hinic3_add_error_stats(HINIC3_VPORT_DEV_BOND_RECV_PRIVATE_DATA_NULL, 1);
            return 0;
        }

        for (uint16_t i = 0; i < upcall_num; i++) {
            mbuf = rx_pkts[i];
            if (mbuf != NULL) {
                mbuf->port = rxq->dpdk_index_id;
                upcall_byt += rte_pktmbuf_pkt_len(mbuf);
            }
        }
        rte_atomic64_add(&rxq->pkt_stats, upcall_num);
        rte_atomic64_add(&bond_dev->bond_upcall_pk_num, upcall_num);
        rte_atomic64_add(&bond_dev->bond_upcall_pk_byt, upcall_byt);
    }

    return upcall_num;
}

uint16_t
hinic3_bond_xmit_pkts(void *tx_queue, struct rte_mbuf **tx_pkts, uint16_t nb_pkts)
{
    struct hinic3_queue *txq = tx_queue;
    struct hinic3_bond_dev *bond_dev = NULL;
    struct rte_mbuf *mbuf = NULL;
    uint16_t reinject_num = 0;
    int64_t reinject_byt = 0;
    bool pmd_pktinfo_free = hinic3_check_masked_to_exact_switch() &&
        hinic3_forward_mode_get() != OVS_KEY_EXTRACT_EXTEND_MODE_7TUPLE;
                                    
    if (hinic3_queue_valid(txq) != 0) {
        hinic3_add_error_stats(HINIC3_VPORT_BOND_TX_QUEUE_INVALID, 1);
        return 0;
    }
    hinic3_set_vport_id_into_userdata(txq->vport_id, tx_pkts, nb_pkts);

    if (pmd_pktinfo_free == true) {
        for (uint16_t i = 0; i < nb_pkts; i++)
            hinic3_free_pmd_pkt_info((struct hinic3_inner_metadata *)(&tx_pkts[i]->dynfield1[1]));
    }

    reinject_num = hinic3_global_rte_eth_tx_burst(txq->dpdk_port_id, txq->queue_id, tx_pkts, nb_pkts);
    if (reinject_num > 0) {
        bond_dev = (struct hinic3_bond_dev *)hinic3_get_private_data(txq->dpdk_index_id);
        if (bond_dev == NULL) {
            hinic3_add_error_stats(HINIC3_VPORT_DEV_BOND_XMIT_PRIVATE_DATA_NULL, 1);
            return 0;
        }

        for (uint16_t i = 0; i < reinject_num; i++) {
            mbuf = tx_pkts[i];
            if (mbuf != NULL)
                reinject_byt += rte_pktmbuf_pkt_len(mbuf);
        }

        rte_atomic64_add(&txq->pkt_stats, reinject_num);
        rte_atomic64_add(&bond_dev->bond_reinject_pk_num, reinject_num);
        rte_atomic64_add(&bond_dev->bond_reinject_pk_byt, reinject_byt);
    }

    return reinject_num;
}

static void
hinic3_bond_dev_init(struct rte_eth_dev *eth_dev, struct rte_ether_addr *mac_addr)
{
    struct hinic3_bond_dev *bond_dev = hinic3_ethdev_get_bond_private(eth_dev);
    struct rte_eth_dev_data *data = eth_dev->data;

    data->dev_started = HINIC3_DEV_STATE_STOP;
    data->dev_link = g_hinic3_bond_vdev_link;
    data->dev_flags |= HINIC3_ETH_DEV_FLOW_OPS_THREAD_SAFE;
    data->nb_rx_queues = HINIC3_INVALID_QUEUE_NUM;
    data->nb_tx_queues = HINIC3_INVALID_QUEUE_NUM;
    data->mac_addrs = mac_addr;
    data->mtu = HINIC3_BOND_DEFAULT_MTU;
    bond_dev->mtu = HINIC3_BOND_DEFAULT_MTU;
    bond_dev->n_txq = data->nb_tx_queues;
    bond_dev->n_upcall_queue = data->nb_rx_queues;
    bond_dev->bond_id = HINIC3_PORT_ID_INVALID;
    bond_dev->vport_id = HINIC3_PORT_ID_INVALID;
    bond_dev->dpdk_port_id = INVALID_DPDK_PORT_ID;
    memset(&bond_dev->uplink_pci_addr, 0, sizeof(struct rte_pci_addr));
    /* set all upcall queue id invalid */
    for (int i = 0; i < MAX_RX_QUEUE_PER_VPORT; i++) {
        bond_dev->upcall_queue.upcall_queue_id[i] = INVALID_UPCALL_QUEUE_ID;
        bond_dev->upcall_queue.is_queue_valid[i] = false;
    }

    rte_atomic64_init(&bond_dev->bond_upcall_pk_num);
    rte_atomic64_init(&bond_dev->bond_upcall_pk_byt);
    rte_atomic64_init(&bond_dev->bond_reinject_pk_num);
    rte_atomic64_init(&bond_dev->bond_reinject_pk_byt);
}

int
hinic3_bond_vdev_init(struct rte_eth_dev *eth_dev, struct rte_kvargs *kvlist)
{
    int ret = 0;
    char priority_upcall_str[PRIORITY_ENABLE_LEN] = {0};
    uint32_t mac_size = 0;
    struct rte_ether_addr *mac_addr = NULL;
    struct hinic3_bond_dev *bond_dev = hinic3_ethdev_get_bond_private(eth_dev);

    if (bond_dev == NULL) {
        HINIC3_LOG(ERR, VPORT, "Failed to get pf dev!");
        return -EPERM;
    }

    struct open_str_extra_args bond_name = {0};
    bond_name.extra_args_str = bond_dev->bond_name;
    bond_name.extra_args_str_len = MAX_NAME;
    struct open_str_extra_args priority_upcall;
    priority_upcall.extra_args_str = priority_upcall_str;
    priority_upcall.extra_args_str_len = PRIORITY_ENABLE_LEN;

    ret = rte_kvargs_process(kvlist, HINIC3_BOND_NAME, &open_str, &bond_name);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "Failed to obtain the bond_name!");
        return -EPERM;
    }

    ret = rte_kvargs_process(kvlist, HINIC3_PRIORITY_UPCALL, &open_str,  &priority_upcall);
    if (ret == 0 && strcmp(priority_upcall_str, "enable") == 0)
        bond_dev->priority_upcall = true;

    /* bond_name is 0 or more than max_name */
    if ((strnlen(bond_dev->bond_name, MAX_NAME) == 0) || (strnlen(bond_dev->bond_name, MAX_NAME) >= MAX_NAME)) {
        HINIC3_LOG(ERR, VPORT, "bond name invalid length!");
        return -EPERM;
    }

    mac_size = HINIC3_MAX_UC_MAC_ADDRS * sizeof(struct rte_ether_addr);
    mac_addr = hinic3_rte_zmalloc(HINIC3_PORTS, mac_size, 0);
    if (!mac_addr) {
        HINIC3_LOG(ERR, VPORT, "Allocate ethernet addresses' memory failed!");
        return -ENOMEM;
    }

    hinic3_bond_dev_init(eth_dev, mac_addr);
    return ret;
}

static int
hinic3_bond_vdev_create(struct rte_vdev_device *vdev_dev, struct rte_kvargs *kvlist)
{
    int ret = 0;
    struct rte_eth_dev *eth_dev = rte_eth_vdev_allocate(vdev_dev, sizeof(struct hinic3_bond_dev));
    if (eth_dev == NULL) {
        HINIC3_LOG(ERR, VPORT, "Failed to allocate the new eth dev for bond!");
        return -ENOMEM;
    }

    ret = hinic3_bond_vdev_init(eth_dev, kvlist);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "bond_vdev_init ERROR!");
        rte_eth_dev_release_port(eth_dev);
        return ret;
    }

    eth_dev->rx_pkt_burst = hinic3_bond_recv_pkts;
    eth_dev->tx_pkt_burst = hinic3_bond_xmit_pkts;
    eth_dev->dev_ops = g_hinic3_bond_vdev_ops;
    rte_eth_dev_probing_finish(eth_dev);

    return 0;
}

static int
hinic3_vdev_probe(struct rte_vdev_device *vdev_dev)
{
    if (vdev_dev == NULL) {
        HINIC3_LOG(ERR, VPORT, "vdev_dev is null!");
        return -EINVAL;
    }

    int ret = 0;
    struct rte_kvargs *kvlist = NULL;
    struct rte_device *device = &vdev_dev->device;
    struct rte_devargs *devargs = device->devargs;
    if (devargs == NULL || devargs->args == NULL) {
        HINIC3_LOG(ERR, VPORT, "vdev probe, get devargs is null!");
        return -EINVAL;
    }

    kvlist = rte_kvargs_parse(devargs->args, g_hinic3_vdev_valid_arguments);
    if (kvlist == NULL) {
        HINIC3_LOG(ERR, VPORT, "vdev probe, kvargs parse failed!");
        return -EPERM;
    }

    if (rte_kvargs_count(kvlist, HINIC3_BOND_NAME) != 0)
        ret = hinic3_bond_vdev_create(vdev_dev, kvlist);
    else
        ret = hinic3_vf_vdev_create(vdev_dev, kvlist);

    rte_kvargs_free(kvlist);
    return ret;
}

void
hinic3_bond_vdev_uninit(struct rte_eth_dev *dev)
{
    hinic3_bond_dev_close(dev);

    dev->dev_ops = NULL;
    dev->rx_pkt_burst = NULL;
    dev->tx_pkt_burst = NULL;
}

void
hinic3_vf_vdev_uninit(struct rte_eth_dev *dev)
{
    struct rte_eth_dev_data *data = dev->data;

    dev->rx_pkt_burst = NULL;
    dev->tx_pkt_burst = NULL;
    dev->dev_ops = NULL;

    data->dev_started = HINIC3_DEV_STATE_STOP;

    struct hinic3_vf_dev *vf_dev = hinic3_ethdev_get_vf_private(dev);
    hinic3_smap_destroy(&vf_dev->qos_options);

    if (data->mac_addrs != NULL) {
        hinic3_rte_free(data->mac_addrs);
        data->mac_addrs = NULL;
    }
}

static int
hinic3_vdev_remove(struct rte_vdev_device *vdev_dev)
{
    if (vdev_dev == NULL)
        return -EINVAL;

    struct rte_eth_dev *eth_dev = rte_eth_dev_allocated(rte_vdev_device_name(vdev_dev));
    if (eth_dev == NULL)
        return 0;

    struct rte_kvargs *kvlist = NULL;
    struct rte_device *device = &vdev_dev->device;
    struct rte_devargs *devargs = device->devargs;
    if (devargs == NULL || devargs->args == NULL) {
        HINIC3_LOG(ERR, VPORT, "vdev remove, get devargs is null!");
        return -EINVAL;
    }

    kvlist = rte_kvargs_parse(devargs->args, g_hinic3_vdev_valid_arguments);
    if (kvlist == NULL) {
        HINIC3_LOG(ERR, VPORT, "vdev remove, kvargs parse failed!");
        return -EPERM;
    }

    if (rte_kvargs_count(kvlist, HINIC3_BOND_NAME) != 0)
        hinic3_bond_vdev_uninit(eth_dev);
    else
        hinic3_vf_vdev_uninit(eth_dev);
    rte_eth_dev_release_port(eth_dev);

    rte_kvargs_free(kvlist);
    return 0;
}

static struct rte_vdev_driver *g_vdev_driver = NULL;

static void
hinic3_vdev_driver_construct(void)
{
    struct rte_vdev_driver *driver = NULL;

    driver = hinic3_calloc(1, sizeof(struct rte_vdev_driver), HINIC3_PORTS);
    if (driver == NULL)
        return;

    driver->probe = hinic3_vdev_probe;
    driver->remove = hinic3_vdev_remove;

    g_vdev_driver = driver;
}

static void
hinic3_vdev_driver_destroy(void)
{
    if (g_vdev_driver == NULL)
        return;

    hinic3_free(g_vdev_driver);
    g_vdev_driver = NULL;
}

static const char *vdrvinit_net_vdev_alias;

static void
hinic3_rte_register_vdev(void)
{
    g_vdev_driver->driver.name = RTE_STR(net_hwsp);
    g_vdev_driver->driver.alias = vdrvinit_net_vdev_alias;
    if (hinic3_get_agent_construct_init() == false)
        rte_vdev_unregister(g_vdev_driver);
    else
        rte_vdev_register(g_vdev_driver);
}

int
hinic3_vdev_port_module_init(void)
{
    int ret = hinic3_queue_init();
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "queue init failed with %d", ret);
        return ret;
    }

    hinic3_bond_flow_ops_construct();
    hinic3_bond_dev_ops_construct();
    hinic3_vf_flow_ops_construct();
    hinic3_vf_dev_ops_construct();
    hinic3_vdev_driver_construct();

    hinic3_rte_register_vdev();
    return 0;
}

void
hinic3_vdev_port_module_uninit(void)
{
    hinic3_vdev_driver_destroy();
    hinic3_vf_dev_ops_destroy();
    hinic3_vf_flow_ops_destroy();
    hinic3_bond_dev_ops_destroy();
    hinic3_bond_flow_ops_destroy();
    hinic3_queue_uninit();
}
