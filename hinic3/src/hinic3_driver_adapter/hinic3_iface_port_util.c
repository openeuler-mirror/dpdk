 /* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <dlfcn.h>
#include "rte_cycles.h"
#include "hinic3_smap.h"
#include "hinic3_map.h"
#include "hinic3_log.h"
#include "hinic3_util.h"
#include "hinic3_provider.h"
#include "hinic3_tlv_key.h"
#include "hinic3_iface_port_api_record.h"
#include "hinic3_smap.h"
#include "hinic3_parse_agent_config.h"
#include "hinic3_iface_port_util.h"
#include "hinic3_packets_types.h"

#define MAX_ARGS_SIZE   (2048)
#define MAX_NAME        32
#define MAX_RX_QUEUE_PER_VPORT      16

static const char *g_port_smap_key[HINIC3_PORT_ARG_TYPE_MAX] = {
    HINIC3_PORT_VLAN_OL,
    HINIC3_PORT_VLAN_TAG,
    HINIC3_PORT_VLAN_MODE,
    HINIC3_PORT_VNI,
    HINIC3_DPDK_PORT_ID,
    NULL,
    NULL,
    HINIC3_PORT_QUEUE_MAP,
    HINIC3_PORT_GRO_ENABLE,
    HINIC3_PORT_IPV6_GRO_ENABLE,
    HINIC3_PORT_UPCALL_QUEUE_MAP,
    HINIC3_PORT_BUCKET_ID,
    HINIC3_PORT_MAX_QUEUE_NUM,
    HINIC3_PORT_MIGRATE_TUNNEL_INFO,
    HINIC3_PORT_MIGRATE_STATE,
    HINIC3_PORT_PCI_ADDR,
    HINIC3_PORT_BLOCK_START,
    HINIC3_PORT_BLOCK_SIZE,
    HINIC3_PORT_UPCALL_QUEUE_NUM,
    HINIC3_PORT_UPCALL_REUSE,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    HINIC3_PORT_VIRTIO_QUEUE_DEPTH,
    HINIC3_PORT_NO_DRIVER_CHECK,
    HINIC3_PORT_FUNCTION_ID,
};

static const char *g_bond_smap_key[HINIC3_BOND_ARG_TYPE_MAX] = {
    HINIC3_BOND_UPLINK_PCI_ID,
    HINIC3_BOND_LACP_DEACTIVE_SLAVES,
    HINIC3_BOND_MODE,
    HINIC3_BOND_XMIT_HASH_POLICY,
    HINIC3_BOND_SLAVES,
    HINIC3_BOND_ACTIVE_SLAVE,
    HINIC3_BOND_ARG_UPDELAY_STR,
    HINIC3_BOND_ARG_DOWNDELAY_STR,
    HINIC3_BOND_ARG_LACP_STATUS_STR,
    HINIC3_BOND_ARG_ACTIVE_MAC_STR,
    HINIC3_BOND_ARG_SLAVE_NAME_STR,
    NULL, /* slave cfg str start */
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL, /* slave cfg str end */
    HINIC3_BOND_LACP_RATE,
    HINIC3_BOND_ARG_SLAVE_PCI_STR,
};

static inline int port_smap_key_to_nla_type(char *smap_key)
{
    int i;

    for (i = 0; i < HINIC3_PORT_ARG_TYPE_MAX; i++) {
        if (g_port_smap_key[i] && !strncmp(smap_key, g_port_smap_key[i], strlen(smap_key) + 1)) {
            return i;
        }
    }

    return HINIC3_PORT_ARG_TYPE_MAX;
}

static int smap_to_nlattr_u8(struct hinic3_nlattr *nla, char *value, int type)
{
    uint8_t val;
    int rc;

    rc = sscanf(value, "%hhu", &val);
    if (rc != 1) {
        HINIC3_LOG(ERR, DRIVER, "Failed to sscanf %d, err is %d!", type, rc);
        return -1;
    }
    hinic3_nlattr_put_u8(nla, type, val);
    return 0;
}

static int smap_to_nlattr_u16(struct hinic3_nlattr *nla, char *value, int type)
{
    uint16_t val;
    int rc;

    rc = sscanf(value, "%hu", &val);
    if (rc != 1) {
        HINIC3_LOG(ERR, DRIVER, "Failed to sscanf %d, err is %d!", type, rc);
        return -1;
    }
    hinic3_nlattr_put_u16(nla, type, val);
    return 0;
}

static int smap_to_nlattr_u32(struct hinic3_nlattr *nla, char *value, int type)
{
    uint32_t val;
    int rc;

    rc = sscanf(value, "%u", &val);
    if (rc != 1) {
        HINIC3_LOG(ERR, DRIVER, "Failed to sscanf %d, err is %d!", type, rc);
        return -1;
    }
    hinic3_nlattr_put_u32(nla, type, val);
    return 0;
}

static int smap_to_nlattr_unspec(struct hinic3_nlattr *nla, char *value, int type)
{
    uint64_t val;
    int rc = sscanf(value, "%" PRIu64 "", &val);
    if (rc != 1) {
        HINIC3_LOG(ERR, DRIVER, "Failed to sscanf %d, err is %d!", type, rc);
        return -1;
    }
    if (type == HINIC3_PORT_ARG_PORT_QUEUE_MAP) {
        hinic3_nlattr_put_unspec(nla, type, &val, sizeof(val));
    } else if (type == HINIC3_PORT_ARG_MIGRATE_TUNNEL_INFO) {
        hinic3_nlattr_put_unspec(nla, type, (struct migrate_tunnel_info *)val, sizeof(struct migrate_tunnel_info));
    } else if (type == HINIC3_PORT_ARG_MIGRATE_STATE) {
        hinic3_nlattr_put_unspec(nla, type, (struct migrate_state_dir *)val, sizeof(struct migrate_state_dir));
    } else if (type == HINIC3_PORT_ARG_PCI_ADDR) {
        hinic3_nlattr_put_unspec(nla, type, (struct rte_pci_addr *)val, sizeof(struct rte_pci_addr));
    } else if (type == HINIC3_PORT_ARG_PORT_UPCALL_QUEUE_MAP) {
        hinic3_nlattr_put_unspec(nla, type, (uint16_t *)(uintptr_t)val, MAX_RX_QUEUE_PER_VPORT * sizeof(uint16_t));
    }

    return 0;
}

static int port_smap_to_nlattr(struct hinic3_nlattr *nla, char *key, char *value)
{
    enum hinic3_port_arg_type nla_type = (enum hinic3_port_arg_type)port_smap_key_to_nla_type(key);
    if (nla_type == HINIC3_PORT_ARG_TYPE_MAX) {
        return 0;
    }
    switch (nla_type) {
        case HINIC3_PORT_ARG_VLAN_OL:
        case HINIC3_PORT_ARG_VLAN_MODE:
        case HINIC3_PORT_ARG_GRO_ENABLE:
        case HINIC3_PORT_ARG_MAX_QUEUE_NUM:
        case HINIC3_PORT_ARG_UPCALL_QUEUE_NUM:
            return smap_to_nlattr_u8(nla, value, nla_type);
        case HINIC3_PORT_ARG_VLAN_TAG:
        case HINIC3_PORT_ARG_DPDK_PORT_ID:
        case HINIC3_PORT_ARG_BUCKET_ID:
        case HINIC3_PORT_ARG_QUEUE_SIZE:
        case HINIC3_PORT_ARG_FUNCTION_ID:
            return smap_to_nlattr_u16(nla, value, nla_type);
        case HINIC3_PORT_ARG_VNI:
        case HINIC3_PORT_ARG_IPV6_GRO_ENABLE:
        case HINIC3_PORT_ARG_BLOCK_START:
        case HINIC3_PORT_ARG_BLOCK_SIZE:
            return smap_to_nlattr_u32(nla, value, nla_type);
        case HINIC3_PORT_ARG_PORT_QUEUE_MAP:
        case HINIC3_PORT_ARG_MIGRATE_TUNNEL_INFO:
        case HINIC3_PORT_ARG_MIGRATE_STATE:
        case HINIC3_PORT_ARG_PCI_ADDR:
        case HINIC3_PORT_ARG_PORT_UPCALL_QUEUE_MAP:
            return smap_to_nlattr_unspec(nla, value, nla_type);
        case HINIC3_PORT_ARG_UPCALL_REUSE:
            return smap_to_nlattr_u8(nla, value, nla_type);
        default:
            break;
    }

    return 0;
}

void port_args_smap_to_nlattr(const struct smap *args, struct hinic3_nlattr *nla_args)
{
    struct smap_node *node = NULL;

    HINIC3_SMAP_FOR_EACH(node, args) {
        if (port_smap_to_nlattr(nla_args, node->key, node->value) != 0) {
            return;
        }
    }
}

static inline int nla_to_smap_u8(struct smap *unset_args, const hinic3_nlattr_itr nla, const char *key)
{
    char *value = NULL;
    char args_buf[MAX_ARGS_SIZE] = { 0 };
    snprintf(args_buf, sizeof(args_buf) - 1, "%hhu", hinic3_nlattr_get_itr_u8(nla));
    value = args_buf;
    hinic3_smap_add(unset_args, key, value, HINIC3_DRIVER_ADAPTER);
    return 0;
}

static inline int nla_to_smap_u16(struct smap *unset_args, const hinic3_nlattr_itr nla, const char *key)
{
    char *value = NULL;
    char args_buf[MAX_ARGS_SIZE] = { 0 };
    snprintf(args_buf, sizeof(args_buf) - 1, "%hu", hinic3_nlattr_get_itr_u16(nla));
    value = args_buf;
    hinic3_smap_add(unset_args, key, value, HINIC3_DRIVER_ADAPTER);
    return 0;
}

static inline int nla_to_smap_u32(struct smap *unset_args, const hinic3_nlattr_itr nla, const char *key)
{
    char *value = NULL;
    char args_buf[MAX_ARGS_SIZE] = { 0 };
    snprintf(args_buf, sizeof(args_buf) - 1, "%u", hinic3_nlattr_get_itr_u32(nla));
    value = args_buf;
    hinic3_smap_add(unset_args, key, value, HINIC3_DRIVER_ADAPTER);
    return 0;
}

static inline int nla_to_smap_u64(struct smap *unset_args, const hinic3_nlattr_itr nla, const char *key)
{
    char *value = NULL;
    char args_buf[MAX_ARGS_SIZE] = { 0 };
    snprintf(args_buf, sizeof(args_buf) - 1, "%" PRIu64 "", hinic3_nlattr_get_itr_u64(nla));
    value = args_buf;
    hinic3_smap_add(unset_args, key, value, HINIC3_DRIVER_ADAPTER);
    return 0;
}

static inline const char *bond_get_smap_key_by_nla_type(uint32_t type)
{
    if (type >= HINIC3_BOND_ARG_TYPE_MAX) {
        return NULL;
    }

    return g_bond_smap_key[type];
}

void bond_args_nlattr_to_smap(const struct hinic3_nlattr *nla_config, struct smap *smap_config)
{
    const char *pstr = NULL;
    const char *smap_key = NULL;
    hinic3_nlattr_itr nla_itr = NULL;

    HINIC3_NLATTR_FOR_EACH (nla_itr, nla_config) {
        smap_key = bond_get_smap_key_by_nla_type(hinic3_nlattr_get_itr_type(nla_itr));
        if (smap_key == NULL) {
            continue;
        }
        switch (hinic3_nlattr_get_itr_type(nla_itr)) {
            case HINIC3_BOND_ARG_BOND_MODE:
            case HINIC3_BOND_ARG_XMIT_HASH_POLICY:
            case HINIC3_BOND_ARG_LACP_RATE:
            case HINIC3_BOND_ARG_LACP_STATUS:
                if (nla_to_smap_u8(smap_config, nla_itr, smap_key) != 0) {
                    return;
                }
                break;
            case HINIC3_BOND_ARG_UPDELAY:
            case HINIC3_BOND_ARG_DOWNDELAY:
                if (nla_to_smap_u16(smap_config, nla_itr, smap_key) != 0) {
                    return;
                }
                break;

            case HINIC3_BOND_ARG_ACTIVE_MAC:
            case HINIC3_BOND_ARG_LACP_DEACTIVE_SLAVES:
            case HINIC3_BOND_ARG_UPLINK_PCI_ID:
            case HINIC3_BOND_ARG_SLAVES:
            case HINIC3_BOND_ARG_SLAVE_NAME:
            case HINIC3_BOND_ARG_ACTIVE_SLAVE:
                pstr = hinic3_nlattr_get_itr_data(nla_itr);
                hinic3_smap_add(smap_config, smap_key, pstr, HINIC3_DRIVER_ADAPTER);
                break;
            default:
                break;
        }
    }
}

static int vport_upcall_ques_to_str(const hinic3_nlattr_itr nla, char *args, int len)
{
    int off;
    size_t i;
    size_t nla_size;
    const uint32_t *que_ids = NULL;
    size_t n_que;
    int succ_len;

    nla_size = hinic3_nlattr_get_itr_size(nla);
    que_ids = hinic3_nlattr_get_itr_unspec(nla, nla_size);
    n_que = nla_size / sizeof(uint32_t);
    if (n_que > MAX_RX_QUEUE_PER_VPORT) {
        HINIC3_LOG(ERR, DRIVER, "Invalid queue ids from hiovs, n_que is %zu, max_upcall_num is %d!",
                  n_que, MAX_RX_QUEUE_PER_VPORT);
        return -1;
    }

    off = 0;
    for (i = 0; i < n_que; i++) {
        succ_len = snprintf(args + off, len - off - 1,
                              "%u,", que_ids[i]);
        if (succ_len <= 0) {
            HINIC3_LOG(ERR, DRIVER, "Failed to snprintf arg buf, err_len is %d!", succ_len);
            return -1;
        }

        off += succ_len;
    }

    /* overwrite the last ',' */
    if (off != 0) {
        args[off - 1] = '\0';
    }

    return 0;
}

static int port_nla_to_smap_queue_nr(struct smap *unset_args, const hinic3_nlattr_itr nla)
{
    char *value = NULL;
    char args_buf[MAX_ARGS_SIZE] = { 0 };
    int rc = vport_upcall_ques_to_str(nla, args_buf, MAX_ARGS_SIZE);
    /* attribute is invalid, error info has print in vport_upcall_ques_to_str */
    if (rc != 0) {
        return -1;
    }

    value = args_buf;
    hinic3_smap_add(unset_args, HINIC3_PORT_UPCALL_QUEUE_MAP, value, HINIC3_DRIVER_ADAPTER);
    return 0;
}

static const char *get_smap_key_by_nla_type(uint32_t type)
{
    if (type >= HINIC3_PORT_ARG_TYPE_MAX) {
        return NULL;
    }

    return g_port_smap_key[type];
}

static int port_nla_to_smap(struct smap *unset_args, const hinic3_nlattr_itr nla)
{
    const char *smap_key = get_smap_key_by_nla_type(hinic3_nlattr_get_itr_type(nla));
    if (smap_key == NULL) {
        return 0;
    }

    switch (hinic3_nlattr_get_itr_type(nla)) {
        case HINIC3_PORT_ARG_VLAN_TAG:
        case HINIC3_PORT_ARG_DPDK_PORT_ID:
        case HINIC3_PORT_ARG_BUCKET_ID:
            return nla_to_smap_u16(unset_args, nla, smap_key);
        case HINIC3_PORT_ARG_VLAN_OL:
        case HINIC3_PORT_ARG_VLAN_MODE:
        case HINIC3_PORT_ARG_GRO_ENABLE:
        case HINIC3_PORT_ARG_MAX_QUEUE_NUM:
            return nla_to_smap_u8(unset_args, nla, smap_key);
        case HINIC3_PORT_ARG_VNI:
        case HINIC3_PORT_ARG_IPV6_GRO_ENABLE:
            return nla_to_smap_u32(unset_args, nla, smap_key);
        case HINIC3_PORT_ARG_PORT_QUEUE_MAP:
            return nla_to_smap_u64(unset_args, nla, smap_key);
        case HINIC3_PORT_ARG_PORT_UPCALL_QUEUE_MAP:
            return port_nla_to_smap_queue_nr(unset_args, nla);
        case HINIC3_PORT_ARG_MIGRATE_TUNNEL_INFO:
        case HINIC3_PORT_ARG_MIGRATE_STATE:
            hinic3_smap_add(unset_args, smap_key, (const char *)hinic3_nlattr_get_itr_data(nla), HINIC3_DRIVER_ADAPTER);
            break;
        default:
            break;
        }
    return 0;
}

void port_args_nlattr_to_smap(const struct hinic3_nlattr *nla_unset_args, struct smap *unset_args)
{
    hinic3_nlattr_itr nla = NULL;

    HINIC3_NLATTR_FOR_EACH (nla, nla_unset_args) {
        if (port_nla_to_smap(unset_args, nla) != 0) {
            return;
        }
    }
}

static int nlattr_to_smap_src_mac(struct smap *args_smap, const hinic3_nlattr_itr nla)
{
    char *value = NULL;
    const struct eth_address *macs = NULL;
    int n_macs;
    int ret;
    char args_buf[MAX_ARGS_SIZE] = { 0 };

    macs = (const struct eth_address *)hinic3_nlattr_get_itr_data(nla);
    n_macs = hinic3_nlattr_get_itr_size(nla) / sizeof(struct eth_address);
    ret = smacs_stringfy(macs, n_macs, args_buf, MAX_ARGS_SIZE);
    if (ret != 0) {
        HINIC3_LOG(ERR, DRIVER, "src macs stringfy error, err is %d!", ret);
        return -EINVAL;
    }

    value = args_buf;
    hinic3_smap_add(args_smap, HINIC3_BUM_ARG_SRC_MAC_STR, value, HINIC3_DRIVER_ADAPTER);
    return 0;
}

static int nlattr_to_smap_ethertype_check(struct smap *args_smap, const hinic3_nlattr_itr nla)
{
    uint8_t u8_val;

    u8_val = hinic3_nlattr_get_itr_u8(nla);
    if (u8_val == ETHER_TYPE_CHECK_TRUE) {
        hinic3_smap_add(args_smap, HINIC3_BUM_ARG_ETHER_TYPE_CHECK_STR, "true", HINIC3_DRIVER_ADAPTER);
    } else if (u8_val == ETHER_TYPE_CHECK_FALSE) {
        hinic3_smap_add(args_smap, HINIC3_BUM_ARG_ETHER_TYPE_CHECK_STR, "false", HINIC3_DRIVER_ADAPTER);
    } else {
        HINIC3_LOG(ERR, DRIVER, "ether_type_check val %hhu is invalid!", u8_val);
        return -EINVAL;
    }

    return 0;
}

static int nlattr_to_smap_extra_ethertype(struct smap *args_smap, const hinic3_nlattr_itr nla)
{
    char *value = NULL;
    const uint16_t *types = NULL;
    int n_types;
    int ret;
    char args_buf[MAX_ARGS_SIZE] = { 0 };

    types = hinic3_nlattr_get_itr_data(nla);
    n_types = hinic3_nlattr_get_itr_size(nla) / sizeof(uint16_t);
    ret = extra_eth_types_stringfy(types, n_types, args_buf, MAX_ARGS_SIZE);
    if (ret != 0) {
        HINIC3_LOG(ERR, DRIVER, "extra_eth_type stringfy error, err is %d!", ret);
        return -EINVAL;
    }

    value = args_buf;
    hinic3_smap_add(args_smap, HINIC3_BUM_ARG_EXTRA_ETH_TYPE_STR, value, HINIC3_DRIVER_ADAPTER);

    return 0;
}

static int nlattr_to_smap_brd_ratelimit(struct smap *args_smap, const hinic3_nlattr_itr nla)
{
    uint32_t u32_val;
    char *value = NULL;
    char args_buf[MAX_ARGS_SIZE] = { 0 };

    u32_val = hinic3_nlattr_get_itr_u32(nla);
    snprintf(args_buf, sizeof(args_buf) - 1, "%u", u32_val);

    value = args_buf;
    hinic3_smap_add(args_smap, HINIC3_BUM_ARG_BRD_RATELIMIT_STR, value, HINIC3_DRIVER_ADAPTER);

    return 0;
}

int bum_args_nlattr_to_smap(const struct hinic3_nlattr *args_nla, struct smap *args_smap)
{
    hinic3_nlattr_itr nla = NULL;
    int ret = 0;

    enum hinic3_bum_ether_type_check eth_type_check = 0;
    HINIC3_NLATTR_FOR_EACH(nla, args_nla) {
        if (hinic3_nlattr_get_itr_type(nla) == HINIC3_SECURITY_ARG_ETHER_TYPE_CHECK) {
            eth_type_check = hinic3_nlattr_get_itr_u8(nla);
            ret = nlattr_to_smap_ethertype_check(args_smap, nla);
            if (ret != 0) {
                goto err;
            }
            break;
        }
    }

    HINIC3_NLATTR_FOR_EACH(nla, args_nla) {
        if (hinic3_nlattr_get_itr_size(nla) == 0) {
            continue;
        }

        switch (hinic3_nlattr_get_itr_type(nla)) {
            case HINIC3_SECURITY_ARG_SRC_MAC:
                ret = nlattr_to_smap_src_mac(args_smap, nla);
                if (ret != 0) {
                    goto err;
                }
                break;
            case HINIC3_SECURITY_ARG_BRD_RATELIMIT:
                ret = nlattr_to_smap_brd_ratelimit(args_smap, nla);
                if (ret != 0) {
                    goto err;
                }
                break;
            case HINIC3_SECURITY_ARG_EXTRA_ETH_TYPE:
                if (eth_type_check == ETHER_TYPE_CHECK_FALSE) {
                    continue;
                }
                ret = nlattr_to_smap_extra_ethertype(args_smap, nla);
                if (ret != 0) {
                    goto err;
                }
                break;
            case HINIC3_SECURITY_ARG_SRC_IPMAC: /* no need */
            default:
                break;
        }
    }

err:
    return ret;
}
