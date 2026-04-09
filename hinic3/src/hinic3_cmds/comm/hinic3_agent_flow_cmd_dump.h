/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2024 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_AGENT_FLOW_DUMP_CMD_H
#define HINIC3_AGENT_FLOW_DUMP_CMD_H

#include <stdio.h>
#include "hinic3_command.h"
#include "hinic3_driver_public.h"
#include "hinic3_nlattr.h"
#include "hinic3_ds.h"
#include "hinic3_flow_dump_public.h"
#include "hinic3_mutex.h"
#include "hinic3_agent_flow_cmd_dump.h"

#ifdef __cplusplus
{
#endif

#define HINIC3_DUMP_FLOWS_MAX_ONCE 1000
#define HINIC3_DUMP_FLOWS_STOP_CHECK_PERIOD_MS 5
#define HINIC3_DUMP_FLOWS_STOP_TIME_OUT_MS 500 /* 500 ms */

    enum hinic3_dump_flows_status
    {
        HINIC3_DUMP_FLOWS_IDLE = 0,
        HINIC3_DUMP_FLOWS_RUNNING,
        HINIC3_DUMP_FLOWS_STOP,
    };

    struct hinic3_dump_all_flows_mgmt_t
    {
        pthread_t dump_thread;
        FILE *file;
        void *start;
        uint16_t dumping;
        bool is_dpak_flow;
        struct hinic3_mutex mutex_lock;
        struct hinic3_dpif_flow_for_get flow;
    };

    struct hinic3_dump_flow_count_t
    {
        uint64_t loss_ct_pkts;
        uint32_t flows;
    };

    struct hinic3_flow_dump_cmd
    {
        int (*hinic3_flow_get_maxflows_ops)(uint32_t, uint32_t *);
        int (*hinic3_flow_dump_start_ops)(void **);
        int (*hinic3_flow_dump_start_by_table_id_ops)(uint32_t, void **);
        int (*hinic3_flow_dump_next_ops)(void *, struct hinic3_dpif_flow_for_get *);
        int (*hinic3_flow_dump_done_ops)(void *);
        void (*hinic3_flow_key_format_output_ops)(const struct hinic3_nlattr *, struct ds *);
        enum hinic3_flow_type type;
    };

    void unixctl_hinic3_flow_dump_cmd_init(void);
    void hinic3_agent_dump_hinic3_emc_flows(struct unixctl_conn * conn, int argc HINIC3_UNUSED, const char *argv[] HINIC3_UNUSED,
                                            void *aux HINIC3_UNUSED);

    void hinic3_agent_dump_hinic3_flows(struct unixctl_conn * conn, int argc, const char *argv[],
                                        void *aux, enum hinic3_flow_type type);
    void hinic3_agent_dump_hinic3_flow_init(void);
    struct hinic3_dump_all_flows_mgmt_t *hinic3_cmd_get_flow_dump_mgmt(void);
    struct hinic3_flow_dump_cmd *hinic3_cmd_get_flow_dump_ops(void);
    int hinic3_alloc_mem_for_flow(struct hinic3_dpif_flow_for_get * f);
    void hinic3_agent_free_mem_for_flow(struct hinic3_dpif_flow_for_get * f);

    void hinic3_cmd_set_flow_get_maxflows_ops(int (*ops)(uint32_t, uint32_t *));
    void hinic3_cmd_set_flow_dump_start_ops(int (*ops)(void **));
    void hinic3_cmd_set_flow_dump_start_by_table_id_ops(int (*ops)(uint32_t, void **));
    void hinic3_cmd_set_flow_dump_next_ops(int (*ops)(void *, struct hinic3_dpif_flow_for_get *));
    void hinic3_cmd_set_flow_dump_done_ops(int (*ops)(void *));
    void hinic3_cmd_set_flow_key_format_output_ops(void (*ops)(const struct hinic3_nlattr *, struct ds *));
    void hinic3_cmd_set_flow_ops_status(enum hinic3_flow_type type);

    int hinic3_agent_force_stop_dump_all_flows_to_file(int argc, const char *argv[], struct ds *ds);
    int hinic3_agent_check_status_dump_flows_to_file(int argc, struct ds *ds);
    int hinic3_agent_dump_flows_to_file(const char *filename, struct ds *ds);

    static inline void hinic3_set_dumping_status(struct hinic3_dump_all_flows_mgmt_t * mgmt, uint16_t status)
    {
        if (mgmt == NULL)
        {
            return;
        }

        hinic3_pthread_mutex_lock(&mgmt->mutex_lock);
        mgmt->dumping = status;
        hinic3_pthread_mutex_unlock(&mgmt->mutex_lock);
    }

    static inline uint16_t hinic3_get_dumping_status(struct hinic3_dump_all_flows_mgmt_t * mgmt)
    {
        if (mgmt == NULL)
            return 0;
        uint16_t status;
        hinic3_pthread_mutex_lock(&mgmt->mutex_lock);
        status = mgmt->dumping;
        hinic3_pthread_mutex_unlock(&mgmt->mutex_lock);
        return status;
    }

    static inline enum hinic3_flow_type hinic3_cmd_get_flow_ops_status(void)
    {
        struct hinic3_flow_dump_cmd *ops = hinic3_cmd_get_flow_dump_ops();
        if (ops == NULL)
            return -1;
        return ops->type;
    }

    static inline int hinic3_cmd_flow_get_maxflows(uint32_t table_id, uint32_t *max_flow_cnt)
    {
        struct hinic3_flow_dump_cmd *ops = hinic3_cmd_get_flow_dump_ops();
        if (ops == NULL || ops->hinic3_flow_get_maxflows_ops == NULL)
            return -1;
        return ops->hinic3_flow_get_maxflows_ops(table_id, max_flow_cnt);
    }

    static inline int hinic3_cmd_flow_dump_start(void **state)
    {
        struct hinic3_flow_dump_cmd *ops = hinic3_cmd_get_flow_dump_ops();
        if (ops == NULL || ops->hinic3_flow_dump_start_ops == NULL)
            return -1;
        return ops->hinic3_flow_dump_start_ops(state);
    }

    static inline int hinic3_flexda_cmd_flow_dump_start_by_table_id(uint32_t table_id, void **state)
    {
        struct hinic3_flow_dump_cmd *ops = hinic3_cmd_get_flow_dump_ops();
        if (ops == NULL || ops->hinic3_flow_dump_start_by_table_id_ops == NULL)
        {
            return -1;
        }
        return ops->hinic3_flow_dump_start_by_table_id_ops(table_id, state);
    }

    static inline int hinic3_cmd_flow_dump_next(void *state, struct hinic3_dpif_flow_for_get *get)
    {
        struct hinic3_flow_dump_cmd *ops = hinic3_cmd_get_flow_dump_ops();
        if (ops == NULL || ops->hinic3_flow_dump_next_ops == NULL)
            return -1;
        return ops->hinic3_flow_dump_next_ops(state, get);
    }

    static inline int hinic3_cmd_flow_dump_done(void *state)
    {
        struct hinic3_flow_dump_cmd *ops = hinic3_cmd_get_flow_dump_ops();
        if (ops == NULL || ops->hinic3_flow_dump_done_ops == NULL)
            return -1;
        return ops->hinic3_flow_dump_done_ops(state);
    }

    static inline void hinic3_cmd_flow_key_format_output(const struct hinic3_nlattr *key, struct ds *ds)
    {
        struct hinic3_flow_dump_cmd *ops = hinic3_cmd_get_flow_dump_ops();
        if (ops == NULL || ops->hinic3_flow_key_format_output_ops == NULL)
            return;
        ops->hinic3_flow_key_format_output_ops(key, ds);
    }

#ifdef __cplusplus
}
#endif
#endif
