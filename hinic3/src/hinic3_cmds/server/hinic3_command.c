/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Huawei Technologies Co., Ltd
 */

#include "hinic3_command.h"
#include <sys/socket.h>
#include <grp.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <unistd.h>
#include "hinic3_shash.h"
#include "hinic3_log.h"
#include "hinic3_thread.h"
#include "hinic3_ds.h"
#include "hinic3_check_thread_health_state.h"
#include "hinic3_meminfo.h"
#include "hinic3_ui_string.h"
#include "hinic3_error_stats.h"
#include "hinic3_parse_agent_config.h"

#define HINIC3_COMMAND_STR_MAX_LEN 50
#define HINIC3_COMMAND_MAX_LEN 1000

#define HINIC3_COMMAND_HASH_BUCKET_NUM 5
#define MAX_COMMAND_ARG_NUM 30
#define HINIC3_COMMAND_NUM 30
#define HINIC3_LISTEN_THREAD_TIMEOUT (20 * 1000)
#define SOCKET_PATH "/opt/dpak/dpak_ovs_command.ctl"
#define HINIC3_SOCKET_RIGHT 0660
#define HINIC3_COMMAND_MAX_RECV_TIME 10000 // 10s
#define HINIC3_COMMAND_MAX_SEND_TIME 10000 // 10s, 如果输出内容过长，需要修改为输出到文件，避免影响其他用户
#define HINIC3_COMMAND_SLEEP_INTERVAL 100000 // 100ms
#define CMD_EPOLL_SIZE  1024
#define EVENT_MAX_COUNT 20

struct hinic3_command_mgr {
    pthread_t thread;
    struct shash command_map;
    int listen_socket;
    int epoll_fd;
    uint32_t thread_exit;
};

struct hinic3_command_mgr g_command_mgr = {0};

static void
hinic3_list_commands(struct unixctl_conn *conn, int argc HINIC3_UNUSED, const char *argv[] HINIC3_UNUSED, void *aux)
{
    struct shash_node *node = NULL;
    struct ds reply = DS_EMPTY_INITIALIZER;

    hinic3_ds_put_format(&reply, "Available commands:\n");

    HINIC3_SHASH_FOR_EACH(node, &g_command_mgr.command_map) {
        const struct hinic3_command *command = node->data;
        hinic3_ds_put_format(&reply, "  %-35s %s\n", node->name, command->usage);
    }

    hinic3_command_reply(conn, hinic3_ds_cstr(&reply));
    hinic3_ds_destroy(&reply);
    *(int *)aux = 0;
}

void
hinic3_command_register(const char *name, const char *usage, int min_args, int max_args, unixctl_cb_func *cb,
    void *aux HINIC3_UNUSED)
{
    struct hinic3_command *command = NULL;
    char key[HINIC3_COMMAND_STR_MAX_LEN] = {0};
    strcpy(key, name);

    const struct hinic3_command *command_ptr = (const struct hinic3_command *)hinic3_shash_find_data(&g_command_mgr.command_map, key);
    if (command_ptr != NULL) {
        HINIC3_LOG(ERR, AGENT, "hinic3 command %s already registered", key);
        return;
    }

    command = (struct hinic3_command*)hinic3_calloc(1, sizeof(struct hinic3_command), HINIC3_COMMAND);
    if (command == NULL) {
        HINIC3_LOG(ERR, AGENT, "Create new hinic3 command %s failed", key);
        return;
    }

    command->usage = usage;
    command->min_argc = min_args;
    command->max_argc = max_args;
    command->cb_func = cb;

    bool ret = hinic3_shash_add_once(&g_command_mgr.command_map, key, (void*)command, HINIC3_COMMAND);
    if (ret != true) {
        HINIC3_LOG(ERR, AGENT, "reg command %s fail", key);
        hinic3_free(command);
        return;
    }

    return;
}

void
hinic3_command_register_or_update(const char *name, const char *usage, int min_args, int max_args, unixctl_cb_func *cb,
    void *aux HINIC3_UNUSED)
{
    struct hinic3_command *command = NULL;
    char key[HINIC3_COMMAND_STR_MAX_LEN] = {0};
    strcpy(key, name);

    struct shash_node *node = hinic3_shash_find(&g_command_mgr.command_map, key);
    if (node) {
        HINIC3_LOG(INFO, AGENT, "Hinic3 command '%s' already registered, overwriting existing command!", key);
        hinic3_shash_steal(&g_command_mgr.command_map, node);
    }

    command = (struct hinic3_command*)hinic3_calloc(1, sizeof(struct hinic3_command), HINIC3_COMMAND);
    if (command == NULL) {
        HINIC3_LOG(ERR, AGENT, "Create new hinic3 command %s failed", key);
        return;
    }

    command->usage = usage;
    command->min_argc = min_args;
    command->max_argc = max_args;
    command->cb_func = cb;

    bool ret = hinic3_shash_add_once(&g_command_mgr.command_map, key, (void*)command, HINIC3_COMMAND);
    if (ret != true) {
        HINIC3_LOG(ERR, AGENT, "reg command %s fail", key);
        hinic3_free(command);
        return;
    }

    return;
}


static int
hinic3_write_str_to_socket(struct unixctl_conn *conn, const char *str)
{
    if (str == NULL)
        return -1;

    long long time_start = hinic3_time_msec();
    const char *cursor = str;

    while (*cursor != 0) {
        long long time_now = hinic3_time_msec();
        if (time_now - time_start > HINIC3_COMMAND_MAX_SEND_TIME) {
            HINIC3_LOG(INFO, AGENT, "command send time out");
            return -1;
        }

        // 实测用fputc接口，数据量较大时，部分数据无法发送出去，这里write接口
        int ret = write(conn->sock_fd, cursor, 1);
        if (ret == EOF) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(HINIC3_COMMAND_SLEEP_INTERVAL);
                continue;
            } else {
                return -1;
            }
        }

        cursor++;
    }

    return 0;
}

void
hinic3_command_reply(struct unixctl_conn *conn, const char *body)
{
    hinic3_write_str_to_socket(conn, "0\n");
    hinic3_write_str_to_socket(conn, body);
}

void
hinic3_command_reply_error(struct unixctl_conn *conn, const char *error)
{
    hinic3_write_str_to_socket(conn, "-1\n");
    hinic3_write_str_to_socket(conn, error);
}

static unixctl_cb_func *get_cmd_func(const char *command_str)
{
    char key[HINIC3_COMMAND_STR_MAX_LEN] = {0};
    strcpy(key, command_str);

    const struct hinic3_command *command = (const struct hinic3_command *)hinic3_shash_find_data(&g_command_mgr.command_map, key);
    if (command == NULL)
        return (unixctl_cb_func *)NULL;

    return command->cb_func;
}

static const struct hinic3_command *
get_cmd_command(const char *command_str)
{
    char key[HINIC3_COMMAND_STR_MAX_LEN] = {0};
    strcpy(key, command_str);
    const struct hinic3_command *command = (const struct hinic3_command *)hinic3_shash_find_data(&g_command_mgr.command_map, key);
    return command;
}

static int
hinic3_cmd_param_check(const char *command_str, int argc)
{
    const struct hinic3_command *command = get_cmd_command(command_str);
    if (command == NULL)
        return HINIC3_COMMAND_ERROR_TYPE_NULL;
    if (argc - 1 > command->max_argc)
        return HINIC3_COMMAND_ERROR_TYPE_EXCESSIVE;
    if (argc - 1 < command->min_argc)
        return HINIC3_COMMAND_ERROR_TYPE_INSUFFICIENT;
    return 0;
}

static int
dispatch_command(struct unixctl_conn *conn, int argc, const char *argv[])
{
    if (argc == 0)
        return -1;
    int ret = 0;
    unixctl_cb_func *cmd_func = get_cmd_func(argv[0]);
    if (cmd_func != NULL) {
        ret = hinic3_cmd_param_check(argv[0], argc);
        if (ret == 0) {
            cmd_func(conn, argc, argv, &ret);
        } else if (ret == HINIC3_COMMAND_ERROR_TYPE_EXCESSIVE) {
            hinic3_add_error_stats(HINIC3_EXCESSIVE_COMMAND, 1);
            HINIC3_LOG(ERR, AGENT, HINIC3_UI_ERROR_TOO_MANY_PARAMETER);
            hinic3_command_reply_error(conn, HINIC3_UI_LEADING_SIGN_ERROR HINIC3_UI_ERROR_TOO_MANY_PARAMETER "!\n");
            return -EINVAL;
        } else if (ret == HINIC3_COMMAND_ERROR_TYPE_INSUFFICIENT) {
            hinic3_add_error_stats(HINIC3_INCOMPLETE_COMMAND, 1);
            HINIC3_LOG(ERR, AGENT, HINIC3_UI_ERROR_INCOMPLETE_COMMAND);
            hinic3_command_reply_error(conn, HINIC3_UI_LEADING_SIGN_ERROR HINIC3_UI_ERROR_INCOMPLETE_COMMAND "!\n");
            return -EINVAL;
        }
    } else {
        HINIC3_LOG(ERR, AGENT, "command mgr can't find func.");
        hinic3_command_reply_error(conn, HINIC3_UI_LEADING_SIGN_ERROR HINIC3_UI_ERROR_UNRECOGNIZED_COMMAND "!\n");
        return -EINVAL;
    }

    return ret;
}

static int
parse_args(struct unixctl_conn *conn, char *command, int *argc_ptr, const char *argv[])
{
    char *cursor = command;
    char *str_end = cursor + strlen(command);
    const char *delimiters = " ";

    *argc_ptr = 0;

    cursor = command;
    while (strchr(delimiters, *cursor) != NULL) {
        cursor++;
    }

    if (cursor == str_end)
        return 0;

    argv[(*argc_ptr)++] = cursor;

    do {
        strsep(&cursor, delimiters);
        if (cursor == NULL)
            break;

        while (strchr(delimiters, *cursor) != NULL) {
            cursor++;
        }

        if (cursor == str_end)
            break;
        if ((*argc_ptr) == MAX_COMMAND_ARG_NUM) {
            HINIC3_LOG(ERR, AGENT, "Too many command parameters");
            hinic3_command_reply_error(conn, "Too many command parameters\n");
            return -1;
        }

        argv[(*argc_ptr)++] = cursor;

        if (strcmp(argv[1], "hwoff/exec-cmd") == 0)
            break;
    } while (cursor < str_end);

    return 0;
}

static int
hinic3_server_recv_command(FILE *file, char *start, char *end)
{
    long long time_start = hinic3_time_msec();

    char *cursor = start;
    while (cursor + 1 < end) {
        long long time_now = hinic3_time_msec();
        if (time_now - time_start > HINIC3_COMMAND_MAX_RECV_TIME) {
            HINIC3_LOG(INFO, AGENT, "command recv time out");
            return -1;
        }

        int ch = fgetc(file);
        if (ch == EOF) {
            if (errno == EAGAIN) {
                usleep(HINIC3_COMMAND_SLEEP_INTERVAL);
                continue;
            } else {
                return -1;
            }
        }

        if (ch == '\n')
            break;

        *cursor++ = ch;
    }

    *cursor = '\0';

    return 0;
}

static int
hinic3_set_socket_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
        return -1;

    flags |= O_NONBLOCK;

    int result = fcntl(fd, F_SETFL, flags);
    if (result < 0)
        return -1;

    return 0;
}

static struct hinic3_command_info g_hinic3_command_info[] = {
    {"hwoff/show-hmap-flow-num", "Queries the number of flow tables in the hash table."},
    {"hwoff/exec-cmd", "This command is used to issue or query OVS driver information for debugging."},
    {"hwoff/show-flow-api", "This API is used to query the hardware flow table."},
    {"hwoff/show-error-stats", "Indicates the error information recorded during flow table unloading."},
    {"hwoff/dump-hwoff-flows", "Queries hardware flow table information."},
    {"hwoff/flow-offload-speed-stat", "Collects statistics on or queries the flow table offloading rate."},
    {"hwoff/show-offload-flow-num",
        "This command is used to query the number of flow tables that are unloaded (software statistics)."},
    {"hwoff/dump-ports", "Queries the port status."},
    {"hwoff/flush-ports", "Clears port/chip statistics."},
    {"hwoff/dump-upcall-queue-info", "Queries upcall queue information."},
    {"hwoff/dump-bond-slave-info", "Displays information about the bond_slave port."},
    {"hwoff/show-function-flavor", "Queries the port list."},
    {"hwoff/show-function-stats", "Queries the VirtIO queue usage."},
    {"hwoff/dump-qos-loss", "Queries the number of packets discarded in the receive direction due to rate limitation."},
    {"hwoff/dump-port-qos-loss",
        "Queries the number of packets discarded due to rate limitation in the receive direction of a port."},
    {"hwoff/show-qos-speed", "Queries the actual rate in a specified rate limit gorup policy."},
    {"hwoff/show-port-qos-speed", "Queries the net rate limit information."},
    {"hwoff/dump-net-qos-loss",
        "Queries the number of packets discarded in the receive direction due to net rate limitation."},
    {"hwoff/show-flow-qos", "Queries the QoS id information of a flow."},
    {"hwoff/dump-flow-qos", "Queries the QoS information of a flow."},
    {"hwoff/dump-flow-qos-stats", "Queries the QoS statistics of a flow."},
    {"hwoff/dump-flow-qos-loss", "Queries the QoS loss information of a"},
    {"hwoff/dump-net-qos", "Queries the QoS information of a specified port."},
    {"hwoff/dump-qos-stats", "Queries the QoS Statistics of a specified group."},
    {"hwoff/dump-port-qos-stats", "Queries the QoS Statistics of a specified port."},
    {"hwoff/dump-net-qos-stats", "Queries the QoS Statistics of a specified net."},
    {"hwoff/show-meter", "Queries meter Information."},
    {"hwoff/show-policy", "Queries policy Information."},
    {"hwoff/show-profile", "Queries profile Information."},
    {"hwoff/show-port-qos", "Indicates the port rate limit type, which can be device rate limit or port rate limit."},
    {"hwoff/dump-qos", "Queries the group rate limit information."},
    {"hwoff/enable-capture-probe", "Enable the packet capture function."},
    {"hwoff/disable-capture-probe", "Disable the packet capture function."},
    {"hwoff/capture-probe", "Capturing Packets function."},
    {"list-commands", "Displays the list of all commands."},
    {"hwoff/show-global-api", "This API is used to query global hardware operations."},
    {"hwoff/show-mpool-stats", "Query the memory pool status."},
    {"hwoff/show-port-api", "This command is used to query API invoking statistics of hardware ports."},
    {"hwoff/show-emc-session", "This command is used to query session info of emc flows."},
    {"hwoff/flow-escape-mode", "Configuring the Escape Mode."},
    {"hwoff/packet-detect-mode", "Configuring the Packet Detect Mode."},

    {"hwoff/dump-trace", "Querying the Error Information Tracing of an Added Flow."},
    {"hwoff/trace-flow", "Configure error information tracing for a specified flow."},

    {"hwoff/show-thread-stats", "This command is used to obtain the thread health status."},
    {"hwoff/show-meminfo", "This command is used to show requested memory size."},
    {"hwoff/show-hugepage-meminfo", "This command is used to show hugepage memory size."},
    {"hwoff/show-sample-session", "This command is used to show sample session."},
    {"hwoff/set-log-level", "This command is used to set log level for module"},
    {"hwoff/show-log-list", "This command is used to show log list."},
    {"hwoff/dump-fuzzy-flows", "This command is used to dump fuzzy flows."},
    {"hwoff/log-limit-ctl", "This command is used to control log limit."},
    {"hwoff/del-flow-by-ufid", "This command is used to delete flow by ufid."},
    {"hwoff/query-offloaded-flow", "This command is used to set query info and query flow."},
    {"hwoff/set-forward-mode", "This command is used to set forward mode."},
    {"hwoff/show-forward-mode", "This command is used to get forward mode configuration."},
    {"hwoff/dump-flow-qos-loss", "This command is used to dump flow qos loss."},

    {"hwoff/flow-statistics-refresh-time-stat", "Collect statistics on or query the full refresh duration of flow table statistics."},
    {"hwoff/flow-offload-time-stat", "Collect statistics on or query the flow table unloading time."},
    {"hwoff/dump-context", "Query the dump handle status."},
    {"hwoff-agent-version", "Query the dpak-libovs version."},
    {"hwoff/add-protolist", "This command is used to add a protocol number value to the block/pass protocol list."},
    {"hwoff/add-rapid-proto", "This command is used to a protocol number value to the rapid protocol list."},
    {"hwoff/del-protolist", "Deletes the specified protocol number value or values from the block/pass list protocol list."},
    {"hwoff/del-rapid-proto", "Delete the rapid protocol from rapid protocol list."},
    {"hwoff/dump-policy-info", "Query the current offload policy information."},
    {"hwoff/dump-protolist", "Queries the current protocol type and lists the protocol types that have been added to the protocol list."},
    {"hwoff/dump-rapid-proto-info", "Queries rapid offload protocol information."},
    {"hwoff/flush-protolist", "Clear the block/pass list protocol list."},
    {"hwoff/flush-rapid-proto-info", "Clear rapid offload protocol information."},
    {"hwoff/set-offload-switch", "Enables or disables flow table offloading.."},
    {"hwoff/set-policy-info", "Configure offload policy."},
    {"hwoff/set-protolist-mode", "Configure the block/pass list mode."},
    {"hwoff/show-port-security-filter", "Queries the security filtering settings of a specified port."},
    {"hwoff/show-security-eth-type-info", "Query all eth_type_group information that has been set."},
    {"hwoff/show-security-src-mac-info", "Queries all the src-mac information that has been set."},
    {"hwoff/dump-ufid-map-hw", "Query the UFID of the software flow table associated with the hardware flow table."},
    {"hwoff/dump-ufid-map-sw", "Query the UFID of the hardware flow table associated with the software flow table."},
    {"hwoff/ufid-map-error-stats", "Processing of the error in querying the software and hardware mapping table."},
    {"hwoff/dump-dp-hash-flows", "This command is used to dump dp-hash flow."},
    {"hwoff/dump-meter", "This command is used to dump meter."},
};

static int
get_cmd_info(const char *func_name, char *cmd_info)
{
    for (size_t i = 0; i < sizeof(g_hinic3_command_info) / sizeof(struct hinic3_command_info); i++) {
        if (strncmp(func_name, g_hinic3_command_info[i].func_name, strlen(g_hinic3_command_info[i].func_name)) == 0) {
            memcpy(cmd_info, g_hinic3_command_info[i].info, HINIC3_CMD_INFO_MAX_LEN);
            return 0;
        }
    }
    return -1;
}

static void
hinic3_set_cmd_info_to_log(const char *usr_name, const char *func_name, int result)
{
    int ret;
    char *cmd_info = (char *)hinic3_calloc(1, HINIC3_CMD_INFO_MAX_LEN, HINIC3_COMMAND);
    if (cmd_info == NULL) {
        HINIC3_LOG(ERR, AGENT, "Calloc cmd info mem failed.");
        return;
    }
    ret = get_cmd_info(func_name, cmd_info);
    if (ret != 0) {
        HINIC3_LOG(WARNING, AGENT, "The command is not registered.");
        hinic3_free(cmd_info);
        return;
    }
    if (result != 0) {
        HINIC3_LOG(INFO, AGENT, "[%s@localhost]%s:%s - Execution fail", usr_name, func_name, cmd_info);
    } else {
        HINIC3_LOG(INFO, AGENT, "[%s@localhost]%s:%s - Execution success", usr_name, func_name, cmd_info);
    }
    hinic3_free(cmd_info);
}

static int
proc_conn(int data_socket)
{
    int ret = hinic3_set_socket_nonblocking(data_socket);
    if (ret != 0) {
        close(data_socket);
        HINIC3_LOG(ERR, AGENT, "set nonblocking fail");
        return ret;
    }

    struct unixctl_conn conn = {0};
    conn.sock_fd = data_socket;

    conn.sock_file = fdopen(conn.sock_fd, "a+");
    if (conn.sock_file == NULL) {
        close(conn.sock_fd);
        return -1;
    }

    char command[HINIC3_COMMAND_MAX_LEN];

    ret = hinic3_server_recv_command(conn.sock_file, command, command + sizeof(command));
    if (ret != 0) {
        fclose(conn.sock_file);
        return -1;
    }

    const char *argv[MAX_COMMAND_ARG_NUM];
    int argc = 0;
    if (parse_args(&conn, command, &argc, argv) != 0) {
        fclose(conn.sock_file);
        return -1;
    }

    ret = dispatch_command(&conn, argc - 1, argv + 1); // argv参数的第一个为uid
    (void)hinic3_set_cmd_info_to_log(argv[0], argv[1], ret);

    (void)fclose(conn.sock_file);
    return 0;
}

static void
hinic3_command_epoll_process(int fd)
{
    int listen_socket = g_command_mgr.listen_socket;
    if (fd == listen_socket) {
        int data_socket = accept(listen_socket, NULL, NULL);
        if (data_socket == -1)
            return;
        proc_conn(data_socket);
    }
    return;
}

static void *
hinic3_command_thread(void *arg HINIC3_UNUSED)
{
    int listen_socket = g_command_mgr.listen_socket;
    int epoll_fd = g_command_mgr.epoll_fd;
    int i = 0;
    HINIC3_LOG(INFO, AGENT, "command mgr start listen");

    struct epoll_event event_list[EVENT_MAX_COUNT];
    memset(&event_list, 0, sizeof(event_list));

    enum check_thread_item_type check_thread = LISTEN_THREAD;
    long long start = hinic3_time_msec();
    while (g_command_mgr.thread_exit == HINIC3_THREAD_NORMAL_STATUS) {
        start = hinic3_thread_signal_increase(start, check_thread, HINIC3_LISTEN_THREAD_SIGNAL_INCREASE_INTER);
        int nfds = epoll_wait(epoll_fd, event_list, EVENT_MAX_COUNT, HINIC3_LISTEN_THREAD_TIMEOUT);
        if (nfds <= 0)
            continue;
        for (i = 0; i < nfds; ++i) {
            hinic3_command_epoll_process(event_list[i].data.fd);
        }
    }

    close(epoll_fd);
    close(listen_socket);

    return 0;
}

static int
hinic3_command_pre_init(int *listen_socket)
{
    int ret;
    int pre_socket;
    const char *sock_path = SOCKET_PATH;
    unlink(sock_path);

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, sock_path);

    pre_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (pre_socket == -1) {
        HINIC3_LOG(ERR, AGENT, "create socket fail");
        return -1;
    }

    ret = bind(pre_socket, (const struct sockaddr *)&addr, sizeof(addr));
    if (ret == -1) {
        HINIC3_LOG(ERR, AGENT, "bind socket fail");
        close(pre_socket);
        return -1;
    }

    ret = listen(pre_socket, 10); // 10: 最大等待链接的客户端个数
    if (ret == -1) {
        HINIC3_LOG(ERR, AGENT, "listen socket fail");
        close(pre_socket);
        return -1;
    }

    *listen_socket = pre_socket;
    return 0;
}

static int
hinic3_command_init(void)
{
    int ret;
    int listen_socket;
    struct epoll_event event;

    ret = hinic3_command_pre_init(&listen_socket);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "hinic3 command pre init fail");
        return -1;
    }

    int epoll_fd = epoll_create(CMD_EPOLL_SIZE);
    if (epoll_fd < 0) {
        HINIC3_LOG(ERR, AGENT, "epoll create fail");
        goto err;
    }

    memset(&event, 0, sizeof(event));
    event.data.fd = listen_socket;
    event.events = EPOLLIN;
    ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_socket, &event);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "epoll_ctl add fail");
        goto err;
    }
    g_command_mgr.listen_socket = listen_socket;
    g_command_mgr.epoll_fd = epoll_fd;
    ret = hinic3_thread_create(&g_command_mgr.thread, "hinic3_listen_thread", hinic3_command_thread, NULL, HINIC3_COMMAND);
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "listen thread create fail");
        goto err;
    }

    hinic3_set_ctrl_thread_cpu_affinity(&g_command_mgr.thread);
    return 0;

err:
    close(epoll_fd);
    close(listen_socket);
    return -1;
}

void
hinic3_command_hmap_init(void)
{
    hinic3_shash_init(&g_command_mgr.command_map);
}

static int
hinic3_change_group(void)
{
    int ret;
    gid_t group_id;
    struct group *grp;

    grp = getgrnam(HINIC3_OVS_GROUP_NAME);
    if (grp == NULL) {
        HINIC3_LOG(ERR, AGENT, "getgrnam fail");
        return -1;
    }
    group_id = grp->gr_gid;

    ret = chown(SOCKET_PATH, -1, group_id);
    if (ret == -1) {
        HINIC3_LOG(ERR, AGENT, "chown fail");
        return -1;
    }

    ret = chmod(SOCKET_PATH, HINIC3_SOCKET_RIGHT);
    if (ret == -1) {
        HINIC3_LOG(ERR, AGENT, "chmod fail");
        return -1;
    }
    return 0;
}


int
hinic3_command_mgr_init(void)
{
    int ret;

    hinic3_command_register("list-commands", "", 0, 0, hinic3_list_commands, NULL);

    ret = hinic3_command_init();
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "command mgr init fail");
        return ret;
    }

    if (hinic3_user_scenario_get() != COM_BD) {
        ret = hinic3_change_group();
        if (ret != 0) {
            HINIC3_LOG(ERR, AGENT, "change group fail");
            return ret;
        }
    }
    
    HINIC3_LOG(INFO, AGENT, "command mgr init success");
    return 0;
}

void
hinic3_command_mgr_uninit(void)
{
    hinic3_shash_destroy_free_data(&g_command_mgr.command_map);
    g_command_mgr.thread_exit = HINIC3_THREAD_EXIT_STATUS;
}
