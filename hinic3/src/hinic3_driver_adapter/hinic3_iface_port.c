/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <dlfcn.h>
#include <rte_atomic.h>
#include "rte_cycles.h"
#include "hinic3_map.h"

#include "hinic3_smap.h"
#include "hinic3_log.h"
#include "hinic3_util.h"
#include "hinic3_provider.h"
#include "hinic3_driver_public.h"
#include "hinic3_meminfo.h"
#include "hinic3_command.h"
#include "hinic3_ds.h"
#include "hinic3_ui_string.h"
#include "hinic3_parse_agent_config.h"
#include "hinic3_iface_port_util.h"
#include "hinic3_iface_port_api_record.h"
#include "hinic3_iface_port.h"

#define MAX_SLAVES_NUM_EACH_BOND RTE_MAX_ETHPORTS
#define MAX_SLAVES_SIZE MAX_SLAVES_NUM_EACH_BOND

#define SHOW_ONE_API_ARG_NUM 2
#define SHOW_MAX_API_ONE_LINE 4
#define CLEAR_PORT_API_ARG_NUM 2
#define SHOW_PORT_API_UI_TWO_SPACE 2
#define SHOW_PORT_API_UI_FOUR_SPACE 4
#define SHOW_PORT_API_UI_SIX_SPACE 6

struct hiovs_port_map
{
    hinic3_port_api api_index;
    const char *api_name;
};

// please modify hinic3_port_api when modify this structure
static const struct hiovs_port_map g_port_api_arr[HINIC3_PORT_API_MAX] = {
    {HINIC3_PORT_MGMT_ADD, "hovs_port_mgmt_add"},
    {HINIC3_PORT_MGMT_DEL, "hovs_port_mgmt_del"},
    {HINIC3_PORT_MGMT_GET, "hovs_port_mgmt_get"},
    {HINIC3_PORT_MGMT_SET, "hovs_port_mgmt_set"},
    {HINIC3_PORT_QUEUE_SET, "hovs_port_mgmt_setup_upcall_queue"},
    {HINIC3_PORT_QUEUE_RELEASE, "hovs_port_mgmt_release_upcall_queue"},
    {HINIC3_BOND_MGMT_CREATE, "hovs_bond_mgmt_create"},
    {HINIC3_BOND_MGMT_DELETE, "hovs_bond_mgmt_delete"},
    {HINIC3_BOND_MGMT_GET, "hovs_bond_mgmt_get"},
    {HINIC3_BOND_MGMT_SET, "hovs_bond_mgmt_set"},
    {HINIC3_BOND_MGMT_CLEAR, "hovs_bond_mgmt_clear"},
    {HINIC3_BOND_SLAVE_INFO_GET, "hovs_bond_slave_info_get"},
    {HINIC3_ETHERADDR_GET, "hovs_etheraddr_get"},
    {HINIC3_MTU_SET, "hovs_mtu_set"},
    {HINIC3_CARRIER_GET, "hovs_carrier_get"},
    {HINIC3_CARRIER_RESETS_GET, "hovs_carrier_resets_get"},
    {HINIC3_PORT_STATISTICS_GET, "hovs_port_statistics_get"},
    {HINIC3_PORT_STATISTICS_FLUSH, "hovs_port_statistics_flush"},
    {HINIC3_FEATURES_GET, "hovs_features_get"},
    {HINIC3_SECURITY_GET, "hovs_bum_get"},
    {HINIC3_SECURITY_SET, "hovs_bum_set"},
    {HINIC3_SECURITY_REMOVE, "hovs_bum_remove"},
    {HINIC3_SECURITY_CLEAR, "hovs_bum_clear"},
    {HINIC3_QOS_INGRESS_LIMIT_SET, "hovs_qos_ingress_limit_set"},
    {HINIC3_QOS_INGRESS_LIMIT_GET, "hovs_qos_ingress_limit_get"},
    {HINIC3_QOS_EGRESS_LIMIT_SET, "hovs_qos_egress_limit_set"},
    {HINIC3_QOS_EGRESS_LIMIT_GET, "hovs_qos_egress_limit_get"},
    {HINIC3_QOS_DROP_THRESH_SET, "hovs_qos_drop_thresh_set"},
    {HINIC3_QOS_DROP_THRESH_GET, "hovs_qos_drop_thresh_get"},
    {HINIC3_PORT_MGMT_GET_CAPABILITY, "hovs_port_mgmt_get_capability"},
    {HINIC3_PORT_MGMT_GET_UPCALL_INFO, "hovs_port_mgmt_get_upcall_info"},
    {HINIC3_QOS_STATISTICS_GET, "hovs_qos_statistics_get_batch"},
    {HINIC3_QOS_STATISTICS_CLEAR_GET, "hovs_qos_statistics_get_all_batch"},
    {HINIC3_QOS_STATISTICS_CLEAR_CLEAR, "hovs_qos_statistics_clear_batch"},
    {HINIC3_HQOS_STATISTICS_CLEAR_GET, "hovs_hqos_statistics_get_all_batch"},
    {HINIC3_HQOS_STATISTICS_CLEAR_CLEAR, "hovs_hqos_statistics_clear_batch"},
    {HINIC3_PORT_MGMT_ADD_DYNAMIC, "hovs_port_mgmt_add_dynamic"},
    {HINIC3_VM_QOS_LIMIT_SET, "hovs_qos_vm_limit_set"},
    {HINIC3_VM_QOS_LIMIT_GET, "hovs_qos_vm_limit_get"},
    {HINIC3_PORT_QOS_LIMIT_SET, "hovs_qos_vport_limit_set"},
    {HINIC3_PORT_QOS_LIMIT_GET, "hovs_qos_vport_limit_get"},
    {HINIC3_VM_QOS_SRTCM_LIMIT_SET, "hovs_qos_vm_srtcm_limit_set"},
    {HINIC3_VM_QOS_SRTCM_LIMIT_GET, "hovs_qos_vm_srtcm_limit_get"},
    {HINIC3_NET_QOS_LIMIT_SET, "hovs_qos_net_limit_set"},
    {HINIC3_NET_QOS_LIMIT_GET, "hovs_qos_net_limit_get"},
    {HINIC3_PORT_MGMT_GET_BOND_SALVE_INFO, "hovs_bond_slave_statistics_get"},
    {HINIC3_PORT_MGMT_SET_USAGE_STATE, "hovs_port_mgmt_set_usage_state"},
    {HINIC3_PORT_MGMT_GET_USAGE_STATE, "hovs_port_mgmt_get_usage_state"},
    {HINIC3_UPCALL_MTU_SET, "hovs_upcall_mtu_set"},
    {HINIC3_HOTPLUG_ADD, "hovs_hotplug_add"},
    {HINIC3_HOTPLUG_DEL, "hovs_hotplug_del"},
    {HINIC3_GET_TX_QUEUE_COUNT, "hovs_rte_get_txq_backlog"},
    {HINIC3_GET_RX_QUEUE_COUNT, "hovs_rte_get_rxq_backlog"},
};

#define RTF_LOG_HINIC_PORT_API_LOG(TIME_START, EXEC_TIME, RET_CODE, API_INDEX, API_FUNC) \
    do                                                                                   \
    {                                                                                    \
        TIME_START = rte_get_tsc_cycles();                                               \
        RET_CODE = API_FUNC;                                                             \
        EXEC_TIME = 0;                                                                   \
        uint64_t hz = rte_get_tsc_hz();                                                  \
        if (hz != 0)                                                                     \
        {                                                                                \
            EXEC_TIME = ((rte_get_tsc_cycles() - (TIME_START)) * 1000000UL) / hz;        \
        }                                                                                \
        hinic3_port_fill_api_record(API_INDEX, EXEC_TIME);                               \
        if ((RET_CODE) != 0)                                                             \
        {                                                                                \
            hinic_port_api_record_error(RET_CODE, API_INDEX, EXEC_TIME);                 \
        }                                                                                \
    } while (0)

int hinic3_port_mgmt_add(uint16_t *port_id, struct rte_pci_addr *pci_addr)
{
    int ret;
    uint64_t time_start;
    uint64_t exec_time;
    struct hinic3_drv_ops *ops = NULL;
    struct rte_pci_addr *p_tmp = (struct rte_pci_addr *)pci_addr;

    ops = hinic3_get_drv_ops();
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_port_mgmt_add, HINIC3_DRV_FUNC_NO_PTR);
    RTF_LOG_HINIC_PORT_API_LOG(time_start, exec_time, ret, HINIC3_PORT_MGMT_ADD,
                               ops->hovs_port_mgmt_add(port_id, p_tmp));
    return hinic3_convert_error_code(ret);
}

int hinic3_hotplug_add(uint16_t port_id)
{
    int ret;
    uint64_t time_start;
    uint64_t exec_time;
    struct hinic3_drv_ops *ops = NULL;

    ops = hinic3_get_drv_ops();
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_hotplug_add, HINIC3_DRV_FUNC_NO_PTR);
    RTF_LOG_HINIC_PORT_API_LOG(time_start, exec_time, ret, HINIC3_HOTPLUG_ADD,
                               ops->hovs_hotplug_add(port_id));
    return hinic3_convert_error_code(ret);
}

int hinic3_hotplug_del(uint16_t port_id)
{
    int ret;
    uint64_t time_start;
    uint64_t exec_time;
    struct hinic3_drv_ops *ops = NULL;

    ops = hinic3_get_drv_ops();
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_hotplug_del, HINIC3_DRV_FUNC_NO_PTR);
    RTF_LOG_HINIC_PORT_API_LOG(time_start, exec_time, ret, HINIC3_HOTPLUG_DEL,
                               ops->hovs_hotplug_del(port_id));
    return hinic3_convert_error_code(ret);
}

void hinic3_port_mgmt_del(uint16_t port_id)
{
    uint64_t time_start;
    uint64_t exec_time;
    struct hinic3_drv_ops *ops = NULL;
    ops = hinic3_get_drv_ops();
    if (ops->hovs_port_mgmt_del == NULL)
    {
        HINIC3_LOG(ERR, DRIVER, "hovs_port_mgmt_del is NULL in ops!");
        return;
    }
    RTF_LOG_HINIC_PORT_API_LOG_VOID(time_start, exec_time, HINIC3_PORT_MGMT_DEL, ops->hovs_port_mgmt_del(port_id));
}

int hinic3_port_mgmt_get(uint16_t port_id, struct smap *args)
{
    int ret = -1;
    uint64_t time_start;
    uint64_t exec_time;
    void *buff = NULL;
    void *tmp_value = NULL;
    struct hinic3_nlattr nla_args;
    struct hinic3_drv_ops *ops = NULL;

    if (args == NULL)
    {
        HINIC3_LOG(WARNING, DRIVER, "Pointer parameter is NULL!");
        return -EINVAL;
    }

    buff = (void *)hinic3_calloc(1, HOVS_MAX_TLV_BUF_LEN, HINIC3_DRIVER_ADAPTER);
    if (!buff)
    {
        HINIC3_LOG(ERR, DRIVER, "Alloc memory error!");
        return -ENOMEM;
    }

    ops = hinic3_get_drv_ops();
    if (ops->hovs_port_mgmt_get == NULL)
    {
        ret = HINIC3_DRV_FUNC_NO_PTR;
        goto out;
    }

    tmp_value = buff;
    hinic3_nlattr_init(&nla_args, tmp_value, HOVS_MAX_TLV_BUF_LEN);
    RTF_LOG_HINIC_PORT_API_LOG(time_start, exec_time, ret, HINIC3_PORT_MGMT_GET,
                               ops->hovs_port_mgmt_get(port_id, nla_args.data, (uint32_t *)&nla_args.used_len));
    if (ret != 0)
    {
        ret = hinic3_convert_error_code(ret);
        goto out;
    }

    hinic3_nlattr_reset_itr(&nla_args, nla_args.used_len);
    /* nlattr to smap */
    port_args_nlattr_to_smap(&nla_args, args);

out:
    hinic3_free(buff);
    return ret;
}

int hinic3_port_mgmt_set(uint16_t port_id, const struct smap *args, struct smap *unset_args)
{
    int ret = -1;
    uint64_t time_start;
    uint64_t exec_time;
    void *buff = NULL;
    void *reply_buff = NULL;
    void *tmp_buff = NULL;
    struct hinic3_nlattr nla_args;
    struct hinic3_nlattr nla_unset_args;
    struct hinic3_drv_ops *ops = NULL;

    if (!args || !unset_args)
    {
        HINIC3_LOG(WARNING, DRIVER, "Pointer parameter is NULL!");
        return -EINVAL;
    }

    ops = hinic3_get_drv_ops();
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_port_mgmt_set, HINIC3_DRV_FUNC_NO_PTR);

    buff = (void *)hinic3_calloc(1, HOVS_MAX_TLV_BUF_LEN, HINIC3_DRIVER_ADAPTER);
    if (!buff)
    {
        HINIC3_LOG(ERR, DRIVER, "Alloc memory buff error!");
        return -ENOMEM;
    }

    reply_buff = (void *)hinic3_calloc(1, HOVS_MAX_TLV_BUF_LEN, HINIC3_DRIVER_ADAPTER);
    if (!reply_buff)
    {
        hinic3_free(buff);
        HINIC3_LOG(ERR, DRIVER, "Alloc memory reply_buff error!");
        return -ENOMEM;
    }

    tmp_buff = buff;
    hinic3_nlattr_init(&nla_args, tmp_buff, HOVS_MAX_TLV_BUF_LEN);
    tmp_buff = reply_buff;
    hinic3_nlattr_init(&nla_unset_args, tmp_buff, HOVS_MAX_TLV_BUF_LEN);
    /* smap to nlattr */
    port_args_smap_to_nlattr(args, &nla_args);

    RTF_LOG_HINIC_PORT_API_LOG(time_start, exec_time, ret, HINIC3_PORT_MGMT_SET,
                               ops->hovs_port_mgmt_set(port_id, nla_args.data, nla_args.used_len, nla_unset_args.data,
                                                       (uint32_t *)&nla_unset_args.used_len));
    if (ret != 0)
    {
        ret = hinic3_convert_error_code(ret);
        goto out;
    }

    /* nlattr to smap */
    hinic3_nlattr_reset_itr(&nla_unset_args, nla_unset_args.used_len);
    port_args_nlattr_to_smap(&nla_unset_args, unset_args);

out:
    hinic3_free(buff);
    hinic3_free(reply_buff);
    return ret;
}

int hinic3_port_mgmt_setup_upcall_queue(uint16_t port_id, uint16_t queue_id, unsigned int socket_id,
                                        struct rte_mempool *mp)
{
    int ret;
    uint64_t time_start;
    uint64_t exec_time;
    struct hinic3_drv_ops *ops = NULL;
    ops = hinic3_get_drv_ops();
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_port_mgmt_setup_upcall_queue, HINIC3_DRV_FUNC_NO_PTR);
    RTF_LOG_HINIC_PORT_API_LOG(time_start, exec_time, ret, HINIC3_PORT_QUEUE_SET,
                               ops->hovs_port_mgmt_setup_upcall_queue(port_id, queue_id, socket_id, (void *)mp));
    return ret;
}

int hinic3_port_mgmt_release_upcall_queue(uint16_t port_id, uint16_t queue_id)
{
    int ret;
    uint64_t time_start;
    uint64_t exec_time;
    struct hinic3_drv_ops *ops = NULL;
    ops = hinic3_get_drv_ops();
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_port_mgmt_release_upcall_queue, HINIC3_DRV_FUNC_NO_PTR);
    RTF_LOG_HINIC_PORT_API_LOG(time_start, exec_time, ret, HINIC3_PORT_QUEUE_RELEASE,
                               ops->hovs_port_mgmt_release_upcall_queue(port_id, queue_id));
    return ret;
}

int hinic3_bond_mgmt_create(uint8_t mode, const char *name, uint16_t *bond_id)
{
    int ret;
    uint64_t time_start;
    uint64_t exec_time;
    struct hinic3_drv_ops *ops = NULL;

    if (bond_id == NULL)
    {
        HINIC3_LOG(WARNING, DRIVER, "Pointer parameter bond_id is NULL!");
        return -EINVAL;
    }

    ops = hinic3_get_drv_ops();
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_bond_mgmt_create, HINIC3_DRV_FUNC_NO_PTR);

    RTF_LOG_HINIC_PORT_API_LOG(time_start, exec_time, ret, HINIC3_BOND_MGMT_CREATE,
                               ops->hovs_bond_mgmt_create(mode, name, bond_id));
    return hinic3_convert_error_code(ret);
}

void hinic3_bond_mgmt_delete(uint16_t bond_id)
{
    uint64_t time_start;
    uint64_t exec_time;
    struct hinic3_drv_ops *ops = NULL;

    ops = hinic3_get_drv_ops();
    if (ops->hovs_bond_mgmt_delete == NULL)
    {
        HINIC3_LOG(ERR, DRIVER, "hovs_port_mgmt_del is NULL in ops!");
        return;
    }
    RTF_LOG_HINIC_PORT_API_LOG_VOID(time_start, exec_time, HINIC3_BOND_MGMT_DELETE, ops->hovs_bond_mgmt_delete(bond_id));
}

int hinic3_bond_mgmt_get(uint16_t bond_id, struct smap *args)
{
    int ret = -1;
    uint64_t time_start;
    uint64_t exec_time;
    void *buff = NULL;
    void *tmp_buff = NULL;
    struct hinic3_nlattr nla_args;
    struct hinic3_drv_ops *ops = NULL;

    if (args == NULL)
    {
        HINIC3_LOG(WARNING, DRIVER, "Pointer parameter is NULL!");
        return -EINVAL;
    }

    ops = hinic3_get_drv_ops();
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_bond_mgmt_get, HINIC3_DRV_FUNC_NO_PTR);

    buff = (void *)hinic3_calloc(1, HOVS_MAX_TLV_BUF_LEN, HINIC3_DRIVER_ADAPTER);
    if (!buff)
    {
        HINIC3_LOG(ERR, DRIVER, "Alloc memory buff error!");
        return -ENOMEM;
    }

    tmp_buff = buff;
    hinic3_nlattr_init(&nla_args, tmp_buff, HOVS_MAX_TLV_BUF_LEN);
    RTF_LOG_HINIC_PORT_API_LOG(time_start, exec_time, ret, HINIC3_BOND_MGMT_GET,
                               ops->hovs_bond_mgmt_get(bond_id, nla_args.data, (uint32_t *)&nla_args.used_len));
    if (ret != 0)
    {
        ret = hinic3_convert_error_code(ret);
        goto out;
    }

    hinic3_nlattr_reset_itr(&nla_args, nla_args.used_len);
    /* nlattr to smap */
    bond_args_nlattr_to_smap(&nla_args, args);

out:
    hinic3_free(buff);
    return ret;
}

int hinic3_etheraddr_get(uint16_t port_id, struct eth_address *mac)
{
    int ret;
    uint64_t time_start;
    uint64_t exec_time;
    struct hovs_eth_addr eth_addr = {{.ea = {0}}};
    struct hinic3_drv_ops *ops = NULL;

    if (mac == NULL)
    {
        HINIC3_LOG(WARNING, DRIVER, "Pointer parameter is NULL!");
        return -EINVAL;
    }
    ops = hinic3_get_drv_ops();
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_etheraddr_get, HINIC3_DRV_FUNC_NO_PTR);

    RTF_LOG_HINIC_PORT_API_LOG(time_start, exec_time, ret, HINIC3_ETHERADDR_GET,
                               ops->hovs_etheraddr_get(port_id, &eth_addr));
    if (ret != 0)
    {
        return hinic3_convert_error_code(ret);
    }

    memcpy(mac, &eth_addr, sizeof(struct hovs_eth_addr));

    return 0;
}

int hinic3_etheraddr_set(uint16_t port_id, struct eth_addr mac)
{
    struct hovs_eth_addr eth_addr = {{.ea = {0}}};
    struct hinic3_drv_ops *ops = NULL;

    ops = hinic3_get_drv_ops();
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_etheraddr_set, HINIC3_DRV_FUNC_NO_PTR);

    memcpy(&eth_addr, &mac, sizeof(struct eth_addr));

    return hinic3_convert_error_code(ops->hovs_etheraddr_set(port_id, eth_addr));
}

int hinic3_mtu_set(uint16_t port_id, int mtu)
{
    int ret;
    uint64_t time_start;
    uint64_t exec_time;
    struct hinic3_drv_ops *ops = NULL;
    ops = hinic3_get_drv_ops();
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_mtu_set, HINIC3_DRV_FUNC_NO_PTR);

    HINIC3_LOG(INFO, DRIVER, "Set mtu value %d.", mtu);
    RTF_LOG_HINIC_PORT_API_LOG(time_start, exec_time, ret, HINIC3_MTU_SET, ops->hovs_mtu_set(port_id, mtu));
    return hinic3_convert_error_code(ret);
}

int hinic3_security_get(uint16_t port_id, struct smap *args)
{
    int ret = -1;
    struct hinic3_nlattr nla_args;
    uint64_t time_start;
    uint64_t exec_time;
    void *buff = NULL;
    void *tmp_buff = NULL;
    struct hinic3_drv_ops *ops = NULL;

    if (args == NULL)
    {
        HINIC3_LOG(WARNING, DRIVER, "Pointer parameter is NULL!");
        return -EINVAL;
    }

    ops = hinic3_get_drv_ops();
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_bum_get, HINIC3_DRV_FUNC_NO_PTR);

    buff = (void *)hinic3_calloc(1, HOVS_MAX_TLV_BUF_LEN, HINIC3_DRIVER_ADAPTER);
    if (!buff)
    {
        HINIC3_LOG(ERR, DRIVER, "Alloc memory buff error!");
        return -ENOMEM;
    }

    tmp_buff = buff;
    hinic3_nlattr_init(&nla_args, tmp_buff, HOVS_MAX_TLV_BUF_LEN);
    RTF_LOG_HINIC_PORT_API_LOG(time_start, exec_time, ret, HINIC3_SECURITY_GET,
                               ops->hovs_bum_get(port_id, nla_args.data, (uint32_t *)&nla_args.used_len));
    if (ret != 0)
    {
        ret = hinic3_convert_error_code(ret);
        goto err;
    }

    hinic3_nlattr_reset_itr(&nla_args, nla_args.used_len);
    ret = bum_args_nlattr_to_smap(&nla_args, args);
    if (ret != 0)
    {
        goto err;
    }

err:
    hinic3_free(buff);
    return ret;
}

int hinic3_security_remove(uint16_t port_id, struct hinic3_nlattr *nla_args)
{
    uint64_t time_start;
    uint64_t exec_time;
    int ret = -1;
    struct hinic3_drv_ops *ops = NULL;

    ops = hinic3_get_drv_ops();
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_bum_remove, HINIC3_DRV_FUNC_NO_PTR);
    RTF_LOG_HINIC_PORT_API_LOG(time_start, exec_time, ret, HINIC3_SECURITY_REMOVE,
                               ops->hovs_bum_remove(port_id, nla_args->data, nla_args->used_len));
    return hinic3_convert_error_code(ret);
}

int hinic3_security_set(uint16_t port_id, struct hinic3_nlattr *nla_args, struct hinic3_nlattr *nla_unset_args)
{
    uint64_t time_start;
    uint64_t exec_time;
    int ret;
    struct hinic3_drv_ops *ops = NULL;

    ops = hinic3_get_drv_ops();
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_bum_set, HINIC3_DRV_FUNC_NO_PTR);
    RTF_LOG_HINIC_PORT_API_LOG(time_start, exec_time, ret, HINIC3_SECURITY_SET,
                               ops->hovs_bum_set(port_id, nla_args->data, nla_args->used_len, nla_unset_args->data,
                                                 (uint32_t *)&nla_unset_args->used_len));
    return hinic3_convert_error_code(ret);
}

int hinic3_port_statistics_get(uint16_t port_id, hinic3_port_stats *stats)
{
    const int data_count = HINIC3_DATA_COUNT;
    int ret;
    struct hovs_port_stats hovs_stats = {0};
    uint64_t time_start;
    uint64_t exec_time;
    struct hinic3_drv_ops *ops = NULL;

    if (stats == NULL)
    {
        HINIC3_LOG(WARNING, DRIVER, "Pointer parameter stats is NULL!");
        return -EINVAL;
    }

    ops = hinic3_get_drv_ops();
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_port_statistics_get, HINIC3_DRV_FUNC_NO_PTR);
    RTF_LOG_HINIC_PORT_API_LOG(time_start, exec_time, ret, HINIC3_PORT_STATISTICS_GET,
                               ops->hovs_port_statistics_get(port_id, &hovs_stats));
    if (ret != 0)
    {
        return hinic3_convert_error_code(ret);
    }

    memcpy(stats, &hovs_stats, sizeof(hovs_stats));
    memcpy(&stats->timestamp, &hovs_stats.timestamp, sizeof(uint64_t) * data_count);
    return 0;
}

int hinic3_port_statistics_flush(uint16_t port_id)
{
    int ret;
    uint64_t time_start;
    uint64_t exec_time;
    struct hinic3_drv_ops *ops = NULL;

    ops = hinic3_get_drv_ops();
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_port_statistics_flush, HINIC3_DRV_FUNC_NO_PTR);
    RTF_LOG_HINIC_PORT_API_LOG(time_start, exec_time, ret, HINIC3_PORT_STATISTICS_FLUSH,
                               ops->hovs_port_statistics_flush(port_id));
    return hinic3_convert_error_code(ret);
}

int hinic3_port_mgmt_get_capability(struct hinic3_port_capability *cap)
{
    uint64_t time_start;
    uint64_t exec_time;
    int ret;
    struct hovs_port_capability hovs_cap;
    struct hinic3_drv_ops *ops = NULL;

    ops = hinic3_get_drv_ops();
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_port_mgmt_get_capability, HINIC3_DRV_FUNC_NO_PTR);
    RTF_LOG_HINIC_PORT_API_LOG(time_start, exec_time, ret, HINIC3_PORT_MGMT_GET_CAPABILITY,
                               ops->hovs_port_mgmt_get_capability(&hovs_cap));
    if (ret != 0)
    {
        return hinic3_convert_error_code(ret);
    }

    cap->supported_upcall_qnum = hovs_cap.supported_upcall_qnum;
    return 0;
}

int hinic3_port_mgmt_get_upcall_info(struct hinic3_port_upcall_info *info)
{
    int ret;
    uint64_t time_start;
    uint64_t exec_time;
    struct hinic3_drv_ops *ops = NULL;

    ops = hinic3_get_drv_ops();
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_port_mgmt_get_upcall_info, HINIC3_DRV_FUNC_NO_PTR);
    RTF_LOG_HINIC_PORT_API_LOG(time_start, exec_time, ret, HINIC3_PORT_MGMT_GET_UPCALL_INFO,
                               ops->hovs_port_mgmt_get_upcall_info((struct hovs_port_upcall_info *)info));
    return hinic3_convert_error_code(ret);
}

int hinic3_port_mgmt_get_bond_slave_info(const uint16_t port_id, struct hovs_bond_slave_stats *bond_slave_info)
{
    int ret;
    uint64_t time_start;
    uint64_t exec_time;
    struct hinic3_drv_ops *ops = NULL;

    ops = hinic3_get_drv_ops();
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_bond_slave_statistics_get, HINIC3_DRV_FUNC_NO_PTR);
    RTF_LOG_HINIC_PORT_API_LOG(time_start, exec_time, ret, HINIC3_PORT_MGMT_GET_BOND_SALVE_INFO,
                               ops->hovs_bond_slave_statistics_get(port_id, bond_slave_info));
    return hinic3_convert_error_code(ret);
}

int hinic3_port_mgmt_set_usage_state(uint16_t port_id, uint16_t status)
{
    int ret;
    uint64_t time_start;
    uint64_t exec_time;
    struct hinic3_drv_ops *ops = NULL;

    ops = hinic3_get_drv_ops();
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_port_mgmt_set_usage_state, HINIC3_DRV_FUNC_NO_PTR);
    RTF_LOG_HINIC_PORT_API_LOG(time_start, exec_time, ret, HINIC3_PORT_MGMT_SET_USAGE_STATE,
                               ops->hovs_port_mgmt_set_usage_state(port_id, status));
    return hinic3_convert_error_code(ret);
}

int hinic3_port_mgmt_get_usage_state(uint16_t port_id, uint32_t *status)
{
    int ret;
    uint64_t time_start;
    uint64_t exec_time;
    struct hinic3_drv_ops *ops = NULL;

    ops = hinic3_get_drv_ops();
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_port_mgmt_get_usage_state, HINIC3_DRV_FUNC_NO_PTR);
    RTF_LOG_HINIC_PORT_API_LOG(time_start, exec_time, ret, HINIC3_PORT_MGMT_GET_USAGE_STATE,
                               ops->hovs_port_mgmt_get_usage_state(port_id, status));
    return hinic3_convert_error_code(ret);
}

int hinic3_port_upcall_mtu_set(uint8_t type, int mtu)
{
    int ret;
    uint64_t time_start;
    uint64_t exec_time;
    struct hinic3_drv_ops *ops = NULL;

    ops = hinic3_get_drv_ops();
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_upcall_mtu_set, HINIC3_DRV_FUNC_NO_PTR);
    RTF_LOG_HINIC_PORT_API_LOG(time_start, exec_time, ret, HINIC3_UPCALL_MTU_SET,
                               ops->hovs_upcall_mtu_set(type, mtu));
    return hinic3_convert_error_code(ret);
}

int16_t hinic3_get_tx_queue_count(uint16_t port_id, uint16_t queue_id)
{
    int16_t ret;
    uint64_t time_start;
    uint64_t exec_time;
    struct hinic3_drv_ops *ops = NULL;

    ops = hinic3_get_drv_ops();
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_rte_get_txq_backlog, HINIC3_DRV_FUNC_NO_PTR);
    RTF_LOG_HINIC_PORT_API_LOG_LESS_THAN_ZERO(time_start, exec_time, ret, HINIC3_GET_TX_QUEUE_COUNT,
                                              ops->hovs_rte_get_txq_backlog(port_id, queue_id));
    return ret;
}

int16_t hinic3_get_rx_queue_count(uint16_t port_id, uint16_t queue_id)
{
    int16_t ret;
    uint64_t time_start;
    uint64_t exec_time;
    struct hinic3_drv_ops *ops = NULL;

    ops = hinic3_get_drv_ops();
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_rte_get_rxq_backlog, HINIC3_DRV_FUNC_NO_PTR);
    RTF_LOG_HINIC_PORT_API_LOG_LESS_THAN_ZERO(time_start, exec_time, ret, HINIC3_GET_RX_QUEUE_COUNT,
                                              ops->hovs_rte_get_rxq_backlog(port_id, queue_id));
    return ret;
}

hinic3_port_api hinic3_port_get_api_index(const char *api_name)
{
    int i;

    for (i = 0; i < HINIC3_PORT_API_MAX; ++i)
    {
        if (g_port_api_arr[i].api_name == NULL)
        {
            continue;
        }
        if (strncmp(api_name, g_port_api_arr[i].api_name, strlen(g_port_api_arr[i].api_name) + 1) == 0)
        {
            return g_port_api_arr[i].api_index;
        }
    }
    return HINIC3_PORT_API_MAX;
}

const char *hinic3_port_get_api_name(hinic3_port_api api_index)
{
    int i;

    for (i = 0; i < HINIC3_PORT_API_MAX; ++i)
    {
        if (api_index == g_port_api_arr[i].api_index)
        {
            return g_port_api_arr[i].api_name;
        }
    }
    return NULL;
}

static int hinic3_vdpa_feature_add_mtu(uint64_t *vdpa_feature)
{
    int ret;
    struct hinic3_drv_ops *ops = NULL;
    uint64_t default_feature = 0;
    uint64_t support_feature = 0;

    ops = hinic3_get_drv_ops();
    if (ops->hovs_global_device_feature_get == NULL)
    {
        HINIC3_LOG(ERR, DRIVER, "hovs_global_device_feature_get is NULL in ops!");
        return -1;
    }
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_global_device_feature_get, HINIC3_DRV_FUNC_NO_PTR);

    ret = ops->hovs_global_device_feature_get(&default_feature, &support_feature);
    if (ret != 0)
    {
        return -1;
    }

    *vdpa_feature = default_feature | (1 << VIRTIO_NET_F_MTU);
    return 0;
}

int hinic3_port_mgmt_add_dynamic(uint16_t *port_id, const struct smap *args)
{
    int ret;
    uint64_t time_start;
    uint64_t exec_time;
    void *buff = NULL;
    void *tmp_buff = NULL;
    struct hinic3_nlattr nla_args;
    struct hinic3_drv_ops *ops = NULL;
    uint64_t vdpa_feature = 0;

    if (!args)
    {
        HINIC3_LOG(WARNING, DRIVER, "Pointer parameter is NULL!");
        return -EINVAL;
    }
    ops = hinic3_get_drv_ops();
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_port_mgmt_add_dynamic, HINIC3_DRV_FUNC_NO_PTR);

    buff = (void *)hinic3_calloc(1, HOVS_MAX_TLV_BUF_LEN, HINIC3_DRIVER_ADAPTER);
    if (!buff)
    {
        HINIC3_LOG(ERR, DRIVER, "Alloc memory buff error!");
        return -ENOMEM;
    }

    tmp_buff = buff;
    hinic3_nlattr_init(&nla_args, tmp_buff, HOVS_MAX_TLV_BUF_LEN);
    /* smap to nlattr */
    port_args_smap_to_nlattr(args, &nla_args);

    if (hinic3_card_mod_get() != PROG_MODE)
    {
        /* vdpa feature */
        ret = hinic3_vdpa_feature_add_mtu(&vdpa_feature);
        if (ret != 0)
        {
            HINIC3_LOG(ERR, DRIVER, "Get featrue failed!");
            hinic3_free(buff);
            return -EPERM;
        }
    } else {
        vdpa_feature = hinic3_vdpa_feature_get();
    }
    if (vdpa_feature != 0)
    {
        hinic3_nlattr_put_u64(&nla_args, HINIC3_PORT_ARG_DEVICE_FEATURE, vdpa_feature);
    }
    RTF_LOG_HINIC_PORT_API_LOG(time_start, exec_time, ret, HINIC3_PORT_MGMT_ADD_DYNAMIC,
                               ops->hovs_port_mgmt_add_dynamic(port_id, nla_args.data, nla_args.used_len));
    hinic3_free(buff);
    return hinic3_convert_error_code(ret);
}

static void hinic3_port_format_api(const char *api_name, hiovs_api_record *rec, struct ds *ds, bool is_all)
{
    char time_str[HINIC3_TIME_STR_LEN] = {0};
    char *pstr = time_str;
    int title_ui_space;
    int value_ui_space;

    if (is_all)
    {
        title_ui_space = SHOW_PORT_API_UI_FOUR_SPACE;
        value_ui_space = SHOW_PORT_API_UI_SIX_SPACE;
    }
    else
    {
        title_ui_space = SHOW_PORT_API_UI_TWO_SPACE;
        value_ui_space = SHOW_PORT_API_UI_FOUR_SPACE;
    }
    hinic3_ds_put_format(ds, "%*sQuery %s:\n", title_ui_space, HINIC3_UI_INDENT_SPACE, api_name);
    hinic3_ds_put_format(ds, "%*smax api time(us):        %u\n", value_ui_space,
                         HINIC3_UI_INDENT_SPACE, rec->max_api_time);
    hinic3_ds_put_format(ds, "%*smin api time(us):        %u\n", value_ui_space,
                         HINIC3_UI_INDENT_SPACE, rec->min_api_time);
    if ((uint64_t)rte_atomic64_read(&rec->call_total_count) != 0)
    {
        hinic3_ds_put_format(ds, "%*saverage api time(us):    %u\n", value_ui_space, HINIC3_UI_INDENT_SPACE,
                             rec->total_api_time / (uint64_t)rte_atomic64_read(&rec->call_total_count));
    }
    else
    {
        hinic3_ds_put_format(ds, "%*saverage api time(us):    0\n", value_ui_space, HINIC3_UI_INDENT_SPACE);
    }
    hinic3_ds_put_format(ds, "%*stotal call counts:       %u\n", value_ui_space,
                         HINIC3_UI_INDENT_SPACE, (uint64_t)rte_atomic64_read(&rec->call_total_count));
    hinic3_ds_put_format(ds, "%*serror call counts:       %u\n", value_ui_space,
                         HINIC3_UI_INDENT_SPACE, (uint64_t)rte_atomic64_read(&rec->call_error_count));
    if ((uint64_t)rte_atomic64_read(&rec->call_error_count) != 0)
    {
        ctime_r(&rec->time_error, pstr);
        hinic3_ds_put_format(ds, "%*slast error info:         return %lld in %s\n",
                             value_ui_space, HINIC3_UI_INDENT_SPACE, rec->last_rtn_value, pstr);
    }
}

static int hinic3_port_show_one_api(struct unixctl_conn *conn, int argc, const char *argv[])
{
    int ret;
    const char *api_name = NULL;
    hiovs_api_record rec;
    struct ds ds = DS_EMPTY_INITIALIZER;

    if (argc < SHOW_ONE_API_ARG_NUM)
    {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR, "Incomplete command, please type -h or --help for help.\n");
        goto err;
    }
    else if (argc > SHOW_ONE_API_ARG_NUM)
    {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR, "Too many command, please type -h or --help for help.\n");
        goto err;
    }

    api_name = argv[1];
    if (hinic3_port_get_api_index(api_name) == HINIC3_PORT_API_MAX)
    {
        hinic3_ds_put_format(&ds, "%sWrong parameter, api-name is invalid [%s].\n",
                             HINIC3_UI_LEADING_SIGN_ERROR, api_name);
        goto err;
    }
    ret = hinic3_port_get_api_record(api_name, false, false, &rec, 1);
    if (ret != 0)
    {
        hinic3_ds_put_format(&ds, "%sWrong parameter, api-name is invalid [%s].\n",
                             HINIC3_UI_LEADING_SIGN_ERROR, api_name);
        goto err;
    }

    hinic3_port_format_api(api_name, &rec, &ds, false);
    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
    return 0;

err:
    hinic3_command_reply_error(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
    return -1;
}

static int hinic3_port_show_all_api(struct unixctl_conn *conn, int argc)
{
    int i;
    int ret;
    const char *api_name = NULL;
    struct ds ds = DS_EMPTY_INITIALIZER;
    hiovs_api_record *rec_list = NULL;

    if (argc < 1)
    {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR "Incomplete command, type -h or --help for help.\n");
        goto err;
    }
    else if (argc > 1)
    {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR "Too many parameters, type -h or --help for help.\n");
        goto err;
    }
    rec_list = (hiovs_api_record *)hinic3_calloc(HINIC3_PORT_API_MAX, sizeof(hiovs_api_record), HINIC3_DRIVER_ADAPTER);
    if (!rec_list)
    {
        hinic3_ds_put_format(&ds, "%scalloc memory failed!\n", HINIC3_UI_LEADING_SIGN_ERROR);
        goto err;
    }

    ret = hinic3_port_get_api_record("", true, false, rec_list, HINIC3_PORT_API_MAX);
    if (ret != 0)
    {
        hinic3_free(rec_list);
        hinic3_ds_put_format(&ds, "%sinternal error, please check log!\n", HINIC3_UI_LEADING_SIGN_ERROR);
        goto err;
    }

    hinic3_ds_put_format(&ds, "%2sAPI Counts: %d\n", HINIC3_UI_INDENT_SPACE, HINIC3_PORT_API_MAX);
    for (i = 0; i < HINIC3_PORT_API_MAX; i++)
    {
        api_name = hinic3_port_get_api_name(i);
        if (api_name == NULL)
        {
            hinic3_free(rec_list);
            goto err;
        }
        hinic3_port_format_api(api_name, &rec_list[i], &ds, true);
        if (i != HINIC3_PORT_API_MAX - 1)
        {
            hinic3_ds_put_format(&ds, "\n");
        }
    }

    hinic3_free(rec_list);
    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
    return 0;

err:
    hinic3_command_reply_error(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
    return -1;
}

static int hinic3_port_clear_api(struct unixctl_conn *conn, int argc, const char *argv[])
{
    int ret;
    struct ds ds = DS_EMPTY_INITIALIZER;
    const char *api_name = NULL;
    hiovs_api_record rec;

    if (argc < CLEAR_PORT_API_ARG_NUM)
    {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR, "Incomplete command, please type -h or --help for help.\n");
        goto err;
    }
    else if (argc > CLEAR_PORT_API_ARG_NUM)
    {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR, "Too many command, please type -h or --help for help.\n");
        goto err;
    }

    if (strcmp("all", argv[CLEAR_PORT_API_ARG_NUM - 1]) == 0)
    {
        ret = hinic3_port_get_api_record("", true, true, NULL, 0);
        if (ret != 0)
        {
            HINIC3_LOG(ERR, DRIVER, "Clear all port api record failed!");
            hinic3_ds_put_format(&ds, "%sClear port API records failed [all]!\n", HINIC3_UI_LEADING_SIGN_ERROR);
            goto err;
        }
        HINIC3_LOG(INFO, DRIVER, "Clear all port api record successful.");
        hinic3_ds_put_format(&ds, "%sClear port API records successful [all].\n", HINIC3_UI_LEADING_SIGN_INFO);
    }
    else
    {
        api_name = argv[CLEAR_PORT_API_ARG_NUM - 1];
        if (hinic3_port_get_api_index(api_name) == HINIC3_PORT_API_MAX)
        {
            hinic3_ds_put_format(&ds, "%sWrong parameter, api-name is invalid [%s].\n", HINIC3_UI_LEADING_SIGN_ERROR, api_name);
            goto err;
        }
        ret = hinic3_port_get_api_record(api_name, false, true, &rec, 1);
        if (ret != 0)
        {
            HINIC3_LOG(ERR, DRIVER, "Clear port api record failed!");
            hinic3_ds_put_format(&ds, "%sClear port API record failed [%s]!\n", HINIC3_UI_LEADING_SIGN_ERROR, api_name);
            goto err;
        }
        HINIC3_LOG(INFO, DRIVER, "Clear port api record successful.");
        hinic3_ds_put_format(&ds, "%sClear port API record successful [%s].\n", HINIC3_UI_LEADING_SIGN_INFO, api_name);
    }

    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
    return 0;
err:
    hinic3_command_reply_error(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
    return -1;
}

static void hinic3_port_api_show_cmd_help(struct unixctl_conn *conn, int argc)
{
    int i;
    const char *api_name = NULL;
    struct ds ds = DS_EMPTY_INITIALIZER;
    if (argc < 1)
    {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR "Incomplete command, type -h or --help for help.\n");
        goto err;
    }
    else if (argc > 1)
    {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR "Too many parameters, type -h or --help for help.\n");
        goto err;
    }
    hinic3_ds_put_format(&ds, "%2sUsage: dpak-ovs-ctl hwoff/show-port-api"
                              " { all | clear { all | <api-name> } | api <api-name> | { -h | --help } }\n",
                         HINIC3_UI_INDENT_SPACE);
    hinic3_ds_put_format(&ds, "\n");
    hinic3_ds_put_format(&ds, "%2s%s\n", HINIC3_UI_INDENT_SPACE, HINIC3_UI_FLOW_OPTION_LIST_STRING);
    hinic3_ds_put_format(&ds, "%4s%s", HINIC3_UI_INDENT_SPACE, HINIC3_UI_FLOW_SHOW_ALL_API_HELP_STRING);
    hinic3_ds_put_format(&ds, "%4s%s", HINIC3_UI_INDENT_SPACE, HINIC3_UI_FLOW_CLEAR_ALL_API_HELP_STRING);
    hinic3_ds_put_format(&ds, "%4s%s", HINIC3_UI_INDENT_SPACE, HINIC3_UI_FLOW_CLEAR_ONE_API_HELP_STRING);
    hinic3_ds_put_format(&ds, "%4s%s", HINIC3_UI_INDENT_SPACE, HINIC3_UI_FLOW_SHOW_ONE_API_HELP_STRING);
    hinic3_ds_put_format(&ds, "%4s%s", HINIC3_UI_INDENT_SPACE, HINIC3_UI_FLOW_HELP_STRING);
    hinic3_ds_put_format(&ds, "\n");
    hinic3_ds_put_format(&ds, "%2sapi names that can be input are as follows:", HINIC3_UI_INDENT_SPACE);
    for (i = 0; i < HINIC3_PORT_API_MAX; ++i)
    {
        api_name = hinic3_port_get_api_name(i);
        if (api_name == NULL)
        {
            continue;
        }
        if (i % SHOW_MAX_API_ONE_LINE == 0)
        {
            hinic3_ds_put_format(&ds, "\n");
            hinic3_ds_put_format(&ds, "%4s", HINIC3_UI_INDENT_SPACE);
        }
        hinic3_ds_put_format(&ds, "%-36s", api_name);
    }
    hinic3_ds_put_format(&ds, "\n");
    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
    return;
err:
    hinic3_command_reply_error(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
    return;
}

static void unixctl_hinic3_show_port_api(struct unixctl_conn *conn, int argc, const char *argv[], void *aux)
{
    int work_argc = argc;
    const char **work_argv = argv;

    work_argc -= 1;
    work_argv += 1;
    if (strcmp("api", work_argv[0]) == 0)
    {
        *(int *)aux = hinic3_port_show_one_api(conn, work_argc, work_argv);
        return;
    }

    if ((strcmp("all", work_argv[0]) == 0) && work_argc == 1)
    {
        *(int *)aux = hinic3_port_show_all_api(conn, work_argc);
        return;
    }

    if ((strcmp("clear", work_argv[0]) == 0))
    {
        *(int *)aux = hinic3_port_clear_api(conn, work_argc, work_argv);
        return;
    }

    if (((strcmp("-h", work_argv[0]) == 0) || (strcmp("--help", work_argv[0]) == 0)) && work_argc == 1)
    {
        *(int *)aux = 0;
        return hinic3_port_api_show_cmd_help(conn, work_argc);
    }

    *(int *)aux = -1;
    hinic3_command_reply_error(conn, HINIC3_UI_LEADING_SIGN_ERROR "Unrecognized command, type -h or --help for help.\n");
    return;
}

int hinic3_port_class_init(void)
{
    hiovs_api_record *port_api_record = NULL;
    port_api_record = hinic3_get_port_api_record();

    memset(port_api_record, 0, sizeof(hiovs_api_record) * HINIC3_PORT_API_MAX);

    hinic3_command_register("hwoff/show-port-api", "{ all | clear "
                                                   "{ all | <api-name> } | api <api-name> | { -h | --help } }",
                            HINIC3_SHOW_PORT_API_MIN_ARG, HINIC3_SHOW_PORT_API_MAX_ARG, unixctl_hinic3_show_port_api, NULL);
    return 0;
}

void hinic3_port_class_uninit(void)
{
    hiovs_api_record *port_api_record = NULL;
    port_api_record = hinic3_get_port_api_record();

    memset(port_api_record, 0, sizeof(hiovs_api_record) * HINIC3_PORT_API_MAX);
}
