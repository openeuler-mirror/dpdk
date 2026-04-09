 /* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_IFACE_GLOBAL_H
#define HINIC3_IFACE_GLOBAL_H

#include <stddef.h>

#include "rte_pci.h"
#include "rte_mempool.h"
#include "rte_cfgfile.h"

#include "hinic3_smap.h"
#include "hinic3_map.h"
#include "hinic3_driver_public.h"
#include "hinic3_provider.h"
#include "hinic3_ds.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ETHER_HDR_MAX_LEN           (RTE_ETHER_HDR_LEN + RTE_ETHER_CRC_LEN \
                                     + (2 * VLAN_HEADER_LEN))
#define NETDEV_DPDK_MBUF_ALIGN      1024
#define NETDEV_DPDK_MAX_PKT_LEN     9728

#define HINIC3_SHOW_GLOBAL_API_MAX_ARG      3
#define HINIC3_SHOW_GLOBAL_API_MIN_ARG      1

#define HINIC3_FLOW_AGE_TIME                 "hinic3-max-idle"
#define HINIC3_RX_THREAD_NUM                 "hinic3-offload-thread-num"
#define HINIC3_MAX_FLOW_NUM                  "hinic3-max-flow-num"
#define HINIC3_PMD_NUM                       "pmd-num"

#define HINIC3_FLOW_AGE_TIME_MAX_LEN         16
#define HINIC3_BDF_TYPE_FAKE_WITH_FUNC_ID    2
#define HINIC3_FRONT_BACK_FAKE_WITH_FUNC_ID 1

#define HINIC3_RX_THREAD_NUM_MIN 1
#define HINIC3_RX_THREAD_NUM_MAX 4
#define HINIC3_RX_THREAD_NUM_DEFAULT 2

#define HINIC3_OFFLOAD_FLOW_NUM_MIN 1
#define HINIC3_OFFLOAD_FLOW_NUM_MAX 2  /* it will be 32 M later */

#define HINIC3_DEVICE_TYPE               (1LLU << 0)
#define HINIC3_DEVICE_DRIVER_TYPE        (1LLU << 1)

#define HINIC3_OFFLOAD_FLOW_NUM_DEFAULT 2
#define HINIC3_VTEP_TABLE_IP_ADDR_LEN    4

#define HINIC3_DEFAULT_BOND_RX_SIZE              1024
#define HINIC3_DEFAULT_BOND_TX_SIZE              512
#define HINIC3_DEFAULT_VPORT_RX_SIZE             256
#define HINIC3_DEFAULT_VPORT_TX_SIZE             256
#define HINIC3_DEFAULT_MBUF_SIZE                 2176
#define HINIC3_BOND_RX_SIZE_STR                  "bond_rx_depth"
#define HINIC3_BOND_TX_SIZE_STR                  "bond_tx_depth"
#define HINIC3_VPORT_RX_SIZE_STR                 "vport_rx_depth"
#define HINIC3_VPORT_TX_SIZE_STR                 "vport_tx_depth"
#define HINIC3_MBUF_SIZE_STR                     "mbuf_size"

enum hinic3_vtep_cmd {
    HINIC3_VTEP_DEL = 0,
    HINIC3_VTEP_ADD
};

struct hinic3_vtep_ip {
    uint32_t is_ipv6; /* 0: ipv4, 1: ipv6 */
    /*
    * ip_dw0: IPv6 DIP[127:96]
    * ip_dw1: IPv6 DIP[95:64]
    * ip_dw2: IPv6 DIP[63:32]
    * ip_dw3: IPv6 DIP[31:0] or IPv4 DIP[31:0]
    */
    uint32_t ip_addr[HINIC3_VTEP_TABLE_IP_ADDR_LEN];
};

struct hinic3_vtep_ip_set_args {
    uint32_t ops;  /* 0: del  1: add */
    struct hinic3_vtep_ip dip;
};

struct hinic_global_api_args {
    void *in_nlattr_data;
    size_t in_nlattr_len;
    void *out_nlattr_data;
    size_t *out_nlattr_len;
    struct hovs_global_stats *hovs_stats;
    uint32_t log_module;
    uint32_t log_level_or_enable;
    const char *cmd_in;
    uint32_t cmd_in_len;
    char *cmd_out;
    uint32_t *cmd_out_len;
    uint32_t cmd_out_buf_size;
    uint8_t front_back;
    uint8_t bdf_type;
    struct hovs_phy_dev_info *dev;
};

enum hinic3_packet_forward_mod {
    HINIC3_FORWARD_MODE_BANDWIDTH,
    HINIC3_FORWARD_MODE_LATENCY,
    HINIC3_FORWARD_MODE_30M
};

// global apis
void hinic3_global_unit(void);
struct rte_mempool *dpdk_shared_mp_get(void);
int hinic3_global_cfg_set(const struct smap *args, struct smap *unset_args);
int hinic3_global_cfg_get(struct smap *args);
int hinic3_global_statistics_get(struct hinic3_global_stats *stats);
int hinic3_global_statistics_flush(void);
int hinic3_global_open_log(uint32_t module, uint32_t enable);
int hinic3_global_set_log_level(uint32_t module, uint32_t level);
int hinic3_global_cmd_exec(const char *cmd_in, uint32_t in_size,
                          char *cmd_out, uint32_t *out_len, uint32_t max_out_len);
int hinic3_global_pcie_list_query(uint8_t front_back, uint8_t bdf_type, struct hovs_phy_dev_info *dev);
void hinic3_eth_dev_tx_lock_init(void);
uint16_t hinic3_global_rte_eth_tx_burst(uint16_t port_id, uint16_t queue_id,
                                       struct rte_mbuf **tx_pkts, uint16_t nb_pkts);
uint16_t hinic3_global_rte_eth_rx_burst(uint16_t port_id, uint16_t queue_id,
                                       struct rte_mbuf **rx_pkts, uint16_t nb_pkts);
int hinic3_global_get_api_record(const char *api_name, bool is_all, bool is_clear,
                                hiovs_api_record *records, int record_len);
void hinic3_global_cfg_set_when_restart(void);
int hinic3_global_class_init(void);
void hinic3_global_class_uninit(void);
int hinic3_adapt_malloc(const char *type, size_t size, int socket_arg, unsigned int flags, size_t align, size_t bound,
                       bool contig, struct hovs_melem *mem);
int hinic3_adapt_mfree(void *addr);
struct hovs_mbuf *hinic3_adapt_mbuf_alloc(void *mp);
void hinic3_adapt_mbuf_free(struct hovs_mbuf *m);
void *hinic3_adapt_malloc_socket(const char *type, size_t size, unsigned int align, int socket_arg);
void hinic3_adapt_free(void *addr);
const void *hinic3_adapt_memzone_reserve(const char *name, size_t len, int socket_id, unsigned flags);
const void *hinic3_adapt_memzone_reserve_aligned(const char *name, size_t len, int socket_id, unsigned flags,
    unsigned align);
int hinic3_adapt_memzone_free(void *addr);
int hinic3_adapt_mbuf_alloc_bulk(void *mp, struct hovs_mbuf **mbufs, uint32_t count);
int hinic3_global_set_vxlan_vtep(const struct hinic3_vtep_ip_set_args *ops);
hinic3_global_api hinic3_global_get_api_index(const char *api_name);
int hinic3_forward_mode_set(enum hinic3_packet_forward_mod mod);
int hinic3_forward_mod_get(struct ds *ds);
int hinic3_forward_mode_init(void);
int hinic3_bond_hash_policy_init(void);
#ifdef __cplusplus
}
#endif

#endif /* HINIC3_IFACE_GLOBAL_H */
