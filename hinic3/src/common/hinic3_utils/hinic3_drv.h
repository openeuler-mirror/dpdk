/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2022 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_DRV_H__
#define HINIC3_DRV_H__

#include <stddef.h>
#include "hiovs_api.h"

#define HINIC3_DRV_FUNC_NO_PTR (-1)
#define HINIC3_DRV_ADD_FUNC(class, name) {#name, (void**)&(class).name}
#define HINIC3_FUNC_PTR_OR_ERR_RET(func, retval) do { \
    if ((func) == NULL) \
        return retval; \
} while (0)


struct hinic3_drv_ops_map {
    const char *name;
    void **func;
};

struct hinic3_drv_ops {
    int (*hovs_port_mgmt_add)(uint16_t *port_id, void *pci_addr);
    int (*hovs_port_mgmt_add_dynamic)(uint16_t *port_id, const struct nlattr *args, uint32_t args_len);
    void (*hovs_port_mgmt_del)(uint16_t port_id);
    int (*hovs_port_mgmt_get)(uint16_t port_id, struct nlattr *args, uint32_t *args_len);
    int (*hovs_port_mgmt_set)(uint16_t port_id, const struct nlattr *args, uint32_t args_len, struct nlattr *unset_args,
        uint32_t *unset_args_len);
    int (*hovs_port_mgmt_get_capability)(struct hovs_port_capability *cap);
    int (*hovs_port_mgmt_get_upcall_info)(struct hovs_port_upcall_info *info);
    int (*hovs_port_mgmt_setup_upcall_queue)(uint16_t port_id, uint16_t queue_id, unsigned int socket_id, void *mp);
    int (*hovs_port_mgmt_release_upcall_queue)(uint16_t port_id, uint16_t queue_id);
    int (*hovs_bond_mgmt_create)(uint8_t mode, const char *name, uint16_t *bond_port_id);
    void (*hovs_bond_mgmt_delete)(uint16_t bond_port_id);
    int (*hovs_bond_mgmt_get)(uint16_t bond_port_id, struct nlattr *args, uint32_t *args_len);
    int (*hovs_etheraddr_get)(uint16_t port_id, struct hovs_eth_addr *mac);
    int (*hovs_etheraddr_set)(uint16_t port_id, const struct hovs_eth_addr mac);
    int (*hovs_mtu_set)(uint16_t port_id, int mtu);
    int (*hovs_port_statistics_get)(uint16_t port_id, struct hovs_port_stats *stats);
    int (*hovs_port_statistics_flush)(uint16_t port_id);
    int (*hovs_bum_get)(uint16_t port_id, struct nlattr *args, uint32_t *args_len);
    int (*hovs_bum_set)(uint16_t port_id, const struct nlattr *args, uint32_t args_len, struct nlattr *unset_args,
        uint32_t *unset_args_len);
    int (*hovs_bum_remove)(uint16_t port_id, const struct nlattr *args, uint32_t args_len);
    int (*hovs_bum_clear)(uint16_t port_id);
    int (*hovs_flow_mgmt_get_forward_mode)(uint8_t *forward_mode);
    int (*hovs_flow_mgmt_set_forward_mode)(uint8_t forward_mode);
    int (*hovs_flow_mgmt_put)(const struct hovs_dpif_flow *put, const struct nlattr *args, size_t args_len);
    int (*hovs_flow_mgmt_get_by_key)(const struct nlattr *key, size_t key_len, struct hovs_dpif_flow_for_get *get);
    int (*hovs_flow_mgmt_get_by_ufid)(uint64_t ufid, struct hovs_dpif_flow_for_get *get);
    int (*hovs_flow_mgmt_del_by_key)(const struct nlattr *key, size_t key_len);
    int (*hovs_flow_mgmt_del_by_ufid)(uint64_t ufid);
    int (*hovs_flow_mgmt_del_by_batch)(const uint64_t *ufids, struct hovs_dpif_flow_for_get **flows, const size_t cnt);
    int (*hovs_flow_mgmt_flush)(void);
    int (*hovs_flow_mgmt_dump_start)(void **state);
    int (*hovs_flow_mgmt_dump_next)(void *state, struct hovs_dpif_flow_for_get *dump);
    int (*hovs_flow_mgmt_dump_done)(void *state);
    int (*hovs_flow_mgmt_get_maxflows)(uint32_t *max_flows);
    int (*hovs_flow_mgmt_get_capability)(struct hovs_flow_capability *cap);
    int (*hovs_statistics_flow_get_by_ufid)(const uint64_t *ufid, const size_t cnt, struct hovs_flow_stats *stats);
    int (*hovs_statistics_flow_flush_by_ufid)(uint64_t ufid);
    hovs_flow_callback_t (*hovs_flow_callback_register)(hovs_flow_callback_t cb);
    int (*hovs_global_cfg_set)(const struct nlattr *args, size_t args_len, struct nlattr *unset_args,
        size_t *unset_args_len);
    int (*hovs_global_cfg_get)(struct nlattr *args, size_t *args_len);
    int (*hovs_global_device_feature_get)(uint64_t *default_device_feature, uint64_t *support_device_feature);
    int (*hovs_global_statistics_get)(struct hovs_global_stats *stats);
    int (*hovs_global_statistics_flush)(void);
    int (*hovs_mml_lib)(const char *buf_in, uint32_t in_size, char *buf_out, uint32_t *out_len,
        uint32_t max_buf_out_len);
    int32_t (*hovs_lib_init)(void *arg);
    int (*hovs_lib_deinit)(void *arg);
    int (*hovs_open_log)(uint32_t module_type, uint32_t enable);
    int (*hovs_set_log_level)(uint32_t module_type, uint32_t log_level);
    int (*hovs_qos_statistics_get_batch)(uint16_t type, uint16_t *ids, struct hovs_qos_stats_batch *stats, size_t cnt);
    int (*hovs_qos_statistics_get_all_batch)(uint16_t type, uint16_t *ids, struct hovs_qos_stats_batch_all *stats,
        size_t cnt);
    int (*hovs_qos_statistics_clear_batch)(uint16_t type, uint16_t *ids, size_t cnt);
    int (*hovs_hqos_statistics_get_all_batch)(uint16_t type, uint16_t *ids, struct hovs_hqos_stats_batch_all *stats,
        size_t cnt);
    int (*hovs_hqos_statistics_clear_batch)(uint16_t type, uint16_t dir, uint16_t *ids, size_t cnt);
    int (*hovs_qos_vm_limit_set)(uint16_t group_id, uint16_t dir, uint16_t type, uint64_t max_rate, uint64_t max_burst,
        uint64_t min_rate, uint64_t min_burst);
    int (*hovs_qos_flow_limit_set)(uint16_t qos_id, uint16_t type, uint64_t max_rate, uint64_t max_burst,
        uint64_t min_rate, uint64_t min_burst);
    int (*hovs_qos_vm_limit_get)(uint16_t group_id, uint16_t dir, uint16_t type, uint64_t *max_rate,
        uint64_t *max_burst, uint64_t *min_rate, uint64_t *min_burst);
    int (*hovs_qos_flow_limit_get)(uint16_t qos_id, uint16_t type, uint64_t *max_rate, uint64_t *max_burst,
        uint64_t *min_rate, uint64_t *min_burst);
    int (*hovs_qos_vport_limit_set)(uint16_t port_id, uint16_t dir, uint16_t type,
        uint64_t max_rate, uint64_t max_burst, uint64_t min_rate, uint64_t min_burst);
    int (*hovs_qos_vport_limit_get)(uint16_t port_id, uint16_t dir, uint16_t type,
        uint64_t *max_rate, uint64_t *max_burst, uint64_t *min_rate, uint64_t *min_burst);
    int (*hovs_qos_net_limit_set)(uint16_t host_id, uint16_t dir, uint16_t type,
        uint64_t max_rate, uint64_t max_burst);
    int (*hovs_qos_net_limit_get)(uint16_t host_id, uint16_t dir, uint16_t type,
        uint64_t *max_rate, uint64_t *max_burst);
    uint16_t (*hovs_rte_tx_burst)(uint16_t dpdk_port_id, uint16_t dpdk_queue_id, void **tx_pkts, uint16_t nb_pkts);
    uint16_t (*hovs_rte_rx_burst)(uint16_t dpdk_port_id, uint16_t dpdk_queue_id, void **rx_pkts, uint16_t nb_pkts);
    int16_t (*hovs_rte_get_txq_backlog)(uint16_t vport_id, uint16_t hiovs_queue_id);
    int16_t (*hovs_rte_get_rxq_backlog)(uint16_t vport_id, uint16_t hiovs_queue_id);
    int (*hovs_global_pcie_list_query)(uint8_t front_back, uint8_t bdf_type, struct hovs_phy_dev_info *dev);
    int (*hovs_qos_vm_srtcm_limit_set)(uint16_t group_id, uint16_t dir, uint16_t type, uint64_t min_rate,
        uint64_t min_burst, uint64_t exs_burst);
    int (*hovs_qos_vm_srtcm_limit_get)(uint16_t group_id, uint16_t dir, uint16_t type, uint64_t *min_rate,
        uint64_t *min_burst, uint64_t *exs_burst);
    int (*hovs_bond_slave_statistics_get)(uint16_t port_id, struct hovs_bond_slave_stats *stats);
    int (*hovs_port_mgmt_set_upcall_priority)(uint16_t port_id, struct hovs_high_priority_protocol_cfg cfgs[],
                                            uint32_t config_num, uint8_t upcall_queue_num);
    int (*hovs_port_mgmt_get_usage_state)(uint16_t port_id, uint32_t *device_status);
    int (*hovs_port_mgmt_set_usage_state)(uint16_t port_id, uint16_t status);
    int (*hovs_upcall_mtu_set)(uint8_t type, int mtu);
    int (*hovs_mega_flow_mgmt_put)(const struct hovs_dpif_flow *put, uint32_t index, uint64_t *ufid);
    int (*hovs_mega_flow_mgmt_del_by_ufid)(uint64_t ufid);
    int (*hovs_mega_flow_mgmt_flush)(void);
    int (*hovs_mega_flow_mgmt_dump_start)(void **state);
    int (*hovs_mega_flow_mgmt_dump_next)(void *state, struct hovs_dpif_flow_for_get *dump);
    int (*hovs_mega_flow_mgmt_dump_done)(void *state);
    int (*hovs_statistics_mega_flow_get_by_ufid)(const uint64_t ufid, struct hovs_flow_stats *stats);
    int (*hovs_mega_flow_set_l3_forward)(bool flag);
    int (*hovs_flow_mgmt_get_block_table_size)(uint32_t *block_num);
    int (*hovs_flow_mgmt_update)(const struct hovs_dpif_flow *modify, const struct nlattr *args, size_t args_len);
    void (*hovs_flow_mgmt_set_block_version)(uint32_t block_num, uint16_t block_id[],
        uint16_t block_version[], int result[]);
    int (*hovs_flow_mgmt_get_block_version)(uint32_t block_num, uint16_t block_id[], uint16_t block_version[]);
    int (*hovs_hotplug_add)(uint16_t port_id);
    int (*hovs_hotplug_del)(uint16_t port_id);

    /*灰卡特有接口*/
    int (*hovs_flexda_flow_mgmt_put)(uint32_t table_id, const struct hovs_flexda_dpif_flow *put, const struct nlattr *args, size_t args_len);
    int (*hovs_flexda_flow_mgmt_get_by_ufid)(uint32_t table_id, uint64_t ufid, struct hovs_flexda_dpif_flow_for_get *get);
    int (*hovs_flexda_flow_mgmt_del_by_ufid)(uint32_t table_id, uint64_t ufid);
    int (*hovs_flexda_flow_mgmt_del_by_batch)(uint32_t table_id, const uint64_t *ufids, struct hovs_dpif_flow_for_get **flows, const size_t cnt);
    int (*hovs_flexda_flow_mgmt_flush)(void);
    int (*hovs_flexda_flow_mgmt_dump_start)(uint32_t table_id, void **state);
    int (*hovs_flexda_flow_mgmt_dump_next)(void *state, struct hovs_dpif_flow_for_get *dump);
    int (*hovs_flexda_flow_mgmt_dump_done)(void *state);
    int (*hovs_flexda_flow_mgmt_get_maxflows)(uint32_t table_id, uint32_t *max_flows);
    int (*hovs_flexda_statistics_flow_get_by_ufid)(uint32_t table_id, const uint64_t *ufid, const size_t cnt, struct hovs_flow_stats *stats);
    hovs_flexda_flow_callback_t (*hovs_flexda_flow_callback_register)(hovs_flexda_flow_callback_t cb);

    int32_t (*hovs_flexda_get_config_info)(hovs_flexda_config_info_t *config_info);
    void (*hovs_flexda_free_config_info)(hovs_flexda_config_info_t *config_info);

    int (*hovs_flexda_global_cfg_set)(const struct nlattr *args, size_t args_len, struct nlattr *unset_args,
        size_t *unset_args_len);
    int32_t (*hovs_flexda_lib_init)(void *arg);
    int (*hovs_flexda_lib_deinit)(void *arg);
    uint16_t (*hovs_flexda_rte_rx_burst)(uint16_t dpdk_port_id, uint16_t dpdk_queue_id, void **tx_pkts, uint16_t nb_pkts);
    int (*hovs_flexda_mml_lib)(const char *buf_in, uint32_t in_size, char *buf_out, uint32_t *out_len, uint32_t max_buf_out_len);

    // 解耦新增
    int (*hovs_flexda_init_adapter)(hinic3_flexda_ovs_ops_t *ops, bool *adapted);
    int (*hovs_flexda_deinit_adapter)(void);
    int (*hovs_flexda_init_ctx)(void **ctx);
    int (*hovs_flexda_deinit_ctx)(void **ctx);
    int (*hovs_flexda_update_ctx)(void *ctx, const struct hinic3_flexda_ovs_info_t *info);
    int (*hovs_flexda_extract_hdr)(void *ctx, const struct hinic3_flexda_ovs_info_t *info);
    int (*hovs_flexda_construct_key)(void *ctx, const struct hinic3_flexda_ovs_info_t *info,
        struct rte_flow_item *hw_key);
    int (*hovs_flexda_construct_action)(void *ctx, const struct hinic3_flexda_ovs_info_t *info,
        struct rte_flow_action *hw_action);
    int (*hovs_flexda_construct_attr)(void *ctx, const struct hinic3_flexda_ovs_info_t *info,
        struct rte_flow_attr *attr);
    int (*hovs_flexda_check_tunnel)(const char *netdev_type);

    //comnet
    int (*hovs_acl_indir_counter_alloc)(uint8_t *indir_id);
    int (*hovs_acl_indir_counter_get)(uint8_t indir_id, struct hovs_acl_indir_stats *indir_stats);
    int (*hovs_acl_indir_counter_free)(uint8_t indir_id);
    int (*hovs_acl_indir_counter_reset)(uint8_t indir_id);
    int (*hovs_acl_mgmt_put)(const struct hovs_dpif_acl *put);
    int (*hovs_acl_mgmt_del)(uint16_t group_id, uint16_t index);
    int (*hovs_acl_get_stats)(uint16_t group_id, uint16_t index, struct hovs_acl_stats *acl_stats);
    int (*hovs_acl_mgmt_flush)(uint16_t group_id);
    int (*hovs_acl_mgmt_dump_start)(uint16_t group_id, void **state);
    int (*hovs_acl_mgmt_dump_next)(void *state, struct hovs_dpif_acl_for_get *get);
    int (*hovs_acl_mgmt_dump_done)(void *state);
    int (*hovs_dp_hash_mgmt_put)(const struct hovs_dpif_flow *put, const struct nlattr *args, size_t args_len);
    int (*hovs_dp_hash_mgmt_del_by_key)(const struct nlattr *key, size_t key_len);
    int (*hovs_dp_hash_mgmt_get_by_key)(const struct nlattr *key, size_t key_len,
        struct hovs_dpif_flow_for_get *get);
    int (*hovs_dp_hash_mgmt_dump_start)(void **state);
    int (*hovs_dp_hash_mgmt_dump_next)(void *state, struct hovs_dpif_flow_for_get *get);
    int (*hovs_dp_hash_mgmt_dump_done)(void *state);
    int (*hovs_dp_hash_mgmt_flush)(void);
};

#endif
