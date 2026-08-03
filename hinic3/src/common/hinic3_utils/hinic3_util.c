/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */

#include "hinic3_util.h"
#include <sched.h>
#include <ctype.h>
#include <pthread.h>
#include "rte_common.h"
#include "rte_bus.h"
#include "hinic3_util.h"
#include "hinic3_log.h"
#include "hinic3_eth_util.h"
#include "rte_ethdev.h"
#include "hinic3_meminfo.h"
#include "hinic3_string_util.h"

#define HINIC3_HEX_PREFIX_LEN 2
static clockid_t g_monotonic_clock = CLOCK_MONOTONIC;

static struct hinic3_mac_transformat g_mac_format[] = {
    {HINIC3_MAC_COLON_FMT, MAC_SCAN_COLON_FMT},
    {HINIC3_MAC_HYPHEN_FMT, MAC_SCAN_HYPHEN_FMT},
    {HINIC3_MAC_HYPHEN_SHORT_FMT, MAC_SCAN_HYPHEN_SHORT_FMT},
};

void* hinic3_xmalloc(size_t size, enum hinic3_module module_id)
{
    void *p = hinic3_malloc(size ? size : 1, module_id);

    return p;
}

char* hinic3_xmemdup0(const char *p_, size_t length, enum hinic3_module module_id)
{
    char *p = NULL;

    p = hinic3_xmalloc(length + 1, module_id);
    if (p == NULL) {
        return NULL;
    }

    memcpy(p, p_, length);
    p[length] = '\0';

    return p;
}

char* hinic3_xstrdup(const char *s, enum hinic3_module module_id)
{
    return hinic3_xmemdup0(s, strlen(s), module_id);
}

char *hinic3_strdup(const char *target_str, enum hinic3_module module_id)
{
    size_t size;
    char *new_str = NULL;

    if (target_str == NULL) {
        return NULL;
    }

    size = strlen(target_str) + 1;
    new_str = hinic3_calloc(1, size, module_id);
    if (new_str != NULL) {
        memcpy(new_str, target_str, size);
    }

    return new_str;
}

char* hinic3_xvasprintf(const char *format, va_list args, enum hinic3_module module_id)
{
#define MAX_BUF_SIZE 256
    char str[MAX_BUF_SIZE + 1];
    va_list args2;
    size_t needed;
    char *s;
    int ret;

    va_copy(args2, args);

    ret = vsnprintf(str, MAX_BUF_SIZE, format, args);
    if (ret < 0) {
        va_end(args2);
        return NULL;
    }

    needed = (size_t)ret;
    s = hinic3_xmalloc(needed + 1, module_id);
    if (s == NULL) {
        va_end(args2);
        return NULL;
    }

    ret = vsnprintf(s, needed + 1, format, args2);
    if (ret <= 0) {
        va_end(args2);
        hinic3_free(s);
        return NULL;
    }

    va_end(args2);

    return s;
}

/*
 * 功能描述: 将mac地址转换成字符串
 */
int smacs_stringfy(const struct eth_address *macs, int n, char *str_macs, int str_macs_len)
{
    int off = 0;
    int suc_len;

    if (!str_macs || !macs) {
        return -EINVAL;
    }

    for (int i = 0; (i < n) && (str_macs_len - off - 1 > 0); i++) {
        suc_len = snprintf(str_macs + off, str_macs_len - off, MAC_FMT, MAC_ARGS(macs[i].ea));
        if (suc_len < 0 || (suc_len > str_macs_len - off - 1)) {
            HINIC3_LOG(ERR, AGENT, "Failed to snprintf macs, the suc_len is %d!", suc_len);
            return -EINVAL;
        }
        off += suc_len;
    }
    /* 删除最后一个',' */
    if (off != 0) {
        str_macs[off - 1] = '\0';
    }

    return 0;
}


/*
 * 功能描述: 将16进制mac转换成字符串
 */
int extra_eth_types_stringfy(const uint16_t types[], int n, char *str_types, int str_types_len)
{
    int off = 0;
    int suc_len;

    if (!str_types || !types) {
        return -EINVAL;
    }

    for (int i = 0; (i < n) && (str_types_len - off - 1 > 0); i++) {
        suc_len = snprintf(str_types + off, str_types_len - off, "%04x, ", types[i]);
        if (suc_len < 0 || (suc_len > str_types_len - off - 1)) {
            HINIC3_LOG(ERR, AGENT, "Failed to snprintf eth type, the suc_len is %d!", suc_len);
            return -EINVAL;
        }
        off += suc_len;
    }
    /* 删除最后一个',' */
    if (off != 0) {
        str_types[off - HINIC3_HEX_PREFIX_LEN] = '\0';
    }

    return 0;
}

bool is_valid_digit(const char *digit_str)
{
    size_t len;

    if (digit_str == NULL) {
        return false;
    }

    len = strlen(digit_str);
    if (len == 0) {
        return false;
    }

    for (size_t i = 0; i < len; i++) {
        if (!isdigit(digit_str[i])) {
            return false;
        }
    }
    return true;
}

/*
 * 功能描述: 将eth_type字符串转成数字
 */
int parse_extra_eth_types_str(const char *str_types, uint16_t types[], int *n)
{
    char *arg = NULL;
    char *next = NULL;
    char *str_type = NULL;
    int i;
    int cnt = 0;
    unsigned long type;

    if (!str_types || !types) {
        HINIC3_LOG(ERR, AGENT, "Parse eth type failed :empty list!");
        return -EINVAL;
    }

    arg = hinic3_strdup(str_types, HINIC3_COMMON_RESOURCE);
    if (!arg) {
        HINIC3_LOG(ERR, AGENT, "Alloc memory error!");
        return -ENOMEM;
    }

    next = arg;
    for (i = 0; i < BUM_ETYPE_MAX_COUNT; i++) {
        char *endp = NULL;

        str_type = strsep(&next, ",");
        if (str_type == NULL) {
            break;
        }
        type = strtoul(str_type, &endp, STR_TO_HEX_BASE);
        if ((type > USHRT_MAX) ||
            (type == 0 && str_type == endp) || *endp != '\0') {
            HINIC3_LOG(ERR, AGENT, "Ethernet type is invalid, index is %d!", i);
            hinic3_free(arg);
            return -EINVAL;
        }
        types[i] = (uint16_t)type;
        ++cnt;
    }
    if (next != NULL) {
        HINIC3_LOG(ERR, AGENT, "Ethernet type list is invalid or too many ethernet type codes!");
        hinic3_free(arg);
        return -EINVAL;
    }

    *n = cnt;
    hinic3_free(arg);
    return 0;
}

/*
 * 功能描述: 解析源mac地址
 */
int parse_src_to_src_macs(const char *str_macs, struct eth_addr macs[], int *n)
{
    char *arg = NULL;
    char *next = NULL;
    char *str_eth = NULL;
    int i;
    int ret;
    int cnt = 0;

    if (!str_macs || !macs) {
        return -EINVAL;
    }

    arg = hinic3_strdup(str_macs, HINIC3_COMMON_RESOURCE);
    if (!arg) {
        HINIC3_LOG(ERR, AGENT, "Alloc memory error!");
        return -ENOMEM;
    }

    next = arg;
    for (i = 0; i < BUM_SMAC_MAX_COUNT; i++) {
        str_eth = strsep(&next, ",");
        if (str_eth == NULL) {
            break;
        }
        ret = parse_mac(str_eth, (struct eth_address *)&macs[i]);
        if (ret != 0) {
            HINIC3_LOG(ERR, AGENT, "Ethernet MAC address is invalid, index is %d!", i);
            hinic3_free(arg);
            return ret;
        }
        ++cnt;
    }

    if (next != NULL) {
        HINIC3_LOG(ERR, AGENT, "SMAC-list is invalid or too many ethernet MAC address!");
        hinic3_free(arg);
        return -EINVAL;
    }

    *n = cnt;
    hinic3_free(arg);
    return 0;
}

/*
 * 功能描述: 将bdf数字转换成字符串
 */
int pci_addr_format(const struct rte_pci_addr *pci_addr, char *buf, int buf_len)
{
    int len = snprintf(buf, buf_len, PCI_PRI_FMT,
        pci_addr->domain, pci_addr->bus, pci_addr->devid, pci_addr->function);
    if (len <= 0) {
        HINIC3_LOG(ERR, AGENT, "Snprintf pci to bdf error!");
        return -1;
    }
    return 0;
}

static inline int char_is_valid_hex(const char input)
{
    if ((input >= '0' && input <= '9') ||
        (input >= 'a' && input <= 'f') ||
        (input >= 'A' && input <= 'F')) {
        return 0;
    }
    return -EINVAL;
}

int check_valid_mac(const char *mac, enum hinic3_mac_fmt fmt)
{
    bool is_ready_for_separator = false;
    int separator_count = 0;
    int hex_count_between_separator = 0;
    const char *p = mac;
    int separator_max;
    int char_num_between_separator;
    char separator;

    switch (fmt) {
        case HINIC3_MAC_COLON_FMT:
            /* xx:xx:xx:xx:xx:xx */
            separator_max = HINIC3_MAC_COLON_FMT_SEPARATOR_NUM;
            char_num_between_separator = HINIC3_MAC_COLON_FMT_CHAR_NUM_BETWEEN_SEPARATOR;
            separator = ':';
            break;
        case HINIC3_MAC_HYPHEN_FMT:
            /* xx-xx-xx-xx-xx-xx */
            separator_max = HINIC3_MAC_HYPHEN_FMT_SEPARATOR_NUM;
            char_num_between_separator = HINIC3_MAC_HYPHEN_FMT_CHAR_NUM_BETWEEN_SEPARATOR;
            separator = '-';
            break;
        case HINIC3_MAC_HYPHEN_SHORT_FMT:
            /* xxxx-xxxx-xxxx */
            separator_max = HINIC3_MAC_HEPHEN_SHORT_FMT_SEPARATOR_NUM;
            char_num_between_separator = HINIC3_MAC_HEPHEN_SHORT_FMT_CHAR_NUM_BETWEEN_SEPARATOR;
            separator = '-';
            break;
        default:
            return -1;
    }

    while (*p != '\0') {
        if (is_ready_for_separator && (*p == separator)) {
            is_ready_for_separator = false;
            hex_count_between_separator = 0;
            separator_count++;
            if (separator_count > separator_max) {
                return -EINVAL;
            }
            p++;
            continue;
        }

        hex_count_between_separator++;
        if (hex_count_between_separator > char_num_between_separator) {
            return -EINVAL;
        }

        if (char_is_valid_hex(*p) != 0) {
            return -EINVAL;
        }

        is_ready_for_separator = true;
        p++;
    }
    return 0;
}

int check_valid_eth_type(const char *eth_type)
{
    int count = 0;
    const char *p = eth_type;
    size_t len = strlen(eth_type);
    if (len < HINIC3_HEX_PREFIX_LEN || eth_type[0] != '0' || eth_type[1] != 'x') {
        return -1;
    } else {
        p += HINIC3_HEX_PREFIX_LEN;
    }

    while (*p != '\0') {
        count++;
        if (count > (HINIC3_ETH_TYPE_LEN + HINIC3_ETH_TYPE_LEN)) {
            return -1;
        }
        if (char_is_valid_hex(*p) != 0) {
            return -1;
        }
        p++;
    }
    return 0;
}

int parse_mac(const char *mac, struct eth_address *output_mac)
{
    int ret = 0;
    size_t i;
    int success_len = 0;
    struct eth_address eth_addr = { 0 };

    for (i = 0; i < ARRAY_SIZE(g_mac_format); i++) {
        ret = check_valid_mac(mac, g_mac_format[i].mac_fmt);
        if (ret == 0 && g_mac_format[i].mac_fmt == HINIC3_MAC_HYPHEN_FMT) {
            success_len = sscanf(mac, "%hhx-%hhx-%hhx-%hhx-%hhx-%hhx", &(*output_mac).ea[0], &(*output_mac).ea[1], &(*output_mac).ea[2], &(*output_mac).ea[3], &(*output_mac).ea[4], &(*output_mac).ea[5]);
            break;
        } else if (ret == 0 && g_mac_format[i].mac_fmt == HINIC3_MAC_COLON_FMT) {
            success_len = sscanf(mac, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &(*output_mac).ea[0], &(*output_mac).ea[1], &(*output_mac).ea[2], &(*output_mac).ea[3], &(*output_mac).ea[4], &(*output_mac).ea[5]);
            break;
        } else if (ret == 0 && g_mac_format[i].mac_fmt == HINIC3_MAC_HYPHEN_SHORT_FMT) {
            success_len = sscanf(mac, "%hx-%hx-%hx", (short unsigned int *)&(eth_addr).ea[0], (short unsigned int *)&(eth_addr).ea[2], (short unsigned int *)&(eth_addr).ea[4]);
            for (size_t j = 0; j < ARRAY_SIZE(output_mac->be16); j++) {
                eth_addr.be16[j] = htons(eth_addr.be16[j]);
            }
            memcpy((void *)(uintptr_t)output_mac->ea, eth_addr.ea, sizeof(uint8_t) * HINIC3_ETH_ADDR_LEN);
            break;
        }
    }

    if (success_len != HINIC3_ETH_ADDR_LEN) {
        if (success_len == ARRAY_SIZE(output_mac->be16) && g_mac_format[i].mac_fmt == HINIC3_MAC_HYPHEN_SHORT_FMT) {
            return 0;
        }
        return -EINVAL;
    }
    return 0;
}

long long int hinic3_time_usec(clockid_t clk)
{
    struct timespec ts;
    int ret;
    ret = clock_gettime(clk, &ts);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "Failed to get time by hinic3_time_uesc function, err is %d!", errno);
        return -1;
    }
    return (long long int)ts.tv_sec * SEC_TO_USEC_BASE + ts.tv_nsec / USEC_TO_NSEC_BASE;
}

long long int hinic3_time_msec(void)
{
    struct timespec ts;
    int ret;
    ret = clock_gettime(g_monotonic_clock, &ts);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "Failed to get time by hinic3_time_mesc function, err is %d!", errno);
        return -1;
    }
    return (long long int)ts.tv_sec * SEC_TO_MSEC_BASE + ts.tv_nsec / MSEC_TO_NSEC_BASE;
}

long long int hinic3_time_sec(void)
{
    struct timespec ts;
    int ret;
    ret = clock_gettime(g_monotonic_clock, &ts);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "Failed to get time by hinic3_time_mesc function, err is %d!", errno);
        return -1;
    }
    return (long long int)ts.tv_sec;
}

static int hinic3_set_cpu_affinity(pthread_t *thread, uint32_t core_list[], uint32_t count)
{
    uint32_t i;
    int ret;
    cpu_set_t cpuset;

    if (thread == NULL) {
        return -1;
    }

    CPU_ZERO(&cpuset);
    for (i = 0; i < count; i++) {
        CPU_SET(core_list[i], &cpuset);
    }

    ret = pthread_setaffinity_np(*thread, sizeof(cpuset), &cpuset);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "Failed to set thread affinity, err is %d!", ret);
        return -1;
    }
    return 0;
}

int hinic3_set_single_cpu_affinity(pthread_t *thread, uint32_t core)
{
    int ret;
    cpu_set_t cpuset;

    if (thread == NULL) {
        return -1;
    }

    CPU_ZERO(&cpuset);
    CPU_SET(core, &cpuset);
    ret = pthread_setaffinity_np(*thread, sizeof(cpuset), &cpuset);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "Failed to set thread affinity, err is %d!", ret);
        return -1;
    }
    return 0;
}

void hinic3_set_ctrl_thread_cpu_affinity(pthread_t *thread)
{
    struct hinic3_cpu_mask control_cpu_mask = hinic3_control_cpu_mask_get();
    struct hinic3_dp_extend_info *dp_info = hinic3_get_offload_extend_info();

    if (dp_info->control_num_in_env > 0) {
        (void)hinic3_set_cpu_affinity(thread, dp_info->control_thread_core, dp_info->control_num_in_env);
    } else {
        if (control_cpu_mask.cpu_mask_invalid_num > 0) {
            (void)hinic3_set_cpu_affinity(thread, control_cpu_mask.cpu_mask,
                                         control_cpu_mask.cpu_mask_invalid_num);
        }
    }
}

int hinic3_set_thread_name_by_str(const char *name)
{
    char thread_name[HINIC3_THREAD_NAME_MAX];
    size_t len = strnlen(name, sizeof(thread_name) - 1);
    memcpy(thread_name, name, len);

    thread_name[len] = '\0';
    return pthread_setname_np(pthread_self(), thread_name);
}
