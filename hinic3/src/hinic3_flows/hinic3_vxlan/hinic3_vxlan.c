/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Huawei Technologies Co., Ltd
 */

#include "hinic3_vxlan.h"

#define HINIC3_IPV6_ADDR_SIZE 4
#define HINIC3_VNI_LOW 0
#define HINIC3_VNI_MID 1
#define HINIC3_VNI_HIGH 2
#define HINIC3_VNI_LOW_MOVE 8
#define HINIC3_VNI_MID_MOVE 16
#define HINIC3_VNI_HIGH_MOVE 24

static int
hinic3_check_tunnel_decap_para(struct rte_flow_tunnel *tunnel, struct rte_flow_error *error)
{
    if (error == NULL) {
        HINIC3_LOG(ERR, FLOW, "Flow tunnel decap set: Invalid input parameter: error.");
        return -EINVAL;
    }

    if (tunnel == NULL || tunnel->type != RTE_FLOW_ITEM_TYPE_VXLAN) {
        HINIC3_LOG(ERR, FLOW, "Flow tunnel decap set: Invalid input parameter: tunnel.");
        return rte_flow_error_set(error, EINVAL, RTE_FLOW_ERROR_TYPE_ATTR, NULL,
                                  "Flow tunnel decap set: Invalid input parameter: tunnel.");
    }

    return 0;
}

int hinic3_flow_tunnel_decap_set(struct rte_eth_dev *eth_dev HINIC3_UNUSED, struct rte_flow_tunnel *tunnel,
    struct rte_flow_action **pmd_actions HINIC3_UNUSED, uint32_t *num_of_actions HINIC3_UNUSED, struct rte_flow_error *error)
{
    int ret;
    struct hinic3_vtep_ip_set_args add_vxlan_args = { 0 };

    ret = hinic3_check_tunnel_decap_para(tunnel, error);
    if (ret != 0) {
        return ret;
    }

    add_vxlan_args.ops = HINIC3_VTEP_ADD;
    if (tunnel->is_ipv6) {
        add_vxlan_args.dip.is_ipv6 = 1;
        memcpy(add_vxlan_args.dip.ip_addr, tunnel->ipv6.dst_addr, sizeof(tunnel->ipv6.dst_addr));
    } else {
        add_vxlan_args.dip.is_ipv6 = 0;
        add_vxlan_args.dip.ip_addr[0] = tunnel->ipv4.dst_addr;
    }

    ret = hinic3_global_set_vxlan_vtep(&add_vxlan_args);
    if (ret != 0) {
        HINIC3_LOG(ERR, FLOW, "Flow tunnel decap set: Driver interface error.");
        return rte_flow_error_set(error, EPERM, RTE_FLOW_ERROR_TYPE_UNSPECIFIED, NULL,
                                  "Flow tunnel decap set: Driver interface error.");
    }
    return 0;
}

uint32_t hinic3_get_hiovs_vni(const uint8_t *vni)
{
    uint32_t vni_dst = 0;
    vni_dst = (vni[HINIC3_VNI_HIGH] << HINIC3_VNI_HIGH_MOVE) |
              (vni[HINIC3_VNI_MID] << HINIC3_VNI_MID_MOVE) |
              (vni[HINIC3_VNI_LOW] << HINIC3_VNI_LOW_MOVE);
    return vni_dst;
}

uint32_t
hinic3_swap_vx_vni(const uint32_t vx_vni)
{
    return ((vx_vni & 0x00FFFFFF) << HINIC3_VNI_LOW_MOVE);
}

uint32_t
hinic3_swap_endian32(uint32_t value)
{
    return ((value & 0x000000FF) << HINIC3_VNI_HIGH_MOVE) |
           ((value & 0x0000FF00) << HINIC3_VNI_LOW_MOVE)  |
           ((value & 0x00FF0000) >> HINIC3_VNI_LOW_MOVE)  |
           ((value & 0xFF000000) >> HINIC3_VNI_HIGH_MOVE);
}