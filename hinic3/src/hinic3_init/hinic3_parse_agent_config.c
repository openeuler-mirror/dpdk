/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2024 Huawei Technologies Co., Ltd
 */

#include "hinic3_util.h"
#include <ctype.h>
#include <stdlib.h>
#include <errno.h>
#include "hinic3_log.h"
#include "hinic3_util.h"
#include "hinic3_driver_public.h"
#include "hinic3_file_util.h"
#include "hinic3_iface_global.h"
#include "hinic3_capture_utils.h"
#include "hinic3_init_arg.h"
#include "hinic3_queue.h"
#include "hinic3_parse_agent_config.h"

#define QOS_SECTION "hwoff_QoS"
#define AGENT_SECTION "hwoff_agent"
#define AGENT_SECTION_OFFLOAD_THREAD_NUM_KEY "offload_thread_num"
#define AGENT_SECTION_MAX_FLOW_NUM_KEY "max_flow_num"
#define AGENT_SECTION_RUN_TIME_MODE "run_time_mode"
#define AGENT_SECTION_CARD_MODE "card_mode"
#define AGENT_SECTION_BOND_HASH_POLICY "bond_hash_policy"
#define AGENT_SECTION_FORWARD_CPU_MASK_KEY "forward_cpu_mask"
#define AGENT_SECTION_CONTROL_CPU_MASK_KEY "control_cpu_mask"
#define AGENT_SECTION_PF_PCI_ADDR_KEY "pf_pci_addr"
#define AGENT_SECTION_DISK_USAGE_KEY "disk_usage"
#define AGENT_SECTION_PCAP_CPU_KEY "pcap_cpu"
#define MEMINFO_SECTION_ENABLE_HUGEPAGE_MEMINFO "hugepage_meminfo_statistic"
#define PORT_SECTION "hwoff_port"
#define PORT_SECTION_MAX_QUEUE_NUM_KEY "max_queue_num"
#define PORT_SECTION_UPCALL_QUEUE_NUM_KEY "upcall_queue_num"
#define PORT_SECTION_MBUF_SIZE_KEY "mbuf_size"
#define PORT_SECTION_VDPA_FEATURE_KEY "vdpa_virtio_feature"
#define PORT_SECTION_BOND_RX_DEPTH "bond_rx_depth"
#define PORT_SECTION_BOND_TX_DEPTH "bond_tx_depth"
#define PORT_SECTION_VPORT_RX_DEPTH "vport_rx_depth"
#define PORT_SECTION_VPORT_TX_DEPTH "vport_tx_depth"
#define PORT_SECTION_VIRTUAL_QUEUE_MULTIPLEX "virtual_queue_multiplex"
#define FLOW_SECTION "hwoff_flow"
#define FLOW_SECTION_FLOW_MAX_IDLE_KEY "flow_max_idle"
#define FLOW_SECTION_AGENT_FORWARD_MOD_KEY "packet_forward_mode"
#define PORT_VIRTIO_QUEUE_DEPTH "virtio_queue_depth"
#define QOS_SECTION_QOS_LEVEL_KEY "qos_level"
#define HINIC3_FORWARD_CPU_MASK_DEFAULT_NUM 0
#define HINIC3_CONTROL_CPU_MASK_DEFAULT_NUM 0
#define HINIC3F_MAX_CPU_MASK_LENGTH 1024
#define HINIC3_OFFLOAD_THREAD_INVALID_NUM 3
#define HINIC3_OFFLOAD_THREAD_DEFAULT_NUM 2
#define HINIC3_OFFLOAD_THREAD_NUM_MIN 1
#define HINIC3_OFFLOAD_THREAD_NUM_MAX 4
#define HINIC3_CARD_MODE_MIN 0
#define HINIC3_CARD_MODE_MAX 1
#define HINIC3_BOND_HASH_POLICY_MIN 0
#define HINIC3_BOND_HASH_POLICY_MAX 1
#define HINIC3_MAX_FLOW_DEFAULT_NUM 2
#define HINIC3_MAX_FLOW_NUM_MIN 1
#define HINIC3_MAX_FLOW_NUM_MAX 4
#define HINIC3_MAX_FLOW_NUM_INVALID 3
#define HINIC3_MAX_QUEUE_DEFAULT_NUM 4
#define HINIC3_MAX_QUEUE_LIMIT_DEFAULT_NUM 32
#define HINIC3_MAX_QUEUE_NUM_MIN 1
#define HINIC3_MAX_QUEUE_NUM_MAX 64
#define HINIC3_UPCALL_QUEUE_DEFAULT_NUM 4
#define HINIC3_UPCALL_QUEUE_NUM_MIN 1
#define HINIC3_UPCALL_QUEUE_NUM_MAX 16
#define HINIC3_MBUF_DEFAULT_SIZE 2176
#define HINIC3_FLOW_MAX_IDLE_MIN 30000
#define HINIC3_FLOW_MAX_IDLE_MAX 500000
#define HINIC3_DISK_USAGE_MIN 10
#define HINIC3_DISK_USAGE_MAX 10240
#define HINIC3_PCAP_CPU_MAX 23U

#define STR_TO_DEC_NUM 10
#define STR_U64_HEX_MAX_LENGTH  19 // 0XFFFFFFFFFFFFFFFF + '\0
#define HINIC3_MIN_BOND_DEPTH 256
#define HINIC3_MAX_BOND_DEPTH 8192
#define HINIC3_DEFAULT_BOND_DEPTH_OVS 1024
#define HINIC3_DEFAULT_BOND_DEPTH 2048
#define HINIC3_MIN_VPORT_DEPTH 256
#define HINIC3_MAX_VPORT_DEPTH 8192
#define HINIC3_DEFAULT_VPORT_DEPTH_OVS 512
#define HINIC3_DEFAULT_VPORT_DEPTH 1024
#define HINIC3_DEFAULT_DISK_USAGE 1024

static struct hinic3_init_arg g_agent_config_init_arg = {
    .offload_thread_num = HINIC3_OFFLOAD_THREAD_DEFAULT_NUM,
    .max_flow_num = HINIC3_MAX_FLOW_DEFAULT_NUM,
    .forward_cpu_mask.cpu_mask = {0},
    .forward_cpu_mask.cpu_mask_invalid_num = HINIC3_FORWARD_CPU_MASK_DEFAULT_NUM,
    .control_cpu_mask.cpu_mask = {0},
    .control_cpu_mask.cpu_mask_invalid_num = HINIC3_CONTROL_CPU_MASK_DEFAULT_NUM,
    .pf_pci_addr = {0},
    .max_queue_num = HINIC3_MAX_QUEUE_DEFAULT_NUM,
    .max_queue_num_limit = HINIC3_MAX_QUEUE_LIMIT_DEFAULT_NUM,
    .upcall_queue_num = HINIC3_UPCALL_QUEUE_DEFAULT_NUM,
    .mbuf_size = HINIC3_MBUF_DEFAULT_SIZE,
    .flow_max_idle = HINIC3_FLOW_MAX_IDLE_DEFAULT,
    .disk_usage = HINIC3_DEFAULT_DISK_USAGE,
    .enable_hugepage_meminfo_statistic = false,
    .virtual_queue_multiplex = 0,
    .packet_forward_mode = 0,
    .run_time_mode = "0x0101", // 没有配置运行模式的情况下，默认为comb模式
    .card_mode = 0, // 没有配置运行模式的情况下，默认为标准模式而非可编程模式
    .bond_hash_policy = 0, // 没有配置hash模式的情况下，默认为高效hash算法模式
    .enable_flexda_ovs_adapter = false,
    .pcap_cpu = 0,
    .qos_level = {0},
    .virtio_queue_depth = HINIC3_PORT_QUEUE_DEPTH_SIZE
};

static struct hinic3_parse_num_type g_num_table[] = {
    {
        AGENT_SECTION,
        AGENT_SECTION_OFFLOAD_THREAD_NUM_KEY,
        HINIC3_OFFLOAD_THREAD_NUM_MIN,
        HINIC3_OFFLOAD_THREAD_NUM_MAX,
        &g_agent_config_init_arg.offload_thread_num,
    },
    {
        AGENT_SECTION,
        AGENT_SECTION_MAX_FLOW_NUM_KEY,
        HINIC3_MAX_FLOW_NUM_MIN,
        HINIC3_MAX_FLOW_NUM_MAX,
        &g_agent_config_init_arg.max_flow_num,
    },
    {
        AGENT_SECTION,
        AGENT_SECTION_CARD_MODE,
        HINIC3_CARD_MODE_MIN,
        HINIC3_CARD_MODE_MAX,
        &g_agent_config_init_arg.card_mode,
    },
    {
        AGENT_SECTION,
        AGENT_SECTION_BOND_HASH_POLICY,
        HINIC3_BOND_HASH_POLICY_MIN,
        HINIC3_BOND_HASH_POLICY_MAX,
        &g_agent_config_init_arg.bond_hash_policy,
    },
    {
        PORT_SECTION,
        PORT_SECTION_MAX_QUEUE_NUM_KEY,
        HINIC3_MAX_QUEUE_NUM_MIN,
        HINIC3_MAX_QUEUE_NUM_MAX,
        &g_agent_config_init_arg.max_queue_num,
    },
    {
        PORT_SECTION,
        PORT_SECTION_UPCALL_QUEUE_NUM_KEY,
        HINIC3_UPCALL_QUEUE_NUM_MIN,
        HINIC3_UPCALL_QUEUE_NUM_MAX,
        &g_agent_config_init_arg.upcall_queue_num,
    },
    {
        PORT_SECTION,
        PORT_SECTION_MBUF_SIZE_KEY,
        0,
        UINT16_MAX,
        &g_agent_config_init_arg.mbuf_size,
    },
    {
        PORT_SECTION,
        PORT_SECTION_BOND_RX_DEPTH,
        HINIC3_MIN_BOND_DEPTH,
        HINIC3_MAX_BOND_DEPTH,
        &g_agent_config_init_arg.bond_rx_depth,
    },
    {
        PORT_SECTION,
        PORT_SECTION_BOND_TX_DEPTH,
        HINIC3_MIN_BOND_DEPTH,
        HINIC3_MAX_BOND_DEPTH,
        &g_agent_config_init_arg.bond_tx_depth,
    },
    {
        PORT_SECTION,
        PORT_SECTION_VPORT_RX_DEPTH,
        HINIC3_MIN_VPORT_DEPTH,
        HINIC3_MAX_VPORT_DEPTH,
        &g_agent_config_init_arg.vport_rx_depth,
    },
    {
        PORT_SECTION,
        PORT_SECTION_VPORT_TX_DEPTH,
        HINIC3_MIN_VPORT_DEPTH,
        HINIC3_MAX_VPORT_DEPTH,
        &g_agent_config_init_arg.vport_tx_depth,
    },
    {
        PORT_SECTION,
        PORT_SECTION_VIRTUAL_QUEUE_MULTIPLEX,
        HINIC3_VIRTUAL_QUEUE_MULTIPLEX_MIN,
        HINIC3_VIRTUAL_QUEUE_MULTIPLEX_MAX,
        &g_agent_config_init_arg.virtual_queue_multiplex,
    },
    {
        FLOW_SECTION,
        FLOW_SECTION_FLOW_MAX_IDLE_KEY,
        HINIC3_FLOW_MAX_IDLE_MIN,
        HINIC3_FLOW_MAX_IDLE_MAX,
        &g_agent_config_init_arg.flow_max_idle,
    },
    {
        AGENT_SECTION,
        FLOW_SECTION_AGENT_FORWARD_MOD_KEY,
        HINIC3_FORWARD_MODE_BANDWIDTH,
        HINIC3_FORWARD_MODE_LATENCY,
        &g_agent_config_init_arg.packet_forward_mode,
    },
};

static int hinic3_parse_virtio_queue_depth(struct rte_cfgfile *rte_file)
{
    char* end_ptr = NULL;
    uint16_t virtio_queue_depth  = 0;
    const char *queue_depth_str = rte_cfgfile_get_entry(rte_file, PORT_SECTION, PORT_VIRTIO_QUEUE_DEPTH);
    if (queue_depth_str == NULL) {
        HINIC3_LOG(INFO, AGENT, "%s is not set", PORT_VIRTIO_QUEUE_DEPTH);
        return 0;
    }

    if (!is_valid_digit(queue_depth_str))
    {
        HINIC3_LOG(ERR, AGENT, "%s is invalid!", PORT_VIRTIO_QUEUE_DEPTH);
        return -1;
    }

    unsigned long value = strtoul(queue_depth_str, &end_ptr, STR_TO_DEC_NUM);
    if (end_ptr == NULL || *end_ptr != '\0' || value > UINT16_MAX) {
        HINIC3_LOG(ERR, AGENT,
            "virtio_queue_depth value %s is invalid!\n", queue_depth_str);
        return -1;
    }

    virtio_queue_depth = (uint16_t)value;

    if (end_ptr == NULL || *end_ptr != '\0') {
        HINIC3_LOG(ERR, AGENT,
                    "virtio_queue_depth %u is invalid, use default value 1024!\n", virtio_queue_depth);
        return -1;
    }
    
    if (virtio_queue_depth < HINIC3_PORT_MIN_QUEUE_DEPTH ||
                        virtio_queue_depth > HINIC3_PORT_MAX_QUEUE_DEPTH) {
        HINIC3_LOG(INFO, AGENT,
            "virtio_queue_depth value %u is out of range!\n", virtio_queue_depth);
        return -1;
    }

    g_agent_config_init_arg.virtio_queue_depth = virtio_queue_depth;
    return 0;
}

static void hinic3_parse_config_num(struct rte_cfgfile *rte_file, uint32_t mod_id)
{
    uint32_t config_num;
    const char *config_num_str = NULL;
    char *end_ptr = NULL;
    config_num_str = rte_cfgfile_get_entry(rte_file, g_num_table[mod_id].section_name, g_num_table[mod_id].name);
    if (config_num_str == NULL) {
        HINIC3_LOG(INFO, AGENT, "%s not set, use default value %u.", g_num_table[mod_id].name,
            *g_num_table[mod_id].num);
        return;
    }

    unsigned long value = strtoul(config_num_str, &end_ptr, STR_TO_DEC_NUM);
    if (end_ptr == NULL || *end_ptr != '\0' || value > UINT32_MAX) {
        HINIC3_LOG(INFO, AGENT, "%s %s is invalid, use default value %u.", g_num_table[mod_id].name, config_num_str,
            *g_num_table[mod_id].num);
        return;
    }
    config_num = (uint32_t)value;
    if ((config_num < g_num_table[mod_id].min_num) || (config_num > g_num_table[mod_id].max_num)) {
        HINIC3_LOG(INFO, AGENT, "%s %u is invalid, use default value %u.", g_num_table[mod_id].name, config_num,
            *g_num_table[mod_id].num);
        return;
    }

    *g_num_table[mod_id].num = config_num;
    HINIC3_LOG(INFO, AGENT, "%s set, value %u.", g_num_table[mod_id].name, *g_num_table[mod_id].num);
}

static int hinic3_split_cpu_mask(const char *cpu_mask_type, uint32_t *cpu_mask_num, uint32_t *invalid_size)
{
    char *core_value = NULL;
    char *saveptr = NULL;
    char *endPtr = NULL;
    char delim[] = ",";
    char core_values[HINIC3F_MAX_CPU_MASK_LENGTH] = { 0 };
    int index = 0;
    uint32_t config_num;

    if (strlen(cpu_mask_type) >= sizeof(core_values)) {
        HINIC3_LOG(ERR, AGENT, "cpu_mask_type is too long.");
        return -1;
    }
    strncpy(core_values, cpu_mask_type, HINIC3F_MAX_CPU_MASK_LENGTH - 1);

    core_value = strtok_r(core_values, delim, &saveptr);
    while (core_value != NULL) {
        config_num = strtoul(core_value, &endPtr, STR_TO_DEC_NUM);
        if (endPtr == NULL || *endPtr != '\0' || index >=  HINIC3_CPU_MASK_NUM) {
            HINIC3_LOG(ERR, AGENT, "Failed to parse CPU index %d.", index);
            return -1;
        }
        cpu_mask_num[index] = config_num;
        core_value = strtok_r(NULL, delim, &saveptr);
        *invalid_size = ++index;
        if (*invalid_size > HINIC3_CPU_MASK_NUM) {
            *invalid_size = HINIC3_CPU_MASK_NUM;
            HINIC3_LOG(INFO, AGENT, "The num of cpu_mask is out of Range");
            return 0;
        }
    }

    return 0;
}

static int hinic3_prase_cpu_mask(struct rte_cfgfile *inifile, const char *name, struct hinic3_cpu_mask *cpu_mask_type)
{
    int ret = 0;
    long nprocessors;
    nprocessors = sysconf(_SC_NPROCESSORS_ONLN);
    if (nprocessors == -1) {
        HINIC3_LOG(ERR, AGENT, "Failed to get the number of processors.");
        return -1;
    }

    const char *config_num_str = NULL;
    config_num_str = rte_cfgfile_get_entry(inifile, AGENT_SECTION, name);
    if (config_num_str == NULL) {
        HINIC3_LOG(INFO, AGENT, "%s not set, use default value.", name);
        return 0;
    }

    ret = hinic3_split_cpu_mask(config_num_str, cpu_mask_type->cpu_mask, &cpu_mask_type->cpu_mask_invalid_num);
    if (ret != 0) {
        HINIC3_LOG(INFO, AGENT, "%s not set, use default value.", name);
        return 0;
    }

    for (uint32_t index = 0; index < cpu_mask_type->cpu_mask_invalid_num; index++) {
        if (cpu_mask_type->cpu_mask[index] >= nprocessors) {
            HINIC3_LOG(ERR, AGENT, "Illegal CPU index %u.", cpu_mask_type->cpu_mask[index]);
            return -1;
        }
    }
    return 0;
}

static int hinic3_init_cpu_mask(struct rte_cfgfile *inifile)
{
    int ret = 0;
    ret = hinic3_prase_cpu_mask(inifile, AGENT_SECTION_FORWARD_CPU_MASK_KEY, &g_agent_config_init_arg.forward_cpu_mask);
    if (ret != 0) {
        return -1;
    }

    ret = hinic3_prase_cpu_mask(inifile, AGENT_SECTION_CONTROL_CPU_MASK_KEY, &g_agent_config_init_arg.control_cpu_mask);
    if (ret != 0) {
        return -1;
    }

    return 0;
}

static int hinic3_parse_pf_pci_addr(struct rte_cfgfile *inifile)
{
    const char *pci_addr = rte_cfgfile_get_entry(inifile, AGENT_SECTION, AGENT_SECTION_PF_PCI_ADDR_KEY);
    if (pci_addr == NULL || strlen(pci_addr) > HINIC3_PCI_MAX_LEN) {
        memset(g_agent_config_init_arg.pf_pci_addr, '\0', HINIC3_PCI_MAX_LEN);
        HINIC3_LOG(INFO, AGENT, "pf_pci_addr is NULL");
        return 0;
    }

    if ((strncmp(pci_addr, HINIC3_AGENT_PCI_NULL, strlen(pci_addr) + 1) == 0)) {
        memset(g_agent_config_init_arg.pf_pci_addr, '\0', HINIC3_PCI_MAX_LEN);
        HINIC3_LOG(INFO, AGENT, "pf_pci_addr is NULL");
        return 0;
    }

    if (strlen(pci_addr) != PCI_ADDR_LEN) {
        HINIC3_LOG(ERR, AGENT, "the pci address length is %lu!", strlen(pci_addr));
        return -1;
    }
    strcpy(g_agent_config_init_arg.pf_pci_addr, pci_addr);
    return 0;
}

static int hinic3_parse_run_time_mode(struct rte_cfgfile *inifile)
{
    const char *run_time_mode_str = rte_cfgfile_get_entry(inifile, AGENT_SECTION, AGENT_SECTION_RUN_TIME_MODE);
    if (run_time_mode_str == NULL || strlen(run_time_mode_str) >= RUN_TIME_MODE_MAX_LENGTH) {
        memset(g_agent_config_init_arg.run_time_mode, '\0', RUN_TIME_MODE_MAX_LENGTH);
        HINIC3_LOG(ERR, AGENT, "run time mode_str is NULL");
        return -1;
    }

    if ((strncmp(run_time_mode_str, HINIC3_AGENT_PCI_NULL, strlen(run_time_mode_str) + 1) == 0)) {
        memset(g_agent_config_init_arg.run_time_mode, '\0', RUN_TIME_MODE_MAX_LENGTH);
        HINIC3_LOG(ERR, AGENT, "run time mode_str is NULL");
        return -1;
    }

    strcpy(g_agent_config_init_arg.run_time_mode, run_time_mode_str);
    return 0;
}

static int hinic3_parse_vpda_feature(struct rte_cfgfile *inifile)
{
    char* end_ptr = NULL;
    uint64_t vpda_feature = 0;
    const char *vdpa_feature_str = rte_cfgfile_get_entry(inifile, PORT_SECTION, PORT_SECTION_VDPA_FEATURE_KEY);
    if (vdpa_feature_str == NULL) {
        HINIC3_LOG(INFO, AGENT, "%s is not set", PORT_SECTION_VDPA_FEATURE_KEY);
        return 0;
    }

    vpda_feature = strtoull(vdpa_feature_str, &end_ptr, STR_TO_HEX_BASE);
    if (!end_ptr || *end_ptr != '\0' || errno == ERANGE || vpda_feature == 0 ||
        vdpa_feature_str[0] == '-') {
        HINIC3_LOG(ERR, AGENT, "Failed to parse %s, please check agent_config", PORT_SECTION_VDPA_FEATURE_KEY);
        return -1;
    }

    g_agent_config_init_arg.vdpa_virtio_feature = vpda_feature;
    return 0;
}

static int hinic3_parse_disk_usage(struct rte_cfgfile *rte_file)
{
    uint32_t disk_usage_config;
    const char *disk_usage_str = NULL;
    char *end_ptr = NULL;
    uint64_t available_size;

    available_size = hinic3_get_partition_free_space_size();
    if (available_size < HINIC3_DISK_USAGE_MIN) {
        HINIC3_LOG(ERR, AGENT, "Disk space is not enough, available size is %lu, need %u", available_size,
            HINIC3_DISK_USAGE_MIN);
        return -1;
    }

    disk_usage_str = rte_cfgfile_get_entry(rte_file, AGENT_SECTION, AGENT_SECTION_DISK_USAGE_KEY);
    if (disk_usage_str == NULL) {
        g_agent_config_init_arg.disk_usage =
            available_size > HINIC3_DEFAULT_DISK_USAGE ? HINIC3_DEFAULT_DISK_USAGE : available_size;
        HINIC3_LOG(INFO, AGENT, "disk_usage not set, use value %u.", g_agent_config_init_arg.disk_usage);
        return 0;
    }

    unsigned long value = strtoul(disk_usage_str, &end_ptr, STR_TO_DEC_NUM);
    if (end_ptr == NULL || *end_ptr != '\0' || (value < HINIC3_DISK_USAGE_MIN) ||
        (value > HINIC3_DISK_USAGE_MAX)) {
        g_agent_config_init_arg.disk_usage =
            available_size > HINIC3_DEFAULT_DISK_USAGE ? HINIC3_DEFAULT_DISK_USAGE : available_size;
        HINIC3_LOG(INFO, AGENT, "disk_usage %lu is invalid, use value %u.", value,
            g_agent_config_init_arg.disk_usage);
        return 0;
    }
    disk_usage_config = (uint32_t)value;

    if (disk_usage_config > available_size) {
        g_agent_config_init_arg.disk_usage = available_size;
        HINIC3_LOG(WARNING, AGENT,
            "The partition does not have enough space left, use available size %lu as disk_usage.\n", available_size);
        return 0;
    }

    g_agent_config_init_arg.disk_usage = disk_usage_config;

    HINIC3_LOG(INFO, AGENT, "disk_usage set, value %u.", disk_usage_config);
    return 0;
}

static int hinic3_parse_pcap_cpu_config(struct rte_cfgfile *rte_file)
{
    char *end_ptr = NULL;
    const char *pcap_cpu_str = NULL;
    uint32_t pcap_cpu = 0;
    long nprocessors;

    nprocessors = sysconf(_SC_NPROCESSORS_ONLN);
    if (nprocessors == -1) {
        HINIC3_LOG(ERR, AGENT, "Failed to get the number of processors.");
        return -1;
    }

    pcap_cpu_str = rte_cfgfile_get_entry(rte_file, AGENT_SECTION, AGENT_SECTION_PCAP_CPU_KEY);
    if (pcap_cpu_str == NULL) {
        HINIC3_LOG(INFO, AGENT, "%s not set, use default value.", AGENT_SECTION_PCAP_CPU_KEY);
        return 0;
    }

    unsigned long value = strtoul(pcap_cpu_str, &end_ptr, STR_TO_DEC_NUM);
    if (end_ptr == NULL || *end_ptr != '\0' || (value > HINIC3_PCAP_CPU_MAX)) {
        HINIC3_LOG(INFO, AGENT, "pcap_cpu value %lu is invalid or out of range, use default value.", value);
        return 0;
    }
    pcap_cpu = (uint32_t)value;

    if (pcap_cpu >= nprocessors) {
        HINIC3_LOG(INFO, AGENT, "Ilegal cpu core id %u for pcap_cpu, use default value.", pcap_cpu);
        return 0;
    }

    g_agent_config_init_arg.pcap_cpu = pcap_cpu;
    HINIC3_LOG(INFO, AGENT, "pcap_cpu set, value %u.", pcap_cpu);
    return 0;
}

struct hinic3_init_arg *hinic3_get_init_arg(void)
{
    return &g_agent_config_init_arg;
}

static int hinic3_set_config_value(void)
{
    int ret;
    struct hinic3_init_arg *arg = NULL;
    ret = hinic3_get_fixed_config(&arg, g_agent_config_init_arg.run_time_mode);
    if (ret != 0){
        HINIC3_LOG(ERR, AGENT, "hinic3 get fixed config fail with %d", ret);
        return ret;
    }
    g_agent_config_init_arg.user_scenario = arg->user_scenario;
    g_agent_config_init_arg.hot_migration = arg->hot_migration;
    g_agent_config_init_arg.is_dpu = arg->is_dpu;
    g_agent_config_init_arg.security_filter = arg->security_filter;
    g_agent_config_init_arg.offload_policy = arg->offload_policy;
    g_agent_config_init_arg.masked_to_exact = arg->masked_to_exact;
    g_agent_config_init_arg.acl_flow = arg->acl_flow;
    g_agent_config_init_arg.dp_hash_flow = arg->dp_hash_flow;
    g_agent_config_init_arg.fuzzy_flow = arg->fuzzy_flow;
    g_agent_config_init_arg.fuzzy_flow_flexda = arg->fuzzy_flow_flexda;
    g_agent_config_init_arg.fuzzy_flow_l3_forward = arg->fuzzy_flow_l3_forward;
    g_agent_config_init_arg.hardware_flow_age = arg->hardware_flow_age;
    g_agent_config_init_arg.con_track = arg->con_track;
    g_agent_config_init_arg.support_sample = arg->support_sample;
    g_agent_config_init_arg.support_sample_pkt_cutoff = arg->support_sample_pkt_cutoff;
    g_agent_config_init_arg.support_sample_ratio = arg->support_sample_ratio;
    g_agent_config_init_arg.status_packet_upcall = arg->status_packet_upcall;
    g_agent_config_init_arg.support_virtio_queue = arg->support_virtio_queue;
    g_agent_config_init_arg.forward_mode = arg->forward_mode;
    g_agent_config_init_arg.hiovs_mode = arg->hiovs_mode;
    g_agent_config_init_arg.max_flow_num_limit = arg->max_flow_num_limit;
    g_agent_config_init_arg.max_queue_num_limit = arg->max_queue_num_limit;
    g_agent_config_init_arg.vdpa_virtio_feature = arg->vdpa_virtio_feature;
    g_agent_config_init_arg.support_flow_qos = arg->support_flow_qos;
    g_agent_config_init_arg.support_multi_qos = arg->support_multi_qos;
    g_agent_config_init_arg.query_bdf_type = arg->query_bdf_type;
    g_agent_config_init_arg.support_port_hot_plug = arg->support_port_hot_plug;
    g_agent_config_init_arg.support_pf_port = arg->support_pf_port;
    g_agent_config_init_arg.support_vf_port = arg->support_vf_port;
    g_agent_config_init_arg.virtio_queue_depth = arg->virtio_queue_depth;
    g_agent_config_init_arg.is_virtio_queue_depth_set = arg->is_virtio_queue_depth_set;
    g_agent_config_init_arg.is_preload_pf_port = arg->is_preload_pf_port;
    g_agent_config_init_arg.is_preload_vf_port = arg->is_preload_vf_port;
    g_agent_config_init_arg.vf_del_no_driver_check = arg->vf_del_no_driver_check;
    g_agent_config_init_arg.support_bond_detect = arg->support_bond_detect;
    g_agent_config_init_arg.support_hardware_flow_age = arg->support_hardware_flow_age;
    g_agent_config_init_arg.support_vlan_tci = arg->support_vlan_tci;
    g_agent_config_init_arg.support_payload_capture = arg->support_payload_capture;
    g_agent_config_init_arg.card_mode = arg->card_mode;
    return 0;
}

static void hinic3_parse_meminfo_feature(struct rte_cfgfile *inifile)
{
    const char *stage = rte_cfgfile_get_entry(inifile, AGENT_SECTION, MEMINFO_SECTION_ENABLE_HUGEPAGE_MEMINFO);
    if (stage == NULL) {
        HINIC3_LOG(INFO, AGENT, "%s is not set, use default value enable", MEMINFO_SECTION_ENABLE_HUGEPAGE_MEMINFO);
        return;
    }

    if (strcmp(stage, "enable") == 0) {
        g_agent_config_init_arg.enable_hugepage_meminfo_statistic = true;
    } else if (strcmp(stage, "disable") == 0) {
        g_agent_config_init_arg.enable_hugepage_meminfo_statistic = false;
    } else {
        HINIC3_LOG(WARNING, AGENT, "%s parse wrong value, use default value enable",
            MEMINFO_SECTION_ENABLE_HUGEPAGE_MEMINFO);
    }
}

static void hinic3_check_config(void)
{
    if (g_agent_config_init_arg.offload_thread_num == HINIC3_OFFLOAD_THREAD_INVALID_NUM) {
        HINIC3_LOG(INFO, AGENT, "illegal %s %u, use default value %u.", AGENT_SECTION_OFFLOAD_THREAD_NUM_KEY,
            g_agent_config_init_arg.offload_thread_num, HINIC3_OFFLOAD_THREAD_DEFAULT_NUM);
        g_agent_config_init_arg.offload_thread_num = HINIC3_OFFLOAD_THREAD_DEFAULT_NUM;
    }

    if (g_agent_config_init_arg.max_flow_num == HINIC3_MAX_FLOW_NUM_INVALID ||
        g_agent_config_init_arg.max_flow_num > hinic3_max_flow_num_limit_get()) {
        HINIC3_LOG(INFO, AGENT, "illegal %s %u, use default value %u.", AGENT_SECTION_MAX_FLOW_NUM_KEY,
            g_agent_config_init_arg.max_flow_num, HINIC3_MAX_FLOW_DEFAULT_NUM);
        g_agent_config_init_arg.max_flow_num = HINIC3_MAX_FLOW_DEFAULT_NUM;
    }

    if (g_agent_config_init_arg.max_queue_num > hinic3_max_queue_num_limit_get()) {
        HINIC3_LOG(INFO, AGENT, "illegal %s %u, use default value %u.", PORT_SECTION_MAX_QUEUE_NUM_KEY,
            g_agent_config_init_arg.max_queue_num, HINIC3_MAX_QUEUE_DEFAULT_NUM);
        g_agent_config_init_arg.max_queue_num = HINIC3_MAX_QUEUE_DEFAULT_NUM;
    }
}

static int hinic3_parse_value_from_cfg(struct rte_cfgfile *inifile)
{
    int ret;
    const char *qos_level = NULL;

    for (uint32_t index = 0; index < sizeof(g_num_table) / sizeof(struct hinic3_parse_num_type); index++) {
        hinic3_parse_config_num(inifile, index);
    }

    ret = hinic3_set_config_value();
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "hinic3 set config value fail with %d", ret);
        return -1;
    }

    hinic3_check_config();

    // 在hinic3_get_agent_value_from_config中解析参数会有超大函数门禁不过问题，所以在这里解析
    ret = hinic3_parse_virtio_queue_depth(inifile);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "hinic3 virtio port_queue_depth fail with %d", ret);
        return -1;
    }
    /* 如果不支持多级QoS，则不读取qos_level */
    if (hinic3_support_multi_qos_get() == false) {
        HINIC3_LOG(INFO, AGENT, "Hwoff support multi qos get is false.");
        return 0;
    }

    qos_level = rte_cfgfile_get_entry(inifile, QOS_SECTION, QOS_SECTION_QOS_LEVEL_KEY);
    if (qos_level == NULL) {
        HINIC3_LOG(ERR, AGENT, "Multi qos: qos level not find.");
        return -1;
    }

    if (strlen(qos_level) > QOS_LEVEL_MAX_LENGTH) {
        HINIC3_LOG(ERR, AGENT, "qos_level is too long");
        return -1;
    }

    strcpy(g_agent_config_init_arg.qos_level, qos_level);
    return 0;
}

int hinic3_get_agent_value_from_config(void)
{
    int ret;
    struct rte_cfgfile *inifile = NULL;

    ret = hinic3_is_softlink(AGENT_CFG_FILE);
    if (ret != 0) {
        return -1;
    }

    inifile = hinic3_cfgfile_load(AGENT_CFG_FILE);
    if (inifile == NULL) {
        HINIC3_LOG(ERR, AGENT, "Faild to load agent config file: %s", AGENT_CFG_FILE);
        return -1;
    }

    ret = hinic3_parse_run_time_mode(inifile);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "hinic3 run_time_mode init fail with %d", ret);
        goto out;
    }
    hinic3_init_log_prefix(g_agent_config_init_arg.run_time_mode);

    ret = hinic3_parse_value_from_cfg(inifile);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "Failed to parse value from config file.");
        goto out;
    }

    ret = hinic3_init_cpu_mask(inifile);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "hinic3 cpu_mask init fail with %d", ret);
        goto out;
    }

    ret = hinic3_parse_pf_pci_addr(inifile);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "hinic3 pf_pci_addr init fail with %d", ret);
        goto out;
    }

    ret = hinic3_parse_vpda_feature(inifile);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "hinic3 vdpa_virtio_feature init fail with %d", ret);
        goto out;
    }

    ret = hinic3_parse_disk_usage(inifile);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "hinic3 disk_usage init fail with %d", ret);
        goto out;
    }

    hinic3_parse_meminfo_feature(inifile);

    if (hinic3_check_masked_to_exact_switch() == true) {
        if (hinic3_parse_pcap_cpu_config(inifile) != 0) {
            HINIC3_LOG(ERR, AGENT, "hinic3 pcap_cpu init fail with %d", ret);
            goto out;
        }
    }
    hinic3_set_virtual_queue_mode_enabled();
    rte_cfgfile_close(inifile);
    return 0;

out:
    rte_cfgfile_close(inifile);
    return -1;
}

bool hinic3_get_enable_hugepage_meminfo_statistic(void)
{
    return g_agent_config_init_arg.enable_hugepage_meminfo_statistic;
}

bool hinic3_get_enable_flexda_ovs_adapter(void)
{
    return g_agent_config_init_arg.enable_flexda_ovs_adapter;
}