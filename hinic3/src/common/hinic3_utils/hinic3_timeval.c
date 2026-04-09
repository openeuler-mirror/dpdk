/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2025 Huawei Technologies Co., Ltd
 */

#include <ctype.h>
#include "hinic3_log.h"
#include "hinic3_util.h"
#include "hinic3_timeval.h"
 
#define HINIC3_TIME_BUFFER_SIZE 64
 
int
hinic3_timespec_compare(const struct timespec *a, const struct timespec *b)
{
    if (a->tv_sec < b->tv_sec) {
        return -1;
    }
    if (a->tv_sec > b->tv_sec) {
        return 1;
    }
    if (a->tv_nsec < b->tv_nsec) {
        return -1;
    }
    if (a->tv_nsec > b->tv_nsec) {
        return 1;
    }
    return 0;
}
 
time_t
hinic3_parse_string_to_duration(const char *time_str)
{
    if (time_str == NULL || strlen(time_str) == 0 || strlen(time_str) >= HINIC3_TIME_BUFFER_SIZE) {
        HINIC3_LOG(ERR, AGENT, "Invalid time string %s!", time_str);
        return -EINVAL;
    }
 
    time_t total_seconds = 0;
    time_t current_value = 0;
    const char *p = time_str;
    bool has_valid_content = false; // 标记是否有有效内容
 
    while (*p != '\0') {
        if (isdigit((unsigned char)*p)) {
            current_value = current_value * BASE_DECIMAL + (*p - '0');
            has_valid_content = true;
        } else {
            char lower_char = tolower((unsigned char)*p);
            switch (lower_char) {
                case 'h':
                    total_seconds += current_value * HINIC3_SEC_PER_HOUR;
                    current_value = 0;
                    has_valid_content = true;
                    break;
                case 'm':
                    total_seconds += current_value * HINIC3_SEC_PER_MIN;
                    current_value = 0;
                    has_valid_content = true;
                    break;
                case 's':
                    total_seconds += current_value;
                    current_value = 0;
                    has_valid_content = true;
                    break;
                case ' ': case '\t': case '\n': case '\r':
                    break; // 忽略空白字符
                default:
                    current_value = 0;
                    break;
            }
        }
        p++;
    }

    // 处理字符串末尾没有单位的情况（例如"123"视为123分钟）
    if (current_value > 0) {
        total_seconds += current_value * HINIC3_SEC_PER_MIN;
        has_valid_content = true;
    }
 
    if (total_seconds <= 0 || !has_valid_content || total_seconds > HINIC3_MAX_DURATION) {
        HINIC3_LOG(ERR, AGENT, "Invalid time string %s!", time_str);
        return -ERANGE;
    }
 
    return total_seconds;
}

static const char *
hinic3_build_time_string(int hours, int minutes, int seconds)
{
    static char time_buffer[HINIC3_TIME_BUFFER_SIZE] = {0};
    int has_content = 0;
    int len = 0;

    time_buffer[0] = '\0';

    if (hours > 0) {
        len = strlen(time_buffer);
        snprintf(time_buffer + len, sizeof(time_buffer) - len, "%dh", hours);
        has_content = 1;
    }

    if (minutes > 0) {
        len = strlen(time_buffer);
        snprintf(time_buffer + len, sizeof(time_buffer) - len, "%dm", minutes);
        has_content = 1;
    }

    if (seconds > 0 || !has_content) {
        len = strlen(time_buffer);
        snprintf(time_buffer + len, sizeof(time_buffer) - len, "%ds", seconds);
    }

    return time_buffer;
}

const char *
hinic3_transform_duration_to_string(time_t time_sec)
{
    int hours = 0;
    int minutes = 0;
    int seconds = 0;
    time_t duration = time_sec;
 
    if (duration <= 0) {
        return "0s";
    }

    if (duration > HINIC3_MAX_DURATION) {
        duration = HINIC3_DEFAULT_DURATION;
    }
 
    hours = duration / HINIC3_SEC_PER_HOUR;
    minutes = (duration % HINIC3_SEC_PER_HOUR) / HINIC3_SEC_PER_MIN;
    seconds = duration % HINIC3_SEC_PER_MIN;
 
    return hinic3_build_time_string(hours, minutes, seconds);
}