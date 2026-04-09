/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Huawei Technologies Co., Ltd
 */

#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include "hinic3_log.h"
#include "hinic3_util.h"
#include "hinic3_mutex.h"

int hinic3_rwlock_init(struct hinic3_rwlock *lock)
{
    int ret = pthread_rwlock_init(&lock->lock, NULL);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "hinic3 rwlock init failed, ret is %d!", ret);
    }
    return ret;
}

int hinic3_rwlock_destroy(struct hinic3_rwlock *lock)
{
    int ret = pthread_rwlock_destroy(&lock->lock);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "hinic3 rwlock destroy failed, ret is %d!", ret);
    }
    return ret;
}

int hinic3_rwlock_write_priority_init(struct hinic3_rwlock *lock)
{
    int ret;
    pthread_rwlockattr_t attr;
    ret = pthread_rwlockattr_init(&attr);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "pthread rwlock init failed, ret is %d!", ret);
        return ret;
    }
    ret = pthread_rwlockattr_setkind_np(&attr, PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "pthread rwlock setkind failed, ret is %d!", ret);
        pthread_rwlockattr_destroy(&attr);
        return ret;
    }
    ret = pthread_rwlock_init(&lock->lock, &attr);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "hinic3 rwlock init failed!");
    }
    pthread_rwlockattr_destroy(&attr);
    return ret;
}

int hinic3_rwlock_rdlock(struct hinic3_rwlock *lock)
{
    int ret = pthread_rwlock_rdlock(&lock->lock);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "hinic3 rwlock rdlock failed, ret is %d!", ret);
    }
    return ret;
}

int hinic3_rwlock_rdunlock(struct hinic3_rwlock *lock)
{
    int ret = pthread_rwlock_unlock(&lock->lock);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "hinic3 rdlock rdunlock failed, ret is %d!", ret);
    }
    return ret;
}

int hinic3_rwlock_wrlock(struct hinic3_rwlock *lock)
{
    int ret = pthread_rwlock_wrlock(&lock->lock);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "hinic3 rdlock wrlock failed, ret is %d!", ret);
    }
    return ret;
}

int hinic3_rwlock_wrunlock(struct hinic3_rwlock *lock)
{
    int ret = pthread_rwlock_unlock(&lock->lock);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "hinic3 rdlock wrUNlock failed, ret is %d!", ret);
    }
    return ret;
}

int hinic3_rwlock_tryrdlock(struct hinic3_rwlock *lock)
{
    return pthread_rwlock_tryrdlock(&lock->lock);
}

int hinic3_mutex_cond_wait(pthread_cond_t *cond, struct hinic3_mutex *mutex)
{
    int ret = pthread_cond_wait(cond, &mutex->lock);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "hinic3 cond wait failed, ret is %d!", ret);
    }
    return ret;
}

int hinic3_pthread_mutex_init(struct hinic3_mutex *mutex)
{
    int ret = pthread_mutex_init(&mutex->lock, NULL);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "hinic3 mutex init failed, ret is %d!", ret);
    }
    return ret;
}

int hinic3_pthread_mutex_lock(struct hinic3_mutex *mutex)
{
    int ret = pthread_mutex_lock(&mutex->lock);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "hinic3 mutex lock failed, ret is %d!", ret);
    }
    return ret;
}

int hinic3_pthread_mutex_unlock(struct hinic3_mutex *mutex)
{
    int ret = pthread_mutex_unlock(&mutex->lock);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "hinic3 mutex unlock failed, ret is %d!", ret);
    }
    return ret;
}

int hinic3_pthread_mutex_destroy(struct hinic3_mutex *mutex)
{
    int ret = pthread_mutex_destroy(&mutex->lock);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "hinic3 mutex destroy failed, ret is %d!", ret);
    }
    return ret;
}

static bool hinic3_thread_once_start_(struct hinic3_thread_once *once)
{
    if (hinic3_pthread_mutex_lock(&once->mutex) == 0) {
        if (!once->done) {
            (void)hinic3_pthread_mutex_unlock(&once->mutex);
            return true;
        }
        (void)hinic3_pthread_mutex_unlock(&once->mutex);
    }
    return false;
}

bool hinic3_thread_once_start(struct hinic3_thread_once *once)
{
    return !once ->done && hinic3_thread_once_start_(once);
}

void hinic3_thread_once_done(struct hinic3_thread_once *once)
{
    if (hinic3_pthread_mutex_lock(&once->mutex) == 0) {
        once->done = true;
        (void)hinic3_pthread_mutex_unlock(&once->mutex);
    }
}

int hinic3_spinlock_init(struct hinic3_spinlock *lock, int pshared)
{
    int ret = pthread_spin_init(&lock->lock, pshared);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "hinic3 spinlock init failed, ret is %d!", ret);
    }
    return ret;
}

int hinic3_spinlock_destroy(struct hinic3_spinlock *lock)
{
    int ret = pthread_spin_destroy(&lock->lock);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "hinic3 spinlock destroy failed, ret is %d!", ret);
    }
    return ret;
}

int hinic3_spinlock_lock(struct hinic3_spinlock *lock)
{
    int ret = pthread_spin_lock(&lock->lock);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "hinic3 spinlock lock failed, ret is %d!", ret);
    }
    return ret;
}

int hinic3_spinlock_trylock(struct hinic3_spinlock *lock)
{
    int ret = pthread_spin_trylock(&lock->lock);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "hinic3 spinlock trylock failed, ret is %d!", ret);
    }
    return ret;
}

int hinic3_spinlock_unlock(struct hinic3_spinlock *lock)
{
    int ret = pthread_spin_unlock(&lock->lock);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "hinic3 spinlock unlock failed, ret is %d!", ret);
    }
    return ret;
}
