/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_FLOW_AGNET_PUBLIC_H
#define HINIC3_FLOW_AGNET_PUBLIC_H

#include "rte_flow.h"
#include "rte_config.h"
#include "rte_spinlock.h"
#include "hinic3_list.h"
#include "hinic3_init_arg.h"
#include <rte_atomic.h>
#include "hinic3_packet_key_public.h"
#include "hinic3_thread.h"
#include "hinic3_mutex.h"
#include "hinic3_map.h"
#include "hinic3_trace_flow.h"
#include "hinic3_flow_agent_enum.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HINIC3_RX_THREAD_MAX              16
#define HINIC3_FLOW_STATS_BLOCK_SIZE      128
#define HINIC3_THREAD_NAME_MAX            16
#define HINIC3_COMMAND_QUEUES_NUM_MAX     HINIC3_RX_THREAD_MAX
#define HINIC3_NETDEV_CLASS_NUM           16
#define HINIC3_MAX_ENTRY_PER_STATS_DUMP   1000
#define HINIC3_MAX_ENTRY_PER_BATCH_DEL    128
#define HINIC3_ESCAPE_MODE_OFFSET         0U
#define HINIC3_FORWARD_MODE_OFFSET        3U
#define HINIC3_MODE_VERSION_OFFSET        6U

#define HINIC3_THREAD_RX_BURST_MAX        256
#define HINIC3_MAX_OFFLOAD_CORE_CNT       16
#define HINIC3_MAX_PMD_CORE_CNT           32
#define HINIC3_MAX_DPDK_CORE_CNT          32
#define HINIC3_CORE_MASK_LEN_MAX          256

#define MAX_PKT_BURST               32
#define BUFSIZE_SHORT               1024
#define PKT_MAX_ASSOCIATED_FLOWS    16
#define HINIC3_MSG_MAX_BUF           512
#define HINIC3_MAX_LCORE_COUNT       128
#define HINIC3_MAX_UNIXCTL_MAX_LEN   1024

#define HINIC3_BYTE_BITS             8
#define HINIC3_SHORT_BITS            16

#define HINIC3_RAW_ACTION_NUM 10

#define HINIC3_LOW_EIGHT_BIT_MASK 0xFF
#define HINIC3_MID_EIGHT_BIT_MASK 0XFF00
#define HINIC3_HIGH_EIGHT_BIT_MASK 0XFF0000
#define HINIC3_BIT_MID_MOVE_INDEX 8
#define HINIC3_BIT_HIGH_MOVE_INDEX 16
#define HINIC3_TCP_PROTO 6
#define HINIC3_UDP_PROTO 17
#define HINIC3_VNI_ARR_LOW 0
#define HINIC3_VNI_ARR_MID 1
#define HINIC3_VNI_ARR_HIGH 2
#define HINIC3_VNI_ARR_SIZE 3

#define HINIC3_TABLE_NUM_MAX 13
#define HINIC3_DEFAULT_TABLE_ID 0

#define HINIC3_IGNORE_REPEAT_TIME 100
#define MAX_PMD_CORE 32

typedef union {
    uint32_t u32[4];
    struct {
#ifdef WORDS_BIGENDIAN
        uint64_t hi, lo;
#else
        uint64_t lo, hi;
#endif
    } u64;
} hinic3_u128;

struct rte_flow {
    /* rte_flow mega ufid. */
    hinic3_u128 mega_ufid;
    /* hardware flow ufid. */
    uint64_t hw_ufid;
    /* Number of packets matched in last query. */
    uint64_t sw_packets;
    /* Number of bytes matched in last query. */
    uint64_t sw_bytes;
    /* Number of packets matched in up-to-date query. */
    uint64_t hw_packets;
    /* Number of bytes matched in up-to-date query. */
    uint64_t hw_bytes;
    bool  is_deleted;
    void* flow_data;
    void* context;
    rte_spinlock_t lock;

    uint32_t hash;

    uint8_t session_id;
    uint8_t table_id;
    uint16_t port_id;
    uint32_t meter_id;
    uint32_t flow_hash;
    struct hinic3_conntrack_full_key key;
    struct {
        uint32_t is_acl : 1;
        uint32_t is_mega : 1;
        uint32_t is_offload : 1;
        uint32_t is_sample : 2;
        uint32_t is_mem_used : 1;
        uint32_t live_time : 13;
        uint32_t is_dp_hash : 1;
        uint32_t is_aged : 1;
        uint32_t is_dumb : 1;
        uint32_t has_flow_qos : 1;          // 0：非流量控制的流表；1：流量控制的流表
        uint32_t is_multi_level_qos : 1;    // 0：其他流表；1：多级限速绑定的流表
        uint32_t multi_level_qos_index : 2; // 0：BW_TX；1：PPS_TX；2：BW_RX；3：PPS_RX
        uint32_t reserved : 6;
    } flags;
};
/*
    模糊流表中存在三个fullkey，其中rte_flow中的fullkey是计算后的key，fuzzy_flow结构体中的key值和mask值表示原始的数据
*/
struct fuzzy_flow {
    struct rte_flow flow;
    // raw mask
    struct hinic3_conntrack_full_key mask;
    // raw key
    struct hinic3_conntrack_full_key raw_key;
};

struct hinic3_flow_capability {
    uint32_t vendor_id;
    uint32_t supported_dev_types;
    uint32_t supported_actions;
    uint8_t key_format_type;
    uint8_t cleanup_max_level;
    uint16_t flags;
    uint32_t max_flow_size;
    uint32_t rx_thread_num;
    uint32_t cmd_queue_base;
    uint32_t cmd_queue_num;
};

struct hinic3_flow_stats {
    /* Number of packets matched. */
    uint64_t packet_count;
    /* Number of bytes matched. */
    uint64_t byte_count;
    /* Number of packets dropped for CT failure. */
    uint64_t ct_loss_pkts;
    /* Remain life time of flow, unit : ms */
    uint32_t live_time;
    /* Total life time of flow, unit : ms */
    uint32_t age_time;
    uint16_t tcp_flags;
    uint8_t block[HINIC3_FLOW_STATS_BLOCK_SIZE];
};

struct hinic3_offload_thread_data {
    pthread_t thread;
    uint32_t thread_id;
    uint32_t exit;
    struct hinic3_dp_extend_info *dp_info;
    char name[HINIC3_THREAD_NAME_MAX];
    uint32_t cur_buk_idx;
    uint32_t min_buk_idx;
    uint32_t max_buk_idx;
};

struct hinic3_forward_engine_info {
    uint32_t port_id;
    uint32_t cmd_queue_base;
    uint32_t cmd_queue_num;
    struct rte_mempool *cmd_mbuf_pool;
};


struct hinic3_dp_extend_info {
    /* Private data of flow agent */
    void *hw_offload;
    /* offload thread */
    struct hinic3_offload_thread_data *offload_threads;
    /* offload thread operations */
    struct hinic3_rx_thread_ops *offload_thread_ops;
    /* Information of forward engine */
    struct hinic3_forward_engine_info forward_engine;
    /* Core info in env */
    uint32_t offload_num_in_env;
    uint32_t offload_thread_core[HINIC3_CPU_MASK_NUM];
    uint32_t control_num_in_env;
    uint32_t control_thread_core[HINIC3_CPU_MASK_NUM];
};

struct hinic3_rx_thread_ops {
    void (*rx_put_ack)(uint32_t thread_id, struct hinic3_dp_extend_info *dp_info);
    void (*rx_age_notice)(uint32_t thread_id, const struct hinic3_dp_extend_info *dp_info);
    void (*del_sw_flow)(uint32_t thread_id, const struct hinic3_dp_extend_info *dp_info);
    int (*sync_stats)(uint32_t thread_id, struct hinic3_dp_extend_info *dp_info);
};

struct hinic3_netdev_class_info {
    uint64_t id;
    char *name;
};
struct hinic3_forward_engine {
    /* Information from forward engine */
    struct hinic3_flow_capability cap;

    /* Information Derived from above */
    struct {
        uint32_t use_hw_ifindex : 1;
        uint32_t reserved : 31;
    };
    /* lock to protect current_flow_size */
    rte_spinlock_t lock;
    rte_atomic32_t current_flow_size;
    int netdev_class_num;
    struct hinic3_netdev_class_info supported_netdev_class[HINIC3_NETDEV_CLASS_NUM];
};

struct hinic3_sw_flow {
    hinic3_u128 mega_ufid;
    int actions_size;
    uint8_t actions[BUFSIZE_SHORT];
    struct rte_flow_action raw_action[HINIC3_RAW_ACTION_NUM];
    bool action_recorded;
};

struct hinic3_pkt_info {
    struct {
        uint32_t offload : 2;
        uint32_t vxlan : 2;
        uint32_t geneve : 2;
        uint32_t ct_establish : 1;
        uint32_t ct_tracked : 1;
        uint32_t snat : 1;
        uint32_t dnat : 1;
        uint32_t orig_tuple : 1;
        uint32_t first_ct_notrack : 1;
        uint32_t last_ct_notrack : 1;
        /* Outer vlan of transmited packet is pushed by OVS actions */
        uint32_t pushed_outer_vlan : 1;
        /* Inner vlan of transmited packet is pushed by OVS actions */
        uint32_t pushed_inner_vlan : 1;
        uint32_t snat_before_ct : 1;
        uint32_t dnat_before_ct : 1;
        uint32_t nat_done_by_ct : 1;
        /* Flag of cvlan in QinQ is recorded */
        uint32_t cvlan : 1;
        uint32_t vxlan_popped : 1;  // set true after check action VXLAN_DECAP
        uint32_t vxlan_popped_fin : 1;  // set true after vxlan decap done
        uint32_t geneve_poped : 1;
        uint32_t geneve_push : 1;
        /* if already fail, we will not process this packet in the left circles, because some action was not recorded */
        uint32_t already_fail : 1;
    } status;

    struct {
        uint16_t ct_action_exist : 1;
        uint16_t encap_action_exist : 1;
    } cur_round_status;
    /* push vlan for receiving packet by OVS actions */
    uint16_t push_vlan_id;
    /* Inner vlan of received packet is popped by OVS actions */
    uint16_t popped_inner_vlan;
    /* Inner vlan of double vlan in QinQ */
    uint16_t inner_cvlan;
    uint16_t input_port;
    uint32_t input_port_type;
    uint16_t first_ct_zone;
    uint16_t last_ct_zone;
    uint8_t flows_num;
    uint32_t hw_conn_id;
    uint32_t vxlan_vni;
    uint32_t geneve_vni;
    struct hinic3_pkt_header orig_pkt_hdr;

    /* Above area will be cleared when init */
    uint8_t is_used : 1;
    uint8_t is_recircle : 1;
    uint8_t padings : 6;
    struct hinic3_sw_flow associated_flows[PKT_MAX_ASSOCIATED_FLOWS];

    /* used by flexda ovs adapter */
    void *hydra_ctx;
};

struct hinic3_pmd_status {
    /* This sequence will add in every PMD receive round */
    uint32_t rx_seq;
    struct hinic3_pkt_info pkt_info_bufs[MAX_PKT_BURST];
};

struct hinic3_offload_buf_s {
    uint8_t key_buf[HINIC3_MSG_MAX_BUF];
    uint8_t mask_buf[HINIC3_MSG_MAX_BUF];
    uint8_t actions_buf[HINIC3_MSG_MAX_BUF];
    uint8_t final_actions_buf[HINIC3_MSG_MAX_BUF];
    uint8_t args_buf[HINIC3_MSG_MAX_BUF];
};

struct hinic3_rarp_mac_s {
    uint8_t mac[ETH_ALEN];
    long long int last_proc_time;
    struct hinic3_list node;
};

struct hinic3_pkt_key_cache_s {
    int count;
    struct hinic3_conntrack_full_key full_key_list[MAX_PKT_BURST];
};

struct hinic3_virtio_queue {
    uint32_t total_count;
    uint32_t used_now;
};

struct hinic3_flow_agent_db {
    /* Forward engine capability description */
    struct hinic3_forward_engine forward_engine;
    rte_spinlock_t operate_disable_lock;
    /* change offload disable status operate count */
    uint16_t operate_disable;
    uint16_t escape_mode;
    /* How long can offload entry live */
    uint32_t live_time;
    uint16_t pmd_status_num;
    struct hinic3_pmd_status *pmd_status;
    struct hinic3_offload_buf_s offload_buf_list[HINIC3_MAX_LCORE_COUNT];
    struct hinic3_list pmd_rarp_list[HINIC3_MAX_LCORE_COUNT];
    /* The time when next hardware stats syncing is to do */
    long long next_hw_stats_sync[HINIC3_COMMAND_QUEUES_NUM_MAX];
    struct hinic3_trace_flow trace_flow_info;
    struct hinic3_virtio_queue queue_num;
    uint32_t max_block_num;
    uint16_t vxlan_dst_port;
};

struct hinic3_stats_dump_context {
    void *dp_info;
    /* current num of hardware flow */
    uint32_t num_entries;
    /* total num of hardware flow */
    uint32_t total_hw_num;
    /* per thread limits num of hardware flow */
    uint32_t total_num_limit;
    /* sync start time */
    long long start_time;
    /* per dump private info */
    void *priv_data[HINIC3_MAX_ENTRY_PER_STATS_DUMP];
    /* per dump ufid list */
    uint64_t ufids[HINIC3_MAX_ENTRY_PER_STATS_DUMP];
    /* per dump statistics info */
    struct hinic3_flow_stats stats[HINIC3_MAX_ENTRY_PER_STATS_DUMP];
    /* per dump table id */
    uint8_t table_id;
};

void hinic3_pmd_status_init(void);
struct hinic3_pmd_status *hinic3_pmd_status_get(uint32_t lcore_idx);

#ifdef __cplusplus
}
#endif

#endif
