/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026-2027 Huawei Technologies Co., Ltd
 */

#ifndef _HNS5_ETH_H_
#define _HNS5_ETH_H_

#include <rte_ethdev.h>
#include <rte_ethdev_driver.h>
#include <rte_spinlock.h>

#include "native.h"
#include "ndrv_mag_api.h"
#include "ndrv_poe_vf_api.h"
#include "hidrv_mag_api.h"
#include "ppe_type_api.h"
#include "ppe_ppp_type_api.h"

#include "hns5_flow.h"
#include "hns5_version.h"
#include "hns5_mempool.h"
#ifdef __cplusplus
extern "C" {
#endif

#define HNS5_ETH_MAX_RXQ_NUM   64
#define HNS5_ETH_MAX_TXQ_NUM   64
#define HNS5_FWL_TNL_MAX_NUM   8           /* fw_tnl channels per cport */

#define HNS5_ETH_RX_BURST_CNT  32
#define HNS5_ETH_TX_BURST_CNT  32
#define HNS5_RSS_KEY_SIZE      40          /* RSS hash key size in bytes */
#define HNS5_RSS_RETA_SIZE     1024        /* RSS redirection table size */
#define HNS5_RSS_INDIRECT_TABLE_SIZE 1024  /* RSS indirect table size */
#define HNS5_ETH_ALEN 6

#define BIT(nr) (1UL << (nr))

#define hns5_set_field(origin, mask, shift, val) \
    do { \
        (origin) &= (~(mask)); \
        (origin) |= ((val) << (shift)) & (mask); \
    } while (0)
#define hns5_get_field(origin, mask, shift) (((origin) & (mask)) >> (shift))
#define hns5_set_bit(origin, shift, val) hns5_set_field((origin), (0x1UL << (shift)), (shift), (val))
#define hns5_get_bit(origin, shift) hns5_get_field((origin), (0x1UL << (shift)), (shift))

/**
 * @brief fw_tnl (hardware tunnel dispatch) config, vqi=base_vqi+qos selects target queue
 * @note Copied from lib/librte_ethdev/rte_ethdev.h into this driver; the public
 *       definition will be removed. Self-contained, do not use the lib definition
 */
struct rte_fw_tnl_conf {
    uint64_t enable : 1;
    uint64_t base_tnl : 12;
    uint64_t tnl_num : 4;
    uint64_t base_vqi : 12; // vqi=base_vqi+qos
    uint64_t qos_sel : 4;
    uint64_t def_qos : 3; // valid 0-7
    uint64_t def_qos_inc : 1; // default qos auto-increment
    uint64_t rsvd : 27;
};

#define HNS5_ETH_RXQ_MODE_QUEUE 0   /* per-queue POE input, supports queue start/stop */
#define HNS5_ETH_RXQ_MODE_GROUP 1   /* group-level POE input, per-queue start/stop unsupported */

struct hns5_eth_port_param
{
    uint32_t mport_id;                      // mac port
    uint32_t cport_id;                      // cpu port
    enum hidrv_mag_port_mode port_mode;     // mac mode
    enum hidrv_mag_port_speed port_speed;   // mac speed
    enum hidrv_mag_duplex_mode duplex_mode; // duplex mode
    uint32_t autoneg;                       // /**< RTE_ETH_LINK_[AUTONEG/FIXED] */
    uint32_t hw_txq_start;                  // TX TM port start queue id, default=cport_id*16 (TM maps 16x port)
    uint32_t hw_txq_num;                    // TX TM port queue count, default 16
    uint32_t max_frame_len;
    uint32_t min_frame_len;
    uint8_t fw_tnl_enable;                  // fw_tnl enable: default 1 (devargs optional), set fw_tnl=0 to disable
    uint32_t share_group;                   // RX shared group, devargs required (share_group=0~50), assigned externally to avoid conflicts
    uint32_t rxq_mode;                      // RX queue mode: HNS5_ETH_RXQ_MODE_QUEUE (default) or _GROUP
    uint32_t base_tnl;                      // fw_tnl base, default=cport_id*8 (step HNS5_POE_CPU_GRP_QUE_NUM, 8 per cport), devargs override
};

struct hns5_eth_txq_stats {
    uint64_t opackets;                      // TX packets count
    uint64_t obytes;                        // TX bytes count
    uint64_t oerrors_txd;                   // PPE TXD errors
    uint64_t oerrors_msg;                   // POE msg errors
    uint64_t oerrors_cache_full;            // cacheable queue errors
};

struct hns5_eth_rxq_stats {
    uint64_t ipackets;                      // RX packets count
    uint64_t ibytes;                        // RX bytes count
    uint64_t ierrors_msg;                   // POE msg errors
    uint64_t ierrors_rxd;                   // PPE RXD errors
};

/* PPE hardware drop counters, exposed as "ppe_*" xstats. */
struct hns5_eth_drop_stats {
    /* SSU packet-drop counters */
    uint64_t ssu_ppp_send_drop_cmd;
    uint64_t ssu_recv_drop_cmd;
    uint64_t ssu_rx_pkt_full_drop;
    /* PPP stage-1 flow-table drop (rte_flow DROP without ingress attr) */
    uint64_t ppp_fw_fd_stg1_drop;
    /* LSW ingress flow-table drop (rte_flow DROP with ingress attr) */
    uint64_t lsw_ing_fd_drop;
    /* RPU (RX engine, packets on the way to CPU) drop counters */
    uint64_t rpu_buf_drop_int;
    uint64_t rpu_recv_err_pkt;
    uint64_t rpu_prep_drop_pkt;
    /* TM VF tail drop (queue full after the queue overflow DDR is exhausted) */
    uint64_t tm_vf_tail_drop;
    /* POE VF add-fail drop (group full / true-full / retry timeout) */
    uint64_t poe_add_fail_drop;
    /* PPP LSW L2 table lookup miss counter (64-bit) */
    uint64_t l2_dmac_miss_drop;
};

struct hns5_eth_port_stats {
    struct hidrv_mag_mac_mib_stats mib_stats;
    struct hns5_eth_rxq_stats rxq_stats;
    struct hns5_eth_txq_stats txq_stats;
    struct hns5_eth_drop_stats drop_stats;
};

enum hns5_eth_rxq_type {
    HNS5_ETH_RXQ_TYPE_GROUP,
    HNS5_ETH_RXQ_TYPE_QUEUE,
    HNS5_ETH_RXQ_TYPE_INVALID,
};

#define HNS5_ETH_MAX_MC_ADDR_NUM 32   /* max multicast address count */

/**
 * @brief multicast address management info
 */
struct hns5_mc_info {
    uint32_t num;                                                       /* current multicast address count */
    struct rte_ether_addr pool[HNS5_ETH_MAX_MC_ADDR_NUM];              /* multicast address pool */
    uint8_t grp_inited;                                                 /* default mcast group (mgid=cport_id) configured */
};

struct hns5_eth_rx_queue {
    enum hns5_eth_rxq_type type;           // queue type
    uint16_t group;                         // RX group
    uint16_t queue;                         // RX queue, special queues see HNS5_ETH_RXQ_BY_*
    uint16_t pool_id;                       // RX mempool
    uint16_t port_id;                       // DPDK port id
    handle poe_vfhandle;                    // perf: set at queue init
    uint8_t bp_drop_on;                     // 1: queue watermark + MAM source drop enabled
    uint8_t poe_enabled;                    // 1: this port's POE queue input is enabled
    struct hns5_eth_rxq_stats stats;
    bool is_hairpin;                        // rx queue used as hairpin
    struct rte_eth_hairpin_conf hairpin_conf;
    struct rte_mempool *mp;                 // RX mempool
    uint16_t nb_rx_desc;                    // configured RX descriptor count
    struct rte_eth_rxconf conf;             // RX queue config, reported by rxq info
} __rte_cache_aligned;

struct hns5_eth_tx_queue {
    union ndrv_poe_cpu_add_msg msgs[HNS5_ETH_TX_BURST_CNT] __rte_cache_aligned;    // perf: faster than local variable
    union ndrv_poe_cpu_add_msg default_tx_msg __rte_cache_aligned;                  // perf: reduce msg fill time per TX
    uint16_t port_id;                       // DPDK port id
    uint32_t tm_port;                       // tm port
    uint32_t tm_queue_id;                   // tm queue
    uint32_t vcpu;                          // vcpu for TX poe msg, bound in tx path
    handle poe_vfhandle;                    // perf: set at queue init
    struct hns5_eth_priv *priv;            /* priv ptr, TX path reads port-level VLAN insert flag */
    volatile uint8_t stopped;               // data plane stop gate
    struct hns5_eth_txq_stats stats;
    bool is_hairpin;                        // tx queue used as hairpin
    struct rte_eth_hairpin_conf hairpin_conf;
    uint16_t nb_tx_desc;                    // configured TX descriptor count
    struct rte_eth_txconf conf;             // TX queue config, reported by txq info
} __rte_cache_aligned;

struct hns5_flow {
    LIST_ENTRY(hns5_flow) next;
    struct hns5_fdir_rule_ele *fdir_rule;
};

struct hns5_rss_info {
    uint64_t hf;                                 /* RSS hash function bitmask */
    uint8_t key[HNS5_RSS_KEY_SIZE];             /* RSS hash key */
    uint8_t key_len;                             /* RSS hash key length */
    uint8_t algo;                                /* RSS hash algorithm */
    uint16_t reta[HNS5_RSS_RETA_SIZE];          /* RSS redirection table */
    uint16_t reta_size;                          /* RSS redirection table size */
    uint8_t tc_valid;                            /* RSS TC valid bitmask (bit per TC) */
    uint8_t tc_mode[8];                          /* RSS TC mode data (tc_offset per TC) */
};

struct hns5_eth_priv {
    struct hns5_eth_rx_queue rxq[HNS5_ETH_MAX_RXQ_NUM];
    struct hns5_eth_tx_queue txq[HNS5_ETH_MAX_TXQ_NUM];
    struct hns5_eth_port_stats xstats __rte_cache_aligned;
    /* mib_inited gates the first MIB read so init traffic is not reported. */
    uint8_t mib_inited;
    /* drop_inited gates the first PPE counter read so init traffic is skipped. */
    uint8_t drop_inited;
    /* TM tail drop is a wrapped 64-bit cumulative value: own baseline. */
    uint64_t tm_vf_tail_drop_last;
    uint8_t tm_vf_tail_drop_inited;
    /* POE add-fail is a wrapping 32-bit cumulative counter: own baseline. */
    uint32_t poe_add_fail_last;
    uint8_t poe_add_fail_inited;
    struct rte_ether_addr address;
    struct hns5_eth_port_param param;
    int ppe_pf_devid;
    int ppe_vf_devid;
    handle ppe_pfhandle;
    handle ppe_vfhandle;
    int poe_pf_devid;
    int poe_vf_devid;
    handle poe_pfhandle;
    handle poe_vfhandle;
    int mag_pf_devid;
    handle mag_handle;
    /* link status interrupts */
    int intr_fd;                        /* LSC中断fd，小于0表示未申请 */
    struct rte_intr_handle intr_handle; /* LSC中断句柄 */
    uint32_t last_link_status;          /* 已上报的链路状态 */
    int alarm_cancel;                   /* teardown flag, accessed via __atomic */
    /* promiscuous mode */
    uint8_t promisc_mode;
    /* flow */
    struct hns5_fdir_info fdir;
    LIST_HEAD(flows, hns5_flow) flow_list;
    LIST_HEAD(counters, hns5_flow_counter) flow_counters;
    uint64_t pool_inited;
    /* rss */
    struct hns5_rss_info rss;
    /* tc */
    uint8_t num_tc;             /* enabled TC count, default 1 */
    uint8_t hw_tc_map;          /* TC bitmap, default 0x01 (TC0 only) */
    /* fw_tnl */
    uint8_t fwl_tnl_inited;     /* fw_tnl init flag: 0 uninit, 1 init */
    struct rte_fw_tnl_conf fwl_tnl_conf; /* first fw_tnl config saved */
    /* vlan */
    uint16_t port_def_vlan;         /* port default VLAN (tx_vlan_set/pvid config) */
    uint8_t vlan_pvid_en;           /* port-level VLAN insert (pvid) enable: 0 off, 1 on */
    uint32_t vlan_insert_flow_entry; /* VLAN insert ingress flow table entry */
    uint8_t vlan_insert_flow_added;  /* VLAN insert flow table added: 0 no, 1 yes */
    uint32_t mcast_mode_flow_entry; /* mcast mode flow entry (matches DMAC I/G bit) */
    uint8_t mcast_mode_flow_added;  /* mcast mode flow table added: 0 no, 1 yes */
    /* crc offload */
    uint8_t crc_offload;        /* CRC offload enable: 0 off, 1 on */
    /* poe->ppe queue backpressure / per-queue watermark */
    uint8_t bp_grp;             /* claimed bp_ppe slot, HNS5_POE_BP_SLOT_INVALID if none */
    uint32_t bp_ppe_th;         /* group share budget (packets) used to split per-queue quota */
};

static __rte_always_inline struct hns5_eth_priv *hns5_eth_priv_get(struct rte_eth_dev *dev)
{
    return dev->data->dev_private;
}

static __rte_always_inline struct rte_eth_dev_data *hns5_eth_get_dev_data(struct rte_eth_dev *dev)
{
    return (struct rte_eth_dev_data *)dev->data;
}

int hns5_get_fixed_mac(struct rte_ether_addr *mac);

/**
 * @brief Check whether port_id is a valid port driven by this PMD
 * @param[in] port_id  port id to check
 * @return true for a valid hns5 port; false for unprobed, released or non-hns5 ports
 * @note Safe to call with any uint16_t value; no global array dereference happens
 *       before the validity check.
 */
bool hns5_eth_dev_is_hns5(uint16_t port_id);

/*
 * @return true when another valid hns5 port shares the same PPE device as @dev.
 *         Such ports share a single RSS indirect table.
 */
bool hns5_eth_has_ppe_peer(struct rte_eth_dev *dev);

int hns5_lsw_mac_table_write(uint8_t port_id, struct rte_ether_addr *mac);
int hns5_set_ppe_bypass_mode(uint8_t port_id, enum hidrv_ppe_bypass_mode mode);
int hns5_promiscuous_enable(struct rte_eth_dev *dev);
int hns5_promiscuous_disable(struct rte_eth_dev *dev);
int hns5_allmulticast_enable(struct rte_eth_dev *dev);
int hns5_allmulticast_disable(struct rte_eth_dev *dev);
int hns5_vlan_pvid_set(struct rte_eth_dev *dev, uint16_t vlan_id, int on);

/**
 * @brief Set device multicast address list (ethdev ops callback, testpmd mcast_addr_add/del)
 * @param[in] dev          eth device
 * @param[in] mc_addr_set  multicast addresses (NULL with nb=0 clears all)
 * @param[in] nb_mc_addr   multicast address count
 * @return 0 on success, non-zero on failure
 * @note Adds an L2 entry per multicast address with mcast action
 *       (des_type=NDRV_PPE_LSW_L2_DES_MPORT_MCAST), mgid defaults to own cport_id,
 *       default group receives only for itself (cpu_port_bitmap has own cport only)
 */
int hns5_dev_set_mc_addr_list(struct rte_eth_dev *dev,
    struct rte_ether_addr *mc_addr_set, uint32_t nb_mc_addr);

/**
 * @brief Configure mcast replication table port group (overwrites)
 * @param[in] dev             eth device
 * @param[in] mgid            multicast id
 * @param[in] mac_port_bitmap MAC port bitmap, one bit per port
 * @param[in] cpu_port_bitmap CPU port bitmap, one bit per port
 * @return 0 on success, non-zero on failure
 */
int hns5_dev_mcast_set_port_grp(struct rte_eth_dev *dev, uint32_t mgid,
    uint64_t mac_port_bitmap, uint64_t cpu_port_bitmap);

/**
 * @brief Get mcast replication table port group
 * @param[in] dev             eth device
 * @param[in] mgid            multicast id
 * @param[out] mac_port_bitmap MAC port bitmap
 * @param[out] cpu_port_bitmap CPU port bitmap
 * @return 0 on success, non-zero on failure
 */
int hns5_dev_mcast_get_port_grp(struct rte_eth_dev *dev, uint32_t mgid,
    uint64_t *mac_port_bitmap, uint64_t *cpu_port_bitmap);

/**
 * @brief Add single port to multicast group
 * @param[in] dev       eth device
 * @param[in] mgid      multicast id
 * @param[in] port_type port type (NDRV_PPE_MAC_PORT_TYPE/NDRV_PPE_CPU_PORT_TYPE)
 * @param[in] port_id   port number
 * @return 0 on success, non-zero on failure
 */
int hns5_dev_mcast_add_port(struct rte_eth_dev *dev, uint32_t mgid,
    enum ndrv_ppe_port_type port_type, uint32_t port_id);

/**
 * @brief Delete single port from multicast group
 * @param[in] dev       eth device
 * @param[in] mgid      multicast id
 * @param[in] port_type port type (NDRV_PPE_MAC_PORT_TYPE/NDRV_PPE_CPU_PORT_TYPE)
 * @param[in] port_id   port number
 * @return 0 on success, non-zero on failure
 */
int hns5_dev_mcast_del_port(struct rte_eth_dev *dev, uint32_t mgid,
    enum ndrv_ppe_port_type port_type, uint32_t port_id);


#ifdef __cplusplus
}
#endif

#endif /* _HNS5_ETH_H_ */
