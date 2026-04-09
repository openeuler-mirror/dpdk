/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Huawei Technologies Co., Ltd
 */

#ifndef __HINIC3_PACKETS_H__
#define __HINIC3_PACKETS_H__

#include "hinic3_packets_types.h"
#include "hinic3_meminfo.h"
#include "hinic3_ds.h"

void hinic3_ipv6_format_addr(const struct in6_addr *addr, struct ds *s);

static inline uint16_t hinic3_vlan_tci_to_vid(hinic3_be16 vlan_tci)
{
    return (ntohs(vlan_tci) & VLAN_VID_MASK);
}

#endif
