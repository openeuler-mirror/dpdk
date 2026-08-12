/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2024 Huawei Technologies Co., Ltd
 */

#include "hinic3_agent_flow_cmd_dump.h"
#include "hinic3_agent_flow_cmd.h"
#include "hinic3_ui_string.h"
#include "hinic3_mutex.h"
#include "hinic3_file_util.h"
#include "hinic3_util.h"
#include "hinic3_agent_cmd_format.h"
#include "rte_string_fns.h"
#include "hinic3_offload_flow_public.h"
#include "hinic3_rte_flow_format.h"
#include "hinic3_ufid_map_rte_flow.h"

int
hinic3_agent_force_stop_dump_all_flows_to_file(int argc, const char *argv[] HINIC3_UNUSED, struct ds *ds)
{
    enum {
        ARGC = 2
    };
    if (argc != ARGC) {
        hinic3_ds_put_format(ds, "%s%s, please input -h or --help to get help info.\n",
            HINIC3_UI_LEADING_SIGN_ERROR, HINIC3_UI_ERROR_TOO_MANY_PARAMETER);
        return -1;
    }

    struct hinic3_dump_all_flows_mgmt_t *mgmt = hinic3_cmd_get_flow_dump_mgmt();
    uint64_t drain_tsc = (rte_get_tsc_hz() / US_PER_MS);
    uint64_t start_tsc;
    uint64_t diff_tsc;

    hinic3_pthread_mutex_lock(&mgmt->mutex_lock);
    if (mgmt->dumping == HINIC3_DUMP_FLOWS_IDLE) {
        hinic3_pthread_mutex_unlock(&mgmt->mutex_lock);
        HINIC3_LOG(INFO, AGENT, "dump all flows into file stopped.");
        hinic3_ds_put_format(ds, "%s%s\n", HINIC3_UI_LEADING_SIGN_INFO, HINIC3_UI_FLOW_DUMP_STOP_DUMP_SUCCESS_STRING);
        return 0;
    }

    mgmt->dumping = HINIC3_DUMP_FLOWS_STOP;
    hinic3_pthread_mutex_unlock(&mgmt->mutex_lock);
    start_tsc = rte_rdtsc();
    while (hinic3_get_dumping_status(mgmt) == HINIC3_DUMP_FLOWS_STOP) {
        diff_tsc = rte_rdtsc() - start_tsc;
        if (diff_tsc > drain_tsc * HINIC3_DUMP_FLOWS_STOP_TIME_OUT_MS) {
            HINIC3_LOG(INFO, AGENT, "Dump all flows into file stopped time out ,please try again.");
            hinic3_ds_put_format(ds, "%s%s\n", HINIC3_UI_LEADING_SIGN_FAILURE,
                HINIC3_UI_FLOW_DUMP_STOP_DUMP_TIME_OUT_STRING);
            return -1;
        }
        rte_delay_ms(HINIC3_DUMP_FLOWS_STOP_CHECK_PERIOD_MS);
    }

    HINIC3_LOG(INFO, AGENT, "dump all flows into file stopped.");
    hinic3_ds_put_format(ds, "%s%s\n", HINIC3_UI_LEADING_SIGN_INFO, HINIC3_UI_FLOW_DUMP_STOP_DUMP_SUCCESS_STRING);
    return 0;
}

int
hinic3_agent_check_status_dump_flows_to_file(int argc, struct ds *ds)
{
    enum {
        ARGC = 2
    };
    if (argc != ARGC) {
        hinic3_ds_put_format_prefix(ds, INDENT_0, HINIC3_UI_LEADING_SIGN_ERROR,
            HINIC3_UI_FLOW_REQUIRES_WRONG_ARGUMENT_STRING, ARGC - 1);
        return -1;
    }

    struct hinic3_dump_all_flows_mgmt_t *mgmt = hinic3_cmd_get_flow_dump_mgmt();
    if (hinic3_get_dumping_status(mgmt) == HINIC3_DUMP_FLOWS_RUNNING) {
        hinic3_ds_put_format(ds, "%s%s\n", HINIC3_UI_LEADING_SIGN_INFO, HINIC3_UI_FLOW_DUMPIMG_TO_FILE_STRING);
    } else {
        hinic3_ds_put_format(ds, "%s%s\n", HINIC3_UI_LEADING_SIGN_INFO, HINIC3_UI_FLOW_DUMP_TO_FILE_COMPLETE_STRING);
    }
    return 0;
}

static int
hinic3_agent_parse_output_file_path(struct hinic3_dump_all_flows_mgmt_t *m, const char *filename,
    struct ds *ds)
{
    char absolute_path[PATH_MAX];

    if (strlen(filename) >= PATH_MAX) {
        m->file = NULL;
        hinic3_ds_put_format(ds, "%s%s", HINIC3_UI_LEADING_SIGN_ERROR, HINIC3_UI_FILE_PATH_LENGTH_TIPS_STRING);
        return -1;
    }

    int ret = hinic3_check_output_file_directory(hinic3_get_default_directory(), filename,
        absolute_path, sizeof(absolute_path), ds);
    if (ret != 0) {
        m->file = NULL;
        hinic3_ds_put_format(ds, HINIC3_UI_FILE_OPEN_FAILED_STRING, HINIC3_UI_LEADING_SIGN_ERROR, filename);
        return -1;
    }

    char *resolve_path = (char *)hinic3_calloc(1, PATH_MAX, HINIC3_COMMAND);
    if (resolve_path == NULL) {
        m->file = NULL;
        hinic3_ds_put_format(ds, "%s%s\n", HINIC3_UI_LEADING_SIGN_FAILURE, HINIC3_UI_FILE_PATH_MALLOC_TIPS_STRING);
        return -1;
    }

    ret = hinic3_check_file_realpath(absolute_path, resolve_path, ds);
    if (ret != 0) {
        m->file = NULL;
        goto fail;
    }

    m->file = fopen(resolve_path, "a+");

    if (m->file == NULL) {
        hinic3_ds_put_format(ds, HINIC3_UI_FILE_OPEN_FAILED_STRING, HINIC3_UI_LEADING_SIGN_ERROR, resolve_path);
        goto fail;
    }

    if (hinic3_user_scenario_get() != COM_BD) {
        // 如果非combd场景 需要改变文件用户组
        if (hinic3_agent_chown_output_file_path(resolve_path) != 0) {
            fclose(m->file);
            m->file = NULL;
            goto fail;
        }
    }

    if (hinic3_agent_chmod_output_file_path(resolve_path) != 0) {
        fclose(m->file);
        m->file = NULL;
        goto fail;
    }
    
    hinic3_free(resolve_path);
    return 0;

fail:
    hinic3_free(resolve_path);
    return -1;
}

static void
hinic3_agent_dump_all_flows_to_file_stop(struct hinic3_dump_all_flows_mgmt_t *m)
{
    hinic3_set_dumping_status(m, HINIC3_DUMP_FLOWS_IDLE);
    if (m->file != NULL)
        fclose(m->file);
    if (m->start != NULL)
        (void)hinic3_cmd_flow_dump_done(m->start);
    m->file = NULL;
    m->start = NULL;
    m->is_dpak_flow = false;
    hinic3_agent_free_mem_for_flow(&m->flow);
}

static int
hinic3_dump_flows_run(struct hinic3_dump_all_flows_mgmt_t *m, struct hinic3_dump_flow_count_t *flow_count)
{
    struct ds ds = DS_EMPTY_INITIALIZER;
    size_t len;
    int ret;
    int i;

    for (i = 0; i < HINIC3_DUMP_FLOWS_MAX_ONCE; i++) {
        ret = hinic3_cmd_flow_dump_next(m->start, &m->flow);
        if (ret != 0)
            break;
        hinic3_agent_flow_format_output(HINIC3_HYDRA_TYPE_TABLE_START, &m->flow, &ds);
        flow_count->loss_ct_pkts += m->flow.stats.ct_loss_pkts;
        flow_count->flows++;
        if (hinic3_get_dumping_status(m) == HINIC3_DUMP_FLOWS_STOP) {
            ret = -1;
            break;
        }
    }
    len = fwrite(hinic3_ds_cstr(&ds), strlen(hinic3_ds_cstr(&ds)), 1, m->file);
    if (((len != 1) || (ferror(m->file) != 0)) && ret != HIOVS_EEMPTY) {
        HINIC3_LOG(WARNING, AGENT, "dump all flows to file failed !");
        ret = -1;
    }

    hinic3_ds_destroy(&ds);

    return ret;
}

static int
hinic3_dump_rte_flows_of_bucket(struct hinic3_dump_all_flows_mgmt_t *m, const struct hmap* map, uint32_t *flow_count)
{
    struct ds ds = DS_EMPTY_INITIALIZER;
    size_t len;
    int ret = 0;

    struct hinic3_ufid_hmap_node *node, *next;
    HINIC3_HMAP_FOR_EACH_SAFE(node, next, node, map) {
        hinic3_format_rte_flow(node->key, &ds);
        (*flow_count)++;
        if (hinic3_get_dumping_status(m) == HINIC3_DUMP_FLOWS_STOP) {
            ret = -1;
            goto end;
        }
    }

    len = fwrite(hinic3_ds_cstr(&ds), strlen(hinic3_ds_cstr(&ds)), 1, m->file);
    if (((len != 1) || (ferror(m->file) != 0)) && ret != HIOVS_EEMPTY) {
        HINIC3_LOG(WARNING, AGENT, "dump all rte flows to file failed !");
        ret = -1;
    }
end:
    hinic3_ds_destroy(&ds);
    return ret;
}

static int
hinic3_dump_rte_flows_run(struct hinic3_dump_all_flows_mgmt_t *m, uint32_t *flow_count)
{
    const struct hinic3_ufid_map_table* table = hinic3_get_rte_flow_map_table();
    for (int i = 0; i < OFFLOAD_FLOW_BUCKETS; ++i) {
        const struct hmap* map= &table->hash_table_array[i].key_hmap;
        if (hinic3_hmap_is_empty(map)) {
            continue;
        }
        if (hinic3_dump_rte_flows_of_bucket(m, map, flow_count) != 0)
            return -1;
    }
    return 0;
}

static void *
hinic3_dump_flows_running_thread(void *args HINIC3_UNUSED)
{
    struct hinic3_dump_all_flows_mgmt_t *m = hinic3_cmd_get_flow_dump_mgmt();
    struct hinic3_dump_flow_count_t flow_count = {0};
    size_t len;
    int ret;

    const char *name = "hinic3_dump_flow";
    ret = hinic3_set_thread_name_by_str(name);
    if (ret != 0)
        HINIC3_LOG(WARNING, AGENT, "set dump flow thread name failed!");

    if (!m->is_dpak_flow) {
        if (m->flow.actions == NULL || m->flow.key == NULL || m->flow.mask == NULL) {
            hinic3_agent_dump_all_flows_to_file_stop(m);
            return NULL;
        }
        do {
            ret = hinic3_dump_flows_run(m, &flow_count);
        } while (ret == 0);
    } else {
        ret = hinic3_dump_rte_flows_run(m, &flow_count.flows);
    }

    struct ds ds_flow = DS_EMPTY_INITIALIZER;
    if (!m->is_dpak_flow) {
        hinic3_ds_put_format(&ds_flow, "%s%" PRIu32 ", %s%" PRIu64 "\n", HINIC3_UI_FLOW_DUMP_TOTAL_FLOW_STRING,
            flow_count.flows, HINIC3_UI_FLOW_DUMP_TOTAL_CT_LOST_PKT_STRING, flow_count.loss_ct_pkts);
    } else {
        hinic3_ds_put_format(&ds_flow, "%s%" PRIu32 "\n", HINIC3_UI_FLOW_DUMP_TOTAL_FLOW_STRING, flow_count.flows);
    }

    len = fwrite(hinic3_ds_cstr(&ds_flow), strlen(hinic3_ds_cstr(&ds_flow)), 1, m->file);
    if ((len != 1) && (ferror(m->file) != 0))
        HINIC3_LOG(WARNING, AGENT, "dump flow statistics to file failed !");

    hinic3_ds_destroy(&ds_flow);
    hinic3_agent_dump_all_flows_to_file_stop(m);
    return NULL;
}

static int
hinic3_agent_dump_all_flows_to_file_run(struct hinic3_dump_all_flows_mgmt_t *m, struct ds *ds)
{
    int ret;
    struct hinic3_dump_all_flows_mgmt_t *mgmt = hinic3_cmd_get_flow_dump_mgmt();

    if (hinic3_get_dumping_status(m) == HINIC3_DUMP_FLOWS_STOP) {
        hinic3_agent_dump_all_flows_to_file_stop(m);
        return 0;
    }

    if (hinic3_get_dumping_status(m) == HINIC3_DUMP_FLOWS_RUNNING) {
        ret = pthread_create(&mgmt->dump_thread, NULL, hinic3_dump_flows_running_thread, NULL);
        if (ret != 0) {
            hinic3_ds_put_format(ds, "%s%s\n", HINIC3_UI_LEADING_SIGN_FAILURE,
                HINIC3_UI_FLOW_DUMP_RUN_THREAD_FAILED_STRING);
            return -1;
        }
        hinic3_set_ctrl_thread_cpu_affinity(&mgmt->dump_thread);
        return 0;
    }
    return 0;
}


static int
hinic3_agent_dump_all_flows_to_file_start(struct hinic3_dump_all_flows_mgmt_t *m, struct ds *ds)
{
    int ret;
    hinic3_pthread_mutex_lock(&m->mutex_lock);
    if (m->dumping == HINIC3_DUMP_FLOWS_RUNNING) {
        hinic3_pthread_mutex_unlock(&m->mutex_lock);
        hinic3_ds_put_format(ds, "%s%s\n", HINIC3_UI_LEADING_SIGN_FAILURE, HINIC3_UI_FLOW_DUMP_FLOW_BUSY_STRING);
        return -1;
    }
    m->dumping = HINIC3_DUMP_FLOWS_RUNNING;
    hinic3_pthread_mutex_unlock(&m->mutex_lock);

    if (m->is_dpak_flow) {
        goto end;
    }

    if (hinic3_card_mod_get() == PROG_MODE) {
       ret = hinic3_flexda_cmd_flow_dump_start_by_table_id(1, &m->start);
    } else {
       ret = hinic3_cmd_flow_dump_start(&m->start);
    }

    if (ret != 0) {
        if (ret != HOWFF_NO_FLOW_ERR) {
            hinic3_ds_put_format(ds, "%s%s\n", HINIC3_UI_LEADING_SIGN_FAILURE,
                HINIC3_UI_FLOW_DUMP_ALL_FLOW_START_FAILED_STRING);
        } else {
            hinic3_ds_put_format(ds, "%s%s\n", HINIC3_UI_LEADING_SIGN_INFO, HINIC3_UI_FLOW_DUMP_NO_AVALIBLE_FLOW_STRING);
        }
        hinic3_agent_dump_all_flows_to_file_stop(m);
        hinic3_set_dumping_status(m, HINIC3_DUMP_FLOWS_IDLE);
        return -1;
    }
    ret = hinic3_alloc_mem_for_flow(&m->flow);
    if (ret != 0) {
        hinic3_agent_dump_all_flows_to_file_stop(m);
        hinic3_set_dumping_status(m, HINIC3_DUMP_FLOWS_IDLE);
        return -1;
    }
end:
    hinic3_ds_put_format(ds, "%s%s\n",
        HINIC3_UI_LEADING_SIGN_INFO, HINIC3_UI_FLOW_DUMP_ALL_FLOW_START_SUCCESS_STRING);

    return 0;
}

int
hinic3_agent_dump_flows_to_file(const char *filename, struct ds *ds)
{
    struct hinic3_dump_all_flows_mgmt_t *mgmt = hinic3_cmd_get_flow_dump_mgmt();

    if (hinic3_get_dumping_status(mgmt) != HINIC3_DUMP_FLOWS_IDLE) {
        hinic3_ds_put_format(ds, "%s%s\n", HINIC3_UI_LEADING_SIGN_FAILURE, HINIC3_UI_FLOW_DUMP_ALL_FLOW_TIPS_STRING);
        return -1;
    }
    
    if ((hinic3_get_dumping_status(mgmt) == HINIC3_DUMP_FLOWS_RUNNING) || mgmt->file || mgmt->start) {
        hinic3_ds_put_format(ds, "%s%s\n", HINIC3_UI_LEADING_SIGN_FAILURE, HINIC3_UI_FLOW_DUMP_FLOW_BUSY_STRING);
        return -1;
    }
    memset(&mgmt->flow, 0, sizeof(struct hinic3_dpif_flow_for_get));
    int ret = hinic3_agent_parse_output_file_path(mgmt, filename, ds);
    if (ret != 0)
        return -1;
    ret = hinic3_agent_dump_all_flows_to_file_start(mgmt, ds);
    if (ret != 0)
        return -1;
    return hinic3_agent_dump_all_flows_to_file_run(mgmt, ds);
}
