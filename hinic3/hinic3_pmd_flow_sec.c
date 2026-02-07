/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2026 Huawei Technologies Co., Ltd
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <rte_byteorder.h>
#include <rte_common.h>
#include <rte_ether.h>
#include <rte_ethdev.h>
#include <rte_flow.h>
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

    if (vlan_mask->inner_type) {
        filter->sec_fdir_filter.key_mask.ether_type =
            (u16)rte_be_to_cpu_16(vlan_mask->inner_type);
        filter->sec_fdir_filter.key_spec.ether_type =
            (u16)rte_be_to_cpu_16(vlan_spec->inner_type);
    }

    return 0;
}

static int
hinic3_flow_sec_fdir_ipv4(const struct rte_flow_item *flow_item,
		      struct hinic3_filter_t	 *filter,
		      struct rte_flow_error	 *error)
{
	const struct rte_flow_item_ipv4 *spec_ipv4, *mask_ipv4;

	mask_ipv4 = (const struct rte_flow_item_ipv4 *)flow_item->mask;
	spec_ipv4 = (const struct rte_flow_item_ipv4 *)flow_item->spec;

	filter->sec_fdir_filter.ip_type = HINIC3_FDIR_IP_TYPE_IPV4;
    filter->sec_fdir_filter.has_ip_flag = true;

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

	filter->sec_fdir_filter.key_mask.ipv4.src_ip = rte_be_to_cpu_32(mask_ipv4->hdr.src_addr);
	filter->sec_fdir_filter.key_spec.ipv4.src_ip = rte_be_to_cpu_32(spec_ipv4->hdr.src_addr);
	filter->sec_fdir_filter.key_mask.ipv4.dst_ip = rte_be_to_cpu_32(mask_ipv4->hdr.dst_addr);
	filter->sec_fdir_filter.key_spec.ipv4.dst_ip = rte_be_to_cpu_32(spec_ipv4->hdr.dst_addr);
	filter->sec_fdir_filter.key_mask.proto = mask_ipv4->hdr.next_proto_id;
	filter->sec_fdir_filter.key_spec.proto = spec_ipv4->hdr.next_proto_id;

	return 0;
}

static int
hinic3_flow_sec_fdir_ipv6(const struct rte_flow_item *flow_item,
		      struct hinic3_filter_t	 *filter,
		      struct rte_flow_error	 *error)
{
	const struct rte_flow_item_ipv6 *spec_ipv6, *mask_ipv6;

	mask_ipv6 = (const struct rte_flow_item_ipv6 *)flow_item->mask;
	spec_ipv6 = (const struct rte_flow_item_ipv6 *)flow_item->spec;

	filter->sec_fdir_filter.ip_type = HINIC3_FDIR_IP_TYPE_IPV6;

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
	net_addr_to_host(filter->sec_fdir_filter.key_mask.ipv6.src_ip, (const uint32_t *)mask_ipv6->hdr.src_addr.a, 4);
	net_addr_to_host(filter->sec_fdir_filter.key_spec.ipv6.src_ip, (const uint32_t *)spec_ipv6->hdr.src_addr.a, 4);
	net_addr_to_host(filter->sec_fdir_filter.key_mask.ipv6.dst_ip, (const uint32_t *)mask_ipv6->hdr.dst_addr.a, 4);
	net_addr_to_host(filter->sec_fdir_filter.key_spec.ipv6.dst_ip, (const uint32_t *)spec_ipv6->hdr.dst_addr.a, 4);
	#else
	net_addr_to_host(filter->sec_fdir_filter.key_mask.ipv6.src_ip, (const uint32_t *)mask_ipv6->hdr.src_addr, 4);
	net_addr_to_host(filter->sec_fdir_filter.key_spec.ipv6.src_ip, (const uint32_t *)spec_ipv6->hdr.src_addr, 4);
	net_addr_to_host(filter->sec_fdir_filter.key_mask.ipv6.dst_ip, (const uint32_t *)mask_ipv6->hdr.dst_addr, 4);
	net_addr_to_host(filter->sec_fdir_filter.key_spec.ipv6.dst_ip, (const uint32_t *)spec_ipv6->hdr.dst_addr, 4);
	#endif
	filter->sec_fdir_filter.key_mask.proto = mask_ipv6->hdr.proto;
	filter->sec_fdir_filter.key_spec.proto = spec_ipv6->hdr.proto;

	return 0;
}

static int
hinic3_flow_sec_fdir_tcp(const struct rte_flow_item *flow_item,
		     struct hinic3_filter_t	*filter,
		     struct rte_flow_error	*error)
{
	const struct rte_flow_item_tcp *spec_tcp, *mask_tcp;

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
			"Not supported by fdir filter, tcp only support src port,dst port");
		return -rte_errno;
	}
	filter->sec_fdir_filter.key_mask.src_port = (u16)rte_be_to_cpu_16(mask_tcp->hdr.src_port);
	filter->sec_fdir_filter.key_spec.src_port = (u16)rte_be_to_cpu_16(spec_tcp->hdr.src_port);
	filter->sec_fdir_filter.key_mask.dst_port = (u16)rte_be_to_cpu_16(mask_tcp->hdr.dst_port);
	filter->sec_fdir_filter.key_spec.dst_port = (u16)rte_be_to_cpu_16(spec_tcp->hdr.dst_port);
    filter->sec_fdir_filter.tcp_flags_mask = mask_tcp->hdr.tcp_flags;
    filter->sec_fdir_filter.tcp_flags_spec = spec_tcp->hdr.tcp_flags;

	return 0;
}

static int
hinic3_flow_sec_fdir_udp(const struct rte_flow_item *flow_item,
		     struct hinic3_filter_t	*filter,
		     struct rte_flow_error	*error)
{
	const struct rte_flow_item_udp *spec_udp, *mask_udp;

	mask_udp = (const struct rte_flow_item_udp *)flow_item->mask;
	spec_udp = (const struct rte_flow_item_udp *)flow_item->spec;

	filter->sec_fdir_filter.key_mask.proto = HINIC3_UINT8_MAX;
	filter->sec_fdir_filter.key_spec.proto = IPPROTO_UDP;

	if (!mask_udp && !spec_udp)
		return 0;

	if (!mask_udp || !spec_udp) {
		rte_flow_error_set(error, EINVAL, HINIC3_FLOW_ERROR_TYPE_ITEM, flow_item,
			"Invalid fdir filter udp mask or spec");
		return -rte_errno;
	}
	filter->sec_fdir_filter.key_mask.src_port = (u16)rte_be_to_cpu_16(mask_udp->hdr.src_port);
	filter->sec_fdir_filter.key_spec.src_port = (u16)rte_be_to_cpu_16(spec_udp->hdr.src_port);
	filter->sec_fdir_filter.key_mask.dst_port = (u16)rte_be_to_cpu_16(mask_udp->hdr.dst_port);
	filter->sec_fdir_filter.key_spec.dst_port = (u16)rte_be_to_cpu_16(spec_udp->hdr.dst_port);

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

    enum hinic3_fdir_tunnel_mode tunnel_mode = HINIC3_FDIR_TUNNEL_MODE_NORMAL;
    filter->sec_fdir_filter.ip_type = HINIC3_FDIR_IP_TYPE_ANY;
    filter->sec_fdir_filter.has_ip_flag = false;

    for (; flow_item->type != HINIC3_FLOW_ITEM_TYPE_END; flow_item++) {
        if (flow_item->last) {
            rte_flow_error_set(error, EINVAL, HINIC3_FLOW_ERROR_TYPE_ITEM,
                       flow_item, "Not support range");
            return -rte_errno;
        }
        type = flow_item->type;
        switch (type) {
        case HINIC3_FLOW_ITEM_TYPE_ETH:
            err = hinic3_flow_sec_fdir_eth(flow_item, filter, error);
            if (err != 0)
                return -rte_errno;
            break;

        case HINIC3_FLOW_ITEM_TYPE_VLAN:
            err = hinic3_flow_sec_fdir_vlan(flow_item, filter, error);
            if (err != 0)
                return -rte_errno;
            break;

        case HINIC3_FLOW_ITEM_TYPE_IPV4:
            err = hinic3_flow_sec_fdir_ipv4(flow_item, filter, error);
            if (err != 0)
                return -rte_errno;
            break;

        case HINIC3_FLOW_ITEM_TYPE_IPV6:
            err = hinic3_flow_sec_fdir_ipv6(flow_item, filter, error);
            if (err != 0)
                return -rte_errno;
            break;

        case HINIC3_FLOW_ITEM_TYPE_TCP:
            err = hinic3_flow_sec_fdir_tcp(flow_item, filter, error);
            if (err != 0)
                return -rte_errno;
            break;

        case HINIC3_FLOW_ITEM_TYPE_UDP:
            err = hinic3_flow_sec_fdir_udp(flow_item, filter, error);
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
	u16 dmac_h, dmac_m, dmac_l;
	u16 smac_h, smac_m, smac_l;
	u16 dmac_h_mask, dmac_m_mask, dmac_l_mask;
	u16 smac_h_mask, smac_m_mask, smac_l_mask;
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

	if (sec_fdir->has_vlan || vlan_mask != 0) {
		key_mask->vlan_flag = HINIC3_UINT1_MAX;
		key_info->vlan_flag = sec_fdir->has_vlan ? 1 : 0;

		key_mask->vlan_vid = vlan_mask & RTE_VLAN_ID_MASK;
		key_mask->vlan_cfi =
			(u16)((vlan_mask & RTE_VLAN_CFI_MASK) >> RTE_VLAN_CFI_SHIFT);
		key_mask->vlan_pri =
			(u16)((vlan_mask & RTE_VLAN_PRI_MASK) >> RTE_VLAN_PRI_SHIFT);

		key_info->vlan_vid = vlan_spec & RTE_VLAN_ID_MASK;
		key_info->vlan_cfi =
			(u16)((vlan_spec & RTE_VLAN_CFI_MASK) >> RTE_VLAN_CFI_SHIFT);
		key_info->vlan_pri =
			(u16)((vlan_spec & RTE_VLAN_PRI_MASK) >> RTE_VLAN_PRI_SHIFT);
	}

	hinic3_sec_fdir_mac_to_u16(&HINIC3_ETHER_HDR_DST_ADDR(&sec_fdir->ether_mask),
				   &dmac_h_mask, &dmac_m_mask, &dmac_l_mask);
	hinic3_sec_fdir_mac_to_u16(&HINIC3_ETHER_HDR_SRC_ADDR(&sec_fdir->ether_mask),
				   &smac_h_mask, &smac_m_mask, &smac_l_mask);
	hinic3_sec_fdir_mac_to_u16(&HINIC3_ETHER_HDR_DST_ADDR(&sec_fdir->ether_spec),
				   &dmac_h, &dmac_m, &dmac_l);
	hinic3_sec_fdir_mac_to_u16(&HINIC3_ETHER_HDR_SRC_ADDR(&sec_fdir->ether_spec),
				   &smac_h, &smac_m, &smac_l);

	key_mask->dmac_h = dmac_h_mask;
	key_mask->dmac_m = dmac_m_mask;
	key_mask->dmac_l = dmac_l_mask;
	key_mask->smac_h = smac_h_mask;
	key_mask->smac_m = smac_m_mask;
	key_mask->smac_l = smac_l_mask;

	key_info->dmac_h = dmac_h;
	key_info->dmac_m = dmac_m;
	key_info->dmac_l = dmac_l;
	key_info->smac_h = smac_h;
	key_info->smac_m = smac_m;
	key_info->smac_l = smac_l;

	key_mask->eth_type = sec_fdir->key_mask.ether_type;
	key_info->eth_type = sec_fdir->key_spec.ether_type;

	key_mask->tcp_flag = sec_fdir->tcp_flags_mask;
	key_info->tcp_flag = sec_fdir->tcp_flags_spec;

	key_mask->ip_proto = sec_fdir->key_mask.proto;
	key_info->ip_proto = sec_fdir->key_spec.proto;

	key_mask->sipv4_h =
		HINIC3_32_UPPER_16_BITS(sec_fdir->key_mask.ipv4.src_ip);
	key_mask->sipv4_l =
		HINIC3_32_LOWER_16_BITS(sec_fdir->key_mask.ipv4.src_ip);
	key_info->sipv4_h =
		HINIC3_32_UPPER_16_BITS(sec_fdir->key_spec.ipv4.src_ip);
	key_info->sipv4_l =
		HINIC3_32_LOWER_16_BITS(sec_fdir->key_spec.ipv4.src_ip);

	key_mask->dipv4_h =
		HINIC3_32_UPPER_16_BITS(sec_fdir->key_mask.ipv4.dst_ip);
	key_mask->dipv4_l =
		HINIC3_32_LOWER_16_BITS(sec_fdir->key_mask.ipv4.dst_ip);
	key_info->dipv4_h =
		HINIC3_32_UPPER_16_BITS(sec_fdir->key_spec.ipv4.dst_ip);
	key_info->dipv4_l =
		HINIC3_32_LOWER_16_BITS(sec_fdir->key_spec.ipv4.dst_ip);

	key_mask->dport = sec_fdir->key_mask.dst_port;
	key_info->dport = sec_fdir->key_spec.dst_port;
	key_mask->sport = sec_fdir->key_mask.src_port;
	key_info->sport = sec_fdir->key_spec.src_port;
}

static void hinic3_sec_fdir_tcam_key_init(struct rte_eth_dev *dev,
			      const struct hinic3_sec_fdir_filter *sec_rule,
			      __rte_unused const struct hinic3_fdir_filter *rule,
			      struct hinic3_tcam_key *tcam_key)
{
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
	u16 block_size = (key_width == HINIC3_FDIR_EXT_640) ? HINIC3_640_TCAM_DYNAMIC_BLOCK_SIZE : HINIC3_TCAM_DYNAMIC_BLOCK_SIZE;
	bool require_even_index = (key_width == HINIC3_FDIR_EXT_640);
	int err;

	TAILQ_FOREACH(tmp, &tcam_info->tcam_dynamic_info.tcam_dynamic_list, entries) {
		if (tmp->key_width != key_width)
			continue;
		if (tmp->dynamic_index_cnt >= block_size)
			continue;
		for (index = 0; index < block_size; index++) {
			if (require_even_index && (index & 0x1))
				continue;
			if (tmp->dynamic_index[index] == 0)
				break;
		}
		if (index < block_size)
			break;
	}

	if (tmp == NULL) {
		if (tcam_info->tcam_dynamic_info.dynamic_block_cnt >=
			(HINIC3_TCAM_DYNAMIC_MAX_FILTERS /
			block_size)) {
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
		for (index = 0; index < block_size; index++) {
			if (require_even_index && (index & 0x1))
				continue;
			if (tmp->dynamic_index[index] == 0)
				break;
		}
	}

	if (index == block_size) {
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
				bool is_hairpin, u8 key_width)
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
					    tcam_rule_type, is_hairpin, key_width);
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
						 &fdir_tcam_rule, fdir_ctrl->is_hairpin,
						 key_width);
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