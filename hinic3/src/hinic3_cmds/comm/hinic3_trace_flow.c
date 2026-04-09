/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2022 Huawei Technologies Co., Ltd
 */

#include "hinic3_trace_flow.h"
#include "hinic3_util.h"
#include "hinic3_eth_packets.h"
#include "hinic3_log.h"
#include "hinic3_iface_flow.h"
#include "hinic3_flow_agent.h"
#include "hinic3_agent_cmd_format.h"
#include "hinic3_ui_string.h"
#include "hinic3_agent_flow_cmd.h"
#include "hinic3_command.h"
#include "hinic3_ds.h"
#include "hinic3_eth_packets.h"
#include "hinic3_parse_agent_config.h"

#define HINIC3_TRACE_FLOW_FILTER_EXIST (-1)
#define HINIC3_TRACE_FLOW_HAS_DIFF_FILTER (-2)
#define HINIC3_TRACE_FLOW_FILTER_OVERFLOW (-3)

#define TRACE_FLOW_MASK_8 0xFF
#define TRACE_FLOW_MASK_16 0xFFFF
#define TRACE_FLOW_MASK_32 0xFFFFFFFF

#define STRTOUL_NUM 10
#define TRACE_FLOW_PARA_MAX 11

static const char *g_hinic3_trace_flow_status_string[] = {
    "hinic3_flow_pkt_key_resolve_done",
    "hinic3_flow_agent_error_input_not_hiovs",
    "hinic3_flow_construct_key_done",
    "hinic3_flow_agent_error_trans_key",
    "hinic3_flow_offload_key_process_done",
    "hinic3_flow_offload_key_hash_done",
    "hinic3_flow_offload_hash_table_insert_done",
    "hinic3_flow_offload_hash_table_insert_error",
    "hinic3_flow_copy_recircle_act_done",
    "hinic3_flow_process_offload_action_done",
    "hinic3_flow_process_offload_action_error",
    "hinic3_backup_ct_state_done",
    "hinic3_flow_agent_error_process_recirc",
    "hinic3_process_offload_key_act_done",
    "hinic3_flow_agent_error_prepare_offload_args",
    "hinic3_process_offload_args_done",
    "hinic3_flow_agent_error_offload_limits",
    "hinic3_flow_agent_error_no_conn_table",
    "hinic3_flow_agent_stats_offload_delay",
    "hinic3_flow_agent_error_duplicate_offload",
    "hinic3_agent_is_flow_ready_put_done",
    "hinic3_flow_agent_error_ct_check_fail",
    "hinic3_flow_agent_error_ct_not_ready",
    "hinic3_agent_is_ct_ready_to_offload_done",
    "hinic3_flow_agent_error_hardware_fail",
    "hinic3_agent_flow_put_done",
};

static void
hinic3_trace_flow_current_key_update(struct hinic3_filter_tuple *cur_key, const struct hinic3_conntrack_key *key)
{
    const struct hinic3_key_5tuple *tuple = (const struct hinic3_key_5tuple *)key->key;

    cur_key->proto = key->meta.tcp_udp_flag;
    cur_key->src_ip = tuple->ip.src;
    cur_key->dst_ip = tuple->ip.dst;
    cur_key->port_src = tuple->port_src;
    cur_key->port_dst = tuple->port_dst;
}

void
hinic3_trace_flow_init(struct hinic3_trace_flow *trace_flow_info)
{
    memset(trace_flow_info->filter, 0,
        sizeof(struct hinic3_trace_filter) * HINIC3_MAX_TRACE_FLOW_NUM);
    trace_flow_info->count = 0;
}

static void
hinic3_trace_flow_clean(struct hinic3_trace_flow *trace_flow_info)
{
    hinic3_trace_flow_init(trace_flow_info);
}

static inline int
hinic3_get_trace_flow_count(struct hinic3_trace_flow *trace_table)
{
    return trace_table->count;
}

static struct hinic3_trace_filter *
hinic3_trace_flow_get_filter(struct hinic3_trace_flow *trace_table, int i)
{
    if (hinic3_get_trace_flow_count(trace_table) <= i)
        return NULL;

    return &trace_table->filter[i];
}

static bool
hinic3_trace_flow_filter_cmp(struct hinic3_filter_tuple *src, struct hinic3_filter_tuple *mask,
    struct hinic3_filter_tuple *dst)
{
    uint8_t *src_ptr = (uint8_t *)src;
    uint8_t *mask_ptr = (uint8_t *)mask;
    uint8_t *dst_ptr = (uint8_t *)dst;
    size_t tuple_size = sizeof(struct hinic3_filter_tuple);

    for (size_t i = 0; i < tuple_size; i++) {
        if ((src_ptr[i] & mask_ptr[i]) != dst_ptr[i])
            return false;
    }

    return true;
}

void
hinic3_trace_flow_info_update(int status, const struct hinic3_conntrack_key *key)
{
    int cnt;
    struct hinic3_trace_flow *trace_table_info = NULL;
    struct hinic3_trace_filter *filter = NULL;
    struct hinic3_flow_agent_db *hw_offload = NULL;
    struct hinic3_filter_tuple cur_key = {0};
    struct hinic3_dp_extend_info *extend_info = hinic3_get_offload_extend_info();

    if (extend_info == NULL || extend_info->hw_offload == NULL || key == NULL)
        return;

    hinic3_trace_flow_current_key_update(&cur_key, key);

    hw_offload = extend_info->hw_offload;
    trace_table_info = &hw_offload->trace_flow_info;
    cnt = trace_table_info->count;

    for (int i = 0; i < cnt; i++) {
        filter = hinic3_trace_flow_get_filter(trace_table_info, i);
        if (HINIC3_UNLIKELY(filter == NULL))
            continue;

        if (hinic3_trace_flow_filter_cmp(&cur_key, &filter->mask, &filter->key))
            filter->status[status]++;
    }
}

static int
hinic3_trace_flow_has_same_filter(struct hinic3_trace_flow *trace_table, struct hinic3_filter_tuple *key)
{
    struct hinic3_trace_filter *filter = NULL;

    for (int i = 0; i < trace_table->count; i++) {
        filter = hinic3_trace_flow_get_filter(trace_table, i);
        if (HINIC3_UNLIKELY(filter == NULL))
            continue;

        if (hinic3_trace_flow_filter_cmp(key, &filter->mask, &filter->key))
            return HINIC3_TRACE_FLOW_FILTER_EXIST;
    }

    return HINIC3_TRACE_FLOW_HAS_DIFF_FILTER;
}

static inline void
hinic3_set_ip_mask(struct hinic3_ip_addr *ip_mask, struct hinic3_ip_addr *ip_addr)
{
    uint32_t *mask = (uint32_t *)&ip_mask->ipv6;
    uint32_t *key = (uint32_t *)&ip_addr->ipv6;
    size_t loop_size = sizeof(struct in6_addr) / sizeof(uint32_t);

    for (size_t i = 0; i < loop_size; i++) {
        if (key[i] != 0) {
            mask[i] = TRACE_FLOW_MASK_32;
            continue;
        }
        mask[i] = 0;
    }
}

static void
hinic3_trace_flow_set_filter(struct hinic3_trace_filter *filter, struct hinic3_filter_tuple *key)
{
    memset(filter, 0, sizeof(struct hinic3_trace_filter));

    filter->key = *key;

    filter->mask.proto = TRACE_FLOW_MASK_8; /* current only support tcp udp */
    hinic3_set_ip_mask(&filter->mask.src_ip, &key->src_ip);
    hinic3_set_ip_mask(&filter->mask.dst_ip, &key->dst_ip);
    filter->mask.port_src = (key->port_src != 0) ? TRACE_FLOW_MASK_16 : 0;
    filter->mask.port_dst = (key->port_dst != 0) ? TRACE_FLOW_MASK_16 : 0;
}

static int
hinic3_add_trace_flow_filter(struct hinic3_trace_flow *trace_table, struct hinic3_filter_tuple *key)
{
    int ret;
    int cnt;

    if (trace_table->count >= HINIC3_MAX_TRACE_FLOW_NUM)
        return HINIC3_TRACE_FLOW_FILTER_OVERFLOW;

    ret = hinic3_trace_flow_has_same_filter(trace_table, key);
    if (ret == HINIC3_TRACE_FLOW_FILTER_EXIST)
        return ret;

    cnt = trace_table->count;
    hinic3_trace_flow_set_filter(&trace_table->filter[cnt], key);
    trace_table->count++;

    return trace_table->count;
}

static int
hinic3_trace_flow_cmd_para_check(int argc HINIC3_UNUSED, const char *argv[], struct hinic3_filter_tuple *key,
    struct ds *reply)
{
    char *endptr = NULL;

    int work_argc = 1;
    do {
        if (strncmp("-proto", argv[work_argc], sizeof("-proto")) == 0) {
            work_argc += 1;
            if (strncmp("tcp", argv[work_argc], sizeof("tcp")) == 0) {
                key->proto = HINIC3_CT_TCP_FLAG;
            } else if (strncmp("udp", argv[work_argc], sizeof("udp")) == 0) {
                key->proto = HINIC3_CT_UDP_FLAG;
            } else {
                hinic3_ds_put_format(reply, "%sWrong parameter, only support tcp or udp\n", HINIC3_UI_LEADING_SIGN_ERROR);
                return -1;
            }
        } else if (strncmp("-sip", argv[work_argc], sizeof("-sip")) == 0) {
            work_argc += 1;
            if (inet_pton(AF_INET, argv[work_argc], &(key->src_ip.ipv4)) <= 0) {
                hinic3_ds_put_format(reply, "%sWrong parameter, invalid source ip\n", HINIC3_UI_LEADING_SIGN_ERROR);
                return -1;
            }
        } else if (strncmp("-dip", argv[work_argc], sizeof("-dip")) == 0) {
            work_argc += 1;
            if (inet_pton(AF_INET, argv[work_argc], &(key->dst_ip.ipv4)) <= 0) {
                hinic3_ds_put_format(reply, "%sWrong parameter, invalid destination ip\n", HINIC3_UI_LEADING_SIGN_ERROR);
                return -1;
            }
        } else if (strncmp("-sport", argv[work_argc], sizeof("-sport")) == 0) {
            work_argc += 1;
            if (strtoul(argv[work_argc], &endptr, STRTOUL_NUM) > UINT16_MAX || !endptr || (*endptr != '\0')) {
                hinic3_ds_put_format(reply, "%sWrong parameter, invalid source port number\n", HINIC3_UI_LEADING_SIGN_ERROR);
                return -1;
            }
            key->port_src = strtoul(argv[work_argc], &endptr, STRTOUL_NUM);
            key->port_src = htons(key->port_src);
        } else if (strncmp("-dport", argv[work_argc], sizeof("-dport")) == 0) {
            work_argc += 1;
            if (strtoul(argv[work_argc], &endptr, STRTOUL_NUM) > UINT16_MAX || !endptr || (*endptr != '\0')) {
                hinic3_ds_put_format(reply, "%sWrong parameter, invalid destination port number\n", HINIC3_UI_LEADING_SIGN_ERROR);
                return -1;
            }
            key->port_dst = strtoul(argv[work_argc], &endptr, STRTOUL_NUM);
            key->port_dst = htons(key->port_dst);
        } else {
            hinic3_ds_put_format(reply, "Error: %s, input -h to get help info.\n", HINIC3_UI_ERROR_WRONG_PARAMETER);
            return -1;
        }
        work_argc += 1;
    } while (work_argc < TRACE_FLOW_PARA_MAX);

    return 0;
}

static void
hinic3_agent_trace_flow_cmd_sub(struct unixctl_conn *conn, int argc, const char *argv[], void *aux HINIC3_UNUSED)
{
    struct ds reply = DS_EMPTY_INITIALIZER;
    struct hinic3_filter_tuple key = {0};
    struct hinic3_flow_agent_db *hw_offload = NULL;
    struct hinic3_dp_extend_info *extend_info = hinic3_get_offload_extend_info();
    int ret = 0;

    if (extend_info == NULL || extend_info->hw_offload == NULL) {
        hinic3_ds_put_format(&reply, "%sdatapath info not exist\n", HINIC3_UI_LEADING_SIGN_INFO);
        goto exit;
    }
    hw_offload = extend_info->hw_offload;

    if (argc != TRACE_FLOW_PARA_MAX) {
        hinic3_ds_put_format(&reply, HINIC3_UI_LEADING_SIGN_ERROR HINIC3_UI_FLOW_REQUIRES_WRONG_ARGUMENT_STRING,
            TRACE_FLOW_PARA_MAX - 1);
        goto exit;
    }

    ret = hinic3_trace_flow_cmd_para_check(argc, argv, &key, &reply);
    if (ret != 0)
        goto exit;

    ret = hinic3_add_trace_flow_filter(&hw_offload->trace_flow_info, &key);
    if (ret == HINIC3_TRACE_FLOW_FILTER_OVERFLOW) {
        HINIC3_LOG(INFO, AGENT, "trace flow table is full.");
        hinic3_ds_put_format(&reply, "%strace flow table is full.\n", HINIC3_UI_LEADING_SIGN_ERROR);
    } else if (ret == HINIC3_TRACE_FLOW_FILTER_EXIST) {
        HINIC3_LOG(INFO, AGENT, "trace flow filter already exist.");
        hinic3_ds_put_format(&reply, "%strace flow filter already exist.\n", HINIC3_UI_LEADING_SIGN_ERROR);
    } else {
        HINIC3_LOG(INFO, AGENT, "Add trace flow filter DONE.");
        hinic3_ds_put_format(&reply, "%sAdd trace flow filter DONE.\n", HINIC3_UI_LEADING_SIGN_INFO);
    }

exit:
    hinic3_command_reply(conn, hinic3_ds_cstr(&reply));
    hinic3_ds_destroy(&reply);
}

static void hinic3_agent_trace_flow_cmd_help(struct unixctl_conn *conn)
{
    struct ds ds = DS_EMPTY_INITIALIZER;
    hinic3_ds_put_format(&ds, "%2s%s%s\n\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_CMD_USAGE_STRING, HINIC3_UI_TRACE_FLOW_TIPS_STRING);
    hinic3_ds_put_format(&ds, "%2s%s\n", HINIC3_UI_INDENT_SPACE, HINIC3_UI_OPTION_LIST_STRING);
    hinic3_ds_put_format(&ds, "%4s%-30s%-s\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_TRACE_FLOW_PROTO_STR, HINIC3_UI_TRACE_FLOW_PROTO_HELP_STR);
    hinic3_ds_put_format(&ds, "%4s%-30s%-s\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_TRACE_FLOW_SIP_STR, HINIC3_UI_TRACE_FLOW_SIP_HELP_STR);
    hinic3_ds_put_format(&ds, "%4s%-30s%-s\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_TRACE_FLOW_DIP_STR, HINIC3_UI_TRACE_FLOW_DIP_HELP_STR);
    hinic3_ds_put_format(&ds, "%4s%-30s%-s\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_TRACE_FLOW_SPORT_STR, HINIC3_UI_TRACE_FLOW_SPORT_HELP_STR);
    hinic3_ds_put_format(&ds, "%4s%-30s%-s\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_TRACE_FLOW_DPORT_STR, HINIC3_UI_TRACE_FLOW_DPORT_HELP_STR);
    hinic3_ds_put_format(&ds, "%4s%-30s%-s\n", HINIC3_UI_INDENT_SPACE,
        HINIC3_UI_ESCAPE_MODE_SHOW_HELP_STRING, HINIC3_UI_ESCAPE_MODE_SHOW_HELP_TIPS_STRING);
    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
}

static void hinic3_agent_trace_flow_cmd(struct unixctl_conn *conn, int argc, const char *argv[], void *aux HINIC3_UNUSED)
{
    enum { HELP_ARGC = 2 };

    if ((argc == HELP_ARGC) && ((strcmp(argv[1], "-h") == 0) || (strcmp(argv[1], "--help") == 0))) {
        hinic3_agent_trace_flow_cmd_help(conn);
    } else {
        hinic3_agent_trace_flow_cmd_sub(conn, argc, argv, aux);
    }
}

static void
hinic3_dump_trace_get_info(uint64_t *status, int i, struct hinic3_filter_tuple *key,
    struct hinic3_trace_flow *trace_table)
{
    for (int j = 0; j < HINIC3_FLOW_TRACE_STATUS_MAX; j++) {
        status[j] = trace_table->filter[i].status[j];
    }
    *key = trace_table->filter[i].key;
}

static void
hinic3_dump_filter_info(struct hinic3_trace_flow *trace_flow, struct ds *reply)
{
    int cnt;
    struct hinic3_filter_tuple key = {0};
    uint64_t status[HINIC3_FLOW_TRACE_STATUS_MAX] = {0};

    cnt = hinic3_get_trace_flow_count(trace_flow);
    hinic3_ds_put_format(reply, "  Total Trace Filter: %d\n", cnt);
    for (int i = 0; i < cnt; i++) {
        hinic3_ds_put_format(reply, "    Filter %d: ", i + 1);
        hinic3_dump_trace_get_info(status, i, &key, trace_flow);
        hinic3_ds_put_format(reply, "%s ", key.proto == HINIC3_CT_TCP_FLAG ? "TCP" : "UDP");
        hinic3_ds_put_format(reply, "" IP_FMT " ", IP_ARGS(key.src_ip.ipv4));
        hinic3_ds_put_format(reply, "" IP_FMT " ", IP_ARGS(key.dst_ip.ipv4));
        hinic3_ds_put_format(reply, "%u ", ntohs(key.port_src));
        hinic3_ds_put_format(reply, "%u\n", ntohs(key.port_dst));

        for (int j = 0; j < HINIC3_FLOW_TRACE_STATUS_MAX; j++) {
            if (status[j] != 0) {
                hinic3_ds_put_format(reply, "%-50s: %" PRIu64 "\n", g_hinic3_trace_flow_status_string[j], status[j]);
            }
        }
    }
}

static void
hinic3_agent_dump_trace_cmd(struct unixctl_conn *conn, int argc, const char *argv[], void *aux HINIC3_UNUSED)
{
    struct ds reply = DS_EMPTY_INITIALIZER;
    struct hinic3_trace_flow *trace_flow = NULL;
    struct hinic3_flow_agent_db *hw_offload = NULL;
    struct hinic3_dp_extend_info *extend_info = hinic3_get_offload_extend_info();

    if (extend_info == NULL || extend_info->hw_offload == NULL) {
        hinic3_ds_put_format(&reply, "%sDatapath info not exist.\n", HINIC3_UI_LEADING_SIGN_INFO);
        goto exit;
    }

    hw_offload = extend_info->hw_offload;
    trace_flow = &hw_offload->trace_flow_info;

    if (argc == 2) {
        if (strncmp("-f", argv[1], sizeof("-f")) == 0) {
            hinic3_trace_flow_clean(trace_flow);
            hinic3_ds_put_format(&reply, "%sTrace flow clean.\n", HINIC3_UI_LEADING_SIGN_INFO);
        } else if ((strncmp("-h", argv[1], sizeof("-h")) == 0) || strncmp("--help", argv[1], sizeof("--help")) == 0) {
            hinic3_ds_put_format(&reply, "%2sUsage: dpak-ovs-ctl hinic3/dump-trace [ -f | { -h | --help } ]\n\n",
                HINIC3_UI_INDENT_SPACE);
            hinic3_ds_put_format(&reply, "%2sOptions list:\n", HINIC3_UI_INDENT_SPACE);
            hinic3_ds_put_format(&reply, "%4s-f           Clear all trace filters\n", HINIC3_UI_INDENT_SPACE);
            hinic3_ds_put_format(&reply, "%4s-h, --help   Display the help information\n", HINIC3_UI_INDENT_SPACE);
        } else {
            hinic3_ds_put_format(&reply, "%s%s, please input -h or --help to get help info.\n",
                HINIC3_UI_LEADING_SIGN_ERROR, HINIC3_UI_ERROR_WRONG_PARAMETER);
        }
        goto exit;
    }

    hinic3_dump_filter_info(trace_flow, &reply);

exit:
    hinic3_command_reply(conn, hinic3_ds_cstr(&reply));
    hinic3_ds_destroy(&reply);
}

void
unixctl_hinic3_trace_flow_init(void)
{
    hinic3_command_register("hwoff/trace-flow",
        "{ -proto <tcp,udp> -sip ENUM<IP<X.X.X.X>,IPV6<X:X::X:X>> -dip "
         "ENUM<IP<X.X.X.X>,IPV6<X:X::X:X>> -sport INTEGER<0-65535> -dport INTEGER<0-65535> | { -h | --help } }",
         1, TRACE_FLOW_PARA_MAX - 1, hinic3_agent_trace_flow_cmd, NULL);
    hinic3_command_register("hwoff/dump-trace", "[ -f | { -h | --help } ]", 0, 1, hinic3_agent_dump_trace_cmd, NULL);
}
