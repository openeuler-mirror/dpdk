/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Huawei Technologies Co., Ltd
 */

#ifndef _HINIC3_TIMEVAL_H_
#define _HINIC3_TIMEVAL_H_

#include <stdio.h>
#include <time.h>

#define HINIC3_US_PER_MS     1000
#define HINIC3_MS_PER_SEC    1000
#define HINIC3_SEC_PER_HOUR  3600
#define HINIC3_SEC_PER_MIN   60
#define HINIC3_MIN_PER_HOUR  60
#define HINIC3_STRUCT_TM_BASE_YEAR   1900
#define HINIC3_STRUCT_TM_BASE_MONTH  1
#define HINIC3_MS_STR_FMT    "%04u-%02u-%02u %02u:%02u:%02u.%03u"
#define HINIC3_MS_STR_SIZE   25 // 2000-01-01 00:00:00.000

#define HINIC3_DEFAULT_DURATION 3600 // 默认1小时
#define HINIC3_MAX_DURATION     (7 * 24 * 3600) // 上限7天

static inline int
time_msec_to_str(long long int msec, char buf[], size_t bufsize __rte_unused)
{
    time_t sec = msec / HINIC3_MS_PER_SEC;
    int millisec = (int) (msec % HINIC3_MS_PER_SEC);
    if (millisec < 0) {
        millisec += HINIC3_MS_PER_SEC;
        sec -= 1;
    }
    struct tm *info = localtime(&sec);
    if (info == NULL) {
        return -1;
    }
    return sprintf(buf, HINIC3_MS_STR_FMT,
        info->tm_year + HINIC3_STRUCT_TM_BASE_YEAR, info->tm_mon + HINIC3_STRUCT_TM_BASE_MONTH, info->tm_mday,
        info->tm_hour, info->tm_min, info->tm_sec, millisec);
}

/*
 * 函 数 名 : hinic3_timespec_compare
 * 功能描述 : 比较两个 timespec 时间
 * 返 回 值 : @return -1 (a < b), 0 (a == b), 1 (a > b)
 */
int hinic3_timespec_compare(const struct timespec *a, const struct timespec *b);

/*
 * 函 数 名 : hinic3_parse_string_to_duration
 * 功能描述 : 时间字符转换为时间戳
 * 输入参数 : @param time_str  复位时间，采用时分秒结构输入(1h5m30s)，给NULL则取默认时长1小时
 * 返 回 值 : @return 将输入字符串转换为秒数返回
                     -EINVAL 入参为NULL，或入参为空串，或入参字符超过缓冲区大小64字节
                     -ERANGE 解析结果超过[1, HINIC3_MAX_DURATION]
 */
time_t hinic3_parse_string_to_duration(const char *time_str);

/*
 * 函 数 名 : hinic3_transform_duration_to_string
 * 功能描述 : 时间戳转换为时间字符
 * 输入参数 : @param time_sec  复位时间秒数
 * 返 回 值 : @return 返回时间字符串时分秒结构(1h5m30s)
 */
const char *hinic3_transform_duration_to_string(time_t time_sec);
#endif
