/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * Description: hinic3 rte_flow结构体格式化
 * Create: 2025-11-11
 */
#ifndef HINIC3_RTE_FLOW_FORMAT_H
#define HINIC3_RTE_FLOW_FORMAT_H
#include "hinic3_ds.h"
#include "rte_flow.h"
#include "hinic3_packet_key_public.h"
#include "hinic3_mega_offload.h"
#include "hinic3_nlattr.h"

void hinic3_format_conntrack_fullkey(const struct hinic3_conntrack_full_key *fullkey, struct ds *ds);
void hinic3_format_rte_flow(const struct rte_flow *flow, struct ds *ds);
int hinic3_format_rte_flow_mega(const struct hinic3_mega_flow* mega, struct ds *ds);

bool hinic3_get_dump_rte_flow_options(int argc, const char* argv[]);
void hinic3_log_fullkey(const char* str, const struct hinic3_conntrack_full_key *fullkey);
void hinic3_log_fullkey_nlattr(const char* str, const struct hinic3_nlattr *key);
#endif