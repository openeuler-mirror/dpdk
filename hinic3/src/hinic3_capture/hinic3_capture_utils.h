/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_CAPTURE_UTILS_H
#define HINIC3_CAPTURE_UTILS_H

#include <stdint.h>
#include <stdio.h>

#include "rte_ether.h"
#include "rte_mempool.h"
#include "rte_spinlock.h"
#include "rte_ring.h"
#include "hinic3_list.h"

#include "hinic3_command.h"
#include "hinic3_message.h"
#include "hinic3_ds.h"

#define PCAP_MAX_FILE_NAME          1024
#define PCAP_MAX_RING_NAME          32
#define PCAP_MAX_PORT_NAME          32
#define PCAP_MAX_PORT_COMBINE_NAME  64

#define PCAP_MAX_CAP_TASK           4
#define PCAP_EPOLL_SIZE             1024
#define PCAP_DUMP_SIZE              160
#define PCAP_MEMPOOL_SIZE          (8192 * 10)
#define PCAP_RING_SIZE              8192
#define PCAP_EPOLL_TIMEOUT         (20 * 1000)

#define PCAP_TIME_S_TO_MS           1000
#define PCAP_SLEEP_TIME             1000

#define PCAP_PERIOD_US              1000
#define PCAP_CPU_TARGET_RATE        0.3
#define PCAP_PID_KI                 0.1

#define PCAP_CMD_MIN_PARAM          1
#define PCAP_CMD_MAX_PARAM          36
#define PCAP_CMD_START_MIN_PARAM    3
#define PCAP_CMD_START_MAX_PARAM    35
#define PCAP_CMD_STOP_MIN_PARAM     1
#define PCAP_CMD_STOP_MAX_PARAM     2
#define PCAP_CMD_SHOW_MIN_PARAM     1
#define PCAP_CMD_SHOW_MAX_PARAM     2
#define PCAP_CMD_ENABLE_MAX_PARAM   2
#define PCAP_CMD_TOO_MANY_ARGS      (-1)
#define PCAP_CMD_TOO_FEW_ARGS       (-2)
#define PCAP_CMD_ERR_ARGS           (-3)

#define PCAP_STOP_CHECK_PERIOD_MS   5
#define PCAP_STOP_TIME_OUT_MS      (10 * 1000)
#define PCAP_STOP_ALL_TIME_OUT_MS  (30 * 1000)
#define PCAP_DISABLE_TIME_S        (2 *60 * 60)
#define PCAP_TIME_MIN_TO_S          60

#define PCAP_IPV4_MAX_MASK_LEN      32
#define PCAP_IPV6_MAX_MASK_LEN      128

#define PCAP_MAX_RULE_NUM           255

#define PCAP_PROBE_DISABLE          0
#define PCAP_PROBE_ENABLE           1
#define PCAP_THREAD_NORMAL_STATUS   0
#define PCAP_THREAD_EXIT_STATUS     1

#define CPU_MAX_NUMBER              23U

#define PCAP_MEMPOOL_NAME           "hinic3-pcap-save-mp:"
#define PCAP_SAVE_THREAD_NAME       "pcap-save-thread"
#define PCAP_RING_NAME_PREFIX       "hinic3-pcap-ring"

enum {
    ENPCAP_HELP_OPT,
    ENPCAP_CPU_MODE_OPT,
    ENPCAP_PCAP_MODE_OPT,
    ENPCAP_QUERY_OPT,
};

enum hinic3_pcap_mode {
    FULLY_CAPTURE = 0,
    LIMITED_CAPTURE = 1,
    ONLY_HEADER = 2,
};

enum PCAP_CPU_USAGE_TYPE {
    PCAP_CPU_HIGH,
    PCAP_CPU_LOW
};

enum pcap_write_way {
    WRITE_BY_PMD = 0,
    WRITE_BY_SEP_THREAD
};

enum pcap_ip_type {
    PCAP_IP_ADDR_V4 = 1,
    PCAP_IP_ADDR_V6
};

enum PCAP_PKT_MEM_TYPE {
    PCAP_PKT_MEM_REUSED = 1,
    PCAP_PKT_MEM_MALLOC
};

struct pcap_ip_t {
    union {
        uint32_t ip4;
        struct in6_addr ip6;
    };
};

struct input_key {
    uint8_t dmac[HINIC3_ETH_ADDR_LEN];    /* Destination MAC address */
    uint8_t smac[HINIC3_ETH_ADDR_LEN];    /* Source MAC address */
    uint16_t eth_type;
    struct pcap_ip_t sip;
    struct pcap_ip_t dip;      /* Destination IP address, network order */
    uint8_t ip_proto;
    uint16_t vlan_id;
    uint16_t sport;
    uint16_t dport;
    bool is_vxlan;
    uint8_t port_id;
    uint8_t icmp_type;
    uint8_t icmp_code;
    uint16_t icmp_id;
    uint32_t vni;
    rte_be16_t inner_type;
};

struct pcap_key_t {
    uint32_t flags;
    uint64_t count_total;
    char filename[PCAP_MAX_FILE_NAME];
    char output_path[PCAP_MAX_FILE_NAME];
    char absolute_path[PCAP_MAX_FILE_NAME];
    uint32_t ip_type;
    struct pcap_ip_t sip;
    struct pcap_ip_t dip;
    struct pcap_ip_t host_ip;
    uint16_t sport;
    uint16_t dport;
    uint32_t vxlan_vni;
    uint8_t  sip_masklen;
    uint8_t  dip_masklen;
    uint8_t  host_masklen;
    uint8_t  smac[RTE_ETHER_ADDR_LEN];
    uint8_t  dmac[RTE_ETHER_ADDR_LEN];
    uint16_t eth_type;
    uint8_t  ip_proto;
    uint8_t  direction;
    uint16_t vlan_id;
    bool     vxlan_inner;
    uint32_t port_id;
    uint8_t icmp_type;
    uint8_t icmp_code;
    uint16_t icmp_id;
    bool is_vxlan;
    rte_be16_t inner_type;
    enum pcap_write_way write_way;
    long long task_start_time;
    long long task_time;
    uint32_t filenum;
    uint32_t count;
};

struct pcap_stats_t {
    uint64_t wr_cnt;
    uint64_t en_ring_fail_cnt;
    uint64_t soft_drop_cnt;
    struct hinic3_pcap_probe_stats hinic3_stats;
};

struct pcap_file_record_hdr {
    uint32_t pkt_ts_sec;
    uint32_t pkt_ts_usec;
    uint32_t pkt_incl_len;
    uint32_t pkt_orig_len;
};

struct pcap_driver_t {
    bool stop_flag;
    struct hinic3_pcap_probe_filter_t driver_filter;
    struct hinic3_pcap_probe_rule_q_map queue_info;
};

struct pcap_task_mirror_t {
    bool stop_flag;
    bool wr_fail_flag;
    uint64_t remain_count;
};

struct pcap_port_t {
    char name[PCAP_MAX_PORT_NAME];
    bool support_cap;
    uint32_t odp_port_no;
    uint16_t hinic3_port_id;
};

struct pcap_task_t {
    struct pcap_key_t key;
    struct pcap_stats_t stats;
    struct pcap_driver_t driver;

    bool stop_flag;
    bool wr_fail_flag;
    uint32_t pcap_id;
    uint64_t remain_count;
    uint32_t count_per_file;
    uint32_t filenum;
    uint32_t file_index;
    uint32_t file_pkt_cnt;

    int event_fd;
    FILE *save_file;
    struct rte_ring *ring;

    rte_spinlock_t lock;
    uint32_t ref_cnt;
    uint32_t rule_idx;
    char *parameter;
    struct pcap_port_t pcap_port;
};

struct pcap_task_mgr_t {
    rte_spinlock_t lock;
    uint32_t  task_cnt;
    pthread_t save_thread;
    bool thread_exit;
    int epoll_fd;
    struct rte_mempool *mem_pool;
    struct pcap_task_t *cap_task_list[PCAP_MAX_CAP_TASK];
};

struct pcap_task_save_t {
    pthread_t thread;
    bool thread_exit;
};

struct pcap_task_batch {
    int count;
    struct pcap_task_t *task_array[PCAP_MAX_CAP_TASK];
};

struct pcap_cmd_t {
    const char *cmd;
    const char *usage;
    unixctl_cb_func *cb;
    int min_args;
    int max_args;
    const char *desc;
};

struct pcap_start_param {
    struct pcap_port_t port_info;
    struct pcap_key_t pcap_key;
};

struct pcap_stop_param {
    uint32_t port_no;
    uint32_t pcap_id;
};

struct pcap_show_param {
    bool is_all;
    uint32_t pcap_id;
};

struct pcap_stop_task_ctl {
    bool is_stopped;
    int  result;
    uint32_t index;
    struct pcap_task_t *task;
};

struct pcap_save_stats_t {
    bool write_fail;
    uint32_t wr_iov_cnt;
    uint32_t wr_cnt;
    uint32_t en_ring_fail_cnt;
    uint32_t parse_fail_cnt;
};

static inline void pcap_task_batch_init(struct pcap_task_batch *task_batch)
{
    task_batch->count = 0;
}

struct pcap_task_save_t *hinic3_get_cap_task_save(void);
struct rte_mempool **hinic3_get_pcap_shared_mp(void);
void hinic3_set_ticks_per_ms(uint64_t value);
bool pcap_check_file_used_no_lock(struct pcap_key_t *cap_key);
struct pcap_task_mgr_t* pcap_get_task_mgr(void);
void pcap_port_tasks_get(struct pcap_task_batch *task_batch, struct pcap_port_t *port_mirror);
int pcap_port_tasks_count_no_lock(uint32_t port_no);
int pcap_port_info_get_by_name(const char *port_name, struct pcap_port_t *port_info, struct ds *ds);
void pcap_mode_set(enum hinic3_pcap_mode pcap_mode);
enum hinic3_pcap_mode pcap_mode_get(void);
void pcap_cpu_usage_set(enum PCAP_CPU_USAGE_TYPE value);
enum PCAP_CPU_USAGE_TYPE pcap_cpu_usage_get(void);
int pcap_switch_get(void);
void pcap_switch_set(int value);
int pcap_task_get(void);
void pcap_task_set(int value);
int pcap_time_get(void);
void pcap_time_set(long long value);
int pcap_task_add(const struct pcap_port_t *port_info, struct pcap_key_t *pcap_key, uint32_t *pcap_id,
    struct ds *save_param);
void pcap_task_batch_put(struct pcap_task_batch *task_batch);
int pcap_task_delete(struct pcap_stop_param *param, struct ds *ds);
int pcap_task_delete_all(uint32_t port_no, struct ds *ds);
int pcap_task_delete_one(uint32_t pcap_id, struct ds *ds);
struct pcap_task_t* pcap_task_find_no_lock(uint32_t pcap_id, uint32_t *index);
struct pcap_task_t* pcap_task_get_by_id(uint32_t pcap_id, struct pcap_task_mirror_t *task_mirror);
void pcap_task_mgr_spin_lock(void);
void pcap_task_mgr_spin_unlock(void);
void pcap_task_mirror_get(struct pcap_task_t *cap_task, struct pcap_task_mirror_t *task_mirror);
void pcap_task_put(struct pcap_task_t *cap_task);
void pcap_task_spin_lock(struct pcap_task_t *task);
void pcap_task_spin_unlock(struct pcap_task_t *task);
void pcap_task_stats_update(struct pcap_task_t *cap_task, struct pcap_save_stats_t *save_stats);
void *pcap_thread_main(void *arg);
bool pcap_timeout_check_s(long long start_sec, long long timeout_s);
void pcap_task_stop_set(uint32_t pcap_id, struct pcap_stop_task_ctl *stop_ctl);
void pcap_task_destroy(struct pcap_task_t *cap_task);
#endif /* HINIC3_CAPTURE_UTILS_H */
