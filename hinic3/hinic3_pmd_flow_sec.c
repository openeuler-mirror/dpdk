/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2025-2026 Huawei Technologies Co., Ltd
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <rte_byteorder.h>
#include <rte_common.h>
#include <rte_ether.h>
#include <rte_ethdev.h>
#include <rte_flow.h>
#include <rte_ip.h>
#include <rte_malloc.h>

#include "base/hinic3_compat.h"
#include "base/hinic3_pmd_hwdev.h"
#include "base/hinic3_pmd_nic_cfg.h"
#include "base/hinic3_pmd_hwif.h"
#include "hinic3_pmd_ethdev.h"
#include "hinic3_pmd_fdir.h"
#include "hinic3_pmd_flow_sec.h"
#include "hinic3_pmd_flow.h"

#define HINIC3_UINT1_MAX  0x1
#define HINIC3_UINT2_MAX  0x3
#define HINIC3_UINT4_MAX  0xf
#define HINIC3_UINT8_MAX  0xff
#define HINIC3_UINT15_MAX 0x7fff
#define HINIC3_INVALID_INDEX (-1)
#define HINIC3_VXLAN_UDP_DPORT 4789
#define HINIC3_GENEVE_UDP_DPORT 6081

#define HINIC3_DEV_PRIVATE_TO_TCAM_INFO(nic_dev) \
	(&((struct hinic3_nic_dev *)(nic_dev))->tcam)

#ifndef RTE_VLAN_ID_MASK
#define RTE_VLAN_ID_MASK 0x0FFF
#endif
#ifndef RTE_VLAN_CFI_MASK
#define RTE_VLAN_CFI_MASK 0x1000
#endif
#ifndef RTE_VLAN_PRI_MASK
#define RTE_VLAN_PRI_MASK 0xE000
#endif
#ifndef RTE_VLAN_CFI_SHIFT
#define RTE_VLAN_CFI_SHIFT 12
#endif
#ifndef RTE_VLAN_PRI_SHIFT
#define RTE_VLAN_PRI_SHIFT 13
#endif

static inline void
net_addr_to_host(uint32_t *dst, const uint32_t *src, size_t len)
{
	size_t i;

	for (i = 0; i < len; i++)
		dst[i] = rte_be_to_cpu_32(src[i]);
}

static inline void
hinic3_sec_fdir_ipv6_to_host(uint32_t *src_ip, uint32_t *dst_ip,
			     const struct rte_ipv6_hdr *hdr)
{
	net_addr_to_host(src_ip,
		(const uint32_t *)HINIC3_IPV6_HDR_SRC_ADDR(hdr), 4);
	net_addr_to_host(dst_ip,
		(const uint32_t *)HINIC3_IPV6_HDR_DST_ADDR(hdr), 4);
}

static inline void
hinic3_sec_fdir_set_ipv6_addrs(uint32_t *mask_src, uint32_t *mask_dst,
			       uint32_t *spec_src, uint32_t *spec_dst,
			       const struct rte_ipv6_hdr *mask_hdr,
			       const struct rte_ipv6_hdr *spec_hdr)
{
	hinic3_sec_fdir_ipv6_to_host(mask_src, mask_dst, mask_hdr);
	hinic3_sec_fdir_ipv6_to_host(spec_src, spec_dst, spec_hdr);
}

static inline void
hinic3_sec_fdir_set_ports(struct hinic3_sec_fdir_filter *sec_fdir,
			  bool is_outer, uint16_t src_mask, uint16_t src_spec,
			  uint16_t dst_mask, uint16_t dst_spec)
{
	if (is_outer) {
		sec_fdir->outer_sport_mask = src_mask;
		sec_fdir->outer_sport_spec = src_spec;
		sec_fdir->outer_dport_mask = dst_mask;
		sec_fdir->outer_dport_spec = dst_spec;
	} else {
		sec_fdir->key_mask.src_port = src_mask;
		sec_fdir->key_spec.src_port = src_spec;
		sec_fdir->key_mask.dst_port = dst_mask;
		sec_fdir->key_spec.dst_port = dst_spec;
	}
}

static void
hinic3_sec_fdir_mac_to_u16(const struct rte_ether_addr *addr,
			   u16 *mac_h, u16 *mac_m, u16 *mac_l)
{
	const u8 *bytes = addr->addr_bytes;

	*mac_h = (u16)(((u16)bytes[0] << 8) | bytes[1]);
	*mac_m = (u16)(((u16)bytes[2] << 8) | bytes[3]);
	*mac_l = (u16)(((u16)bytes[4] << 8) | bytes[5]);
}

static inline struct hinic3_tcam_filter *
hinic3_tcam_filter_lookup(struct hinic3_tcam_filter_list *filter_list,
			  struct hinic3_tcam_key *key, u8 action_type, u16 tcam_index)
{
	struct hinic3_tcam_filter *it;

	if (action_type == HINIC3_ACTION_ADD) {
		TAILQ_FOREACH (it, filter_list, entries) {
			if (memcmp(key, &it->tcam_key, sizeof(struct hinic3_tcam_key)) == 0)
				return it;
		}
	} else {
		TAILQ_FOREACH (it, filter_list, entries) {
			if ((it->index + HINIC3_PKT_TCAM_DYNAMIC_INDEX_START(it->dynamic_block_id)) == tcam_index)
				return it;
		}
	}

	return NULL;
}

#define HINIC3_SEC_FDIR_TCAM_SET_U32_HL(key, base, value) \
	do { \
		(key)->base##_h = HINIC3_32_UPPER_16_BITS(value); \
		(key)->base##_l = HINIC3_32_LOWER_16_BITS(value); \
	} while (0)

#define HINIC3_SEC_FDIR_TCAM_SET_IPV6_ADDR(key, base, ip) \
	do { \
		(key)->base##0_h = HINIC3_32_UPPER_16_BITS((ip)[0]); \
		(key)->base##0_l = HINIC3_32_LOWER_16_BITS((ip)[0]); \
		(key)->base##1_h = HINIC3_32_UPPER_16_BITS((ip)[1]); \
		(key)->base##1_l = HINIC3_32_LOWER_16_BITS((ip)[1]); \
		(key)->base##2_h = HINIC3_32_UPPER_16_BITS((ip)[2]); \
		(key)->base##2_l = HINIC3_32_LOWER_16_BITS((ip)[2]); \
		(key)->base##3_h = HINIC3_32_UPPER_16_BITS((ip)[3]); \
		(key)->base##3_l = HINIC3_32_LOWER_16_BITS((ip)[3]); \
	} while (0)

#define HINIC3_SEC_FDIR_TCAM_SET_VLAN(key_mask, key_info, sec_fdir, \
				      vlan_spec, vlan_mask) \
	do { \
		if ((sec_fdir)->has_vlan || (vlan_mask) != 0) { \
			(key_mask)->vlan_flag = HINIC3_UINT1_MAX; \
			(key_info)->vlan_flag = (sec_fdir)->has_vlan ? 1 : 0; \
			(key_mask)->vlan_vid = (vlan_mask) & RTE_VLAN_ID_MASK; \
			(key_mask)->vlan_cfi = \
				(u16)(((vlan_mask) & RTE_VLAN_CFI_MASK) >> \
				      RTE_VLAN_CFI_SHIFT); \
			(key_mask)->vlan_pri = \
				(u16)(((vlan_mask) & RTE_VLAN_PRI_MASK) >> \
				      RTE_VLAN_PRI_SHIFT); \
			(key_info)->vlan_vid = (vlan_spec) & RTE_VLAN_ID_MASK; \
			(key_info)->vlan_cfi = \
				(u16)(((vlan_spec) & RTE_VLAN_CFI_MASK) >> \
				      RTE_VLAN_CFI_SHIFT); \
			(key_info)->vlan_pri = \
				(u16)(((vlan_spec) & RTE_VLAN_PRI_MASK) >> \
				      RTE_VLAN_PRI_SHIFT); \
		} \
	} while (0)

#define HINIC3_SEC_FDIR_TCAM_SET_MAC(key_mask, key_info, sec_fdir) \
	do { \
		u16 __dmac_h, __dmac_m, __dmac_l; \
		u16 __smac_h, __smac_m, __smac_l; \
		u16 __dmac_h_mask, __dmac_m_mask, __dmac_l_mask; \
		u16 __smac_h_mask, __smac_m_mask, __smac_l_mask; \
		hinic3_sec_fdir_mac_to_u16( \
			&HINIC3_ETHER_HDR_DST_ADDR(&(sec_fdir)->ether_mask), \
			&__dmac_h_mask, &__dmac_m_mask, &__dmac_l_mask); \
		hinic3_sec_fdir_mac_to_u16( \
			&HINIC3_ETHER_HDR_SRC_ADDR(&(sec_fdir)->ether_mask), \
			&__smac_h_mask, &__smac_m_mask, &__smac_l_mask); \
		hinic3_sec_fdir_mac_to_u16( \
			&HINIC3_ETHER_HDR_DST_ADDR(&(sec_fdir)->ether_spec), \
			&__dmac_h, &__dmac_m, &__dmac_l); \
		hinic3_sec_fdir_mac_to_u16( \
			&HINIC3_ETHER_HDR_SRC_ADDR(&(sec_fdir)->ether_spec), \
			&__smac_h, &__smac_m, &__smac_l); \
		(key_mask)->dmac_h = __dmac_h_mask; \
		(key_mask)->dmac_m = __dmac_m_mask; \
		(key_mask)->dmac_l = __dmac_l_mask; \
		(key_mask)->smac_h = __smac_h_mask; \
		(key_mask)->smac_m = __smac_m_mask; \
		(key_mask)->smac_l = __smac_l_mask; \
		(key_info)->dmac_h = __dmac_h; \
		(key_info)->dmac_m = __dmac_m; \
		(key_info)->dmac_l = __dmac_l; \
		(key_info)->smac_h = __smac_h; \
		(key_info)->smac_m = __smac_m; \
		(key_info)->smac_l = __smac_l; \
	} while (0)

#define HINIC3_SEC_FDIR_TCAM_SET_L3L4_COMMON(key_mask, key_info, sec_fdir) \
	do { \
		(key_mask)->eth_type = (sec_fdir)->key_mask.ether_type; \
		(key_info)->eth_type = (sec_fdir)->key_spec.ether_type; \
		(key_mask)->tcp_flag = (sec_fdir)->tcp_flags_mask; \
		(key_info)->tcp_flag = (sec_fdir)->tcp_flags_spec; \
		(key_mask)->ip_proto = (sec_fdir)->key_mask.proto; \
		(key_info)->ip_proto = (sec_fdir)->key_spec.proto; \
		(key_mask)->dport = (sec_fdir)->key_mask.dst_port; \
		(key_info)->dport = (sec_fdir)->key_spec.dst_port; \
		(key_mask)->sport = (sec_fdir)->key_mask.src_port; \
		(key_info)->sport = (sec_fdir)->key_spec.src_port; \
	} while (0)

#define HINIC3_SEC_FDIR_TCAM_SET_OUTER_PORTS(key_mask, key_info, sec_fdir) \
	do { \
		(key_mask)->outer_sport = (sec_fdir)->outer_sport_mask; \
		(key_mask)->outer_dport = (sec_fdir)->outer_dport_mask; \
		(key_info)->outer_sport = (sec_fdir)->outer_sport_spec; \
		(key_info)->outer_dport = (sec_fdir)->outer_dport_spec; \
	} while (0)

#define HINIC3_SEC_FDIR_TCAM_SET_VNI(key_mask, key_info, sec_fdir) \
	do { \
		HINIC3_SEC_FDIR_TCAM_SET_U32_HL((key_mask), vni, \
					       (sec_fdir)->key_mask.tunnel.tunnel_id); \
		HINIC3_SEC_FDIR_TCAM_SET_U32_HL((key_info), vni, \
					       (sec_fdir)->key_spec.tunnel.tunnel_id); \
	} while (0)

#define HINIC3_SEC_FDIR_TCAM_SET_INNER_PORTS(key_mask, key_info, sec_fdir) \
	do { \
		(key_mask)->inner_dport = (sec_fdir)->key_mask.dst_port; \
		(key_info)->inner_dport = (sec_fdir)->key_spec.dst_port; \
		(key_mask)->inner_sport = (sec_fdir)->key_mask.src_port; \
		(key_info)->inner_sport = (sec_fdir)->key_spec.src_port; \
	} while (0)

#define HINIC3_SEC_FDIR_TCAM_SET_INNER_PROTO_TCP(key_mask, key_info, sec_fdir) \
	do { \
		(key_mask)->inner_tcp_flag = (sec_fdir)->tcp_flags_mask; \
		(key_info)->inner_tcp_flag = (sec_fdir)->tcp_flags_spec; \
		(key_mask)->inner_ip_proto = (sec_fdir)->key_mask.proto; \
		(key_info)->inner_ip_proto = (sec_fdir)->key_spec.proto; \
	} while (0)

#define HINIC3_SEC_FDIR_TCAM_INIT_TUNNEL_COMMON(key_mask, key_info, sec_fdir, \
						nic_dev, outer_ip, inner_ip, \
						tunnel, vlan_spec, vlan_mask) \
	do { \
		(key_mask)->func_id = HINIC3_UINT15_MAX; \
		(key_info)->func_id = \
			hinic3_global_func_id((nic_dev)->hwdev) & \
			HINIC3_UINT15_MAX; \
		(key_mask)->outer_ip_type = HINIC3_UINT1_MAX; \
		(key_info)->outer_ip_type = (outer_ip); \
		(key_mask)->inner_ip_type = HINIC3_UINT1_MAX; \
		(key_info)->inner_ip_type = (inner_ip); \
		(key_mask)->key_width = HINIC3_UINT2_MAX; \
		(key_info)->key_width = 1; \
		(key_mask)->tunnel_type = HINIC3_UINT4_MAX; \
		(key_info)->tunnel_type = (tunnel); \
		HINIC3_SEC_FDIR_TCAM_SET_VLAN(key_mask, key_info, sec_fdir, \
					      vlan_spec, vlan_mask); \
		(key_mask)->eth_type = (sec_fdir)->key_mask.ether_type; \
		(key_info)->eth_type = (sec_fdir)->key_spec.ether_type; \
		(key_mask)->outer_tcp_flag = 0; \
		(key_info)->outer_tcp_flag = 0; \
		(key_mask)->outer_ip_proto = (sec_fdir)->outer_proto_mask; \
		(key_info)->outer_ip_proto = (sec_fdir)->outer_proto_spec; \
	} while (0)

static void
hinic3_sec_fdir_tcam_init_ipv4_common(struct hinic3_nic_dev *nic_dev,
				      const struct hinic3_sec_fdir_filter *sec_fdir,
				      struct hinic3_tcam_sec_key_ipv4_mem *key_mask,
				      struct hinic3_tcam_sec_key_ipv4_mem *key_info)
{
	u16 vlan_spec = sec_fdir->vlan_tci_spec;
	u16 vlan_mask = sec_fdir->vlan_tci_mask;

	key_mask->func_id = HINIC3_UINT15_MAX;
	key_info->func_id =
		hinic3_global_func_id(nic_dev->hwdev) & HINIC3_UINT15_MAX;

	key_mask->has_ip_flag = HINIC3_UINT1_MAX;
	key_info->has_ip_flag = sec_fdir->has_ip_flag ? 1 : 0;
	key_mask->ip_type = HINIC3_UINT1_MAX;
	key_info->ip_type = sec_fdir->ip_type;

	key_mask->key_width = HINIC3_UINT2_MAX;
	if (!sec_fdir->has_ip_flag ||
	    sec_fdir->ip_type == HINIC3_FDIR_IP_TYPE_IPV4)
		key_info->key_width = HINIC3_FDIR_EXT_320;
	else
		key_info->key_width = HINIC3_FDIR_EXT_640;

	key_mask->tunnel_type = HINIC3_UINT4_MAX;
	key_info->tunnel_type = HINIC3_FDIR_TUNNEL_MODE_NORMAL;

	HINIC3_SEC_FDIR_TCAM_SET_VLAN(key_mask, key_info, sec_fdir,
				      vlan_spec, vlan_mask);
	HINIC3_SEC_FDIR_TCAM_SET_MAC(key_mask, key_info, sec_fdir);
	HINIC3_SEC_FDIR_TCAM_SET_L3L4_COMMON(key_mask, key_info, sec_fdir);
}

static void
hinic3_sec_fdir_tcam_init_ipv6_common(struct hinic3_nic_dev *nic_dev,
				      const struct hinic3_sec_fdir_filter *sec_fdir,
				      struct hinic3_tcam_sec_key_ipv6_mem *key_mask,
				      struct hinic3_tcam_sec_key_ipv6_mem *key_info)
{
	u16 vlan_spec = sec_fdir->vlan_tci_spec;
	u16 vlan_mask = sec_fdir->vlan_tci_mask;

	key_mask->func_id = HINIC3_UINT15_MAX;
	key_info->func_id =
		hinic3_global_func_id(nic_dev->hwdev) & HINIC3_UINT15_MAX;

	key_mask->ip_type = HINIC3_UINT1_MAX;
	key_info->ip_type = sec_fdir->ip_type;

	key_mask->key_width = HINIC3_UINT2_MAX;
	key_info->key_width = 1;

	key_mask->tunnel_type = HINIC3_UINT4_MAX;
	key_info->tunnel_type = HINIC3_FDIR_TUNNEL_MODE_NORMAL;

	HINIC3_SEC_FDIR_TCAM_SET_VLAN(key_mask, key_info, sec_fdir,
				      vlan_spec, vlan_mask);
	HINIC3_SEC_FDIR_TCAM_SET_MAC(key_mask, key_info, sec_fdir);
	HINIC3_SEC_FDIR_TCAM_SET_L3L4_COMMON(key_mask, key_info, sec_fdir);
}

static int
hinic3_flow_sec_fdir_eth(const struct rte_flow_item *flow_item,
            struct hinic3_filter_t *filter,
            struct rte_flow_error  *error)
{
    const struct rte_flow_item_eth *ether_spec, *ether_mask;
    ether_spec = (const struct rte_flow_item_eth *)flow_item->spec;
    ether_mask = (const struct rte_flow_item_eth *)flow_item->mask;

    if (!ether_mask && !ether_spec)
        return 0;

    if (!ether_mask || !ether_spec) {
        rte_flow_error_set(error, EINVAL,
                HINIC3_FLOW_ERROR_TYPE_ITEM, flow_item,
                "Invalid fdir filter ether mask or spec");
        return -rte_errno;
    }

    /* Fill dmac/smac mask and spec */
	rte_ether_addr_copy(&ether_spec->dst,
        &HINIC3_ETHER_HDR_DST_ADDR(&filter->sec_fdir_filter.ether_spec));
    rte_ether_addr_copy(&ether_spec->src,
        &HINIC3_ETHER_HDR_SRC_ADDR(&filter->sec_fdir_filter.ether_spec));
    rte_ether_addr_copy(&ether_mask->dst,
        &HINIC3_ETHER_HDR_DST_ADDR(&filter->sec_fdir_filter.ether_mask));
    rte_ether_addr_copy(&ether_mask->src,
        &HINIC3_ETHER_HDR_SRC_ADDR(&filter->sec_fdir_filter.ether_mask));
    filter->sec_fdir_filter.key_mask.ether_type = (u16)rte_be_to_cpu_16(ether_mask->type);
    filter->sec_fdir_filter.key_spec.ether_type = (u16)rte_be_to_cpu_16(ether_spec->type);
#if HINIC3_FLOW_ITEM_ETH_HAS_VLAN
    if (ether_mask->has_vlan)
        filter->sec_fdir_filter.has_vlan = !!ether_spec->has_vlan;
#endif

    return 0;
}

static int
hinic3_flow_sec_fdir_vlan(const struct rte_flow_item *flow_item,
              struct hinic3_filter_t *filter,
              struct rte_flow_error *error)
{
    const struct rte_flow_item_vlan *vlan_spec, *vlan_mask;

    vlan_spec = (const struct rte_flow_item_vlan *)flow_item->spec;
    vlan_mask = (const struct rte_flow_item_vlan *)flow_item->mask;

    if (!vlan_spec && !vlan_mask)
        return 0;

    if (!vlan_spec || !vlan_mask) {
        rte_flow_error_set(error, EINVAL,
                HINIC3_FLOW_ERROR_TYPE_ITEM, flow_item,
                "Invalid sec fdir filter vlan mask or spec");
        return -rte_errno;
    }

    filter->sec_fdir_filter.has_vlan = true;
    filter->sec_fdir_filter.vlan_tci_spec = rte_be_to_cpu_16(vlan_spec->tci);
    filter->sec_fdir_filter.vlan_tci_mask = rte_be_to_cpu_16(vlan_mask->tci);

    return 0;
}

static int
hinic3_flow_sec_fdir_ipv4(const struct rte_flow_item *flow_item,
		      struct hinic3_filter_t	 *filter,
		      struct rte_flow_error	 *error,
		      enum hinic3_fdir_tunnel_mode tunnel_mode,
		      bool is_tunnel)
{
	const struct rte_flow_item_ipv4 *spec_ipv4, *mask_ipv4;
	bool is_outer = is_tunnel && (tunnel_mode == HINIC3_FDIR_TUNNEL_MODE_NORMAL);

	mask_ipv4 = (const struct rte_flow_item_ipv4 *)flow_item->mask;
	spec_ipv4 = (const struct rte_flow_item_ipv4 *)flow_item->spec;

	filter->sec_fdir_filter.ip_type = HINIC3_FDIR_IP_TYPE_IPV4;
	filter->sec_fdir_filter.has_ip_flag = true;
	if (is_outer) {
		filter->fdir_filter.outer_ip_type = HINIC3_FDIR_IP_TYPE_IPV4;
		filter->fdir_filter.ip_type = HINIC3_FDIR_IP_TYPE_ANY;
	} else
		filter->fdir_filter.ip_type = HINIC3_FDIR_IP_TYPE_IPV4;

	/* When both L3 mask and spec are empty, return 0, then proceed to evaluate L4. */
	if (!mask_ipv4 && !spec_ipv4)
		return 0;

	if (!mask_ipv4 || !spec_ipv4) {
		rte_flow_error_set(error, EINVAL, HINIC3_FLOW_ERROR_TYPE_ITEM, flow_item,
			"Invalid fdir filter ipv4 mask or spec");
		return -rte_errno;
	}

	if (is_outer) {
		/* Only support src address, dst addresses, proto, others should be masked. */
		if (mask_ipv4->hdr.version_ihl || mask_ipv4->hdr.type_of_service ||
		    mask_ipv4->hdr.total_length || mask_ipv4->hdr.packet_id ||
		    mask_ipv4->hdr.fragment_offset || mask_ipv4->hdr.time_to_live ||
		    mask_ipv4->hdr.hdr_checksum) {
			rte_flow_error_set(error, EINVAL,HINIC3_FLOW_ERROR_TYPE_ITEM, flow_item,
				"Not supported by sec fdir filter, tunnel outer ipv4 only support src ip,dst ip, proto");
			return -rte_errno;
		}

		filter->sec_fdir_filter.key_mask.ipv4.src_ip =
			rte_be_to_cpu_32(mask_ipv4->hdr.src_addr);
		filter->sec_fdir_filter.key_spec.ipv4.src_ip =
			rte_be_to_cpu_32(spec_ipv4->hdr.src_addr);
		filter->sec_fdir_filter.key_mask.ipv4.dst_ip =
			rte_be_to_cpu_32(mask_ipv4->hdr.dst_addr);
		filter->sec_fdir_filter.key_spec.ipv4.dst_ip =
			rte_be_to_cpu_32(spec_ipv4->hdr.dst_addr);
		filter->sec_fdir_filter.outer_proto_mask = mask_ipv4->hdr.next_proto_id;
		filter->sec_fdir_filter.outer_proto_spec = spec_ipv4->hdr.next_proto_id;
		return 0;
	}

	/* Only support src address, dst addresses, proto, others should be masked. */
	if (mask_ipv4->hdr.version_ihl || mask_ipv4->hdr.type_of_service ||
	    mask_ipv4->hdr.total_length || mask_ipv4->hdr.packet_id ||
	    mask_ipv4->hdr.fragment_offset || mask_ipv4->hdr.time_to_live ||
	    mask_ipv4->hdr.hdr_checksum) {
		rte_flow_error_set(error, EINVAL,HINIC3_FLOW_ERROR_TYPE_ITEM, flow_item,
			"Not supported by sec fdir filter, ipv4 only support src ip,dst ip, proto");
		return -rte_errno;
	}

	if (tunnel_mode == HINIC3_FDIR_TUNNEL_MODE_NORMAL) {
		filter->sec_fdir_filter.key_mask.ipv4.src_ip =
			rte_be_to_cpu_32(mask_ipv4->hdr.src_addr);
		filter->sec_fdir_filter.key_spec.ipv4.src_ip =
			rte_be_to_cpu_32(spec_ipv4->hdr.src_addr);
		filter->sec_fdir_filter.key_mask.ipv4.dst_ip =
			rte_be_to_cpu_32(mask_ipv4->hdr.dst_addr);
		filter->sec_fdir_filter.key_spec.ipv4.dst_ip =
			rte_be_to_cpu_32(spec_ipv4->hdr.dst_addr);
	} else {
		filter->sec_fdir_filter.key_mask.inner_ipv4.src_ip =
			rte_be_to_cpu_32(mask_ipv4->hdr.src_addr);
		filter->sec_fdir_filter.key_spec.inner_ipv4.src_ip =
			rte_be_to_cpu_32(spec_ipv4->hdr.src_addr);
		filter->sec_fdir_filter.key_mask.inner_ipv4.dst_ip =
			rte_be_to_cpu_32(mask_ipv4->hdr.dst_addr);
		filter->sec_fdir_filter.key_spec.inner_ipv4.dst_ip =
			rte_be_to_cpu_32(spec_ipv4->hdr.dst_addr);
	}
	filter->sec_fdir_filter.key_mask.proto = mask_ipv4->hdr.next_proto_id;
	filter->sec_fdir_filter.key_spec.proto = spec_ipv4->hdr.next_proto_id;

	return 0;
}

static int
hinic3_flow_sec_fdir_ipv6(const struct rte_flow_item *flow_item,
		      struct hinic3_filter_t	 *filter,
		      struct rte_flow_error	 *error,
		      enum hinic3_fdir_tunnel_mode tunnel_mode,
		      bool is_tunnel)
{
	const struct rte_flow_item_ipv6 *spec_ipv6, *mask_ipv6;
	bool is_outer = is_tunnel && (tunnel_mode == HINIC3_FDIR_TUNNEL_MODE_NORMAL);

	mask_ipv6 = (const struct rte_flow_item_ipv6 *)flow_item->mask;
	spec_ipv6 = (const struct rte_flow_item_ipv6 *)flow_item->spec;

	filter->sec_fdir_filter.ip_type = HINIC3_FDIR_IP_TYPE_IPV6;
	if (is_outer)
		filter->fdir_filter.outer_ip_type = HINIC3_FDIR_IP_TYPE_IPV6;
	else
		filter->fdir_filter.ip_type = HINIC3_FDIR_IP_TYPE_IPV6;

	/* When both L3 mask and spec are empty, return 0, then proceed to evaluate L4. */
	if (!mask_ipv6 && !spec_ipv6)
		return 0;

	if (!mask_ipv6 || !spec_ipv6) {
		rte_flow_error_set(error, EINVAL, HINIC3_FLOW_ERROR_TYPE_ITEM, flow_item,
			"Invalid fdir filter ipv6 mask or spec");
		return -rte_errno;
	}

	if (is_outer) {
		if (mask_ipv6->hdr.vtc_flow || mask_ipv6->hdr.payload_len ||
		    mask_ipv6->hdr.hop_limits) {
			rte_flow_error_set(error, EINVAL, HINIC3_FLOW_ERROR_TYPE_ITEM, flow_item,
				"Not supported by sec fdir filter, tunnel outer ipv6 only support src ip,dst ip, proto");
			return -rte_errno;
			}

		hinic3_sec_fdir_set_ipv6_addrs(
			filter->sec_fdir_filter.key_mask.ipv6.src_ip,
			filter->sec_fdir_filter.key_mask.ipv6.dst_ip,
			filter->sec_fdir_filter.key_spec.ipv6.src_ip,
			filter->sec_fdir_filter.key_spec.ipv6.dst_ip,
			&mask_ipv6->hdr, &spec_ipv6->hdr);

		filter->sec_fdir_filter.outer_proto_mask = mask_ipv6->hdr.proto;
		filter->sec_fdir_filter.outer_proto_spec = spec_ipv6->hdr.proto;

		return 0;
	}

	/* Only support dst addresses, src addresses, proto. */
	if (mask_ipv6->hdr.vtc_flow || mask_ipv6->hdr.payload_len || mask_ipv6->hdr.hop_limits) {
		rte_flow_error_set(error, EINVAL, HINIC3_FLOW_ERROR_TYPE_ITEM, flow_item,
			"Not supported by sec fdir filter, ipv6 only support src ip,dst ip, proto");
		return -rte_errno;
	}

	if (tunnel_mode == HINIC3_FDIR_TUNNEL_MODE_NORMAL) {
		hinic3_sec_fdir_set_ipv6_addrs(
			filter->sec_fdir_filter.key_mask.ipv6.src_ip,
			filter->sec_fdir_filter.key_mask.ipv6.dst_ip,
			filter->sec_fdir_filter.key_spec.ipv6.src_ip,
			filter->sec_fdir_filter.key_spec.ipv6.dst_ip,
			&mask_ipv6->hdr, &spec_ipv6->hdr);
	} else {
		hinic3_sec_fdir_set_ipv6_addrs(
			filter->sec_fdir_filter.key_mask.inner_ipv6.src_ip,
			filter->sec_fdir_filter.key_mask.inner_ipv6.dst_ip,
			filter->sec_fdir_filter.key_spec.inner_ipv6.src_ip,
			filter->sec_fdir_filter.key_spec.inner_ipv6.dst_ip,
			&mask_ipv6->hdr, &spec_ipv6->hdr);
	}
	filter->sec_fdir_filter.key_mask.proto = mask_ipv6->hdr.proto;
	filter->sec_fdir_filter.key_spec.proto = spec_ipv6->hdr.proto;

	return 0;
}

static int
hinic3_flow_sec_fdir_tcp(const struct rte_flow_item *flow_item,
		     struct hinic3_filter_t	*filter,
		     struct rte_flow_error	*error,
		     enum hinic3_fdir_tunnel_mode tunnel_mode,
		     bool is_tunnel)
{
	const struct rte_flow_item_tcp *spec_tcp, *mask_tcp;
	bool is_outer = is_tunnel && (tunnel_mode == HINIC3_FDIR_TUNNEL_MODE_NORMAL);

	if (is_tunnel && tunnel_mode == HINIC3_FDIR_TUNNEL_MODE_NORMAL) {
		rte_flow_error_set(error, EINVAL, HINIC3_FLOW_ERROR_TYPE_ITEM, flow_item,
			"Not supported by sec fdir filter, vxlan/geneve only support inner tcp");
		return -rte_errno;
	}

	mask_tcp = (const struct rte_flow_item_tcp *)flow_item->mask;
	spec_tcp = (const struct rte_flow_item_tcp *)flow_item->spec;

	filter->sec_fdir_filter.key_mask.proto = HINIC3_UINT8_MAX;
	filter->sec_fdir_filter.key_spec.proto = IPPROTO_TCP;

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
	    mask_tcp->hdr.rx_win || mask_tcp->hdr.cksum ||mask_tcp->hdr.tcp_urp) {
		rte_flow_error_set(error, EINVAL, HINIC3_FLOW_ERROR_TYPE_ITEM, flow_item,
			"Not supported by sec fdir filter, tcp only support src port,dst port");
		return -rte_errno;
	}

	hinic3_sec_fdir_set_ports(&filter->sec_fdir_filter, is_outer,
		(u16)rte_be_to_cpu_16(mask_tcp->hdr.src_port),
		(u16)rte_be_to_cpu_16(spec_tcp->hdr.src_port),
		(u16)rte_be_to_cpu_16(mask_tcp->hdr.dst_port),
		(u16)rte_be_to_cpu_16(spec_tcp->hdr.dst_port));

	filter->sec_fdir_filter.tcp_flags_mask = mask_tcp->hdr.tcp_flags;
	filter->sec_fdir_filter.tcp_flags_spec = spec_tcp->hdr.tcp_flags;

	return 0;
}

static int
hinic3_flow_sec_fdir_udp(const struct rte_flow_item *flow_item,
		     struct hinic3_filter_t	*filter,
		     struct rte_flow_error	*error,
		     enum hinic3_fdir_tunnel_mode tunnel_mode,
		     bool is_tunnel)
{
	const struct rte_flow_item_udp *spec_udp, *mask_udp;
	bool is_outer = is_tunnel && (tunnel_mode == HINIC3_FDIR_TUNNEL_MODE_NORMAL);

	mask_udp = (const struct rte_flow_item_udp *)flow_item->mask;
	spec_udp = (const struct rte_flow_item_udp *)flow_item->spec;

	if (!is_outer) {
		filter->sec_fdir_filter.key_mask.proto = HINIC3_UINT8_MAX;
		filter->sec_fdir_filter.key_spec.proto = IPPROTO_UDP;
	}

	if (!mask_udp && !spec_udp)
		return 0;

	if (!mask_udp || !spec_udp) {
		rte_flow_error_set(error, EINVAL, HINIC3_FLOW_ERROR_TYPE_ITEM, flow_item,
			"Invalid fdir filter udp mask or spec");
		return -rte_errno;
	}

	hinic3_sec_fdir_set_ports(&filter->sec_fdir_filter, is_outer,
		(u16)rte_be_to_cpu_16(mask_udp->hdr.src_port),
		(u16)rte_be_to_cpu_16(spec_udp->hdr.src_port),
		(u16)rte_be_to_cpu_16(mask_udp->hdr.dst_port),
		(u16)rte_be_to_cpu_16(spec_udp->hdr.dst_port));

	return 0;
}

static int
hinic3_flow_sec_fdir_vxlan_geneve(struct rte_flow_error	  *error,
			      struct hinic3_filter_t	  *filter,
			      enum hinic3_fdir_tunnel_mode tunnel_mode,
			      const struct rte_flow_item  *flow_item)
{
	const struct rte_flow_item_vxlan *spec_vxlan, *mask_vxlan;
	uint32_t vxlan_vni_id = 0;
	uint32_t vxlan_vni_id_mask = 0;
	uint16_t expected_dport;
	uint16_t outer_dport_mask;
	uint16_t outer_dport_spec;
	uint8_t outer_proto_mask;
	uint8_t outer_proto_spec;

	spec_vxlan = (const struct rte_flow_item_vxlan *)flow_item->spec;
	mask_vxlan = (const struct rte_flow_item_vxlan *)flow_item->mask;

	filter->fdir_filter.tunnel_type = tunnel_mode;

	if (tunnel_mode == HINIC3_FDIR_TUNNEL_MODE_VXLAN)
		expected_dport = HINIC3_VXLAN_UDP_DPORT;
	else
		expected_dport = HINIC3_GENEVE_UDP_DPORT;

	outer_dport_mask = filter->sec_fdir_filter.outer_dport_mask;
	outer_dport_spec = filter->sec_fdir_filter.outer_dport_spec;
	outer_proto_mask = filter->sec_fdir_filter.outer_proto_mask;
	outer_proto_spec = filter->sec_fdir_filter.outer_proto_spec;

	if (outer_dport_mask != 0 &&
	    (outer_dport_spec & outer_dport_mask) != (expected_dport & outer_dport_mask)) {
		if (tunnel_mode == HINIC3_FDIR_TUNNEL_MODE_VXLAN) {
			rte_flow_error_set(error, EINVAL, HINIC3_FLOW_ERROR_TYPE_ITEM, flow_item,
				"Invalid sec fdir filter outer UDP dport, vxlan expects 4789");
		} else {
			rte_flow_error_set(error, EINVAL, HINIC3_FLOW_ERROR_TYPE_ITEM, flow_item,
				"Invalid sec fdir filter outer UDP dport, geneve expects 6081");
		}

		return -rte_errno;
	}

	if (outer_proto_mask != 0 &&
	    (outer_proto_spec & outer_proto_mask) != (IPPROTO_UDP & outer_proto_mask)) {
		rte_flow_error_set(error, EINVAL, HINIC3_FLOW_ERROR_TYPE_ITEM, flow_item,
			"Invalid sec fdir filter outer IP proto, vxlan/geneve expects UDP");
		return -rte_errno;
	}

	if (!spec_vxlan && !mask_vxlan)
		return 0;

	if (!spec_vxlan || !mask_vxlan) {
		rte_flow_error_set(error, EINVAL, HINIC3_FLOW_ERROR_TYPE_ITEM, flow_item,
			"Invalid sec fdir filter vxlan/geneve mask or spec");
		return -rte_errno;
	}

	rte_memcpy(((uint8_t *)&vxlan_vni_id + 1), spec_vxlan->vni, 3);
	filter->sec_fdir_filter.key_spec.tunnel.tunnel_id = rte_be_to_cpu_32(vxlan_vni_id);
	rte_memcpy(((uint8_t *)&vxlan_vni_id_mask + 1), mask_vxlan->vni, 3);
	filter->sec_fdir_filter.key_mask.tunnel.tunnel_id = rte_be_to_cpu_32(vxlan_vni_id_mask);

	return 0;
}

int
hinic3_flow_parse_sec_fdir_pattern(__rte_unused struct rte_eth_dev *dev,
                   const struct rte_flow_item      *pattern,
                   struct rte_flow_error          *error,
                   struct hinic3_filter_t         *filter)
{
    const struct rte_flow_item *flow_item = pattern;
    enum rte_flow_item_type type;
    int err;

	if (pattern == NULL) {
		rte_flow_error_set(error, EINVAL, HINIC3_FLOW_ERROR_TYPE_ITEM,
							NULL, "Invalid pattern");
		return -rte_errno;
	}
    enum hinic3_fdir_tunnel_mode tunnel_mode = HINIC3_FDIR_TUNNEL_MODE_NORMAL;
    bool is_tunnel = false;
    bool vlan_precessed = false;
    filter->sec_fdir_filter.ip_type = HINIC3_FDIR_IP_TYPE_ANY;
    filter->sec_fdir_filter.has_ip_flag = false;
    filter->sec_fdir_filter.outer_proto_mask = HINIC3_UINT8_MAX;
    filter->sec_fdir_filter.outer_proto_spec = IPPROTO_UDP;
    filter->fdir_filter.outer_ip_type = HINIC3_FDIR_IP_TYPE_ANY;
    filter->fdir_filter.tunnel_type = HINIC3_FDIR_TUNNEL_MODE_NORMAL;

    for (; flow_item->type != HINIC3_FLOW_ITEM_TYPE_END; flow_item++) {
        if (flow_item->type == HINIC3_FLOW_ITEM_TYPE_VXLAN ||
            flow_item->type == HINIC3_FLOW_ITEM_TYPE_GENEVE) {
            is_tunnel = true;
            break;
        }
    }
    flow_item = pattern;

    for (; flow_item->type != HINIC3_FLOW_ITEM_TYPE_END; flow_item++) {
        if (flow_item->last) {
            rte_flow_error_set(error, EINVAL, HINIC3_FLOW_ERROR_TYPE_ITEM,
                       flow_item, "Not support range");
            return -rte_errno;
        }
        type = flow_item->type;
        switch (type) {
        case HINIC3_FLOW_ITEM_TYPE_ETH:
            if (is_tunnel && tunnel_mode != HINIC3_FDIR_TUNNEL_MODE_NORMAL)
                break;
            err = hinic3_flow_sec_fdir_eth(flow_item, filter, error);
            if (err != 0)
                return -rte_errno;
            break;

        case HINIC3_FLOW_ITEM_TYPE_VLAN:
            if ((is_tunnel && tunnel_mode != HINIC3_FDIR_TUNNEL_MODE_NORMAL) || vlan_precessed)
                break;
            err = hinic3_flow_sec_fdir_vlan(flow_item, filter, error);
            if (err != 0)
                return -rte_errno;
	    vlan_precessed = true;
            break;

        case HINIC3_FLOW_ITEM_TYPE_IPV4:
            err = hinic3_flow_sec_fdir_ipv4(flow_item, filter, error,
                    tunnel_mode, is_tunnel);
            if (err != 0)
                return -rte_errno;
            break;

        case HINIC3_FLOW_ITEM_TYPE_IPV6:
            err = hinic3_flow_sec_fdir_ipv6(flow_item, filter, error,
                    tunnel_mode, is_tunnel);
            if (err != 0)
                return -rte_errno;
            break;

        case HINIC3_FLOW_ITEM_TYPE_TCP:
            err = hinic3_flow_sec_fdir_tcp(flow_item, filter, error,
                    tunnel_mode, is_tunnel);
            if (err != 0)
                return -rte_errno;
            break;

        case HINIC3_FLOW_ITEM_TYPE_UDP:
            err = hinic3_flow_sec_fdir_udp(flow_item, filter, error,
                    tunnel_mode, is_tunnel);
            if (err != 0)
                return -rte_errno;
            break;

        case HINIC3_FLOW_ITEM_TYPE_VXLAN:
            tunnel_mode = HINIC3_FDIR_TUNNEL_MODE_VXLAN;
            err = hinic3_flow_sec_fdir_vxlan_geneve(error, filter,
                    tunnel_mode, flow_item);
            if (err != 0)
                return -rte_errno;
            break;

        case HINIC3_FLOW_ITEM_TYPE_GENEVE:
            tunnel_mode = HINIC3_FDIR_TUNNEL_MODE_GENEVE;
            err = hinic3_flow_sec_fdir_vxlan_geneve(error, filter,
                    tunnel_mode, flow_item);
            if (err != 0)
                return -rte_errno;
            break;

        default:
            rte_flow_error_set(error, EINVAL, HINIC3_FLOW_ERROR_TYPE_ITEM,
                       flow_item, "Current item not support");
            return -rte_errno;
            break;
        }
    }

    return 0;
}

static void
hinic3_sec_fdir_tcam_key_init_ipv4(struct rte_eth_dev *dev,
				   const struct hinic3_sec_fdir_filter *sec_fdir,
				   struct hinic3_tcam_key *tcam_key)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct hinic3_tcam_sec_key_ipv4_mem *key_mask = &tcam_key->key_mask_sec;
	struct hinic3_tcam_sec_key_ipv4_mem *key_info = &tcam_key->key_info_sec;
	hinic3_sec_fdir_tcam_init_ipv4_common(nic_dev, sec_fdir,
					      key_mask, key_info);

	HINIC3_SEC_FDIR_TCAM_SET_U32_HL(key_mask, sipv4,
				       sec_fdir->key_mask.ipv4.src_ip);
	HINIC3_SEC_FDIR_TCAM_SET_U32_HL(key_info, sipv4,
				       sec_fdir->key_spec.ipv4.src_ip);
	HINIC3_SEC_FDIR_TCAM_SET_U32_HL(key_mask, dipv4,
				       sec_fdir->key_mask.ipv4.dst_ip);
	HINIC3_SEC_FDIR_TCAM_SET_U32_HL(key_info, dipv4,
				       sec_fdir->key_spec.ipv4.dst_ip);
}

static void
hinic3_sec_fdir_tcam_key_init_ipv6(struct rte_eth_dev *dev,
				   const struct hinic3_sec_fdir_filter *sec_fdir,
				   struct hinic3_tcam_key *tcam_key)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct hinic3_tcam_sec_key_ipv6_mem *key_mask =
		&tcam_key->key_mask_sec_ipv6;
	struct hinic3_tcam_sec_key_ipv6_mem *key_info =
		&tcam_key->key_info_sec_ipv6;
	hinic3_sec_fdir_tcam_init_ipv6_common(nic_dev, sec_fdir,
					      key_mask, key_info);

	HINIC3_SEC_FDIR_TCAM_SET_IPV6_ADDR(key_mask, sip,
					   sec_fdir->key_mask.ipv6.src_ip);
	HINIC3_SEC_FDIR_TCAM_SET_IPV6_ADDR(key_info, sip,
					   sec_fdir->key_spec.ipv6.src_ip);
	HINIC3_SEC_FDIR_TCAM_SET_IPV6_ADDR(key_mask, dip,
					   sec_fdir->key_mask.ipv6.dst_ip);
	HINIC3_SEC_FDIR_TCAM_SET_IPV6_ADDR(key_info, dip,
					   sec_fdir->key_spec.ipv6.dst_ip);

}

static void
hinic3_sec_fdir_tcam_key_init_ipv6_ipv4(struct rte_eth_dev *dev,
				   const struct hinic3_sec_fdir_filter *sec_fdir,
				   const struct hinic3_fdir_filter *rule,
				   struct hinic3_tcam_key *tcam_key)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct hinic3_tcam_sec_key_ipv6_ipv4_mem *key_mask =
		&tcam_key->key_mask_sec_ipv6_ipv4;
	struct hinic3_tcam_sec_key_ipv6_ipv4_mem *key_info =
		&tcam_key->key_info_sec_ipv6_ipv4;
	u16 vlan_spec = sec_fdir->vlan_tci_spec;
	u16 vlan_mask = sec_fdir->vlan_tci_mask;

	HINIC3_SEC_FDIR_TCAM_INIT_TUNNEL_COMMON(key_mask, key_info, sec_fdir,
		nic_dev, HINIC3_FDIR_IP_TYPE_IPV6, HINIC3_FDIR_IP_TYPE_IPV4,
		rule->tunnel_type, vlan_spec, vlan_mask);
	HINIC3_SEC_FDIR_TCAM_SET_MAC(key_mask, key_info, sec_fdir);

	HINIC3_SEC_FDIR_TCAM_SET_IPV6_ADDR(key_mask, outer_sip,
					   sec_fdir->key_mask.ipv6.src_ip);
	HINIC3_SEC_FDIR_TCAM_SET_IPV6_ADDR(key_info, outer_sip,
					   sec_fdir->key_spec.ipv6.src_ip);
	HINIC3_SEC_FDIR_TCAM_SET_IPV6_ADDR(key_mask, outer_dip,
					   sec_fdir->key_mask.ipv6.dst_ip);
	HINIC3_SEC_FDIR_TCAM_SET_IPV6_ADDR(key_info, outer_dip,
					   sec_fdir->key_spec.ipv6.dst_ip);

	HINIC3_SEC_FDIR_TCAM_SET_OUTER_PORTS(key_mask, key_info, sec_fdir);

	HINIC3_SEC_FDIR_TCAM_SET_U32_HL(key_mask, inner_sipv4,
				       sec_fdir->key_mask.inner_ipv4.src_ip);
	HINIC3_SEC_FDIR_TCAM_SET_U32_HL(key_info, inner_sipv4,
				       sec_fdir->key_spec.inner_ipv4.src_ip);
	HINIC3_SEC_FDIR_TCAM_SET_U32_HL(key_mask, inner_dipv4,
				       sec_fdir->key_mask.inner_ipv4.dst_ip);
	HINIC3_SEC_FDIR_TCAM_SET_U32_HL(key_info, inner_dipv4,
				       sec_fdir->key_spec.inner_ipv4.dst_ip);

	HINIC3_SEC_FDIR_TCAM_SET_INNER_PORTS(key_mask, key_info, sec_fdir);
	HINIC3_SEC_FDIR_TCAM_SET_VNI(key_mask, key_info, sec_fdir);
	HINIC3_SEC_FDIR_TCAM_SET_INNER_PROTO_TCP(key_mask, key_info, sec_fdir);
}

static void
hinic3_sec_fdir_tcam_key_init_ipv4_ipv6(struct rte_eth_dev *dev,
				   const struct hinic3_sec_fdir_filter *sec_fdir,
				   const struct hinic3_fdir_filter *rule,
				   struct hinic3_tcam_key *tcam_key)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct hinic3_tcam_sec_key_ipv4_ipv6_mem *key_mask =
		&tcam_key->key_mask_sec_ipv4_ipv6;
	struct hinic3_tcam_sec_key_ipv4_ipv6_mem *key_info =
		&tcam_key->key_info_sec_ipv4_ipv6;
	u16 vlan_spec = sec_fdir->vlan_tci_spec;
	u16 vlan_mask = sec_fdir->vlan_tci_mask;

	HINIC3_SEC_FDIR_TCAM_INIT_TUNNEL_COMMON(key_mask, key_info, sec_fdir,
		nic_dev, HINIC3_FDIR_IP_TYPE_IPV4, HINIC3_FDIR_IP_TYPE_IPV6,
		rule->tunnel_type, vlan_spec, vlan_mask);
	HINIC3_SEC_FDIR_TCAM_SET_MAC(key_mask, key_info, sec_fdir);

	HINIC3_SEC_FDIR_TCAM_SET_U32_HL(key_mask, sipv4,
				       sec_fdir->key_mask.ipv4.src_ip);
	HINIC3_SEC_FDIR_TCAM_SET_U32_HL(key_info, sipv4,
				       sec_fdir->key_spec.ipv4.src_ip);
	HINIC3_SEC_FDIR_TCAM_SET_U32_HL(key_mask, dipv4,
				       sec_fdir->key_mask.ipv4.dst_ip);
	HINIC3_SEC_FDIR_TCAM_SET_U32_HL(key_info, dipv4,
				       sec_fdir->key_spec.ipv4.dst_ip);

	HINIC3_SEC_FDIR_TCAM_SET_OUTER_PORTS(key_mask, key_info, sec_fdir);
	HINIC3_SEC_FDIR_TCAM_SET_VNI(key_mask, key_info, sec_fdir);

	HINIC3_SEC_FDIR_TCAM_SET_IPV6_ADDR(key_mask, inner_sip,
					   sec_fdir->key_mask.inner_ipv6.src_ip);
	HINIC3_SEC_FDIR_TCAM_SET_IPV6_ADDR(key_info, inner_sip,
					   sec_fdir->key_spec.inner_ipv6.src_ip);
	HINIC3_SEC_FDIR_TCAM_SET_IPV6_ADDR(key_mask, inner_dip,
					   sec_fdir->key_mask.inner_ipv6.dst_ip);
	HINIC3_SEC_FDIR_TCAM_SET_IPV6_ADDR(key_info, inner_dip,
					   sec_fdir->key_spec.inner_ipv6.dst_ip);

	HINIC3_SEC_FDIR_TCAM_SET_INNER_PORTS(key_mask, key_info, sec_fdir);
	HINIC3_SEC_FDIR_TCAM_SET_INNER_PROTO_TCP(key_mask, key_info, sec_fdir);
}

static void
hinic3_sec_fdir_tcam_key_init_ipv4_ipv4(struct rte_eth_dev *dev,
				   const struct hinic3_sec_fdir_filter *sec_fdir,
				   const struct hinic3_fdir_filter *rule,
				   struct hinic3_tcam_key *tcam_key)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct hinic3_tcam_sec_key_ipv4_ipv4_mem *key_mask =
		&tcam_key->key_mask_sec_ipv4_ipv4;
	struct hinic3_tcam_sec_key_ipv4_ipv4_mem *key_info =
		&tcam_key->key_info_sec_ipv4_ipv4;
	u16 vlan_spec = sec_fdir->vlan_tci_spec;
	u16 vlan_mask = sec_fdir->vlan_tci_mask;
	bool is_inner_any = (rule->ip_type == HINIC3_FDIR_IP_TYPE_ANY);

	HINIC3_SEC_FDIR_TCAM_INIT_TUNNEL_COMMON(key_mask, key_info, sec_fdir,
		nic_dev, HINIC3_FDIR_IP_TYPE_IPV4, HINIC3_FDIR_IP_TYPE_IPV4,
		rule->tunnel_type, vlan_spec, vlan_mask);
	HINIC3_SEC_FDIR_TCAM_SET_MAC(key_mask, key_info, sec_fdir);

	HINIC3_SEC_FDIR_TCAM_SET_U32_HL(key_mask, sipv4,
				       sec_fdir->key_mask.ipv4.src_ip);
	HINIC3_SEC_FDIR_TCAM_SET_U32_HL(key_info, sipv4,
				       sec_fdir->key_spec.ipv4.src_ip);
	HINIC3_SEC_FDIR_TCAM_SET_U32_HL(key_mask, dipv4,
				       sec_fdir->key_mask.ipv4.dst_ip);
	HINIC3_SEC_FDIR_TCAM_SET_U32_HL(key_info, dipv4,
				       sec_fdir->key_spec.ipv4.dst_ip);

	HINIC3_SEC_FDIR_TCAM_SET_OUTER_PORTS(key_mask, key_info, sec_fdir);
	HINIC3_SEC_FDIR_TCAM_SET_VNI(key_mask, key_info, sec_fdir);

	if (is_inner_any) {
		/* 内层IP类型不指定的情况，内层字段全填0 */
		key_mask->inner_ip_type = 0;
		key_info->inner_ip_type = 0;
		HINIC3_SEC_FDIR_TCAM_SET_U32_HL(key_mask, inner_sipv4, 0);
		HINIC3_SEC_FDIR_TCAM_SET_U32_HL(key_info, inner_sipv4, 0);
		HINIC3_SEC_FDIR_TCAM_SET_U32_HL(key_mask, inner_dipv4, 0);
		HINIC3_SEC_FDIR_TCAM_SET_U32_HL(key_info, inner_dipv4, 0);

		key_mask->inner_dport = 0;
		key_info->inner_dport = 0;
		key_mask->inner_sport = 0;
		key_info->inner_sport = 0;

		key_mask->inner_tcp_flag = 0;
		key_info->inner_tcp_flag = 0;
		key_mask->inner_ip_proto = 0;
		key_info->inner_ip_proto = 0;
	} else {
		HINIC3_SEC_FDIR_TCAM_SET_U32_HL(key_mask, inner_sipv4,
					       sec_fdir->key_mask.inner_ipv4.src_ip);
		HINIC3_SEC_FDIR_TCAM_SET_U32_HL(key_info, inner_sipv4,
					       sec_fdir->key_spec.inner_ipv4.src_ip);
		HINIC3_SEC_FDIR_TCAM_SET_U32_HL(key_mask, inner_dipv4,
					       sec_fdir->key_mask.inner_ipv4.dst_ip);
		HINIC3_SEC_FDIR_TCAM_SET_U32_HL(key_info, inner_dipv4,
					       sec_fdir->key_spec.inner_ipv4.dst_ip);

		HINIC3_SEC_FDIR_TCAM_SET_INNER_PORTS(key_mask, key_info, sec_fdir);
		HINIC3_SEC_FDIR_TCAM_SET_INNER_PROTO_TCP(key_mask, key_info, sec_fdir);
	}
}

static void
hinic3_sec_fdir_tcam_key_init_ipv6_ipv6(struct rte_eth_dev *dev,
				   const struct hinic3_sec_fdir_filter *sec_fdir,
				   const struct hinic3_fdir_filter *rule,
				   struct hinic3_tcam_key *tcam_key)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct hinic3_tcam_sec_key_ipv6_ipv6_mem *key_mask =
		&tcam_key->key_mask_sec_ipv6_ipv6;
	struct hinic3_tcam_sec_key_ipv6_ipv6_mem *key_info =
		&tcam_key->key_info_sec_ipv6_ipv6;
	u16 vlan_spec = sec_fdir->vlan_tci_spec;
	u16 vlan_mask = sec_fdir->vlan_tci_mask;

	HINIC3_SEC_FDIR_TCAM_INIT_TUNNEL_COMMON(key_mask, key_info, sec_fdir,
		nic_dev, HINIC3_FDIR_IP_TYPE_IPV6, HINIC3_FDIR_IP_TYPE_IPV6,
		rule->tunnel_type, vlan_spec, vlan_mask);

	key_mask->outer_sip2_h =
		HINIC3_32_UPPER_16_BITS(sec_fdir->key_mask.ipv6.src_ip[2]);
	key_mask->outer_sip2_l =
		HINIC3_32_LOWER_16_BITS(sec_fdir->key_mask.ipv6.src_ip[2]);
	key_mask->outer_sip3_h =
		HINIC3_32_UPPER_16_BITS(sec_fdir->key_mask.ipv6.src_ip[3]);
	key_mask->outer_sip3_l =
		HINIC3_32_LOWER_16_BITS(sec_fdir->key_mask.ipv6.src_ip[3]);
	key_mask->outer_sip1_l =
		HINIC3_32_LOWER_16_BITS(sec_fdir->key_mask.ipv6.src_ip[1]);
	key_mask->outer_sip1_h =
		(u8)((sec_fdir->key_mask.ipv6.src_ip[1] >> 16) & 0xFF);

	key_info->outer_sip2_h =
		HINIC3_32_UPPER_16_BITS(sec_fdir->key_spec.ipv6.src_ip[2]);
	key_info->outer_sip2_l =
		HINIC3_32_LOWER_16_BITS(sec_fdir->key_spec.ipv6.src_ip[2]);
	key_info->outer_sip3_h =
		HINIC3_32_UPPER_16_BITS(sec_fdir->key_spec.ipv6.src_ip[3]);
	key_info->outer_sip3_l =
		HINIC3_32_LOWER_16_BITS(sec_fdir->key_spec.ipv6.src_ip[3]);
	key_info->outer_sip1_l =
		HINIC3_32_LOWER_16_BITS(sec_fdir->key_spec.ipv6.src_ip[1]);
	key_info->outer_sip1_h =
		(u8)((sec_fdir->key_spec.ipv6.src_ip[1] >> 16) & 0xFF);

	key_mask->outer_dip2_h =
		HINIC3_32_UPPER_16_BITS(sec_fdir->key_mask.ipv6.dst_ip[2]);
	key_mask->outer_dip2_l =
		HINIC3_32_LOWER_16_BITS(sec_fdir->key_mask.ipv6.dst_ip[2]);
	key_mask->outer_dip3_h =
		HINIC3_32_UPPER_16_BITS(sec_fdir->key_mask.ipv6.dst_ip[3]);
	key_mask->outer_dip3_l =
		HINIC3_32_LOWER_16_BITS(sec_fdir->key_mask.ipv6.dst_ip[3]);
	key_mask->outer_dip3_l =
		HINIC3_32_LOWER_16_BITS(sec_fdir->key_mask.ipv6.dst_ip[1]);
	key_mask->outer_dip1_h =
		(u8)((sec_fdir->key_mask.ipv6.dst_ip[1] >> 16) & 0xFF);

	key_info->outer_dip2_h =
		HINIC3_32_UPPER_16_BITS(sec_fdir->key_spec.ipv6.dst_ip[2]);
	key_info->outer_dip2_l =
		HINIC3_32_LOWER_16_BITS(sec_fdir->key_spec.ipv6.dst_ip[2]);
	key_info->outer_dip3_h =
		HINIC3_32_UPPER_16_BITS(sec_fdir->key_spec.ipv6.dst_ip[3]);
	key_info->outer_dip3_l =
		HINIC3_32_LOWER_16_BITS(sec_fdir->key_spec.ipv6.dst_ip[3]);
	key_info->outer_dip1_l =
		HINIC3_32_LOWER_16_BITS(sec_fdir->key_spec.ipv6.dst_ip[1]);
	key_info->outer_dip1_h =
		(u8)((sec_fdir->key_spec.ipv6.dst_ip[1] >> 16) & 0xFF);

	HINIC3_SEC_FDIR_TCAM_SET_OUTER_PORTS(key_mask, key_info, sec_fdir);
	HINIC3_SEC_FDIR_TCAM_SET_VNI(key_mask, key_info, sec_fdir);

	HINIC3_SEC_FDIR_TCAM_SET_IPV6_ADDR(key_mask, inner_sip,
					   sec_fdir->key_mask.inner_ipv6.src_ip);
	HINIC3_SEC_FDIR_TCAM_SET_IPV6_ADDR(key_info, inner_sip,
					   sec_fdir->key_spec.inner_ipv6.src_ip);
	HINIC3_SEC_FDIR_TCAM_SET_IPV6_ADDR(key_mask, inner_dip,
					   sec_fdir->key_mask.inner_ipv6.dst_ip);
	HINIC3_SEC_FDIR_TCAM_SET_IPV6_ADDR(key_info, inner_dip,
					   sec_fdir->key_spec.inner_ipv6.dst_ip);

	HINIC3_SEC_FDIR_TCAM_SET_INNER_PORTS(key_mask, key_info, sec_fdir);
	HINIC3_SEC_FDIR_TCAM_SET_INNER_PROTO_TCP(key_mask, key_info, sec_fdir);
}

struct hinic3_sec_fdir_tcam_key_init_entry {
	u8 tunnel_type;
	u8 outer_ip_type;
	u8 inner_ip_type;
	void (*init)(struct rte_eth_dev *dev,
		     const struct hinic3_sec_fdir_filter *sec_fdir,
		     const struct hinic3_fdir_filter *fdir_ctrl,
		     struct hinic3_tcam_key *tcam_key);
};

static void hinic3_sec_fdir_tcam_key_init(struct rte_eth_dev *dev,
			      const struct hinic3_sec_fdir_filter *sec_rule,
			      __rte_unused const struct hinic3_fdir_filter *rule,
			      struct hinic3_tcam_key *tcam_key)
{
	size_t i;
	static const struct hinic3_sec_fdir_tcam_key_init_entry tcam_key_inits[] = {
		{
			HINIC3_FDIR_TUNNEL_MODE_VXLAN,
			HINIC3_FDIR_IP_TYPE_IPV6,
			HINIC3_FDIR_IP_TYPE_IPV4,
			hinic3_sec_fdir_tcam_key_init_ipv6_ipv4,
		},
		{
			HINIC3_FDIR_TUNNEL_MODE_VXLAN,
			HINIC3_FDIR_IP_TYPE_IPV6,
			HINIC3_FDIR_IP_TYPE_IPV6,
			hinic3_sec_fdir_tcam_key_init_ipv6_ipv6,
		},
		{
			HINIC3_FDIR_TUNNEL_MODE_VXLAN,
			HINIC3_FDIR_IP_TYPE_IPV4,
			HINIC3_FDIR_IP_TYPE_IPV6,
			hinic3_sec_fdir_tcam_key_init_ipv4_ipv6,
		},
		{
			HINIC3_FDIR_TUNNEL_MODE_VXLAN,
			HINIC3_FDIR_IP_TYPE_IPV4,
			HINIC3_FDIR_IP_TYPE_IPV4,
			hinic3_sec_fdir_tcam_key_init_ipv4_ipv4,
		},
		{
			HINIC3_FDIR_TUNNEL_MODE_GENEVE,
			HINIC3_FDIR_IP_TYPE_IPV6,
			HINIC3_FDIR_IP_TYPE_IPV4,
			hinic3_sec_fdir_tcam_key_init_ipv6_ipv4,
		},
		{
			HINIC3_FDIR_TUNNEL_MODE_GENEVE,
			HINIC3_FDIR_IP_TYPE_IPV6,
			HINIC3_FDIR_IP_TYPE_IPV6,
			hinic3_sec_fdir_tcam_key_init_ipv6_ipv6,
		},
		{
			HINIC3_FDIR_TUNNEL_MODE_GENEVE,
			HINIC3_FDIR_IP_TYPE_IPV4,
			HINIC3_FDIR_IP_TYPE_IPV6,
			hinic3_sec_fdir_tcam_key_init_ipv4_ipv6,
		},
		{
			HINIC3_FDIR_TUNNEL_MODE_GENEVE,
			HINIC3_FDIR_IP_TYPE_IPV4,
			HINIC3_FDIR_IP_TYPE_IPV4,
			hinic3_sec_fdir_tcam_key_init_ipv4_ipv4,
		},
	};

	for (i = 0; i < sizeof(tcam_key_inits) / sizeof(tcam_key_inits[0]); i++) {
		if (rule->tunnel_type == tcam_key_inits[i].tunnel_type &&
		    rule->outer_ip_type == tcam_key_inits[i].outer_ip_type &&
		    rule->ip_type == tcam_key_inits[i].inner_ip_type) {
			tcam_key_inits[i].init(dev, sec_rule, rule, tcam_key);
			return;
		}
	}

	/* 处理外层IPv4，内层IP类型不指定的情况 */
	if (rule->tunnel_type != HINIC3_FDIR_TUNNEL_MODE_NORMAL &&
	    rule->outer_ip_type == HINIC3_FDIR_IP_TYPE_IPV4 &&
	    rule->ip_type == HINIC3_FDIR_IP_TYPE_ANY) {
		hinic3_sec_fdir_tcam_key_init_ipv4_ipv4(dev, sec_rule, rule, tcam_key);
		return;
	}

	if (sec_rule->ip_type == HINIC3_FDIR_IP_TYPE_IPV6)
		hinic3_sec_fdir_tcam_key_init_ipv6(dev, sec_rule, tcam_key);
	else
		hinic3_sec_fdir_tcam_key_init_ipv4(dev, sec_rule, tcam_key);
}

static struct hinic3_tcam_dynamic_block *
hinic3_dynamic_lookup_sec_tcam_filter(struct rte_eth_dev *dev,
		struct hinic3_ext_tcam_cfg_rule *fdir_tcam_rule,
		struct hinic3_tcam_info *tcam_info,
		struct hinic3_tcam_filter *tcam_filter,
		u16 *tcam_index, u8 key_width)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct hinic3_tcam_dynamic_block *dynamic_block_ptr = NULL;
	struct hinic3_tcam_dynamic_block *tmp = NULL;
	int block_alloc_flag = 0;
	u16 dynamic_block_id = 0;
	u16 index;
	u16 block_index_range = HINIC3_TCAM_DYNAMIC_BLOCK_SIZE;
	u16 block_capacity = (key_width == HINIC3_FDIR_EXT_640) ? HINIC3_640_TCAM_DYNAMIC_BLOCK_SIZE : HINIC3_TCAM_DYNAMIC_BLOCK_SIZE;
	bool require_even_index = (key_width == HINIC3_FDIR_EXT_640);
	int err;

	TAILQ_FOREACH(tmp, &tcam_info->tcam_dynamic_info.tcam_dynamic_list, entries) {
		if (tmp->key_width != key_width)
			continue;
		if (tmp->dynamic_index_cnt >= block_capacity)
			continue;
		for (index = 0; index < block_index_range; index++) {
			if (require_even_index && (index & 0x1))
				continue;
			if (tmp->dynamic_index[index] == 0)
				break;
		}
		if (index < block_index_range)
			break;
	}

	if (tmp == NULL) {
		if (tcam_info->tcam_dynamic_info.dynamic_block_cnt >=
			(HINIC3_TCAM_DYNAMIC_MAX_FILTERS /
			block_capacity)) {
			PMD_DRV_LOG(ERR, "Dynamic tcam block is full, alloc failed!");
			goto failed;
		}

		err = hinic3_fdir_alloc_sec_tcam_block(nic_dev->hwdev, key_width, &dynamic_block_id);
		if (err) {
			PMD_DRV_LOG(ERR, "Fdir filter dynamic tcam alloc block failed!");
			goto failed;
		}

		block_alloc_flag = 1;

		dynamic_block_ptr =
			hinic3_alloc_dynamic_block_resource(tcam_info, dynamic_block_id, key_width, true);
		if (dynamic_block_ptr == NULL) {
			PMD_DRV_LOG(ERR, "Fdir filter dynamic alloc block memory failed!");
			goto block_alloc_failed;
		}
		tmp = dynamic_block_ptr;
		for (index = 0; index < block_index_range; index++) {
			if (require_even_index && (index & 0x1))
				continue;
			if (tmp->dynamic_index[index] == 0)
				break;
		}
	}

	if (index == block_index_range) {
		PMD_DRV_LOG(ERR, "tcam block 0x%x supports filter rules is full!",
			tmp->dynamic_block_id);
		goto look_up_failed;
	}

	tcam_filter->dynamic_block_id = tmp->dynamic_block_id;
	tcam_filter->index = index;
	*tcam_index = index;

	fdir_tcam_rule->index = HINIC3_PKT_TCAM_DYNAMIC_INDEX_START(tmp->dynamic_block_id) + index;

	return tmp;

look_up_failed:
	if (dynamic_block_ptr != NULL)
		hinic3_free_dynamic_block_resource(tcam_info, dynamic_block_ptr);

block_alloc_failed:
	if (block_alloc_flag == 1)
		(void)hinic3_fdir_sec_tcam_block_free(nic_dev->hwdev, key_width, &dynamic_block_id);

failed:
	return NULL;
}

static int hinic3_add_sec_tcam_filter(struct rte_eth_dev *dev,
				struct hinic3_tcam_key *tcam_key,
				struct hinic3_ext_tcam_cfg_rule *fdir_tcam_rule,
				u8 key_width)
{
	struct hinic3_tcam_info *tcam_info =
		HINIC3_DEV_PRIVATE_TO_TCAM_INFO(dev->data->dev_private);
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct hinic3_tcam_dynamic_block *dynamic_block_ptr = NULL;
	struct hinic3_tcam_dynamic_block *tmp = NULL;
	struct hinic3_tcam_filter *tcam_filter;
	u16 tcam_block_index = 0;
	u16 index = 0;
	u8 tcam_rule_type;
	int err;

	if (nic_dev->tcam_rule_nums + nic_dev->ethertype_rule_nums >= (HINIC3_SEC_TCAM_DYNAMIC_MAX_FILTERS / 2)) {
		PMD_DRV_LOG(ERR, "Rule number exceed 2K!");
			return EFAULT;
	}
	tcam_filter = rte_zmalloc("hinic3_fdir_filter",
			sizeof(struct hinic3_tcam_filter), 0);
	if (tcam_filter == NULL)
		return -ENOMEM;
	(void)rte_memcpy(&tcam_filter->tcam_key,
			 tcam_key, sizeof(struct hinic3_tcam_key));

	tcam_filter->queue = (u16)(fdir_tcam_rule->data.dw0.qid);
	if (nic_dev->tcam_rule_nums == 0) {
		err = hinic3_fdir_alloc_sec_tcam_block(nic_dev->hwdev, key_width, &tcam_block_index);
		if (err) {
			PMD_DRV_LOG(ERR, "Fdir filter tcam alloc block failed!");
			goto failed;
		}

		dynamic_block_ptr =
			hinic3_alloc_dynamic_block_resource(tcam_info, tcam_block_index, key_width, true);
		if (dynamic_block_ptr == NULL) {
			PMD_DRV_LOG(ERR, "Fdir filter alloc dynamic first block memory failed!");
			goto alloc_block_failed;
		}
	}

	tmp = hinic3_dynamic_lookup_sec_tcam_filter(dev,
		fdir_tcam_rule, tcam_info, tcam_filter, &index, key_width);
	if (tmp == NULL) {
		PMD_DRV_LOG(ERR, "Dynamic lookup tcam filter failed!");
		goto lookup_tcam_index_failed;
	}

	if (fdir_tcam_rule->data.dw1.bs.action != 0)
		tcam_rule_type =  TCAM_RULE_Q_GROUP_TYPE;
	else
		tcam_rule_type = TCAM_RULE_FDIR_TYPE;

	err = hinic3_fdir_add_sec_tcam_rule(nic_dev->hwdev, fdir_tcam_rule,
					    tcam_rule_type, key_width);
	if (err) {
		PMD_DRV_LOG(ERR, "Fdir_tcam_rule add failed!");
		goto add_tcam_rules_failed;
	}

	if (!(nic_dev->ethertype_rule_nums + nic_dev->tcam_rule_nums)) {
		err = hinic3_fdir_set_fdir_sec_tcam_rule_filter(nic_dev->hwdev, true);
		if (err)
			goto enable_failed;
	}

	TAILQ_INSERT_TAIL(&tcam_info->tcam_list, tcam_filter, entries);
	tmp->dynamic_index[index] = 1;
	tmp->dynamic_index_cnt++;
	nic_dev->tcam_rule_nums++;

	PMD_DRV_LOG(INFO, "Add fdir tcam rule, function_id: 0x%x, "
		    "tcam_block_id: %d, local_index: %d, global_index: %d, queue: %u, "
		    "tcam_rule_nums: %d succeed",
		    hinic3_global_func_id(nic_dev->hwdev),
		    tcam_filter->dynamic_block_id, index, fdir_tcam_rule->index,
		    fdir_tcam_rule->data.dw0.qid, nic_dev->tcam_rule_nums);

	return 0;

enable_failed:
	(void)hinic3_fdir_del_sec_tcam_rule(nic_dev->hwdev, fdir_tcam_rule->index,
					    TCAM_RULE_FDIR_TYPE, key_width);

add_tcam_rules_failed:
lookup_tcam_index_failed:
	if (nic_dev->tcam_rule_nums == 0 && dynamic_block_ptr != NULL)
		hinic3_free_dynamic_block_resource(tcam_info,
			dynamic_block_ptr);

alloc_block_failed:
	if (nic_dev->tcam_rule_nums == 0)
		(void)hinic3_fdir_sec_tcam_block_free(nic_dev->hwdev, key_width, &tcam_block_index);

failed:
	rte_free(tcam_filter);
	return -EFAULT;
}

static void
hinic3_fdir_sec_tcam_info_init(struct rte_eth_dev *dev,
			       const struct hinic3_sec_fdir_filter *sec_rule,
			       struct hinic3_fdir_filter *rule,
			       struct hinic3_tcam_key *tcam_key,
			       struct hinic3_ext_tcam_cfg_rule *fdir_tcam_rule)
{
	hinic3_sec_fdir_tcam_key_init(dev, sec_rule, rule, tcam_key);
	hinic3_fdir_tcam_action_init(dev, rule, (struct hinic3_tcam_cfg_rule *)fdir_tcam_rule);

	tcam_key_calculate(tcam_key, fdir_tcam_rule, HINIC3_SEC_TCAM_FLOW_KEY_SIZE);
}

static int
hinic3_del_sec_tcam_filter(struct rte_eth_dev *dev,
			   struct hinic3_tcam_filter *tcam_filter, u8 key_width)
{
	struct hinic3_tcam_info *tcam_info =
		HINIC3_DEV_PRIVATE_TO_TCAM_INFO(dev->data->dev_private);
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct hinic3_tcam_dynamic_block *tmp = NULL;
	u16 dynamic_block_id = tcam_filter->dynamic_block_id;
	u16 index;
	int err;

	TAILQ_FOREACH(tmp, &tcam_info->tcam_dynamic_info.tcam_dynamic_list,
			entries) {
		if (tmp->dynamic_block_id == dynamic_block_id)
			break;
			}

	if (tmp == NULL || tmp->dynamic_block_id != dynamic_block_id) {
		PMD_DRV_LOG(ERR, "Sec fdir del dynamic lookup for block failed!");
		return -EINVAL;
	}

	index = HINIC3_PKT_TCAM_DYNAMIC_INDEX_START(tmp->dynamic_block_id) + tcam_filter->index;

	err = hinic3_fdir_del_sec_tcam_rule(nic_dev->hwdev, index,
					    TCAM_RULE_FDIR_TYPE, key_width);
	if (err) {
		PMD_DRV_LOG(ERR, "Sec fdir tcam rule del failed!");
		return -EFAULT;
	}

	PMD_DRV_LOG(INFO, "Del sec fdir_tcam_dynamic_rule function_id: 0x%x, "
		    "tcam_block_id: %d, local_index: %d, global_index: %d, "
		    "local_rules_nums: %d, global_rule_nums: %d succeed",
		    hinic3_global_func_id(nic_dev->hwdev), dynamic_block_id,
		    tcam_filter->index, index, tmp->dynamic_index_cnt - 1,
		    nic_dev->tcam_rule_nums - 1);

	tmp->dynamic_index[tcam_filter->index] = 0;
	tmp->dynamic_index_cnt--;
	nic_dev->tcam_rule_nums--;
	if (tmp->dynamic_index_cnt == 0) {
		(void)hinic3_fdir_sec_tcam_block_free(nic_dev->hwdev,
						      key_width, &dynamic_block_id);
		hinic3_free_dynamic_block_resource(tcam_info, tmp);
	}

	if (!(nic_dev->ethertype_rule_nums + nic_dev->tcam_rule_nums))
		(void)hinic3_fdir_set_fdir_sec_tcam_rule_filter(nic_dev->hwdev, false);

	TAILQ_REMOVE(&tcam_info->tcam_list, tcam_filter, entries);
	rte_free(tcam_filter);

	return 0;
}

int hinic3_flow_add_del_sec_fdir_filter(struct rte_eth_dev *dev,
					struct hinic3_sec_fdir_filter *sec_fdir_filter,
					struct hinic3_fdir_filter *fdir_ctrl,
					bool add)
{
	struct hinic3_tcam_info *tcam_info =
		HINIC3_DEV_PRIVATE_TO_TCAM_INFO(dev->data->dev_private);
	struct hinic3_tcam_filter *tcam_filter;
	struct hinic3_ext_tcam_cfg_rule fdir_tcam_rule = {0};
	struct hinic3_tcam_key tcam_key = {0};
	u8 key_width;
	int ret;

	hinic3_fdir_sec_tcam_info_init(dev, sec_fdir_filter, fdir_ctrl,
				       &tcam_key, (struct hinic3_ext_tcam_cfg_rule *)&fdir_tcam_rule);
	if (fdir_ctrl->tunnel_type != HINIC3_FDIR_TUNNEL_MODE_NORMAL)
		key_width = HINIC3_FDIR_EXT_640;
	else
		key_width = (sec_fdir_filter->ip_type == HINIC3_FDIR_IP_TYPE_IPV6) ? HINIC3_FDIR_EXT_640 : HINIC3_FDIR_EXT_320;

	if (add) {
		tcam_filter = hinic3_tcam_filter_lookup(&tcam_info->tcam_list, &tcam_key,
							HINIC3_ACTION_ADD, HINIC3_INVALID_INDEX);
		if (tcam_filter != NULL) {
			PMD_DRV_LOG(ERR, "Sec fdir filter exists.");
			return -EEXIST;
		}

		ret = hinic3_add_sec_tcam_filter(dev, &tcam_key,
						 &fdir_tcam_rule, key_width);
		if (ret)
			goto cfg_tcam_filter_err;

		fdir_ctrl->tcam_index = (int)(fdir_tcam_rule.index);
	} else {
		tcam_filter = hinic3_tcam_filter_lookup(&tcam_info->tcam_list, &tcam_key,
							HINIC3_ACTION_NOT_ADD,
							fdir_ctrl->tcam_index);
		if (tcam_filter == NULL) {
			PMD_DRV_LOG(ERR, "Sec fdir filter doesn't exist.");
			return -ENOENT;
		}

		PMD_DRV_LOG(INFO, "begin to del sec fdir tcam filter");
		ret = hinic3_del_sec_tcam_filter(dev, tcam_filter, key_width);
		if (ret)
			goto cfg_tcam_filter_err;
	}

	return 0;

cfg_tcam_filter_err:
	return ret;
}