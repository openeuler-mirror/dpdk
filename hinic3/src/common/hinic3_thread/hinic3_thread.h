/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_THREAD_H
#define HINIC3_THREAD_H

#include <pthread.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include "hinic3_log.h"
#include "hinic3_meminfo.h"

enum hinic3_thread_status {
    HINIC3_THREAD_NORMAL_STATUS,
    HINIC3_THREAD_EXIT_STATUS,
};

int hinic3_thread_create(pthread_t *thread, const char *name, void *(*start)(void *), void *arg,
    enum hinic3_module module_id);
int hinic3_thread_cond_init(pthread_cond_t *cond, pthread_condattr_t *attr);
int hinic3_thread_cond_destroy(pthread_cond_t *cond);
int hinic3_thread_cond_signal(pthread_cond_t *cond);

#endif
