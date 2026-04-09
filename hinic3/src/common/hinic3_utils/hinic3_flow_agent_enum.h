/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2024 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_FLOW_AGNET_ENUM_H
#define HINIC3_FLOW_AGNET_ENUM_H

#ifdef __cplusplus
extern "C" {
#endif

enum {
    VXLAN_STATUS_NONE = 0,
    VXLAN_STATUS_ENCAP,
    VXLAN_STATUS_DECAP
};

enum {
    VLAN_STATUS_NONE = 0,
    VLAN_STATUS_PUSH
};

enum hinic3_packet_detect_mode {
    HINIC3_PACKET_DETECT_DISABLE = 0,
    HINIC3_PACKET_DETECT_ENABLE = 1,
};

enum hinic3_flow_engine_bit_type {
    HINIC3_ENG_CAP_F_BATCH_DEL,
    HINIC3_ENG_CAP_F_OFFLOAD_RAW_IP,
    HINIC3_ENG_CAP_F_OFFLOAD_ETHERNET,
    HINIC3_ENG_CAP_F_STATS_SYNC_PULL,
    HINIC3_ENG_CAP_F_OFFLOAD_ICMP,
    HINIC3_ENG_CAP_F_OFFLOAD_IPV6,
    HINIC3_ENG_CAP_F_OFFLOAD_ICMPV6,
    HINIC3_ENG_CAP_F_OFFLOAD_3TUPLE,
    HINIC3_ENG_CAP_F_ASYNC_FLUSH,
    HINIC3_ENG_CAP_F_MAX
};

enum {
    /* 0, not offload any flow */
    HINIC3_ESCAPE_MODE_NO_OFFLOAD,
    /* 1, not offload ipv6 flow */
    HINIC3_ESCAPE_MODE_NO_IPV6_OFFLOAD,
    /* 2, offload all flows */
    HINIC3_ESCAPE_MODE_ALL_OFFLOAD,
    HINIC3_ESCAPE_MODE_MAX
};

enum offload_flow_status {
    HINIC3_FLOW_OFFLOAD_FAILED,
    HINIC3_FLOW_OFFLOAD_SUCCESS,
    HINIC3_FLOW_OFFLOAD_DOING,
};


enum hinic3_offload_mode_status {
    HINIC3_FLOW_OFFLOAD_ENABLE,
    HINIC3_FLOW_OFFLOAD_DISABLE,
};

enum hinic3_module {
    HINIC3_COMMON_RESOURCE,
    HINIC3_CAPTURE,
    HINIC3_COMMAND,
    HINIC3_CT_OFFLOAD,
    HINIC3_DRIVER_ADAPTER,
    HINIC3_FLOWS,
    HINIC3_OVS_FLOW,
    HINIC3_INIT,
    HINIC3_POLICY,
    HINIC3_PORTS,
    HINIC3_QOS,
    HINIC3_SECURITY_FILTER,
    HINIC3_UFID_MAP,
    HINIC3_OVS_MEMPOOL,
    HINIC3_OVS_UFID_MEMPOOL,
    HINIC3_OVS_UFID_MAP,
    HINIC3_PACKET_PARSE,
    HINIC3_OVS_SMAC,
    HINIC3_OVS_HINIC3_PRIVATE,
    HIOVS_MEM,
    HINIC3_CMD,
    HINIC3_MODULE_MAX
};

#define HINIC3_MODULE_NAME_MAX 30

struct hinic3_module_name {
    enum hinic3_module module_id;
    char module_name[HINIC3_MODULE_NAME_MAX];
};

#ifdef __cplusplus
}
#endif

#endif
