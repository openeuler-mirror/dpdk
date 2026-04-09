/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Huawei Technologies Co., Ltd
 */
#ifndef HINIC3_FLOW_DUMP_H
#define HINIC3_FLOW_DUMP_H
#include "rte_flow.h"
#include "hinic3_util.h"
#include "hinic3_ds.h"
#include "hinic3_packet_key_public.h"

void hinic3_dump_flow_init(void);
int hinic3_eth_flow_dump_start(void **context, uint32_t type, struct rte_flow_error *error);
int hinic3_eth_flow_dump_next(void *context, uint32_t count, uint32_t *dumped_count, struct rte_flow_item **pattern[],
                             struct rte_flow_action **actions[], struct rte_flow_error *error);
int hinic3_eth_flow_dump_done(void *context, struct rte_flow_error *error);
void hinic3_dump_context(struct ds *ds);
int hinic3_flow_query_ufid(const struct rte_flow_attr *attr, const struct rte_flow_item *pattern, uint64_t *ufid,
    struct hinic3_conntrack_full_key *full_key, struct ds *ds);
void hinic3_dump_contexts_lock(void);
void hinic3_dump_contexts_unlock(void);
#endif
