/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2023. All rights reserved.
 * Description:
 */

#ifndef HINIC3_LIST_H
#define HINIC3_LIST_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "hinic3_util.h"
#include "hinic3_util.h"

struct hinic3_list {
    struct hinic3_list *prev;     /* Previous list element. */
    struct hinic3_list *next;     /* Next list element. */
};

static inline void hinic3_list_init(struct hinic3_list *list);
static inline void hinic3_list_insert(struct hinic3_list *before, struct hinic3_list *elem);
static inline struct hinic3_list *hinic3_list_remove(struct hinic3_list *elem);
static inline size_t hinic3_list_size(const struct hinic3_list *list);
static inline bool hinic3_list_is_empty(const struct hinic3_list *list);

#define LIST_FOR_EACH(ITER, MEMBER, LIST)                               \
    for (HINIC3_CONTAINER_INIT(ITER, (LIST)->next, MEMBER);                    \
         &(ITER)->MEMBER != (LIST);                                     \
         HINIC3_CONTAINER_ASSIGN(ITER, (ITER)->MEMBER.next, MEMBER))

#define LIST_FOR_EACH_SAFE(ITER, NEXT, MEMBER, LIST)               \
    for (HINIC3_CONTAINER_INIT(ITER, (LIST)->next, MEMBER);               \
         (&(ITER)->MEMBER != (LIST) ? HINIC3_CONTAINER_INIT(NEXT, (ITER)->MEMBER.next, MEMBER), 1 : 0);    \
         (ITER) = (NEXT))

static inline void hinic3_list_init(struct hinic3_list *list)
{
    list->next = list->prev = list;
}

static inline void hinic3_list_insert(struct hinic3_list *before, struct hinic3_list *elem)
{
    elem->prev = before->prev;
    elem->next = before;
    before->prev->next = elem;
    before->prev = elem;
}

static inline struct hinic3_list* hinic3_list_remove(struct hinic3_list *elem)
{
    elem->prev->next = elem->next;
    elem->next->prev = elem->prev;
    return elem->next;
}

static inline size_t hinic3_list_size(const struct hinic3_list *list)
{
    const struct hinic3_list *e;
    size_t cnt = 0;

    for (e = list->next; e != list; e = e->next) {
        cnt++;
    }
    return cnt;
}

static inline bool hinic3_list_is_empty(const struct hinic3_list *list)
{
    return list->next == list;
}

#endif
