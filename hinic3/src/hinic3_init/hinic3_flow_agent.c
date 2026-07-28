/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */

#include "hinic3_flow_agent.h"
#include <unistd.h>
#include "rte_lcore.h"
#include "rte_bus_vdev.h"
#include "hinic3_util.h"
#include "hiovs_api.h"
#include "hinic3_log.h"
#include "hinic3_init.h"
#include "hinic3_message.h"
#include "hinic3_driver_public.h"
#include "hinic3_iface_flow.h"
#include "hinic3_thread.h"
#include "hinic3_check_thread_health_state.h"
#include <rte_atomic.h>
#include "hinic3_iface_global.h"
#include "hinic3_meminfo.h"
#include "hinic3_flow_agent_sync_stats.h"
#include "hinic3_age_delete_flow.h"
#include "hinic3_offload_flow.h"
#include "hinic3_ufid_map_rte_flow.h"
#include "hinic3_capture_main.h"
#include "hinic3_flow_session.h"
#include "hinic3_agent_cmd_time.h"
#include "hinic3_map.h"
#include "hinic3_flow_agent_sync_stats.h"
#include "hinic3_parse_agent_config.h"
#include "hinic3_offload_flow.h"
#include "hinic3_util.h"
#include "hinic3_error_stats.h"
#include "hinic3_iface_flow_api_record.h"
#include "hinic3_flow_dump_item_key.h"

#define HINIC3_MAX_CORE_LENGTH 1024
#define NO_ENV_CORE_ERR (-2)
#define MAX_PMD_CORE 32

enum hinic3_thread_type {
    HINIC3_OFFLOAD_THREAD_TYPE,
    HINIC3_CONTROL_THREAD_TYPE,
};

struct hinic3_thread_type_in_env {
    enum hinic3_thread_type type;
    char thread_name[30];
};

static struct hinic3_thread_type_in_env g_hinic3_thread_type[] = {
    {HINIC3_OFFLOAD_THREAD_TYPE, "HINIC3_NIC_CORE_ISOLATION"},
    {HINIC3_CONTROL_THREAD_TYPE, "HINIC3_NIC_CTL_CORE_ISOLATION"},
};

static struct hinic3_dp_extend_info *g_offload_extend_info = NULL;
struct hinic3_flow_time g_flow_offload_time = { 0 };
static struct hinic3_rx_thread_ops g_rx_thread_ops;

static void hinic3_agent_init_thread_ops(void)
{
    g_rx_thread_ops.rx_put_ack = hinic3_offload_thread_rx_put_ack;
    g_rx_thread_ops.rx_age_notice = hinic3_thread_rx_hw_age_info;
    g_rx_thread_ops.sync_stats = hinic3_sync_flow_statistics;
    g_rx_thread_ops.del_sw_flow = NULL;
}

static int hinic3_get_thread_core_from_env(enum hinic3_thread_type type, uint32_t thread_core[],
                                          uint32_t max_core, uint32_t *active_num)
{
    char *core_value = NULL;
    char *saveptr = NULL;
    char *endPtr = NULL;
    char delim[] = ",";
    uint32_t index = 0;
    long nprocessors;
    char core_values[HINIC3_MAX_CORE_LENGTH] = { 0 };
     
    char *env_value = NULL;
    env_value = getenv(g_hinic3_thread_type[type].thread_name);

    if (env_value == NULL) {
        HINIC3_LOG(INFO, AGENT, "No offload cpu info in environment variables.");
        return NO_ENV_CORE_ERR;
    }
    // 检查环境变量长度
    size_t env_len = strlen(env_value);
    if (env_len >= sizeof(core_values)) {
        HINIC3_LOG(ERR, AGENT, "Environment variable value too long: %zu bytes (max: %zu)", env_len, sizeof(core_values) - 1);
        return -1;
    }

    nprocessors = sysconf(_SC_NPROCESSORS_ONLN);
    if (nprocessors == -1) {
        HINIC3_LOG(ERR, AGENT, "Failed to get the number of processors.");
        return -1;
    }

    strncpy(core_values, env_value, HINIC3_MAX_CORE_LENGTH - 1);

    core_value = strtok_r(core_values, delim, &saveptr);
    while (core_value != NULL) {
        thread_core[index] = strtoul(core_value, &endPtr, STR_TO_DEC_NUM);
        if (endPtr == NULL || *endPtr != '\0') {
            HINIC3_LOG(ERR, AGENT, "Failed to parse CPU index.");
            return -1;
        }

        if (thread_core[index] >= nprocessors) {
            HINIC3_LOG(ERR, AGENT, "Illegal CPU index %u.", thread_core[index]);
            return -1;
        }

        index++;
        if (index >= max_core) {
            HINIC3_LOG(WARNING, AGENT, "The num of offload cores for environment variables is out of Range %u.", max_core);
            break;
        }
        core_value = strtok_r(NULL, delim, &saveptr);
    }
    *active_num = index;
    return 0;
}

static void *hinic3_offload_thread_main(void *args)
{
    struct hinic3_offload_thread_data *thread = args;
    struct hinic3_rx_thread_ops *ops = thread->dp_info->offload_thread_ops;

    enum check_thread_item_type check_thread = thread->thread_id + CONST_THREAD_NUM;
    long long start = hinic3_time_msec();
    while (thread->exit == HINIC3_THREAD_NORMAL_STATUS) {
        start = hinic3_thread_signal_increase(start, check_thread, HINIC3LOAD_THREAD_SIGNAL_INCREASE_INTER);
        if (ops->rx_put_ack) {
            ops->rx_put_ack(thread->thread_id, thread->dp_info);
        }
        if (ops->del_sw_flow) {
            ops->del_sw_flow(thread->thread_id, thread->dp_info);
        }
        if (ops->sync_stats) {
            ops->sync_stats(thread->thread_id, thread->dp_info);
        }

        if (ops->rx_age_notice) {
            ops->rx_age_notice(thread->thread_id, thread->dp_info);
        }
    }
    return NULL;
}

static void hinic3_stop_remain_offload_threads(struct hinic3_offload_thread_data *threads, uint32_t index)
{
    uint32_t i;

    for (i = 0; i < index; i++) {
        threads[i].exit = HINIC3_THREAD_EXIT_STATUS;
        pthread_join(threads[i].thread, NULL);
    }
}

static int hinic3_set_offload_thread_affinity(struct hinic3_dp_extend_info *dp_info,
    struct hinic3_offload_thread_data *threads, uint32_t index)
{
    int ret;
    struct hinic3_cpu_mask forward_cpu_mask = hinic3_forward_cpu_mask_get();

    if (dp_info->offload_num_in_env > 0) {
        ret = hinic3_set_single_cpu_affinity(&threads[index].thread, dp_info->offload_thread_core[index]);
        if (ret != 0) {
            HINIC3_LOG(ERR, AGENT, "Failed to bind offload cores based on environment variables.");
            return -1;
        }
    } else {
        ret = hinic3_set_single_cpu_affinity(&threads[index].thread, forward_cpu_mask.cpu_mask[index]);
        if (ret != 0) {
            HINIC3_LOG(ERR, AGENT, "Failed to bind offload cores based on config.");
            return -1;
        }
    }
    return 0;
}

static void hinic3_set_thread_sync_index(uint32_t thread_num, uint32_t thread_index,
    struct hinic3_offload_thread_data *thread)
{
    if (thread_num == 0) {
        return;
    }
    uint32_t scope = OFFLOAD_FLOW_BUCKETS / thread_num;
    thread->cur_buk_idx = scope * thread_index;
    thread->min_buk_idx = thread->cur_buk_idx;
    thread->max_buk_idx = thread->min_buk_idx + scope;
    if (thread->max_buk_idx > OFFLOAD_FLOW_BUCKETS) {
        thread->max_buk_idx = OFFLOAD_FLOW_BUCKETS;
    }
}

int hinic3_create_offload_threads(struct hinic3_dp_extend_info *dp_info)
{
    int ret;
    struct hinic3_flow_agent_db *hw_offload = dp_info->hw_offload;
    uint32_t thread_num = hw_offload->forward_engine.cap.rx_thread_num;

    struct hinic3_offload_thread_data *threads = (struct hinic3_offload_thread_data *)hinic3_calloc(thread_num,
        sizeof(struct hinic3_offload_thread_data), HINIC3_INIT);
    if (threads == NULL) {
        HINIC3_LOG(ERR, AGENT, "Flow agent create offload threads allocate failed.");
        return -1;
    }

    for (uint32_t index = 0; index < thread_num; index++) {
        snprintf(threads[index].name, sizeof(threads[index].name), "hinic3_offload%u", index);
        threads[index].exit = HINIC3_THREAD_NORMAL_STATUS;
        threads[index].thread_id = index;
        threads[index].dp_info = dp_info;
        hinic3_set_thread_sync_index(thread_num, index, &threads[index]);

        ret = hinic3_thread_create(&threads[index].thread, threads[index].name, hinic3_offload_thread_main,
        &threads[index], HINIC3_INIT);
        if (ret != 0) {
            hinic3_stop_remain_offload_threads(threads, index);
            HINIC3_LOG(ERR, AGENT, "Flow offload thread create fail. index is %u, errno is %d", index, ret);
            goto err;
        }

        ret = hinic3_set_offload_thread_affinity(dp_info, threads, index);
        if (ret != 0) {
            hinic3_stop_remain_offload_threads(threads, index + 1);
            goto err;
        }
    }

    dp_info->offload_threads = threads;
    return 0;
err:
    hinic3_free(threads);
    return -1;
}

static void hinic3_stop_offload_threads(struct hinic3_dp_extend_info *dp_info)
{
    if (dp_info == NULL) {
        return;
    }

    struct hinic3_flow_agent_db *hw_offload = dp_info->hw_offload;
    uint32_t thread_num = hw_offload->forward_engine.cap.rx_thread_num;
    struct hinic3_offload_thread_data *threads = dp_info->offload_threads;
    uint32_t index;

    if (threads == NULL) {
        return;
    }

    for (index = 0; index < thread_num; index++) {
        threads[index].exit = HINIC3_THREAD_EXIT_STATUS;
    }

    for (index = 0; index < thread_num; index++) {
        pthread_join(threads[index].thread, NULL);
    }

    hinic3_free(threads);
    dp_info->offload_threads = NULL;
}


static bool hinic3_flow_agent_init_forward_engine(struct hinic3_flow_agent_db *hw_offload)
{
    if (hw_offload == NULL) {
        return false;
    }

    struct hinic3_forward_engine *engine = &hw_offload->forward_engine;
    struct hinic3_flow_capability *cap = &engine->cap;
    rte_atomic32_init(&engine->current_flow_size);
    int ret = hinic3_flow_get_capability(cap);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "Flow agent failed to get cap!");
        return false;
    }
    if (cap->rx_thread_num > HINIC3_RX_THREAD_MAX) {
        HINIC3_LOG(ERR, AGENT, "rx thread num: %u is greater than max num: %d!", cap->rx_thread_num,
            HINIC3_RX_THREAD_MAX);
        return false;
    }

    return true;
}

static int hinic3_flow_agent_set_forward_mode(void)
{
    uint8_t mode;
    uint8_t hinic3_version = HINIC3_MODE_VERSION_01;
    uint8_t escape_mode = HINIC3_ESCAPE_MODE_ALL_OFFLOAD;
    uint8_t forward_mode = hinic3_forward_mode_get();

    mode = (hinic3_version << HINIC3_MODE_VERSION_OFFSET) | (forward_mode << HINIC3_FORWARD_MODE_OFFSET) |
        (escape_mode << HINIC3_ESCAPE_MODE_OFFSET);

    /* Set flow forward mode to hardware */
    int ret = hinic3_flow_set_forward_mode(mode);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "Failed to set forward mode to 0x%x", mode);
        return ret;
    }
    return 0;
}

static void hinic3_process_flow_put_callback_info(const struct hinic3_nlattr *args,
    struct hinic3_flow_callback_info *callback_info)
{
    hinic3_nlattr_itr nla = NULL;

    HINIC3_NLATTR_FOR_EACH(nla, args) {
        int type = hinic3_nlattr_get_itr_type(nla);
        switch (type) {
            case HINIC3_FLOW_ARG_PUT_RESULT:
                callback_info->flow_put_result = hinic3_nlattr_get_itr_u8(nla);
                break;
            case HINIC3_FLOW_ARG_NO_CT_UFID:
                memcpy(&callback_info->no_ct_key, hinic3_nlattr_get_itr_data(nla),
                    sizeof(callback_info->no_ct_key));
                break;
            case HINIC3_FLOW_ARG_TIME:
                callback_info->start_t = hinic3_nlattr_get_itr_u64(nla);
                break;
            case HINIC3_FLOW_ARG_DP_HASH:
                callback_info->is_dp_hash = true;
                break;
            case HINIC3_FLOW_ARG_FLOW_HASH:
                callback_info->flow_hash = hinic3_nlattr_get_itr_u32(nla);
                break;
            case HINIC3_FLOW_ARG_MODIFY:
                callback_info->is_modify = true;
                break;
            case HINIC3_FLOW_ARG_CT_UFID:
                memcpy(&callback_info->ct_key, hinic3_nlattr_get_itr_data(nla),
                    sizeof(callback_info->ct_key));
                callback_info->is_ct = true;
                break;
            case HINIC3_FLOW_ARG_CT_DIRECT:
                callback_info->reply = *(const bool*)hinic3_nlattr_get_itr_data(nla);
                break;
            case HINIC3_FLOW_ARG_SW_UFID:
                callback_info->mega_ufid =
                    *(const hinic3_u128 *)hinic3_nlattr_get_itr_unspec(nla, sizeof(hinic3_u128));
                break;
            default:
                break;
        }
    }
    return;
}

static int hinic3_flow_agent_put_flow_callback(uint32_t table_id, uint64_t ufid, struct hinic3_flow_callback_info *callback_info)
{
    int ret;

    if (callback_info->flow_put_result != HINIC3_PUT_FLOW_SUCCESS) {
        hinic3_add_error_stats(HINIC3_FLOWS_ERROR_CALLBACK_FLOW_PUT_INFO, 1);
        return -1;
    }

    ret = hinic3_try_rlock_flush_all();
    if (ret == EBUSY) {
        return 0;
    }
    if (ret != 0) {
        return -1;
    }

    ret = hinic3_set_ufid_in_rte_flow(table_id, callback_info, ufid);
    if (ret != 0) {
        hinic3_add_error_stats(HINIC3_FLOWS_ERROR_CALLBACK_SET_UFID_IN_RTE_FLOW, 1);
        (void)hinic3_runlock_flush_all();
        return -1;
    }

    (void)hinic3_runlock_flush_all();
    if (HINIC3_UNLIKELY(hinic3_is_offload_measure_alive() == true)) {
        clock_t finish_t = clock();
        g_flow_offload_time.time = ((double)(finish_t - callback_info->start_t)) / CLOCKS_PER_SEC;
        g_flow_offload_time.update = 1;
    }

    hinic3_inc_offload_flow_nums();
    return 0;
}

static int hinic3_flow_agent_modify_flow_callback(struct hinic3_flow_callback_info *callback_info)
{
    if (callback_info->flow_put_result != HINIC3_PUT_FLOW_SUCCESS) {
        hinic3_add_error_stats(HINIC3_FLOWS_ERROR_CALLBACK_MODIFY_FLOW_PUT, 1);
        return -1;
    }

    return 0;
}

static int hinic3_flow_agent_put_callback(uint32_t table_id, uint64_t ufid, const struct hinic3_nlattr *args)
{
    struct hinic3_flow_callback_info callback_info = { 0 };
    int ret = 0;

    hinic3_process_flow_put_callback_info(args, &callback_info);

    if (callback_info.is_modify) {
        HINIC3_LOG_HINIC_FLOW_API_LOG_NO_LOG(ret, HINIC3_FLOW_AGENT_MODIFY_FLOW_CALLBACK,
            hinic3_flow_agent_modify_flow_callback(&callback_info));
        return ret;
    }

    HINIC3_LOG_HINIC_FLOW_API_LOG_NO_LOG(ret, HINIC3_FLOW_AGENT_PUT_FLOW_CALLBACK,
        hinic3_flow_agent_put_flow_callback(table_id, ufid, &callback_info));
    return ret;
}

static void hinic3_flow_agent_init_db(struct hinic3_flow_agent_db *hinic3_db)
{
    rte_spinlock_init(&hinic3_db->forward_engine.lock);
    rte_spinlock_init(&hinic3_db->operate_disable_lock);
    hinic3_db->operate_disable = 0;
    hinic3_db->max_block_num = UINT16_MAX;
    hinic3_db->escape_mode = HINIC3_ESCAPE_MODE_ALL_OFFLOAD;
    hinic3_db->vxlan_dst_port = DEFAULT_VXLAN_PORT;
    hinic3_db->queue_num.total_count = hinic3_support_virtio_queue_get();
}

static int hinic3_flow_agent_init_common(struct hinic3_flow_agent_db *hinic3_db)
{
    int ret = 0;

    (void)hinic3_flow_agent_init_db(hinic3_db);
    if (!hinic3_flow_agent_init_forward_engine(hinic3_db)) {
        hinic3_add_error_stats(HINIC3_FLOWS_ERROR_FORWARD_ENGINE_NOT_READY, 1);
        HINIC3_LOG(ERR, AGENT, "Flow agent init forward engine failed.");
        return -1;
    }

    ret = hinic3_flow_agent_set_forward_mode();
    if (ret != 0) {
        hinic3_add_error_stats(HINIC3_FLOWS_ERROR_FORWARD_ENGINE_NOT_READY, 1);
        HINIC3_LOG(ERR, AGENT, "Flow agent set forward_mode failed.");
        return ret;
    }

    return ret;
}

static struct hinic3_flow_agent_db *hinic3_flow_agent_init_database(void)
{
    int ret = 0;
    struct hinic3_flow_agent_db *hinic3_db = hinic3_calloc(1, sizeof(struct hinic3_flow_agent_db), HINIC3_INIT);
    if (hinic3_db == NULL) {
        HINIC3_LOG(WARNING, AGENT, "Flow agent init database allocate failed");
        return NULL;
    }

    ret = hinic3_flow_agent_init_common(hinic3_db);
    if (ret != 0) {
        HINIC3_LOG(WARNING, AGENT, "Flow agent init common failed");

        goto err;
    }

    return hinic3_db;
err:
    hinic3_free(hinic3_db);
    return NULL;
}

static int hinic3_flexda_flow_callback(uint32_t table_id __rte_unused, uint64_t ufid, const struct hinic3_dpif_flow_for_get *flow,
    const struct hinic3_nlattr_obj *args, size_t args_len)
{
    struct hinic3_nlattr nla_args;
    hinic3_nlattr_itr nla = NULL;
    uint8_t age = 0;
    uint8_t flush_ret = 0xff;
    int ret;

    hinic3_nlattr_init(&nla_args,(void *)(uintptr_t)args, args_len);
    hinic3_nlattr_reset_itr(&nla_args, args_len);
    HINIC3_NLATTR_FOR_EACH(nla, &nla_args)
    {
        int type = hinic3_nlattr_get_itr_type(nla);
        switch (type) {
            case HINIC3_FLOW_ARG_AGE:
                age = hinic3_nlattr_get_itr_u8(nla);
                break;
            case HINIC3_FLOW_ARG_FLUSH_RESULT:
                flush_ret = hinic3_nlattr_get_itr_u8(nla);
                break;
            default:
                break;
        }
    }

    if (flush_ret != 0xff) {
        ret = 0;
    } else if (age != 0) {
        ret = hinic3_flow_agent_age_callback(ufid, flow, &nla_args);
    } else {
        ret = hinic3_flow_agent_put_callback(table_id, ufid, &nla_args);
    }

    return ret;
}

static int hinic3_flow_callback(uint64_t ufid, const struct hinic3_dpif_flow_for_get *flow,
    const struct hinic3_nlattr_obj *args, size_t args_len)
{
    return hinic3_flexda_flow_callback(0, ufid, flow, args, args_len);
}

int hinic3_flow_callback_register(void)
{
    struct hinic3_drv_ops *ops = NULL;

    ops = hinic3_get_drv_ops();
    if (hinic3_card_mod_get() == PROG_MODE) {
        HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_flexda_flow_callback_register, HINIC3_DRV_FUNC_NO_PTR);
        ops->hovs_flexda_flow_callback_register((hovs_flexda_flow_callback_t)hinic3_flexda_flow_callback);
    } else {
        HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_flow_callback_register, HINIC3_DRV_FUNC_NO_PTR);
        ops->hovs_flow_callback_register((hovs_flow_callback_t)hinic3_flow_callback);
    }
    return 0;
}

int hinic3_get_thread_core_isolation(struct hinic3_dp_extend_info *dp_info)
{
    int ret;
    struct hinic3_cpu_mask forward_cpu_mask = hinic3_forward_cpu_mask_get();
    uint32_t offload_core_cnt = forward_cpu_mask.cpu_mask_invalid_num;
    uint32_t offload_thread_num = hinic3_offload_thread_num_get();

    /* 解析环境变量中的绑核信息，如果信息存在且解析失败则直接退出,如果绑核信息不存在，则使用配置文件进行绑核 */
    ret = hinic3_get_thread_core_from_env(HINIC3_OFFLOAD_THREAD_TYPE, dp_info->offload_thread_core,
                                         HINIC3_RX_THREAD_NUM_MAX, &dp_info->offload_num_in_env);
    if (ret != 0 && ret != NO_ENV_CORE_ERR) {
        HINIC3_LOG(ERR, AGENT, "Failed to get offload cores from environment variables.");
        return -1;
    } else if (ret == NO_ENV_CORE_ERR) {
        HINIC3_LOG(INFO, AGENT, "No offload cores in environment variables.");
        if (offload_core_cnt < offload_thread_num) {
            HINIC3_LOG(ERR, AGENT, "Offload cores num in config  %u is less than offload threads %u.",
                      offload_core_cnt, offload_thread_num);
            return -1;
        }
    } else {
        if (dp_info->offload_num_in_env < offload_thread_num) {
            HINIC3_LOG(ERR, AGENT, "Offload cores num in environment variables %u is less than offload threads num %u.",
                      dp_info->offload_num_in_env, offload_thread_num);
            return -1;
        }
    }

    ret = hinic3_get_thread_core_from_env(HINIC3_CONTROL_THREAD_TYPE, dp_info->control_thread_core,
                                         HINIC3_RX_THREAD_NUM_MAX, &dp_info->control_num_in_env);
    if (ret != 0 && ret != NO_ENV_CORE_ERR) {
        HINIC3_LOG(ERR, AGENT, "Failed to get control cores from environment variables.");
        return -1;
    }

    if (ret == NO_ENV_CORE_ERR) {
        HINIC3_LOG(INFO, AGENT, "No control cores in environment variables.");
    }
    return 0;
}

static int hinic3_flow_agent_init(struct hinic3_dp_extend_info *dp_info)
{
    if (dp_info->hw_offload) {
        HINIC3_LOG(WARNING, AGENT, "Flow agent database already initialized");
        return 0;
    }

    int ret;
    struct hinic3_flow_agent_db *hinic3_db = hinic3_flow_agent_init_database();
    if (hinic3_db == NULL) {
        HINIC3_LOG(WARNING, AGENT, "Flow agent init database failed");
        return -1;
    }

    hinic3_flow_callback_register();
    hinic3_agent_init_thread_ops();
    dp_info->offload_thread_ops = &g_rx_thread_ops;
    dp_info->hw_offload = hinic3_db;

    ret = hinic3_get_thread_core_isolation(dp_info);
    if (ret != 0) {
        return -1;
    }

    HINIC3_LOG(INFO, AGENT, "Flow agent init successfully");
    return 0;
}

int hinic3_offload_extend_init(void)
{
    g_offload_extend_info = (struct hinic3_dp_extend_info *)hinic3_calloc(1, sizeof(*g_offload_extend_info), HINIC3_INIT);
    if (g_offload_extend_info == NULL) {
        return -1;
    }
    return 0;
}

int hinic3_flow_agent_construct(void)
{
    int ret;

    ret  = hinic3_offload_extend_init();
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "hinic3 flow agent construct calloc memory failed.");
        return -1;
    }

    ret = hinic3_flow_agent_init(g_offload_extend_info);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "hinic3 flow agent init failed");
        goto err;
    }

    (void)hinic3_ufid_map_rte_flow_init();

    ret = hinic3_create_offload_threads(g_offload_extend_info);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "flow agent component construct failed");
        goto err;
    }
    return 0;
err:
    hinic3_free(g_offload_extend_info);
    g_offload_extend_info = NULL;
    return ret;
}

static void hinic3_rarp_info_destroy(struct hinic3_flow_agent_db *hinic3_db)
{
    int i;
    struct hinic3_rarp_mac_s *cur_rarp = NULL;
    struct hinic3_rarp_mac_s *next_rarp = NULL;
    struct hinic3_list *rarp_list = NULL;

    for (i = 0; i < HINIC3_MAX_LCORE_COUNT; i++) {
        rarp_list = &hinic3_db->pmd_rarp_list[i];
        LIST_FOR_EACH_SAFE(cur_rarp, next_rarp, node, rarp_list) {
            hinic3_list_remove(&cur_rarp->node);
            hinic3_free(cur_rarp);
        }
    }
}

void hinic3_flow_agent_destruct(void)
{
    struct hinic3_flow_agent_db *hinic3_db = NULL;
    if (g_offload_extend_info != NULL) {
        hinic3_stop_offload_threads(g_offload_extend_info);
        hinic3_db = g_offload_extend_info->hw_offload;
        if (hinic3_db == NULL) {
            hinic3_free(g_offload_extend_info);
            g_offload_extend_info = NULL;
            return;
        }
        hinic3_db->pmd_status_num = 0;
        hinic3_free(hinic3_db->pmd_status);
        hinic3_db->pmd_status = NULL;
        if (hinic3_hot_migration_get() == true) {
            hinic3_rarp_info_destroy(hinic3_db);
        }
        hinic3_free(hinic3_db);
        g_offload_extend_info->hw_offload = NULL;
        hinic3_free(g_offload_extend_info);
        g_offload_extend_info = NULL;
    }
}

struct hinic3_dp_extend_info *hinic3_get_offload_extend_info(void)
{
    return g_offload_extend_info;
}

void hinic3_offload_extend_deinit(void)
{
    if (g_offload_extend_info != NULL) {
        hinic3_free(g_offload_extend_info); 
        g_offload_extend_info = NULL;
    }
}

void hinic3_inc_offload_flow_nums(void)
{
    if (g_offload_extend_info == NULL || g_offload_extend_info->hw_offload == NULL) {
        return;
    }
    struct hinic3_flow_agent_db *hinic3_db = (struct hinic3_flow_agent_db *)(g_offload_extend_info->hw_offload);
    rte_atomic32_inc(&hinic3_db->forward_engine.current_flow_size);
    return;
}

void hinic3_dec_offload_flow_nums(void)
{
    if (g_offload_extend_info == NULL || g_offload_extend_info->hw_offload == NULL) {
        return;
    }
    struct hinic3_flow_agent_db *hinic3_db = (struct hinic3_flow_agent_db *)(g_offload_extend_info->hw_offload);
    rte_atomic32_dec(&hinic3_db->forward_engine.current_flow_size);
    return;
}

int32_t hinic3_get_offload_flow_nums(void)
{
    struct hinic3_flow_agent_db *hinic3_db = (struct hinic3_flow_agent_db *)(g_offload_extend_info->hw_offload);
    return rte_atomic32_read(&hinic3_db->forward_engine.current_flow_size);
}

void hinic3_reset_offload_flow_nums(void)
{
    struct hinic3_flow_agent_db *hinic3_db = (struct hinic3_flow_agent_db *)(g_offload_extend_info->hw_offload);
    rte_atomic32_set(&hinic3_db->forward_engine.current_flow_size, 0);
    return;
}

struct hinic3_flow_time *hinic3_get_flow_offload_time(void)
{
    return &g_flow_offload_time;
}
