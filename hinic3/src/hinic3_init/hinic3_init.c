/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */

#include <stdlib.h>
#include <dlfcn.h>
#include <unistd.h>
#include <string.h>
#include "rte_pci.h"
#include "rte_bus.h"
#include "rte_bus_pci.h"
#include "hinic3_util.h"
#include "hinic3_map.h"

#include "hinic3_log.h"
#include "hinic3_tlv_key.h"
#include "hinic3_iface_global.h"
#include "hinic3_iface_port.h"
#include "hinic3_iface_flow.h"
#include "hinic3_message.h"
#include "hinic3_vf_mgmt.h"
#include "hinic3_agent.h"
#include "hinic3_smap.h"
#include "hinic3_parse_agent_config.h"
#include "hinic3_flexda_flow_public.h"

#define HINIC3_DRV_SHARD_LIBRARY_OVS3         "/usr/lib64/libhiovs3.so"
#define HINIC3_DRV_SHARD_LIBRARY_OPA        "/usr/lib64/libhiflexda.so"
#define HINIC3_PF_ADDR_NULL_STR          "XXXX:XX:XX.X"

#define HINIC3_FLEXDA_OVS_SHARD_LIBRARY "/usr/lib64/libhiflexda_ovs_adapter.so"

static hinic3_lib_dlsym_uninit_cb_t g_hinic3_lib_dlsym_uninit_cb = NULL;
static void *g_hinic3_drv_handler = NULL;
static struct hinic3_drv_ops g_hinic3_drv_ops = {0};
static void *g_hinic3_flexda_ovs_handler = NULL;
static hinic3_flexda_ovs_ops_t g_hinic3_flexda_ovs_ops = {0};

static struct hinic3_drv_ops_map g_hinic3_drv_ops_map[] = {
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_port_mgmt_add),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_port_mgmt_add_dynamic),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_port_mgmt_del),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_port_mgmt_get),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_port_mgmt_set),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_port_mgmt_get_capability),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_port_mgmt_get_upcall_info),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_port_mgmt_setup_upcall_queue),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_port_mgmt_release_upcall_queue),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_bond_mgmt_create),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_bond_mgmt_delete),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_bond_mgmt_get),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_etheraddr_get),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_etheraddr_set),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_mtu_set),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_port_statistics_get),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_port_statistics_flush),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_bum_get),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_bum_set),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_bum_remove),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_bum_clear),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_flow_mgmt_get_forward_mode),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_flow_mgmt_set_forward_mode),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_flow_mgmt_put),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_flow_mgmt_get_by_key),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_flow_mgmt_get_by_ufid),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_flow_mgmt_del_by_key),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_flow_mgmt_del_by_ufid),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_flow_mgmt_del_by_batch),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_flow_mgmt_flush),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_flow_mgmt_dump_start),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_flow_mgmt_dump_next),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_flow_mgmt_dump_done),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_flow_mgmt_get_maxflows),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_flow_mgmt_get_capability),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_statistics_flow_get_by_ufid),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_statistics_flow_flush_by_ufid),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_flow_callback_register),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_global_cfg_set),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_global_cfg_get),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_global_statistics_get),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_global_statistics_flush),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_mml_lib),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_lib_init),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_lib_deinit),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_open_log),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_set_log_level),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_qos_statistics_get_batch),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_qos_statistics_get_all_batch),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_qos_statistics_clear_batch),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_hqos_statistics_get_all_batch),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_hqos_statistics_clear_batch),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_qos_vm_limit_set),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_qos_vm_limit_get),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_qos_vport_limit_set),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_qos_vport_limit_get),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_rte_tx_burst),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_rte_rx_burst),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_global_pcie_list_query),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_qos_vm_srtcm_limit_set),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_qos_vm_srtcm_limit_get),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_qos_net_limit_set),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_qos_net_limit_get),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_port_mgmt_set_upcall_priority),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_qos_flow_limit_set),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_qos_flow_limit_get),

    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_bond_slave_statistics_get),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_port_mgmt_set_usage_state),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_port_mgmt_get_usage_state),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_upcall_mtu_set),

    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_flow_mgmt_get_block_table_size),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_flow_mgmt_update),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_flow_mgmt_set_block_version),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_flow_mgmt_get_block_version),
};

static struct hinic3_drv_ops_map g_hinic3_drv_standard_ops_map[] = {
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_global_device_feature_get),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_rte_get_txq_backlog),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_rte_get_rxq_backlog),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_hotplug_add),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_hotplug_del),
};

static struct hinic3_drv_ops_map g_hinic3_drv_mega_ops_map[] = {
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_mega_flow_mgmt_put),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_mega_flow_mgmt_del_by_ufid),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_mega_flow_mgmt_flush),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_mega_flow_mgmt_dump_start),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_mega_flow_mgmt_dump_next),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_mega_flow_mgmt_dump_done),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_statistics_mega_flow_get_by_ufid),
};

static struct hinic3_drv_ops_map g_hinic3_drv_mega_forward_ops_map[] = {
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_mega_flow_set_l3_forward),
};

static struct hinic3_drv_ops_map g_hinic3_drv_flexda_ops_map[] = {
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_flexda_flow_mgmt_put),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_flexda_flow_mgmt_get_by_ufid),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_flexda_flow_mgmt_del_by_ufid),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_flexda_flow_mgmt_del_by_batch),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_flexda_flow_mgmt_flush),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_flexda_flow_mgmt_dump_start),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_flexda_flow_mgmt_dump_next),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_flexda_flow_mgmt_dump_done),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_flexda_flow_mgmt_get_maxflows),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_flexda_statistics_flow_get_by_ufid),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_flexda_flow_callback_register),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_flexda_get_config_info),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_flexda_free_config_info),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_flexda_global_cfg_set),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_flexda_lib_init),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_flexda_lib_deinit),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_flexda_rte_rx_burst),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_flexda_mml_lib),
};

static struct hinic3_drv_ops_map g_hinic3_drv_acl_ops_map[] = {
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_acl_indir_counter_alloc),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_acl_indir_counter_get),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_acl_indir_counter_free),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_acl_indir_counter_reset),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_acl_mgmt_put),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_acl_mgmt_del),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_acl_get_stats),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_acl_mgmt_flush),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_acl_mgmt_dump_start),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_acl_mgmt_dump_next),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_acl_mgmt_dump_done),
};

static struct hinic3_drv_ops_map g_hinic3_drv_dphash_ops_map[] = {
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_dp_hash_mgmt_put),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_dp_hash_mgmt_del_by_key),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_dp_hash_mgmt_get_by_key),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_dp_hash_mgmt_dump_start),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_dp_hash_mgmt_dump_next),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_dp_hash_mgmt_dump_done),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_dp_hash_mgmt_flush),
};

static struct hinic3_drv_ops_map g_hinic3_flexda_ovs_ops_map[] = {
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_flexda_init_adapter),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_flexda_deinit_adapter),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_flexda_init_ctx),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_flexda_deinit_ctx),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_flexda_update_ctx),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_flexda_extract_hdr),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_flexda_construct_key),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_flexda_construct_action),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_flexda_construct_attr),
    HINIC3_DRV_ADD_FUNC(g_hinic3_drv_ops, hovs_flexda_check_tunnel),
};

struct hinic3_drv_ops* hinic3_get_drv_ops(void)
{
    return &g_hinic3_drv_ops;
}

hinic3_flexda_ovs_ops_t* hinic3_get_flexda_ovs_api(void)
{
    return &g_hinic3_flexda_ovs_ops;
}

static void hinic3_drv_ops_uninit(void)
{
    if (g_hinic3_flexda_ovs_handler != NULL) {
        if (g_hinic3_drv_ops.hovs_flexda_deinit_adapter) {
            g_hinic3_drv_ops.hovs_flexda_deinit_adapter();
        }
        memset(&g_hinic3_flexda_ovs_ops, 0, sizeof(hinic3_flexda_ovs_ops_t));
        dlclose(g_hinic3_flexda_ovs_handler);
    }

    if (g_hinic3_drv_handler != NULL) {
        memset(&g_hinic3_drv_ops, 0, sizeof(struct hinic3_drv_ops));
        dlclose(g_hinic3_drv_handler);
    }
}

static int hinic3_drv_ops_init_sub(void *handler, struct hinic3_drv_ops_map driver_map[], int size)
{
    for (int index = 0; index < size; index++) {
        if (*driver_map[index].func != NULL) {
            continue;
        }

        *driver_map[index].func = dlsym(handler, driver_map[index].name);
        if (*driver_map[index].func == NULL) {
            HINIC3_LOG(ERR, AGENT, "load func %s fail: %s", driver_map[index].name, dlerror());

            return -1;
        }
    }
    return 0;
}

static const char * select_hinic3_dev_shard_library(void) {
    if (hinic3_card_mod_get() == PROG_MODE) {
        return HINIC3_DRV_SHARD_LIBRARY_OPA;
    } else {
        return HINIC3_DRV_SHARD_LIBRARY_OVS3;
    }
}

int hinic3_drv_ops_init(void)
{
    int ret = 0;
    const char *hinic3_dev_shard_library = select_hinic3_dev_shard_library();
    void *handler = dlopen(hinic3_dev_shard_library, RTLD_NOW);
    if (handler == NULL) {
        HINIC3_LOG(ERR, AGENT, "%s load err %s!", hinic3_dev_shard_library, dlerror());
        return -1;
    }

    ret = hinic3_drv_ops_init_sub(handler, g_hinic3_drv_ops_map, ARRAY_SIZE(g_hinic3_drv_ops_map));
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "hinic3 drv ops init: common api load failed!");
        goto err;
    }

    if (hinic3_card_mod_get() == PROG_MODE) {
        ret = hinic3_drv_ops_init_sub(handler, g_hinic3_drv_flexda_ops_map, ARRAY_SIZE(g_hinic3_drv_flexda_ops_map));
        if (ret != 0) {
            HINIC3_LOG(ERR, AGENT, "hinic3 drv ops init: opa api load failed!");
            goto err;
        }  
    } else {
        ret = hinic3_drv_ops_init_sub(handler, g_hinic3_drv_standard_ops_map, ARRAY_SIZE(g_hinic3_drv_standard_ops_map));
        if (ret != 0) {
            HINIC3_LOG(ERR, AGENT, "hinic3 drv ops init: standard api load failed!");
            goto err;
        }  
    }

    if (hinic3_check_fuzzy_flow_switch()) {
        ret = hinic3_drv_ops_init_sub(handler, g_hinic3_drv_mega_ops_map, ARRAY_SIZE(g_hinic3_drv_mega_ops_map));
        if (ret != 0) {
            HINIC3_LOG(ERR, AGENT, "hinic3 drv ops init: mega flow api load failed!");
            goto err;
        }
        if (hinic3_check_fuzzy_flow_l3_forward_switch()) {
            ret = hinic3_drv_ops_init_sub(handler, g_hinic3_drv_mega_forward_ops_map, ARRAY_SIZE(g_hinic3_drv_mega_forward_ops_map));
            if (ret != 0) {
                HINIC3_LOG(ERR, AGENT, "hinic3 drv ops init: mega flow l3 forward api load failed!");
                goto err;
            }
        }
    }

    if (hinic3_acl_flow_get() == true) {
        ret = hinic3_drv_ops_init_sub(handler, g_hinic3_drv_acl_ops_map, ARRAY_SIZE(g_hinic3_drv_acl_ops_map));
        if (ret != 0) {
            HINIC3_LOG(ERR, AGENT, "hinic3 drv ops init: acl flow api load failed ret is %d", ret);
            goto err;
        }
    }

    if (hinic3_dp_hash_flow_get() == true) {
        ret = hinic3_drv_ops_init_sub(handler, g_hinic3_drv_dphash_ops_map, ARRAY_SIZE(g_hinic3_drv_dphash_ops_map));
        if (ret != 0) {
            HINIC3_LOG(ERR, AGENT, "hinic3 drv ops init: dp-hash flow api load failed ret is %d", ret);
            goto err;
        }
    }

    g_hinic3_drv_handler = handler;
    hinic3_lib_dlsym_uninit_cb_register(hinic3_drv_ops_uninit);
    return 0;
err:
    dlclose(handler);
    return -1;
}

int hinic3_flexda_ovs_ops_init(void)
{
    int ret = 0;
    void *handler = dlopen(HINIC3_FLEXDA_OVS_SHARD_LIBRARY, RTLD_NOW);
    if (handler == NULL) {
        HINIC3_LOG(WARNING, AGENT, "%s load err %s \n", HINIC3_FLEXDA_OVS_SHARD_LIBRARY, dlerror());
        return -1;
    }
    ret = hinic3_drv_ops_init_sub(handler, g_hinic3_flexda_ovs_ops_map, ARRAY_SIZE(g_hinic3_flexda_ovs_ops_map));
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "hwoff flexda ovs ops init: flexda ovs hook api load failed");
        goto err;
    }

    g_hinic3_flexda_ovs_handler = handler;
    return 0;
err:
    if (handler != NULL) {
        dlclose(handler);
        handler = NULL;
    }
    return -1;
}

int hinic3_ops_init(void)
{
    int ret;

    ret = hinic3_get_agent_value_from_config();
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "Agent_construct: get_agent_value_from_config failed, ret is %d.", ret);
        return -1;
    }

    ret = hinic3_drv_ops_init();
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "Agent_construct: driver ops sysmbols init failed, ret is %d.", ret);
        return -1;
    }

    return 0;
}

static int hinic3_flexda_flow_config_info_init(void)
{

    int ret;
    hovs_flexda_config_info_t *hovs_flexda_config = hinic3_calloc(1, sizeof(hovs_flexda_config_info_t), HIOVS_MEM);
    if (hovs_flexda_config == NULL) {
        HINIC3_LOG(ERR, AGENT, "hinic3_flexda_flow_config_info_init: hinic3_calloc error");
        return -1;
    }

    ret = hinic3_flexda_get_flow_cfg_info(hovs_flexda_config);
    if  (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "hinic3_flexda_flow_config_info_init: hinic3_flexda_get_flow_cfg_info error");
        goto err;
    }

    ret = hinic3_flexda_flow_parse_config_info(hovs_flexda_config);
    if  (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "hinic3_flexda_flow_config_info_init: hinic3_flexda_flow_parse_config_info error");
        goto err;
    }
    HINIC3_LOG(INFO, AGENT, "hinic3 opa flow config info init success");
    return ret;

err:
    hinic3_free(hovs_flexda_config);
    return -1;
}

/*  this function will be called  (ife & hinic) component load */
int hinic3_driver_class_init(void)
{
    int ret;

    ret = hinic3_global_class_init();
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "hinic3_resource_init: hinic3_global_class_init failed with %d", ret);
        return ret;
    }

    ret = hinic3_flow_class_init();
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "hinic3_resource_init: hinic3_flow_class_init failed with %d", ret);
        goto rollback_global;
    }
    ret = hinic3_port_class_init();
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "hinic3_resource_init: hinic3_port_class_init failed with %d", ret);
        goto rollback_flow;
    }

    if (hinic3_card_mod_get() == PROG_MODE) {
        ret = hinic3_flexda_flow_config_info_init();
        if (ret != 0) {
            HINIC3_LOG(ERR, AGENT, "hinic3_resource_init: hinic3_flexda_flow_config_info_init failed with %d", ret);
            goto rollback_global;
        }
    }
    return 0;
rollback_flow:
    hinic3_flow_class_uninit();
rollback_global:
    hinic3_global_class_uninit();
    HINIC3_LOG(ERR, AGENT, "hinic3_driver_class_init failed");
    return ret;
}

/*  this function will be called when thread stop and (ife & hinic) component unload */
void hinic3_driver_class_uninit(void)
{
    hinic3_flow_class_uninit();
    hinic3_port_class_uninit();
    if (hinic3_card_mod_get() == PROG_MODE) {
       hinic3_flexda_free_flow_config();
    }
    /* hiovs lib deinit */
    hinic3_global_class_uninit();

    if (g_hinic3_lib_dlsym_uninit_cb != NULL) {
        g_hinic3_lib_dlsym_uninit_cb();
        hinic3_lib_dlsym_uninit_cb_unregister();
    }
}

void hinic3_lib_dlsym_uninit_cb_register(hinic3_lib_dlsym_uninit_cb_t cb)
{
    g_hinic3_lib_dlsym_uninit_cb = cb;
}

void hinic3_lib_dlsym_uninit_cb_unregister(void)
{
    g_hinic3_lib_dlsym_uninit_cb = NULL;
}

static int hinic3_pf_pci_length_check(const char *pf_pci_addr_str, struct rte_pci_addr *pf_pci_addr)
{
    int pf_pci_addr_str_len = strlen(pf_pci_addr_str);
    if ((pf_pci_addr_str_len != PCI_ID_DOMAIN_LENGTH) && (pf_pci_addr_str_len != PCI_ID_LENGTH)) {
        HINIC3_LOG(ERR, AGENT, "the PCI address length is incorrect.");
        return -1;
    }

    int ret = rte_pci_addr_parse(pf_pci_addr_str, pf_pci_addr);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "%s is not a valid pci address!", pf_pci_addr_str);
        return -1;
    }

    if ((pf_pci_addr->domain == 0) && (pf_pci_addr->bus == 0) && (pf_pci_addr->devid == 0) &&
        (pf_pci_addr->function == 0)) {
        HINIC3_LOG(ERR, AGENT, "incorrect PCI address, pci addr is %s", pf_pci_addr_str);
        return -1;
    }
    return 0;
}

int hinic3_pf_vdev_enable(void)
{
    int ret;
    struct rte_pci_addr pf_pci_addr = { 0 };
    struct smap config;
    struct smap unset_config;
    char pci_str_buf[BUFSIZE_SHORT] = { 0 };
    const char *pf_pci_addr_str = hinic3_pf_pci_addr_get();

    hinic3_smap_init(&config);
    hinic3_smap_init(&unset_config);
    if (pf_pci_addr_str[0] == '\0') {
        hinic3_smap_add(&config, HINIC3_GLOBAL_CFG_ARG_VF_ENABLE_STR, HINIC3_PF_ADDR_NULL_STR, HINIC3_INIT);
    } else {
        ret = hinic3_pf_pci_length_check(pf_pci_addr_str, &pf_pci_addr);
        if (ret != 0) {
            HINIC3_LOG(ERR, AGENT, "hinic3_pf_vdev_enable invalid pf %s", pf_pci_addr_str);
            goto out;
        }
        ret = pci_addr_format(&pf_pci_addr, pci_str_buf, sizeof(pci_str_buf));
        if (ret != 0) {
            HINIC3_LOG(ERR, AGENT, "pf pco addr parse failed.");
            goto out;
        }
        hinic3_smap_add(&config, HINIC3_GLOBAL_CFG_ARG_VF_ENABLE_STR, pci_str_buf, HINIC3_INIT);
    }

    ret = hinic3_global_cfg_set(&config, &unset_config);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "hinic3_pf_vdev_enable : fail to enable pf sriov");
        goto out;
    }
    ret = hinic3_hwpt_flavor_mgmt_init();
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "hinic3_hwpt_flavor_mgmt_init : fail to init vf lists");
        goto out;
    }

out:
    hinic3_smap_destroy(&config);
    hinic3_smap_destroy(&unset_config);
    return ret;
}

void hinic3_show_version(void)
{
    HINIC3_LOG(INFO, AGENT, "%s", HINIC3_AGENT_COMPONENT_NAME);
    HINIC3_LOG(INFO, AGENT, "%s", HINIC3_AGENT_FEATURE_NAME);
    HINIC3_LOG(INFO, AGENT, "%s%s", HINIC3_AGENT_COMPONENT_VERSION, HINIC3_BUILD_MAJOR_VERSION);
}
