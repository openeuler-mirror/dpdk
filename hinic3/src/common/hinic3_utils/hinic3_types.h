/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_TYPES_H
#define HINIC3_TYPES_H

#include <arpa/inet.h>
#include <stdint.h>
#include <netinet/in.h>

#ifdef __CHECKER__
#define HINIC3_BITWISE __attribute__((bitwise))
#define HINIC3_FORCE __attribute__((force))
#else
#define HINIC3_BITWISE
#define HINIC3_FORCE
#endif

typedef uint16_t HINIC3_BITWISE hinic3_be16;
typedef uint32_t HINIC3_BITWISE hinic3_be32;
typedef uint64_t HINIC3_BITWISE hinic3_be64;

typedef struct {
    hinic3_be16 hi, lo;
} hinic3_16aligned_be32;

union ct_addr {
    hinic3_be32 ipv4;
    struct in6_addr ipv6;
};

struct ct_endpoint {
    union ct_addr addr;
    union {
        hinic3_be16 port;
        struct {
            hinic3_be16 icmp_id;
            uint8_t icmp_type;
            uint8_t icmp_code;
        };
    };
};

struct conn_key {
    struct ct_endpoint src;
    struct ct_endpoint dst;
    hinic3_be16 dl_type;
    uint16_t zone;
    uint8_t nw_proto;
};

#endif /* HINIC3_TYPES_H */
