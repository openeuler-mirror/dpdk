/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Huawei Technologies Co., Ltd
 */

#include <pthread.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include "hinic3_meminfo.h"
#include "hinic3_thread.h"
#define MIN_STACK_SIZE          (512 * 1024)
#define MAX_THREAD_NAME_LENGTH  16

struct hinic3_thread_aux {
    void *(*start)(void *);
    void *arg;
    char name[MAX_THREAD_NAME_LENGTH];
};

static void set_min_stack_size(pthread_attr_t *attr, size_t min_stacksize)
{
    size_t stacksize;
    int error;

    error = pthread_attr_getstacksize(attr, &stacksize);
    if (error != 0) {
        HINIC3_LOG(ERR, AGENT, "pthread_attr_getstacksize failed, err is %d!", error);
        return;
    }

    if (stacksize < min_stacksize) {
        error = pthread_attr_setstacksize(attr, min_stacksize);
        if (error != 0) {
            HINIC3_LOG(ERR, AGENT, "pthread_attr_setstacksize failed, err is %d!", error);
            return;
        }
    }
}

static void *hinic3_set_thread_name(void *aux_)
{
    struct hinic3_thread_aux aux;

    aux = *(struct hinic3_thread_aux *)aux_;
    hinic3_free(aux_);

    if (pthread_setname_np(pthread_self(), aux.name) != 0) {
        HINIC3_LOG(ERR, AGENT, "thread set name failed. errno is %d!", errno);
    }
    return aux.start(aux.arg);
}

int hinic3_thread_create(pthread_t *thread, const char *name, void *(*start)(void *), void *arg,
    enum hinic3_module module_id)
{
    int ret;
    pthread_attr_t attr;
    struct hinic3_thread_aux *aux = NULL;
    size_t len;

    aux = hinic3_malloc(sizeof(struct hinic3_thread_aux), module_id);
    if (aux == NULL) {
        return -1;
    }
    aux->start = start;
    aux->arg = arg;

    len = strnlen(name, sizeof(aux->name) - 1);
    memcpy(aux->name, name, len);
    (aux->name)[len] = '\0';
    /* on Linux pthread_attr_init/pthread_attr_destroy always succeed */
    (void)pthread_attr_init(&attr);
    set_min_stack_size(&attr, MIN_STACK_SIZE);
    ret = pthread_create(thread, &attr, hinic3_set_thread_name, aux);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "pthread_create failed. errno is %d!", ret);
        (void)pthread_attr_destroy(&attr);
        hinic3_free(aux);
        return -1;
    }
    (void)pthread_attr_destroy(&attr);
    return 0;
}

int hinic3_thread_cond_init(pthread_cond_t *cond, pthread_condattr_t *attr)
{
    int ret = pthread_cond_init(cond, attr);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "pthread cond init failed, ret is %d!", ret);
    }
    return ret;
}

int hinic3_thread_cond_destroy(pthread_cond_t *cond)
{
    int ret = pthread_cond_destroy(cond);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "pthread cond destroy failed, ret is %d!", ret);
    }
    return ret;
}

int hinic3_thread_cond_signal(pthread_cond_t *cond)
{
    int ret = pthread_cond_signal(cond);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "pthread cond signal failed, ret is %d!", ret);
    }
    return ret;
}
