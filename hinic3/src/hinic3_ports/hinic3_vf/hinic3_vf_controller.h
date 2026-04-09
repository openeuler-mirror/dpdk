/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2025 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_VF_CONTROLLER_H
#define HINIC3_VF_CONTROLLER_H

#include <stdbool.h>
#include <sys/queue.h>
#include <ethdev_driver.h>
#include "rte_log.h"
#include "rte_ring.h"
#include "rte_malloc.h"
#include "rte_ethdev.h"
#include "rte_bus_pci.h"
#include "hinic3_eth_util.h"
#include "hinic3_vf_mgmt.h"
#include "hinic3_queue.h"
#include "hinic3_age_delete_flow.h"

#define HINIC3_VF_DEFAULT_NB_QUEUES             1
#define HINIC3_MAX_QOS_GROUP_NAME               512
#define HINIC3_VF_VNI_DEFAULT                   0
#define HINIC3_VF_SECURITY_SMAC_INVALID         0
#define HINIC3_VF_SECURITY_SMAC_SINGLE          1
#define HINIC3_VF_SECURITY_BRD_LIMIT_INVALID    0
#define HINIC3_VF_SECURITY_ETH_GROUP_INVALID    0
#define HINIC3_VF_MTU_MAX                       9216
#define HINIC3_VF_MTU_MIN                       256
#define HINIC3_VF_MAX_QUEUE_NUM                 32
#define HINIC3_VF_UPCALL_NUM                    16
#define VNI_INVALID                            (1UL << 24)
#define HINIC3_DEFAULT_MTU                      1500
#define HINIC3_VF_SIZE_OFFSET                   26   /* eth头(14) + 内层vlan(4) * 2 + FCS(4) */
#define HINIC3_SET_VF_STATE_DOWN                0
#define HINIC3_SET_VF_STATE_UP                  1
#define HINIC3_MAX_PORT_NAME_LEN                32
#define HINIC3_MAX_MAC_ADDR_LEN                 18
#define INVALID_MTR_ID                          0xFFFF
#define INVALID_MTR_DIR                         4
#define HINIC3_MTR_NUM                          4
#define HINIC3_PORT_TYPE_NO_QOS                 (-2)
#define HINIC3_MAX_MAC_ADDRS                    1
#define HINIC3_MAX_HASH_MAC_ADDRS               0
#define HINIC3_MAX_VFS                          0
#define HINIC3_MAX_VMDQ_POOLS                   0
#define HW_ETH_DEVICE_STATUS                   (1U << 2)

#define HINIC3_ETH_RX_OFFLOAD_SCATTER           (1UL << 13)
#define HINIC3_ETH_TX_OFFLOAD_MULTI_SEGS        (1UL << 15)
#define HINIC3_ETH_TX_OFFLOAD_OUTER_IPV4_CKSUM  (1UL << 7)
#define HINIC3_ETH_TX_OFFLOAD_OUTER_UDP_CKSUM   (1UL << 20)
#define HINIC3_ETH_TX_OFFLOAD_CAPA ( \
    HINIC3_ETH_TX_OFFLOAD_MULTI_SEGS | \
    HINIC3_ETH_TX_OFFLOAD_OUTER_IPV4_CKSUM | \
    HINIC3_ETH_TX_OFFLOAD_OUTER_UDP_CKSUM)

#define HINIC3_ETH_RSS_IPV4                     (1UL << 2)
#define HINIC3_ETH_RSS_FRAG_IPV4                (1UL << 3)
#define HINIC3_ETH_RSS_NONFRAG_IPV4_TCP         (1UL << 4)
#define HINIC3_ETH_RSS_NONFRAG_IPV4_UDP         (1UL << 5)
#define HINIC3_ETH_RSS_IPV6                     (1UL << 8)
#define HINIC3_ETH_RSS_FRAG_IPV6                (1UL << 9)
#define HINIC3_ETH_RSS_NONFRAG_IPV6_TCP         (1UL << 10)
#define HINIC3_ETH_RSS_NONFRAG_IPV6_UDP         (1UL << 11)
#define HINIC3_ETH_RSS_IPV6_EX                  (1UL << 15)
#define HINIC3_ETH_RSS_IPV6_TCP_EX              (1UL << 16)
#define HINIC3_ETH_RSS_VXLAN                    (1UL << 19)

#define HINIC3_ETH_SUPPORT_RSS ( \
    HINIC3_ETH_RSS_IPV4 | \
    HINIC3_ETH_RSS_FRAG_IPV4 | \
    HINIC3_ETH_RSS_NONFRAG_IPV4_TCP | \
    HINIC3_ETH_RSS_NONFRAG_IPV4_UDP | \
    HINIC3_ETH_RSS_IPV6 | \
    HINIC3_ETH_RSS_FRAG_IPV6 | \
    HINIC3_ETH_RSS_NONFRAG_IPV6_TCP | \
    HINIC3_ETH_RSS_NONFRAG_IPV6_UDP | \
    HINIC3_ETH_RSS_IPV6_EX | \
    HINIC3_ETH_RSS_IPV6_TCP_EX | \
    HINIC3_ETH_RSS_VXLAN)

enum hinic3_vf_config_state {
    HINIC3_VF_CONFIG_ADD,
    HINIC3_VF_CONFIG_DEL,
    HINIC3_VF_CONFIG_STABLE,
    HINIC3_VF_CONFIG_NONE,
};

enum hinic3_vf_config {
    HINIC3_VF_CONFIG_VNI,
    HINIC3_VF_CONFIG_BUCKET_ID,
    HINIC3_VF_CONFIG_SMAC,
    HINIC3_VF_CONFIG_ETH_GROUP,
    HINIC3_VF_CONFIG_BRD_RATE_LIMIT,
    HINIC3_VF_CONFIG_MAX,
};

struct hinic3_mtr {
    uint32_t direction;
    uint32_t mtr_id;
    bool is_used;
};

enum hinic3_phy_dev_type {
    HINIC3_PHY_DEV_TYPE_PF,
    HINIC3_PHY_DEV_TYPE_VF
};

/* 此处将bond口及VF端口私有数据的首个uint16数据作为端口的vport_id，注意不要更换vport_id的位置 */
struct hinic3_vf_dev {
    uint16_t vport_id;
    uint8_t n_txq;
    uint8_t n_upcall_queue;
    uint16_t dpdk_port_id;
    uint16_t port_ifindex;
    uint32_t vni;
    uint32_t mtu;
    bool attach_mtu;
    uint32_t max_queue_num;
    struct hinic3_upcall_queue upcall_queue;
    struct hinic3_reinject_queue reinject_queue;
    struct eth_addr mac_addr;
    struct rte_pci_addr pci_addr;
    uint16_t function_id;
    uint32_t qos_type;
    char group_qos_name[HINIC3_MAX_QOS_GROUP_NAME + 1];
    struct smap qos_options;
    uint16_t qos_id;
    uint16_t group_qos_id;
    struct hinic3_list profiles;
    struct hinic3_mtr mtr[HINIC3_MTR_NUM];
    uint32_t brd_rate_limit;
    uint16_t eth_type_group_id;
    struct eth_addr src_mac_list[BUM_SMAC_MAX_COUNT];
    uint16_t src_mac_num;
    enum hinic3_vf_config_state state[HINIC3_VF_CONFIG_MAX];
    rte_atomic64_t vf_upcall_pk_num;
    rte_atomic64_t vf_upcall_pk_byt;
    rte_atomic64_t vf_reinject_pk_num;
    rte_atomic64_t vf_reinject_pk_byt;
    struct rte_eth_dev *dev;
    uint8_t share_upcall;
    uint16_t virtio_queue_depth;
    struct rte_aged_flow_list aged_flow_list;
    enum hinic3_phy_dev_type dev_type;
};

static inline bool
hinic3_is_preload_dev(struct hinic3_vf_dev *vf_dev)
{
    return (hinic3_is_preload_pf_port() && vf_dev->dev_type == HINIC3_PHY_DEV_TYPE_PF) ||
           (hinic3_is_preload_vf_port() && vf_dev->dev_type == HINIC3_PHY_DEV_TYPE_VF);
}

static inline struct hinic3_vf_dev *
hinic3_ethdev_get_vf_private(struct rte_eth_dev *dev)
{
    return (struct hinic3_vf_dev *)dev->data->dev_private;
}

void hinic3_vf_flow_ops_construct(void);
void hinic3_vf_flow_ops_destroy(void);

/* 获取设备信息 */
int hinic3_vf_dev_infos_get(struct rte_eth_dev *dev, struct rte_eth_dev_info *info);
/* 对设备进行同步设置 */
int hinic3_vf_dev_configure(struct rte_eth_dev *dev);
/* 设备启动 */
int hinic3_vf_dev_start(struct rte_eth_dev *dev);
/* 设备停止 */
int hinic3_vf_dev_stop(struct rte_eth_dev *dev);
/* 设置设备的连接状态为down */
int hinic3_vf_set_link_down(struct rte_eth_dev *dev);
/* 设置设备的连接状态为up */
int hinic3_vf_set_link_up(struct rte_eth_dev *dev);
/* 设备退出 */
int hinic3_vf_dev_close(struct rte_eth_dev *dev);
/* 更新设备连接状态 */
int hinic3_vf_link_update(struct rte_eth_dev *dev, int wait_to_complete);
/* 设置设备的mtu */
int hinic3_vf_dev_set_mtu(struct rte_eth_dev *dev, uint16_t mtu);

/* 释放设备的rx队列 */
void hinic3_vf_rx_queue_release(struct rte_eth_dev *dev, uint16_t queue_id);
/* 释放设备的tx队列 */
void hinic3_vf_tx_queue_release(struct rte_eth_dev *dev, uint16_t queue_id);

/* 设置设备的rx队列 */
int hinic3_vf_rx_queue_setup(struct rte_eth_dev *dev, uint16_t idx, uint16_t desc,
    unsigned int socket_id, const struct rte_eth_rxconf *conf, struct rte_mempool *mp);
/* 设置设备的tx队列 */
int hinic3_vf_tx_queue_setup(struct rte_eth_dev *dev, uint16_t idx, uint16_t desc,
    unsigned int socket_id, const struct rte_eth_txconf *conf);
/* rte_flow接口 */
int hinic3_vf_dev_flow_ops_get(struct rte_eth_dev *dev __rte_unused, const struct rte_flow_ops **ops);

/* 设置端口mac地址 */
int hinic3_vf_dev_mac_addr_set(struct rte_eth_dev *dev, struct rte_ether_addr *mac_addr);
/* 初始化PCI设备列表 */
int hinic3_init_pci_list(struct hovs_phy_dev_info *dev, struct hinic3_vf_dev *vf_dev, uint32_t dev_num);

int hinic3_share_upcall_set(uint8_t queue_num);
void hinic3_share_upcall_incress(void);
void hinic3_share_upcall_decress(void);
uint8_t hinic3_get_vf_share_upcall_num(void);
uint16_t hinic3_get_vf_share_upcall_ref_cnt(void);
int hinic3_vf_dynamic_port_add(struct hinic3_vf_dev *vf_dev);
#endif
