/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_MUTEX_H
#define HINIC3_MUTEX_H
#include <pthread.h>
#include "rte_rwlock.h"

#ifdef PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP
#define HINIC3_MUTEX_INITIALIZER { PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP, "<unlocked>" }
#else
#define HINIC3_MUTEX_INITIALIZER { PTHREAD_MUTEX_INITIALIZER, "<unlocked>" }
#endif

struct hinic3_mutex {
    pthread_mutex_t lock;
    const char *where;
};

struct hinic3_thread_once {
    bool done;
    struct hinic3_mutex mutex;
};

struct hinic3_rwlock {
    pthread_rwlock_t lock;
};

struct hinic3_spin_rwlock {
    rte_rwlock_t lock;
};
struct hinic3_spinlock {
    pthread_spinlock_t lock;
};

int hinic3_rwlock_init(struct hinic3_rwlock *lock);
int hinic3_rwlock_destroy(struct hinic3_rwlock *lock);
int hinic3_rwlock_write_priority_init(struct hinic3_rwlock *lock);
int hinic3_rwlock_rdlock(struct hinic3_rwlock *lock);
int hinic3_rwlock_rdunlock(struct hinic3_rwlock *lock);
int hinic3_rwlock_wrlock(struct hinic3_rwlock *lock);
int hinic3_rwlock_wrunlock(struct hinic3_rwlock *lock);
int hinic3_rwlock_tryrdlock(struct hinic3_rwlock *lock);
int hinic3_mutex_cond_wait(pthread_cond_t *cond, struct hinic3_mutex *mutex);
int hinic3_pthread_mutex_init(struct hinic3_mutex *mutex);
int hinic3_pthread_mutex_lock(struct hinic3_mutex *mutex);
int hinic3_pthread_mutex_unlock(struct hinic3_mutex *mutex);
int hinic3_pthread_mutex_destroy(struct hinic3_mutex *mutex);
bool hinic3_thread_once_start(struct hinic3_thread_once *once);
void hinic3_thread_once_done(struct hinic3_thread_once *once);
int hinic3_spinlock_init(struct hinic3_spinlock *lock, int pshared);
int hinic3_spinlock_destroy(struct hinic3_spinlock *lock);
int hinic3_spinlock_lock(struct hinic3_spinlock *lock);
int hinic3_spinlock_trylock(struct hinic3_spinlock *lock);
int hinic3_spinlock_unlock(struct hinic3_spinlock *lock);
#endif
