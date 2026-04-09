/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_NLATTR_H
#define HINIC3_NLATTR_H

#include <stdint.h>
#include <stdbool.h>
#include "hinic3_util.h"

#define HINIC3_NLA_ALIGN_SIZE                4

struct hinic3_nlattr_obj {
    uint16_t nla_len;
    uint16_t nla_type;
};

typedef struct hinic3_nlattr_obj *hinic3_nlattr_itr;

struct hinic3_nlattr {
    /* the buf begin ptr */
    void *data;
    /* the buf length */
    size_t total_len;
    size_t used_len;
    /* first unused obj ptr */
    hinic3_nlattr_itr nla_itr;
};

static inline size_t hinic3_nlattr_size_align(size_t size)
{
    return (size + HINIC3_NLA_ALIGN_SIZE - 1) & ~(HINIC3_NLA_ALIGN_SIZE - 1);
}

#define HINIC3_NLA_HDRLEN (hinic3_nlattr_size_align(sizeof(struct hinic3_nlattr_obj)))

static inline hinic3_nlattr_itr hinic3_nlattr_itr_next(const hinic3_nlattr_itr nla)
{
    return (hinic3_nlattr_itr)((uint8_t *)nla + hinic3_nlattr_size_align(nla->nla_len));
}

#define HINIC3_NLATTR_FOR_EACH(ITER, ATTRS) \
    for ((ITER) = ((ATTRS)->data); (ITER) != ((ATTRS)->nla_itr); \
        (ITER) = hinic3_nlattr_itr_next(ITER))

static inline void hinic3_nlattr_init(struct hinic3_nlattr *nla, void *buf, size_t buf_len)
{
    if (buf) {
        nla->data = buf;
        nla->used_len = 0;
        nla->total_len = buf_len;
        nla->nla_itr = (hinic3_nlattr_itr)buf;
        return;
    }
    memset(nla, 0, sizeof(struct hinic3_nlattr));
}

static inline void hinic3_nlattr_reset_itr(struct hinic3_nlattr *nla, size_t real_len)
{
    if (nla) {
        nla->used_len = real_len;
        nla->nla_itr = (hinic3_nlattr_itr)((uint8_t *)nla->data + nla->used_len);
    }
}

static inline void *hinic3_nlattr_put_unspec_uninit(struct hinic3_nlattr *nla, uint16_t type, size_t size)
{
    hinic3_nlattr_itr nla_itr = nla->nla_itr;
    void *data_store_ptr = NULL;
    size_t initial_size = HINIC3_NLA_HDRLEN + size;
    size_t aligned_size = hinic3_nlattr_size_align(initial_size);
    if (HINIC3_LIKELY((nla->total_len - nla->used_len) >= aligned_size)) {
        nla_itr->nla_len = initial_size;
        nla_itr->nla_type = type;
        data_store_ptr = nla_itr + 1;
        nla->nla_itr = (hinic3_nlattr_itr)((uint8_t *)nla_itr + aligned_size);
        nla->used_len += aligned_size;
    }
    return data_store_ptr;
}

static inline uint16_t hinic3_nlattr_get_itr_type(const hinic3_nlattr_itr nla)
{
    return nla->nla_type;
}

static inline size_t hinic3_nlattr_get_itr_size(const hinic3_nlattr_itr nla)
{
    return nla->nla_len - HINIC3_NLA_HDRLEN;
}

static inline const void *hinic3_nlattr_get_itr_data(const hinic3_nlattr_itr nla)
{
    return nla + 1;
}

static inline const void *hinic3_nlattr_get_itr_unspec(const hinic3_nlattr_itr nla, size_t size HINIC3_UNUSED)
{
    return nla + 1;
}

static inline const char *hinic3_nlattr_get_itr_string(const hinic3_nlattr_itr nla)
{
    return (const char *)(nla + 1);
}

static inline uint64_t hinic3_nlattr_get_itr_u64(const hinic3_nlattr_itr nla)
{
    uint64_t *data_ptr = (uint64_t *)(nla + 1);
    return *data_ptr;
}

static inline uint32_t hinic3_nlattr_get_itr_u32(const hinic3_nlattr_itr nla)
{
    uint32_t *data_ptr = (uint32_t *)(nla + 1);
    return *data_ptr;
}

static inline uint16_t hinic3_nlattr_get_itr_u16(const hinic3_nlattr_itr nla)
{
    uint16_t *data_ptr = (uint16_t *)(nla + 1);
    return *data_ptr;
}

static inline uint8_t hinic3_nlattr_get_itr_u8(const hinic3_nlattr_itr nla)
{
    uint8_t *data_ptr = (uint8_t *)(nla + 1);
    return *data_ptr;
}

static inline bool hinic3_nlattr_get_itr_flag(const hinic3_nlattr_itr nla HINIC3_UNUSED)
{
    return true;
}

static inline int hinic3_nlattr_put_unspec(struct hinic3_nlattr *nla, uint16_t type, const void *data, size_t size)
{
    void *store_buf = hinic3_nlattr_put_unspec_uninit(nla, type, size);
    if (store_buf == NULL) {
        return -1;
    }

    if (data == NULL) {
        return 0;
    }

    memcpy(store_buf, data, size);
    return 0;
}

static inline int hinic3_nlattr_put_u64(struct hinic3_nlattr *nla, uint16_t type, uint64_t value)
{
    void *store_buf = hinic3_nlattr_put_unspec_uninit(nla, type, sizeof(uint64_t));
    if (store_buf == NULL) {
        return -1;
    }
    *(uint64_t*)store_buf = value;

    return 0;
}

static inline int hinic3_nlattr_put_u32(struct hinic3_nlattr *nla, uint16_t type, uint32_t value)
{
    void *store_buf = hinic3_nlattr_put_unspec_uninit(nla, type, sizeof(uint32_t));
    if (store_buf == NULL) {
        return -1;
    }
    *(uint32_t*)store_buf = value;

    return 0;
}

static inline int hinic3_nlattr_put_u16(struct hinic3_nlattr *nla, uint16_t type, uint16_t value)
{
    void *store_buf = hinic3_nlattr_put_unspec_uninit(nla, type, sizeof(uint16_t));
    if (store_buf == NULL) {
        return -1;
    }
    *(uint16_t*)store_buf = value;

    return 0;
}

static inline int hinic3_nlattr_put_u8(struct hinic3_nlattr *nla, uint16_t type, uint8_t value)
{
    void *store_buf = hinic3_nlattr_put_unspec_uninit(nla, type, sizeof(uint8_t));
    if (store_buf == NULL) {
        return -1;
    }
    *(uint8_t*)store_buf = value;

    return 0;
}

static inline int hinic3_nlattr_put_flag(struct hinic3_nlattr *nla, uint16_t type)
{
    void *store_buf = hinic3_nlattr_put_unspec_uninit(nla, type, 0);
    if (store_buf == NULL) {
        return -1;
    }

    return 0;
}
#endif
