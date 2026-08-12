/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2024 Huawei Technologies Co., Ltd
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <math.h>
#include <pwd.h>
#include <grp.h>
#include "hinic3_log.h"
#include "hinic3_util.h"
#include "hinic3_ui_string.h"
#include "hinic3_parse_agent_config.h"
#include "hinic3_file_util.h"

#define HINIC3_FILE_USAGE_CMD_LEN 256
#define HINIC3_FILE_OUTER_DIRECTORY_PATH "/var/log/dpak/"
#define HINIC3_FILE_DIRECTORY_PATH "/var/log/dpak/dpak_ovs_data/"
#define HINIC3_FILE_USER "dpak_maint"
#define HINIC3_FILE_GROUP "dpak_ovs"
#define HINIC3_FILE_USAGE_RESULT_LEN 64
#define HINIC3_FILE_INNER_PRIVILEGE 0750
#define HINIC3_FILE_PRIVILEGE 0640
#define HINIC3_FILE_SIZE_KB 1024
#define HINIC3_FILE_SIZE_MB (HINIC3_FILE_SIZE_KB * HINIC3_FILE_SIZE_KB)

int hinic3_get_absolute_file_path(const char *path, const char *file_name, char *absolute_path, int len)
{
    int ret;

    if (strchr(file_name, '/') != NULL) {
        HINIC3_LOG(ERR, AGENT, "The file name (%s) is wrong!", file_name);
        return -1;
    }

    if (path[strlen(path) - 1] == '/') {
        ret = snprintf(absolute_path, len, "%s%s", path, file_name);
    } else {
        ret = snprintf(absolute_path, len, "%s/%s", path, file_name);
    }
    if (ret <= 0) {
        HINIC3_LOG(ERR, AGENT, "get absolute path failed, err is %d!", ret);
        return -1;
    }

    return 0;
}

static bool hinic3_check_directory_existence(const char *path)
{
    struct stat info;

    // 检查路径是否存在
    if (access(path, F_OK) != 0) {
        return false;
    }

    // 检查路径是否可访问
    if (stat(path, &info) != 0) {
        return false;
    }

    // 检查是否为目录类型
    return (info.st_mode & S_IFDIR) ? true : false;
}

static int hinic3_mkdir(const char *path)
{
    char dir_path[PATH_MAX];
    struct passwd *user = NULL;
    struct group *grp = NULL;
    int ret;

    ret = snprintf(dir_path, PATH_MAX, "%s", path);
    if (ret <= 0) {
        HINIC3_LOG(ERR, AGENT, "get file directory failed, path: %s!", path);
        return -1;
    }

    if (mkdir(dir_path, HINIC3_FILE_INNER_PRIVILEGE) && errno != EEXIST) {
        HINIC3_LOG(ERR, AGENT, "create file directory failed, path: %s!", path);
        return -1;
    }

    if (hinic3_user_scenario_get() == COM_BD) {
        return 0;
    }
    
    user = getpwnam(HINIC3_FILE_USER);
    if (user == NULL) {
        HINIC3_LOG(ERR, AGENT, "Get file directory user failed, path: %s!", path);
        return 0;
    }
    grp = getgrnam(HINIC3_FILE_GROUP);
    if (grp == NULL) {
        HINIC3_LOG(ERR, AGENT, "Get file directory group failed, path: %s!", path);
        return 0;
    }

    if (hinic3_check_masked_to_exact_switch() == false) {
        if (chown(path, -1, grp->gr_gid) == -1) {
            HINIC3_LOG(ERR, AGENT, "Change file directory group failed, path: %s!", path);
            return 0;
        }
    } else {
        if (chown(path, user->pw_uid, grp->gr_gid) == -1) {
            HINIC3_LOG(ERR, AGENT, "Change file directory user and group failed, path: %s!", path);
            return 0;
        }
    }

    return 0;
}

static int hinic3_check_directory(const char *path)
{
    if (hinic3_check_directory_existence(path) == true) {
        return 1;
    } else if (hinic3_check_directory_existence(HINIC3_FILE_OUTER_DIRECTORY_PATH) == false) {
        HINIC3_LOG(INFO, AGENT, "The directory not exist, path: %s.",
            HINIC3_FILE_OUTER_DIRECTORY_PATH);
        return -1;
    }

    if (hinic3_mkdir(path) != 0) {
        HINIC3_LOG(ERR, AGENT, "create file directory failed, path: %s!", path);
        return -1;
    }
    return 0;
}

uint64_t hinic3_get_directory_size(const char *path, bool *is_obtained)
{
    char cmd[HINIC3_FILE_USAGE_CMD_LEN];
    char result[HINIC3_FILE_USAGE_RESULT_LEN] = {0};
    char *endptr = NULL;
    FILE *file = NULL;
    uint64_t real_size = 0;
    int ret;

    ret = snprintf(cmd, HINIC3_FILE_USAGE_CMD_LEN, "du -BM -s %s | cut -f1", path);
    if (ret <= 0) {
        HINIC3_LOG(ERR, AGENT, "get directory path failed, path: %s!", path);
        *is_obtained = false;
        return 0;
    }

    file = popen(cmd, "r");
    if (file == NULL) {
        HINIC3_LOG(ERR, AGENT, "open directory size failed, path: %s!", path);
        *is_obtained = false;
        return 0;
    }

    while (fgets(result, HINIC3_FILE_USAGE_RESULT_LEN, file) != NULL) {
        real_size = strtoul(result, &endptr, STR_TO_DEC_NUM);
        if (endptr == NULL || *endptr != 'M') {
            HINIC3_LOG(ERR, AGENT, "convert directory size failed, path: %s!", path);
            pclose(file);
            *is_obtained = false;
            return 0;
        }

        pclose(file);
        *is_obtained = true;
        return real_size;
    }

    pclose(file);
    HINIC3_LOG(ERR, AGENT, "get directory size failed!");
    *is_obtained = false;
    return 0;
}

static bool hinic3_is_directory_reach_limit(void)
{
    uint64_t real_size = 0;
    uint32_t directory_size_max = hinic3_disk_usage_get();
    int ret;
    bool is_obtained = false;

    ret = hinic3_check_directory(HINIC3_FILE_DIRECTORY_PATH);
    if (ret < 0) {
        HINIC3_LOG(ERR, AGENT, "check directory failed, path: %s!", HINIC3_FILE_DIRECTORY_PATH);
        return true;
    }

    real_size = hinic3_get_directory_size(HINIC3_FILE_DIRECTORY_PATH, &is_obtained);
    if (is_obtained == false) {
        HINIC3_LOG(ERR, AGENT, "get directory real size fail, path: %s!", HINIC3_FILE_DIRECTORY_PATH);
        return true;
    } else if (real_size >= directory_size_max) {
        HINIC3_LOG(ERR, AGENT, "directory %s size %" PRIu64 "MB reachs or exceeds the limit %uMB!",
            HINIC3_FILE_DIRECTORY_PATH, real_size, directory_size_max);
        return true;
    }

    return false;
}

static bool hinic3_is_path_under_default_directory(const char *path)
{
    size_t default_len = strlen(HINIC3_FILE_DIRECTORY_PATH);

    /* HINIC3_FILE_DIRECTORY_PATH 以 '/' 结尾，如 "/var/log/dpak/dpak_ovs_data/" */
    /* 精确匹配默认目录（带尾部 '/'） */
    if (strcmp(path, HINIC3_FILE_DIRECTORY_PATH) == 0)
        return true;

    /* 精确匹配默认目录（不带尾部 '/'） */
    if (strlen(path) == default_len - 1 &&
        strncmp(path, HINIC3_FILE_DIRECTORY_PATH, default_len - 1) == 0)
        return true;

    /* 子目录：path 以 "/var/log/dpak/dpak_ovs_data/" 开头 */
    if (strncmp(path, HINIC3_FILE_DIRECTORY_PATH, default_len) == 0)
        return true;

    return false;
}

int hinic3_check_output_file_directory(const char *path, const char *filename,
    char *absolute_path, unsigned int path_len, struct ds *ds)
{
    int ret;
    uint64_t dir_size;
    bool is_obtained = false;

    if (path == NULL || filename == NULL || absolute_path == NULL || ds == NULL ||
        path_len == 0 || path_len > PATH_MAX) {
        HINIC3_LOG(ERR, AGENT, "input param is invalid!");
        return -1;
    }

    ret = hinic3_get_absolute_file_path(path, filename, absolute_path, path_len);
    if (ret != 0) {
        hinic3_ds_put_format(ds, HINIC3_UI_FILE_GET_ABSOLUTE_PATH_FAIL_STRING, HINIC3_UI_LEADING_SIGN_ERROR, filename);
        return -1;
    }

    /* 子目录共享默认目录的磁盘配额，配额检查仍基于 HINIC3_FILE_DIRECTORY_PATH */
    if (hinic3_is_directory_reach_limit()) {
        dir_size = hinic3_get_directory_size(HINIC3_FILE_DIRECTORY_PATH, &is_obtained);
        if (is_obtained == false) {
            hinic3_ds_put_format(ds, "%sGet directory real size fail, path: %s.\n", HINIC3_UI_LEADING_SIGN_FAILURE,
                HINIC3_FILE_DIRECTORY_PATH);
            return -1;
        }
        hinic3_ds_put_format(ds, HINIC3_UI_FILE_NO_SPACE_STRING, HINIC3_UI_LEADING_SIGN_FAILURE, filename);
        hinic3_ds_put_format(ds, HINIC3_UI_FILE_DIRECTORY_SIZE_REACH_LIMIT_STRING, HINIC3_UI_INDENT_SPACE,
            hinic3_get_default_directory(), dir_size, hinic3_disk_usage_get());
        return -1;
    }

    return ret;
}

const char *hinic3_get_default_directory(void)
{
    return HINIC3_FILE_DIRECTORY_PATH;
}

uint64_t hinic3_get_partition_free_space_size(void)
{
    struct statvfs stat;
    uint64_t available_space;
    int ret;

    ret = hinic3_check_directory(HINIC3_FILE_DIRECTORY_PATH);
    if (ret < 0) {
        HINIC3_LOG(ERR, AGENT, "check directory failed, path: %s!", HINIC3_FILE_DIRECTORY_PATH);
        return 0;
    }

    if (statvfs(HINIC3_FILE_DIRECTORY_PATH, &stat) != 0) {
        HINIC3_LOG(ERR, AGENT, "get partition free space size failed, errno: %s, path: %s!", strerror(errno),
            HINIC3_FILE_DIRECTORY_PATH);
        return 0;
    }

    available_space = (uint64_t)stat.f_frsize * (uint64_t)stat.f_bavail;

    return (uint64_t)ceil(available_space / HINIC3_FILE_SIZE_MB);
}

int hinic3_check_file_realpath(const char *absolute_path, char *resolve_path, struct ds *ds)
{
    char *result = realpath(absolute_path, resolve_path);

    if (result == NULL) {
        hinic3_ds_put_format(ds, HINIC3_UI_FILE_PATH_CREATE_STRING, HINIC3_UI_LEADING_SIGN_INFO, resolve_path);
    } else {
        hinic3_ds_put_format(ds, HINIC3_UI_FILE_PATH_APPEND_STRING, HINIC3_UI_LEADING_SIGN_INFO, resolve_path);
    }

    if ((strlen(resolve_path) == 0) || (strlen(resolve_path) >= PATH_MAX)) {
        hinic3_ds_put_format(ds, HINIC3_UI_FILE_PATH_TIPS_STRING, HINIC3_UI_LEADING_SIGN_ERROR, resolve_path);
        return -1;
    }

    if (strcmp(resolve_path, absolute_path) != 0) {
        hinic3_ds_put_format(ds, "%s%s\n", HINIC3_UI_LEADING_SIGN_ERROR, HINIC3_UI_FILE_NOT_ABSOLUTE_PATH_STRING);
        return -1;
    }

    return 0;
}

int hinic3_agent_chown_output_file_path(const char *resolve_path)
{
    struct group *grp = getgrnam(HINIC3_OVS_GROUP_NAME);
    if (grp != NULL) {
        gid_t group_id = grp->gr_gid;
        if (chown(resolve_path, -1, group_id) == -1) {
            HINIC3_LOG(ERR, AGENT, "Chown fail, errno: %d!", errno);
            return -1;
        }
    } else {
        HINIC3_LOG(ERR, AGENT, "Getgrnam %s fail!", HINIC3_OVS_GROUP_NAME);
        return -1;
    }
    return 0;
}

int hinic3_agent_chmod_output_file_path(const char *resolve_path)
{
    int ret = chmod(resolve_path, HINIC3_FILE_PRIVILEGE);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "chmod fail, errno: %d!", errno);
    }

    return ret;
}

static int hinic3_mkdir_if_not_exist(const char *path)
{
    if (mkdir(path, HINIC3_FILE_INNER_PRIVILEGE) != 0) {
        if (errno != EEXIST)
            return -1;
    }
    return 0;
}

static int hinic3_create_output_directory(const char *path)
{
    char path_copy[PATH_MAX];
    int ret = snprintf(path_copy, sizeof(path_copy), "%s", path);
    if (ret <= 0) {
        HINIC3_LOG(ERR, AGENT, "Create output dir copy path failed, path: %s!", path);
        return -1;
    }

    for (char *p = path_copy + 1; *p != '\0'; p++) {
        if (*p != '/')
            continue;
        *p = '\0';
        if (access(path_copy, F_OK) != 0) {
            if (hinic3_mkdir_if_not_exist(path_copy) != 0) {
                *p = '/';
                return -1;
            }
        }
        *p = '/';
    }

    if (access(path_copy, F_OK) != 0)
        return hinic3_mkdir_if_not_exist(path_copy);

    return 0;
}

int hinic3_build_output_absolute_path(const char *path, const char *filename,
    char *absolute_path, unsigned int path_len, struct ds *ds)
{
    int ret;
    char resolve_path[PATH_MAX];

    if (path == NULL || filename == NULL || absolute_path == NULL ||
        ds == NULL || path_len == 0 || path_len > PATH_MAX) {
        HINIC3_LOG(ERR, AGENT, "input param is invalid!");
        return -1;
    }

    /* 目录不存在时自动创建 */
    if (hinic3_check_directory_existence(path) == false) {
        ret = hinic3_create_output_directory(path);
        if (ret != 0) {
            hinic3_ds_put_format(ds, "%sfailed to create directory: %s\n",
                HINIC3_UI_LEADING_SIGN_ERROR, path);
            return -1;
        }
    }

    if (hinic3_is_path_under_default_directory(path))
        return hinic3_check_output_file_directory(path, filename, absolute_path, path_len, ds);

    ret = hinic3_get_absolute_file_path(path, filename, absolute_path, path_len);
    if (ret != 0) {
        hinic3_ds_put_format(ds, HINIC3_UI_FILE_GET_ABSOLUTE_PATH_FAIL_STRING,
            HINIC3_UI_LEADING_SIGN_ERROR, filename);
        return -1;
    }

    ret = hinic3_check_file_realpath(absolute_path, resolve_path, ds);
    if (ret != 0)
        return -1;

    return ret;
}