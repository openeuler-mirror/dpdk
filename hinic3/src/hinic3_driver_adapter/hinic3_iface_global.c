/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */
#include <string.h>
#include <pthread.h>
#include <rte_atomic.h>
#include "rte_cycles.h"
#include "rte_ether.h"
#include "rte_lcore.h"
#include "rte_malloc.h"
#include "hinic3_packets.h"
#include "hinic3_init.h"
#include "hinic3_log.h"
#include "hinic3_provider.h"
#include "hinic3_message.h"
#include "hinic3_tlv_key.h"
#include "hinic3_iface_flow.h"
#include "hinic3_flow_agent.h"
#include "hinic3_util.h"
#include "hinic3_meminfo.h"
#include "hinic3_hugepage_meminfo.h"
#include "hinic3_smap.h"
#include "hinic3_ds.h"
#include "hinic3_iface_global.h"
#include "hinic3_command.h"
#include "hinic3_parse_agent_config.h"
#include "hinic3_ui_string.h"
#include "hinic3_iface_global_api_record.h"
#include "hinic3_iface_flow_api_record.h"
#include "hinic3_port_util.h"

#define MAX_ARGS_SIZE (2048)
#define MAX_KEY_VAL_LEN (64)
#define DEFAULT_LOCAL_IP "0.0.0.0"
#define DEFAULT_ZERO_VALUE "0"
#define GLOBAL_ARGS_SIZE (128)
#define HINIC3_IO_MODE_LEN (10)
#define VLAN_HEADER_LEN 4
#define NETDEV_DPDK_MBUF_ALIGN 1024
#define DEFAULT_MTU 1500
#define HINIC3_DEC_BASE 10
#define DP_HASH_FLOW_MAX_NUM 64
#define HINIC3_HIGH_BANDWIDTH_MODE_ADAPTE 2

/*
 * PF upcall queue depth.
 * PF4 has 64 queues with a single queue depth of 1024 .
 * PF5 has 256 queues with a single queue depth of 512 .
 * Reserve 1024 * 64 mbufs for command delivery.
 */
#define HINIC3_VTEP_TABLE_MAX_IP_ADDR 8

#define SHOW_ONE_API_ARG_NUM 2
#define CLEAR_GLOBAL_API_ARG_NUM 2
#define SHOW_MAX_API_ONE_LINE 4
#define SHOW_GLOBAL_API_UI_TWO_SPACE 2
#define SHOW_GLOBAL_API_UI_FOUR_SPACE 4
#define SHOW_GLOBAL_API_UI_SIX_SPACE 6

/* hardware global args flag */
#define HINIC3_FLAG_RX_THREAD_NUM (1LLU << 0)
#define HINIC3_FLAG_MAX_FLOW_NUM (1LLU << 1)
#define RTE_DP_PACKETS_SIZE 1152
#define HINIC3_PMD_NUM_DEFAULT 2
#define HINIC3_FLOW_UNAGE_FLAG "1"
#define HINIC3_FLOW_AGE_FLAG "0"
#define VLAN_HEADER_COUNT 2
#define HINIC3_FORWARD_MODE_BUFFER_LEN 64
#define HINIC3_BOND_HASH_POLICY_LEN 64

struct hinic3_vtep_ip_get_args
{
    uint32_t num;
    struct hinic3_vtep_ip dip[HINIC3_VTEP_TABLE_MAX_IP_ADDR];
};

struct hiovs_global_map
{
    hinic3_global_api api_index;
    const char *api_name;
};

static pthread_mutex_t g_hinic3_global_mutex = PTHREAD_MUTEX_INITIALIZER;
static rte_spinlock_t g_hinic3_tx_burst_lock[MAX_TX_QUEUE_PER_VPORT];

static volatile struct rte_mempool *g_dpdk_shared_mp = NULL;
/* hardware global args set flag */
static uint64_t g_global_args_flag = 0;
// please modify hinic3_global_api when modify this structure
static const struct hiovs_global_map g_global_api_arr[HINIC3_GLOBAL_API_MAX] = {
    {HINIC3_GLOBAL_CFG_SET, "hovs_global_cfg_set"},
    {HINIC3_GLOBAL_CFG_GET, "hovs_global_cfg_get"},
    {HINIC3_GLOBAL_STATISTICS_GET, "hovs_global_statistics_get"},
    {HINIC3_GLOBAL_STATISTICS_FLUSH, "hovs_global_statistics_flush"},
    {HINIC3_GLOBAL_OPEN_LOG, "hovs_open_log"},
    {HINIC3_GLOBAL_SET_LOG_LEVEL, "hovs_set_log_level"},
    {HINIC3_GLOBAL_CMD_EXEC, "hovs_mml_lib"},
    {HINIC3_GLOBAL_PCIE_LIST_QUERY, "hovs_global_pcie_list_query"},
};

struct hovs_global_api_func_map
{
    hinic3_global_api api_index;
    int (*func)(struct hinic_global_api_args *args);
};

static int hovs_global_cfg_set_wrapper(struct hinic_global_api_args *args)
{
    struct hinic3_drv_ops *ops = NULL;
    ops = hinic3_get_drv_ops();
    if (hinic3_card_mod_get() == PROG_MODE)
    {
        HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_flexda_global_cfg_set, HINIC3_DRV_FUNC_NO_PTR);
        return ops->hovs_flexda_global_cfg_set(args->in_nlattr_data, args->in_nlattr_len, args->out_nlattr_data,
                                               args->out_nlattr_len);
    }
    else
    {
        HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_global_cfg_set, HINIC3_DRV_FUNC_NO_PTR);
        return ops->hovs_global_cfg_set(args->in_nlattr_data, args->in_nlattr_len, args->out_nlattr_data,
                                        args->out_nlattr_len);
    }
}

static int hovs_global_cfg_get_wrapper(struct hinic_global_api_args *args)
{
    struct hinic3_drv_ops *ops = NULL;
    ops = hinic3_get_drv_ops();
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_global_cfg_get, HINIC3_DRV_FUNC_NO_PTR);
    return ops->hovs_global_cfg_get(args->out_nlattr_data, args->out_nlattr_len);
}

static int hovs_global_statistics_get_wrapper(struct hinic_global_api_args *args)
{
    struct hinic3_drv_ops *ops = NULL;
    ops = hinic3_get_drv_ops();
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_global_statistics_get, HINIC3_DRV_FUNC_NO_PTR);
    return ops->hovs_global_statistics_get(args->hovs_stats);
}

static int hovs_global_statistics_flush_wrapper(struct hinic_global_api_args *args __rte_unused)
{
    struct hinic3_drv_ops *ops = NULL;
    ops = hinic3_get_drv_ops();
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_global_statistics_flush, HINIC3_DRV_FUNC_NO_PTR);
    return ops->hovs_global_statistics_flush();
}

static int hovs_open_log_wrapper(struct hinic_global_api_args *args)
{
    struct hinic3_drv_ops *ops = NULL;
    ops = hinic3_get_drv_ops();
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_open_log, HINIC3_DRV_FUNC_NO_PTR);
    return ops->hovs_open_log(args->log_module, args->log_level_or_enable);
}

static int hovs_set_log_level_wrapper(struct hinic_global_api_args *args)
{
    struct hinic3_drv_ops *ops = NULL;
    ops = hinic3_get_drv_ops();
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_set_log_level, HINIC3_DRV_FUNC_NO_PTR);
    return ops->hovs_set_log_level(args->log_module, args->log_level_or_enable);
}

static int hovs_mml_lib_wrapper(struct hinic_global_api_args *args)
{
    struct hinic3_drv_ops *ops = NULL;
    ops = hinic3_get_drv_ops();
    int ret;
    if (hinic3_card_mod_get() == PROG_MODE) {
        HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_flexda_mml_lib, HINIC3_DRV_FUNC_NO_PTR);
        HINIC3_LOG_HINIC_FLOW_API_LOG_NO_LOG(ret, HINIC3_FLEXDA_MML_LIB,
            ops->hovs_flexda_mml_lib(args->cmd_in, args->cmd_in_len, args->cmd_out,
            args->cmd_out_len, args->cmd_out_buf_size));
        return ret;
    } else {
        HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_mml_lib, HINIC3_DRV_FUNC_NO_PTR);
        return ops->hovs_mml_lib(args->cmd_in, args->cmd_in_len, args->cmd_out, args->cmd_out_len, args->cmd_out_buf_size);
    }
}

static int hovs_global_pcie_list_query_wrapper(struct hinic_global_api_args *args)
{
    struct hinic3_drv_ops *ops = NULL;
    ops = hinic3_get_drv_ops();
    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_global_pcie_list_query, HINIC3_DRV_FUNC_NO_PTR);
    return ops->hovs_global_pcie_list_query(args->front_back, args->bdf_type, args->dev);
}

struct hovs_global_api_func_map hovs_global_api_func_map_array[] = {
    {HINIC3_GLOBAL_CFG_SET, hovs_global_cfg_set_wrapper},
    {HINIC3_GLOBAL_CFG_GET, hovs_global_cfg_get_wrapper},
    {HINIC3_GLOBAL_STATISTICS_GET, hovs_global_statistics_get_wrapper},
    {HINIC3_GLOBAL_STATISTICS_FLUSH, hovs_global_statistics_flush_wrapper},
    {HINIC3_GLOBAL_OPEN_LOG, hovs_open_log_wrapper},
    {HINIC3_GLOBAL_SET_LOG_LEVEL, hovs_set_log_level_wrapper},
    {HINIC3_GLOBAL_CMD_EXEC, hovs_mml_lib_wrapper},
    {HINIC3_GLOBAL_PCIE_LIST_QUERY, hovs_global_pcie_list_query_wrapper},
};

static int hinic3_log_hinic_global_api_log(hinic3_global_api api_index, struct hinic_global_api_args *args)
{
    int ret = -1;
    uint64_t time_start;
    uint64_t exec_time = 0;

    if (api_index >= HINIC3_GLOBAL_CFG_SET && api_index < HINIC3_GLOBAL_API_MAX)
    {
        time_start = rte_get_tsc_cycles();

        ret = hovs_global_api_func_map_array[api_index].func(args);
        uint64_t hz = rte_get_tsc_hz();
        if (hz != 0)
        {
            exec_time = ((rte_get_tsc_cycles() - time_start) * 1000000UL) / hz;
        }
        hinic_global_fill_api_record(api_index, exec_time);
        hinic_global_api_record_error(ret, (hinic3_port_api)api_index, exec_time);
    }
    return ret;
}

static int global_cfg_parse_action(const char *action, uint8_t *val)
{
    if (strncmp(action, GLOBAL_CFG_ACTION_DROP_STR, strlen(action) + 1) == 0)
    {
        *val = GLOBAL_CFG_ACTION_DROP;
    }
    else if (strncmp(action, GLOBAL_CFG_ACTION_UPCALL_STR, strlen(action) + 1) == 0)
    {
        *val = GLOBAL_CFG_ACTION_UPCALL;
    }
    else
    {
        return -EINVAL;
    }

    return 0;
}

static const char *global_cfg_action_stringfy(uint8_t val)
{
    if (val == GLOBAL_CFG_ACTION_DROP)
    {
        return GLOBAL_CFG_ACTION_DROP_STR;
    }
    else if (val == GLOBAL_CFG_ACTION_UPCALL)
    {
        return GLOBAL_CFG_ACTION_UPCALL_STR;
    }

    return NULL;
}

static const char *global_cfg_unage_flag_stringfy(uint8_t val)
{
    if (val == 1)
    {
        return HINIC3_FLOW_UNAGE_FLAG;
    }
    else if (val == 0)
    {
        return HINIC3_FLOW_AGE_FLAG;
    }

    return NULL;
}

static int global_cfg_parse_thread_mode(const char *mode, uint8_t *val)
{
    if (strncmp(mode, GLOBAL_CFG_THREAD_MODE_ROUND_ROBIN_STR, strlen(mode) + 1) == 0)
    {
        *val = GLOBAL_CFG_THREAD_MODE_ROUND_ROBIN;
    }
    else if (strncmp(mode, GLOBAL_CFG_THREAD_MODE_INTERRUPT_STR, strlen(mode) + 1) == 0)
    {
        *val = GLOBAL_CFG_THREAD_MODE_INTERRUPT;
    }
    else
    {
        return -EINVAL;
    }

    return 0;
}

static int global_cfg_parse_pmd_mode(const char *mode, uint8_t *val)
{
    if (strncmp(mode, HINIC3_GLOBAL_CFG_ARG_PMD_MODE_STOP_STR, strlen(mode) + 1) == 0)
    {
        *val = GLOBAL_CFG_PMD_MODE_STOP;
    }
    else if (strncmp(mode, HINIC3_GLOBAL_CFG_ARG_PMD_MODE_START_STR, strlen(mode) + 1) == 0)
    {
        *val = GLOBAL_CFG_PMD_MODE_START;
    }
    else
    {
        return -EINVAL;
    }

    return 0;
}

static const char *global_cfg_thread_mode_stringfy(uint8_t val)
{
    if (val == GLOBAL_CFG_THREAD_MODE_ROUND_ROBIN)
    {
        return GLOBAL_CFG_THREAD_MODE_ROUND_ROBIN_STR;
    }
    else if (val == GLOBAL_CFG_THREAD_MODE_INTERRUPT)
    {
        return GLOBAL_CFG_THREAD_MODE_INTERRUPT_STR;
    }

    return NULL;
}

static int global_cfg_parse_action_check_mode(const char *mode, uint8_t *val)
{
    if (strncmp(mode, HINIC3_GLOBAL_CFG_UPCALL_PUSH_VXLAN_ON_STR, strlen(mode) + 1) == 0)
    {
        *val = GLOBAL_ACTION_CHECK_ON;
    }
    else if (strncmp(mode, HINIC3_GLOBAL_CFG_UPCALL_PUSH_VXLAN_OFF_STR, strlen(mode) + 1) == 0)
    {
        *val = GLOBAL_ACTION_CHECK_OFF;
    }
    else
    {
        return -EINVAL;
    }

    return 0;
}

static const char *g_global_smap_key[HINIC3_GLOBAL_CFG_ARG_TYPE_MAX] = {
    HINIC3_GLOBAL_CFG_ARG_VLAN_ETHTYPE_STR,
    HINIC3_GLOBAL_CFG_ARG_RSS_VF_NUM_STR,
    HINIC3_GLOBAL_CFG_ARG_VXLAN_LOCAL_IP_STR,
    HINIC3_GLOBAL_CFG_ARG_PCI_VENDOR_ID_STR,
    HINIC3_GLOBAL_CFG_ARG_PCI_DEVICE_ID_STR,
    HINIC3_GLOBAL_CFG_ARG_PCI_SUB_VENDOR_ID_STR,
    HINIC3_GLOBAL_CFG_ARG_PCI_SUB_DEVICE_ID_STR,
    NULL,
    HINIC3_GLOBAL_CFG_ARG_INVALID_TCP_ACTION_STR,
    HINIC3_GLOBAL_CFG_ARG_IP_FRAG_ACTION_STR,
    HINIC3_GLOBAL_CFG_ARG_THREAD_MODE_STR,
    HINIC3_GLOBAL_CFG_ARG_FLOW_AGE_TIME_STR,
    HINIC3_GLOBAL_CFG_ARG_PTHREAD_FDS_STR,
    NULL,
    HINIC3_GLOBAL_CFG_ARG_VF_INFO_STR,
    HINIC3_GLOBAL_CFG_ARG_VF_ENABLE_STR,
    HINIC3_GLOBAL_CFG_ARG_PMD_MODE_STR,
    HINIC3_GLOBAL_CFG_ARG_PCAP_PROBE_STR,
    HINIC3_GLOBAL_GET_PCAP_PROBE_STATS_STR,
    HINIC3_GLOBAL_CFG_ARG_ETP_STR,
    HINIC3_GLOBAL_GET_ETP_STATS_STR,
    HINIC3_GLOBAL_CFG_ARG_PHY_DEV_INFO_STR,
    HINIC3_GLOBAL_CFG_UPCALL_PUSH_VXLAN_STR,
    HINIC3_GLOBAL_CFG_ARG_QUEUE_POOL_SIZE_STR,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    HINIC3_GLOBAL_CFG_UNAGE_FLAG_STR,
    NULL,
    HINIC3_GLOBAL_CFG_PKT_FORWARD_MOD_STR,
};

static enum hinic3_global_cfg_arg_type get_nl_type_by_smap_key(char *key)
{
    int i;
    for (i = 0; i < HINIC3_GLOBAL_CFG_ARG_TYPE_MAX; i++)
    {
        if (g_global_smap_key[i] && strncmp(key, g_global_smap_key[i], strlen(key) + 1) == 0)
        {
            break;
        }
    }

    return (enum hinic3_global_cfg_arg_type)i;
}

static void handle_nl_put_unspec(struct hinic3_nlattr *nla, enum hinic3_global_cfg_arg_type type, char *value)
{
    size_t size = strlen(value) + 1;
    hinic3_nlattr_put_unspec(nla, type, value, size);
}

static int handle_nl_put_u8(struct hinic3_nlattr *nla, enum hinic3_global_cfg_arg_type type, char *value)
{
    uint8_t val = 0;
    int ret = 0;
    char *end_ptr = NULL;

    if (type == HINIC3_GLOBAL_CFG_ARG_VLAN_ETHTYPE &&
        strncmp(value, GLOBAL_VLAN_ETHTYPE_8021Q_STR, strlen(value) + 1) != 0)
    {
        val = GLOBAL_VLAN_ETHTYPE_8021Q;
        ret = 0;
    }
    else if (type == HINIC3_GLOBAL_CFG_ARG_INVALID_TCP_ACTION || type == HINIC3_GLOBAL_CFG_ARG_IP_FRAG_ACTION)
    {
        ret = global_cfg_parse_action(value, &val);
    }
    else if (type == HINIC3_GLOBAL_CFG_ARG_THREAD_MODE)
    {
        ret = global_cfg_parse_thread_mode(value, &val);
    }
    else if (type == HINIC3_GLOBAL_CFG_ARG_PMD_MODE)
    {
        ret = global_cfg_parse_pmd_mode(value, &val);
    }
    else if (type == HINIC3_GLOBAL_CFG_ARG_ACTION_CHECKING)
    {
        ret = global_cfg_parse_action_check_mode(value, &val);
    }
    else if (type == HINIC3_GLOBAL_CFG_UNAGE_FLAG)
    {
        uint64_t temp = strtol(value, &end_ptr, HINIC3_DEC_BASE);
        if (end_ptr == value || end_ptr == NULL || temp > UINT8_MAX)
        {
            HINIC3_LOG(ERR, DRIVER, "strtol in global unage cfg failed!");
            ret = -1;
        }
        else
        {
            val = (uint8_t)temp;
        }
    }

    if (ret != 0)
    {
        return -1;
    }

    return hinic3_nlattr_put_u8(nla, type, val);
}

static int handle_nl_put_u32(struct hinic3_nlattr *nla, enum hinic3_global_cfg_arg_type type, char *value)
{
    uint32_t val = 0;
    int ret;

    if (type == HINIC3_GLOBAL_CFG_ARG_VXLAN_LOCAL_IP)
    {
        ret = inet_pton(AF_INET, value, &val);
        /* inet_pton return 1 when success
         * return 0 when address is invaild
         * return -1 when str format error
         */
        if (ret <= 0)
        {
            return -1;
        }
    }
    else
    {
        ret = parse_str_to_value(value, &val);
        if (ret != 0)
        {
            return -1;
        }
    }

    hinic3_nlattr_put_u32(nla, type, val);
    return 0;
}

static int global_smap_to_nlattr(struct hinic3_nlattr *nla, enum hinic3_global_cfg_arg_type nl_type, char *value)
{
    switch (nl_type)
    {
    case HINIC3_GLOBAL_CFG_ARG_VF_INFO:
    case HINIC3_GLOBAL_CFG_ARG_RSS_VF_NUM:
    case HINIC3_GLOBAL_CFG_ARG_VF_ENABLE:
    case HINIC3_GLOBAL_CFG_ARG_PCAP_PROBE:
    case HINIC3_GLOBAL_CFG_ARG_ETP:
    case HINIC3_GLOBAL_CFG_ARG_QUEUE_POOL_SIZE:
        handle_nl_put_unspec(nla, nl_type, value);
        break;
    case HINIC3_GLOBAL_CFG_ARG_VLAN_ETHTYPE:
    case HINIC3_GLOBAL_CFG_ARG_INVALID_TCP_ACTION:
    case HINIC3_GLOBAL_CFG_ARG_IP_FRAG_ACTION:
    case HINIC3_GLOBAL_CFG_ARG_THREAD_MODE:
    case HINIC3_GLOBAL_CFG_ARG_PMD_MODE:
    case HINIC3_GLOBAL_CFG_ARG_ACTION_CHECKING:
    case HINIC3_GLOBAL_CFG_UNAGE_FLAG:
    case HINIC3_GLOBAL_CFG_ARG_LATENCY_MOD:
        return handle_nl_put_u8(nla, nl_type, value);
    case HINIC3_GLOBAL_CFG_ARG_VXLAN_LOCAL_IP:
    case HINIC3_GLOBAL_CFG_ARG_PCI_VENDOR_ID:
    case HINIC3_GLOBAL_CFG_ARG_PCI_DEVICE_ID:
    case HINIC3_GLOBAL_CFG_ARG_PCI_SUB_VENDOR_ID:
    case HINIC3_GLOBAL_CFG_ARG_PCI_SUB_DEVICE_ID:
    case HINIC3_GLOBAL_CFG_ARG_FLOW_AGE_TIME:
        return handle_nl_put_u32(nla, nl_type, value);
    default:
        return 0;
    }

    return 0;
}

static int global_cfg_smap_to_nlattr(const struct smap *args, struct hinic3_nlattr *args_nla)
{
    int ret;
    struct smap_node *node = NULL;
    enum hinic3_global_cfg_arg_type nl_type;

    HINIC3_SMAP_FOR_EACH(node, args)
    {
        nl_type = get_nl_type_by_smap_key(node->key);
        if (nl_type == HINIC3_GLOBAL_CFG_ARG_TYPE_MAX)
        {
            continue;
        }
        ret = global_smap_to_nlattr(args_nla, nl_type, node->value);
        if (ret != 0)
        {
            return ret;
        }
    }

    return 0;
}

static int nla_to_smap_u8(struct smap *args, const hinic3_nlattr_itr nla, const char *key)
{
    uint8_t val = hinic3_nlattr_get_itr_u8(nla);
    int type = hinic3_nlattr_get_itr_type(nla);
    const char *smap_value = NULL;

    if (type == HINIC3_GLOBAL_CFG_ARG_VLAN_ETHTYPE)
    {
        if (val == GLOBAL_VLAN_ETHTYPE_8021Q)
        {
            smap_value = GLOBAL_VLAN_ETHTYPE_8021Q_STR;
        }
    }
    else if (type == HINIC3_GLOBAL_CFG_ARG_INVALID_TCP_ACTION)
    {
        smap_value = global_cfg_action_stringfy(val);
    }
    else if (type == HINIC3_GLOBAL_CFG_ARG_IP_FRAG_ACTION)
    {
        smap_value = global_cfg_action_stringfy(val);
    }
    else if (type == HINIC3_GLOBAL_CFG_ARG_THREAD_MODE)
    {
        smap_value = global_cfg_thread_mode_stringfy(val);
    }
    else if (type == HINIC3_GLOBAL_CFG_ARG_ACTION_CHECKING)
    {
        if (val == GLOBAL_ACTION_CHECK_ON)
        {
            smap_value = HINIC3_GLOBAL_CFG_UPCALL_PUSH_VXLAN_ON_STR;
        }
        else if (val == GLOBAL_ACTION_CHECK_OFF)
        {
            smap_value = HINIC3_GLOBAL_CFG_UPCALL_PUSH_VXLAN_OFF_STR;
        }
    }
    else if (type == HINIC3_GLOBAL_CFG_UNAGE_FLAG)
    {
        smap_value = global_cfg_unage_flag_stringfy(val);
    }
    else if (type == HINIC3_GLOBAL_CFG_ARG_LATENCY_MOD)
    {
        if (val == 0 || val == HINIC3_HIGH_BANDWIDTH_MODE_ADAPTE)
        {
            smap_value = HINIC3_GLOBAL_CFG_PKT_HIGH_THROUGH_STR;
        }
        else if (val == 1)
        {
            smap_value = HINIC3_GLOBAL_CFG_PKT_LOW_LATENCY_MOD_STR;
        }
    }

    if (smap_value == NULL)
    {
        return -1;
    }

    hinic3_smap_add(args, key, smap_value, HINIC3_DRIVER_ADAPTER);
    return 0;
}

static int nla_to_smap_u16(struct smap *args, const hinic3_nlattr_itr nla, const char *key)
{
    char *value = NULL;
    char value_buf[MAX_ARGS_SIZE] = {0};

    snprintf(value_buf, sizeof(value_buf) - 1, "%hu", hinic3_nlattr_get_itr_u16(nla));
    value = value_buf;
    hinic3_smap_add(args, key, value, HINIC3_DRIVER_ADAPTER);
    return 0;
}

static int nla_to_smap_u32(struct smap *args, const hinic3_nlattr_itr nla, const char *key)
{
    char *value = NULL;
    char value_buf[MAX_ARGS_SIZE] = {0};
    uint32_t val = hinic3_nlattr_get_itr_u32(nla);
    int type = hinic3_nlattr_get_itr_type(nla);

    if (type == HINIC3_GLOBAL_CFG_ARG_VXLAN_LOCAL_IP)
    {
        inet_ntop(AF_INET, &val, value_buf, INET_ADDRSTRLEN);
    }
    else
    {
        snprintf(value_buf, sizeof(value_buf) - 1, "%x", val);
    }
    value = value_buf;
    hinic3_smap_add(args, key, value, HINIC3_DRIVER_ADAPTER);
    return 0;
}

static int nla_to_smap_raw(struct smap *args, const hinic3_nlattr_itr nla, const char *key)
{
    char *value = NULL;
    char value_buf[MAX_ARGS_SIZE] = {0};
    int type = hinic3_nlattr_get_itr_type(nla);
    const int *int_p = NULL;
    enum SMAP_PTR_INDEX
    {
        HEAD_PTR,
        MID_PTR,
        TAIL_PTR
    };

    if (type == HINIC3_GLOBAL_CFG_ARG_PTHREAD_FDS)
    {
        int_p = hinic3_nlattr_get_itr_data(nla);
        snprintf(value_buf, sizeof(value_buf) - 1, "%d,%d,%d", int_p[0], int_p[MID_PTR], int_p[TAIL_PTR]);
    }

    value = value_buf;
    hinic3_smap_add(args, key, value, HINIC3_DRIVER_ADAPTER);
    return 0;
}

static const char *global_smap_key_from_nla_type(uint32_t type)
{
    if (type >= HINIC3_GLOBAL_CFG_ARG_TYPE_MAX)
    {
        return NULL;
    }

    return g_global_smap_key[type];
}

static int global_nla_to_smap(const hinic3_nlattr_itr nla_itr, struct smap *args, const char *key)
{
    const char *vf_info = NULL;

    switch (hinic3_nlattr_get_itr_type(nla_itr))
    {
    case HINIC3_GLOBAL_CFG_ARG_VLAN_ETHTYPE:
    case HINIC3_GLOBAL_CFG_ARG_INVALID_TCP_ACTION:
    case HINIC3_GLOBAL_CFG_ARG_IP_FRAG_ACTION:
    case HINIC3_GLOBAL_CFG_ARG_THREAD_MODE:
    case HINIC3_GLOBAL_CFG_ARG_ACTION_CHECKING:
    case HINIC3_GLOBAL_CFG_UNAGE_FLAG:
    case HINIC3_GLOBAL_CFG_ARG_LATENCY_MOD:
        return nla_to_smap_u8(args, nla_itr, key);
    case HINIC3_GLOBAL_CFG_ARG_RSS_VF_NUM:
    case HINIC3_GLOBAL_CFG_ARG_QUEUE_POOL_SIZE:
        return nla_to_smap_u16(args, nla_itr, key);
    case HINIC3_GLOBAL_CFG_ARG_VXLAN_LOCAL_IP:
    case HINIC3_GLOBAL_CFG_ARG_PCI_VENDOR_ID:
    case HINIC3_GLOBAL_CFG_ARG_PCI_DEVICE_ID:
    case HINIC3_GLOBAL_CFG_ARG_PCI_SUB_VENDOR_ID:
    case HINIC3_GLOBAL_CFG_ARG_PCI_SUB_DEVICE_ID:
    case HINIC3_GLOBAL_CFG_ARG_FLOW_AGE_TIME:
        return nla_to_smap_u32(args, nla_itr, key);
    case HINIC3_GLOBAL_CFG_ARG_PTHREAD_FDS:
    case HINIC3_GLOBAL_GET_PCAP_PROBE_STATS:
    case HINIC3_GLOBAL_CFG_ARG_PCAP_PROBE:
    case HINIC3_GLOBAL_GET_ETP_STATS:
    case HINIC3_GLOBAL_CFG_ARG_ETP:
        return nla_to_smap_raw(args, nla_itr, key);
    case HINIC3_GLOBAL_CFG_ARG_VF_INFO:
        vf_info = hinic3_nlattr_get_itr_string(nla_itr);
        hinic3_smap_add(args, HINIC3_GLOBAL_CFG_ARG_VF_INFO_STR, vf_info,
                        HINIC3_DRIVER_ADAPTER);
        break;
    case HINIC3_GLOBAL_CFG_ARG_PHY_DEV_INFO:
    default:
        break;
    }

    return 0;
}

static int global_cfg_nlattr_to_smap(const struct hinic3_nlattr *args_nla, struct smap *args)
{
    hinic3_nlattr_itr nla_itr = NULL;
    const char *key = NULL;

    HINIC3_NLATTR_FOR_EACH(nla_itr, args_nla)
    {
        if (hinic3_nlattr_get_itr_size(nla_itr) == 0)
        {
            continue;
        }

        key = global_smap_key_from_nla_type(hinic3_nlattr_get_itr_type(nla_itr));
        if (key == NULL)
        {
            continue;
        }

        if (global_nla_to_smap(nla_itr, args, key) != 0)
        {
            return -EINVAL;
        }
    }

    return 0;
}

hinic3_global_api hinic3_global_get_api_index(const char *api_name)
{
    int i;
    for (i = 0; i < HINIC3_GLOBAL_API_MAX; ++i)
    {
        if (strncmp(api_name, g_global_api_arr[i].api_name, strlen(g_global_api_arr[i].api_name) + 1) == 0)
        {
            return g_global_api_arr[i].api_index;
        }
    }
    return HINIC3_GLOBAL_API_MAX;
}

static void hinic3_lib_arg_init(struct rte_mempool *mempool, uint32_t data_room_size, struct hiovs_lib_arg *arg)
{
    arg->mempool = mempool;
    arg->mode = HIOVS_DPDK_MODE;
    arg->role = 1;
    arg->state = 1;
    arg->work_mode = hinic3_hiovs_mode_get();
    arg->flow_num = hinic3_max_flow_num_get();
    arg->dp_hash_flow_num = hinic3_dp_hash_flow_get() ? DP_HASH_FLOW_MAX_NUM : 0;
    arg->rx_thread_num = hinic3_offload_thread_num_get();
    arg->data_room_size = data_room_size;
    arg->cb_ops.hovs_malloc = hinic3_adapt_malloc;
    arg->cb_ops.hovs_mfree = hinic3_adapt_mfree;
    arg->cb_ops.hovs_mbuf_alloc = hinic3_adapt_mbuf_alloc;
    arg->cb_ops.hovs_mbuf_alloc_bulk = hinic3_adapt_mbuf_alloc_bulk;
    arg->cb_ops.hovs_mbuf_free = hinic3_adapt_mbuf_free;
    arg->ext_cb_ops.hovs_malloc_socket = hinic3_adapt_malloc_socket;
    arg->ext_cb_ops.hovs_free = hinic3_adapt_free;
    arg->ext_cb_ops.hovs_memzone_reserve = hinic3_adapt_memzone_reserve;
    arg->ext_cb_ops.hovs_memzone_reserve_aligned = hinic3_adapt_memzone_reserve_aligned;
    arg->ext_cb_ops.hovs_memzone_free = hinic3_adapt_memzone_free;
    arg->ext_cb_ops.hovs_memzone_lookup = rte_memzone_lookup;
    arg->bond_rx_depth = hinic3_bond_rx_depth_get();
    arg->bond_tx_depth = hinic3_bond_tx_depth_get();
    arg->vport_rx_depth = hinic3_vport_rx_depth();
    arg->vport_tx_depth = hinic3_vport_tx_depth();
    arg->ext_cb_flag = 1;
    arg->ext_mp_flag = 1;
    arg->mega_flow_mode = 1;
    arg->clean_qos = 1;
}

static int hinic3_global_init(struct rte_mempool *mempool, uint32_t data_room_size)
{
    struct hiovs_lib_arg arg;
    struct hinic3_drv_ops *ops = NULL;
    uint64_t time_start;
    uint64_t exec_time = 0;
    int ret;

    memset(&arg, 0, sizeof(struct hiovs_lib_arg));

    /* hinic lib just only support DPDK MODE */
    memset(&arg, 0, sizeof(arg));
    hinic3_lib_arg_init(mempool, data_room_size, &arg);

    ops = hinic3_get_drv_ops();
    if (hinic3_card_mod_get() == PROG_MODE)
    {
        HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_flexda_lib_init, HINIC3_DRV_FUNC_NO_PTR);
        time_start = rte_get_tsc_cycles();
        ret = ops->hovs_flexda_lib_init((void *)(&arg));
    }
    else
    {
        HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_lib_init, HINIC3_DRV_FUNC_NO_PTR);
        time_start = rte_get_tsc_cycles();
        ret = ops->hovs_lib_init((void *)(&arg));
    }

    uint64_t hz = rte_get_tsc_hz();
    if (hz != 0)
    {
        exec_time = ((rte_get_tsc_cycles() - time_start) * 1000000UL) / hz;
    }
    if (ret != 0)
    {
        HINIC3_LOG(ERR, DRIVER, "hinic HWAPI %s called, return %d, cost %" PRIu64 " us!", __FUNCTION__, ret, exec_time);
    }
    else
    {
        g_global_args_flag |= HINIC3_FLAG_RX_THREAD_NUM;
        g_global_args_flag |= HINIC3_FLAG_MAX_FLOW_NUM;
        HINIC3_LOG(WARNING, DRIVER, "hinic HWAPI %s called, return %d, cost %" PRIu64 "us!", __FUNCTION__, ret, exec_time);
    }
    return ret;
}

void hinic3_global_unit(void)
{
    struct hiovs_lib_arg arg;
    struct hinic3_drv_ops *ops = NULL;
    uint64_t time_start;
    uint64_t exec_time = 0;
    uint32_t old_role_arg = 1;

    /* *
     * Wait for all other threads to know hinic3 lib uninit
     * This step must be done after hinic3_flow and hinic3_port uninit
     * no finished
     */
    ops = hinic3_get_drv_ops();

    if (hinic3_card_mod_get() == PROG_MODE)
    {
        if (ops->hovs_flexda_lib_deinit == NULL)
        {
            HINIC3_LOG(ERR, DRIVER, "There is no hovs_flexda_lib_deinit ptr!");
            return;
        }
    }
    else
    {
        if (ops->hovs_lib_deinit == NULL)
        {
            HINIC3_LOG(ERR, DRIVER, "There is no hovs_lib_deinit ptr!");
            return;
        }
    }

    memset(&arg, 0, sizeof(struct hiovs_lib_arg));
    /* just set role and state */
    arg.role = old_role_arg;
    arg.state = 1;
    time_start = rte_get_tsc_cycles();
    if (hinic3_card_mod_get() == PROG_MODE)
    {
        ops->hovs_flexda_lib_deinit((void *)(&arg));
    }
    else
    {
        ops->hovs_lib_deinit((void *)(&arg));
    }
    uint64_t hz = rte_get_tsc_hz();
    if (hz != 0)
    {
        exec_time = ((rte_get_tsc_cycles() - time_start) * 1000000UL) / hz;
    }
    HINIC3_LOG(WARNING, DRIVER, "hinic HWAPI hinic3_global_unit called, return void, cost %" PRIu64 " us!", exec_time);

    if (g_dpdk_shared_mp != NULL)
    {
        rte_mempool_free((struct rte_mempool *)(uintptr_t)g_dpdk_shared_mp);
        g_dpdk_shared_mp = NULL;
    }
}

int hinic3_global_cfg_set(const struct smap *args, struct smap *unset_args)
{
    int ret;
    void *buff = NULL;
    void *reply_buff = NULL;
    void *tmp_buff = NULL;
    struct hinic3_nlattr set_args_nla;
    struct hinic3_nlattr unset_args_nla;
    struct hinic_global_api_args api_args = {0};

    if (args == NULL || unset_args == NULL)
    {
        HINIC3_LOG(WARNING, DRIVER, "global cfg set, Pointer parameter is NULL!");
        return -1;
    }

    buff = (void *)hinic3_calloc(1, HOVS_MAX_TLV_BUF_LEN, HINIC3_DRIVER_ADAPTER);
    if (buff == NULL)
    {
        return -1;
    }

    reply_buff = (void *)hinic3_calloc(1, HOVS_MAX_TLV_BUF_LEN, HINIC3_DRIVER_ADAPTER);
    if (reply_buff == NULL)
    {
        hinic3_free(buff);
        return -1;
    }

    tmp_buff = buff;
    hinic3_nlattr_init(&set_args_nla, tmp_buff, HOVS_MAX_TLV_BUF_LEN);
    tmp_buff = reply_buff;
    hinic3_nlattr_init(&unset_args_nla, tmp_buff, HOVS_MAX_TLV_BUF_LEN);

    /* struct smap to hinic3_nlattr */
    ret = global_cfg_smap_to_nlattr(args, &set_args_nla);
    if (ret != 0)
    {
        HINIC3_LOG(ERR, DRIVER, "global cfg set, Call global_cfg_smap_to_nlattr error!");
        goto err;
    }

    if (set_args_nla.used_len == 0)
    {
        goto err;
    }

    api_args.in_nlattr_data = set_args_nla.data;
    api_args.in_nlattr_len = set_args_nla.used_len;
    api_args.out_nlattr_data = unset_args_nla.data;
    api_args.out_nlattr_len = &unset_args_nla.used_len;
    ret = hinic3_log_hinic_global_api_log(HINIC3_GLOBAL_CFG_SET, &api_args);
    if (ret != 0)
    {
        goto err;
    }
    hinic3_nlattr_reset_itr(&unset_args_nla, unset_args_nla.used_len);
    /* hinic3_nlattr to struct smap */
    ret = global_cfg_nlattr_to_smap(&unset_args_nla, unset_args);

err:
    hinic3_free(buff);
    hinic3_free(reply_buff);
    return ret;
}

int hinic3_global_cfg_get(struct smap *args)
{
    int ret;
    void *buff = NULL;
    void *tmp_buff = NULL;
    struct hinic3_nlattr args_nla;
    struct hinic_global_api_args api_args = {0};

    if (args == NULL)
    {
        HINIC3_LOG(WARNING, DRIVER, "Pointer parameter is NULL!");
        return -1;
    }

    buff = (void *)hinic3_calloc(1, HOVS_MAX_TLV_BUF_LEN, HINIC3_DRIVER_ADAPTER);
    if (buff == NULL)
    {
        HINIC3_LOG(ERR, DRIVER, "global cfg get, Alloc memory error!");
        return -1;
    }

    tmp_buff = buff;
    hinic3_nlattr_init(&args_nla, tmp_buff, HOVS_MAX_TLV_BUF_LEN);
    /* struct smap to hinic3_nlattr */
    ret = global_cfg_smap_to_nlattr(args, &args_nla);
    if (ret != 0)
    {
        HINIC3_LOG(ERR, DRIVER, "global cfg get, Call global_cfg_smap_to_nlattr error!");
        goto err;
    }

    api_args.out_nlattr_data = args_nla.data;
    api_args.out_nlattr_len = &args_nla.used_len;
    ret = hinic3_log_hinic_global_api_log(HINIC3_GLOBAL_CFG_GET, &api_args);
    if (ret != 0)
    {
        goto err;
    }

    hinic3_smap_clear(args);
    hinic3_nlattr_reset_itr(&args_nla, args_nla.used_len);
    /* hinic3_nlattr to struct smap */
    ret = global_cfg_nlattr_to_smap(&args_nla, args);

err:
    hinic3_free(buff);
    return ret;
}

int hinic3_global_statistics_get(struct hinic3_global_stats *stats)
{
    int ret;
    struct hovs_global_stats hovs_stats = {0};
    struct hinic_global_api_args api_args = {0};

    if (stats == NULL)
    {
        HINIC3_LOG(WARNING, DRIVER, "global statistics get, Pointer parameter is NULL!");
        return -1;
    }

    api_args.hovs_stats = &hovs_stats;
    ret = hinic3_log_hinic_global_api_log(HINIC3_GLOBAL_STATISTICS_GET, &api_args);
    if (ret != 0)
    {
        return ret;
    }
    stats->offload_flow_num = hovs_stats.offload_flow_num;
    stats->offload_qpc_num = hovs_stats.offload_qpc_num;
    stats->same_flow_with_multi_gpa = hovs_stats.same_flow_with_multi_gpa;
    stats->vxlan_rx_vtep_miss = hovs_stats.vxlan_rx_vtep_miss;

    stats->upcall_limit_drop_total = hovs_stats.upcall_limit_drop_total;
    stats->upcall_from_vf_total = hovs_stats.upcall_from_vf_total;
    stats->upcall_from_hwbond_total = hovs_stats.upcall_from_hwbond_total;
    stats->flow_default_upcall = hovs_stats.flow_default_upcall;
    stats->flow_miss_upcall = hovs_stats.flow_miss_upcall;
    stats->flow_invalid_upcall = hovs_stats.flow_invalid_upcall;
    stats->qu_prefetch_statics_fail = hovs_stats.qu_prefetch_statics_fail;

    return ret;
}

int hinic3_global_statistics_flush(void)
{
    int ret;

    ret = hinic3_log_hinic_global_api_log(HINIC3_GLOBAL_STATISTICS_FLUSH, NULL);

    return ret;
}

int hinic3_global_open_log(uint32_t module, uint32_t enable)
{
    int ret;
    struct hinic_global_api_args api_args = {0};

    api_args.log_module = module;
    api_args.log_level_or_enable = enable;
    ret = hinic3_log_hinic_global_api_log(HINIC3_GLOBAL_OPEN_LOG, &api_args);
    return ret;
}

int hinic3_global_set_log_level(uint32_t module, uint32_t level)
{
    int ret;
    struct hinic_global_api_args api_args = {0};

    api_args.log_module = module;
    api_args.log_level_or_enable = level;
    ret = hinic3_log_hinic_global_api_log(HINIC3_GLOBAL_SET_LOG_LEVEL, &api_args);
    return ret;
}

int hinic3_global_cmd_exec(const char *cmd_in, uint32_t in_size, char *cmd_out, uint32_t *out_len, uint32_t max_out_len)
{
    int ret;
    struct hinic_global_api_args api_args = {0};

    api_args.cmd_in = cmd_in;
    api_args.cmd_in_len = in_size;
    api_args.cmd_out = cmd_out;
    api_args.cmd_out_len = out_len;
    api_args.cmd_out_buf_size = max_out_len;
    ret = hinic3_log_hinic_global_api_log(HINIC3_GLOBAL_CMD_EXEC, &api_args);
    return ret;
}

int hinic3_global_pcie_list_query(uint8_t front_back, uint8_t bdf_type, struct hovs_phy_dev_info *dev)
{
    int ret;
    struct hinic_global_api_args api_args = {0};

    api_args.bdf_type = bdf_type;
    api_args.front_back = front_back;
    api_args.dev = dev;
    ret = hinic3_log_hinic_global_api_log(HINIC3_GLOBAL_PCIE_LIST_QUERY, &api_args);
    return ret;
}

void hinic3_eth_dev_tx_lock_init(void)
{
    for (uint8_t i = 0; i < MAX_TX_QUEUE_PER_VPORT; i++)
    {
        rte_spinlock_init(&g_hinic3_tx_burst_lock[i]);
    }
}

uint16_t hinic3_global_rte_eth_tx_burst(uint16_t port_id, uint16_t queue_id, struct rte_mbuf **tx_pkts, uint16_t nb_pkts)
{
    uint16_t ret;
    struct hinic3_drv_ops *ops = NULL;
    ops = hinic3_get_drv_ops();
    if (HINIC3_UNLIKELY(ops->hovs_rte_tx_burst == NULL))
    {
        hinic3_add_error_stats(HINIC3_PORTS_ERROR_HOVS_RTE_TX_BURST_NULL, 1);
        return 0;
    }

    if (HINIC3_UNLIKELY(queue_id >= MAX_TX_QUEUE_PER_VPORT))
    {
        hinic3_add_error_stats(HINIC3_PORTS_ERROR_HOVS_RTE_TX_BURST_QUEUE_ID_OUT_OF_RANGE, 1);
        return 0;
    }

    rte_spinlock_lock(&g_hinic3_tx_burst_lock[queue_id]);
    ret = ops->hovs_rte_tx_burst(port_id, queue_id, (void **)tx_pkts, nb_pkts);
    rte_spinlock_unlock(&g_hinic3_tx_burst_lock[queue_id]);

    return ret;
}

uint16_t hinic3_global_rte_eth_rx_burst(uint16_t port_id, uint16_t queue_id, struct rte_mbuf **rx_pkts, uint16_t nb_pkts)
{
    struct hinic3_drv_ops *ops = NULL;
    ops = hinic3_get_drv_ops();
    if (hinic3_card_mod_get() == PROG_MODE)
    {
        if (HINIC3_UNLIKELY(ops->hovs_flexda_rte_rx_burst == NULL))
        {
            hinic3_add_error_stats(HINIC3_PORTS_ERROR_HOVS_RTE_RX_BURST_NULL, 1);
            return 0;
        }
        return ops->hovs_flexda_rte_rx_burst(port_id, queue_id, (void **)rx_pkts, nb_pkts);
    }
    else
    {
        if (HINIC3_UNLIKELY(ops->hovs_rte_rx_burst == NULL))
        {
            hinic3_add_error_stats(HINIC3_PORTS_ERROR_HOVS_RTE_RX_BURST_NULL, 1);
            return 0;
        }
        return ops->hovs_rte_rx_burst(port_id, queue_id, (void **)rx_pkts, nb_pkts);
    }
}

int hinic3_global_set_vxlan_vtep(const struct hinic3_vtep_ip_set_args *ops)
{
    int ret = -1;
    void *buff = NULL;
    void *reply_buff = NULL;
    void *tmp_buff = NULL;
    const uint32_t nla_buf_size = 32;
    struct hinic3_nlattr args_nla;
    struct hinic3_nlattr unset_args_nla;
    struct hinic3_vtep_ip_set_args *ops_buf = NULL;
    struct hinic_global_api_args api_args = {0};

    buff = (void *)hinic3_calloc(1, nla_buf_size, HINIC3_DRIVER_ADAPTER);
    if (buff == NULL)
    {
        HINIC3_LOG(ERR, DRIVER, "Alloc memory error!");
        return -1;
    }

    reply_buff = (void *)hinic3_calloc(1, nla_buf_size, HINIC3_DRIVER_ADAPTER);
    if (reply_buff == NULL)
    {
        hinic3_free(buff);
        HINIC3_LOG(ERR, DRIVER, "Alloc memory error!");
        return -1;
    }

    tmp_buff = buff;
    hinic3_nlattr_init(&args_nla, tmp_buff, nla_buf_size);
    tmp_buff = reply_buff;
    hinic3_nlattr_init(&unset_args_nla, tmp_buff, nla_buf_size);
    ops_buf = hinic3_nlattr_put_unspec_uninit(&args_nla, HINIC3_GLOBAL_CFG_ARG_VXLAN_LOCAL_IP,
                                              sizeof(struct hinic3_vtep_ip_set_args));
    if (ops_buf == NULL)
    {
        goto clear_exit;
    }

    memcpy(ops_buf, ops, sizeof(struct hinic3_vtep_ip_set_args));

    api_args.in_nlattr_data = args_nla.data;
    api_args.in_nlattr_len = args_nla.used_len;
    api_args.out_nlattr_data = unset_args_nla.data;
    api_args.out_nlattr_len = &unset_args_nla.used_len;
    ret = hinic3_log_hinic_global_api_log(HINIC3_GLOBAL_CFG_SET, &api_args);
clear_exit:
    hinic3_free(buff);
    hinic3_free(reply_buff);
    return ret;
}

static void hinic3_init_flow_unage_flag(struct smap *args)
{
    hinic3_smap_add(args, HINIC3_GLOBAL_CFG_UNAGE_FLAG_STR, HINIC3_FLOW_UNAGE_FLAG, HINIC3_DRIVER_ADAPTER);
    HINIC3_LOG(INFO, DRIVER, "Set the hardware flow not to be aged.");
}

static void hinic3_init_flow_age_time(struct smap *args)
{
    int ret;
    char age_time_str[HINIC3_FLOW_AGE_TIME_MAX_LEN] = {0};
    uint32_t age_time = hinic3_flow_max_idle_get();

    ret = snprintf(age_time_str, HINIC3_FLOW_AGE_TIME_MAX_LEN, "%u", age_time);
    if (ret < 0 || ret >= HINIC3_FLOW_AGE_TIME_MAX_LEN)
    {
        HINIC3_LOG(WARNING, DRIVER, "Function sprintf_s fail when init flow age time, age time is %u, ret is %d!", age_time, ret);
        hinic3_smap_add(args, HINIC3_GLOBAL_CFG_ARG_FLOW_AGE_TIME_STR, HINIC3_FLOW_AGE_TIME_DEFAULT, HINIC3_DRIVER_ADAPTER);
        HINIC3_LOG(INFO, DRIVER, "Set the aging time of the hardware flow table to default %s ms.", HINIC3_FLOW_AGE_TIME_DEFAULT);
    }
    else
    {
        hinic3_smap_add(args, HINIC3_GLOBAL_CFG_ARG_FLOW_AGE_TIME_STR, age_time_str, HINIC3_DRIVER_ADAPTER);
        HINIC3_LOG(INFO, DRIVER, "Set the aging time of the hardware flow table to %s ms.", age_time_str);
    }
}

void hinic3_global_cfg_set_when_restart(void)
{
    int ret;
    struct smap args;
    struct smap unset_args;

    hinic3_smap_init(&args);
    hinic3_smap_init(&unset_args);

    if (hinic3_check_hardware_flow_age_switch() == true)
    {
        (void)hinic3_init_flow_age_time(&args);
    }
    else
    {
        (void)hinic3_init_flow_unage_flag(&args);
    }

    ret = hinic3_global_cfg_set(&args, &unset_args);
    if (ret != 0)
    {
        HINIC3_LOG(ERR, DRIVER, "hinic3_global_cfg_set error, %s!", __FUNCTION__);
    }

    hinic3_smap_destroy(&args);
    hinic3_smap_destroy(&unset_args);

    hinic3_flow_flush();
}

struct rte_mempool *dpdk_shared_mp_get(void)
{
    return (struct rte_mempool *)(uintptr_t)g_dpdk_shared_mp;
}

static const char *hinic3_global_get_api_name(hinic3_global_api api_index)
{
    int i;
    for (i = 0; i < HINIC3_GLOBAL_API_MAX; ++i)
    {
        if (api_index == g_global_api_arr[i].api_index)
        {
            return g_global_api_arr[i].api_name;
        }
    }
    return NULL;
}

static void hinic3_global_format_api(const char *api_name, hiovs_api_record *rec, struct ds *ds, bool is_all)
{
    char time_str[HINIC3_TIME_STR_LEN] = {0};
    char *pstr = time_str;
    int title_ui_space;
    int value_ui_space;

    if (is_all)
    {
        title_ui_space = SHOW_GLOBAL_API_UI_FOUR_SPACE;
        value_ui_space = SHOW_GLOBAL_API_UI_SIX_SPACE;
    }
    else
    {
        title_ui_space = SHOW_GLOBAL_API_UI_TWO_SPACE;
        value_ui_space = SHOW_GLOBAL_API_UI_FOUR_SPACE;
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
                             value_ui_space, HINIC3_UI_INDENT_SPACE, rec->last_rtn_value,
                             pstr);
    }
}

static int hinic3_global_show_one_api(struct unixctl_conn *conn, int argc, const char *argv[])
{
    int ret;
    const char *api_name = NULL;
    hiovs_api_record rec;
    struct ds ds = DS_EMPTY_INITIALIZER;

    if (argc < SHOW_ONE_API_ARG_NUM)
    {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR "Incomplete command, type -h or --help for help.\n");
        goto err;
    }
    else if (argc > SHOW_ONE_API_ARG_NUM)
    {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR "Too many parameters, type -h or --help for help.\n");
        goto err;
    }
    api_name = argv[1];
    if (hinic3_global_get_api_index(api_name) == HINIC3_GLOBAL_API_MAX)
    {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR "Wrong parameter, api-name is invalid [%s].\n", api_name);
        goto err;
    }
    ret = hinic3_global_get_api_record(api_name, false, false, &rec, 1);
    if (ret != 0)
    {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR "Wrong parameter, api-name is invalid [%s].\n", api_name);
        goto err;
    }

    hinic3_global_format_api(api_name, &rec, &ds, false);
    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
    return 0;

err:
    hinic3_command_reply_error(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
    return -1;
}

static int hinic3_global_show_all_api(struct unixctl_conn *conn, int argc)
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
    rec_list = (hiovs_api_record *)hinic3_calloc(HINIC3_GLOBAL_API_MAX, sizeof(hiovs_api_record), HINIC3_DRIVER_ADAPTER);
    if (rec_list == NULL)
    {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_FAILURE "calloc memory failed!\n");
        goto err;
    }

    ret = hinic3_global_get_api_record("", true, false, rec_list, HINIC3_GLOBAL_API_MAX);
    if (ret != 0)
    {
        hinic3_free(rec_list);
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR "internal error, please check log!\n");
        goto err;
    }

    hinic3_ds_put_format(&ds, "%2sAPI Counts: %d\n", HINIC3_UI_INDENT_SPACE, HINIC3_GLOBAL_API_MAX);
    for (i = 0; i < HINIC3_GLOBAL_API_MAX; i++)
    {
        api_name = hinic3_global_get_api_name(i);
        if (api_name == NULL)
        {
            hinic3_free(rec_list);
            goto err;
        }
        hinic3_global_format_api(api_name, &rec_list[i], &ds, true);
        if (i != HINIC3_GLOBAL_API_MAX - 1)
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

static int hinic3_global_clear_api(struct unixctl_conn *conn, int argc, const char *argv[])
{
    int ret;
    struct ds ds = DS_EMPTY_INITIALIZER;
    const char *api_name = NULL;
    hiovs_api_record rec;

    if (argc < CLEAR_GLOBAL_API_ARG_NUM)
    {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR "Incomplete command, type -h or --help for help.\n");
        goto err;
    }
    else if (argc > CLEAR_GLOBAL_API_ARG_NUM)
    {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR "Too many parameters, type -h or --help for help.\n");
        goto err;
    }
    if (strcmp("all", argv[CLEAR_GLOBAL_API_ARG_NUM - 1]) == 0)
    {
        ret = hinic3_global_get_api_record("", true, true, NULL, 0);
        if (ret != 0)
        {
            HINIC3_LOG(ERR, DRIVER, "Clear all global api record failed!");
            hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_FAILURE "Clear global API records failed [all]!\n");
            goto err;
        }
        HINIC3_LOG(INFO, DRIVER, "Clear all global api record successful.");
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_INFO "Clear global API records successful [all].\n");
    }
    else
    {
        api_name = argv[CLEAR_GLOBAL_API_ARG_NUM - 1];
        if (hinic3_global_get_api_index(api_name) == HINIC3_GLOBAL_API_MAX)
        {
            hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR "Wrong parameter, api-name is invalid [%s].\n", api_name);
            goto err;
        }
        ret = hinic3_global_get_api_record(api_name, false, true, &rec, 1);
        if (ret != 0)
        {
            HINIC3_LOG(ERR, DRIVER, "Clear global api record failed!");
            hinic3_ds_put_format(&ds, "%sClear global API record failed [%s]!\n", HINIC3_UI_LEADING_SIGN_ERROR, api_name);
            goto err;
        }
        HINIC3_LOG(INFO, DRIVER, "Clear global api record successful.");
        hinic3_ds_put_format(&ds, "%sClear global API record successful [%s].\n", HINIC3_UI_LEADING_SIGN_INFO, api_name);
    }

    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
    return 0;
err:
    hinic3_command_reply_error(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
    return -1;
}

static void hinic3_global_api_show_cmd_help(struct unixctl_conn *conn, int argc)
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
    hinic3_ds_put_format(&ds, "%2sUsage: dpak-ovs-ctl hwoff/show-global-api  { all | clear { all | <api-name> } | "
                              "api <api-name> | { -h | --help } }\n",
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
    for (i = 0; i < HINIC3_GLOBAL_API_MAX; i++)
    {
        api_name = hinic3_global_get_api_name(i);
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

static void unixctl_hinic3_show_global_api(struct unixctl_conn *conn, int argc, const char *argv[], void *aux)
{
    int work_agrc = argc;
    const char **work_argv = argv;

    work_agrc -= 1;
    work_argv += 1;
    if (strcmp("api", work_argv[0]) == 0)
    {
        *(int *)aux = hinic3_global_show_one_api(conn, work_agrc, work_argv);
        return;
    }

    if ((strcmp("all", work_argv[0]) == 0) && work_agrc == 1)
    {
        *(int *)aux = hinic3_global_show_all_api(conn, work_agrc);
        return;
    }

    if ((strcmp("clear", work_argv[0]) == 0) && work_agrc == CLEAR_GLOBAL_API_ARG_NUM)
    {
        *(int *)aux = hinic3_global_clear_api(conn, work_agrc, work_argv);
        return;
    }

    if (((strcmp("-h", work_argv[0]) == 0) || (strcmp("--help", work_argv[0]) == 0)) && work_agrc == 1)
    {
        *(int *)aux = 0;
        return hinic3_global_api_show_cmd_help(conn, work_agrc);
    }

    hinic3_command_reply_error(conn,
                               HINIC3_UI_LEADING_SIGN_ERROR "Unrecognized command, type -h or --help for help.\n");
    *(int *)aux = -1;
    return;
}

/*  this function must be call when thread start */
int hinic3_global_class_init(void)
{
    int ret;
    hiovs_api_record *global_api_record = NULL;

    global_api_record = hinic3_get_global_api_record();
    if (global_api_record == NULL)
    {
        return -1;
    }

    memset(global_api_record, 0, sizeof(hiovs_api_record) * HINIC3_GLOBAL_API_MAX);
    pthread_mutex_lock(&g_hinic3_global_mutex);
    ret = hinic3_global_init(NULL, hinic3_mbuf_size_get());
    if (ret != 0)
    {
        HINIC3_LOG(ERR, DRIVER, "hinic3_global_class_init: hinic3_global_init failed!");
        hinic3_global_unit();
        pthread_mutex_unlock(&g_hinic3_global_mutex);
        return ret;
    }

    pthread_mutex_unlock(&g_hinic3_global_mutex);
    hinic3_command_register("hwoff/show-global-api", "{ all | clear { all | <api-name> } |"
                                                     " api <api-name> | { -h | --help } }",
                            HINIC3_SHOW_GLOBAL_API_MIN_ARG, HINIC3_SHOW_GLOBAL_API_MAX_ARG, unixctl_hinic3_show_global_api, NULL);
    return ret;
}

/*  this function must be call when thread stop */
void hinic3_global_class_uninit(void)
{
    pthread_mutex_lock(&g_hinic3_global_mutex);
    hinic3_global_unit();
    pthread_mutex_unlock(&g_hinic3_global_mutex);

    hiovs_api_record *global_api_record = NULL;

    global_api_record = hinic3_get_global_api_record();
    if (global_api_record == NULL)
    {
        HINIC3_LOG(ERR, DRIVER, "global_api_record is NULL!");
        return;
    }

    memset(global_api_record, 0, sizeof(hiovs_api_record) * HINIC3_GLOBAL_API_MAX);
}

int hinic3_adapt_malloc(const char *type HINIC3_UNUSED, size_t size HINIC3_UNUSED, int socket_arg HINIC3_UNUSED,
                        unsigned int flags HINIC3_UNUSED, size_t align HINIC3_UNUSED, size_t bound HINIC3_UNUSED,
                        bool contig HINIC3_UNUSED, struct hovs_melem *mem HINIC3_UNUSED)
{
    return -1;
}

int hinic3_adapt_mfree(void *addr HINIC3_UNUSED)
{
    return -1;
}

void *hinic3_adapt_malloc_socket(const char *type HINIC3_UNUSED, size_t size, unsigned int align, int socket_arg)
{
    return hinic3_rte_malloc_socket(HIOVS_MEM, size, align, socket_arg);
}

void hinic3_adapt_free(void *addr)
{
    return hinic3_rte_free(addr);
}

const void *hinic3_adapt_memzone_reserve(const char *name, size_t len, int socket_id, unsigned flags)
{
    return hinic3_rte_memzone_reserve(name, len, socket_id, flags, HIOVS_MEM);
}

const void *hinic3_adapt_memzone_reserve_aligned(const char *name, size_t len, int socket_id, unsigned flags,
                                                 unsigned align)
{
    return hinic3_rte_memzone_reserve_aligned(name, len, socket_id, flags, align, HIOVS_MEM);
}

int hinic3_adapt_memzone_free(void *addr)
{
    return hinic3_rte_memzone_free((struct rte_memzone *)addr);
}

const struct rte_memzone *hinic3_memzone_lookup(const char *name)
{
    return rte_memzone_lookup(name);
}

struct hovs_mbuf *hinic3_adapt_mbuf_alloc(void *mp)
{
    struct rte_mbuf *mbuf = NULL;
    struct hovs_mbuf *trans_mbuf = NULL;

    mbuf = rte_pktmbuf_alloc((struct rte_mempool *)mp);
    if (mbuf == NULL)
    {
        return NULL;
    }

    trans_mbuf = (struct hovs_mbuf *)mbuf;
    return trans_mbuf;
}

int hinic3_adapt_mbuf_alloc_bulk(void *mp, struct hovs_mbuf **mbufs, uint32_t count)
{
    return rte_pktmbuf_alloc_bulk((struct rte_mempool *)mp, (struct rte_mbuf **)mbufs, count);
}

void hinic3_adapt_mbuf_free(struct hovs_mbuf *m)
{
    rte_pktmbuf_free((struct rte_mbuf *)m);
}

static int hinic3_forward_mode_set_sub(struct hinic3_nlattr *nla, enum hinic3_packet_forward_mod mod)
{
    if (nla == NULL)
    {
        HINIC3_LOG(ERR, DRIVER, "hinic3 forward mod set error, input null ptr!");
        return -1;
    }
    int ret = 0;

    if (mod == HINIC3_FORWARD_MODE_BANDWIDTH)
    {
        HINIC3_LOG(INFO, DRIVER, "hinic3 forward mod set, set bandwidth.");
        ret = hinic3_nlattr_put_u8(nla, HINIC3_GLOBAL_CFG_ARG_LATENCY_MOD, 0);
    }
    else if (mod == HINIC3_FORWARD_MODE_LATENCY)
    {
        HINIC3_LOG(INFO, DRIVER, "hinic3 forward mod set, set latency.");
        ret = hinic3_nlattr_put_u8(nla, HINIC3_GLOBAL_CFG_ARG_LATENCY_MOD, HINIC3_FORWARD_MODE_LATENCY);
    }
    else if (mod == HINIC3_FORWARD_MODE_30M)
    {
        HINIC3_LOG(INFO, DRIVER, "hinic3 forward mod set, 30M.");
        ret = hinic3_nlattr_put_u8(nla, HINIC3_GLOBAL_CFG_ARG_LATENCY_MOD, HINIC3_FORWARD_MODE_30M);
    }
    else
    {
        HINIC3_LOG(ERR, DRIVER, "hinic3 forward mod set error, input invalid mod %d!", mod);
        return -1;
    }

    return ret;
}

int hinic3_forward_mode_set(enum hinic3_packet_forward_mod mod)
{
    struct hinic3_nlattr args_in;
    struct hinic3_nlattr args_out;
    char buffer_out[HINIC3_FORWARD_MODE_BUFFER_LEN] = {0};
    char buffer_in[HINIC3_FORWARD_MODE_BUFFER_LEN] = {0};
    int ret = 0;
    struct hinic_global_api_args api_args = {0};

    hinic3_nlattr_init(&args_in, buffer_in, HINIC3_FORWARD_MODE_BUFFER_LEN);
    hinic3_nlattr_init(&args_out, buffer_out, HINIC3_FORWARD_MODE_BUFFER_LEN);

    ret = hinic3_forward_mode_set_sub(&args_in, mod);
    if (ret != 0)
    {
        HINIC3_LOG(ERR, DRIVER, "Failed to set forward mode!");
        return -1;
    }

    api_args.in_nlattr_data = args_in.data;
    api_args.in_nlattr_len = args_in.used_len;
    api_args.out_nlattr_data = args_out.data;
    api_args.out_nlattr_len = &args_out.used_len;

    ret = hinic3_log_hinic_global_api_log(HINIC3_GLOBAL_CFG_SET, &api_args);
    if (ret != 0)
    {
        HINIC3_LOG(ERR, DRIVER, "Failed to set forward mode, ret %d!", ret);
    }

    return ret;
}

static int hinic3_bond_hash_policy_set_sub(struct hinic3_nlattr *nla, enum hinic3_bond_hash_policy mod)
{
    if (nla == NULL)
    {
        HINIC3_LOG(ERR, DRIVER, "hinic3 bond hash policy set error, input null ptr!");
        return -1;
    }
    int ret = 0;

    if (mod == EFFICIENT_HASH)
    {
        HINIC3_LOG(INFO, DRIVER, "hinic3 bond hash policy set, set efficient hash.");
        ret = hinic3_nlattr_put_u8(nla, HINIC3_GLOBAL_CFG_ARG_BOND_HASH_POLICY_CFG, EFFICIENT_HASH);
    }
    else if (mod == STANDARD_HASH)
    {
        ret = hinic3_nlattr_put_u8(nla, HINIC3_GLOBAL_CFG_ARG_BOND_HASH_POLICY_CFG, STANDARD_HASH);
        HINIC3_LOG(INFO, DRIVER, "hinic3 bond hash policy set, set standard hash.");
    }
    else
    {
        HINIC3_LOG(ERR, DRIVER, "hinic3 bond hash policy set error, input invalid mod %d!", mod);
        return -1;
    }

    return ret;
}

static int hinic3_bond_hash_policy_set(enum hinic3_packet_forward_mod mod)
{
    struct hinic3_nlattr args_in;
    struct hinic3_nlattr args_out;
    char buffer_out[HINIC3_BOND_HASH_POLICY_LEN] = {0};
    char buffer_in[HINIC3_BOND_HASH_POLICY_LEN] = {0};
    int ret = 0;
    struct hinic_global_api_args api_args = {0};

    hinic3_nlattr_init(&args_in, buffer_in, HINIC3_BOND_HASH_POLICY_LEN);
    hinic3_nlattr_init(&args_out, buffer_out, HINIC3_BOND_HASH_POLICY_LEN);

    ret = hinic3_bond_hash_policy_set_sub(&args_in, (enum hinic3_bond_hash_policy)mod);
    if (ret != 0)
    {
        HINIC3_LOG(ERR, DRIVER, "Failed to bond hash policy!");
        return -1;
    }

    api_args.in_nlattr_data = args_in.data;
    api_args.in_nlattr_len = args_in.used_len;
    api_args.out_nlattr_data = args_out.data;
    api_args.out_nlattr_len = &args_out.used_len;

    ret = hinic3_log_hinic_global_api_log(HINIC3_GLOBAL_CFG_SET, &api_args);
    if (ret != 0)
    {
        HINIC3_LOG(ERR, DRIVER, "Failed to set bond hash policy, ret %d!", ret);
    }

    return ret;
}

int hinic3_forward_mod_get(struct ds *ds)
{
    int ret = 0;
    struct smap cfg_map;
    hinic3_smap_init(&cfg_map);

    ret = hinic3_global_cfg_get(&cfg_map);
    if (ret != 0)
    {
        HINIC3_LOG(ERR, DRIVER, "Failed to get forward mode!");
        hinic3_smap_destroy(&cfg_map);
        return -1;
    }

    const char *res = hinic3_smap_get(&cfg_map, HINIC3_GLOBAL_CFG_PKT_FORWARD_MOD_STR);
    if (res == NULL)
    {
        HINIC3_LOG(ERR, DRIVER, "Failed to get forward configuration!");
        hinic3_smap_destroy(&cfg_map);
        return -1;
    }

    hinic3_ds_put_format(ds, "Info: Current forward mode: %s.\n", res);
    hinic3_smap_destroy(&cfg_map);
    return 0;
}

int hinic3_forward_mode_init(void)
{
    enum hinic3_packet_forward_mod mod = hinic3_packet_forward_mod_get();
    int ret = 0;

    if (mod == HINIC3_FORWARD_MODE_BANDWIDTH)
    {
        mod = HINIC3_FORWARD_MODE_30M;
    }

    ret = hinic3_forward_mode_set(mod);
    if (ret != 0)
    {
        HINIC3_LOG(ERR, DRIVER, "Failed to init forward mode!");
    }
    return ret;
}

int hinic3_bond_hash_policy_init(void)
{
    enum hinic3_bond_hash_policy mod = hinic3_packet_bond_hash_policy_get();
    int ret = 0;

    ret = hinic3_bond_hash_policy_set((enum hinic3_packet_forward_mod)mod);
    if (ret != 0)
    {
        HINIC3_LOG(ERR, DRIVER, "Failed to init bond hash policy!");
    }
    return ret;
}
