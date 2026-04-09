/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */
#ifndef HINIC3_AGENT_CMD_FORMAT_H
#define HINIC3_AGENT_CMD_FORMAT_H
#include "hinic3_util.h"
#include "hinic3_message.h"
#include "hinic3_nlattr.h"
#include "hinic3_flow_agent_public.h"
#include "hinic3_ds.h"
#include "hinic3_driver_public.h"
#include "hinic3_provider.h"
#include "hinic3_util.h"
#ifdef __cplusplus
extern "C" {
#endif
#define BITS_PER_BYTE          8
#define BYTE_ALIGN_MASK        7
#define HINIC3_HW_UFID_FMT "%08x-%08x"
#define HINIC3_HW_UFID_ARGS(ufid) ((unsigned int)((ufid) >> 32)), ((unsigned int)((ufid) & 0xffffffff))
#define HINIC3_MAC_FMT "%02x:%02x:%02x:%02x:%02x:%02x"
#define HINIC3_OUTPUT_MAC(XOUT) (XOUT)[0], (XOUT)[1], (XOUT)[2], (XOUT)[3], (XOUT)[4], (XOUT)[5]
#define IPV6_ARGS(ip)                                                                             \
    ntohl((ip)[0]) >> 16, ntohl((ip)[0]) & 0xFFFF, ntohl((ip)[1]) >> 16, ntohl((ip)[1]) & 0xFFFF, \
        ntohl((ip)[2]) >> 16, ntohl((ip)[2]) & 0xFFFF, ntohl((ip)[3]) >> 16, ntohl((ip)[3]) & 0xFFFF
#define IPV6_FMT \
    "%04" PRIx16 ":%04" PRIx16 ":%04" PRIx16 ":%04" PRIx16 ":%04" PRIx16 ":%04" PRIx16 ":%04" PRIx16 ":%04" PRIx16
#define HINIC3_OUTPUT_FUNC_LEN sizeof(hinic3_actions_format_output_func_array) \
                              / sizeof(struct hinic3_actions_format_output_func_map)

struct hinic3_actions_format_output_func_map {
    enum hinic3_flow_action_type type;
    void (*func)(const hinic3_nlattr_itr nla, struct ds *ds);
};

void hinic3_hw_ufid_format_output(const uint64_t *hw_ufid, struct ds *ds);
void hinic3_flow_key_format_output(const struct hinic3_nlattr *key, struct ds *ds);
void hinic3_actions_format_output(struct ds *ds, const struct hinic3_nlattr *actions);
void hinic3_flow_stats_format_output(const struct hinic3_flow_stats *stats, struct ds *ds);
void hinic3_agent_flow_format_output(uint32_t table_id, struct hinic3_dpif_flow_for_get *f, struct ds *ds);

void hinic3_show_upcall_port_stats(struct ds *output_msg, const hinic3_port_stats *port_stats);
void hinic3_show_base_port_stats(struct ds *output_msg, const hinic3_port_stats *port_stats);
void hinic3_show_check_port_stats(struct ds *output_msg, const hinic3_port_stats *port_stats);
void hinic3_show_hiovs_port_stats(struct ds *output_msg, const hinic3_port_stats *port_stats);

#ifdef __cplusplus
}
#endif
#endif
