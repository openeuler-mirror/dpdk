/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_CAPTURE_COMMAND_H
#define HINIC3_CAPTURE_COMMAND_H
void pcap_cmd_exec(struct unixctl_conn *conn, int argc, const char *argv[], void *aux HINIC3_UNUSED);
void pcap_cmd_help(struct unixctl_conn *conn, int argc, const char *argv[], void *aux HINIC3_UNUSED);
void pcap_cmd_show(struct unixctl_conn *conn, int argc, const char *argv[], void *aux HINIC3_UNUSED);
void pcap_cmd_start(struct unixctl_conn *conn, int argc, const char *argv[], void *aux HINIC3_UNUSED);
void pcap_cmd_stop(struct unixctl_conn *conn, int argc, const char *argv[], void *aux HINIC3_UNUSED);
void pcap_unix_cmd_register(void);
#endif /* HINIC3_CAPTURE_COMMAND_H */
