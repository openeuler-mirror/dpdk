/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2024 Huawei Technologies Co., Ltd
 */

#include "hinic3_agent_flow_cmd.h"
#include "hinic3_ui_string.h"
#include "hinic3_flow_agent.h"
#include "hinic3_util.h"

#define HINIC3_FLOW_SPEED_ARG_GAP 2

struct hinic3_speed_measure_task_t g_offload_speed_task = {
    .alive = false,
    .mutex = HINIC3_MUTEX_INITIALIZER,
};

static int
hinic3_speed_parse_int_arg(const char *str_value, uint32_t min, uint32_t max, uint32_t *dst_value)
{
    uint32_t tmp_value;
    if (is_valid_digit(str_value) == false)
        return -1;

    tmp_value = strtoul(str_value, NULL, STR_TO_DEC_NUM);
    if (tmp_value < min || tmp_value > max)
        return -1;

    *dst_value = tmp_value;
    return 0;
}

static int
hinic3_speed_parse_cmd_argv(int argc, const char *argv[], struct hinic3_speed_cmd_param_t *param,
    struct ds *ds)
{
    int i;
    int ret;
    int work_argc = argc;
    const char **work_argv = argv;

    i = 0;
    work_argc -= 1;
    work_argv += 1;
    while (i < work_argc) {
        if (i + 1 >= work_argc) {
            hinic3_ds_put_format(ds, HINIC3_UI_LEADING_SIGN_ERROR HINIC3_UI_ERROR_TOO_MANY_PARAMETER ".\n");
            return -1;
        }

        if (strcmp("-i", work_argv[i]) == 0) {
            ret = hinic3_speed_parse_int_arg(work_argv[i + 1], HINIC3_SPPED_MIN_INTERVAL, HINIC3_SPPED_MAX_INTERVAL,
                &param->interval);
            if (ret != 0) {
                hinic3_ds_put_format(ds,
                    HINIC3_UI_LEADING_SIGN_ERROR  HINIC3_UI_ERROR_WRONG_PARAMETER
                    ", invalid interval value, should be an integer range <%u-%u>.\n",
                    HINIC3_SPPED_MIN_INTERVAL, HINIC3_SPPED_MAX_INTERVAL);
                return -1;
            }

            i += HINIC3_FLOW_SPEED_ARG_GAP;
            continue;
        }

        if (strcmp("-t", work_argv[i]) == 0) {
            ret = hinic3_speed_parse_int_arg(work_argv[i + 1], HINIC3_SPPED_MIN_SAMPLE, HINIC3_SPPED_MAX_SAMPLE,
                &param->total_samples);
            if (ret != 0) {
                hinic3_ds_put_format(ds,
                    HINIC3_UI_LEADING_SIGN_ERROR HINIC3_UI_ERROR_WRONG_PARAMETER 
                    ", invalid samples value, should be an integer range <%u-%u>.\n",
                    HINIC3_SPPED_MIN_SAMPLE, HINIC3_SPPED_MAX_SAMPLE);
                return -1;
            }

            i += HINIC3_FLOW_SPEED_ARG_GAP;
            continue;
        }

        hinic3_ds_put_format(ds, HINIC3_UI_LEADING_SIGN_ERROR
            HINIC3_UI_ERROR_UNRECOGNIZED_COMMAND ", please type -h or --help for help.\n");
        return -1;
    }

    return 0;
}

static void
hinic3_speed_task_wait(struct hinic3_speed_measure_task_t *task)
{
    hinic3_pthread_mutex_lock(&task->mutex);
    hinic3_mutex_cond_wait(&task->cond, &task->mutex);
    hinic3_pthread_mutex_unlock(&task->mutex);
}

static int
hinic3_speed_task_wakeup(struct hinic3_speed_measure_task_t *task)
{
    int ret;
    hinic3_pthread_mutex_lock(&task->mutex);
    ret = hinic3_thread_cond_signal(&task->cond);
    hinic3_pthread_mutex_unlock(&task->mutex);
    return ret;
}

static void
hinic3_speed_fetch_samples(struct hinic3_speed_measure_task_t *task)
{
    uint32_t tmp_value;

    task->status = HINIC3_SPEED_TASK_ST_SAMPLING;
    while (true) {
        tmp_value = hinic3_get_offload_flow_nums();
        task->sample_list[task->count] = tmp_value;
        task->count++;
        sleep(task->interval);

        if (task->count >= task->total_samples)
            break;
    }
    task->status = HINIC3_SPEED_TASK_ST_IDLE;

    return;
}

static void
hinic3_speed_show_speeds(struct hinic3_speed_measure_task_t *task, struct ds *ds)
{
    uint32_t i;
    uint32_t pre_value;
    uint32_t value;
    uint32_t diff;
    uint32_t speed;

    hinic3_ds_put_format(ds, "%2ssamples: count=%u, [", HINIC3_UI_INDENT_SPACE, task->count);
    for (i = 0; i < task->count; i++) {
        if (i == task->count - 1) {
            hinic3_ds_put_format(ds, "%1s%u ]\n", HINIC3_UI_INDENT_SPACE, task->sample_list[i]);
        } else {
            hinic3_ds_put_format(ds, "%1s%u,", HINIC3_UI_INDENT_SPACE, task->sample_list[i]);
        }
    }

    if (task->count <= 1) {
        hinic3_ds_put_format(ds, HINIC3_UI_LEADING_SIGN_FAILURE "Too few samples,can't calculate speed.\n");
        return;
    }

    pre_value = task->sample_list[0];
    for (i = 1; i < task->count; i++) {
        value = task->sample_list[i];
        if (value < pre_value) {
            hinic3_ds_put_format(ds, "%2s%u\n", HINIC3_UI_INDENT_SPACE, 0);
            pre_value = value;
            continue;
        }

        diff = value - pre_value;
        speed = task->interval == 0 ? 0 : diff / task->interval;
        hinic3_ds_put_format(ds, "%2s%u\n", HINIC3_UI_INDENT_SPACE, speed);
        pre_value = value;
    }

    return;
}

static void*
hinic3_speed_measure_thread(void *args)
{
    struct hinic3_speed_measure_task_t *task = (struct hinic3_speed_measure_task_t *)args;

    const char *name = "hinic3_speed_measure";
    int ret = hinic3_set_thread_name_by_str(name);
    if (ret != 0)
        HINIC3_LOG(WARNING, AGENT, "set speed measure thread name failed!");

    while (true) {
        hinic3_speed_fetch_samples(task);
        if (task->command == HINIC3_SPEED_TASK_CMD_STOP) {
            break;
        } else if (task->command == HINIC3_SPEED_TASK_CMD_RESTART) {
            task->count = 0;
            task->command = 0;
            continue;
        }

        while (true) {
            hinic3_speed_task_wait(task);
            if (task->command == 0) {
                continue;
            } else {
                break;
            }
        }

        if (task->command == HINIC3_SPEED_TASK_CMD_STOP) {
            break;
        } else if (task->command == HINIC3_SPEED_TASK_CMD_RESTART) {
            task->count = 0;
            task->command = 0;
            continue;
        }
    }

    return NULL;
}

static int
hinic3_speed_measure_thread_create(struct hinic3_speed_measure_task_t *task,
    struct hinic3_speed_cmd_param_t *param, struct ds *ds)
{
    int ret = 0;

    task->interval = param->interval;
    task->total_samples = param->total_samples;
    task->count = 0;
    task->command = 0;
    task->status = HINIC3_SPEED_TASK_ST_IDLE;
    ret = hinic3_thread_cond_init(&task->cond, NULL);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "Failed to create thread cond.");
        hinic3_ds_put_format(ds, HINIC3_UI_LEADING_SIGN_FAILURE "Internal failure.\n");
        return -1;
    }
    ret = pthread_create(&task->thread, NULL, hinic3_speed_measure_thread, task);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "Create thread fail.");
        hinic3_ds_put_format(ds, HINIC3_UI_LEADING_SIGN_FAILURE "Internal failure.\n");
        hinic3_thread_cond_destroy(&task->cond);
        return -1;
    }
    task->alive = true;
    hinic3_set_ctrl_thread_cpu_affinity(&task->thread);
    return 0;
}

static int
hinic3_speed_task_cmd_start(struct unixctl_conn *conn, int argc, const char *argv[])
{
    int ret;
    struct hinic3_speed_cmd_param_t param;
    struct ds ds = DS_EMPTY_INITIALIZER;
    struct hinic3_speed_measure_task_t *task = &g_offload_speed_task;

    if (task->alive) {
        hinic3_ds_put_format(&ds,
            HINIC3_UI_LEADING_SIGN_FAILURE "Offload speed task is still alive, please use restart.\n");
        goto fail;
    }

    param.interval = 0;
    param.total_samples = 0;
    ret = hinic3_speed_parse_cmd_argv(argc, argv, &param, &ds);
    if (ret != 0)
        goto fail;
    if (param.interval == 0 || param.total_samples < 1) {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR HINIC3_UI_ERROR_INCOMPLETE_COMMAND ".\n");
        goto fail;
    }

    ret = hinic3_speed_measure_thread_create(task, &param, &ds);
    if (ret != 0)
        goto fail;

    HINIC3_LOG(INFO, AGENT, "Offload speed measure start success, interval is %u(s), samples is %u.", task->interval,
        task->total_samples);
    hinic3_ds_put_format(&ds,
        HINIC3_UI_LEADING_SIGN_INFO "Offload speed measure start success, interval is %u(s), samples is %u.\n",
        task->interval, task->total_samples);
    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
    return 0;

fail:
    hinic3_command_reply_error(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
    return -1;
}

static int
hinic3_speed_task_cmd_restart(struct unixctl_conn *conn, int argc, const char *argv[])
{
    int ret;
    struct hinic3_speed_cmd_param_t param;
    struct ds ds = DS_EMPTY_INITIALIZER;
    struct hinic3_speed_measure_task_t *task = &g_offload_speed_task;

    if (!task->alive) {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_FAILURE "Offload speed task is not alive, please use start.\n");
        goto fail;
    }

    if (task->status != HINIC3_SPEED_TASK_ST_IDLE) {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_FAILURE "Offload speed task is sampling, do it later.\n");
        goto fail;
    }

    param.interval = 0;
    param.total_samples = 0;
    ret = hinic3_speed_parse_cmd_argv(argc, argv, &param, &ds);
    if (ret != 0)
        goto fail;

    if (param.interval != 0)
        task->interval = param.interval;
    if (param.total_samples != 0)
        task->total_samples = param.total_samples;
    task->command = HINIC3_SPEED_TASK_CMD_RESTART;
    ret = hinic3_speed_task_wakeup(task);
    if (ret != 0) {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_FAILURE "Failed to wakeup speed task.\n");
        goto fail;
    }
    HINIC3_LOG(INFO, AGENT, "offload speed measure restart success, interval is %u(s), samples is %u!", task->interval,
        task->total_samples);
    hinic3_ds_put_format(&ds,
        HINIC3_UI_LEADING_SIGN_INFO "Offload speed measure restart success, interval is %u(s), samples is %u.\n",
        task->interval, task->total_samples);
    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
    return 0;

fail:
    hinic3_command_reply_error(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
    return -1;
}

static int
hinic3_speed_task_cmd_stop(struct unixctl_conn *conn, int argc, const char *argv[] HINIC3_UNUSED)
{
    int ret;
    struct ds ds = DS_EMPTY_INITIALIZER;
    struct hinic3_speed_measure_task_t *task = &g_offload_speed_task;

    if (argc > 1) {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR HINIC3_UI_ERROR_TOO_MANY_PARAMETER ".\n");
        goto fail;
    }

    if (!task->alive) {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_FAILURE "Offload speed task is not alive.\n");
        goto fail;
    }

    if (task->status == HINIC3_SPEED_TASK_ST_SAMPLING) {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_FAILURE "Offload speed task is sampling, do it later.\n");
        goto fail;
    }

    task->alive = false;
    task->command = HINIC3_SPEED_TASK_CMD_STOP;
    ret = hinic3_speed_task_wakeup(task);
    if (ret != 0) {
        hinic3_ds_put_format(&ds, "Failed to wakeup speed task.\n");
        goto fail;
    }
    pthread_join(task->thread, NULL);
    ret = hinic3_thread_cond_destroy(&task->cond);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "failed to destroy thread cond.");
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_FAILURE "Internal failure.\n");
        goto fail;
    }
    HINIC3_LOG(INFO, AGENT, "offload speed measure stop success.");
    hinic3_command_reply(conn, HINIC3_UI_LEADING_SIGN_INFO "Offload speed measure stop success.\n");
    hinic3_ds_destroy(&ds);
    return 0;

fail:
    hinic3_command_reply_error(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
    return -1;
}

static int
hinic3_speed_task_cmd_show(struct unixctl_conn *conn, int argc, const char *argv[] HINIC3_UNUSED)
{
    struct ds ds = DS_EMPTY_INITIALIZER;
    struct hinic3_speed_measure_task_t *task = &g_offload_speed_task;

    if (argc > 1) {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR HINIC3_UI_ERROR_TOO_MANY_PARAMETER ".\n");
        goto fail;
    }

    if (!task->alive) {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_FAILURE "Offload speed task is not alive.\n");
        goto fail;
    }

    if (task->status == HINIC3_SPEED_TASK_ST_SAMPLING) {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_FAILURE "Offload speed task is sampling, do it later.\n");
        goto fail;
    }

    hinic3_speed_show_speeds(task, &ds);
    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
    return 0;

fail:
    hinic3_command_reply_error(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
    return -1;
}

static int
hinic3_speed_task_cmd_help(struct unixctl_conn *conn, int argc HINIC3_UNUSED, const char *argv[] HINIC3_UNUSED)
{
    struct ds ds = DS_EMPTY_INITIALIZER;
    const char *help_desc = "    start              Start the offload speed measuring task\n"
                      "      -i <interval>    Interval, in second. It is an integer with a range <1-10>\n"
                      "      -t <sample-num>  Sample number, an integer with a range <1-300>\n"
                      "    restart            Restart the offload speed measuring task\n"
                      "      -i <interval>    Interval, in second. It is an integer with a range <1-10>\n"
                      "      -t <sample-num>  Sample number, an integer with a range <1-300>\n"
                      "    show               Show the offload speed\n"
                      "    stop               Stop the offload speed measuring task\n"
                      "    -h, --help         Display the help information\n";

    hinic3_ds_put_format(&ds,
        "%2sUsage: dpak-ovs-ctl hwoff/flow-offload-speed-stat { "
        "start -i INTEGER<1-10> -t INTEGER<1-300> | \n%54srestart [ -i "
        "INTEGER<1-10> | -t INTEGER<1-300> ] * | \n%54sstop | show | { -h | --help } }\n",
        HINIC3_UI_INDENT_SPACE, HINIC3_UI_INDENT_SPACE, HINIC3_UI_INDENT_SPACE);
    hinic3_ds_put_format(&ds, "\n");
    hinic3_ds_put_format(&ds, "%2s%s\n", HINIC3_UI_INDENT_SPACE, HINIC3_UI_FLOW_OPTION_LIST_STRING);
    hinic3_ds_put_format(&ds, help_desc);

    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
    return 0;
}

void
hinic3_speed_task_cmd(struct unixctl_conn *conn, int argc, const char *argv[], void *aux)
{
    int work_argc;
    const char **work_argv = argv + 1;
    const char *sub_cmd_name = NULL;

    work_argc = argc - 1;
    sub_cmd_name = work_argv[0];

    if (strcmp("start", sub_cmd_name) == 0) {
        *(int *)aux = hinic3_speed_task_cmd_start(conn, work_argc, work_argv);
        return;
    }

    if (strcmp("restart", sub_cmd_name) == 0) {
        *(int *)aux = hinic3_speed_task_cmd_restart(conn, work_argc, work_argv);
        return;
    }

    if (strcmp("show", sub_cmd_name) == 0) {
        *(int *)aux = hinic3_speed_task_cmd_show(conn, work_argc, work_argv);
        return;
    }

    if (strcmp("stop", sub_cmd_name) == 0) {
        *(int *)aux = hinic3_speed_task_cmd_stop(conn, work_argc, work_argv);
        return;
    }

    if ((strcmp("-h", sub_cmd_name) == 0) || (strcmp("--help", sub_cmd_name) == 0)) {
        if (work_argc != 1) {
            hinic3_command_reply_error(conn,
                "Error: Too many parameters, please input -h or --help to get help info!\n");
            *(int *)aux = -1;
            return;
        }
        *(int *)aux = hinic3_speed_task_cmd_help(conn, work_argc, work_argv);
        return;
    }

    hinic3_command_reply_error(conn, HINIC3_UI_LEADING_SIGN_ERROR
        HINIC3_UI_ERROR_UNRECOGNIZED_COMMAND ", please type -h or --help for help.\n");
    return;
}
