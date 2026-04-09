/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_UTIL_H
#define HINIC3_UTIL_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>
#include <unistd.h>
#include "rte_pci.h"
#include "rte_bus_pci.h"
#include "rte_cycles.h"
#include "rte_config.h"
#include "hinic3_packets_types.h"
#include "hinic3_meminfo.h"
#include "hinic3_flow_agent_enum.h"
#include "hinic3_packets_types.h"

#define CPUSET_DIGITAL_BASE                 10
#define STR_TO_DEC_NUM                      10
#define STR_TO_HEX_BASE                     16
#define BUM_SMAC_MAX_COUNT                  16
#define MAC_STR_LEN                         18
#define BUM_ETYPE_MAX_COUNT                 64
#define ETH_TYPE_ALEN                       5
#define CACHE_LINE_SIZE                     64
#define ETC_MAXLINE                         1024
#define BASE_DECIMAL                        10
#define MAX_BYTE_DATA                       256
#define LONG_BIT_LONG                       64
#define HALF_CHAR                           4

#define PCI_ADDR_LEN                        12
#define SEC_TO_MSEC_BASE                    1000
#define MSEC_TO_NSEC_BASE                   1000000

#define MAC_FMT                             "%02x:%02x:%02x:%02x:%02x:%02x,"
#define MAC_ARGS(eth)                       (eth)[0], (eth)[1], (eth)[2], (eth)[3], (eth)[4], (eth)[5]

/** for bit ops */
#define BITS_PER_BYTE                       8

#define RTE_LCORE_INDEX_CURRENT             (rte_lcore_index(-1) < 0 ? 0 : rte_lcore_index(-1))
#define HINIC3_OVS_GROUP_NAME "dpak_ovs"

#define MULTI_2_UNIT(len)            ((len) << 1)
#define MULTI_4_UNIT(len)            ((len) << 2)
#define MULTI_8_UNIT(len)            ((len) << 3)
#define MULTI_16_UNIT(len)           ((len) << 4)
#define MULTI_32_UNIT(len)           ((len) << 5)

#define DIV_2_UNIT(len)              ((len) >> 1)
#define DIV_4_UNIT(len)              ((len) >> 2)
#define DIV_8_UNIT(len)              ((len) >> 3)
#define DIV_16_UNIT(len)             ((len) >> 4)
#define DIV_32_UNIT(len)             ((len) >> 5)

#define HINIC3_UNUSED                        __attribute__((__unused__))
#define HINIC3_WARN_UNUSED_RESULT            __attribute__((__warn_unused_result__))
#define HINIC3_LIKELY(CONDITION)             __builtin_expect(!!(CONDITION), 1)
#define HINIC3_UNLIKELY(CONDITION)           __builtin_expect(!!(CONDITION), 0)

#define HINIC3_PRIORITY_SYMBOL      101
#define HINIC3_PRIORITY_CONFIG      110
#define HINIC3_PRIORITY_MUTEX       120
#define HINIC3_PRIORITY_INIT        130
#define HINIC3_PRIORITY_DRIVER      140
#define HINIC3_PRIORITY_LAST        65535

#define DPU_MODE_DEVICE_ID_HINIC_PF 0x0224

#ifndef MIN
#define MIN(X, Y) ((X) < (Y) ? (X) : (Y))
#endif

#define ARRAY_SIZE_NOCHECK(ARRAY) (sizeof(ARRAY) / sizeof((ARRAY)[0]))

#define ARRAY_CHECK(ARRAY)    \
    !__builtin_types_compatible_p(typeof(ARRAY), typeof(&(ARRAY)[0]))

#define ARRAY_FAIL(ARRAY) (sizeof(char[-2*!ARRAY_CHECK(ARRAY)]))

#define ARRAY_SIZE(ARRAY) \
    __builtin_choose_expr(ARRAY_CHECK(ARRAY), \
        ARRAY_SIZE_NOCHECK(ARRAY), ARRAY_FAIL(ARRAY))

static inline int parse_str_to_value(const char *str, uint32_t *value)
{
    long long int val;
    char *end = NULL;

    val = strtoll(str, &end, STR_TO_DEC_NUM);
    if (!end || *end != '\0' || val > UINT_MAX || val < 0) {
        return -EINVAL;
    }

    *value = (uint32_t)val;
    return 0;
}

#ifndef __has_feature
  #define __has_feature(x) 0
#endif

#if __has_feature(c_thread_safety_attributes)
#define HINIC3_GUARDED                   __attribute__((guarded_var))
#define HINIC3_GUARDED_BY(...)           __attribute__((guarded_by(__VA_ARGS__)))
#define HINIC3_RELEASES(...)             __attribute__((unlock_function(__VA_ARGS__)))
#define HINIC3_EXCLUDED(...)             __attribute__((locks_excluded(__VA_ARGS__)))
#define HINIC3_ACQ_BEFORE(...)           __attribute__((acquired_before(__VA_ARGS__)))
#define HINIC3_ACQ_AFTER(...)            __attribute__((acquired_after(__VA_ARGS__)))
#define HINIC3_NO_THREAD_SAFETY_ANALYSIS \
    __attribute__((no_thread_safety_analysis))
#else /* not Clang */
#define HINIC3_LOCKABLE
#define HINIC3_REQ_RDLOCK(...)
#define HINIC3_ACQ_RDLOCK(...)
#define HINIC3_REQ_WRLOCK(...)
#define HINIC3_ACQ_WRLOCK(...)
#define HINIC3_REQUIRES(...)
#define HINIC3_ACQUIRES(...)
#define HINIC3_TRY_WRLOCK(...)
#define HINIC3_TRY_RDLOCK(...)
#define HINIC3_TRY_LOCK(...)
#define HINIC3_GUARDED
#define HINIC3_GUARDED_BY(...)
#define HINIC3_EXCLUDED(...)
#define HINIC3_RELEASES(...)
#define HINIC3_ACQ_BEFORE(...)
#define HINIC3_ACQ_AFTER(...)
#define HINIC3_NO_THREAD_SAFETY_ANALYSIS
#endif

#define HINIC3_OBJECT_OFFSETOF(OBJECT, MEMBER) offsetof(typeof(*(OBJECT)), MEMBER)

#define HINIC3_OBJECT_CONTAINING(POINTER, OBJECT, MEMBER)                      \
    ((typeof(OBJECT)) (void *)                                      \
     ((char *) (POINTER) - HINIC3_OBJECT_OFFSETOF(OBJECT, MEMBER)))

#define HINIC3_CONTAINER_ASSIGN(OBJECT, POINTER, MEMBER) \
    ((OBJECT) = HINIC3_OBJECT_CONTAINING(POINTER, OBJECT, MEMBER), (void) 0)

#define HINIC3_CONTAINER_INIT(OBJECT, POINTER, MEMBER) \
    ((OBJECT) = NULL, HINIC3_CONTAINER_ASSIGN(OBJECT, POINTER, MEMBER))

#define HINIC3_CONTAINER_OF(POINTER, STRUCT, MEMBER)                           \
        ((STRUCT *) (void *) ((char *) (POINTER) - offsetof (STRUCT, MEMBER)))

#define HINIC3_BUILD_ASSERT_TYPE(POINTER, TYPE) \
    ((void)sizeof((int)((POINTER) == (TYPE)(POINTER))))

#define HINIC3_CONST_CAST(TYPE, POINTER) \
    (HINIC3_BUILD_ASSERT_TYPE(POINTER, TYPE), (TYPE)(POINTER))

#define HINIC3_SOURCE_LOCATOR __FILE__ ":" HINIC3_STRINGIZE(__LINE__)
#define HINIC3_STRINGIZE(ARG) HINIC3_STRINGIZE2(ARG)
#define HINIC3_STRINGIZE2(ARG) #ARG

#define MAC_SCAN_HYPHEN_SHORT_FMT "%" SCNx16 "-%" SCNx16 "-%" SCNx16
#define MAC_SCAN_HYPHEN_FMT       "%" SCNx8 "-%" SCNx8 "-%" SCNx8 "-%" SCNx8 "-%" SCNx8 "-%" SCNx8
#define MAC_SCAN_COLON_FMT        "%" SCNx8 ":%" SCNx8 ":%" SCNx8 ":%" SCNx8 ":%" SCNx8 ":%" SCNx8
#define MAC_SCAN_ARGS(EA) &(EA).ea[0], &(EA).ea[1], &(EA).ea[2], &(EA).ea[3], &(EA).ea[4], &(EA).ea[5]
#define MAC_SCAN_HYPHEN_SHORT_ARGS(EA) &(EA).ea[0], &(EA).ea[2], &(EA).ea[4]

#ifndef HINIC3_MIN
#define HINIC3_MIN(X, Y) ((X) < (Y) ? (X) : (Y))
#endif

#define HINIC3_MAC_COLON_FMT_SEPARATOR_NUM 5
#define HINIC3_MAC_HYPHEN_FMT_SEPARATOR_NUM 5
#define HINIC3_MAC_HEPHEN_SHORT_FMT_SEPARATOR_NUM 2
#define HINIC3_MAC_COLON_FMT_CHAR_NUM_BETWEEN_SEPARATOR 2
#define HINIC3_MAC_HYPHEN_FMT_CHAR_NUM_BETWEEN_SEPARATOR 2
#define HINIC3_MAC_HEPHEN_SHORT_FMT_CHAR_NUM_BETWEEN_SEPARATOR 4

enum hinic3_mac_fmt {
    HINIC3_MAC_COLON_FMT = 0,
    HINIC3_MAC_HYPHEN_FMT,
    HINIC3_MAC_HYPHEN_SHORT_FMT,
    HINIC3_MAC_FMT_MAX,
};

struct hinic3_mac_transformat {
    enum hinic3_mac_fmt mac_fmt;
    const char *fmt_str;
};

int smacs_stringfy(const struct eth_address *macs, int n, char *str_macs, int str_macs_len);
int extra_eth_types_stringfy(const uint16_t types[], int n, char *str_types, int str_types_len);
int parse_src_to_src_macs(const char *str_macs, struct eth_addr macs[], int *n);
int parse_extra_eth_types_str(const char *str_types, uint16_t types[], int *n);
bool is_valid_digit(const char *digit_str);
void* hinic3_xmalloc(size_t size, enum hinic3_module module_id);
char* hinic3_xmemdup0(const char *p_, size_t length, enum hinic3_module module_id);
char* hinic3_xstrdup(const char *s, enum hinic3_module module_id);
char *hinic3_strdup(const char *target_str, enum hinic3_module module_id);
char* hinic3_xvasprintf(const char *format, va_list args, enum hinic3_module module_id);
int pci_addr_format(const struct rte_pci_addr *pci_addr, char *buf, int buf_len);

int parse_mac(const char *mac, struct eth_address *output_mac);
int check_valid_mac(const char *mac, enum hinic3_mac_fmt fmt);
int check_valid_eth_type(const char *eth_type);

long long int hinic3_time_msec(void);
long long int hinic3_time_sec(void);
void hinic3_set_ctrl_thread_cpu_affinity(pthread_t *thread);
int hinic3_set_single_cpu_affinity(pthread_t *thread, uint32_t core);
int hinic3_set_thread_name_by_str(const char *name);

#endif
