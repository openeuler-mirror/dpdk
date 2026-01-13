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
#include "base/hinic3_pmd_hwif.h"
#include "base/hinic3_pmd_mgmt.h"
#include "base/hinic3_pmd_nic_cfg.h"
#include "base/hinic3_pmd_hw_cfg.h"
#include "hinic3_pmd_ethdev.h"
#include "hinic3_pmd_csr.h"
#ifdef HINIC3_TRAFFIC_BIFUR
#include "hinic3_pmd_bifur.h"
#endif
#include "hinic3_pmd_fdir.h"

#define HINIC3_UINT1_MAX          0x1
#define HINIC3_UINT2_MAX          0x3
#define HINIC3_UINT4_MAX          0xf
#define HINIC3_UINT15_MAX         0x7fff
#define BIFUR_EN              0x2

#define HINIC3_INVALID_INDEX      -1

#define HINIC3_DEV_PRIVATE_TO_TCAM_INFO(nic_dev) \
	(&((struct hinic3_nic_dev *)(nic_dev))->tcam)

static void tcam_translate_key_y(u8 *key_y, u8 *src_input, u8 *mask, u8 len)
{
	u8 idx;

	for (idx = 0; idx < len; idx++)
		key_y[idx] = src_input[idx] & mask[idx];
}

static void tcam_translate_key_x(u8 *key_x, u8 *key_y, u8 *mask, u8 len)
{
	u8 idx;

	for (idx = 0; idx < len; idx++)
		key_x[idx] = key_y[idx] ^ mask[idx];
}

static void tcam_key_calculate(struct hinic3_tcam_key *tcam_key,
			struct hinic3_tcam_cfg_rule *fdir_tcam_rule)
{
	tcam_translate_key_y(fdir_tcam_rule->key.y,
		(u8 *)(&tcam_key->key_info),
		(u8 *)(&tcam_key->key_mask),
		HINIC3_TCAM_FLOW_KEY_SIZE);
	tcam_translate_key_x(fdir_tcam_rule->key.x,
		fdir_tcam_rule->key.y,
		(u8 *)(&tcam_key->key_mask),
		HINIC3_TCAM_FLOW_KEY_SIZE);
}


#ifdef HINIC3_TRAFFIC_BIFUR
static void hinic3_fdir_tcam_ipv4_init(struct hinic3_fdir_filter *rule,
			struct hinic3_tcam_key *tcam_key, struct rte_eth_dev *dev)
#else
static void hinic3_fdir_tcam_ipv4_init(struct hinic3_fdir_filter *rule,
			struct hinic3_tcam_key *tcam_key)
#endif
{
	tcam_key->key_mask.ip_type = HINIC3_UINT1_MAX;
	tcam_key->key_info.ip_type = HINIC3_FDIR_IP_TYPE_IPV4;

#ifdef HINIC3_TRAFFIC_BIFUR
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct rte_pci_device *pci_dev = NULL;
	pci_dev = RTE_ETH_DEV_TO_PCI(dev);

	if (pci_dev->id.device_id == HINIC3_DEV_ID_DPU || hinic3_bifur_is_shared_dev(nic_dev->hwdev->pci_dev)) {
		/* VF RSS flow table traffic distribution */
		tcam_key->key_mask.bifur_flag = HINIC3_UINT2_MAX;
		tcam_key->key_info.bifur_flag = BIFUR_EN;
	} else {
		/* FDIR flow table traffic distribution */
		tcam_key->key_mask.bifur_flag = 0;
        tcam_key->key_info.bifur_flag = 0;
	}
#endif

	tcam_key->key_mask.sipv4_h =
		HINIC3_32_UPPER_16_BITS(rule->key_mask.ipv4.src_ip);
	tcam_key->key_mask.sipv4_l =
		HINIC3_32_LOWER_16_BITS(rule->key_mask.ipv4.src_ip);
	tcam_key->key_info.sipv4_h =
		HINIC3_32_UPPER_16_BITS(rule->key_spec.ipv4.src_ip);
	tcam_key->key_info.sipv4_l =
		HINIC3_32_LOWER_16_BITS(rule->key_spec.ipv4.src_ip);

	tcam_key->key_mask.dipv4_h =
		HINIC3_32_UPPER_16_BITS(rule->key_mask.ipv4.dst_ip);
	tcam_key->key_mask.dipv4_l =
		HINIC3_32_LOWER_16_BITS(rule->key_mask.ipv4.dst_ip);
	tcam_key->key_info.dipv4_h =
		HINIC3_32_UPPER_16_BITS(rule->key_spec.ipv4.dst_ip);
	tcam_key->key_info.dipv4_l =
		HINIC3_32_LOWER_16_BITS(rule->key_spec.ipv4.dst_ip);

}

#ifdef HINIC3_TRAFFIC_BIFUR
static void hinic3_fdir_tcam_ipv6_init(struct hinic3_fdir_filter *rule,
			struct hinic3_tcam_key *tcam_key, struct rte_eth_dev *dev)
#else
static void hinic3_fdir_tcam_ipv6_init(struct hinic3_fdir_filter *rule,
			struct hinic3_tcam_key *tcam_key)
#endif
{
	tcam_key->key_mask_ipv6.ip_type = HINIC3_UINT1_MAX;
	tcam_key->key_info_ipv6.ip_type = HINIC3_FDIR_IP_TYPE_IPV6;

#ifdef HINIC3_TRAFFIC_BIFUR
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct rte_pci_device *pci_dev = NULL;
	pci_dev = RTE_ETH_DEV_TO_PCI(dev);

	/* ipv6 bifur_flag*/
    if (pci_dev->id.device_id == HINIC3_DEV_ID_DPU || hinic3_bifur_is_shared_dev(nic_dev->hwdev->pci_dev)) {
		/* VF RSS flow table traffic distribution */
		tcam_key->key_mask_ipv6.bifur_flag = HINIC3_UINT2_MAX;
		tcam_key->key_info_ipv6.bifur_flag = BIFUR_EN;
	} else {
		/* FDIR flow table traffic distribution */
		tcam_key->key_mask_ipv6.bifur_flag = 0;
        tcam_key->key_info_ipv6.bifur_flag = 0;
	}
#endif

	tcam_key->key_mask_ipv6.sipv6_key0 =
		HINIC3_32_UPPER_16_BITS(rule->key_mask.ipv6.src_ip[0]);
	tcam_key->key_mask_ipv6.sipv6_key1 =
		HINIC3_32_LOWER_16_BITS(rule->key_mask.ipv6.src_ip[0]);
	tcam_key->key_mask_ipv6.sipv6_key2 =
		HINIC3_32_UPPER_16_BITS(rule->key_mask.ipv6.src_ip[0x1]);
	tcam_key->key_mask_ipv6.sipv6_key3 =
		HINIC3_32_LOWER_16_BITS(rule->key_mask.ipv6.src_ip[0x1]);
	tcam_key->key_mask_ipv6.sipv6_key4 =
		HINIC3_32_UPPER_16_BITS(rule->key_mask.ipv6.src_ip[0x2]);
	tcam_key->key_mask_ipv6.sipv6_key5 =
		HINIC3_32_LOWER_16_BITS(rule->key_mask.ipv6.src_ip[0x2]);
	tcam_key->key_mask_ipv6.sipv6_key6 =
		HINIC3_32_UPPER_16_BITS(rule->key_mask.ipv6.src_ip[0x3]);
	tcam_key->key_mask_ipv6.sipv6_key7 =
		HINIC3_32_LOWER_16_BITS(rule->key_mask.ipv6.src_ip[0x3]);
	tcam_key->key_info_ipv6.sipv6_key0 =
		HINIC3_32_UPPER_16_BITS(rule->key_spec.ipv6.src_ip[0]);
	tcam_key->key_info_ipv6.sipv6_key1 =
		HINIC3_32_LOWER_16_BITS(rule->key_spec.ipv6.src_ip[0]);
	tcam_key->key_info_ipv6.sipv6_key2 =
		HINIC3_32_UPPER_16_BITS(rule->key_spec.ipv6.src_ip[0x1]);
	tcam_key->key_info_ipv6.sipv6_key3 =
		HINIC3_32_LOWER_16_BITS(rule->key_spec.ipv6.src_ip[0x1]);
	tcam_key->key_info_ipv6.sipv6_key4 =
		HINIC3_32_UPPER_16_BITS(rule->key_spec.ipv6.src_ip[0x2]);
	tcam_key->key_info_ipv6.sipv6_key5 =
		HINIC3_32_LOWER_16_BITS(rule->key_spec.ipv6.src_ip[0x2]);
	tcam_key->key_info_ipv6.sipv6_key6 =
		HINIC3_32_UPPER_16_BITS(rule->key_spec.ipv6.src_ip[0x3]);
	tcam_key->key_info_ipv6.sipv6_key7 =
		HINIC3_32_LOWER_16_BITS(rule->key_spec.ipv6.src_ip[0x3]);

	tcam_key->key_mask_ipv6.dipv6_key0 =
		HINIC3_32_UPPER_16_BITS(rule->key_mask.ipv6.dst_ip[0]);
	tcam_key->key_mask_ipv6.dipv6_key1 =
		HINIC3_32_LOWER_16_BITS(rule->key_mask.ipv6.dst_ip[0]);
	tcam_key->key_mask_ipv6.dipv6_key2 =
		HINIC3_32_UPPER_16_BITS(rule->key_mask.ipv6.dst_ip[0x1]);
	tcam_key->key_mask_ipv6.dipv6_key3 =
		HINIC3_32_LOWER_16_BITS(rule->key_mask.ipv6.dst_ip[0x1]);
	tcam_key->key_mask_ipv6.dipv6_key4 =
		HINIC3_32_UPPER_16_BITS(rule->key_mask.ipv6.dst_ip[0x2]);
	tcam_key->key_mask_ipv6.dipv6_key5 =
		HINIC3_32_LOWER_16_BITS(rule->key_mask.ipv6.dst_ip[0x2]);
	tcam_key->key_mask_ipv6.dipv6_key6 =
		HINIC3_32_UPPER_16_BITS(rule->key_mask.ipv6.dst_ip[0x3]);
	tcam_key->key_mask_ipv6.dipv6_key7 =
		HINIC3_32_LOWER_16_BITS(rule->key_mask.ipv6.dst_ip[0x3]);
	tcam_key->key_info_ipv6.dipv6_key0 =
		HINIC3_32_UPPER_16_BITS(rule->key_spec.ipv6.dst_ip[0]);
	tcam_key->key_info_ipv6.dipv6_key1 =
		HINIC3_32_LOWER_16_BITS(rule->key_spec.ipv6.dst_ip[0]);
	tcam_key->key_info_ipv6.dipv6_key2 =
		HINIC3_32_UPPER_16_BITS(rule->key_spec.ipv6.dst_ip[0x1]);
	tcam_key->key_info_ipv6.dipv6_key3 =
		HINIC3_32_LOWER_16_BITS(rule->key_spec.ipv6.dst_ip[0x1]);
	tcam_key->key_info_ipv6.dipv6_key4 =
		HINIC3_32_UPPER_16_BITS(rule->key_spec.ipv6.dst_ip[0x2]);
	tcam_key->key_info_ipv6.dipv6_key5 =
		HINIC3_32_LOWER_16_BITS(rule->key_spec.ipv6.dst_ip[0x2]);
	tcam_key->key_info_ipv6.dipv6_key6 =
		HINIC3_32_UPPER_16_BITS(rule->key_spec.ipv6.dst_ip[0x3]);
	tcam_key->key_info_ipv6.dipv6_key7 =
		HINIC3_32_LOWER_16_BITS(rule->key_spec.ipv6.dst_ip[0x3]);
}

static void hinic3_fdir_tcam_notunnel_init(struct rte_eth_dev *dev,
			struct hinic3_fdir_filter *rule,
			struct hinic3_tcam_key *tcam_key)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);

	tcam_key->key_mask.sport = rule->key_mask.src_port;
	tcam_key->key_info.sport = rule->key_spec.src_port;

	tcam_key->key_mask.dport = rule->key_mask.dst_port;
	tcam_key->key_info.dport = rule->key_spec.dst_port;

	tcam_key->key_mask.tunnel_type = HINIC3_UINT4_MAX;
	tcam_key->key_info.tunnel_type = HINIC3_FDIR_TUNNEL_MODE_NORMAL;

	tcam_key->key_mask.function_id = HINIC3_UINT15_MAX;

	tcam_key->key_mask.vlan_flag = 1;
	tcam_key->key_info.vlan_flag = 0;
#ifdef HINIC3_TRAFFIC_BIFUR
    u8 bifur_en, iso_en;
    u8 er_id = nic_dev->hwdev->cfg_mgmt->svc_cap.er_id;
    if (hinic3_get_bifur_enable(nic_dev->hwdev, &bifur_en, &iso_en) != 0) {
        PMD_DRV_LOG(ERR, "hinic3 get port table bifur enable staus failed!");
    }

    if (bifur_en) {
		tcam_key->key_info.function_id = HINIC3_UINT15_MAX;
		tcam_key->key_mask.ether_type = rule->key_mask.ether_type;
		tcam_key->key_info.ether_type = rule->key_spec.ether_type;
		tcam_key->key_info.vlan_flag = 1;
    } else {
        tcam_key->key_info.function_id =
            hinic3_global_func_id(nic_dev->hwdev) & HINIC3_UINT15_MAX;
    }

    if (iso_en) {
        tcam_key->key_mask.er_id = HINIC3_UINT4_MAX;
        tcam_key->key_info.er_id = er_id;
    }
#else
	tcam_key->key_info.function_id =
		hinic3_global_func_id(nic_dev->hwdev) & HINIC3_UINT15_MAX;
#endif
	tcam_key->key_mask.ip_proto = rule->key_mask.proto;
	tcam_key->key_info.ip_proto = rule->key_spec.proto;

	if (rule->ip_type == HINIC3_FDIR_IP_TYPE_IPV4)
#ifdef HINIC3_TRAFFIC_BIFUR
 		hinic3_fdir_tcam_ipv4_init(rule, tcam_key, dev);
#else
		hinic3_fdir_tcam_ipv4_init(rule, tcam_key);
#endif
	else if (rule->ip_type == HINIC3_FDIR_IP_TYPE_IPV6)
#ifdef HINIC3_TRAFFIC_BIFUR
		hinic3_fdir_tcam_ipv6_init(rule, tcam_key, dev);
#else
		hinic3_fdir_tcam_ipv6_init(rule, tcam_key);
#endif
}

static void
hinic3_fdir_tcam_vxlan_geneve_ipv4_init(struct hinic3_fdir_filter *rule,
					struct hinic3_tcam_key	  *tcam_key)
{
	tcam_key->key_mask.ip_type = HINIC3_UINT1_MAX;
	tcam_key->key_info.ip_type = HINIC3_FDIR_IP_TYPE_IPV4;

	tcam_key->key_mask.sipv4_h =
		HINIC3_32_UPPER_16_BITS(rule->key_mask.inner_ipv4.src_ip);
	tcam_key->key_mask.sipv4_l =
		HINIC3_32_LOWER_16_BITS(rule->key_mask.inner_ipv4.src_ip);
	tcam_key->key_info.sipv4_h =
		HINIC3_32_UPPER_16_BITS(rule->key_spec.inner_ipv4.src_ip);
	tcam_key->key_info.sipv4_l =
		HINIC3_32_LOWER_16_BITS(rule->key_spec.inner_ipv4.src_ip);

	tcam_key->key_mask.dipv4_h =
		HINIC3_32_UPPER_16_BITS(rule->key_mask.inner_ipv4.dst_ip);
	tcam_key->key_mask.dipv4_l =
		HINIC3_32_LOWER_16_BITS(rule->key_mask.inner_ipv4.dst_ip);
	tcam_key->key_info.dipv4_h =
		HINIC3_32_UPPER_16_BITS(rule->key_spec.inner_ipv4.dst_ip);
	tcam_key->key_info.dipv4_l =
		HINIC3_32_LOWER_16_BITS(rule->key_spec.inner_ipv4.dst_ip);
}

static void
hinic3_fdir_tcam_vxlan_geneve_ipv6_init(struct hinic3_fdir_filter *rule,
					struct hinic3_tcam_key	  *tcam_key)
{
	tcam_key->key_mask_vxlan_ipv6.ip_type = HINIC3_UINT1_MAX;
	tcam_key->key_info_vxlan_ipv6.ip_type = HINIC3_FDIR_IP_TYPE_IPV6;

	tcam_key->key_mask_vxlan_ipv6.dipv6_key0 =
		HINIC3_32_UPPER_16_BITS(rule->key_mask.inner_ipv6.dst_ip[0]);
	tcam_key->key_mask_vxlan_ipv6.dipv6_key1 =
		HINIC3_32_LOWER_16_BITS(rule->key_mask.inner_ipv6.dst_ip[0]);
	tcam_key->key_mask_vxlan_ipv6.dipv6_key2 =
		HINIC3_32_UPPER_16_BITS(rule->key_mask.inner_ipv6.dst_ip[0x1]);
	tcam_key->key_mask_vxlan_ipv6.dipv6_key3 =
		HINIC3_32_LOWER_16_BITS(rule->key_mask.inner_ipv6.dst_ip[0x1]);
	tcam_key->key_mask_vxlan_ipv6.dipv6_key4 =
		HINIC3_32_UPPER_16_BITS(rule->key_mask.inner_ipv6.dst_ip[0x2]);
	tcam_key->key_mask_vxlan_ipv6.dipv6_key5 =
		HINIC3_32_LOWER_16_BITS(rule->key_mask.inner_ipv6.dst_ip[0x2]);
	tcam_key->key_mask_vxlan_ipv6.dipv6_key6 =
		HINIC3_32_UPPER_16_BITS(rule->key_mask.inner_ipv6.dst_ip[0x3]);
	tcam_key->key_mask_vxlan_ipv6.dipv6_key7 =
		HINIC3_32_LOWER_16_BITS(rule->key_mask.inner_ipv6.dst_ip[0x3]);
	tcam_key->key_info_vxlan_ipv6.dipv6_key0 =
		HINIC3_32_UPPER_16_BITS(rule->key_spec.inner_ipv6.dst_ip[0]);
	tcam_key->key_info_vxlan_ipv6.dipv6_key1 =
		HINIC3_32_LOWER_16_BITS(rule->key_spec.inner_ipv6.dst_ip[0]);
	tcam_key->key_info_vxlan_ipv6.dipv6_key2 =
		HINIC3_32_UPPER_16_BITS(rule->key_spec.inner_ipv6.dst_ip[0x1]);
	tcam_key->key_info_vxlan_ipv6.dipv6_key3 =
		HINIC3_32_LOWER_16_BITS(rule->key_spec.inner_ipv6.dst_ip[0x1]);
	tcam_key->key_info_vxlan_ipv6.dipv6_key4 =
		HINIC3_32_UPPER_16_BITS(rule->key_spec.inner_ipv6.dst_ip[0x2]);
	tcam_key->key_info_vxlan_ipv6.dipv6_key5 =
		HINIC3_32_LOWER_16_BITS(rule->key_spec.inner_ipv6.dst_ip[0x2]);
	tcam_key->key_info_vxlan_ipv6.dipv6_key6 =
		HINIC3_32_UPPER_16_BITS(rule->key_spec.inner_ipv6.dst_ip[0x3]);
	tcam_key->key_info_vxlan_ipv6.dipv6_key7 =
		HINIC3_32_LOWER_16_BITS(rule->key_spec.inner_ipv6.dst_ip[0x3]);
}

static void
hinic3_fdir_tcam_outer_ipv6_init(struct hinic3_fdir_filter *rule,
				 struct hinic3_tcam_key	   *tcam_key)
{
	tcam_key->key_mask_ipv6.sipv6_key0 =
		HINIC3_32_UPPER_16_BITS(rule->key_mask.ipv6.src_ip[0]);
	tcam_key->key_mask_ipv6.sipv6_key1 =
		HINIC3_32_LOWER_16_BITS(rule->key_mask.ipv6.src_ip[0]);
	tcam_key->key_mask_ipv6.sipv6_key2 =
		HINIC3_32_UPPER_16_BITS(rule->key_mask.ipv6.src_ip[0x1]);
	tcam_key->key_mask_ipv6.sipv6_key3 =
		HINIC3_32_LOWER_16_BITS(rule->key_mask.ipv6.src_ip[0x1]);
	tcam_key->key_mask_ipv6.sipv6_key4 =
		HINIC3_32_UPPER_16_BITS(rule->key_mask.ipv6.src_ip[0x2]);
	tcam_key->key_mask_ipv6.sipv6_key5 =
		HINIC3_32_LOWER_16_BITS(rule->key_mask.ipv6.src_ip[0x2]);
	tcam_key->key_mask_ipv6.sipv6_key6 =
		HINIC3_32_UPPER_16_BITS(rule->key_mask.ipv6.src_ip[0x3]);
	tcam_key->key_mask_ipv6.sipv6_key7 =
		HINIC3_32_LOWER_16_BITS(rule->key_mask.ipv6.src_ip[0x3]);
	tcam_key->key_info_ipv6.sipv6_key0 =
		HINIC3_32_UPPER_16_BITS(rule->key_spec.ipv6.src_ip[0]);
	tcam_key->key_info_ipv6.sipv6_key1 =
		HINIC3_32_LOWER_16_BITS(rule->key_spec.ipv6.src_ip[0]);
	tcam_key->key_info_ipv6.sipv6_key2 =
		HINIC3_32_UPPER_16_BITS(rule->key_spec.ipv6.src_ip[0x1]);
	tcam_key->key_info_ipv6.sipv6_key3 =
		HINIC3_32_LOWER_16_BITS(rule->key_spec.ipv6.src_ip[0x1]);
	tcam_key->key_info_ipv6.sipv6_key4 =
		HINIC3_32_UPPER_16_BITS(rule->key_spec.ipv6.src_ip[0x2]);
	tcam_key->key_info_ipv6.sipv6_key5 =
		HINIC3_32_LOWER_16_BITS(rule->key_spec.ipv6.src_ip[0x2]);
	tcam_key->key_info_ipv6.sipv6_key6 =
		HINIC3_32_UPPER_16_BITS(rule->key_spec.ipv6.src_ip[0x3]);
	tcam_key->key_info_ipv6.sipv6_key7 =
		HINIC3_32_LOWER_16_BITS(rule->key_spec.ipv6.src_ip[0x3]);

	tcam_key->key_mask_ipv6.dipv6_key0 =
		HINIC3_32_UPPER_16_BITS(rule->key_mask.ipv6.dst_ip[0]);
	tcam_key->key_mask_ipv6.dipv6_key1 =
		HINIC3_32_LOWER_16_BITS(rule->key_mask.ipv6.dst_ip[0]);
	tcam_key->key_mask_ipv6.dipv6_key2 =
		HINIC3_32_UPPER_16_BITS(rule->key_mask.ipv6.dst_ip[0x1]);
	tcam_key->key_mask_ipv6.dipv6_key3 =
		HINIC3_32_LOWER_16_BITS(rule->key_mask.ipv6.dst_ip[0x1]);
	tcam_key->key_mask_ipv6.dipv6_key4 =
		HINIC3_32_UPPER_16_BITS(rule->key_mask.ipv6.dst_ip[0x2]);
	tcam_key->key_mask_ipv6.dipv6_key5 =
		HINIC3_32_LOWER_16_BITS(rule->key_mask.ipv6.dst_ip[0x2]);
	tcam_key->key_mask_ipv6.dipv6_key6 =
		HINIC3_32_UPPER_16_BITS(rule->key_mask.ipv6.dst_ip[0x3]);
	tcam_key->key_mask_ipv6.dipv6_key7 =
		HINIC3_32_LOWER_16_BITS(rule->key_mask.ipv6.dst_ip[0x3]);
	tcam_key->key_info_ipv6.dipv6_key0 =
		HINIC3_32_UPPER_16_BITS(rule->key_spec.ipv6.dst_ip[0]);
	tcam_key->key_info_ipv6.dipv6_key1 =
		HINIC3_32_LOWER_16_BITS(rule->key_spec.ipv6.dst_ip[0]);
	tcam_key->key_info_ipv6.dipv6_key2 =
		HINIC3_32_UPPER_16_BITS(rule->key_spec.ipv6.dst_ip[0x1]);
	tcam_key->key_info_ipv6.dipv6_key3 =
		HINIC3_32_LOWER_16_BITS(rule->key_spec.ipv6.dst_ip[0x1]);
	tcam_key->key_info_ipv6.dipv6_key4 =
		HINIC3_32_UPPER_16_BITS(rule->key_spec.ipv6.dst_ip[0x2]);
	tcam_key->key_info_ipv6.dipv6_key5 =
		HINIC3_32_LOWER_16_BITS(rule->key_spec.ipv6.dst_ip[0x2]);
	tcam_key->key_info_ipv6.dipv6_key6 =
		HINIC3_32_UPPER_16_BITS(rule->key_spec.ipv6.dst_ip[0x3]);
	tcam_key->key_info_ipv6.dipv6_key7 =
		HINIC3_32_LOWER_16_BITS(rule->key_spec.ipv6.dst_ip[0x3]);
}

static void
hinic3_fdir_tcam_ipv6_vxlan_geneve_init(struct rte_eth_dev *	   dev,
					struct hinic3_fdir_filter *rule,
					struct hinic3_tcam_key *   tcam_key)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	u8 bifur_en, iso_en;

	tcam_key->key_mask_ipv6.ip_proto = rule->key_mask.proto;
	tcam_key->key_info_ipv6.ip_proto = rule->key_spec.proto;

	tcam_key->key_mask_ipv6.tunnel_type = HINIC3_UINT4_MAX;
	tcam_key->key_info_ipv6.tunnel_type = rule->tunnel_type;

	tcam_key->key_mask_ipv6.outer_ip_type = HINIC3_UINT1_MAX;
	tcam_key->key_info_ipv6.outer_ip_type = HINIC3_FDIR_IP_TYPE_IPV6;

	tcam_key->key_mask_ipv6.function_id = HINIC3_UINT15_MAX;
	tcam_key->key_mask_ipv6.vlan_flag = 1;

    if (hinic3_get_bifur_enable(nic_dev->hwdev, &bifur_en, &iso_en) != 0) {
        PMD_DRV_LOG(ERR, "hinic3 get port table bifur enable staus failed!");
    }

	if (bifur_en) {
		tcam_key->key_info_ipv6.vlan_flag = 1;
		tcam_key->key_info_ipv6.function_id = HINIC3_UINT15_MAX;
	} else {
		tcam_key->key_info_ipv6.vlan_flag = 0;
		tcam_key->key_info_ipv6.function_id =
			hinic3_global_func_id(nic_dev->hwdev) & HINIC3_UINT15_MAX;
	}

	tcam_key->key_mask_ipv6.dport = rule->key_mask.dst_port;
	tcam_key->key_info_ipv6.dport = rule->key_spec.dst_port;

	tcam_key->key_mask_ipv6.sport = rule->key_mask.src_port;
	tcam_key->key_info_ipv6.sport = rule->key_spec.src_port;

	if (rule->ip_type == HINIC3_FDIR_IP_TYPE_ANY)
		hinic3_fdir_tcam_outer_ipv6_init(rule, tcam_key);
}

static void
hinic3_fdir_tcam_vxlan_geneve_init(struct rte_eth_dev *	      dev,
				   struct hinic3_fdir_filter *rule,
				   struct hinic3_tcam_key *   tcam_key)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	u8 bifur_en, iso_en;

	if (rule->outer_ip_type == HINIC3_FDIR_IP_TYPE_IPV6) {
		hinic3_fdir_tcam_ipv6_vxlan_geneve_init(dev, rule, tcam_key);
		return;
	}

	tcam_key->key_mask.ip_proto = rule->key_mask.proto;
	tcam_key->key_info.ip_proto = rule->key_spec.proto;

	tcam_key->key_mask.sport = rule->key_mask.src_port;
	tcam_key->key_info.sport = rule->key_spec.src_port;

	tcam_key->key_mask.dport = rule->key_mask.dst_port;
	tcam_key->key_info.dport = rule->key_spec.dst_port;

	tcam_key->key_mask.outer_sipv4_h =
		HINIC3_32_UPPER_16_BITS(rule->key_mask.ipv4.src_ip);
	tcam_key->key_mask.outer_sipv4_l =
		HINIC3_32_LOWER_16_BITS(rule->key_mask.ipv4.src_ip);
	tcam_key->key_info.outer_sipv4_h =
		HINIC3_32_UPPER_16_BITS(rule->key_spec.ipv4.src_ip);
	tcam_key->key_info.outer_sipv4_l =
		HINIC3_32_LOWER_16_BITS(rule->key_spec.ipv4.src_ip);

	tcam_key->key_mask.outer_dipv4_h =
		HINIC3_32_UPPER_16_BITS(rule->key_mask.ipv4.dst_ip);
	tcam_key->key_mask.outer_dipv4_l =
		HINIC3_32_LOWER_16_BITS(rule->key_mask.ipv4.dst_ip);
	tcam_key->key_info.outer_dipv4_h =
		HINIC3_32_UPPER_16_BITS(rule->key_spec.ipv4.dst_ip);
	tcam_key->key_info.outer_dipv4_l =
		HINIC3_32_LOWER_16_BITS(rule->key_spec.ipv4.dst_ip);

	tcam_key->key_mask.vni_h =
		HINIC3_32_UPPER_16_BITS(rule->key_mask.tunnel.tunnel_id);
	tcam_key->key_mask.vni_l =
		HINIC3_32_LOWER_16_BITS(rule->key_mask.tunnel.tunnel_id);
	tcam_key->key_info.vni_h =
		HINIC3_32_UPPER_16_BITS(rule->key_spec.tunnel.tunnel_id);
	tcam_key->key_info.vni_l =
		HINIC3_32_LOWER_16_BITS(rule->key_spec.tunnel.tunnel_id);

	tcam_key->key_mask.tunnel_type = HINIC3_UINT4_MAX;
	tcam_key->key_info.tunnel_type = rule->tunnel_type;

	tcam_key->key_mask.vlan_flag = 1;
	tcam_key->key_mask.function_id = HINIC3_UINT15_MAX;

    if (hinic3_get_bifur_enable(nic_dev->hwdev, &bifur_en, &iso_en) != 0) {
        PMD_DRV_LOG(ERR, "hinic3 get port table bifur enable staus failed!");
    }

	if (bifur_en) {
		tcam_key->key_info.vlan_flag = 1;
		tcam_key->key_info.function_id = HINIC3_UINT15_MAX;
	} else {
		tcam_key->key_info.vlan_flag = 0;
		tcam_key->key_info.function_id =
			hinic3_global_func_id(nic_dev->hwdev) & HINIC3_UINT15_MAX;
	}

	if (rule->ip_type == HINIC3_FDIR_IP_TYPE_IPV4)
		hinic3_fdir_tcam_vxlan_geneve_ipv4_init(rule, tcam_key);

	else if (rule->ip_type == HINIC3_FDIR_IP_TYPE_IPV6)
		hinic3_fdir_tcam_vxlan_geneve_ipv6_init(rule, tcam_key);
}

static void
hinic3_fdir_tcam_info_init(struct rte_eth_dev	       *dev,
			   struct hinic3_fdir_filter   *rule,
			   struct hinic3_tcam_key      *tcam_key,
			   struct hinic3_tcam_cfg_rule *fdir_tcam_rule)
{
	if (rule->tunnel_type == HINIC3_FDIR_TUNNEL_MODE_NORMAL)
		hinic3_fdir_tcam_notunnel_init(dev, rule, tcam_key);
	else
		hinic3_fdir_tcam_vxlan_geneve_init(dev, rule, tcam_key);

	fdir_tcam_rule->data.qid = rule->rq_index;
#ifdef HINIC3_TRAFFIC_BIFUR
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	u8 bifur_en, iso_en;

	if (hinic3_get_bifur_enable(nic_dev->hwdev, &bifur_en, &iso_en) != 0)
		PMD_DRV_LOG(ERR, "hinic3 get port table bifur enable status failed.");

	if (bifur_en)
		fdir_tcam_rule->data.queue_num = rule->queue_num;
#endif
	tcam_key_calculate(tcam_key, fdir_tcam_rule);
}

#ifdef HINIC3_TRAFFIC_BIFUR
static void hinic3_fdir_tcam_key_get(struct rte_eth_dev *dev,
			struct hinic3_fdir_filter *rule,
			struct hinic3_tcam_key *tcam_key)
{
    if (rule->tunnel_type == HINIC3_FDIR_TUNNEL_MODE_NORMAL)
		hinic3_fdir_tcam_notunnel_init(dev, rule, tcam_key);
	else
		hinic3_fdir_tcam_vxlan_geneve_init(dev, rule, tcam_key);
}
#endif

static inline uint16_t
hinic3_ethertype_filter_lookup(struct hinic3_ethertype_filter_list *ethertype_list,
			uint16_t type)
{
	struct rte_flow *it;
	struct hinic3_filter_t *filter_rules;

	TAILQ_FOREACH(it, ethertype_list, node) {
		filter_rules = it->rule;
		if (type == filter_rules->ethertype_filter.ether_type)
			return filter_rules->ethertype_filter.ether_type;
	}

	return RTE_ETH_FILTER_NONE;
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

static struct hinic3_tcam_dynamic_block *
hinic3_alloc_dynamic_block_resource(struct hinic3_tcam_info *tcam_info,
			u16 dynamic_block_id)
{
	struct hinic3_tcam_dynamic_block *dynamic_block_ptr = NULL;

	dynamic_block_ptr = rte_zmalloc("hinic3_tcam_dynamic_mem",
		sizeof(struct hinic3_tcam_dynamic_block), 0);
	if (dynamic_block_ptr == NULL) {
		PMD_DRV_LOG(ERR,
			"Alloc fdir filter dynamic block index %d memory failed!",
			dynamic_block_id);
		return NULL;
	}

	dynamic_block_ptr->dynamic_block_id = dynamic_block_id;

	TAILQ_INSERT_TAIL(&tcam_info->tcam_dynamic_info.tcam_dynamic_list,
			dynamic_block_ptr, entries);

	tcam_info->tcam_dynamic_info.dynamic_block_cnt++;

	return dynamic_block_ptr;
}

static void hinic3_free_dynamic_block_resource(
			struct hinic3_tcam_info *tcam_info,
			struct hinic3_tcam_dynamic_block *dynamic_block_ptr)
{
	if (dynamic_block_ptr == NULL)
		return;

	TAILQ_REMOVE(&tcam_info->tcam_dynamic_info.tcam_dynamic_list,
		dynamic_block_ptr, entries);
	rte_free(dynamic_block_ptr);

	tcam_info->tcam_dynamic_info.dynamic_block_cnt--;
}

static struct hinic3_tcam_dynamic_block *
hinic3_dynamic_lookup_tcam_filter(struct rte_eth_dev *dev,
		struct hinic3_tcam_cfg_rule *fdir_tcam_rule,
		struct hinic3_tcam_info *tcam_info,
		struct hinic3_tcam_filter *tcam_filter,
		u16 *tcam_index)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	u16 block_cnt = tcam_info->tcam_dynamic_info.dynamic_block_cnt;
	struct hinic3_tcam_dynamic_block *dynamic_block_ptr = NULL;
	struct hinic3_tcam_dynamic_block *tmp = NULL;
	u16 rule_nums = nic_dev->tcam_rule_nums;
	int block_alloc_flag = 0;
	u16 dynamic_block_id = 0;
	u16 index;
	int err;

	if (rule_nums >= block_cnt * HINIC3_TCAM_DYNAMIC_BLOCK_SIZE) {
		if (block_cnt >=
			(HINIC3_TCAM_DYNAMIC_MAX_FILTERS /
			HINIC3_TCAM_DYNAMIC_BLOCK_SIZE)) {
			PMD_DRV_LOG(ERR, "Dynamic tcam block is full, alloc failed!");
			goto failed;
		}

		err = hinic3_alloc_tcam_block(nic_dev->hwdev,
					&dynamic_block_id);
		if (err) {
			PMD_DRV_LOG(ERR, "Fdir filter dynamic tcam alloc block failed!");
			goto failed;
		}

		block_alloc_flag = 1;

		dynamic_block_ptr =
			hinic3_alloc_dynamic_block_resource(tcam_info,
				dynamic_block_id);
		if (dynamic_block_ptr == NULL) {
			PMD_DRV_LOG(ERR, "Fdir filter dynamic alloc block memory failed!");
			goto block_alloc_failed;
		}
	}

	TAILQ_FOREACH(tmp, &tcam_info->tcam_dynamic_info.tcam_dynamic_list,
			entries) {
		if (tmp->dynamic_index_cnt < HINIC3_TCAM_DYNAMIC_BLOCK_SIZE)
			break;
	}

	if (tmp == NULL ||
		tmp->dynamic_index_cnt >= HINIC3_TCAM_DYNAMIC_BLOCK_SIZE) {
		PMD_DRV_LOG(ERR, "Fdir filter dynamic lookup for index failed!");
		goto look_up_failed;
	}

	for (index = 0; index < HINIC3_TCAM_DYNAMIC_BLOCK_SIZE; index++) {
		if (tmp->dynamic_index[index] == 0)
			break;
	}

	if (index == HINIC3_TCAM_DYNAMIC_BLOCK_SIZE) {
		PMD_DRV_LOG(ERR, "tcam block 0x%x supports filter rules is full!",
			tmp->dynamic_block_id);
		goto look_up_failed;
	}

	tcam_filter->dynamic_block_id = tmp->dynamic_block_id;
	tcam_filter->index = index;
	*tcam_index = index;

	fdir_tcam_rule->index =
		HINIC3_PKT_TCAM_DYNAMIC_INDEX_START(tmp->dynamic_block_id) +
			index;

	return tmp;

look_up_failed:
	if (dynamic_block_ptr != NULL)
		hinic3_free_dynamic_block_resource(tcam_info,
			dynamic_block_ptr);

block_alloc_failed:
	if (block_alloc_flag == 1)
		(void)hinic3_free_tcam_block(nic_dev->hwdev, &dynamic_block_id);

failed:
	return NULL;
}

static int hinic3_add_tcam_filter(struct rte_eth_dev *dev,
				struct hinic3_tcam_key *tcam_key,
				struct hinic3_tcam_cfg_rule *fdir_tcam_rule, bool is_hairpin)
{
	struct hinic3_tcam_info *tcam_info =
		HINIC3_DEV_PRIVATE_TO_TCAM_INFO(dev->data->dev_private);
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct hinic3_tcam_dynamic_block *dynamic_block_ptr = NULL;
	struct hinic3_tcam_dynamic_block *tmp = NULL;
	struct hinic3_tcam_filter *tcam_filter;
	u16 tcam_block_index = 0;
	u16 index = 0;
	int err;

	tcam_filter = rte_zmalloc("hinic3_fdir_filter",
			sizeof(struct hinic3_tcam_filter), 0);
	if (tcam_filter == NULL)
		return -ENOMEM;
	(void)rte_memcpy(&tcam_filter->tcam_key,
			 tcam_key, sizeof(struct hinic3_tcam_key));
	tcam_filter->queue = (u16)(fdir_tcam_rule->data.qid);

	if (nic_dev->tcam_rule_nums == 0) {
		err = hinic3_alloc_tcam_block(nic_dev->hwdev,
					&tcam_block_index);
		if (err) {
			PMD_DRV_LOG(ERR, "Fdir filter tcam alloc block failed!");
			goto failed;
		}

		dynamic_block_ptr =
			hinic3_alloc_dynamic_block_resource(tcam_info,
				tcam_block_index);
		if (dynamic_block_ptr == NULL) {
			PMD_DRV_LOG(ERR, "Fdir filter alloc dynamic first block memory failed!");
			goto alloc_block_failed;
		}
	}

	tmp = hinic3_dynamic_lookup_tcam_filter(dev,
		fdir_tcam_rule, tcam_info, tcam_filter, &index);
	if (tmp == NULL) {
		PMD_DRV_LOG(ERR, "Dynamic lookup tcam filter failed!");
		goto lookup_tcam_index_failed;
	}
	err = hinic3_add_tcam_rule(nic_dev->hwdev, fdir_tcam_rule, TCAM_RULE_FDIR_TYPE, is_hairpin);
	if (err) {
		PMD_DRV_LOG(ERR, "Fdir_tcam_rule add failed!");
		goto add_tcam_rules_failed;
	}

	if (!(nic_dev->ethertype_rule_nums + nic_dev->tcam_rule_nums)) {
		err = hinic3_set_fdir_tcam_rule_filter(nic_dev->hwdev, true);
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
		    fdir_tcam_rule->data.qid, nic_dev->tcam_rule_nums);

	return 0;

enable_failed:
	(void)hinic3_del_tcam_rule(nic_dev->hwdev, fdir_tcam_rule->index, TCAM_RULE_FDIR_TYPE);

add_tcam_rules_failed:
lookup_tcam_index_failed:
	if (nic_dev->tcam_rule_nums == 0 && dynamic_block_ptr != NULL)
		hinic3_free_dynamic_block_resource(tcam_info,
			dynamic_block_ptr);

alloc_block_failed:
	if (nic_dev->tcam_rule_nums == 0)
		(void)hinic3_free_tcam_block(nic_dev->hwdev, &tcam_block_index);

failed:
    rte_free(tcam_filter);
	return -EFAULT;
}

static int hinic3_del_dynamic_tcam_filter(struct rte_eth_dev *dev,
				struct hinic3_tcam_filter *tcam_filter)
{
	struct hinic3_tcam_info *tcam_info =
		HINIC3_DEV_PRIVATE_TO_TCAM_INFO(dev->data->dev_private);
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	u16 dynamic_block_id = tcam_filter->dynamic_block_id;
	struct hinic3_tcam_dynamic_block *tmp = NULL;
	u32 index = 0;
	int err;

	TAILQ_FOREACH(tmp, &tcam_info->tcam_dynamic_info.tcam_dynamic_list,
			entries) {
		if (tmp->dynamic_block_id == dynamic_block_id)
			break;
	}

	if (tmp == NULL || tmp->dynamic_block_id != dynamic_block_id) {
		PMD_DRV_LOG(ERR, "Fdir filter del dynamic lookup for block failed!");
		return -EINVAL;
	}

	index = HINIC3_PKT_TCAM_DYNAMIC_INDEX_START(tmp->dynamic_block_id) +
			tcam_filter->index;

	err = hinic3_del_tcam_rule(nic_dev->hwdev, index, TCAM_RULE_FDIR_TYPE);
	if (err) {
		PMD_DRV_LOG(ERR, "Fdir tcam rule del failed!");
		return -EFAULT;
	}

	PMD_DRV_LOG(INFO, "Del fdir_tcam_dynamic_rule function_id: 0x%x, "
		    "tcam_block_id: %d, local_index: %d, global_index: %d, "
		    "local_rules_nums: %d, global_rule_nums: %d succeed",
		    hinic3_global_func_id(nic_dev->hwdev), dynamic_block_id,
		    tcam_filter->index, index, tmp->dynamic_index_cnt - 1,
		    nic_dev->tcam_rule_nums - 1);

	tmp->dynamic_index[tcam_filter->index] = 0;
	tmp->dynamic_index_cnt--;
	nic_dev->tcam_rule_nums--;
	if (tmp->dynamic_index_cnt == 0) {
		(void)hinic3_free_tcam_block(nic_dev->hwdev, &dynamic_block_id);

		hinic3_free_dynamic_block_resource(tcam_info, tmp);
	}

	if (!(nic_dev->ethertype_rule_nums + nic_dev->tcam_rule_nums))
		(void)hinic3_set_fdir_tcam_rule_filter(nic_dev->hwdev, false);

	return 0;
}

static int hinic3_del_tcam_filter(struct rte_eth_dev *dev,
				struct hinic3_tcam_filter *tcam_filter)
{
	struct hinic3_tcam_info *tcam_info =
		HINIC3_DEV_PRIVATE_TO_TCAM_INFO(dev->data->dev_private);
	int err;

	err = hinic3_del_dynamic_tcam_filter(dev, tcam_filter);
	if (err < 0) {
		PMD_DRV_LOG(ERR, "Del dynamic tcam filter failed!");
		return err;
	}

	TAILQ_REMOVE(&tcam_info->tcam_list, tcam_filter, entries);

	rte_free(tcam_filter);

	return 0;
}

int hinic3_flow_add_del_fdir_filter(struct rte_eth_dev *dev,
				    struct hinic3_fdir_filter *fdir_filter,
				    bool add)
{
	struct hinic3_tcam_info *tcam_info =
		HINIC3_DEV_PRIVATE_TO_TCAM_INFO(dev->data->dev_private);
	struct hinic3_tcam_filter *tcam_filter;
	struct hinic3_tcam_cfg_rule fdir_tcam_rule;
	struct hinic3_tcam_key tcam_key;
	int ret;

	memset(&fdir_tcam_rule, 0, sizeof(struct hinic3_tcam_cfg_rule));
	memset((void *)&tcam_key, 0, sizeof(struct hinic3_tcam_key));

	hinic3_fdir_tcam_info_init(dev, fdir_filter,
			&tcam_key, &fdir_tcam_rule);

	if (add) {
		tcam_filter = hinic3_tcam_filter_lookup(&tcam_info->tcam_list, &tcam_key,
							HINIC3_ACTION_ADD, HINIC3_INVALID_INDEX);
		if (tcam_filter != NULL) {
			PMD_DRV_LOG(ERR, "Filter exists.");
			return -EEXIST;
		}

		ret = hinic3_add_tcam_filter(dev, &tcam_key,
				&fdir_tcam_rule, fdir_filter->is_hairpin);
		if (ret)
			goto cfg_tcam_filter_err;

		fdir_filter->tcam_index = (int)(fdir_tcam_rule.index);
	} else {
		tcam_filter = hinic3_tcam_filter_lookup(&tcam_info->tcam_list, &tcam_key,
							HINIC3_ACTION_NOT_ADD,
							fdir_filter->tcam_index);
		if (tcam_filter == NULL) {
			PMD_DRV_LOG(ERR, "Filter doesn't exist.");
			return -ENOENT;
		}

		PMD_DRV_LOG(INFO, "begin to del tcam filter");
		ret = hinic3_del_tcam_filter(dev, tcam_filter);
		if (ret)
			goto cfg_tcam_filter_err;
	}

	return 0;

cfg_tcam_filter_err:

	return ret;
}

int hinic3_enable_rxq_fdir_filter(struct rte_eth_dev *dev, u32 queue_id, u32 able)
{
	struct hinic3_tcam_info *tcam_info = HINIC3_DEV_PRIVATE_TO_TCAM_INFO(dev->data->dev_private);
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct hinic3_tcam_filter *it;
	struct hinic3_tcam_cfg_rule fdir_tcam_rule;
	int ret;
	u32 queue_res;
	uint16_t index;

	memset(&fdir_tcam_rule, 0, sizeof(struct hinic3_tcam_cfg_rule));

	if (able) {
		TAILQ_FOREACH (it, &tcam_info->tcam_list, entries) {
			if (queue_id == it->queue) {
				index = (u16)(HINIC3_PKT_TCAM_DYNAMIC_INDEX_START(it->dynamic_block_id) + it->index);

				/* When the rxq is start, find invalid rxq_id and delete
				   the fdir rule from the tcam. */
				ret = hinic3_del_tcam_rule(nic_dev->hwdev, index, TCAM_RULE_FDIR_TYPE);
				if (ret) {
					PMD_DRV_LOG(ERR, "del invalid tcam rule failed!");
					return -EFAULT;
				}

				fdir_tcam_rule.index = index;
				fdir_tcam_rule.data.qid = queue_id;
				tcam_key_calculate(&it->tcam_key, &fdir_tcam_rule);

				ret = hinic3_add_tcam_rule(nic_dev->hwdev, &fdir_tcam_rule, TCAM_RULE_FDIR_TYPE, 0);
				if (ret) {
					PMD_DRV_LOG(ERR, "add correct tcam rule failed!");
					return -EFAULT;
				}
			}
		}
	} else {
		queue_res = HINIC3_INVAILD_QID_BASE | queue_id;

		TAILQ_FOREACH (it, &tcam_info->tcam_list, entries) {
			if (queue_id == it->queue) {
				index = (u16)(HINIC3_PKT_TCAM_DYNAMIC_INDEX_START(it->dynamic_block_id) + it->index);

				/* When the rxq is stop, delete the fdir rule from the tcam
				   and add the corret fdir rule from the tcam */
				ret = hinic3_del_tcam_rule(nic_dev->hwdev, index, TCAM_RULE_FDIR_TYPE);
				if (ret) {
					PMD_DRV_LOG(ERR, "del correct tcam rule failed!");
					return -EFAULT;
				}

				fdir_tcam_rule.index = index;
				fdir_tcam_rule.data.qid = queue_res;
				tcam_key_calculate(&it->tcam_key, &fdir_tcam_rule);

				ret = hinic3_add_tcam_rule(nic_dev->hwdev, &fdir_tcam_rule, TCAM_RULE_FDIR_TYPE, 0);
				if (ret) {
					PMD_DRV_LOG(ERR, "add invalid tcam rule failed!");
					return -EFAULT;
				}
			}
		}
	}

	return ret;
}

void hinic3_free_fdir_filter(struct rte_eth_dev *dev)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);

	(void)hinic3_set_fdir_tcam_rule_filter(nic_dev->hwdev, false);

	(void)hinic3_flush_tcam_rule(nic_dev->hwdev);
}

static int hinic3_flow_set_arp_filter(struct rte_eth_dev *dev, struct rte_eth_ethertype_filter *ethertype_filter,
				      bool add)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	int ret;

	ret = hinic3_set_fdir_ethertype_filter(nic_dev->hwdev, HINIC3_PKT_TYPE_ARP, ethertype_filter, add);
	if (ret) {
		PMD_DRV_LOG(ERR, "%s fdir ethertype rule failed, err: %d", add ? "Add" : "Del", ret);
		return ret;
	}

	ret = hinic3_set_fdir_ethertype_filter(nic_dev->hwdev, HINIC3_PKT_TYPE_ARP_REQ, ethertype_filter, add);
	if (ret) {
		PMD_DRV_LOG(ERR, "%s arp request rule failed, err: %d", add ? "Add" : "Del", ret);
		goto set_arp_req_failed;
	}

	ret = hinic3_set_fdir_ethertype_filter(nic_dev->hwdev, HINIC3_PKT_TYPE_ARP_REP, ethertype_filter, add);
	if (ret) {
		PMD_DRV_LOG(ERR, "%s arp response rule failed, err: %d", add ? "Add" : "Del", ret);
		goto set_arp_rep_failed;
	}

	return 0;

set_arp_rep_failed:
	(void)hinic3_set_fdir_ethertype_filter(nic_dev->hwdev, HINIC3_PKT_TYPE_ARP_REQ, ethertype_filter, !add);

set_arp_req_failed:
	(void)hinic3_set_fdir_ethertype_filter(nic_dev->hwdev, HINIC3_PKT_TYPE_ARP, ethertype_filter, !add);

	return ret;
}

static int hinic3_flow_set_slow_filter(struct rte_eth_dev *dev,
				       struct rte_eth_ethertype_filter *ethertype_filter,
				       bool add)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	int ret;

	ret = hinic3_set_fdir_ethertype_filter(nic_dev->hwdev, HINIC3_PKT_TYPE_LACP, ethertype_filter, add);
	if (ret) {
		PMD_DRV_LOG(ERR, "%s lacp fdir rule failed, err: %d", add ? "Add" : "Del", ret);
		return ret;
	}

	ret = hinic3_set_fdir_ethertype_filter(nic_dev->hwdev, HINIC3_PKT_TYPE_OAM, ethertype_filter, add);
	if (ret) {
		PMD_DRV_LOG(ERR, "%s oam rule failed, err: %d", add ? "Add" : "Del", ret);
		goto set_arp_oam_failed;
	}

	return 0;

set_arp_oam_failed:
	(void)hinic3_set_fdir_ethertype_filter(nic_dev->hwdev, HINIC3_PKT_TYPE_LACP, ethertype_filter, !add);

	return ret;
}

static int hinic3_flow_set_lldp_filter(struct rte_eth_dev *dev,
				       struct rte_eth_ethertype_filter *ethertype_filter,
				       bool add)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	int ret;

	ret = hinic3_set_fdir_ethertype_filter(nic_dev->hwdev, HINIC3_PKT_TYPE_LLDP, ethertype_filter, add);
	if (ret) {
		PMD_DRV_LOG(ERR, "%s lldp fdir rule failed, err: %d", add ? "Add" : "Del", ret);
		return ret;
	}

	ret = hinic3_set_fdir_ethertype_filter(nic_dev->hwdev, HINIC3_PKT_TYPE_CDCP, ethertype_filter, add);
	if (ret) {
		PMD_DRV_LOG(ERR, "%s cdcp fdir rule failed, err: %d", add ? "Add" : "Del", ret);
		goto set_arp_cdcp_failed;
	}

	return 0;

set_arp_cdcp_failed:
	(void)hinic3_set_fdir_ethertype_filter(nic_dev->hwdev, HINIC3_PKT_TYPE_LLDP, ethertype_filter, !add);

	return ret;
}

static int hinic3_flow_add_del_ethertype_filter_rule(struct rte_eth_dev *dev,
						     struct rte_eth_ethertype_filter *ethertype_filter,
						     bool add)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct hinic3_ethertype_filter_list *ethertype_list = &nic_dev->filter_ethertype_list;

	if (hinic3_ethertype_filter_lookup(ethertype_list, ethertype_filter->ether_type)) {
		if (add) {
			PMD_DRV_LOG(ERR, "The rule already exists, can not to be added");
			return -EPERM;
		}
	} else {
		if (!add) {
			PMD_DRV_LOG(ERR, "The rule not exists, can not to be delete");
			return -EPERM;
		}
	}

	switch (ethertype_filter->ether_type) {
		case RTE_ETHER_TYPE_ARP:
			return hinic3_flow_set_arp_filter(dev, ethertype_filter, add);
		case RTE_ETHER_TYPE_RARP:
			return hinic3_set_fdir_ethertype_filter(nic_dev->hwdev,
				HINIC3_PKT_TYPE_RARP, ethertype_filter, add);

		case RTE_ETHER_TYPE_SLOW:
			return hinic3_flow_set_slow_filter(dev, ethertype_filter, add);

		case RTE_ETHER_TYPE_LLDP:
			return hinic3_flow_set_lldp_filter(dev, ethertype_filter, add);

		case RTE_ETHER_TYPE_CNM:
			return hinic3_set_fdir_ethertype_filter(nic_dev->hwdev,
				HINIC3_PKT_TYPE_CNM, ethertype_filter, add);

		case RTE_ETHER_TYPE_ECP:
			return hinic3_set_fdir_ethertype_filter(nic_dev->hwdev,
				HINIC3_PKT_TYPE_ECP, ethertype_filter, add);

		default:
			PMD_DRV_LOG(ERR, "Unknown ethertype %d queue_id %d",
				    ethertype_filter->ether_type, ethertype_filter->queue);
			return -EPERM;
	}
}

static int hinic3_flow_ethertype_rule_nums(struct rte_eth_ethertype_filter *ethertype_filter)
{
	switch (ethertype_filter->ether_type) {
		case RTE_ETHER_TYPE_ARP:
			return HINIC3_ARP_RULE_NUM;
		case RTE_ETHER_TYPE_RARP:
			return HINIC3_RARP_RULE_NUM;
		case RTE_ETHER_TYPE_SLOW:
			return HINIC3_SLOW_RULE_NUM;
		case RTE_ETHER_TYPE_LLDP:
			return HINIC3_LLDP_RULE_NUM;
		case RTE_ETHER_TYPE_CNM:
			return HINIC3_CNM_RULE_NUM;
		case RTE_ETHER_TYPE_ECP:
			return HINIC3_ECP_RULE_NUM;

		default:
			PMD_DRV_LOG(ERR, "Unknown ethertype %d",
				    ethertype_filter->ether_type);
			return 0;
	}
}

int hinic3_flow_add_del_ethertype_filter(struct rte_eth_dev *dev,
					 struct rte_eth_ethertype_filter *ethertype_filter,
					 bool add)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	int ret;

	ret = hinic3_flow_add_del_ethertype_filter_rule(dev, ethertype_filter, add);

	if (ret) {
		PMD_DRV_LOG(ERR, "%s fdir ethertype rule failed, err: %d", add ? "Add" : "Del", ret);
		return ret;
	}

	if (add) {
		if (nic_dev->ethertype_rule_nums == 0) {
			ret = hinic3_set_fdir_tcam_rule_filter(nic_dev->hwdev, true);
			if (ret) {
				PMD_DRV_LOG(ERR, "enable fdir rule failed, err: %d", ret);
				goto enable_fdir_failed;
			}
		}
		nic_dev->ethertype_rule_nums = nic_dev->ethertype_rule_nums +
			hinic3_flow_ethertype_rule_nums(ethertype_filter);
	} else {
		nic_dev->ethertype_rule_nums = nic_dev->ethertype_rule_nums -
			hinic3_flow_ethertype_rule_nums(ethertype_filter);

		if (!(nic_dev->ethertype_rule_nums + nic_dev->tcam_rule_nums)) {
			ret = hinic3_set_fdir_tcam_rule_filter(nic_dev->hwdev, false);
			if (ret) {
				PMD_DRV_LOG(ERR, "disable fdir rule failed, err: %d", ret);
			}
		}
	}

	return 0;

enable_fdir_failed:
	(void)hinic3_flow_add_del_ethertype_filter_rule(dev, ethertype_filter, !add);
	return ret;
}

#ifdef HINIC3_TRAFFIC_BIFUR
int
hinic3_flow_query_fdir_filter(struct rte_eth_dev *dev, struct hinic3_fdir_filter *fdir_filter,
			      __rte_unused u64 *hits, __rte_unused u64 *bytes_count)
{
	struct hinic3_tcam_info	  *tcam_info = HINIC3_DEV_PRIVATE_TO_TCAM_INFO(dev->data->dev_private);
	struct hinic3_nic_dev	  *nic_dev   = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct hinic3_tcam_filter *tcam_filter;
	struct hinic3_tcam_key	   tcam_key;

	memset((void *)&tcam_key, 0, sizeof(struct hinic3_tcam_key));
	hinic3_fdir_tcam_key_get(dev, fdir_filter, &tcam_key);
	tcam_filter = hinic3_tcam_filter_lookup(&tcam_info->tcam_list, &tcam_key,
						HINIC3_ACTION_NOT_ADD, fdir_filter->tcam_index);
	if (tcam_filter == NULL) {
		PMD_DRV_LOG(ERR, "Filter doesn't exist.");
		return -EINVAL;
	}

	if (fdir_filter->ip_type == HINIC3_FDIR_IP_TYPE_IPV4) {
		PMD_DRV_LOG(INFO, "fdir ipv4 flow get:");
		PMD_DRV_LOG(INFO, "glb index:%d", fdir_filter->tcam_index);
		PMD_DRV_LOG(INFO, "ether_type mask: 0x%x", fdir_filter->key_mask.ether_type);
		PMD_DRV_LOG(INFO, "ether_type spec: 0x%x", fdir_filter->key_spec.ether_type);
		PMD_DRV_LOG(INFO, "protocol mask: 0x%x", fdir_filter->key_mask.ipv4.proto);
		PMD_DRV_LOG(INFO, "protocol spec: 0x%x", fdir_filter->key_spec.ipv4.proto);
		PMD_DRV_LOG(INFO, "src_ip mask: 0x%x", fdir_filter->key_mask.ipv4.src_ip);
		PMD_DRV_LOG(INFO, "src_ip spec: 0x%x", fdir_filter->key_spec.ipv4.src_ip);
		PMD_DRV_LOG(INFO, "dst_ip mask: 0x%x", fdir_filter->key_mask.ipv4.dst_ip);
		PMD_DRV_LOG(INFO, "dst_ip spec: 0x%x", fdir_filter->key_spec.ipv4.dst_ip);
		PMD_DRV_LOG(INFO, "src_port mask: 0x%x", fdir_filter->key_mask.src_port);
		PMD_DRV_LOG(INFO, "src_port spec: 0x%x", fdir_filter->key_spec.src_port);
		PMD_DRV_LOG(INFO, "dst_port mask: 0x%x", fdir_filter->key_mask.dst_port);
		PMD_DRV_LOG(INFO, "dst_port spec: 0x%x", fdir_filter->key_spec.dst_port);
		PMD_DRV_LOG(INFO, "rule_nums: 0x%x", nic_dev->tcam_rule_nums);
		PMD_DRV_LOG(INFO, "mark_id: 0x%x", fdir_filter->rq_index);
	} else {
		PMD_DRV_LOG(INFO, "fdir ipv6 flow get:");
		PMD_DRV_LOG(INFO, "glb index:%d", fdir_filter->tcam_index);
		PMD_DRV_LOG(INFO, "protocol mask: 0x%x", fdir_filter->key_mask.ipv6.proto);
		PMD_DRV_LOG(INFO, "protocol spec: 0x%x", fdir_filter->key_spec.ipv6.proto);
		PMD_DRV_LOG(INFO, "src_ip mask: 0x%08x:%08x:%08x:%08x",
			    fdir_filter->key_mask.ipv6.src_ip[0],
			    fdir_filter->key_mask.ipv6.src_ip[1],
			    fdir_filter->key_mask.ipv6.src_ip[2],
			    fdir_filter->key_mask.ipv6.src_ip[3]);
		PMD_DRV_LOG(INFO, "src_ip spec: 0x%08x:%08x:%08x:%08x",
			    fdir_filter->key_spec.ipv6.src_ip[0],
			    fdir_filter->key_spec.ipv6.src_ip[1],
			    fdir_filter->key_spec.ipv6.src_ip[2],
			    fdir_filter->key_spec.ipv6.src_ip[3]);
		PMD_DRV_LOG(INFO, "dst_ip mask: 0x%08x:%08x:%08x:%08x",
			    fdir_filter->key_mask.ipv6.dst_ip[0],
			    fdir_filter->key_mask.ipv6.dst_ip[1],
			    fdir_filter->key_mask.ipv6.dst_ip[2],
			    fdir_filter->key_mask.ipv6.dst_ip[3]);
		PMD_DRV_LOG(INFO, "dst_ip spec: 0x%08x:%08x:%08x:%08x",
			    fdir_filter->key_spec.ipv6.dst_ip[0],
			    fdir_filter->key_spec.ipv6.dst_ip[1],
			    fdir_filter->key_spec.ipv6.dst_ip[2],
			    fdir_filter->key_spec.ipv6.dst_ip[3]);
		PMD_DRV_LOG(INFO, "src_port mask: 0x%x", fdir_filter->key_mask.src_port);
		PMD_DRV_LOG(INFO, "src_port spec: 0x%x", fdir_filter->key_spec.src_port);
		PMD_DRV_LOG(INFO, "dst_port mask: 0x%x", fdir_filter->key_mask.dst_port);
		PMD_DRV_LOG(INFO, "dst_port spec: 0x%x", fdir_filter->key_spec.dst_port);
		PMD_DRV_LOG(INFO, "rule_nums: 0x%x", nic_dev->tcam_rule_nums);
		PMD_DRV_LOG(INFO, "mark_id: 0x%x", fdir_filter->rq_index);
	}

	if (fdir_filter->queue_num > 1) {
		char   queues[HINIC3_RSS_QUEUE_BUF] = {0};
		size_t offset			    = 0;
		queues[0]			    = '\0';
		u32 rq_index			    = fdir_filter->rq_index;

		for (int idx = 0; rq_index > 0 && offset < (HINIC3_RSS_QUEUE_BUF - 1); ++idx, rq_index >>= 1) {
			if (!(rq_index & 1))
				continue;

			offset += snprintf(queues + offset, HINIC3_RSS_QUEUE_BUF - offset, "%s%d", (offset > 0) ? " " : "", idx);
			if (offset >= HINIC3_RSS_QUEUE_BUF - 1)
				break;
		}
		PMD_DRV_LOG(INFO, "queues: [%s]", queues);
		PMD_DRV_LOG(INFO, "bifur_flag: %d", tcam_key.key_info.bifur_flag);
	}

	return 0;
}
#endif