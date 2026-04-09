 /* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_FLOW_MIRROR_H
#define HINIC3_FLOW_MIRROR_H

#include "rte_flow.h"
#include "hinic3_flow_session.h"
#include "hiovs_acl_api.h"
#include "hinic3_offload_flow_public.h"

#define HINIC3_MIRROR_VXLAN_ITEM_LEN sizeof(hinic3_mirror_vxlan_item_func_array) \
                                    / sizeof(struct hinic3_mirror_vxlan_item_func_map)

#define HINIC3_FLG_MIRROR_ETH     (1LLU << 0)
#define HINIC3_FLG_MIRROR_IPV4    (1LLU << 1)
#define HINIC3_FLG_MIRROR_IPV6    (1LLU << 2)
#define HINIC3_FLG_MIRROR_VLAN    (1LLU << 3)
#define HINIC3_FLG_MIRROR_VXLAN   (1LLU << 4)
#define HINIC3_FLG_MIRROR_UDP     (1LLU << 5)
#define HINIC3_FLG_MIRROR_GRE     (1LLU << 6)
#define HINIC3_FLG_MIRROR_VXLAN_GPE (1LLU << 7)
#define HINIC3_FLG_MIRROR_SHIM    (1LLU << 8)

#define HINIC3_FLG_MIRROR_IPV4_VLAN (HINIC3_FLG_MIRROR_ETH |   \
                                    HINIC3_FLG_MIRROR_IPV4 |  \
                                    HINIC3_FLG_MIRROR_VLAN |  \
                                    HINIC3_FLG_MIRROR_VXLAN | \
                                    HINIC3_FLG_MIRROR_UDP)

#define HINIC3_FLG_MIRROR_IPV6_VLAN (HINIC3_FLG_MIRROR_ETH |   \
                                    HINIC3_FLG_MIRROR_IPV6 |  \
                                    HINIC3_FLG_MIRROR_VLAN |  \
                                    HINIC3_FLG_MIRROR_VXLAN | \
                                    HINIC3_FLG_MIRROR_UDP)

#define HINIC3_FLG_MIRROR_IPV4_VXLAN (HINIC3_FLG_MIRROR_ETH |   \
                                     HINIC3_FLG_MIRROR_IPV4 |  \
                                     HINIC3_FLG_MIRROR_VXLAN | \
                                     HINIC3_FLG_MIRROR_UDP)

#define HINIC3_FLG_MIRROR_IPV6_VXLAN (HINIC3_FLG_MIRROR_ETH |   \
                                     HINIC3_FLG_MIRROR_IPV6 |  \
                                     HINIC3_FLG_MIRROR_VXLAN | \
                                     HINIC3_FLG_MIRROR_UDP)

#define HINIC3_FLG_MIRROR_IPV4_GRE (HINIC3_FLG_MIRROR_ETH |  \
                                     HINIC3_FLG_MIRROR_IPV4 | \
                                     HINIC3_FLG_MIRROR_GRE)

#define HINIC3_FLG_MIRROR_IPV6_GRE (HINIC3_FLG_MIRROR_ETH |  \
                                     HINIC3_FLG_MIRROR_IPV6 | \
                                     HINIC3_FLG_MIRROR_GRE)

#define HINIC3_FLG_MIRROR_IPV4_VLAN_GRE (HINIC3_FLG_MIRROR_ETH | \
                                         HINIC3_FLG_MIRROR_IPV4 | \
                                         HINIC3_FLG_MIRROR_VLAN | \
                                         HINIC3_FLG_MIRROR_GRE)

#define HINIC3_FLG_MIRROR_IPV6_VLAN_GRE (HINIC3_FLG_MIRROR_ETH | \
                                         HINIC3_FLG_MIRROR_IPV6 | \
                                         HINIC3_FLG_MIRROR_VLAN | \
                                         HINIC3_FLG_MIRROR_GRE)

#define HINIC3_FLG_MIRROR_IPV4_VXLAN_GPE_SHIM (HINIC3_FLG_MIRROR_ETH | \
                                         HINIC3_FLG_MIRROR_IPV4 | \
                                         HINIC3_FLG_MIRROR_UDP | \
                                         HINIC3_FLG_MIRROR_VXLAN_GPE | \
                                         HINIC3_FLG_MIRROR_SHIM)

struct hinic3_mirror_vxlan_item_func_map {
    enum rte_flow_item_type item;
    int (*func)(const struct rte_flow_item *item, struct hinic3_mirror_session_info *session_info,
        const uint8_t flow_tpye);
};

int hinic3_offload_parse_sample_act(const struct rte_flow_action *act, struct hinic3_nlattr *act_nla,
    struct rte_flow *mega_flow, uint8_t mirror_dir_flag);

#endif
