/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Huawei Technologies Co., Ltd
 */

#include <netinet/in.h>
#include <string.h>
#include "hinic3_eth_packets.h"
#include "hinic3_log.h"
#include "hinic3_meminfo.h"
#include "hinic3_ds.h"
#include "hinic3_packets.h"

void hinic3_ipv6_format_addr(const struct in6_addr *addr, struct ds *s)
{
    char buf[INET6_ADDRSTRLEN];
    if (inet_ntop(AF_INET6, addr, buf, INET6_ADDRSTRLEN) == NULL) {
        HINIC3_LOG(ERR, AGENT, "The inet ntop failed!");
    }
    hinic3_ds_put_format(s, "%s", buf);
}
