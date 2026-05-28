/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */

#include <errno.h>
#include <unistd.h>
#include <sys/eventfd.h>
#include <sys/epoll.h>
#include <sched.h>
#include <time.h>
#include "rte_cycles.h"
#include "rte_lcore.h"
#include "rte_spinlock.h"
#include "rte_ethdev.h"

#include "hinic3_ds.h"
#include "hinic3_log.h"
#include "hinic3_init.h"
#include "hinic3_iface_port.h"
#include "hinic3_capture_core.h"
#include "hinic3_capture_filter.h"
#include "hinic3_nlattr.h"
#include "hinic3_util.h"
#include "hinic3_option.h"
#include "hinic3_eth_util.h"
#include "hinic3_command.h"
#include "hinic3_cmd_exec.h"
#include "hinic3_timeval.h"
#include "hinic3_ui_string.h"
#include "hinic3_capture_command.h"

#define PCAP_CMD_DESC                                                                                                \
"  Usage: dpak-ovs-ctl hwoff/capture-probe { start <portname> -w <filename> -t INTEGER<1-1440>\n"                    \
"                                          [ [ [ -sip ENUM<IP<X.X.X.X>,IPV6<X:X::X:X>> | \n"                         \
"                                          -dip ENUM<IP<X.X.X.X>,IPV6<X:X::X:X>> ] * | \n"                           \
"                                          -host ENUM<IP<X.X.X.X>,IPV6<X:X::X:X>> ] | \n"                            \
"                                          -thread | -smac MAC<H-H-H> | -dmac MAC<H-H-H> | \n"                       \
"                                          -eth_type <eth-type-hex> | -ip_proto ENUM<TCP,UDP,ICMP,SCTP,ICMPV6> |\n"  \
"                                          -vxlan_inner | -vlan INTEGER<0-4095> | -sport INTEGER<0-65535> |\n"       \
"                                          -dport INTEGER<0-65535> | -vxlan_vni INTEGER<0-16777215> |\n"             \
"                                          -P ENUM<in,out,intout> | -c INTEGER<1-1000000> ] * |\n"                   \
"                                          stop { <portname> | -pcap_id <id> } |\n"                                  \
"                                          show { all | -pcap_id <id> } |\n"                                         \
"                                          { -h | --help } }\n\n"                                                    \
"  Options list:                                                                                            \n"  \
"    start                                           Start the packet capture task on a specified port\n"        \
"      -w                                            Name of the packet capture file\n"                          \
"      -t                                            Set time for the capture task, in minute, "                 \
"The value is an integer with a range <1-1440>\n"                                                                \
"      -sip                                          Source IP address\n"                                        \
"      -dip                                          Destination IP address\n"                                   \
"      -host                                         Host IP address\n"                                          \
"      -smac                                         Source MAC address\n"                                       \
"      -dmac                                         Destination MAC address\n"                                  \
"      -eth_type                                     Data type, in hexadecimal format, such as 0x0800, 0x86dd, " \
"0x8600\n"                                                                                                       \
"      -ip_proto                                     Layer 4 protocol type, ENUM<TCP,UDP,ICMP,ICMPv6,SCTP>\n"    \
"      -vxlan_inner                                  If VXLAN packets are captured, apply to the inner packet, " \
"Both outer and inner headers are captured\n"                                                                    \
"      -vlan                                         VLAN ID, an integer with a range <0-4095>\n"                \
"      -sport                                        Source port\n"                                              \
"      -dport                                        Destination port\n"                                         \
"      -vxlan_vni                                    VXLAN ID, an integer with a range <0-16777215>\n"           \
"      -P                                            Packet capture direction, ENUM<in,out,inout>\n"             \
"      -c                                            Number of captured packets, an integer with a "             \
"range <1-1000000>. The default value is 8000 \n"                                                                \
"      -thread                                       Write files without the PMD thread\n"                       \
"    stop                                            Stop the packet capture task of a specified port or id\n"   \
"      -pcap_id                                      Stop the packet capture task of a specified id. The value " \
"is an integer with a range <1-4294967295>\n"                                                                    \
"    show                                            Display a packet capture task\n"                            \
"      all                                           Display all packet capture tasks\n"                         \
"      -pcap_id                                      Display the packet capture task of a specified id. "        \
"The value is an integer with a range <1-4294967295>\n"                                                          \
    "    -h, --help                                      Display the help information\n"

#define PCAP_CMD_ENBLE_DESC                                                                                 \
"  Usage: dpak-ovs-ctl hwoff/enable-capture-probe [ -c ENUM<high,low> | -q | { -h | --help } ]\n\n"         \
"  Options list:                                                                                  \n"       \
"    -c                            Set capture thread CPU usage level, ENUM<high,low>, the default value is high\n"                    \
"    -q                            Query current capture enable status and CPU usage\n"                     \
"    -h, --help                    Display the help information\n"

#define PCAP_CMD_ENBLE_DESC_FULLY_CAP                                                                       \
"  Usage: dpak-ovs-ctl hwoff/enable-capture-probe "                                                         \
"[ [ -p ENUM#<limited-capture,fully-capture> | -c ENUM#<high,low> ] * | -q | { -h | --help } ]\n\n"         \
"  Options list:                                                                                  \n"       \
"    -p                            Set packet capture mode, the default value is limited-capture\n"         \
"    -c                            Set capture thread CPU usage level, ENUM<high,low>, the default value is high\n"                    \
"    -q                            Query current capture enable status and CPU usage\n"                     \
"    -h, --help                    Display the help information\n"

static struct pcap_cmd_t g_cap_main_command = { "hwoff/capture-probe",
                                                "{ start <portname> -w <filename> -t INTEGER<1-1440>"
                                                "[ [ [ -sip ENUM<IP<X.X.X.X>,IPV6<X:X::X:X>> | "
                                                "-dip ENUM<IP<X.X.X.X>,IPV6<X:X::X:X>> ] * | "
                                                "-host ENUM<IP<X.X.X.X>,IPV6<X:X::X:X>> ] | "
                                                "-thread | -smac MAC<H-H-H> | -dmac MAC<H-H-H> | "
                                                "-eth_type <eth-type-hex> | "
                                                "-ip_proto ENUM<TCP,UDP,ICMP,SCTP,ICMPV6> | "
                                                "-vxlan_inner | -vlan INTEGER<0-4095> | "
                                                "-sport INTEGER<0-65535> | -dport INTEGER<0-65535> | "
                                                "-vxlan_vni INTEGER<0-16777215> | -P ENUM<in,out,intout> | "
                                                "-c INTEGER<1-1000000> ] * | "
                                                "stop { <portname> | -pcap_id <id> } | "
                                                "show { all | -pcap_id <id> } | "
                                                "{ -h | --help } }",
                                                pcap_cmd_exec,
                                                PCAP_CMD_MIN_PARAM,
                                                PCAP_CMD_MAX_PARAM,
                                                PCAP_CMD_DESC };

static struct pcap_cmd_t g_cap_sub_commands[] = {
    {"start", "<portname> [ options ]", pcap_cmd_start, PCAP_CMD_START_MIN_PARAM, PCAP_CMD_START_MAX_PARAM, NULL},
    {"stop",  "{ <portname> | -pcap_id <id> }", pcap_cmd_stop, PCAP_CMD_STOP_MIN_PARAM, PCAP_CMD_STOP_MAX_PARAM, NULL},
    {"show",  "{ all | -pcap_id <id> }", pcap_cmd_show, PCAP_CMD_SHOW_MIN_PARAM, PCAP_CMD_SHOW_MAX_PARAM, NULL},
    {"-h",  "", pcap_cmd_help, 0, 0, NULL},
    {"--help",  "", pcap_cmd_help, 0, 0, NULL},
};

static const struct hinic3_opt enable_pcap_cmd_opts[] = {
    {"-h",        REQUIRED_ARGUMENT, 0, ENPCAP_HELP_OPT},
    {"--help",    REQUIRED_ARGUMENT, 0, ENPCAP_HELP_OPT},
    {"-c",        OPTIONAL_ARGUMENT, 0, ENPCAP_CPU_MODE_OPT},
    {"--cpu",     OPTIONAL_ARGUMENT, 0, ENPCAP_CPU_MODE_OPT},
    {"-p",        OPTIONAL_ARGUMENT, 0, ENPCAP_PCAP_MODE_OPT},
    {"--pcap",    OPTIONAL_ARGUMENT, 0, ENPCAP_PCAP_MODE_OPT},
    {"-q",        REQUIRED_ARGUMENT, 0, ENPCAP_QUERY_OPT},
    {"--query",   REQUIRED_ARGUMENT, 0, ENPCAP_QUERY_OPT},
    {NULL,        0,                 0, 0}
};

static void
pcap_cmd_usage_print(struct ds *ds)
{
    struct pcap_cmd_t *p_cmd = &g_cap_main_command;

    hinic3_ds_put_cstr(ds, p_cmd->desc);
}

static int
pcap_cmd_option_check(const struct hinic3_opt *opts, const char *name)
{
    for (int i = 0; opts[i].name != NULL; i++) {
        if (strcmp(name, opts[i].name) == 0)
            return opts[i].val;
    }
    return PCAP_CMD_ERR_ARGS;
}

static void
pcap_cmd_enable_capture_err(int pcap_err, struct ds *ds)
{
    if (pcap_err == PCAP_CMD_TOO_FEW_ARGS) {
        hinic3_ds_put_format(ds, "%s", HINIC3_UI_LEADING_SIGN_ERROR
            HINIC3_UI_ERROR_INCOMPLETE_COMMAND HINIC3_COMMAND_HELP_INFO);
    }

    if (pcap_err == PCAP_CMD_TOO_MANY_ARGS) {
        hinic3_ds_put_format(ds, "%s", HINIC3_UI_LEADING_SIGN_ERROR
            HINIC3_UI_ERROR_TOO_MANY_PARAMETER HINIC3_COMMAND_HELP_INFO);
    }

    if (pcap_err == PCAP_CMD_ERR_ARGS) {
        hinic3_ds_put_format(ds, "%s", HINIC3_UI_LEADING_SIGN_ERROR
            HINIC3_UI_ERROR_WRONG_PARAMETER HINIC3_COMMAND_HELP_INFO);
    }
}

static void
pcap_cmd_enable_help(struct ds *ds)
{
    if (hinic3_support_payload_capture_get() == false)
        hinic3_ds_put_format(ds, PCAP_CMD_ENBLE_DESC);
    else
        hinic3_ds_put_format(ds, PCAP_CMD_ENBLE_DESC_FULLY_CAP);
}

static int
pcap_cmd_enable_set_pcap_mode(const char *mode_str)
{
    if (strcmp(mode_str, "limited-capture") == 0)
        pcap_mode_set(LIMITED_CAPTURE);
    else if (strcmp(mode_str, "fully-capture") == 0)
        pcap_mode_set(FULLY_CAPTURE);
    else
        return PCAP_CMD_ERR_ARGS;

    return 0;
}

static int
pcap_cmd_enable_set_cpu_level(const char *level)
{
    if (strcmp("low", level) == 0)
        pcap_cpu_usage_set(PCAP_CPU_LOW);
    else if (strcmp("high", level) == 0)
        pcap_cpu_usage_set(PCAP_CPU_HIGH);
    else
        return PCAP_CMD_ERR_ARGS;

    return 0;
}

static const char *
pcap_cmd_pcap_mode_str(void)
{
    if (pcap_mode_get() == ONLY_HEADER)
        return "pcap mode is only header";
    if (pcap_mode_get() == LIMITED_CAPTURE)
        return "pcap mode is limited capture";
    if (pcap_mode_get() == FULLY_CAPTURE)
        return "pcap mode is fully capture";
    return "";
}

static const char *
pcap_cmd_cpu_usage_str(void)
{
    if (pcap_cpu_usage_get() == PCAP_CPU_HIGH)
        return "the CPU usage level is high";
    if (pcap_cpu_usage_get() == PCAP_CPU_LOW)
        return "the CPU usage level is low";
    return "";
}

static void
pcap_cmd_enable_query(struct ds *ds)
{
    enum { STR_BUF_SIZE = 1024 };
    char buf[STR_BUF_SIZE];
    if (pcap_switch_get() != 1) {
        snprintf(buf, sizeof(buf), "Capture status is disabled.");
    } else {
        snprintf(buf, sizeof(buf), "Capture status is enabled, %s, %s.",
            pcap_cmd_pcap_mode_str(), pcap_cmd_cpu_usage_str());
    }

    hinic3_ds_put_format_prefix(ds, 0, HINIC3_UI_LEADING_SIGN_INFO, "%s\n", buf);
    HINIC3_LOG(INFO, CAPTURE, "%s", buf);
}

static int
pcap_cmd_enable_parse_status_args(int argc, const char *argv[], struct ds *ds)
{
    if (argc == 1)
        return 1;

    int option_val = pcap_cmd_option_check(enable_pcap_cmd_opts, argv[1]);
    if (argc == PCAP_CMD_ENABLE_MAX_PARAM) {
        switch (option_val) {
            case ENPCAP_HELP_OPT:
                pcap_cmd_enable_help(ds);
                return 0;
            case ENPCAP_QUERY_OPT:
                pcap_cmd_enable_query(ds);
                return 0;
            default:
                return PCAP_CMD_ERR_ARGS;
        }
    }

    if (option_val == ENPCAP_HELP_OPT || option_val == ENPCAP_QUERY_OPT)
        return PCAP_CMD_TOO_MANY_ARGS;

    return 1;
}

static int
pcap_cmd_enable_parse_setup_args(int argc, const char *argv[])
{
    int ret = 0;
    int option_val = 0;

    pcap_cpu_usage_set(PCAP_CPU_HIGH);
    if (hinic3_support_payload_capture_get() == false)
        pcap_mode_set(ONLY_HEADER);
    else
        pcap_mode_set(LIMITED_CAPTURE);

    if (argc == 1)
        return 0;

    for (int i = 1; i < argc; ++i) {
        option_val = pcap_cmd_option_check(enable_pcap_cmd_opts, argv[i]);
        switch (option_val) {
            case ENPCAP_CPU_MODE_OPT:
                if (argc <= ++i)
                    return PCAP_CMD_TOO_FEW_ARGS;
                ret = pcap_cmd_enable_set_cpu_level(argv[i]);
                break;
            case ENPCAP_PCAP_MODE_OPT:
                if (hinic3_support_payload_capture_get() == false)
                    return PCAP_CMD_ERR_ARGS;
                if (argc <= ++i)
                    return PCAP_CMD_TOO_FEW_ARGS;
                ret = pcap_cmd_enable_set_pcap_mode(argv[i]);
                break;
            default:
                return PCAP_CMD_ERR_ARGS;
        }
        if (ret != 0)
            return ret;
    }

    return 0;
}

static int
hinic3_set_pcap_mode(struct ds *ds)
{
    int ret = 0;
    enum { PCAP_BUF_SIZE = 64 };
    uint8_t buf[PCAP_BUF_SIZE] = {0};
    struct hinic3_nlattr set_nla = {0};
    struct hinic3_drv_ops *ops = hinic3_get_drv_ops();

    HINIC3_FUNC_PTR_OR_ERR_RET(ops->hovs_global_cfg_set, HINIC3_DRV_FUNC_NO_PTR);
    hinic3_nlattr_init(&set_nla, buf, PCAP_BUF_SIZE * sizeof(uint8_t));
    /* only-header与limited-capture由解析模块截断，组件不区分这两个模式 */
    if (pcap_mode_get() == FULLY_CAPTURE)
        hinic3_nlattr_put_u8(&set_nla, HINIC3_GLOBAL_CFG_ARG_PCAP, 1);
    else
        hinic3_nlattr_put_u8(&set_nla, HINIC3_GLOBAL_CFG_ARG_PCAP, 0);

    ret = ops->hovs_global_cfg_set((struct nlattr *)set_nla.data, set_nla.used_len,
        (struct nlattr *)set_nla.data, &set_nla.used_len);
    if (ret != 0) {
        hinic3_ds_put_format_prefix(ds, 0, HINIC3_UI_LEADING_SIGN_ERROR,
            "hovs_global_cfg_set pcap mode failed, ret is %d!\n", ret);
        HINIC3_LOG(ERR, CAPTURE, "hovs_global_cfg_set pcap mode failed, ret is %d!", ret);
    }

    return hinic3_convert_error_code(ret);
}

static void
pcap_cmd_enable_capture(struct unixctl_conn *conn, int argc, const char *argv[], void *aux)
{
    int ret = 0;
    struct ds ds = DS_EMPTY_INITIALIZER;

    ret = pcap_cmd_enable_parse_status_args(argc, argv, &ds);
    if (ret <= 0) {
        pcap_cmd_enable_capture_err(ret, &ds);
        goto end;
    }

    if (pcap_switch_get() != 0) {
        hinic3_ds_put_format_prefix(&ds, 0, HINIC3_UI_LEADING_SIGN_WARNING,
            "Capture is already enabled, please disable first and then re-enable!\n");
        HINIC3_LOG(WARNING, CAPTURE, "Capture is already enabled, please disable first and then re-enable!");
        goto end;
    }

    ret = pcap_cmd_enable_parse_setup_args(argc, argv);
    if (ret != 0) {
        pcap_cmd_enable_capture_err(ret, &ds);
        goto end;
    }

    ret = hinic3_set_pcap_mode(&ds);
    if (ret != 0)
        goto end;
    pcap_switch_set(1);
    pcap_task_set(0);
    pcap_time_set(hinic3_time_sec());
    pcap_cmd_enable_query(&ds);

end:
    if (ret != 0)
        hinic3_command_reply_error(conn, hinic3_ds_cstr(&ds));
    else
        hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
    *(int *)aux = ret;
}

static void
pcap_cmd_disable_capture(struct unixctl_conn *conn, int argc HINIC3_UNUSED, const char *argv[] HINIC3_UNUSED, void *aux)
{
    struct ds ds = DS_EMPTY_INITIALIZER;
    pcap_switch_set(0);
    HINIC3_LOG(INFO, CAPTURE, "Capture is disabled.");
    hinic3_ds_put_format(&ds, "%sCapture is disabled.\n", HINIC3_UI_LEADING_SIGN_INFO);
    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
    *(int *)aux = 0;
}

int
pcap_stop_param_parse(int argc, const char *argv[], struct pcap_stop_param *param, struct ds *ds)
{
    int ret;
    struct pcap_port_t port_info;
    const char *port_name = NULL;

    if (strcmp("-pcap_id", argv[0]) == 0 && argc == PCAP_CMD_STOP_MAX_PARAM) {
        param->pcap_id = strtoul(argv[PCAP_CMD_STOP_MAX_PARAM - 1], NULL, STR_TO_DEC_NUM);
        if (param->pcap_id == 0) {
            hinic3_ds_put_format(ds, "%sInvalid parameter value.\n", HINIC3_UI_LEADING_SIGN_ERROR);
            return -1;
        }
        return 0;
    }

    if (argc == PCAP_CMD_STOP_MIN_PARAM) {
        port_name = (const char *)argv[0];
        memset(&port_info, 0, sizeof(port_info));
        ret = pcap_port_info_get_by_name(port_name, &port_info, ds);
        if (ret != 0)
            return -1;

        param->port_no = port_info.odp_port_no;
        return 0;
    }

    hinic3_ds_put_format(ds, "%sInvalid command format, please type help.\n", HINIC3_UI_LEADING_SIGN_ERROR);
    return -1;
}

int
pcap_task_delete(struct pcap_stop_param *param, struct ds *ds)
{
    if (param->pcap_id == 0)
        return pcap_task_delete_all(param->port_no, ds);

    return pcap_task_delete_one(param->pcap_id, ds);
}

void
pcap_cmd_stop(struct unixctl_conn *conn, int argc, const char *argv[], void *aux)
{
    int ret;
    struct ds ds = DS_EMPTY_INITIALIZER;
    struct pcap_stop_param param;

    HINIC3_LOG(INFO, CAPTURE, "Going to stop capture tasks.");
    param.port_no = 0;
    param.pcap_id = 0;
    ret = pcap_stop_param_parse(argc, argv, &param, &ds);
    if (ret != 0)
        goto fail;

    ret = pcap_task_delete(&param, &ds);
    if (ret != 0)
        goto fail;

    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
    *(int *)aux = 0;
    return;

fail:
    HINIC3_LOG(ERR, CAPTURE, "Stop capture tasks failed!");
    hinic3_command_reply_error(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
    *(int *)aux = -1;
    return;
}

static int
pcap_task_start_prepare(int argc, const char *argv[], struct pcap_start_param *param, struct ds *ds, struct ds *save_param)
{
    int ret;
    const char *port_name = (const char *)argv[0];

    ret = pcap_key_parse(&param->pcap_key, argc, argv, ds, save_param);
    if (ret != 0)
        return -1;

    if (pcap_check_file_used_no_lock(&param->pcap_key)) {
        hinic3_ds_put_format(ds, "%sFilename has beed used by other task.\n", HINIC3_UI_LEADING_SIGN_ERROR);
        return -1;
    }
    ret = pcap_port_info_get_by_name(port_name, &param->port_info, ds);
    if (ret != 0)
        return -1;

    if (!param->port_info.support_cap) {
        hinic3_ds_put_format(ds, "%sPort %s don't support capture.\n", HINIC3_UI_LEADING_SIGN_FAILURE, port_name);
        return -1;
    }

    return 0;
}

void
pcap_cmd_start(struct unixctl_conn *conn, int argc, const char *argv[], void *aux)
{
    int ret;
    uint32_t pcap_id;
    const char *port_name = argv[0];
    struct pcap_start_param param;
    struct ds ds = DS_EMPTY_INITIALIZER;
    struct ds save_param = DS_EMPTY_INITIALIZER;
    struct pcap_task_mgr_t *task_mgr = pcap_get_task_mgr();

    HINIC3_LOG(INFO, CAPTURE, "Going to start a capture task.");
    if (pcap_switch_get() == 0) {
        hinic3_ds_put_format(&ds, "%sPlease enable capture first, use command hwoff/enable-capture-probe.\n",
            HINIC3_UI_LEADING_SIGN_WARNING);
        goto fail;
    }

    memset(&param, 0, sizeof(param));
    ret = pcap_task_start_prepare(argc, argv, &param, &ds, &save_param);
    if (ret != 0)
        goto fail;

    if (task_mgr->task_cnt >= PCAP_MAX_CAP_TASK) {
        hinic3_ds_put_format(&ds, "%sCapture tasks reach %u now, can't start anymore.\n", HINIC3_UI_LEADING_SIGN_WARNING,
            PCAP_MAX_CAP_TASK);
        goto fail;
    }

    ret = pcap_task_add(&param.port_info, &param.pcap_key, &pcap_id, &save_param);
    if (ret != 0) {
        hinic3_ds_put_format(&ds, "%sInternal error, please check log.\n", HINIC3_UI_LEADING_SIGN_FAILURE);
        goto fail;
    }

    HINIC3_LOG(INFO, CAPTURE, "Start a new capture task, pcap_id is %u.", pcap_id);
    hinic3_ds_put_format(&ds, "%sPort %s start packet capture, pcap id %u.\n", HINIC3_UI_LEADING_SIGN_INFO, port_name,
        pcap_id);
    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
    hinic3_ds_destroy(&save_param);
    *(int *)aux = 0;
    return;

fail:
    HINIC3_LOG(ERR, CAPTURE, "Start a capture task fail!");
    hinic3_command_reply_error(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
    hinic3_ds_destroy(&save_param);
    *(int *)aux = -1;
    return;
}

void
pcap_cmd_exec(struct unixctl_conn *conn, int argc, const char *argv[], void *aux)
{
    size_t i;
    int work_argc;
    const char **work_argv = argv + 1;
    struct ds ds = DS_EMPTY_INITIALIZER;
    const char *sub_cmd_name = NULL;
    struct pcap_cmd_t *dst_sub_cmd = NULL;
    struct pcap_cmd_t *tmp_sub_cmd = NULL;

    work_argc = argc - 1;
    sub_cmd_name = work_argv[0];
    for (i = 0; i < ARRAY_SIZE(g_cap_sub_commands); i++) {
        tmp_sub_cmd = &g_cap_sub_commands[i];
        if (strcmp(tmp_sub_cmd->cmd, sub_cmd_name) == 0) {
            dst_sub_cmd = tmp_sub_cmd;
            break;
        }
    }

    if (!dst_sub_cmd) {
        hinic3_ds_put_format(&ds, "%s%s, please input -h or --help to get help info.\n", HINIC3_UI_LEADING_SIGN_ERROR,
            HINIC3_UI_ERROR_UNRECOGNIZED_COMMAND);
        hinic3_command_reply_error(conn, hinic3_ds_cstr(&ds));
        *(int *)aux = -1;
        hinic3_ds_destroy(&ds);
        return;
    }

    work_argc -= 1;
    work_argv += 1;
    if ((work_argc < dst_sub_cmd->min_args) || (work_argc > dst_sub_cmd->max_args)) {
        hinic3_ds_put_format(&ds, "%s%s, please input -h or --help to get help info.\n", HINIC3_UI_LEADING_SIGN_ERROR,
            HINIC3_UI_ERROR_INCOMPLETE_COMMAND);
        hinic3_command_reply_error(conn, hinic3_ds_cstr(&ds));
        *(int *)aux = -1;
        hinic3_ds_destroy(&ds);
        return;
    }

    dst_sub_cmd->cb(conn, work_argc, work_argv, aux);
}

static void
pcap_task_show_reply_format(struct pcap_task_t *task, struct ds *ds)
{
    const char *tmp = NULL;
    long long time_left = 0;
    struct pcap_stats_t *stats = &task->stats;

    tmp = task->pcap_port.name;
    hinic3_ds_put_format(ds, "  port:             %s\n", tmp);
    hinic3_ds_put_format(ds, "  pcap-id:          %u\n", task->pcap_id);
    hinic3_ds_put_format(ds, "  captured:         %llu\n", (unsigned long long)stats->wr_cnt);
    hinic3_ds_put_format(ds, "  enqueue-fail:     %llu\n", (unsigned long long)stats->en_ring_fail_cnt);
    hinic3_ds_put_format(ds, "  drop:             %llu\n", (unsigned long long)stats->soft_drop_cnt);
    tmp = task->wr_fail_flag ? "true" : "false";
    hinic3_ds_put_format(ds, "  write-success:    %s\n", tmp);
    time_left = task->key.task_time - (hinic3_time_sec() - task->key.task_start_time);
    hinic3_ds_put_format(ds, "  remaining-time:   %s\n", hinic3_transform_duration_to_string((time_t)time_left));
    hinic3_ds_put_format(ds, "  parameters:      %s\n", task->parameter);
}

static void
pcap_all_tasks_show(struct ds *ds)
{
    int i;
    struct pcap_task_t *cap_task = NULL;
    struct pcap_task_mgr_t *task_mgr = pcap_get_task_mgr();

    /* called by maintain thread, task list will not changed, no need to lock task_mgr */
    if (task_mgr->task_cnt == 0)
        return;

    for (i = 0; i < PCAP_MAX_CAP_TASK; i++) {
        cap_task = task_mgr->cap_task_list[i];
        if (!cap_task)
            continue;
        if (i != 0)
            hinic3_ds_put_cstr(ds, "\n");
        pcap_task_spin_lock(cap_task);
        pcap_task_show_reply_format(cap_task, ds);
        pcap_task_spin_unlock(cap_task);
    }

    return;
}

void
pcap_cmd_show(struct unixctl_conn *conn, int argc, const char *argv[], void *aux)
{
    int ret;
    uint32_t index;
    struct pcap_show_param param;
    struct ds ds = DS_EMPTY_INITIALIZER;
    struct pcap_task_t *cap_task = NULL;

    memset(&param, 0, sizeof(param));
    ret = pcap_show_param_parse(argc, argv, &param, &ds);
    if (ret != 0) {
        hinic3_command_reply_error(conn, hinic3_ds_cstr(&ds));
        hinic3_ds_destroy(&ds);
        *(int *)aux = -1;
        return;
    }

    if (!param.is_all) {
        cap_task = pcap_task_find_no_lock(param.pcap_id, &index);
        if (!cap_task) {
            hinic3_ds_put_format(&ds, "%sCapture task of pcap_id=%u not found.\n", HINIC3_UI_LEADING_SIGN_INFO,
                param.pcap_id);
            hinic3_command_reply_error(conn, hinic3_ds_cstr(&ds));
            hinic3_ds_destroy(&ds);
            *(int *)aux = -1;
            return;
        }

        pcap_task_spin_lock(cap_task);
        pcap_task_show_reply_format(cap_task, &ds);
        pcap_task_spin_unlock(cap_task);
        hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
        hinic3_ds_destroy(&ds);
        *(int *)aux = 0;
        return;
    }

    pcap_all_tasks_show(&ds);
    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
    *(int *)aux = 0;
    return;
}

void
pcap_cmd_help(struct unixctl_conn *conn, int argc HINIC3_UNUSED,
    const char *argv[] HINIC3_UNUSED, void *aux)
{
    struct ds ds = DS_EMPTY_INITIALIZER;

    pcap_cmd_usage_print(&ds);
    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
    *(int *)aux = 0;
}

void
pcap_unix_cmd_register(void)
{
    enum { ARGC = 4 };
    struct pcap_cmd_t *p_cmd = &g_cap_main_command;

    hinic3_command_register(p_cmd->cmd, p_cmd->usage, p_cmd->min_args, p_cmd->max_args, p_cmd->cb, NULL);
    if (hinic3_support_payload_capture_get() == false) {
        hinic3_command_register("hwoff/enable-capture-probe", "[ -c ENUM<high,low> | -q | { -h | --help } ]",
            0, PCAP_CMD_ENABLE_MAX_PARAM, pcap_cmd_enable_capture, NULL);
    } else {
        hinic3_command_register("hwoff/enable-capture-probe",
            "[ [ -p ENUM<limited-capture,fully-capture> | -c ENUM<high,low> ] * | -q | { -h | --help } ]",
            0, ARGC, pcap_cmd_enable_capture, NULL);
    }
    hinic3_command_register("hwoff/disable-capture-probe", "", 0, 0, pcap_cmd_disable_capture, NULL);
}
