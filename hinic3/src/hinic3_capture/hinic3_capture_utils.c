/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */

#include <errno.h>
#include <unistd.h>
#include <sys/eventfd.h>
#include <sys/epoll.h>
#include <sched.h>
#include <time.h>
#include "rte_cycles.h"
#include "rte_lcore.h"
#include "rte_spinlock.h"

#include "hinic3_log.h"
#include "hinic3_init.h"
#include "hinic3_iface_port.h"
#include "hinic3_capture_core.h"
#include "hinic3_capture_filter.h"
#include "hinic3_capture_main.h"
#include "hinic3_nlattr.h"
#include "hinic3_util.h"
#include "rte_ethdev.h"
#include "hinic3_port_util.h"
#include "hinic3_check_thread_health_state.h"
#include "hinic3_cmd_exec.h"
#include "hinic3_meminfo.h"
#include "hinic3_hugepage_meminfo.h"
#include "hinic3_ds.h"
#include "hinic3_ui_string.h"
#include "hinic3_capture_utils.h"

#define HINIC3_EVENT_FD_INVALID (-1)
uint64_t g_ticks_per_ms;
static uint32_t g_pcap_id_begin = 1;
static bool g_pcap_rule_idx[PCAP_MAX_CAP_TASK] = {0};
struct rte_mempool *g_pcap_shared_mp = NULL;

static int g_cap_switch = 0;
static int g_pcap_task = 0;
static enum PCAP_CPU_USAGE_TYPE g_cap_cpu_usage = PCAP_CPU_HIGH;
static enum hinic3_pcap_mode g_current_pcap_mode = ONLY_HEADER;
static long long g_no_pcap_task_time = 0;
struct pcap_task_mgr_t g_cap_task_mgr;
struct pcap_task_save_t g_cap_task_save = {0};

static long long period_us = 0;
static long long target_work_us = 0;
static long long work_us = 0;
static long long integral_err = 0;

struct pcap_task_mgr_t *
pcap_get_task_mgr(void)
{
    return &g_cap_task_mgr;
}

void pcap_task_mgr_spin_lock(void)
{
    rte_spinlock_lock(&g_cap_task_mgr.lock);
}

void pcap_task_mgr_spin_unlock(void)
{
    rte_spinlock_unlock(&g_cap_task_mgr.lock);
}

void pcap_task_spin_lock(struct pcap_task_t *task)
{
    rte_spinlock_lock(&task->lock);
}

void pcap_task_spin_unlock(struct pcap_task_t *task)
{
    rte_spinlock_unlock(&task->lock);
}

static inline int
pcap_idle_rule_index_occupy(void)
{
    int i;
    for (i = 0; i < PCAP_MAX_CAP_TASK; i++)
    {
        if (!g_pcap_rule_idx[i])
        {
            g_pcap_rule_idx[i] = true;
            return i;
        }
    }
    return -1;
}

static void
pcap_rule_index_free(int idx)
{
    g_pcap_rule_idx[idx] = false;
}

static int
hinic3_get_port_info(uint16_t port_id, struct pcap_port_t *port_info)
{
    struct rte_eth_dev_info info = {0};

    int ret = rte_eth_dev_info_get(port_id, &info);
    if (ret != 0)
    {
        return -1;
    }

    uint32_t if_index = info.if_index;
    if (if_index > UINT16_MAX)
    {
        HINIC3_LOG(ERR, CAPTURE, "if_index %u is invalid!", if_index);
        return -1;
    }

    const char *driver_name = info.driver_name;
    if (driver_name == NULL || strcmp(driver_name, HINIC3_ETH_VDEV_DRV_NAME) != 0)
    {
        return -1;
    }
    port_info->hinic3_port_id = (uint16_t)if_index;
    port_info->odp_port_no = (uint16_t)if_index;

    return 0;
}

int pcap_port_info_get_by_name(const char *port_name, struct pcap_port_t *port_info, struct ds *ds)
{
    int ret;
    uint16_t port_id = 0;
    char name[PCAP_MAX_PORT_COMBINE_NAME] = {0};

    ret = snprintf(name, PCAP_MAX_PORT_COMBINE_NAME - 1, "%s%s", HINIC3_ETH_VDEV_DRV_NAME, port_name);
    if (ret <= 0 || strlen(port_name) > PCAP_MAX_PORT_NAME)
    {
        HINIC3_LOG(ERR, CAPTURE, "The port name is misspelled!");
        return -1;
    }

    ret = rte_eth_dev_get_port_by_name(name, &port_id);
    if (ret != 0)
    {
        hinic3_ds_put_format(ds, "%sCan not find device in dp for name %s.\n", HINIC3_UI_LEADING_SIGN_ERROR, name);
        return -1;
    }

    strcpy(port_info->name, port_name);
    if (port_info->name == NULL)
    {
        HINIC3_LOG(ERR, CAPTURE, "strcpy_s port name fail!");
        hinic3_ds_put_format(ds, "%sInternal error.\n", HINIC3_UI_LEADING_SIGN_FAILURE);
        return -1;
    }

    ret = hinic3_get_port_info(port_id, port_info);
    if (ret != 0)
    {
        hinic3_ds_put_format(ds, "%sFailed to obtain the port information.\n", HINIC3_UI_LEADING_SIGN_FAILURE);
        return -1;
    }

    port_info->support_cap = true;
    return 0;
}

static bool
pcap_timeout_check(uint64_t start_tsc, uint64_t timeout_ms)
{
    uint64_t now;

    now = rte_rdtsc();
    if ((now - start_tsc) > timeout_ms * g_ticks_per_ms)
        return true;

    return false;
}

bool pcap_timeout_check_s(long long start_sec, long long timeout_s)
{
    long long cur_time;

    cur_time = hinic3_time_sec();
    if ((cur_time - start_sec) > timeout_s)
        return true;

    return false;
}

static void
pcap_task_stop_reply_format(struct pcap_task_t *task, struct ds *ds)
{
    const char *tmp_str = NULL;
    struct pcap_stats_t *stats = &task->stats;
    char full_path[PCAP_MAX_FILE_NAME];

    if (task->key.output_path[strlen(task->key.output_path) - 1] == '/') {
        snprintf(full_path, sizeof(full_path), "%s%s", task->key.output_path, task->key.filename);
    } else {
        snprintf(full_path, sizeof(full_path), "%s/%s", task->key.output_path, task->key.filename);
    }

    hinic3_ds_put_format(ds, "%2sstopped pcap id:  %u \n", HINIC3_UI_INDENT_SPACE, task->pcap_id);
    tmp_str = task->wr_fail_flag ? "true" : "false";
    hinic3_ds_put_format(ds, "%2scaptured:         %llu \n",
                         HINIC3_UI_INDENT_SPACE, (unsigned long long)stats->wr_cnt);
    hinic3_ds_put_format(ds, "%2senqueue-fail:     %llu \n",
                         HINIC3_UI_INDENT_SPACE, (unsigned long long)stats->en_ring_fail_cnt);
    hinic3_ds_put_format(ds, "%2sdrop:             %llu \n",
                         HINIC3_UI_INDENT_SPACE, (unsigned long long)stats->soft_drop_cnt);
    hinic3_ds_put_format(ds, "%2swrite-success:    %s\n",
                         HINIC3_UI_INDENT_SPACE, tmp_str);
    hinic3_ds_put_format(ds, "%2soutput path:      %s\n", HINIC3_UI_INDENT_SPACE, full_path);

    hinic3_ds_put_format(ds, "%2shardware card:\n", HINIC3_UI_INDENT_SPACE);
    hinic3_ds_put_format(ds, "%4scaptured:       %llu\n",
                         HINIC3_UI_INDENT_SPACE, (unsigned long long)stats->hinic3_stats.pkts_pcap_cnt);
    hinic3_ds_put_format(ds, "%4sdrop:           %llu\n",
                         HINIC3_UI_INDENT_SPACE, (unsigned long long)stats->hinic3_stats.pkts_drop_cnt);
}

static void
pcap_task_batch_reply_format(struct pcap_task_batch *task_batch, const char *prefix, struct ds *ds)
{
    int i;
    struct pcap_task_t *pcap_task_record = NULL;

    if (task_batch->count <= 0)
    {
        return;
    }

    hinic3_ds_put_format(ds, "stop %s tasks: ", prefix);
    for (i = 0; i < task_batch->count; i++)
    {
        pcap_task_record = task_batch->task_array[i];
        hinic3_ds_put_format(ds, "%u ", pcap_task_record->pcap_id);
    }
    hinic3_ds_put_cstr(ds, "\n");
}

static int
pcap_driver_exec(struct pcap_task_t *task, struct hinic3_nlattr *reply_nla)
{
    int ret;
    void *buff = NULL;
    char *tmp_value = NULL;
    struct hinic3_nlattr set_nla;
    struct hinic3_drv_ops *ops = NULL;

    ops = hinic3_get_drv_ops();
    if (hinic3_card_mod_get() == PROG_MODE)
    {
        HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_flexda_global_cfg_set, HINIC3_DRV_FUNC_NO_PTR);
    }
    else
    {
        HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_global_cfg_set, HINIC3_DRV_FUNC_NO_PTR);
    }

    buff = (void *)hinic3_calloc(1, HOVS_MAX_TLV_BUF_LEN, HINIC3_CAPTURE);
    if (!buff)
    {
        HINIC3_LOG(ERR, CAPTURE, "pcap_driver_exec calloc fail!");
        return -1;
    }

    tmp_value = buff;
    hinic3_nlattr_init(&set_nla, tmp_value, HOVS_MAX_TLV_BUF_LEN);
    hinic3_nlattr_put_unspec(&set_nla, HINIC3_GLOBAL_CFG_ARG_PCAP_PROBE, &task->driver.driver_filter,
                             sizeof(struct hinic3_pcap_probe_filter_t));
    if (hinic3_card_mod_get() == PROG_MODE)
    {
        ret = ops->hovs_flexda_global_cfg_set(
            (struct nlattr *)set_nla.data, set_nla.used_len, (struct nlattr *)reply_nla->data, &reply_nla->used_len);
    }
    else
    {
        ret = ops->hovs_global_cfg_set(
            (struct nlattr *)set_nla.data, set_nla.used_len, (struct nlattr *)reply_nla->data, &reply_nla->used_len);
    }

    if (ret != 0)
    {
        HINIC3_LOG(ERR, CAPTURE, "hovs_global_cfg_set exec fail, ret is %d!", ret);
        hinic3_free(buff);
        return -1;
    }

    hinic3_free(buff);
    return 0;
}

static int
pcap_extract_start_reply(struct pcap_task_t *task, struct hinic3_nlattr *reply_nla)
{
    int ret;
    bool flag = false;
    hinic3_nlattr_itr nla_itr = NULL;
    const struct hinic3_pcap_probe_rule_q_map *rule_q_map = NULL;

    HINIC3_NLATTR_FOR_EACH(nla_itr, reply_nla)
    {
        if (hinic3_nlattr_get_itr_type(nla_itr) == HINIC3_GLOBAL_CFG_ARG_PCAP_PROBE)
        {
            flag = true;
            break;
        }
    }
    if (!flag)
    {
        HINIC3_LOG(ERR, CAPTURE, "no reply info from hovs_global_cfg_set!");
        return -1;
    }

    rule_q_map = (const struct hinic3_pcap_probe_rule_q_map *)hinic3_nlattr_get_itr_data(nla_itr);
    task->driver.queue_info.hinic3_dpdk_port_id = rule_q_map->hinic3_dpdk_port_id;
    task->driver.queue_info.queue_id = rule_q_map->queue_id;
    task->driver.queue_info.rule_idx = rule_q_map->rule_idx;
    HINIC3_LOG(INFO, CAPTURE, "start capture in hardware success, dpdk_port_id is %u, queue is %u, rule_idx is %u.",
               rule_q_map->hinic3_dpdk_port_id, rule_q_map->queue_id, rule_q_map->rule_idx);

    if (g_pcap_shared_mp == NULL)
    {
        HINIC3_LOG(ERR, CAPTURE, "pcap mempool has not been created!");
        return -1;
    }

    ret = hinic3_port_mgmt_setup_upcall_queue(rule_q_map->hinic3_dpdk_port_id, rule_q_map->queue_id,
                                              0, g_pcap_shared_mp);
    if (ret != 0)
    {
        HINIC3_LOG(ERR, CAPTURE, "pcap setup upcall queue error.");
        return -1;
    }

    return 0;
}

static int
pcap_extract_stop_reply(struct pcap_task_t *task, struct hinic3_nlattr *reply_nla)
{
    int ret;
    bool flag = false;
    hinic3_nlattr_itr nla_itr = NULL;
    const struct hinic3_pcap_probe_stats *hw_stats = NULL;

    HINIC3_NLATTR_FOR_EACH(nla_itr, reply_nla)
    {
        if (hinic3_nlattr_get_itr_type(nla_itr) == HINIC3_GLOBAL_GET_PCAP_PROBE_STATS)
        {
            flag = true;
            break;
        }
    }
    if (!flag)
    {
        HINIC3_LOG(ERR, CAPTURE, "no reply info from hovs_global_cfg_set!");
        return -1;
    }

    hw_stats = (const struct hinic3_pcap_probe_stats *)hinic3_nlattr_get_itr_data(nla_itr);
    task->stats.hinic3_stats.pkts_pcap_cnt = hw_stats->pkts_pcap_cnt;
    task->stats.hinic3_stats.pkts_drop_cnt = hw_stats->pkts_drop_cnt;

    ret = hinic3_port_mgmt_release_upcall_queue(task->driver.queue_info.hinic3_dpdk_port_id,
                                                task->driver.queue_info.queue_id);
    if (ret != 0)
    {
        HINIC3_LOG(ERR, CAPTURE, "pcap release upcall queue error!");
        return -1;
    }

    return 0;
}

static int
pcap_start_in_driver(struct pcap_task_t *task)
{
    int ret;
    void *buff = NULL;
    char *tmp_value = NULL;
    struct hinic3_nlattr reply_nla;

    buff = (void *)hinic3_calloc(1, HOVS_MAX_TLV_BUF_LEN, HINIC3_CAPTURE);
    if (!buff)
    {
        HINIC3_LOG(ERR, CAPTURE, "The pcap start calloc buff failed!");
        return -1;
    }

    tmp_value = buff;
    hinic3_nlattr_init(&reply_nla, tmp_value, HOVS_MAX_TLV_BUF_LEN);
    ret = pcap_driver_exec(task, &reply_nla);
    if (ret != 0)
        goto fail;

    hinic3_nlattr_reset_itr(&reply_nla, reply_nla.used_len);
    ret = pcap_extract_start_reply(task, &reply_nla);
    if (ret != 0)
        goto fail;

    hinic3_free(buff);
    return 0;

fail:
    hinic3_free(buff);
    return -1;
}

static int
pcap_stop_in_driver(struct pcap_task_t *task)
{
    int ret;
    void *buff = NULL;
    char *tmp_value = NULL;
    struct hinic3_nlattr reply_nla;

    buff = (void *)hinic3_calloc(1, HOVS_MAX_TLV_BUF_LEN, HINIC3_CAPTURE);
    if (!buff)
    {
        HINIC3_LOG(ERR, CAPTURE, "The pcap stop calloc buff failed!");
        return -1;
    }

    tmp_value = buff;
    hinic3_nlattr_init(&reply_nla, tmp_value, HOVS_MAX_TLV_BUF_LEN);
    ret = pcap_driver_exec(task, &reply_nla);
    if (ret != 0)
        goto fail;

    hinic3_nlattr_reset_itr(&reply_nla, reply_nla.used_len);
    ret = pcap_extract_stop_reply(task, &reply_nla);
    if (ret != 0)
        goto fail;

    hinic3_free(buff);
    return 0;

fail:
    hinic3_free(buff);
    return -1;
}

void pcap_task_stats_update(struct pcap_task_t *cap_task, struct pcap_save_stats_t *save_stats)
{
    pcap_task_spin_lock(cap_task);
    if (cap_task->remain_count <= save_stats->wr_cnt)
        cap_task->remain_count = 0;
    else
        cap_task->remain_count -= save_stats->wr_cnt;

    if (save_stats->write_fail)
        cap_task->wr_fail_flag = true;
    cap_task->stats.wr_cnt += save_stats->wr_cnt;
    cap_task->stats.en_ring_fail_cnt += save_stats->en_ring_fail_cnt;
    cap_task->stats.soft_drop_cnt += save_stats->parse_fail_cnt;
    pcap_task_spin_unlock(cap_task);
}

void pcap_task_mirror_get(struct pcap_task_t *cap_task, struct pcap_task_mirror_t *task_mirror)
{
    pcap_task_spin_lock(cap_task);
    task_mirror->stop_flag = cap_task->stop_flag;
    task_mirror->wr_fail_flag = cap_task->wr_fail_flag;
    task_mirror->remain_count = cap_task->remain_count;
    pcap_task_spin_unlock(cap_task);
}

static int
pcap_task_driver_start(struct pcap_task_t *task)
{
    int ret;
    int port_task_cnt;
    struct hinic3_pcap_probe_filter_t *filter = &task->driver.driver_filter;

    pcap_convert_key_to_driver_filter(task);
    filter->rule_cfg.cap_id = task->rule_idx;
    filter->rule_cfg.vport_id = task->pcap_port.hinic3_port_id;

    filter->pcap_ctl.rule_idx = task->rule_idx;
    filter->pcap_ctl.enable = PCAP_PROBE_ENABLE;
    filter->pcap_ctl.hinic3_port_id = task->pcap_port.hinic3_port_id;

    port_task_cnt = pcap_port_tasks_count_no_lock(task->pcap_port.odp_port_no);
    filter->pcap_ctl.first_last_rule = (port_task_cnt == 0);

    ret = pcap_start_in_driver(task);
    return ret;
}

static int
pcap_task_driver_stop(struct pcap_task_t *task)
{
    int ret;
    int port_task_cnt;
    struct hinic3_pcap_probe_filter_t *filter = &task->driver.driver_filter;

    port_task_cnt = pcap_port_tasks_count_no_lock(task->pcap_port.odp_port_no);
    filter->pcap_ctl.enable = PCAP_PROBE_DISABLE;
    filter->pcap_ctl.first_last_rule = (port_task_cnt == 1);

    ret = pcap_stop_in_driver(task);
    return ret;
}

static int
pcap_task_ring_create(struct pcap_task_t *task)
{
    int ret;
    char *tmp_str = NULL;
    struct rte_ring *ring = NULL;
    char ring_name[PCAP_MAX_RING_NAME] = {0};

    ret = sprintf(ring_name, "%s_%u", PCAP_RING_NAME_PREFIX, task->pcap_id);
    if (ret < 0)
    {
        HINIC3_LOG(ERR, CAPTURE, "sprintf_s fail, err is %d!", ret);
        return -1;
    }

    tmp_str = ring_name;
    ring = rte_ring_lookup(tmp_str);
    if (ring)
    {
        task->ring = ring;
        return 0;
    }

    ring = hinic3_ring_create(tmp_str, PCAP_RING_SIZE, rte_socket_id(), 0, HINIC3_CAPTURE);
    if (!ring)
    {
        HINIC3_LOG(ERR, CAPTURE, "hinic3_ring_create fail, pcap_id is %u!", task->pcap_id);
        return -1;
    }

    task->ring = ring;
    return 0;
}

static int
pcap_task_epoll_fd_create(struct pcap_task_t *task)
{
    int ret;
    struct epoll_event event;
    struct pcap_task_mgr_t *task_mgr = pcap_get_task_mgr();

    task->event_fd = eventfd(0, EFD_NONBLOCK);
    if (task->event_fd < 0)
    {
        HINIC3_LOG(ERR, CAPTURE, "Create eventfd fail, errno is %d!", errno);
        return -1;
    }

    memset(&event, 0, sizeof(event));
    event.events = EPOLLIN;
    event.data.u32 = task->pcap_id;

    ret = epoll_ctl(task_mgr->epoll_fd, EPOLL_CTL_ADD, task->event_fd, &event);
    if (ret != 0)
    {
        HINIC3_LOG(ERR, CAPTURE, "EPOLL_CTL_ADD fail, ret is %d, errno is %d!", ret, errno);
        close(task->event_fd);
        task->event_fd = HINIC3_EVENT_FD_INVALID;
        return -1;
    }

    return 0;
}

static int
pcap_task_resource_create(struct pcap_task_t *task)
{
    int ret;
    struct pcap_key_t *task_key = &task->key;
    char full_path[PCAP_MAX_FILE_NAME];

    rte_spinlock_init(&task->lock);

    HINIC3_LOG(INFO, CAPTURE, "pcap_task_resource_create output_path=%s, filename=%s",
        task_key->output_path, task_key->filename);
    if (task_key->output_path[strlen(task_key->output_path) - 1] == '/') {
        snprintf(full_path, sizeof(full_path), "%s%s", task_key->output_path, task_key->filename);
    } else {
        snprintf(full_path, sizeof(full_path), "%s/%s", task_key->output_path, task_key->filename);
    }
    task->save_file = pcap_file_open(full_path, "wb");
    if (!task->save_file)
        return -1;

    ret = pcap_file_write_header(task->save_file);
    if (ret != 0)
        goto file_free;

    if (task_key->write_way == WRITE_BY_PMD)
        return 0;

    ret = pcap_task_ring_create(task);
    if (ret != 0)
        goto file_free;

    ret = pcap_task_epoll_fd_create(task);
    if (ret != 0)
        goto ring_free;

    return 0;

ring_free:
    hinic3_ring_free(task->ring, HINIC3_CAPTURE);
    task->ring = NULL;
file_free:
    (void)fclose(task->save_file);
    return -1;
}

static void
pcap_task_resource_destroy(struct pcap_task_t *task)
{
    int ret;
    struct pcap_task_mgr_t *task_mgr = pcap_get_task_mgr();

    ret = fflush(task->save_file);
    if (ret != 0)
        HINIC3_LOG(ERR, CAPTURE, "fflush fail, err is %d!", ret);
    (void)fclose(task->save_file);
    task->save_file = NULL;

    if (task->key.write_way == WRITE_BY_PMD)
        return;

    if (task->ring)
        hinic3_ring_free(task->ring, HINIC3_CAPTURE);

    if (task->event_fd > 0)
    {
        epoll_ctl(task_mgr->epoll_fd, EPOLL_CTL_DEL, task->event_fd, NULL);
        close(task->event_fd);
        task->event_fd = HINIC3_EVENT_FD_INVALID;
    }

    return;
}

static struct pcap_task_t *
pcap_task_create(struct pcap_key_t *pcap_key, struct ds *save_param)
{
    int ret;
    int len;
    struct pcap_task_t *cap_task = NULL;

    cap_task = (struct pcap_task_t *)hinic3_calloc(1, sizeof(struct pcap_task_t), HINIC3_CAPTURE);
    if (!cap_task)
    {
        HINIC3_LOG(ERR, CAPTURE, "malloc cap_task failed!");
        return NULL;
    }

    len = strlen(hinic3_ds_cstr(save_param)) + 1;
    cap_task->parameter = (char *)hinic3_calloc(1, len, HINIC3_CAPTURE);
    if (!cap_task->parameter)
    {
        hinic3_free(cap_task);
        HINIC3_LOG(ERR, CAPTURE, "malloc cap_task->parameter failed!");
        return NULL;
    }

    memcpy(cap_task->parameter, hinic3_ds_cstr(save_param), len - 1);
    if (cap_task->parameter == NULL)
    {
        hinic3_free(cap_task->parameter);
        hinic3_free(cap_task);
        return NULL;
    }

    memcpy(&cap_task->key, pcap_key, sizeof(struct pcap_key_t));
    if (&cap_task->key == NULL)
    {
        hinic3_free(cap_task->parameter);
        hinic3_free(cap_task);
        return NULL;
    }

    cap_task->ref_cnt = 1;
    cap_task->remain_count = pcap_key->count_total;
    cap_task->stop_flag = false;
    cap_task->filenum = pcap_key->filenum;
    cap_task->count_per_file = (pcap_key->filenum > 0) ? pcap_key->count : pcap_key->count_total;
    cap_task->file_index = 0;
    cap_task->file_pkt_cnt = 0;
    cap_task->pcap_id = g_pcap_id_begin++;
    ret = pcap_task_resource_create(cap_task);
    if (ret != 0)
    {
        hinic3_free(cap_task->parameter);
        hinic3_free(cap_task);
        return NULL;
    }

    cap_task->rule_idx = pcap_idle_rule_index_occupy();
    return cap_task;
}

void pcap_task_destroy(struct pcap_task_t *cap_task)
{
    HINIC3_LOG(INFO, CAPTURE, "Stop a capture task, pcap_id is %u.", cap_task->pcap_id);
    pcap_task_resource_destroy(cap_task);
    pcap_rule_index_free(cap_task->rule_idx);
    hinic3_free(cap_task->parameter);
    hinic3_free(cap_task);
}

static void
pcap_task_batch_destroy(struct pcap_task_batch *task_batch, struct ds *ds)
{
    int i;
    bool first_flag = true;
    struct pcap_task_t *pcap_task_record = NULL;

    for (i = 0; i < task_batch->count; i++)
    {
        if (!first_flag)
            hinic3_ds_put_cstr(ds, "\n");

        pcap_task_record = task_batch->task_array[i];
        pcap_task_stop_reply_format(pcap_task_record, ds);
        pcap_task_destroy(pcap_task_record);
        first_flag = false;
    }
}

static void
pcap_task_batch_destroy_raw(struct pcap_task_batch *task_batch)
{
    int i;
    struct pcap_task_t *pcap_task_record = NULL;

    for (i = 0; i < task_batch->count; i++)
    {
        pcap_task_record = task_batch->task_array[i];
        pcap_task_destroy(pcap_task_record);
    }
}

/* this function is called by maintain thread, task list will not changed, no need to lock */
bool pcap_check_file_used_no_lock(struct pcap_key_t *cap_key)
{
    int i;
    struct pcap_task_t *cap_task = NULL;
    struct pcap_task_mgr_t *task_mgr = pcap_get_task_mgr();

    if (task_mgr->task_cnt == 0)
    {
        return 0;
    }

    for (i = 0; i < PCAP_MAX_CAP_TASK; i++)
    {
        cap_task = task_mgr->cap_task_list[i];
        if (!cap_task)
            continue;

        if (cap_task->stop_flag)
            continue;

        if (strcmp(cap_task->key.filename, cap_key->filename) == 0)
            return true;
    }

    return false;
}

/* this function is called by maintain thread, task list will not changed, no need to lock */
int pcap_port_tasks_count_no_lock(uint32_t port_no)
{
    int i;
    int count;
    struct pcap_task_t *cap_task = NULL;
    struct pcap_task_mgr_t *task_mgr = pcap_get_task_mgr();

    if (task_mgr->task_cnt == 0)
        return 0;

    count = 0;
    for (i = 0; i < PCAP_MAX_CAP_TASK; i++)
    {
        cap_task = task_mgr->cap_task_list[i];
        if (!cap_task)
            continue;

        if (cap_task->stop_flag)
            continue;

        if (cap_task->pcap_port.odp_port_no != port_no)
            continue;

        count++;
    }

    return count;
}

struct pcap_task_t *
pcap_task_find_no_lock(uint32_t pcap_id, uint32_t *index)
{
    int i;
    struct pcap_task_t *cap_task = NULL;
    struct pcap_task_mgr_t *task_mgr = pcap_get_task_mgr();

    if (task_mgr->task_cnt == 0)
        return NULL;

    for (i = 0; i < PCAP_MAX_CAP_TASK; i++)
    {
        cap_task = task_mgr->cap_task_list[i];
        if (!cap_task)
            continue;

        if (cap_task->pcap_id != pcap_id)
            continue;

        *index = i;
        return cap_task;
    }

    return NULL;
}

void pcap_port_tasks_get(struct pcap_task_batch *task_batch, struct pcap_port_t *port_mirror HINIC3_UNUSED)
{
    int i;
    struct pcap_task_t *cap_task = NULL;
    struct pcap_task_mgr_t *task_mgr = pcap_get_task_mgr();

    pcap_task_mgr_spin_lock();
    if (task_mgr->task_cnt == 0)
    {
        pcap_task_mgr_spin_unlock();
        return;
    }

    for (i = 0; i < PCAP_MAX_CAP_TASK; i++)
    {
        cap_task = task_mgr->cap_task_list[i];
        if (!cap_task)
            continue;

        if (cap_task->stop_flag)
            continue;

        cap_task->ref_cnt++;
        task_batch->task_array[task_batch->count] = cap_task;
        task_batch->count++;
    }

    pcap_task_mgr_spin_unlock();
    return;
}

struct pcap_task_t *
pcap_task_get_by_id(uint32_t pcap_id, struct pcap_task_mirror_t *task_mirror)
{
    int i;
    struct pcap_task_t *cap_task = NULL;
    struct pcap_task_mgr_t *task_mgr = pcap_get_task_mgr();

    pcap_task_mgr_spin_lock();
    if (task_mgr->task_cnt == 0)
    {
        pcap_task_mgr_spin_unlock();
        return NULL;
    }

    for (i = 0; i < PCAP_MAX_CAP_TASK; i++)
    {
        cap_task = task_mgr->cap_task_list[i];
        if (!cap_task)
            continue;

        if (cap_task->stop_flag || (cap_task->remain_count == 0))
            continue;

        if (cap_task->pcap_id != pcap_id)
            continue;

        cap_task->ref_cnt++;
        if (task_mirror)
        {
            task_mirror->stop_flag = cap_task->stop_flag;
            task_mirror->wr_fail_flag = cap_task->wr_fail_flag;
            task_mirror->remain_count = cap_task->remain_count;
        }
        pcap_task_mgr_spin_unlock();
        return cap_task;
    }

    pcap_task_mgr_spin_unlock();
    return NULL;
}

void pcap_task_put(struct pcap_task_t *cap_task)
{
    pcap_task_mgr_spin_lock();
    cap_task->ref_cnt--;
    pcap_task_mgr_spin_unlock();
}

void pcap_task_batch_put(struct pcap_task_batch *task_batch)
{
    int i;

    pcap_task_mgr_spin_lock();
    for (i = 0; i < task_batch->count; i++)
        task_batch->task_array[i]->ref_cnt--;
    pcap_task_mgr_spin_unlock();
}

static uint32_t
pcap_all_vf_tasks_stop_set(uint16_t vport_id, struct pcap_task_batch *fail_task_batch)
{
    int i;
    int ret;
    uint32_t count;
    struct pcap_task_t *cap_task = NULL;
    struct pcap_task_mgr_t *task_mgr = pcap_get_task_mgr();

    if (task_mgr->task_cnt == 0)
        return 0;

    count = 0;
    for (i = 0; i < PCAP_MAX_CAP_TASK; i++)
    {
        cap_task = task_mgr->cap_task_list[i];
        if (!cap_task)
            continue;

        if (cap_task->pcap_port.hinic3_port_id != vport_id)
            continue;

        if (cap_task->stop_flag)
        {
            count++;
            continue;
        }

        pcap_task_spin_lock(cap_task);
        if (!cap_task->driver.stop_flag)
        {
            ret = pcap_task_driver_stop(cap_task);
            if (ret != 0)
            {
                fail_task_batch->task_array[fail_task_batch->count] = cap_task;
                fail_task_batch->count++;
                pcap_task_spin_unlock(cap_task);
                continue;
            }
            cap_task->driver.stop_flag = true;
        }
        cap_task->stop_flag = true;
        pcap_task_spin_unlock(cap_task);
        count++;
    }

    return count;
}

static uint32_t
pcap_all_tasks_stop_set(uint32_t port_no, struct pcap_task_batch *fail_task_batch)
{
    int i;
    int ret;
    uint32_t count;
    struct pcap_task_t *cap_task = NULL;
    struct pcap_task_mgr_t *task_mgr = pcap_get_task_mgr();

    if (task_mgr->task_cnt == 0)
        return 0;

    count = 0;
    for (i = 0; i < PCAP_MAX_CAP_TASK; i++)
    {
        cap_task = task_mgr->cap_task_list[i];
        if (!cap_task)
            continue;

        if (cap_task->pcap_port.odp_port_no != port_no)
            continue;

        if (cap_task->stop_flag)
        {
            count++;
            continue;
        }

        pcap_task_spin_lock(cap_task);
        if (!cap_task->driver.stop_flag)
        {
            ret = pcap_task_driver_stop(cap_task);
            if (ret != 0)
            {
                fail_task_batch->task_array[fail_task_batch->count] = cap_task;
                fail_task_batch->count++;
                pcap_task_spin_unlock(cap_task);
                continue;
            }
            cap_task->driver.stop_flag = true;
        }
        cap_task->stop_flag = true;
        pcap_task_spin_unlock(cap_task);
        count++;
    }

    return count;
}

void pcap_task_stop_set(uint32_t pcap_id, struct pcap_stop_task_ctl *stop_ctl)
{
    int ret;
    struct pcap_task_t *dst_task = NULL;
    struct pcap_task_mgr_t *task_mgr = pcap_get_task_mgr();

    dst_task = pcap_task_find_no_lock(pcap_id, &stop_ctl->index);
    if (!dst_task)
        return;

    stop_ctl->task = dst_task;
    pcap_task_spin_lock(dst_task);
    if (!dst_task->driver.stop_flag)
    {
        ret = pcap_task_driver_stop(dst_task);
        if (ret != 0)
        {
            stop_ctl->result = -1;
            pcap_task_spin_unlock(dst_task);
            return;
        }
    }
    dst_task->driver.stop_flag = true;
    dst_task->stop_flag = true;
    pcap_task_spin_unlock(dst_task);

    pcap_task_mgr_spin_lock();
    if (dst_task->ref_cnt <= 1)
    {
        task_mgr->cap_task_list[stop_ctl->index] = NULL;
        stop_ctl->is_stopped = true;
        task_mgr->task_cnt--;
    }
    pcap_task_mgr_spin_unlock();

    return;
}

static uint32_t
pcap_stopped_vf_tasks_select(uint16_t vport_id, struct pcap_task_batch *stoped_task_batch,
                             struct pcap_task_batch *not_stopped_task_batch)
{
    int i;
    uint32_t remain_cnt;
    struct pcap_task_t *cap_task = NULL;
    struct pcap_task_mgr_t *task_mgr = pcap_get_task_mgr();

    pcap_task_mgr_spin_lock();
    if (task_mgr->task_cnt == 0)
    {
        pcap_task_mgr_spin_unlock();
        return 0;
    }

    remain_cnt = 0;
    for (i = 0; i < PCAP_MAX_CAP_TASK; i++)
    {
        cap_task = task_mgr->cap_task_list[i];
        if (!cap_task)
            continue;

        if (cap_task->pcap_port.hinic3_port_id != vport_id)
            continue;

        if (!cap_task->stop_flag)
            continue;

        if (cap_task->ref_cnt > 1)
        {
            not_stopped_task_batch->task_array[not_stopped_task_batch->count] = cap_task;
            not_stopped_task_batch->count++;
            remain_cnt++;
            continue;
        }

        stoped_task_batch->task_array[stoped_task_batch->count] = cap_task;
        stoped_task_batch->count++;
        task_mgr->cap_task_list[i] = NULL;
        task_mgr->task_cnt--;
    }

    pcap_task_mgr_spin_unlock();
    return remain_cnt;
}

static uint32_t
pcap_stopped_tasks_select(uint32_t port_no, struct pcap_task_batch *stoped_task_batch,
                          struct pcap_task_batch *not_stopped_task_batch)
{
    int i;
    uint32_t remain_cnt;
    struct pcap_task_t *cap_task = NULL;
    struct pcap_task_mgr_t *task_mgr = pcap_get_task_mgr();

    pcap_task_mgr_spin_lock();
    if (task_mgr->task_cnt == 0)
    {
        pcap_task_mgr_spin_unlock();
        return 0;
    }

    remain_cnt = 0;
    for (i = 0; i < PCAP_MAX_CAP_TASK; i++)
    {
        cap_task = task_mgr->cap_task_list[i];
        if (!cap_task)
            continue;

        if (cap_task->pcap_port.odp_port_no != port_no)
            continue;

        if (!cap_task->stop_flag)
            continue;

        if (cap_task->ref_cnt > 1)
        {
            not_stopped_task_batch->task_array[not_stopped_task_batch->count] = cap_task;
            not_stopped_task_batch->count++;
            remain_cnt++;
            continue;
        }

        stoped_task_batch->task_array[stoped_task_batch->count] = cap_task;
        stoped_task_batch->count++;
        task_mgr->cap_task_list[i] = NULL;
        task_mgr->task_cnt--;
    }

    pcap_task_mgr_spin_unlock();
    return remain_cnt;
}

static void
pcap_task_stopped_check(struct pcap_stop_task_ctl *stop_ctl)
{
    struct pcap_task_mgr_t *task_mgr = pcap_get_task_mgr();

    pcap_task_mgr_spin_lock();

    if (stop_ctl->task->ref_cnt > 1)
    {
        pcap_task_mgr_spin_unlock();
        return;
    }

    stop_ctl->is_stopped = true;
    task_mgr->cap_task_list[stop_ctl->index] = NULL;
    task_mgr->task_cnt--;
    pcap_task_mgr_spin_unlock();
    return;
}

int pcap_task_add(const struct pcap_port_t *port_info, struct pcap_key_t *pcap_key, uint32_t *pcap_id,
                  struct ds *save_param)
{
    int i;
    int ret;
    struct pcap_task_t *cap_task = NULL;
    struct pcap_task_mgr_t *task_mgr = pcap_get_task_mgr();

    cap_task = pcap_task_create(pcap_key, save_param);
    if (!cap_task)
        return -1;

    cap_task->pcap_port = *port_info;
    *pcap_id = cap_task->pcap_id;

    ret = pcap_task_driver_start(cap_task);
    if (ret != 0)
    {
        pcap_task_destroy(cap_task);
        return -1;
    }

    pcap_task_mgr_spin_lock();
    for (i = 0; i < PCAP_MAX_CAP_TASK; i++)
    {
        if (task_mgr->cap_task_list[i] == NULL)
        {
            task_mgr->cap_task_list[i] = cap_task;
            task_mgr->task_cnt++;
            break;
        }
    }
    pcap_task_mgr_spin_unlock();
    return 0;
}

int pcap_task_delete_all(uint32_t port_no, struct ds *ds)
{
    bool flag = false;
    uint64_t start_tsc;
    uint32_t remain_cnt;
    struct pcap_task_batch stopped_task_batch;
    struct pcap_task_batch not_stopped_task_batch;
    struct pcap_task_batch stop_fail_task_batch;

    pcap_task_batch_init(&stop_fail_task_batch);
    remain_cnt = pcap_all_tasks_stop_set(port_no, &stop_fail_task_batch);
    if (remain_cnt == 0)
    {
        if (stop_fail_task_batch.count == 0)
        {
            hinic3_ds_put_format(ds, "%sPort isn't in packet capture.\n", HINIC3_UI_LEADING_SIGN_WARNING);
            return -1;
        }

        hinic3_ds_put_format(ds, "%sAll capture tasks stop fail.\n", HINIC3_UI_LEADING_SIGN_FAILURE);
        pcap_task_batch_reply_format(&stop_fail_task_batch, "fail", ds);
        return -1;
    }

    start_tsc = rte_rdtsc();
    while (true)
    {
        rte_delay_ms(PCAP_STOP_CHECK_PERIOD_MS);

        pcap_task_batch_init(&stopped_task_batch);
        pcap_task_batch_init(&not_stopped_task_batch);
        remain_cnt = pcap_stopped_tasks_select(port_no, &stopped_task_batch, &not_stopped_task_batch);
        pcap_task_batch_destroy(&stopped_task_batch, ds);
        if (remain_cnt == 0)
        {
            pcap_task_batch_reply_format(&stop_fail_task_batch, "fail", ds);
            break;
        }

        flag = pcap_timeout_check(start_tsc, PCAP_STOP_ALL_TIME_OUT_MS);
        if (flag)
        {
            pcap_task_batch_reply_format(&not_stopped_task_batch, "timeout", ds);
            pcap_task_batch_reply_format(&stop_fail_task_batch, "fail", ds);
            hinic3_ds_put_format(ds, "%sSome capture tasks stop timeout, please try again later.\n",
                                 HINIC3_UI_LEADING_SIGN_INFO);
            return 0;
        }
    }

    return 0;
}

int pcap_task_delete_one(uint32_t pcap_id, struct ds *ds)
{
    bool flag = false;
    uint64_t start_tsc;
    struct pcap_stop_task_ctl stop_ctl;

    stop_ctl.task = NULL;
    stop_ctl.is_stopped = false;
    stop_ctl.result = 0;
    pcap_task_stop_set(pcap_id, &stop_ctl);
    if (!stop_ctl.task)
    {
        hinic3_ds_put_format(ds, "%sCapture task of pcap id %u not found.\n", HINIC3_UI_LEADING_SIGN_INFO, pcap_id);
        return -1;
    }
    if (stop_ctl.result != 0)
    {
        hinic3_ds_put_format(ds, "%sCapture task of pcap id %u stopped fail, please try again later.\n",
                             HINIC3_UI_LEADING_SIGN_FAILURE, pcap_id);
        return -1;
    }

    if (stop_ctl.is_stopped)
    {
        hinic3_ds_put_format(ds, "%s\n", HINIC3_UI_LEADING_SIGN_INFO);
        pcap_task_stop_reply_format(stop_ctl.task, ds);
        pcap_task_destroy(stop_ctl.task);
        return 0;
    }

    start_tsc = rte_rdtsc();
    while (true)
    {
        rte_delay_ms(PCAP_STOP_CHECK_PERIOD_MS);

        pcap_task_stopped_check(&stop_ctl);
        if (stop_ctl.is_stopped)
        {
            hinic3_ds_put_format(ds, "%s\n", HINIC3_UI_LEADING_SIGN_INFO);
            pcap_task_stop_reply_format(stop_ctl.task, ds);
            pcap_task_destroy(stop_ctl.task);
            break;
        }

        flag = pcap_timeout_check(start_tsc, PCAP_STOP_TIME_OUT_MS);
        if (flag)
        {
            hinic3_ds_put_format(ds, "%sWait pcap id %u stop timeout, please try again later.\n",
                                 HINIC3_UI_LEADING_SIGN_FAILURE, pcap_id);
            return -1;
        }
    }

    return 0;
}

void pcap_task_stop_as_eth_port_del(uint16_t vport_id)
{
    bool flag = false;
    uint64_t start_tsc;
    uint32_t remain_cnt;
    struct pcap_task_batch stopped_task_batch;
    struct pcap_task_batch not_stopped_task_batch;
    struct pcap_task_batch stop_fail_task_batch;

    HINIC3_LOG(INFO, CAPTURE, "To stop capture task on vport_id is %u, as this port will be deleted.", vport_id);
    if (vport_id == HINIC3_PORT_ID_INVALID)
    {
        HINIC3_LOG(ERR, CAPTURE, "pcap_task_stop_as_port_del, vport_id is invalid!");
        return;
    }

    pcap_task_batch_init(&stop_fail_task_batch);
    remain_cnt = pcap_all_vf_tasks_stop_set(vport_id, &stop_fail_task_batch);
    if (remain_cnt == 0)
    {
        if (stop_fail_task_batch.count > 0)
            HINIC3_LOG(INFO, CAPTURE, "Some capture task stop fail, count is %d.", stop_fail_task_batch.count);
        return;
    }

    start_tsc = rte_rdtsc();
    while (true)
    {
        rte_delay_ms(PCAP_STOP_CHECK_PERIOD_MS);

        pcap_task_batch_init(&stopped_task_batch);
        pcap_task_batch_init(&not_stopped_task_batch);
        remain_cnt = pcap_stopped_vf_tasks_select(vport_id, &stopped_task_batch, &not_stopped_task_batch);
        pcap_task_batch_destroy_raw(&stopped_task_batch);
        if (remain_cnt == 0)
            break;

        flag = pcap_timeout_check(start_tsc, PCAP_STOP_ALL_TIME_OUT_MS);
        if (flag)
        {
            HINIC3_LOG(ERR, CAPTURE, "Stop some capture tasks timeout!");
            break;
        }
    }

    return;
}

void pcap_mode_set(enum hinic3_pcap_mode pcap_mode)
{
    g_current_pcap_mode = pcap_mode;
}

enum hinic3_pcap_mode pcap_mode_get(void)
{
    return g_current_pcap_mode;
}

void pcap_cpu_usage_set(enum PCAP_CPU_USAGE_TYPE value)
{
    g_cap_cpu_usage = value;
}

enum PCAP_CPU_USAGE_TYPE pcap_cpu_usage_get(void)
{
    return g_cap_cpu_usage;
}

int pcap_switch_get(void)
{
    return g_cap_switch;
}

void pcap_switch_set(int value)
{
    g_cap_switch = value;
}

int pcap_task_get(void)
{
    return g_pcap_task;
}

void pcap_task_set(int value)
{
    g_pcap_task = value;
}

int pcap_time_get(void)
{
    return g_no_pcap_task_time;
}

void pcap_time_set(long long value)
{
    g_no_pcap_task_time = value;
}

static void 
pcap_rx_limit(void)
{
    long long wall_start = hinic3_time_usec(CLOCK_MONOTONIC);
    long long cpu_start = hinic3_time_usec(CLOCK_THREAD_CPUTIME_ID);

    /* 1. 运行阶段：精确控制运行时间*/
    while (hinic3_time_usec(CLOCK_MONOTONIC) - wall_start <= work_us) {
        pcap_hook_rx_pre();
    }

    long long wall_after_burn = hinic3_time_usec(CLOCK_MONOTONIC);
    long long cpu_after_burn = hinic3_time_usec(CLOCK_THREAD_CPUTIME_ID);
    
    /* 2. 睡眠阶段：释放CPU */
    long long to_sleep = period_us - (wall_after_burn - wall_start);
    if (to_sleep > 0) {
        usleep((useconds_t)to_sleep);
    }
    
    /* 3. 测量与补PI控制器 */
    long long actual_work_cpu = cpu_after_burn - cpu_start;
    long long error = target_work_us - actual_work_cpu;  // 正=运行时间少了，负=运行时间多了
    
    integral_err += error;
    /* 抗积分饱和 */
    if (integral_err > period_us)
        integral_err = period_us;
    if (integral_err < -period_us)
        integral_err = -period_us;
    
    /* PI公式：下次工作时长 = 目标 + 比例项 + 积分项 */
    work_us = target_work_us + error + integral_err * PCAP_PID_KI;
    if (work_us < 0)
        work_us = 0;
    if (work_us > period_us)
        work_us = period_us;
}

void *
pcap_thread_main(void *arg HINIC3_UNUSED)
{
    enum check_thread_item_type check_thread = CAPTURE_THREAD;
    long long start = hinic3_time_msec();
    period_us = PCAP_PERIOD_US;
    target_work_us = PCAP_PERIOD_US * PCAP_CPU_TARGET_RATE;
    work_us = target_work_us;
    integral_err = 0;

    for (;;)
    {
        if (pcap_switch_get() == 1 && pcap_task_get() == 0 && pcap_timeout_check_s(pcap_time_get(), PCAP_DISABLE_TIME_S))
        {
            HINIC3_LOG(INFO, CAPTURE, "Disable capture probe because timeout.");
            pcap_switch_set(0);
        }
        start = hinic3_thread_signal_increase(start, check_thread, HINIC3_CAPTURE_THREAD_SIGNAL_INCREASE_INTER);

        if (g_cap_task_save.thread_exit == PCAP_THREAD_EXIT_STATUS)
            break;

        if (pcap_switch_get() == 0)
        {
            usleep(PCAP_SLEEP_TIME);
            continue;
        }

        if (pcap_cpu_usage_get() == PCAP_CPU_LOW) {
            pcap_rx_limit();
        } else {
            pcap_hook_rx_pre();
        }

    }

    return 0;
}

void hinic3_stop_pcap_threads(void)
{
    if (g_cap_task_save.thread != 0)
    {
        g_cap_task_save.thread_exit = PCAP_THREAD_EXIT_STATUS;
        pthread_join(g_cap_task_save.thread, NULL);
    }
}

struct pcap_task_save_t *
hinic3_get_cap_task_save(void)
{
    return &g_cap_task_save;
}

void hinic3_set_ticks_per_ms(uint64_t value)
{
    g_ticks_per_ms = value;
}

struct rte_mempool **
hinic3_get_pcap_shared_mp(void)
{
    return &g_pcap_shared_mp;
}
