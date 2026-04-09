/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2024 Huawei Technologies Co., Ltd
 */

#include <stdlib.h>
#include <inttypes.h>
#include "hinic3_ds.h"
#include "hinic3_command.h"
#include "hinic3_ui_string.h"
#include "hinic3_mpool_rte_flow.h"
#include "hinic3_cmd_meminfo.h"

#define HINIC3_MODULE_NAME_MAX_LEN 30

void
hinic3_show_meminfo_command(struct unixctl_conn *conn, int argc HINIC3_UNUSED,
    const char *argv[] HINIC3_UNUSED, void *aux HINIC3_UNUSED)
{
    struct ds ds = DS_EMPTY_INITIALIZER;
    struct hinic3_mem_statistics *mem_stats = hinic3_get_meminfo();
    uint64_t total_size = 0;

    for (int i = 0; i < HINIC3_MODULE_MAX; i++) {
        uint64_t module_size = 0;
        struct hinic3_module_meminfo *module_meminfo = &mem_stats->module_meminfo[i];

        hinic3_rwlock_rdlock(&module_meminfo->rwlock);
        module_size = module_meminfo->allocated_size;
        hinic3_rwlock_rdunlock(&module_meminfo->rwlock);
        total_size += module_size;
        char module_name[HINIC3_MODULE_NAME_MAX_LEN] = {0};
        int ret = snprintf(module_name, sizeof(module_name), "%s:", hinic3_get_module_name_from_module_id(i));
        if (ret <= 0) {
            HINIC3_LOG(ERR, AGENT, "snprintf for module_name failed.");
            return;
        }
        hinic3_ds_put_format(&ds, "%2s%-40s%u \n", HINIC3_UI_INDENT_SPACE, module_name, module_size);
    }
    hinic3_ds_put_format(&ds, "\n");
    hinic3_ds_put_format(&ds, "%2s%-40s%u \n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_MEMINFO_TOTAL_ALLOCATED_STRING, total_size);
    hinic3_ds_put_format(&ds, "%2s%s\n", HINIC3_UI_INDENT_SPACE, HINIC3_UI_MEMINFO_UNIT_STRING);

    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
}

static uint64_t
hinic3_format_hugepage_meminfo(struct ds *ds, struct hinic3_module_hugepage_meminfo *module_meminfo)
{
    uint64_t module_size = 0;
    uint64_t total_size = 0;

    hinic3_rwlock_rdlock(&module_meminfo->rwlock);
    for (int i = 0; i < HINIC3_MODULE_MAX; i++) {
        module_size = module_meminfo->allocated_size[i];
        total_size += module_size;
        char module_name[HINIC3_MODULE_NAME_MAX_LEN] = {0};
        int ret = snprintf(module_name, sizeof(module_name), "%s:", hinic3_get_module_name_from_module_id(i));
        if (ret <= 0) {
            HINIC3_LOG(ERR, AGENT, "snprintf for module_name failed.");
            return 0;
        }
        hinic3_ds_put_format(ds, "%4s%-30s%u\n",
            HINIC3_UI_INDENT_SPACE, module_name, module_size);
    }
    hinic3_rwlock_rdunlock(&module_meminfo->rwlock);
    return total_size;
}

void
hinic3_show_hugepage_meminfo_command(struct unixctl_conn *conn, int argc HINIC3_UNUSED,
    const char *argv[] HINIC3_UNUSED, void *aux HINIC3_UNUSED)
{
    struct ds ds = DS_EMPTY_INITIALIZER;
    uint64_t total_size = 0;
    struct hinic3_rte_mempool_ele *iter_pool = NULL;
    struct hinic3_rte_meminfo_list *mempool_list = NULL;
    struct hinic3_hugepage_mem_statistics *mem_stats = hinic3_get_hugepage_meminfo();

    if (mem_stats == NULL) { // 大页内存配置为disable分支
        hinic3_ds_put_format(&ds, "%s%s\n", HINIC3_UI_LEADING_SIGN_INFO, HINIC3_UI_HUGEPAGE_MEMINFO_DISABLED_STRING);
        goto out;
    }

    hinic3_ds_put_format(&ds, "%2s%s\n", HINIC3_UI_INDENT_SPACE, HINIC3_UI_HEAP_STATISTICS_STRING);
    total_size += hinic3_format_hugepage_meminfo(&ds, &mem_stats->heap_meminfo);
    hinic3_ds_put_format(&ds, "\n");
    hinic3_ds_put_format(&ds, "%2s%s\n", HINIC3_UI_INDENT_SPACE, HINIC3_UI_MEMZONE_STATISTICS_STRING);
    total_size += hinic3_format_hugepage_meminfo(&ds, &mem_stats->memzone_meminfo);
    hinic3_ds_put_format(&ds, "\n");

    mempool_list = &mem_stats->mempool_list;
    hinic3_ds_put_format(&ds, "%2s%s\n", HINIC3_UI_INDENT_SPACE, HINIC3_UI_MEMPOOL_STATISTICS_STRING);
    hinic3_pthread_mutex_lock(&mempool_list->mutex);
    LIST_FOR_EACH(iter_pool, node, &mempool_list->list_head)
    {
        total_size += iter_pool->allocated_size;
        hinic3_ds_put_format(&ds, "%4s%-30s%u\n",
            HINIC3_UI_INDENT_SPACE, iter_pool->name, iter_pool->allocated_size);
        hinic3_ds_put_format(&ds, "%4s%-30s%u /"
        " %u\n", HINIC3_UI_INDENT_SPACE, HINIC3_UI_MEMINFO_AVAILABLE_ELEM_STRING,
            rte_mempool_avail_count(iter_pool->mempool), iter_pool->mempool->size);
    }
    hinic3_pthread_mutex_unlock(&mempool_list->mutex);
    hinic3_ds_put_format(&ds, "\n");

    hinic3_ds_put_format(&ds, "%2s%s\n", HINIC3_UI_INDENT_SPACE, HINIC3_UI_TOTAL_STATISTICS_STRING);
    hinic3_ds_put_format(&ds, "%4s%-30s%u\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_MEMINFO_TOTAL_ALLOCATED_STRING, total_size);
    hinic3_ds_put_format(&ds, "\n");
    hinic3_ds_put_format(&ds, "%2s%s\n", HINIC3_UI_INDENT_SPACE, HINIC3_UI_MEMINFO_UNIT_STRING);
out:
    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
}

void
unixctl_hinic3_cmd_meminfo_register(void)
{
    hinic3_command_register("hwoff/show-meminfo", "", 0, 0, hinic3_show_meminfo_command, NULL);
    hinic3_command_register("hwoff/show-hugepage-meminfo", "", 0, 0, hinic3_show_hugepage_meminfo_command, NULL);
    hinic3_command_register("hwoff/show-mpool-stats", "", 0, 0, hinic3_dump_mempool_info, NULL);
}
