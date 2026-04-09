 /* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_IFACE_PORT_H
#define HINIC3_IFACE_PORT_H

#include <stddef.h>
#include <stdint.h>
#include "rte_pci.h"
#include "rte_common.h"
#include "hinic3_smap.h"
#include "hinic3_map.h"
#include "hinic3_provider.h"

#ifndef VIRTIO_NET_F_MTU
#define VIRTIO_NET_F_MTU 3
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_PORT_NAME                   15

/* Macros for pci id verification .support two format:XX:XX.X,XXXX:XX:XX.X */
#define PCI_ID_LENGTH                   7
#define PCI_ID_DELIMITER1_OFFSET        2
#define PCI_ID_DELIMITER2_OFFSET        5
#define PCI_ID_DOMAIN_LENGTH            12
#define PCI_ID_DOMAIN_DELIMITER1_OFFSET 4
#define PCI_ID_DOMAIN_DELIMITER2_OFFSET 7
#define PCI_ID_DOMAIN_DELIMITER3_OFFSET 10

#define HINIC3_PORT_ID_INVALID       0
#define INVALID_DPDK_PORT_ID        UINT16_MAX

#define HINIC3_DATA_COUNT                25
#define VM_LEVEL                        1

#define HINIC3_SHOW_PORT_API_MAX_ARG     3
#define HINIC3_SHOW_PORT_API_MIN_ARG     1

enum EVS_PORT_TYPE {
    PORT_TYPE_NULLITY = -1,    /* inefficacy port. */
    PORT_TYPE_HNIC,            /* port which is connected to host. */
    PORT_TYPE_PATCHNIC,        /* port which is connected between bridge and bridge. */
    PORT_TYPE_DPDKPHY,         /* port which is connected to DPDK physical network. */
    PORT_TYPE_LINUXPHY,        /* port which is connected to linux physical network. */
    PORT_TYPE_VIRTIO,          /* port which is connected to vhost-user. */
    PORT_TYPE_BOND,            /* port which is connected to bond */
    PORT_TYPE_VXLAN,           /* port which is connected to vxlan */
    PORT_TYPE_DUMMY,
    PORT_TYPE_DUMMY_INTERNAL,
    PORT_TYPE_DUMMY_PMD,
    PORT_TYPE_STT,
    PORT_TYPE_GRE,
    PORT_TYPE_GENEVE,
    PORT_TYPE_LISP,
    PORT_TYPE_HWBOND,          /* port which is connected to hwbond */
    PORT_TYPE_VIRTIO_VF,       /* port which is connected to vhost agent */
    PORT_TYPE_HWPT,            /* port which is connected to hwpt */
    PORT_TYPE_DPDK,
    PORT_TYPE_DPDK_VHOSTUSER,
};

enum hiovs_bdf_type {
    BDF_TYPE_REAL,      /**< real bdf type. */
    BDF_TYPE_FAKE,      /**< fake bdf type. */
};

union bdf_info_u {
    struct {
        uint8_t bdf_type : 1;
        uint8_t func_id_flag : 1;
        uint8_t rsvd1 : 6;
    } bs;
    uint8_t value;
};

int hinic3_port_mgmt_add(uint16_t *port_id, struct rte_pci_addr *pci_addr);
int hinic3_port_mgmt_add_dynamic(uint16_t *port_id, const struct smap *args);
void hinic3_port_mgmt_del(uint16_t port_id);
int hinic3_port_mgmt_get(uint16_t port_id, struct smap *args);
int hinic3_port_mgmt_set(uint16_t port_id, const struct smap *args, struct smap *unset_args);
int hinic3_port_mgmt_setup_upcall_queue(uint16_t port_id, uint16_t queue_id, unsigned int socket_id,
    struct rte_mempool *mp);
int hinic3_port_mgmt_release_upcall_queue(uint16_t port_id, uint16_t queue_id);
int hinic3_bond_mgmt_create(uint8_t mode, const char *name, uint16_t *bond_id);
void hinic3_bond_mgmt_delete(uint16_t bond_id);
int hinic3_bond_mgmt_get(uint16_t bond_id, struct smap *args);
int hinic3_etheraddr_get(uint16_t port_id, struct eth_address *mac);
int hinic3_etheraddr_set(uint16_t port_id, struct eth_addr mac);
int hinic3_mtu_set(uint16_t port_id, int mtu);
int hinic3_security_get(uint16_t port_id, struct smap *args);
int hinic3_security_remove(uint16_t port_id, struct hinic3_nlattr *nla_args);
int hinic3_security_set(uint16_t port_id, struct hinic3_nlattr *nla_args, struct hinic3_nlattr *nla_unset_args);
int hinic3_port_statistics_get(uint16_t port_id, hinic3_port_stats *stats);
int hinic3_port_statistics_flush(uint16_t port_id);
int hinic3_port_mgmt_get_capability(struct hinic3_port_capability *cap);
int hinic3_port_mgmt_get_upcall_info(struct hinic3_port_upcall_info *info);
int hinic3_port_get_api_record(const char *api_name, bool is_all, bool is_clear,
                              hiovs_api_record *records, int record_len);
const char *hinic3_port_get_api_name(hinic3_port_api api_index);
int hinic3_port_class_init(void);
void hinic3_port_class_uninit(void);
int hinic3_port_mgmt_get_bond_slave_info(const uint16_t port_id, struct hovs_bond_slave_stats *info);
int hinic3_port_mgmt_set_usage_state(uint16_t port_id, uint16_t status);
int hinic3_port_mgmt_get_usage_state(uint16_t port_id, uint32_t *status);
int hinic3_port_upcall_mtu_set(uint8_t type, int mtu);
int16_t hinic3_get_tx_queue_count(uint16_t port_id, uint16_t queue_id);
int16_t hinic3_get_rx_queue_count(uint16_t port_id, uint16_t queue_id);
hinic3_port_api hinic3_port_get_api_index(const char *api_name);
int hinic3_hotplug_add(uint16_t port_id);
int hinic3_hotplug_del(uint16_t port_id);

#ifdef __cplusplus
}
#endif

#endif /* _HINIC3_IFACE_PORT_H_ */
