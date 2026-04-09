/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Huawei Technologies Co., Ltd
 */

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <pwd.h>
#include "hinic3_agent.h"
#include "hinic3_ui_string.h"

#define COMMAND_CTL_LOCATION "/opt/dpak/dpak_ovs_command.ctl"
#define MAX_UN_LEN (sizeof(((struct sockaddr_un *)0)->sun_path) - 1)
#define HINIC3_COMMAND_LACK_PARA_ERROR "Incomplete command, please input -h or --help to get help info!"
#define HINIC3_NOT_SUPPORT_OPTION "Unrecognized command, please input -h or --help to get help info!"
#define HINIC3_CONNECTION_FAILED_ERROR "The connection to the server cannot be established."
#define HINIC3_TIMEOUT_ERROR "Command execution timed out."
#define HINIC3_EXECUTION_ERROR "Command server does not return the correct result."
#define HINIC3_FOPEN_ERROR "fdopen operation failed"
#define HINIC3_COMMAND_LENGTH_ERROR "The command is too long."
#define HINIC3_FLUSH_PORT_WARNING_CONTINUE \
    "Warning: flush-ports may cuase port statistics on vSwitch to jump, Continue? [Y/N]"

#define COMMAND_OPTION_MATCH 1
#define MIN_COMMAND_NUM 2
#define MAX_COMMAND_LENGTH 512
#define MAX_REPLY_BUFFER_SIZE 1000
#define CONNECT_FAILED_TIMEOUT 10
#define RETRY_TIMES 3
#define HINIC3_DEFAULT_MAJOR_VERSION_STRING "25.0"
#define HINIC3_DEFAULT_SUB_VERSION_STRING "(null)"

#ifdef BUILD_MAJOR_VERSION
#define HINIC3_BUILD_MAJOR_VERSION BUILD_MAJOR_VERSION
#else
#define HINIC3_BUILD_MAJOR_VERSION HINIC3_DEFAULT_MAJOR_VERSION_STRING
#endif

#ifdef BUILD_SUB_VERSION
#define HINIC3_BUILD_SUB_VERSION BUILD_SUB_VERSION
#else
#define HINIC3_BUILD_SUB_VERSION HINIC3_DEFAULT_SUB_VERSION_STRING
#endif

enum command_error {
    TIMEOUT_ERROR = -2,
    EXECUTION_ERROR = -3,
};

static void
hinic3_show_command_help_info(void)
{
    printf("%2sUsage: ", HINIC3_UI_INDENT_SPACE);
    printf("dpak-ovs-ctl COMMAND [ARG...]\n\n");
    printf("%2sCommon commands:\n", HINIC3_UI_INDENT_SPACE);
    printf("%4s%-30sList the commands supported by dpak-ovs\n\n", HINIC3_UI_INDENT_SPACE, "list-commands");
    printf("%2sOther options:\n", HINIC3_UI_INDENT_SPACE);
    printf("%4s%-30sDisplay the help information\n", HINIC3_UI_INDENT_SPACE, "-h, --help");
}

static int
hinic3_client_make_sockaddr_un(const char *name, struct sockaddr_un *un)
{
    size_t max_size = sizeof(un->sun_path);
    size_t len = strnlen(name, max_size - 1);
    memcpy(un->sun_path, name, len);
    un->sun_path[len] = '\0';
    un->sun_family = AF_UNIX;
    return 0;
}

static int
hinic3_connect_to_target(const char *connect_path)
{
    int ret;
    int fd;
    struct sockaddr_un un;
    struct timeval timeo = {CONNECT_FAILED_TIMEOUT, 0};

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeo, sizeof(struct timeval));
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeo, sizeof(struct timeval));
    ret = hinic3_client_make_sockaddr_un(connect_path, &un);
    if (ret != 0) {
        close(fd);
        return -1;
    }
    ret = connect(fd, (struct sockaddr *)&un, sizeof(struct sockaddr_un));
    if (ret != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int
hinic3_client_send_command(FILE *file, int argc, char *agrv[])
{
    uid_t uid = getuid();
    struct passwd *pw = getpwuid(uid);
    if (pw == NULL) {
        printf("%sgetpwuid error, uid is %u(errno=%d).\n", HINIC3_UI_LEADING_SIGN_FAILURE, uid, errno);
        return -1;
    }
    int ret = fprintf(file, "%s", pw->pw_name);
    if (ret <= 0) {
        printf("fprintf error.\n");
        return -1;
    }
    (void)fputs(" ", file);
    for (int i = 0; i < argc; i++) {
        (void)fputs(agrv[i], file);

        if (i + 1 != argc) {
            (void)fputs(" ", file);
        }
    }
    (void)fputs("\n", file);
    (void)fflush(file);
    return 0;
}

static int
hinic3_client_print_response(FILE *file)
{
    int res = TIMEOUT_ERROR;
    char resp_buff[MAX_REPLY_BUFFER_SIZE] = { 0 };
    bool is_result = true;
    bool exit_cond = false;
    do {
        char *result = fgets(resp_buff, sizeof(resp_buff) - 1, file);
        if (result == NULL) {
            exit_cond = true;
            break;
        }
        if (is_result) {
            if (strcmp(result, "0\n") == 0) {
                res = 0;
            } else if (strcmp(result, "-1\n") == 0) {
                res = -1;
            } else {
                return EXECUTION_ERROR;
            }
            is_result = false;
        } else {
            printf("%s", resp_buff);
            memset(resp_buff, 0, MAX_REPLY_BUFFER_SIZE);
        }
    } while (!exit_cond);
    return res;
}

static int
hinic3_get_confirm(const char *warningInfo)
{
    char buf[MAX_COMMAND_LENGTH + 1] = {0};
    int ch;
    for (int i = 0; i < RETRY_TIMES; i++) {
        printf("%s", warningInfo);
        if (fgets(buf, MAX_COMMAND_LENGTH + 1, stdin) == NULL)
            continue;
        if (strcmp(buf, "Y\n") == 0 || strcmp(buf, "y\n") == 0)
            return 0;
        if (strcmp(buf, "N\n") == 0 || strcmp(buf, "n\n") == 0)
            return -1;
        if (strlen(buf) >= MAX_COMMAND_LENGTH)
            while ((ch = getchar()) != (int)'\n' && ch != EOF) {};
    }
    return -1;
}

static int
hinic3_check_client_args(int argc, char *argv[])
{
    int length = 0;
    for (int i = 0; i < argc; i++) {
        length += strlen(argv[i]);
    }
    if (length > MAX_COMMAND_LENGTH) {
        printf("%s%s\n", HINIC3_UI_LEADING_SIGN_ERROR, HINIC3_COMMAND_LENGTH_ERROR);
        return -1;
    }
    if (argv[MIN_COMMAND_NUM - 1][0] == '-') {
        if ((strcmp(argv[MIN_COMMAND_NUM - 1], "-h") == 0) || (strcmp(argv[MIN_COMMAND_NUM - 1], "--help") == 0)) {
            (void)hinic3_show_command_help_info();
            return COMMAND_OPTION_MATCH;
        } else {
            printf("%s%s\n", HINIC3_UI_LEADING_SIGN_ERROR, HINIC3_NOT_SUPPORT_OPTION);
            return -1;
        }
    }
    if (strcmp(argv[MIN_COMMAND_NUM - 1], "hwoff/flush-ports") == 0 &&
        strcmp(argv[argc - 1], "-h") != 0 && strcmp(argv[argc - 1], "--help") != 0) {
        if (hinic3_get_confirm(HINIC3_FLUSH_PORT_WARNING_CONTINUE) == -1) {
            printf("%sUser not confirmed, exit command.\n", HINIC3_UI_LEADING_SIGN_ERROR);
            return -1;
        }
    }
    return 0;
}

int
main(int argc, char *argv[])
{
    int fd;
    FILE *file = NULL;
    int ret;

    if (argc < MIN_COMMAND_NUM) {
        printf("%s%s\n", HINIC3_UI_LEADING_SIGN_ERROR, HINIC3_COMMAND_LACK_PARA_ERROR);
        return -1;
    }

    ret = hinic3_check_client_args(argc, argv);
    if (ret == COMMAND_OPTION_MATCH)
        return 0;
    if (ret == -1)
        return -1;

    fd = hinic3_connect_to_target(COMMAND_CTL_LOCATION);
    if (fd < 0) {
        printf("%s%s\n", HINIC3_UI_LEADING_SIGN_FAILURE, HINIC3_CONNECTION_FAILED_ERROR);
        return -1;
    }

    file = fdopen(fd, "a+");
    if (file == NULL) {
        printf("%s%s\n", HINIC3_UI_LEADING_SIGN_FAILURE, HINIC3_FOPEN_ERROR);
        close(fd);
        return -1;
    }

    ret = hinic3_client_send_command(file, argc - 1, argv + 1);
    if (ret != 0)
        goto fail;

    ret = hinic3_client_print_response(file);
    if (ret != 0) {
        if (ret == -1) {
            goto fail;
        } else if (ret == EXECUTION_ERROR) {
            printf("%s%s\n", HINIC3_UI_LEADING_SIGN_FAILURE, HINIC3_EXECUTION_ERROR);
            goto fail;
        } else {
            printf("%s%s\n", HINIC3_UI_LEADING_SIGN_FAILURE, HINIC3_TIMEOUT_ERROR);
            goto fail;
        }
    }
    (void)fclose(file);
    return 0;
fail:
    (void)fclose(file);
    return -1;
}
