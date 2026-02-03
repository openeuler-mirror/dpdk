/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2019 Huawei Technologies Co., Ltd
 */

#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <rte_byteorder.h>
#include <rte_common.h>
#include <rte_ether.h>
#include <rte_ethdev.h>
#include <rte_malloc.h>
#include <rte_flow.h>
#include <rte_flow_driver.h>

#include "base/hinic3_compat.h"
#include "base/hinic3_pmd_hwdev.h"
#include "base/hinic3_pmd_mgmt.h"
#include "base/hinic3_pmd_nic_cfg.h"
#include "hinic3_pmd_ethdev.h"
#include "hinic3_pmd_fdir.h"
#include "hinic3_pmd_flow_sec.h"
#include "hinic3_pmd_flow.h"
#include "hinic3_pmd_rx.h"

#ifdef HINIC3_TRAFFIC_BIFUR
#ifdef DPDK_20_11
#include <rte_bitops.h>
#else
#define RTE_BIT32(nr) (UINT32_C(1) << (nr))

static inline uint32_t
rte_bit_relaxed_get32(unsigned int nr, volatile uint32_t *addr)
{
	RTE_ASSERT(nr < 32);

	uint32_t mask = UINT32_C(1) << nr;
	return (*addr) & mask;
}

static inline void
rte_bit_relaxed_set32(unsigned int nr, volatile uint32_t *addr)
{
	RTE_ASSERT(nr < 32);

	uint32_t mask = RTE_BIT32(nr);
	*addr = (*addr) | mask;
}

static inline void
rte_bit_relaxed_clear32(unsigned int nr, volatile uint32_t *addr)
{
	RTE_ASSERT(nr < 32);

	uint32_t mask = RTE_BIT32(nr);
	*addr = (*addr) & (~mask);
}
#endif
#include "hinic3_pmd_rx.h"
#include "base/hinic3_pmd_csr.h"
#include "hinic3_pmd_bifur.h"

#define HINIC3_QUEUE_MAX          16
#define HINIC3_QUEUE_ALLOW_NUM    32
#endif

#define HINIC3_UINT8_MAX          0xff

static enum rte_flow_item_type pattern_ipv4_icmp[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_ICMP,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv4_any[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_ANY,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ethertype[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ethertype_tcp[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_TCP,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ethertype_udp[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv4_vxlan[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_VXLAN,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv4_geneve[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_GENEVE,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv4_vxlan_any[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_VXLAN,
	HINIC3_FLOW_ITEM_TYPE_ANY,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv4_geneve_any[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_GENEVE,
	HINIC3_FLOW_ITEM_TYPE_ANY,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv4_vxlan_tcp[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_VXLAN,
	HINIC3_FLOW_ITEM_TYPE_TCP,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv4_geneve_tcp[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_GENEVE,
	HINIC3_FLOW_ITEM_TYPE_TCP,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv4_vxlan_udp[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_VXLAN,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv4_geneve_udp[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_GENEVE,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv4_vxlan_ipv4[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_VXLAN,
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv4_geneve_ipv4[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_GENEVE,
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv4_vxlan_ipv4_tcp[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_VXLAN,
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_TCP,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv4_geneve_ipv4_tcp[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_GENEVE,
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_TCP,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv4_vxlan_ipv4_udp[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_VXLAN,
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv4_geneve_ipv4_udp[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_GENEVE,
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv4_vxlan_ipv6[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_VXLAN,
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv4_geneve_ipv6[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_GENEVE,
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv4_vxlan_ipv6_tcp[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_VXLAN,
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_TCP,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv4_geneve_ipv6_tcp[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_GENEVE,
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_TCP,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv4_vxlan_ipv6_udp[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_VXLAN,
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv4_geneve_ipv6_udp[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_GENEVE,
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_END,
};
static enum rte_flow_item_type pattern_ipv4[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv4_udp[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv4_tcp[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_TCP,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv6[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv6_udp[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv6_tcp[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_TCP,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv6_vxlan[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_VXLAN,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv6_geneve[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_GENEVE,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv6_vxlan_any[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_VXLAN,
	HINIC3_FLOW_ITEM_TYPE_ANY,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv6_geneve_any[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_GENEVE,
	HINIC3_FLOW_ITEM_TYPE_ANY,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv6_vxlan_tcp[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_VXLAN,
	HINIC3_FLOW_ITEM_TYPE_TCP,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv6_geneve_tcp[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_GENEVE,
	HINIC3_FLOW_ITEM_TYPE_TCP,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv6_vxlan_udp[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_VXLAN,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_END,
};


static enum rte_flow_item_type pattern_ipv6_geneve_udp[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_GENEVE,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv4_gpe_eth[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_VXLAN_GPE,
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv4_gpe_eth_ipv4_tcp[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_VXLAN_GPE,
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_TCP,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv4_gpe_eth_ipv4_udp[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_VXLAN_GPE,
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv4_gpe_eth_ipv6[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_VXLAN_GPE,
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv4_gpe_eth_ipv6_tcp[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_VXLAN_GPE,
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_TCP,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv4_gpe_eth_ipv6_udp[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_VXLAN_GPE,
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv4_gpe_ipv6_udp[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_VXLAN_GPE,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv4_gpe_ipv6_tcp[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_VXLAN_GPE,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_TCP,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv4_gpe_ipv6[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_VXLAN_GPE,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv4_gpe_ipv4_tcp[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_VXLAN_GPE,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_TCP,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv4_gpe_ipv4_udp[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_VXLAN_GPE,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv4_gpe_ipv4[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_VXLAN_GPE,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv6_gpe_ipv4[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_VXLAN_GPE,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv6_gpe_ipv4_tcp[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_VXLAN_GPE,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_TCP,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv6_gpe_ipv4_udp[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_VXLAN_GPE,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv6_gpe_ipv6[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_VXLAN_GPE,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv6_gpe_ipv6_tcp[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_VXLAN_GPE,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_TCP,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv6_gpe_ipv6_udp[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_VXLAN_GPE,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv6_gpe_eth[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_VXLAN_GPE,
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv4_gpe_eth_ipv4[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_VXLAN_GPE,
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv6_gpe_eth_ipv4[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_VXLAN_GPE,
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv6_gpe_eth_ipv4_tcp[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_VXLAN_GPE,
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_TCP,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv6_gpe_eth_ipv4_udp[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_VXLAN_GPE,
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv6_gpe_eth_ipv6[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_VXLAN_GPE,
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv6_gpe_eth_ipv6_tcp[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_VXLAN_GPE,
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_TCP,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv6_gpe_eth_ipv6_udp[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_VXLAN_GPE,
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv6_gpe[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_VXLAN_GPE,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv4_gpe[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_VXLAN_GPE,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv4_ipv4[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv4_ipv4_udp[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv4_ipv4_tcp[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_TCP,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv4_ipv4_any[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_ANY,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv4_ipv6[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv4_ipv6_tcp[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_TCP,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv4_ipv6_udp[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv4_ipv6_any[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_ANY,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv6_ipv6[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv6_ipv6_tcp[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_TCP,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv6_ipv6_udp[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv6_ipv6_any[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_ANY,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv6_ipv4[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv6_ipv4_udp[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv6_ipv4_tcp[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_TCP,
	HINIC3_FLOW_ITEM_TYPE_END,
};

static enum rte_flow_item_type pattern_ipv6_ipv4_any[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_ANY,
	HINIC3_FLOW_ITEM_TYPE_END,
};

typedef int (*hinic3_parse_filter_t)(struct rte_eth_dev *	  dev,
				     const struct rte_flow_attr * attr,
				     const struct rte_flow_item	  pattern[],
				     const struct rte_flow_action actions[],
				     struct rte_flow_error *	  error,
				     struct hinic3_filter_t *	  filter);

struct hinic3_valid_pattern {
	enum rte_flow_item_type *items;
	hinic3_parse_filter_t parse_filter;
};

static int hinic3_flow_parse_fdir_filter(struct rte_eth_dev *	      dev,
					 const struct rte_flow_attr * attr,
					 const struct rte_flow_item   pattern[],
					 const struct rte_flow_action actions[],
					 struct rte_flow_error *      error,
					 struct hinic3_filter_t *     filter);

static int hinic3_flow_parse_ethertype_filter(
	struct rte_eth_dev *dev, const struct rte_flow_attr *attr,
	const struct rte_flow_item   pattern[],
	const struct rte_flow_action actions[], struct rte_flow_error *error,
	struct hinic3_filter_t *filter);

static int hinic3_flow_parse_fdir_vxlan_geneve_filter(
	struct rte_eth_dev *dev, const struct rte_flow_attr *attr,
	const struct rte_flow_item   pattern[],
	const struct rte_flow_action actions[], struct rte_flow_error *error,
	struct hinic3_filter_t *filter);

static int
hinic3_flow_parse_sec_fdir_filter(struct rte_eth_dev	  *dev,
			      const struct rte_flow_attr  *attr,
			      const struct rte_flow_item   pattern[],
			      const struct rte_flow_action actions[],
			      struct rte_flow_error	  *error,
			      struct hinic3_filter_t	  *filter);

static const struct hinic3_valid_pattern hinic3_supported_patterns[] = {
	/* support ethertype */
	{ pattern_ethertype, hinic3_flow_parse_ethertype_filter },
	/* support ipv4 but not tunnel, and any field can be masked  */
	{ pattern_ipv4, hinic3_flow_parse_fdir_filter },
	{ pattern_ipv4_any, hinic3_flow_parse_fdir_filter },
	/* support ipv4 + l4 but not tunnel, and any field can be masked  */
	{ pattern_ipv4_udp, hinic3_flow_parse_fdir_filter },
	{ pattern_ipv4_tcp, hinic3_flow_parse_fdir_filter },
	/* support ipv4 + icmp not tunnel, and any field can be masked  */
	{ pattern_ipv4_icmp, hinic3_flow_parse_fdir_filter },

	/* support ipv4 + l4 but not tunnel, and any field can be masked  */
	{ pattern_ethertype_udp, hinic3_flow_parse_fdir_filter },
	{ pattern_ethertype_tcp, hinic3_flow_parse_fdir_filter },

	/* support ipv4 + vxlan/geneve + any, and any field can be masked */
	{ pattern_ipv4_vxlan, hinic3_flow_parse_fdir_vxlan_geneve_filter },
	{ pattern_ipv4_geneve, hinic3_flow_parse_fdir_vxlan_geneve_filter },
	/* support ipv4 + vxlan/geneve + ipv4, and any field can be masked */
	{ pattern_ipv4_vxlan_ipv4, hinic3_flow_parse_fdir_vxlan_geneve_filter },
	{ pattern_ipv4_geneve_ipv4, hinic3_flow_parse_fdir_vxlan_geneve_filter },
	/* support ipv4 + vxlan/geneve + ipv4 + l4, and any field can be masked */
	{ pattern_ipv4_vxlan_ipv4_tcp, hinic3_flow_parse_fdir_vxlan_geneve_filter },
	{ pattern_ipv4_vxlan_ipv4_udp, hinic3_flow_parse_fdir_vxlan_geneve_filter },
	{ pattern_ipv4_geneve_ipv4_tcp, hinic3_flow_parse_fdir_vxlan_geneve_filter },
	{ pattern_ipv4_geneve_ipv4_udp, hinic3_flow_parse_fdir_vxlan_geneve_filter },
	/* support ipv4 + vxlan/geneve + ipv6, and any field can be masked */
	{ pattern_ipv4_vxlan_ipv6, hinic3_flow_parse_fdir_vxlan_geneve_filter },
	{ pattern_ipv4_geneve_ipv6, hinic3_flow_parse_fdir_vxlan_geneve_filter },
	/* support ipv4 + vxlan/geneve + ipv6 + l4, and any field can be masked */
	{ pattern_ipv4_vxlan_ipv6_tcp, hinic3_flow_parse_fdir_vxlan_geneve_filter },
	{ pattern_ipv4_vxlan_ipv6_udp, hinic3_flow_parse_fdir_vxlan_geneve_filter },
	{ pattern_ipv4_geneve_ipv6_tcp, hinic3_flow_parse_fdir_vxlan_geneve_filter },
	{ pattern_ipv4_geneve_ipv6_udp, hinic3_flow_parse_fdir_vxlan_geneve_filter },
	/* support ipv4 + vxlan/geneve + l4, and any field can be masked */
	{ pattern_ipv4_vxlan_tcp, hinic3_flow_parse_fdir_vxlan_geneve_filter },
	{ pattern_ipv4_vxlan_udp, hinic3_flow_parse_fdir_vxlan_geneve_filter },
	{ pattern_ipv4_vxlan_any, hinic3_flow_parse_fdir_vxlan_geneve_filter },
	{ pattern_ipv4_geneve_tcp, hinic3_flow_parse_fdir_vxlan_geneve_filter },
	{ pattern_ipv4_geneve_udp, hinic3_flow_parse_fdir_vxlan_geneve_filter },
	{ pattern_ipv4_geneve_any, hinic3_flow_parse_fdir_vxlan_geneve_filter },

	/* support ipv6 but not tunnel, and any field can be masked */
	{ pattern_ipv6, hinic3_flow_parse_fdir_filter },
	/* support ipv6 + l4 but not tunnel, and any field can be masked */
	{ pattern_ipv6_udp, hinic3_flow_parse_fdir_filter },
	{ pattern_ipv6_tcp, hinic3_flow_parse_fdir_filter },

	/* support ipv6 + vxlan/geneve + any, and any field can be masked */
	{ pattern_ipv6_vxlan, hinic3_flow_parse_fdir_vxlan_geneve_filter },
	{ pattern_ipv6_vxlan_any, hinic3_flow_parse_fdir_vxlan_geneve_filter },
	{ pattern_ipv6_geneve, hinic3_flow_parse_fdir_vxlan_geneve_filter },
	{ pattern_ipv6_geneve_any, hinic3_flow_parse_fdir_vxlan_geneve_filter },

	/* support ipv6 + vxlan/geneve + l4, and any field can be masked */
	{ pattern_ipv6_vxlan_tcp, hinic3_flow_parse_fdir_vxlan_geneve_filter },
	{ pattern_ipv6_vxlan_udp, hinic3_flow_parse_fdir_vxlan_geneve_filter },
	{ pattern_ipv6_geneve_tcp, hinic3_flow_parse_fdir_vxlan_geneve_filter },
	{ pattern_ipv6_geneve_udp, hinic3_flow_parse_fdir_vxlan_geneve_filter },

	/* support vxlan-gre */
	{ pattern_ipv4_gpe_eth, hinic3_flow_parse_fdir_vxlan_geneve_filter },
	{ pattern_ipv4_gpe_eth_ipv4, hinic3_flow_parse_fdir_vxlan_geneve_filter },
	{ pattern_ipv4_gpe_eth_ipv4_tcp, hinic3_flow_parse_fdir_vxlan_geneve_filter },
	{ pattern_ipv4_gpe_eth_ipv4_udp, hinic3_flow_parse_fdir_vxlan_geneve_filter },
	{ pattern_ipv4_gpe_eth_ipv6, hinic3_flow_parse_fdir_vxlan_geneve_filter },
	{ pattern_ipv4_gpe_eth_ipv6_tcp, hinic3_flow_parse_fdir_vxlan_geneve_filter },
	{ pattern_ipv4_gpe_eth_ipv6_udp, hinic3_flow_parse_fdir_vxlan_geneve_filter },
	{ pattern_ipv4_gpe_ipv6_udp, hinic3_flow_parse_fdir_vxlan_geneve_filter },
	{ pattern_ipv4_gpe_ipv6_tcp, hinic3_flow_parse_fdir_vxlan_geneve_filter },
	{ pattern_ipv4_gpe_ipv6, hinic3_flow_parse_fdir_vxlan_geneve_filter },
	{ pattern_ipv4_gpe_ipv4_tcp, hinic3_flow_parse_fdir_vxlan_geneve_filter },
	{ pattern_ipv4_gpe_ipv4_udp, hinic3_flow_parse_fdir_vxlan_geneve_filter },
	{ pattern_ipv4_gpe_ipv4, hinic3_flow_parse_fdir_vxlan_geneve_filter },
	{ pattern_ipv6_gpe_ipv4, hinic3_flow_parse_fdir_vxlan_geneve_filter },
	{ pattern_ipv6_gpe_ipv4_tcp, hinic3_flow_parse_fdir_vxlan_geneve_filter },
	{ pattern_ipv6_gpe_ipv4_udp, hinic3_flow_parse_fdir_vxlan_geneve_filter },
	{ pattern_ipv6_gpe_ipv6, hinic3_flow_parse_fdir_vxlan_geneve_filter },
	{ pattern_ipv6_gpe_ipv6_tcp, hinic3_flow_parse_fdir_vxlan_geneve_filter },
	{ pattern_ipv6_gpe_ipv6_udp, hinic3_flow_parse_fdir_vxlan_geneve_filter },
	{ pattern_ipv6_gpe_eth, hinic3_flow_parse_fdir_vxlan_geneve_filter },
	{ pattern_ipv6_gpe_eth_ipv4, hinic3_flow_parse_fdir_vxlan_geneve_filter },
	{ pattern_ipv6_gpe_eth_ipv4_tcp, hinic3_flow_parse_fdir_vxlan_geneve_filter },
	{ pattern_ipv6_gpe_eth_ipv4_udp, hinic3_flow_parse_fdir_vxlan_geneve_filter },
	{ pattern_ipv6_gpe_eth_ipv6, hinic3_flow_parse_fdir_vxlan_geneve_filter },
	{ pattern_ipv6_gpe_eth_ipv6_tcp, hinic3_flow_parse_fdir_vxlan_geneve_filter },
	{ pattern_ipv6_gpe_eth_ipv6_udp, hinic3_flow_parse_fdir_vxlan_geneve_filter },
	{ pattern_ipv4_gpe, hinic3_flow_parse_fdir_vxlan_geneve_filter },
	{ pattern_ipv6_gpe, hinic3_flow_parse_fdir_vxlan_geneve_filter },

	/*support pinip */
	{ pattern_ipv4_ipv4, hinic3_flow_parse_fdir_filter},
	{ pattern_ipv4_ipv4_tcp, hinic3_flow_parse_fdir_filter},
	{ pattern_ipv4_ipv4_udp, hinic3_flow_parse_fdir_filter},
	{ pattern_ipv4_ipv4_any, hinic3_flow_parse_fdir_filter},
	{ pattern_ipv4_ipv6, hinic3_flow_parse_fdir_filter},
	{ pattern_ipv4_ipv6_tcp, hinic3_flow_parse_fdir_filter},
	{ pattern_ipv4_ipv6_udp, hinic3_flow_parse_fdir_filter},
	{ pattern_ipv4_ipv6_any, hinic3_flow_parse_fdir_filter},
	{ pattern_ipv6_ipv4, hinic3_flow_parse_fdir_filter},
	{ pattern_ipv6_ipv4_tcp, hinic3_flow_parse_fdir_filter},
	{ pattern_ipv6_ipv4_udp, hinic3_flow_parse_fdir_filter},
	{ pattern_ipv6_ipv4_any, hinic3_flow_parse_fdir_filter},
	{ pattern_ipv6_ipv6, hinic3_flow_parse_fdir_filter},
	{ pattern_ipv6_ipv6_tcp, hinic3_flow_parse_fdir_filter},
	{ pattern_ipv6_ipv6_udp, hinic3_flow_parse_fdir_filter},
	{ pattern_ipv6_ipv6_any, hinic3_flow_parse_fdir_filter},
};

static inline void
net_addr_to_host(uint32_t *dst, const uint32_t *src, size_t len)
{
	size_t i;
	for (i = 0; i < len; i++)
		dst[i] = rte_be_to_cpu_32(src[i]);
}

/* Check if the pattern matches a supported item type array */
static bool
hinic3_match_pattern(enum rte_flow_item_type *	 item_array,
		     const struct rte_flow_item *pattern)
{
	const struct rte_flow_item *item = pattern;

	/* skip the first void item */
	while (item->type == HINIC3_FLOW_ITEM_TYPE_VOID)
		item++;

	/* find no void item */
	while (((*item_array == item->type) &&
	       (*item_array != HINIC3_FLOW_ITEM_TYPE_END)) ||
	       (item->type == HINIC3_FLOW_ITEM_TYPE_VOID)) {
		if (item->type == HINIC3_FLOW_ITEM_TYPE_VOID) {
			item++;
		} else {
			item_array++;
			item++;
		}
	}

	return (*item_array == HINIC3_FLOW_ITEM_TYPE_END &&
		item->type == HINIC3_FLOW_ITEM_TYPE_END);
}

static enum rte_flow_item_type sec_first_items[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_IPV6,
	HINIC3_FLOW_ITEM_TYPE_TCP,
	HINIC3_FLOW_ITEM_TYPE_UDP,
	HINIC3_FLOW_ITEM_TYPE_VXLAN,
	HINIC3_FLOW_ITEM_TYPE_GENEVE
};

static enum rte_flow_item_type sec_l2_next_items[] = {
	HINIC3_FLOW_ITEM_TYPE_VLAN,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_IPV6
};

static enum rte_flow_item_type sec_l3_next_items[] = {
	HINIC3_FLOW_ITEM_TYPE_TCP,
	HINIC3_FLOW_ITEM_TYPE_UDP
};

static enum rte_flow_item_type sec_l4_next_items[] = {
	HINIC3_FLOW_ITEM_TYPE_VXLAN,
	HINIC3_FLOW_ITEM_TYPE_GENEVE
};

static enum rte_flow_item_type sec_tunnel_next_items[] = {
	HINIC3_FLOW_ITEM_TYPE_ETH,
	HINIC3_FLOW_ITEM_TYPE_VLAN,
	HINIC3_FLOW_ITEM_TYPE_IPV4,
	HINIC3_FLOW_ITEM_TYPE_IPV6
};

static bool
hinic3_item_in_step(enum rte_flow_item_type type,
		    enum rte_flow_item_type *items,
		    size_t count)
{
	size_t i;

	for (i = 0; i < count; i++) {
		if (items[i] == type)
			return true;
	}

	return false;
}

static bool
hinic3_match_sec_pattern(const struct rte_flow_item *pattern)
{
	const struct rte_flow_item *item = pattern;
	enum rte_flow_item_type *items = sec_first_items;
	size_t count = RTE_DIM(sec_first_items);

	for (; item->type != HINIC3_FLOW_ITEM_TYPE_END; item++) {
		if (item->type == HINIC3_FLOW_ITEM_TYPE_VOID)
			continue;

		if (!hinic3_item_in_step(item->type, items, count))
			return false;

		switch (item->type) {
		case HINIC3_FLOW_ITEM_TYPE_ETH:
		case HINIC3_FLOW_ITEM_TYPE_VLAN:
			items = sec_l2_next_items;
			count = RTE_DIM(sec_l2_next_items);
			break;
		case HINIC3_FLOW_ITEM_TYPE_IPV4:
		case HINIC3_FLOW_ITEM_TYPE_IPV6:
			items = sec_l3_next_items;
			count = RTE_DIM(sec_l3_next_items);
			break;
		case HINIC3_FLOW_ITEM_TYPE_TCP:
		case HINIC3_FLOW_ITEM_TYPE_UDP:
			items = sec_l4_next_items;
			count = RTE_DIM(sec_l4_next_items);
			break;
		case HINIC3_FLOW_ITEM_TYPE_VXLAN:
		case HINIC3_FLOW_ITEM_TYPE_GENEVE:
			items = sec_tunnel_next_items;
			count = RTE_DIM(sec_tunnel_next_items);
			break;
		default:
			return false;
		}
	}

	return true;
}

/* Find if there's parse filter function matched */
static hinic3_parse_filter_t hinic3_find_parse_filter_func( struct rte_eth_dev *dev, const struct rte_flow_item *pattern)
{
	hinic3_parse_filter_t parse_filter = NULL;
	struct hinic3_nic_dev *nic_dev = NULL;
	uint8_t sec_tcam_en = 0;
	uint8_t i;

	nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	if (hinic3_fdir_cfg_sec_tcam(nic_dev->hwdev, &sec_tcam_en) != 0) {
		PMD_DRV_LOG(ERR, "hinic3 get port table second tcam enable status failed.");
		return parse_filter;
	}

	if (sec_tcam_en == 1) {
		if (hinic3_match_sec_pattern(pattern))
			parse_filter = hinic3_flow_parse_sec_fdir_filter;
		return parse_filter;
	}

	for (i = 0; i < RTE_DIM(hinic3_supported_patterns); i++) {
		if (hinic3_match_pattern(hinic3_supported_patterns[i].items,
					pattern)) {
			parse_filter =
				hinic3_supported_patterns[i].parse_filter;
			break;
		}
	}

	return parse_filter;
}

#ifdef HINIC3_TRAFFIC_BIFUR
static int
hinic3_flow_set_rss_action_config(struct rte_eth_dev	       *dev,
				  const struct rte_flow_action *actions,
				  struct rte_flow_error	       *error)
{
	struct rte_flow_action_rss *act_r = NULL;
	struct rte_eth_rss_conf rss_conf = { 0 };
	int ret;
	u8 hash[HINIC3_RSS_KEY_SIZE] = {0};

	act_r = (struct rte_flow_action_rss *)actions->conf;
	rss_conf.rss_hf = act_r->types;
	rte_memcpy(hash, act_r->key, act_r->key_len);
	rss_conf.rss_key = hash;
	rss_conf.rss_key_len = act_r->key_len;
	ret = hinic3_update_rss_config(dev, &rss_conf);
	if (ret) {
		rte_flow_error_set(error,
				EINVAL, HINIC3_FLOW_ERROR_TYPE_HANDLE,
				NULL, "Failed to create hash filter for RSS configuration.");
	}

	return ret;
}

static int
hinic3_check_rss_queues(struct rte_eth_dev		 *dev,
			const struct rte_pci_device	 *pci_dev,
			const struct rte_flow_action_rss *act_r,
			const struct rte_flow_action	 *act,
			struct rte_flow_error		 *error)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	uint32_t i;

	if (act_r->queue_num == 0) {
		rte_flow_error_set(error, EINVAL,
			HINIC3_FLOW_ERROR_TYPE_ACTION,
			act, "Invalid action queue number.");
		return -rte_errno;
	}

	for (i = 0; i < act_r->queue_num; i++) {
		if ((pci_dev->id.device_id == HINIC3_DEV_ID_SP920 && act_r->queue[i] >= HINIC3_QUEUE_MAX) ||
			(hinic3_bifur_is_shared_dev(nic_dev->hwdev->pci_dev) && act_r->queue[i] >= HINIC3_QUEUE_ALLOW_NUM) ||
			(act_r->queue[i] >= dev->data->nb_rx_queues)) {
			rte_flow_error_set(error, EINVAL,
						HINIC3_FLOW_ERROR_TYPE_ACTION,
						act, "Invalid action queue id.");
			return -rte_errno;
		}
	}

	if ((hinic3_bifur_is_shared_dev(nic_dev->hwdev->pci_dev) && act_r->queue_num > HINIC3_QUEUE_ALLOW_NUM) ||
			(pci_dev->id.device_id == HINIC3_DEV_ID_SP920 && act_r->queue_num > HINIC3_QUEUE_MAX)) {
			rte_flow_error_set(error, EINVAL,
					   HINIC3_FLOW_ERROR_TYPE_ACTION,
					   act, "Invalid action queue number.");
			return -rte_errno;
	}

	return 0;
}

#else
static int hinic3_flow_set_normal_rss_action_config(struct rte_eth_dev *dev,
							const struct rte_flow_action_rss *act_r,
							const struct rte_flow_action *act,
							struct rte_flow_error *error,
							struct hinic3_rss_template_entry **template_entry_out)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct hinic3_rss_template_entry *template_entry = NULL;
	bool queues_match = false;
	int ret;
	u32 template_count = 0;
	u32 j;
	u16 q_grp_id = 0;

	/* Traverse the existing RSS template list to check if there is already a matching queue configureation */
	TAILQ_FOREACH(template_entry, &nic_dev->rss_template_list, node) {
		if (template_entry->queue_num != act_r->queue_num || template_entry->types != act_r->types)
			continue;
		/* The same number of elements are compared element by element*/
		queues_match = true;
		for (j = 0; j < act_r->queue_num; j++) {
			if (template_entry->queues[j] != act_r->queue[j]) {
				queues_match = false;
				break;
			}
		}

		/* If all elements match, the same queue list is found, and the existing template is reused */
		if (queues_match) {
			q_grp_id = template_entry->q_grp_id;
			template_entry->ref_count++;
			break;
		}
	}

	/* The matching fails, Apply for a new RSS template.*/
	if (!queues_match) {
		/* The number of function templates connot exceed 32. */
		TAILQ_FOREACH(template_entry, &nic_dev->rss_template_list, node)
			template_count++;

		if (template_count >= FUNC_MAX_DPDK_NUM) {
			rte_flow_error_set(error, EINVAL, HINIC3_FLOW_ERROR_TYPE_ACTION, act, "RSS template entries exceeds max");
			return -rte_errno;
		}

		/* Alloc global group id*/
		ret = hinic3_mgmt_cfg_qgrp_id(nic_dev->hwdev, HINIC3_QUEUE_GROUP_ID_ALLOC, &q_grp_id);
		if (ret != 0) {
			rte_flow_error_set(error, EINVAL, HINIC3_FLOW_ERROR_TYPE_ACTION, act,
						"Failed to alloc q_grp_id");
			return ret;
		}

		/* Alloc rss template */
		ret = hinic3_rss_template_alloc(nic_dev->hwdev, q_grp_id);
		if (ret != 0) {
			rte_flow_error_set(error, EINVAL, HINIC3_FLOW_ERROR_TYPE_ACTION, act,
						"Failed to alloc rss template");
			goto free_g_grp_id;
		}

		/* If RSS types are set in the rule, the RSS types are delivered.
		 * Otherwise, the RSS types of the func are used.
		 */
		if (act_r->types == 0) {
			ret = hinic3_cmdq_set_rss_queue_type(nic_dev->hwdev, nic_dev->rss_type, q_grp_id, 0);
		} else {
			struct hinic3_rss_type rss_type = {0};
			rss_type.ipv4 = (act_r->types & (ETH_RSS_IPV4 | ETH_RSS_FRAG_IPV4)) ? 1 : 0;
			rss_type.tcp_ipv4 = (act_r->types & ETH_RSS_NONFRAG_IPV4_TCP) ? 1 : 0;
			rss_type.ipv6 = (act_r->types & (ETH_RSS_IPV6 | ETH_RSS_FRAG_IPV6)) ? 1 : 0;
			rss_type.tcp_ipv6 = (act_r->types & ETH_RSS_NONFRAG_IPV6_TCP) ? 1 : 0;
			rss_type.udp_ipv4 = (act_r->types & ETH_RSS_NONFRAG_IPV4_UDP) ? 1 : 0;
			rss_type.udp_ipv6 = (act_r->types & ETH_RSS_NONFRAG_IPV6_UDP) ? 1 : 0;
			ret = hinic3_cmdq_set_rss_queue_type(nic_dev->hwdev, rss_type, q_grp_id, 1);
		}

		if (ret) {
			rte_flow_error_set(error, EINVAL, HINIC3_FLOW_ERROR_TYPE_ACTION, act,
					"Failed to set rss queue types");
			goto free_rss_template;
		}

		template_entry = rte_zmalloc("template_entry", sizeof(struct hinic3_rss_template_entry), 0);
		if (template_entry == NULL) {
			rte_flow_error_set(error, ENOMEM, HINIC3_FLOW_ERROR_TYPE_ACTION, act,
						"Failed to alloc memory for template entry");
			goto free_rss_template;

		}

		/* Fill the information in the template_entry */
		template_entry->q_grp_id = q_grp_id;
		template_entry->queue_num = act_r->queue_num;
		template_entry->ref_count = 1;
		template_entry->types = act_r->types;
		rte_memcpy(template_entry->queues, act_r->queue, act_r->queue_num * sizeof(uint16_t));

		TAILQ_INSERT_TAIL(&nic_dev->rss_template_list, template_entry, node);
	}

	*template_entry_out = template_entry;

	return 0;

	free_rss_template:
	hinic3_rss_template_free(nic_dev->hwdev, q_grp_id);

	free_g_grp_id:
	hinic3_mgmt_cfg_qgrp_id(nic_dev->hwdev, HINIC3_QUEUE_GROUP_ID_FREE, &q_grp_id);

	return ret;
	}
#endif

static int
hinic3_flow_parse_action(struct rte_eth_dev	      *dev,
			 const struct rte_flow_action *actions,
			 struct rte_flow_error	      *error,
			 struct hinic3_filter_t	      *filter)
{
	const struct rte_flow_action_queue *act_q;
	const struct rte_flow_action *act = actions;
	const struct rte_flow_action_rss *act_r;
	struct hinic3_rxq *rxq;
	uint32_t i;
	int err;
#ifdef HINIC3_TRAFFIC_BIFUR
	struct rte_pci_device *pci_dev = NULL;
	pci_dev = RTE_ETH_DEV_TO_PCI(dev);

	for (i = 0; i < HINIC3_QUEUE_MAX; i++) {
		rte_bit_relaxed_clear32(i, &filter->fdir_filter.rq_index);
	}
#endif

	/* find the last non-VOID action before END */
	const struct rte_flow_action *last_act = NULL;
	for (act = actions; act->type != RTE_FLOW_ACTION_TYPE_END; act++) {
		if (act->type != RTE_FLOW_ACTION_TYPE_VOID)
			last_act = act;
	}
	if (last_act == NULL) {
		rte_flow_error_set(error, EINVAL,
				   HINIC3_FLOW_ERROR_TYPE_ACTION, actions,
				   "No valid action.");
		return -rte_errno;
	}
	act = last_act;

	switch (act->type) {
	case RTE_FLOW_ACTION_TYPE_QUEUE:
		act_q =
		(const struct rte_flow_action_queue *)act->conf;
		filter->fdir_filter.rq_index = act_q->index;
#ifdef HINIC3_TRAFFIC_BIFUR
		filter->fdir_filter.queue_num = 1;
#endif
		if (filter->fdir_filter.rq_index >=
			dev->data->nb_rx_queues) {
			rte_flow_error_set(error, EINVAL,
					   HINIC3_FLOW_ERROR_TYPE_ACTION,
					   act, "Invalid action param.");
			return -rte_errno;
		}
		rxq = (struct hinic3_rxq *)dev->data->rx_queues[act_q->index];
		if (rxq->is_hairpin)
			filter->fdir_filter.is_hairpin = 1;

		break;
/* RSS process */
#ifdef HINIC3_TRAFFIC_BIFUR
	case RTE_FLOW_ACTION_TYPE_RSS:
		act_r =
		(const struct rte_flow_action_rss *)act->conf;
		err = hinic3_check_rss_queues(dev, pci_dev, act_r, act, error);
		if (err) {
			return err;
		}
		if (act_r->queue_num > 1) {
			for (i = 0; i < act_r->queue_num; i++) {
				if (rte_bit_relaxed_get32(act_r->queue[i], &filter->fdir_filter.rq_index) != 0) {
					rte_flow_error_set(error, EINVAL,
							   HINIC3_FLOW_ERROR_TYPE_ACTION, act,
							   "Duplicate action queue numbers.");
					return -rte_errno;
				}
				rte_bit_relaxed_set32(act_r->queue[i], &filter->fdir_filter.rq_index);
			}
		} else {
			filter->fdir_filter.rq_index = act_r->queue[0];
		}
		filter->fdir_filter.queue_num = act_r->queue_num;
		if (act_r->key)
			return hinic3_flow_set_rss_action_config(dev, actions, error);

		break;
#else
	case RTE_FLOW_ACTION_TYPE_RSS:
		act_r = (const struct rte_flow_action_rss *)act->conf;

		if (!act_r || act_r->queue_num == 0) {
 	 		rte_flow_error_set(error, EINVAL, HINIC3_FLOW_ERROR_TYPE_ACTION, act,
 	 						   "Invalid rss queue num is zero");
 	 		return -rte_errno;
		}

		for (i = 0; i < act_r->queue_num; i++) {
 	 		if (act_r->queue[i] >= dev->data->nb_rx_queues) {
 	 			rte_flow_error_set(error, EINVAL, HINIC3_FLOW_ERROR_TYPE_ACTION, act,
								   "Invalid action queue id.");
 	 			return -rte_errno;
 	 		}
 	 	}

		err = hinic3_flow_set_normal_rss_action_config(dev, act_r, act, error, &filter->template_entry);
 	 	if (err)
 	 		return err;

		filter->fdir_filter.q_grp_id = filter->template_entry->q_grp_id;
 	 	filter->fdir_filter.level = act_r->level;
 	 	filter->fdir_filter.action = RTE_FLOW_ACTION_TYPE_RSS;
		break;
#endif

	case RTE_FLOW_ACTION_TYPE_DROP:
 	 	filter->fdir_filter.action = RTE_FLOW_ACTION_TYPE_DROP;
 	 	break;

	default:
		rte_flow_error_set(error, EINVAL,
				   HINIC3_FLOW_ERROR_TYPE_ACTION, act,
				   "Invalid action type.");
		return -rte_errno;
	}

	return 0;
}

/* Parse attributes */
int
hinic3_flow_parse_attr(const struct rte_flow_attr *attr,
		       struct rte_flow_error	  *error)
{
    /** Not supported egress */
    if (attr->egress) {
        rte_flow_error_set(error, EINVAL,
                    HINIC3_FLOW_ERROR_TYPE_ATTR_EGRESS,
                    attr, "Not support egress.");
        return -rte_errno;
    }

    /** Not supported priority */
    if (attr->priority) {
        rte_flow_error_set(error, EINVAL,
                    HINIC3_FLOW_ERROR_TYPE_ATTR_PRIORITY,
                    attr, "Not support priority.");
        return -rte_errno;
    }

    /** Not supported group */
    if (attr->group) {
        rte_flow_error_set(error, EINVAL,
                    HINIC3_FLOW_ERROR_TYPE_ATTR_GROUP,
                    attr, "Not support group.");
        return -rte_errno;
    }

    /** Parse attr ingress */
    if (!attr->ingress) {
        rte_flow_error_set(error, EINVAL,
                   HINIC3_FLOW_ERROR_TYPE_ATTR_INGRESS,
                   attr, "Only support ingress.");
        return -rte_errno;
    }

	return 0;
}

#ifdef HINIC3_TRAFFIC_BIFUR
static int
hinic3_flow_fdir_eth(const struct rte_flow_item *flow_item,
		     struct hinic3_filter_t	*filter,
		     struct rte_flow_error	*error)
{
	const struct rte_flow_item_eth *ether_spec, *ether_mask;
	ether_spec = (const struct rte_flow_item_eth *)flow_item->spec;
	ether_mask = (const struct rte_flow_item_eth *)flow_item->mask;
	if (!ether_spec || !ether_mask)
		return 0;

	/*
		* Mask bits of source MAC address must be full of 0.
		* Mask bits of destination MAC address must be full 0.
		*/
	if (!rte_is_zero_ether_addr(&ether_mask->src) ||
		(!rte_is_zero_ether_addr(&ether_mask->dst))) {
		rte_flow_error_set(error, EINVAL,
				HINIC3_FLOW_ERROR_TYPE_ITEM, flow_item,
				"Invalid ether address mask");
		return -rte_errno;
	}

	filter->fdir_filter.key_mask.ether_type = (u16)rte_be_to_cpu_16(ether_mask->type);
	filter->fdir_filter.key_spec.ether_type = (u16)rte_be_to_cpu_16(ether_spec->type);

	return 0;
}
#endif

static int
hinic3_flow_fdir_ipv4(const struct rte_flow_item *flow_item,
		      struct hinic3_filter_t	 *filter,
		      struct rte_flow_error	 *error)
{
	const struct rte_flow_item_ipv4 *spec_ipv4, *mask_ipv4;

	mask_ipv4 = (const struct rte_flow_item_ipv4 *)flow_item->mask;
	spec_ipv4 = (const struct rte_flow_item_ipv4 *)flow_item->spec;

	filter->fdir_filter.ip_type = HINIC3_FDIR_IP_TYPE_IPV4;
	filter->fdir_filter.tunnel_type = HINIC3_FDIR_TUNNEL_MODE_NORMAL;

	/* When both L3 mask and spec are empty, return 0, then proceed to evaluate L4. */
	if (!mask_ipv4 && !spec_ipv4)
		return 0;

	if (!mask_ipv4 || !spec_ipv4) {
		rte_flow_error_set(error, EINVAL, HINIC3_FLOW_ERROR_TYPE_ITEM, flow_item,
			"Invalid fdir filter ipv4 mask or spec");
		return -rte_errno;
	}

	/* Only support src address, dst addresses, proto, others should be masked. */
	if (mask_ipv4->hdr.version_ihl || mask_ipv4->hdr.type_of_service ||
	    mask_ipv4->hdr.total_length || mask_ipv4->hdr.packet_id ||
	    mask_ipv4->hdr.fragment_offset || mask_ipv4->hdr.time_to_live ||
	    mask_ipv4->hdr.hdr_checksum) {
		rte_flow_error_set(error, EINVAL,HINIC3_FLOW_ERROR_TYPE_ITEM, flow_item,
			"Not supported by fdir filter, ipv4 only support src ip,dst ip, proto");
		return -rte_errno;
	}

	filter->fdir_filter.key_mask.ipv4.src_ip = rte_be_to_cpu_32(mask_ipv4->hdr.src_addr);
	filter->fdir_filter.key_spec.ipv4.src_ip = rte_be_to_cpu_32(spec_ipv4->hdr.src_addr);
	filter->fdir_filter.key_mask.ipv4.dst_ip = rte_be_to_cpu_32(mask_ipv4->hdr.dst_addr);
	filter->fdir_filter.key_spec.ipv4.dst_ip = rte_be_to_cpu_32(spec_ipv4->hdr.dst_addr);
	filter->fdir_filter.key_mask.proto = mask_ipv4->hdr.next_proto_id;
	filter->fdir_filter.key_spec.proto = spec_ipv4->hdr.next_proto_id;

	return 0;
}

static int
hinic3_flow_fdir_ipv6(const struct rte_flow_item *flow_item,
		      struct hinic3_filter_t	 *filter,
		      struct rte_flow_error	 *error)
{
	const struct rte_flow_item_ipv6 *spec_ipv6, *mask_ipv6;

	mask_ipv6 = (const struct rte_flow_item_ipv6 *)flow_item->mask;
	spec_ipv6 = (const struct rte_flow_item_ipv6 *)flow_item->spec;

	filter->fdir_filter.ip_type = HINIC3_FDIR_IP_TYPE_IPV6;
	filter->fdir_filter.tunnel_type = HINIC3_FDIR_TUNNEL_MODE_NORMAL;

	/* When both L3 mask and spec are empty, return 0, then proceed to evaluate L4. */
	if (!mask_ipv6 && !spec_ipv6)
		return 0;

	if (!mask_ipv6 || !spec_ipv6) {
		rte_flow_error_set(error, EINVAL, HINIC3_FLOW_ERROR_TYPE_ITEM, flow_item,
			"Invalid fdir filter ipv6 mask or spec");
		return -rte_errno;
	}

	/* Only support dst addresses, src addresses, proto. */
	if (mask_ipv6->hdr.vtc_flow || mask_ipv6->hdr.payload_len || mask_ipv6->hdr.hop_limits) {
		rte_flow_error_set(error, EINVAL, HINIC3_FLOW_ERROR_TYPE_ITEM, flow_item,
			"Not supported by fdir filter, ipv6 only support src ip,dst ip, proto");
		return -rte_errno;
	}

	#ifdef DPDK_24_11
	net_addr_to_host(filter->fdir_filter.key_mask.ipv6.src_ip, (const uint32_t *)mask_ipv6->hdr.src_addr.a, 4);
	net_addr_to_host(filter->fdir_filter.key_spec.ipv6.src_ip, (const uint32_t *)spec_ipv6->hdr.src_addr.a, 4);
	net_addr_to_host(filter->fdir_filter.key_mask.ipv6.dst_ip, (const uint32_t *)mask_ipv6->hdr.dst_addr.a, 4);
	net_addr_to_host(filter->fdir_filter.key_spec.ipv6.dst_ip, (const uint32_t *)spec_ipv6->hdr.dst_addr.a, 4);
	#else
	net_addr_to_host(filter->fdir_filter.key_mask.ipv6.src_ip, (const uint32_t *)mask_ipv6->hdr.src_addr, 4);
	net_addr_to_host(filter->fdir_filter.key_spec.ipv6.src_ip, (const uint32_t *)spec_ipv6->hdr.src_addr, 4);
	net_addr_to_host(filter->fdir_filter.key_mask.ipv6.dst_ip, (const uint32_t *)mask_ipv6->hdr.dst_addr, 4);
	net_addr_to_host(filter->fdir_filter.key_spec.ipv6.dst_ip, (const uint32_t *)spec_ipv6->hdr.dst_addr, 4);
	#endif
	filter->fdir_filter.key_mask.proto = mask_ipv6->hdr.proto;
	filter->fdir_filter.key_spec.proto = spec_ipv6->hdr.proto;

	return 0;
}

static int
hinic3_flow_fdir_tcp(const struct rte_flow_item *flow_item,
		     struct hinic3_filter_t	*filter,
		     struct rte_flow_error	*error)
{
	const struct rte_flow_item_tcp *spec_tcp, *mask_tcp;

	mask_tcp = (const struct rte_flow_item_tcp *)flow_item->mask;
	spec_tcp = (const struct rte_flow_item_tcp *)flow_item->spec;

	filter->fdir_filter.key_mask.proto = HINIC3_UINT8_MAX;
	filter->fdir_filter.key_spec.proto = IPPROTO_TCP;

	if (!mask_tcp && !spec_tcp)
		return 0;

	if (!mask_tcp || !spec_tcp) {
		rte_flow_error_set(error, EINVAL, HINIC3_FLOW_ERROR_TYPE_ITEM, flow_item,
			"Invalid fdir filter tcp mask or spec");
		return -rte_errno;
	}

	/*
	* Only support src, dst ports, others should be masked.
	*/
	if (mask_tcp->hdr.sent_seq || mask_tcp->hdr.recv_ack || mask_tcp->hdr.data_off ||
	    mask_tcp->hdr.rx_win || mask_tcp->hdr.tcp_flags ||
	    mask_tcp->hdr.cksum ||mask_tcp->hdr.tcp_urp) {
		rte_flow_error_set(error, EINVAL, HINIC3_FLOW_ERROR_TYPE_ITEM, flow_item,
			"Not supported by fdir filter, tcp only support src port,dst port");
		return -rte_errno;
	}
	filter->fdir_filter.key_mask.src_port = (u16)rte_be_to_cpu_16(mask_tcp->hdr.src_port);
	filter->fdir_filter.key_spec.src_port = (u16)rte_be_to_cpu_16(spec_tcp->hdr.src_port);
	filter->fdir_filter.key_mask.dst_port = (u16)rte_be_to_cpu_16(mask_tcp->hdr.dst_port);
	filter->fdir_filter.key_spec.dst_port = (u16)rte_be_to_cpu_16(spec_tcp->hdr.dst_port);

	return 0;
}

static int
hinic3_flow_fdir_udp(const struct rte_flow_item *flow_item,
		     struct hinic3_filter_t	*filter,
		     struct rte_flow_error	*error)
{
	const struct rte_flow_item_udp *spec_udp, *mask_udp;

	mask_udp = (const struct rte_flow_item_udp *)flow_item->mask;
	spec_udp = (const struct rte_flow_item_udp *)flow_item->spec;

	filter->fdir_filter.key_mask.proto = HINIC3_UINT8_MAX;
	filter->fdir_filter.key_spec.proto = IPPROTO_UDP;

	if (!mask_udp && !spec_udp)
		return 0;

	if (!mask_udp || !spec_udp) {
		rte_flow_error_set(error, EINVAL, HINIC3_FLOW_ERROR_TYPE_ITEM, flow_item,
			"Invalid fdir filter udp mask or spec");
		return -rte_errno;
	}
	filter->fdir_filter.key_mask.src_port = (u16)rte_be_to_cpu_16(mask_udp->hdr.src_port);
	filter->fdir_filter.key_spec.src_port = (u16)rte_be_to_cpu_16(spec_udp->hdr.src_port);
	filter->fdir_filter.key_mask.dst_port = (u16)rte_be_to_cpu_16(mask_udp->hdr.dst_port);
	filter->fdir_filter.key_spec.dst_port = (u16)rte_be_to_cpu_16(spec_udp->hdr.dst_port);

	return 0;
}

static int
hinic3_flow_parse_fdir_pattern(__rte_unused struct rte_eth_dev *dev,
			       const struct rte_flow_item      *pattern,
			       struct rte_flow_error	       *error,
			       struct hinic3_filter_t	       *filter)
{
	const struct rte_flow_item *flow_item = pattern;
	enum rte_flow_item_type type;
	int err;

	filter->fdir_filter.ip_type = HINIC3_FDIR_IP_TYPE_ANY;

	for (; flow_item->type != HINIC3_FLOW_ITEM_TYPE_END; flow_item++) {
		if (flow_item->last) {
			rte_flow_error_set(error, EINVAL, HINIC3_FLOW_ERROR_TYPE_ITEM,
					   flow_item, "Not support range");
			return -rte_errno;
		}
		type = flow_item->type;
		switch (type) {
		case HINIC3_FLOW_ITEM_TYPE_ETH:
#ifdef HINIC3_TRAFFIC_BIFUR
			err = hinic3_flow_fdir_eth(flow_item, filter, error);
			if (err != 0)
				return -rte_errno;
#else
			/* All should be masked. */
			if (flow_item->spec || flow_item->mask) {
				rte_flow_error_set(error, EINVAL,
					HINIC3_FLOW_ERROR_TYPE_ITEM, flow_item,
					"Not supported by fdir filter, not support mac");
				return -rte_errno;
			}
#endif
			break;

		case HINIC3_FLOW_ITEM_TYPE_IPV4:
			err = hinic3_flow_fdir_ipv4(flow_item, filter, error);
			if (err != 0)
				return -rte_errno;
			break;

		case HINIC3_FLOW_ITEM_TYPE_IPV6:
			err = hinic3_flow_fdir_ipv6(flow_item, filter, error);
			if (err != 0)
				return -rte_errno;
			break;

		case HINIC3_FLOW_ITEM_TYPE_TCP:
			err = hinic3_flow_fdir_tcp(flow_item, filter, error);
			if (err != 0)
				return -rte_errno;
			break;

		case HINIC3_FLOW_ITEM_TYPE_UDP:
			err = hinic3_flow_fdir_udp(flow_item, filter, error);
			if (err != 0)
				return -rte_errno;
			break;

		default:
			break;
		}
	}

	return 0;
}

static int
hinic3_flow_parse_fdir_filter(struct rte_eth_dev	  *dev,
			      const struct rte_flow_attr  *attr,
			      const struct rte_flow_item   pattern[],
			      const struct rte_flow_action actions[],
			      struct rte_flow_error	  *error,
			      struct hinic3_filter_t	  *filter)
{
	int ret;

	ret = hinic3_flow_parse_fdir_pattern(dev, pattern, error,
					   filter);
	if (ret)
		return ret;

	ret = hinic3_flow_parse_action(dev, actions, error, filter);
	if (ret)
		return ret;

	ret = hinic3_flow_parse_attr(attr, error);
	if (ret)
		return ret;

	filter->filter_type = RTE_ETH_FILTER_FDIR;

	return 0;
}

static int
hinic3_flow_parse_ethertype_action(struct rte_eth_dev		*dev,
				   const struct rte_flow_action *actions,
				   struct rte_flow_error	*error,
				   struct hinic3_filter_t	*filter)
{
	const struct rte_flow_action *act = actions;
	const struct rte_flow_action_queue *act_q;

	/* find the last non-VOID action before END */
	const struct rte_flow_action *last_act = NULL;
	for (act = actions; act->type != RTE_FLOW_ACTION_TYPE_END; act++) {
		if (act->type != RTE_FLOW_ACTION_TYPE_VOID)
			last_act = act;
	}
	if (last_act == NULL) {
		rte_flow_error_set(error, EINVAL,
				   HINIC3_FLOW_ERROR_TYPE_ACTION, actions,
				   "No valid action.");
		return -rte_errno;
	}
	act = last_act;

	switch (act->type) {
	case RTE_FLOW_ACTION_TYPE_QUEUE:
		act_q =
		(const struct rte_flow_action_queue *)act->conf;
		filter->ethertype_filter.queue = act_q->index;
		if (filter->ethertype_filter.queue >=
			dev->data->nb_rx_queues) {
			rte_flow_error_set(error, EINVAL,
					   HINIC3_FLOW_ERROR_TYPE_ACTION,
					   act, "Invalid action param.");
			return -rte_errno;
		}
		break;

	case RTE_FLOW_ACTION_TYPE_DROP:
 	 	filter->ethertype_filter.flags |= RTE_ETHTYPE_FLAGS_DROP;
 	 	break;

	default:
		rte_flow_error_set(error, EINVAL,
				   HINIC3_FLOW_ERROR_TYPE_ACTION, act,
				   "Invalid action type.");
		return -rte_errno;
	}

	return 0;
}

static int
hinic3_flow_parse_ethertype_pattern(__rte_unused struct rte_eth_dev *dev,
				    const struct rte_flow_item	    *pattern,
				    struct rte_flow_error	    *error,
				    struct hinic3_filter_t	    *filter)
{
	const struct rte_flow_item_eth *ether_spec, *ether_mask;
	const struct rte_flow_item *flow_item = pattern;
	enum rte_flow_item_type type;

	for (; flow_item->type != HINIC3_FLOW_ITEM_TYPE_END; flow_item++) {
		if (flow_item->last) {
			rte_flow_error_set(error, EINVAL,
					   HINIC3_FLOW_ERROR_TYPE_ITEM,
					   flow_item, "Not support range");
			return -rte_errno;
		}
		type = flow_item->type;
		switch (type) {
		case HINIC3_FLOW_ITEM_TYPE_ETH:
			ether_spec = (const struct rte_flow_item_eth *)flow_item->spec;
			ether_mask = (const struct rte_flow_item_eth *)flow_item->mask;
			if (!ether_spec || !ether_mask) {
				rte_flow_error_set(error, EINVAL,
					HINIC3_FLOW_ERROR_TYPE_ITEM,
					flow_item, "NULL ETH spec/mask");
				return -rte_errno;
			}

			/*
			 * Mask bits of source MAC address must be full of 0.
			 * Mask bits of destination MAC address must be full 0.
			 */
			if (!rte_is_zero_ether_addr(&ether_mask->src) ||
			    (!rte_is_zero_ether_addr(&ether_mask->dst))) {
				rte_flow_error_set(error, EINVAL,
						HINIC3_FLOW_ERROR_TYPE_ITEM, flow_item,
						"Invalid ether address mask");
				return -rte_errno;
			}

			if ((ether_mask->type & UINT16_MAX) != UINT16_MAX) { /*lint !e40*/
				rte_flow_error_set(error, EINVAL,
						HINIC3_FLOW_ERROR_TYPE_ITEM, flow_item,
						"Invalid ethertype mask");
				return -rte_errno;
			}

			filter->ethertype_filter.ether_type =
				(u16)rte_be_to_cpu_16(ether_spec->type);

			switch (filter->ethertype_filter.ether_type) {
			case RTE_ETHER_TYPE_SLOW:
				break;

			case RTE_ETHER_TYPE_ARP:
				break;

			case RTE_ETHER_TYPE_RARP:
				break;

			case RTE_ETHER_TYPE_LLDP:
				break;

			default:
				rte_flow_error_set(error, EINVAL,
				   HINIC3_FLOW_ERROR_TYPE_ITEM, flow_item,
				   "Unsupported ether_type in"
				   " control packet filter.");
				return -rte_errno;
			}
			break;

		default:
			break;
		}
	}

	return 0;
}

static int
hinic3_flow_parse_ethertype_filter(struct rte_eth_dev	       *dev,
				   const struct rte_flow_attr  *attr,
				   const struct rte_flow_item	pattern[],
				   const struct rte_flow_action actions[],
				   struct rte_flow_error       *error,
				   struct hinic3_filter_t      *filter)
{
	int ret;

	ret = hinic3_flow_parse_ethertype_pattern(dev, pattern, error,
					   filter);
	if (ret)
		return ret;

	ret = hinic3_flow_parse_ethertype_action(dev, actions, error, filter);
	if (ret)
		return ret;

	ret = hinic3_flow_parse_attr(attr, error);
	if (ret)
		return ret;

	filter->filter_type = RTE_ETH_FILTER_ETHERTYPE;
	return 0;
}

static int
hinic3_flow_fdir_tunnel_ipv4(struct rte_flow_error	 *error,
			     struct hinic3_filter_t	 *filter,
			     const struct rte_flow_item	 *flow_item,
			     enum hinic3_fdir_tunnel_mode tunnel_mode)
{
	const struct rte_flow_item_ipv4 *spec_ipv4, *mask_ipv4;
	mask_ipv4 = (const struct rte_flow_item_ipv4 *)flow_item->mask;
	spec_ipv4 = (const struct rte_flow_item_ipv4 *)flow_item->spec;

	if (tunnel_mode == HINIC3_FDIR_TUNNEL_MODE_NORMAL) {
		filter->fdir_filter.outer_ip_type = HINIC3_FDIR_IP_TYPE_IPV4;

		if (!mask_ipv4 && !spec_ipv4)
			return 0;

		if (!mask_ipv4 || !spec_ipv4) {
			rte_flow_error_set(error, EINVAL, HINIC3_FLOW_ERROR_TYPE_ITEM, flow_item,
				"Invalid fdir filter, vxlan/geneve outer ipv4 mask or spec");
			return -rte_errno;
		}

		/*
		* Only support src address , dst addresses,
		* others should be masked.
		*/
		if (mask_ipv4->hdr.version_ihl || mask_ipv4->hdr.type_of_service ||
		    mask_ipv4->hdr.total_length || mask_ipv4->hdr.packet_id ||
		    mask_ipv4->hdr.fragment_offset || mask_ipv4->hdr.time_to_live ||
		    mask_ipv4->hdr.next_proto_id || mask_ipv4->hdr.hdr_checksum) {
			rte_flow_error_set(error, EINVAL, HINIC3_FLOW_ERROR_TYPE_ITEM, flow_item,
				"Not supported by fdir filter, vxlan/geneve outer ipv4 only support src ip,dst ip");
			return -rte_errno;
		}

		filter->fdir_filter.key_mask.ipv4.src_ip =
			rte_be_to_cpu_32(mask_ipv4->hdr.src_addr);
		filter->fdir_filter.key_spec.ipv4.src_ip =
			rte_be_to_cpu_32(spec_ipv4->hdr.src_addr);
		filter->fdir_filter.key_mask.ipv4.dst_ip =
			rte_be_to_cpu_32(mask_ipv4->hdr.dst_addr);
		filter->fdir_filter.key_spec.ipv4.dst_ip =
			rte_be_to_cpu_32(spec_ipv4->hdr.dst_addr);
	} else {
		filter->fdir_filter.ip_type = HINIC3_FDIR_IP_TYPE_IPV4;

		if (!mask_ipv4 && !spec_ipv4)
			return 0;

		if (!mask_ipv4 || !spec_ipv4) {
			rte_flow_error_set(error, EINVAL, HINIC3_FLOW_ERROR_TYPE_ITEM, flow_item,
				"Invalid fdir filter, vxlan/geneve inner ipv4 mask or spec");
			return -rte_errno;
		}

		/*
		* Only support src addr , dst addr, ip proto
		* others should be masked.
		*/
		if (mask_ipv4->hdr.version_ihl || mask_ipv4->hdr.type_of_service ||
		    mask_ipv4->hdr.total_length || mask_ipv4->hdr.packet_id ||
		    mask_ipv4->hdr.fragment_offset || mask_ipv4->hdr.time_to_live ||
		    mask_ipv4->hdr.hdr_checksum) {
			rte_flow_error_set(error, EINVAL, HINIC3_FLOW_ERROR_TYPE_ITEM, flow_item,
				"Not supported by fdir filter, vxlan/geneve inner ipv4 only support src ip,dst ip, proto");
			return -rte_errno;
		}

		filter->fdir_filter.key_mask.inner_ipv4.src_ip =
			rte_be_to_cpu_32(mask_ipv4->hdr.src_addr);
		filter->fdir_filter.key_spec.inner_ipv4.src_ip =
			rte_be_to_cpu_32(spec_ipv4->hdr.src_addr);
		filter->fdir_filter.key_mask.inner_ipv4.dst_ip =
			rte_be_to_cpu_32(mask_ipv4->hdr.dst_addr);
		filter->fdir_filter.key_spec.inner_ipv4.dst_ip =
			rte_be_to_cpu_32(spec_ipv4->hdr.dst_addr);
		filter->fdir_filter.key_mask.proto = mask_ipv4->hdr.next_proto_id;
		filter->fdir_filter.key_spec.proto = spec_ipv4->hdr.next_proto_id;
	}
	return 0;
}

static int
hinic3_flow_fdir_tunnel_ipv6(struct rte_flow_error	 *error,
			     struct hinic3_filter_t	 *filter,
			     const struct rte_flow_item	 *flow_item,
			     enum hinic3_fdir_tunnel_mode tunnel_mode)
{
	const struct rte_flow_item_ipv6 *spec_ipv6, *mask_ipv6;

	mask_ipv6 = (const struct rte_flow_item_ipv6 *)flow_item->mask;
	spec_ipv6 = (const struct rte_flow_item_ipv6 *)flow_item->spec;

	if (tunnel_mode == HINIC3_FDIR_TUNNEL_MODE_NORMAL) {
		filter->fdir_filter.outer_ip_type = HINIC3_FDIR_IP_TYPE_IPV6;

		if (!mask_ipv6 && !spec_ipv6)
			return 0;

		if (!mask_ipv6 || !spec_ipv6) {
			rte_flow_error_set(error, EINVAL, HINIC3_FLOW_ERROR_TYPE_ITEM, flow_item,
				"Invalid fdir filter ipv6 mask or spec");
			return -rte_errno;
		}

		/* Only support dst addresses, src addresses */
		if (mask_ipv6->hdr.vtc_flow || mask_ipv6->hdr.payload_len || mask_ipv6->hdr.hop_limits ||
			mask_ipv6->hdr.proto) {
			rte_flow_error_set(error, EINVAL, HINIC3_FLOW_ERROR_TYPE_ITEM, flow_item,
				"Not supported by fdir filter, ipv6 only support src ip, dst ip, proto");
			return -rte_errno;
		}

		#ifdef DPDK_24_11
		net_addr_to_host(filter->fdir_filter.key_mask.ipv6.src_ip,
			(const uint32_t *)mask_ipv6->hdr.src_addr.a, 4);
		net_addr_to_host(filter->fdir_filter.key_spec.ipv6.src_ip,
			(const uint32_t *)spec_ipv6->hdr.src_addr.a, 4);
		net_addr_to_host(filter->fdir_filter.key_mask.ipv6.dst_ip,
			(const uint32_t *)mask_ipv6->hdr.dst_addr.a, 4);
		net_addr_to_host(filter->fdir_filter.key_spec.ipv6.dst_ip,
			(const uint32_t *)spec_ipv6->hdr.dst_addr.a, 4);
		#else
		net_addr_to_host(filter->fdir_filter.key_mask.ipv6.src_ip,
			(const uint32_t *)mask_ipv6->hdr.src_addr, 4);
		net_addr_to_host(filter->fdir_filter.key_spec.ipv6.src_ip,
			(const uint32_t *)spec_ipv6->hdr.src_addr, 4);
		net_addr_to_host(filter->fdir_filter.key_mask.ipv6.dst_ip,
			(const uint32_t *)mask_ipv6->hdr.dst_addr, 4);
		net_addr_to_host(filter->fdir_filter.key_spec.ipv6.dst_ip,
			(const uint32_t *)spec_ipv6->hdr.dst_addr, 4);
		#endif
	} else {
		filter->fdir_filter.ip_type = HINIC3_FDIR_IP_TYPE_IPV6;

		if (!mask_ipv6 && !spec_ipv6)
			return 0;

		if (!mask_ipv6 || !spec_ipv6) {
			rte_flow_error_set(error, EINVAL, HINIC3_FLOW_ERROR_TYPE_ITEM, flow_item,
				"Invalid fdir filter ipv6 mask or spec");
			return -rte_errno;
		}

		/* Only support dst addresses, src addresses, proto */
		if (mask_ipv6->hdr.vtc_flow || mask_ipv6->hdr.payload_len || mask_ipv6->hdr.hop_limits) {
			rte_flow_error_set(error, EINVAL, HINIC3_FLOW_ERROR_TYPE_ITEM, flow_item,
				"Not supported by fdir filter, ipv6 only support src ip, dst ip, proto");
			return -rte_errno;
		}

		#ifdef DPDK_24_11
		net_addr_to_host(filter->fdir_filter.key_mask.inner_ipv6.src_ip,
			(const uint32_t *)mask_ipv6->hdr.src_addr.a, 4);
		net_addr_to_host(filter->fdir_filter.key_spec.inner_ipv6.src_ip,
			(const uint32_t *)spec_ipv6->hdr.src_addr.a, 4);
		net_addr_to_host(filter->fdir_filter.key_mask.inner_ipv6.dst_ip,
			(const uint32_t *)mask_ipv6->hdr.dst_addr.a, 4);
		net_addr_to_host(filter->fdir_filter.key_spec.inner_ipv6.dst_ip,
			(const uint32_t *)spec_ipv6->hdr.dst_addr.a, 4);
		#else
		net_addr_to_host(filter->fdir_filter.key_mask.inner_ipv6.src_ip,
			(const uint32_t *)mask_ipv6->hdr.src_addr, 4);
		net_addr_to_host(filter->fdir_filter.key_spec.inner_ipv6.src_ip,
			(const uint32_t *)spec_ipv6->hdr.src_addr, 4);
		net_addr_to_host(filter->fdir_filter.key_mask.inner_ipv6.dst_ip,
			(const uint32_t *)mask_ipv6->hdr.dst_addr, 4);
		net_addr_to_host(filter->fdir_filter.key_spec.inner_ipv6.dst_ip,
			(const uint32_t *)spec_ipv6->hdr.dst_addr, 4);
		#endif

		filter->fdir_filter.key_mask.proto = mask_ipv6->hdr.proto;
		filter->fdir_filter.key_spec.proto = spec_ipv6->hdr.proto;
	}

	return 0;
}

static int
hinic3_flow_fdir_tunnel_tcp(struct rte_flow_error	*error,
			    struct hinic3_filter_t	*filter,
			    enum hinic3_fdir_tunnel_mode tunnel_mode,
			    const struct rte_flow_item	*flow_item)
{
	const struct rte_flow_item_tcp *spec_tcp, *mask_tcp;

	if (tunnel_mode == HINIC3_FDIR_TUNNEL_MODE_NORMAL) {
		rte_flow_error_set(error, EINVAL, HINIC3_FLOW_ERROR_TYPE_ITEM, flow_item,
			"Not supported by fdir filter, vxlan/geneve only support inner tcp");
		return -rte_errno;
	}

	filter->fdir_filter.key_mask.proto = HINIC3_UINT8_MAX;
	filter->fdir_filter.key_spec.proto = IPPROTO_TCP;

	mask_tcp = (const struct rte_flow_item_tcp *)flow_item->mask;
	spec_tcp = (const struct rte_flow_item_tcp *)flow_item->spec;
	if (!mask_tcp && !spec_tcp)
		return 0;
	if (!mask_tcp || !spec_tcp) {
		rte_flow_error_set(error, EINVAL, HINIC3_FLOW_ERROR_TYPE_ITEM, flow_item,
			"Invalid fdir filter tcp mask or spec");
		return -rte_errno;
	}

	/*
	* Only support src, dst ports, others should be masked.
	*/
	if (mask_tcp->hdr.sent_seq || mask_tcp->hdr.recv_ack || mask_tcp->hdr.data_off ||
		mask_tcp->hdr.rx_win || mask_tcp->hdr.tcp_flags ||
		mask_tcp->hdr.cksum || mask_tcp->hdr.tcp_urp) {
		rte_flow_error_set(error, EINVAL, HINIC3_FLOW_ERROR_TYPE_ITEM, flow_item,
			"Not supported by fdir filter, vxlan/geneve inner tcp only support src port,dst port");
		return -rte_errno;
	}
	filter->fdir_filter.key_mask.src_port = (u16)rte_be_to_cpu_16(mask_tcp->hdr.src_port);
	filter->fdir_filter.key_spec.src_port = (u16)rte_be_to_cpu_16(spec_tcp->hdr.src_port);
	filter->fdir_filter.key_mask.dst_port = (u16)rte_be_to_cpu_16(mask_tcp->hdr.dst_port);
	filter->fdir_filter.key_spec.dst_port = (u16)rte_be_to_cpu_16(spec_tcp->hdr.dst_port);
	return 0;
}

static int
hinic3_flow_fdir_tunnel_udp(struct rte_flow_error	*error,
			    struct hinic3_filter_t	*filter,
			    enum hinic3_fdir_tunnel_mode tunnel_mode,
			    const struct rte_flow_item	*flow_item)
{
	const struct rte_flow_item_udp *spec_udp, *mask_udp;

	mask_udp = (const struct rte_flow_item_udp *)flow_item->mask;
	spec_udp = (const struct rte_flow_item_udp *)flow_item->spec;

	if (tunnel_mode == HINIC3_FDIR_TUNNEL_MODE_NORMAL) {
		/* UDP is used to describe protocol,
		* spec and mask should be NULL.
		*/
		if (flow_item->spec || flow_item->mask) {
			rte_flow_error_set(error, EINVAL, HINIC3_FLOW_ERROR_TYPE_ITEM,
				flow_item, "Invalid UDP item");
			return -rte_errno;
		}
	} else {
		filter->fdir_filter.key_mask.proto = HINIC3_UINT8_MAX;
		filter->fdir_filter.key_spec.proto = IPPROTO_UDP;
		if (!mask_udp && !spec_udp)
			return 0;

		if (!mask_udp || !spec_udp) {
			rte_flow_error_set(error, EINVAL, HINIC3_FLOW_ERROR_TYPE_ITEM, flow_item,
				"Invalid fdir filter vxlan/geneve inner udp mask or spec");
			return -rte_errno;
		}
		filter->fdir_filter.key_mask.src_port =
			(u16)rte_be_to_cpu_16(mask_udp->hdr.src_port);
		filter->fdir_filter.key_spec.src_port =
			(u16)rte_be_to_cpu_16(spec_udp->hdr.src_port);
		filter->fdir_filter.key_mask.dst_port =
			(u16)rte_be_to_cpu_16(mask_udp->hdr.dst_port);
		filter->fdir_filter.key_spec.dst_port =
			(u16)rte_be_to_cpu_16(spec_udp->hdr.dst_port);
	}
	return 0;
}

static int
hinic3_flow_fdir_vxlan_geneve(struct rte_flow_error	  *error,
			      struct hinic3_filter_t	  *filter,
			      enum hinic3_fdir_tunnel_mode tunnel_mode,
			      const struct rte_flow_item  *flow_item)
{
	const struct rte_flow_item_vxlan *spec_vxlan, *mask_vxlan;
	uint32_t vxlan_vni_id = 0;
	uint32_t vxlan_vni_id_mask = 0;

	spec_vxlan = (const struct rte_flow_item_vxlan *)flow_item->spec;
	mask_vxlan = (const struct rte_flow_item_vxlan *)flow_item->mask;

	filter->fdir_filter.tunnel_type = tunnel_mode;

	if (!spec_vxlan && !mask_vxlan)
		return 0;
	else if (filter->fdir_filter.outer_ip_type == HINIC3_FDIR_IP_TYPE_IPV6) {
		rte_flow_error_set(error, EINVAL, HINIC3_FLOW_ERROR_TYPE_ITEM, flow_item,
				"Invalid fdir filter vxlan/geneve mask or spec, ipv6 vxlan/geneve don't support vni");
		return -rte_errno;
	}

	if (!spec_vxlan || !mask_vxlan) {
		rte_flow_error_set(error, EINVAL, HINIC3_FLOW_ERROR_TYPE_ITEM, flow_item,
			"Invalid fdir filter vxlan/geneve mask or spec");
		return -rte_errno;
	}

	rte_memcpy(((uint8_t *)&vxlan_vni_id + 1), spec_vxlan->vni, 3);
	filter->fdir_filter.key_spec.tunnel.tunnel_id = rte_be_to_cpu_32(vxlan_vni_id);
	rte_memcpy(((uint8_t *)&vxlan_vni_id_mask + 1), mask_vxlan->vni, 3);
	filter->fdir_filter.key_mask.tunnel.tunnel_id =	rte_be_to_cpu_32(vxlan_vni_id_mask);

	return 0;
}

static int
hinic3_flow_parse_fdir_vxlan_geneve_pattern(
	__rte_unused struct rte_eth_dev *dev,
	const struct rte_flow_item      *pattern,
	struct rte_flow_error           *error,
	struct hinic3_filter_t          *filter)
{
	const struct rte_flow_item *flow_item = pattern;
	enum hinic3_fdir_tunnel_mode tunnel_mode = HINIC3_FDIR_TUNNEL_MODE_NORMAL;
	enum rte_flow_item_type type;
	int err;

	/* inner and outer ip type, set it to any by default */
	filter->fdir_filter.ip_type = HINIC3_FDIR_IP_TYPE_ANY;
	filter->fdir_filter.outer_ip_type = HINIC3_FDIR_IP_TYPE_ANY;

	for (; flow_item->type != HINIC3_FLOW_ITEM_TYPE_END; flow_item++) {
		if (flow_item->last) {
			rte_flow_error_set(error, EINVAL, HINIC3_FLOW_ERROR_TYPE_ITEM, flow_item,
					   "Not support range");
			return -rte_errno;
		}

		type = flow_item->type;
		switch (type) {
		case HINIC3_FLOW_ITEM_TYPE_ETH:
			/* All should be masked. */
			if (flow_item->spec || flow_item->mask) {
				rte_flow_error_set(error, EINVAL, HINIC3_FLOW_ERROR_TYPE_ITEM,
					flow_item, "Not supported by fdir filter, not support mac");
				return -rte_errno;
			}
			break;

		case HINIC3_FLOW_ITEM_TYPE_IPV4:
			err = hinic3_flow_fdir_tunnel_ipv4(error, filter, flow_item, tunnel_mode);
			if (err != 0)
				return -rte_errno;
			break;

		case HINIC3_FLOW_ITEM_TYPE_IPV6:
			err = hinic3_flow_fdir_tunnel_ipv6(error, filter, flow_item, tunnel_mode);
			if (err != 0)
				return -rte_errno;
			break;

		case HINIC3_FLOW_ITEM_TYPE_TCP:
			err = hinic3_flow_fdir_tunnel_tcp(error, filter, tunnel_mode, flow_item);
			if (err != 0)
				return -rte_errno;
			break;

		case HINIC3_FLOW_ITEM_TYPE_UDP:
			err = hinic3_flow_fdir_tunnel_udp(error, filter, tunnel_mode, flow_item);
			if (err != 0)
				return -rte_errno;
			break;

		case HINIC3_FLOW_ITEM_TYPE_VXLAN:
			tunnel_mode = HINIC3_FDIR_TUNNEL_MODE_VXLAN;
			err = hinic3_flow_fdir_vxlan_geneve(error, filter, tunnel_mode, flow_item);
			if (err != 0)
				return -rte_errno;
			break;

		case HINIC3_FLOW_ITEM_TYPE_GENEVE:
			tunnel_mode = HINIC3_FDIR_TUNNEL_MODE_GENEVE;
			err = hinic3_flow_fdir_vxlan_geneve(error, filter, tunnel_mode, flow_item);
			if (err != 0)
				return -rte_errno;
			break;

		case HINIC3_FLOW_ITEM_TYPE_VXLAN_GPE:
			tunnel_mode = HINIC3_FDIR_TUNNEL_MODE_GPE;
			err = hinic3_flow_fdir_vxlan_geneve(error, filter, tunnel_mode, flow_item);
			if (err != 0)
				return -rte_errno;
			break;

		default:
			break;
		}
	}

	return 0;
}

static int
hinic3_flow_parse_fdir_vxlan_geneve_filter(
	struct rte_eth_dev          *dev,
	const struct rte_flow_attr  *attr,
	const struct rte_flow_item   pattern[],
	const struct rte_flow_action actions[],
	struct rte_flow_error       *error,
	struct hinic3_filter_t      *filter)
{
	int ret;

	ret = hinic3_flow_parse_fdir_vxlan_geneve_pattern(dev, pattern, error,
					   filter);
	if (ret)
		return ret;

	ret = hinic3_flow_parse_action(dev, actions, error, filter);
	if (ret)
		return ret;

	ret = hinic3_flow_parse_attr(attr, error);
	if (ret)
		return ret;

	filter->filter_type = RTE_ETH_FILTER_FDIR;

	return 0;
}

static int
hinic3_flow_parse_sec_fdir_filter(struct rte_eth_dev	  *dev,
			      const struct rte_flow_attr  *attr,
			      const struct rte_flow_item   pattern[],
			      const struct rte_flow_action actions[],
			      struct rte_flow_error	  *error,
			      struct hinic3_filter_t	  *filter)
{
	int ret;

	ret = hinic3_flow_parse_sec_fdir_pattern(dev, pattern, error,
					   filter);
	if (ret)
		return ret;

	ret = hinic3_flow_parse_action(dev, actions, error, filter);
	if (ret)
		return ret;

	ret = hinic3_flow_parse_attr(attr, error);
	if (ret)
		return ret;

	filter->filter_type = RTE_ETH_FILTER_FDIR;
	filter->is_sec_fdir = true;

	return 0;
}

static int
hinic3_flow_parse(struct rte_eth_dev          *dev,
		  const struct rte_flow_attr  *attr,
		  const struct rte_flow_item   pattern[],
		  const struct rte_flow_action actions[],
		  struct rte_flow_error       *error,
		  struct hinic3_filter_t      *filter)
{
	hinic3_parse_filter_t parse_filter;
	uint32_t pattern_num = 0;
	int ret = 0;

    if (!pattern) {
		rte_flow_error_set(error, EINVAL,
				   HINIC3_FLOW_ERROR_TYPE_UNSPECIFIED,
				   NULL, "Pattern is NULL.");
		return -rte_errno;
	}

    if (!actions) {
        rte_flow_error_set(error, EINVAL,
                    HINIC3_FLOW_ERROR_TYPE_ACTION,
                    NULL, "Actions is NULL.");
        return -rte_errno;
    }

    if (!attr) {
        rte_flow_error_set(error, EINVAL,
                    HINIC3_FLOW_ERROR_TYPE_ATTR,
                    NULL, "Attr is NULL.");
        return -rte_errno;
    }

	while ((pattern + pattern_num)->type != HINIC3_FLOW_ITEM_TYPE_END) {
		pattern_num++;
		if (pattern_num > HINIC3_FLOW_MAX_PATTERN_NUM) {
			rte_flow_error_set(error,
					   EINVAL, HINIC3_FLOW_MAX_PATTERN_NUM,
					   NULL, "Too many patterns.");
			return -rte_errno;
		}
	}

	parse_filter = hinic3_find_parse_filter_func(dev, pattern);
	if (!parse_filter) {
		rte_flow_error_set(error, EINVAL,
				   HINIC3_FLOW_ERROR_TYPE_ITEM,
				   pattern, "Unsupported pattern");
		return -rte_errno;
	}

	ret = parse_filter(dev, attr, pattern, actions,
			error, filter);

	return ret;
}

static int
hinic3_flow_validate(struct rte_eth_dev          *dev,
		     const struct rte_flow_attr  *attr,
		     const struct rte_flow_item   pattern[],
		     const struct rte_flow_action actions[],
		     struct rte_flow_error       *error)
{
	struct hinic3_filter_t filter_rules = {0};

	return hinic3_flow_parse(dev, attr, pattern, actions, error,
			&filter_rules);
}

static void
hinic3_fillout_indir_tbl_by_rss_template(struct hinic3_nic_dev *nic_dev,
					struct hinic3_rss_template_entry *template_entry,
					u32 *indir)
{
	u32 i;
	u16 queue_idx;
	u16 queue_num;

	if (template_entry == NULL || template_entry->queue_num == 0) {
		for (i = 0; i < HINIC3_RSS_INDIR_SIZE; i++)
			indir[i] = i % nic_dev->num_rqs;
		return;
	}

	queue_num = template_entry->queue_num;
	queue_idx = 0;

	/* fillout indir table used queue list */
	for (i = 0; i < HINIC3_RSS_INDIR_SIZE; i++) {
		indir[i] = template_entry->queues[queue_idx];
		queue_idx = (queue_idx + 1) % queue_num;
	}
}

static struct rte_flow *
hinic3_flow_create(struct rte_eth_dev          *dev,
		   const struct rte_flow_attr  *attr,
		   const struct rte_flow_item   pattern[],
		   const struct rte_flow_action actions[],
		   struct rte_flow_error       *error)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct hinic3_filter_t *filter_rules =  NULL;
	struct rte_flow *flow = NULL;
	struct hinic3_rss_template_entry *template_entry = NULL;
 	u32 indirtbl[HINIC3_RSS_INDIR_SIZE] = {0};
	int ret;

	filter_rules = rte_zmalloc("filter_rules",
			sizeof(struct hinic3_filter_t), 0);
	if (!filter_rules) {
		rte_flow_error_set(error,
			   EINVAL, HINIC3_FLOW_ERROR_TYPE_HANDLE,
			   NULL, "Failed to allocate filter rules memory.");
		return NULL;
	}

	flow = rte_zmalloc("hinic3_rte_flow", sizeof(struct rte_flow), 0);
	if (!flow) {
		rte_flow_error_set(error,
			   EINVAL, HINIC3_FLOW_ERROR_TYPE_HANDLE,
			   NULL, "Failed to allocate flow memory.");
		rte_free(filter_rules);
		return NULL;
	}

	ret = hinic3_flow_parse(dev, attr, pattern, actions, error,
			filter_rules);
	if (ret < 0) {
		goto free_flow;
	}

	switch (filter_rules->filter_type) {
	case RTE_ETH_FILTER_ETHERTYPE:
		ret = hinic3_flow_add_del_ethertype_filter(dev,
							   &filter_rules->ethertype_filter,
							   true);
		if (ret) {
			rte_flow_error_set(error,
				   EINVAL, HINIC3_FLOW_ERROR_TYPE_HANDLE,
				   NULL, "Create ethertype filter failed.");
			goto free_flow;
		}

		flow->rule = filter_rules;
		flow->filter_type = filter_rules->filter_type;
		TAILQ_INSERT_TAIL(&nic_dev->filter_ethertype_list, flow, node);
		break;

	case RTE_ETH_FILTER_FDIR:
		if (filter_rules->is_sec_fdir) {
			ret = hinic3_flow_add_del_sec_fdir_filter(dev,
					&filter_rules->sec_fdir_filter,
					&filter_rules->fdir_filter, true);
		} else {
			ret = hinic3_flow_add_del_fdir_filter(dev,
					&filter_rules->fdir_filter, true);
		}

		if (ret) {
			rte_flow_error_set(error,
				   EINVAL, HINIC3_FLOW_ERROR_TYPE_HANDLE,
				   NULL, "Create fdir filter failed.");
			goto free_flow;
		}

		flow->rule = filter_rules;
		flow->filter_type = filter_rules->filter_type;
		TAILQ_INSERT_TAIL(&nic_dev->filter_fdir_rule_list, flow, node);

		if (filter_rules->template_entry == NULL)
			break;

		template_entry = filter_rules->template_entry;
		if (template_entry->ref_count == 1) {
			hinic3_fillout_indir_tbl_by_rss_template(nic_dev, template_entry, indirtbl);
			ret = hinic3_rss_queue_set_indir_tbl(nic_dev->hwdev, indirtbl, HINIC3_RSS_INDIR_SIZE, template_entry->q_grp_id);
			if (ret) {
				PMD_DRV_LOG(ERR, "Set rss queue indir tbl failed");
				goto free_flow;
			}
		}

		break;
	default:
		PMD_DRV_LOG(ERR, "Filter type %d not supported",
			    filter_rules->filter_type);
		rte_flow_error_set(error,
				   EINVAL, HINIC3_FLOW_ERROR_TYPE_HANDLE,
				   NULL, "Unsupport filter type.");
		goto free_flow;
	}
	return flow;

free_flow:
	rte_free(flow);
	rte_free(filter_rules);

	return NULL;
}

static void hinic3_flow_release_rss_template(struct hinic3_nic_dev *nic_dev,
						struct hinic3_rss_template_entry *template_entry)
{
	int ret = 0;
	u16 q_grp_id;

	if (template_entry == NULL)
		return;

	/* Check the reference count */
	if (template_entry->ref_count > 1) {
		template_entry->ref_count--;
		PMD_DRV_LOG(INFO, "RSS template q_grp_id: %u ref_count decreased to %u",
				template_entry->q_grp_id, template_entry->ref_count);
		return;
	}

	/* If reference count is 1，delete RSS template and q_grp_id */
	q_grp_id = template_entry->q_grp_id;

	hinic3_rss_template_free(nic_dev->hwdev, q_grp_id);

	ret = hinic3_mgmt_cfg_qgrp_id(nic_dev->hwdev, HINIC3_QUEUE_GROUP_ID_FREE, &q_grp_id);
	if (ret != 0)
		PMD_DRV_LOG(ERR, "Failed to free q_grp_id: %u, ret: %d", q_grp_id, ret);

	TAILQ_REMOVE(&nic_dev->rss_template_list, template_entry, node);
	rte_free(template_entry);
	PMD_DRV_LOG(INFO, "RSS template q_grp_id: %u deleted and removed from list", q_grp_id);

	return;
}

static int
hinic3_flow_destroy(struct rte_eth_dev *dev, struct rte_flow *flow,
		    struct rte_flow_error *error)
{
	int ret = -EINVAL;
	enum rte_filter_type type;
	struct hinic3_filter_t *rules = NULL;
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);

	if (!flow) {
		PMD_DRV_LOG(ERR, "Invalid flow parameter!");
		return -EPERM;
	}

	type = flow->filter_type;
	rules = (struct hinic3_filter_t *)flow->rule;

	switch (type) {
	case RTE_ETH_FILTER_ETHERTYPE:
		ret = hinic3_flow_add_del_ethertype_filter(dev,
				&rules->ethertype_filter, false);
		if (!ret)
			TAILQ_REMOVE(&nic_dev->filter_ethertype_list, flow, node);

		flow->rule = rules;
		flow->filter_type = rules->filter_type;
		TAILQ_REMOVE(&nic_dev->filter_ethertype_list, flow, node);
		break;

	case RTE_ETH_FILTER_FDIR:
		ret = hinic3_flow_add_del_fdir_filter(dev,
				&rules->fdir_filter, false);
		if (!ret)
			TAILQ_REMOVE(&nic_dev->filter_fdir_rule_list, flow, node);

		if (!ret && rules->template_entry != NULL)
 	 			hinic3_flow_release_rss_template(nic_dev,rules->template_entry);

		break;
	default:
		PMD_DRV_LOG(WARNING, "Filter type %d not supported",
			    type);
		ret = -EINVAL;
		break;
	}

	if (!ret) {
		rte_free(rules);
		rte_free(flow);
	} else {
		rte_flow_error_set(error, -ret,
				   HINIC3_FLOW_ERROR_TYPE_HANDLE, NULL,
				   "Failed to destroy flow.");
	}

	return ret;
}

static int
hinic3_flow_flush_fdir_filter(struct rte_eth_dev *dev)
{
	int ret = 0;
	struct hinic3_filter_t *filter_rules =  NULL;
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct rte_flow *flow;

	while (true) { /*lint !e716*/
		flow = TAILQ_FIRST(&nic_dev->filter_fdir_rule_list);
		if (flow == NULL)
			break;
		filter_rules = (struct hinic3_filter_t *)flow->rule;
		ret = hinic3_flow_add_del_fdir_filter(dev,
				&filter_rules->fdir_filter, false);
		if (ret)
			return ret;

		if (filter_rules->template_entry != NULL)
 	 			hinic3_flow_release_rss_template(nic_dev, filter_rules->template_entry);

		TAILQ_REMOVE(&nic_dev->filter_fdir_rule_list, flow, node);
		rte_free(filter_rules);
		rte_free(flow);
	}

	return ret;
}

static int
hinic3_flow_flush_ethertype_filter(struct rte_eth_dev *dev)
{
	struct hinic3_filter_t *filter_rules =  NULL;
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct rte_flow *flow;
	int ret = 0;

	while (true) { /*lint !e716*/
		flow = TAILQ_FIRST(&nic_dev->filter_ethertype_list);
		if (flow == NULL)
			break;
		filter_rules = (struct hinic3_filter_t *)flow->rule;
		ret = hinic3_flow_add_del_ethertype_filter(dev,
							   &filter_rules->ethertype_filter,
							   false);
		if (ret)
			return ret;

		TAILQ_REMOVE(&nic_dev->filter_ethertype_list, flow, node);
		rte_free(filter_rules);
		rte_free(flow);
	}

	return ret;
}

static int
hinic3_flow_flush(struct rte_eth_dev *dev, struct rte_flow_error *error)
{
	int ret;

	ret = hinic3_flow_flush_fdir_filter(dev);
	if (ret) {
		rte_flow_error_set(error, -ret,
				   HINIC3_FLOW_ERROR_TYPE_HANDLE, NULL,
				   "Failed to flush fdir flows.");
		return -rte_errno;
	}

	ret = hinic3_flow_flush_ethertype_filter(dev);
	if (ret) {
		rte_flow_error_set(error, -ret,
				   HINIC3_FLOW_ERROR_TYPE_HANDLE, NULL,
				   "Failed to flush ethertype flows.");
		return -rte_errno;
	}

	return ret;
}

#ifdef HINIC3_TRAFFIC_BIFUR
static int
hinic3_flow_query(struct rte_eth_dev *dev, struct rte_flow *flow,
		  __rte_unused const struct rte_flow_action *actions,
		  void *data, struct rte_flow_error *error)
{
	int ret = -EINVAL;
	enum rte_filter_type filter_type;
	struct hinic3_filter_t *filter_rules = NULL;
	struct rte_flow_query_count *flow_count = NULL;

	if (!flow || !data) {
		PMD_DRV_LOG(ERR, "Invalid flow parameter!");
		return -EPERM;
	}

	flow_count = (struct rte_flow_query_count *)data;
	filter_type = flow->filter_type;
	switch (filter_type) {
		case RTE_ETH_FILTER_ETHERTYPE:
			PMD_DRV_LOG(ERR, "Ethertype type %d, current not to process", filter_type);
			break;
		case RTE_ETH_FILTER_FDIR:
			filter_rules = (struct hinic3_filter_t *)flow->rule;
			ret = hinic3_flow_query_fdir_filter(dev, &filter_rules->fdir_filter, &flow_count->hits, &flow_count->bytes);
			break;
		default:
			PMD_DRV_LOG(ERR, "Filter type %d not support to query", filter_type);
			ret = -EINVAL;
			break;
	}

	if (ret) {
		rte_flow_error_set(error, -ret, HINIC3_FLOW_ERROR_TYPE_HANDLE, NULL, "Failed to query flow.");
    }

    return ret;
}
#endif

const struct rte_flow_ops hinic3_flow_ops = {
	.validate = hinic3_flow_validate,
	.create = hinic3_flow_create,
	.destroy = hinic3_flow_destroy,
	.flush = hinic3_flow_flush,
#ifdef HINIC3_TRAFFIC_BIFUR
	.query = hinic3_flow_query,
#endif
};
