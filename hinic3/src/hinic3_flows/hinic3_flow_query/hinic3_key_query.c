/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2024 Huawei Technologies Co., Ltd
 */

#include <stdio.h>
#include "hinic3_capture_filter.h"
#include "hinic3_age_delete_flow.h"
#include "hinic3_command.h"
#include "hinic3_cmd_exec.h"
#include "hinic3_capture_utils.h"
#include "hinic3_flow_dump_item.h"
#include "hinic3_flow_dump.h"
#include "hinic3_packet_key_public.h"
#include "hinic3_iface_flow.h"
#include "hinic3_agent_cmd_format.h"
#include "hinic3_ui_string.h"
#include "hinic3_agent_flow_cmd.h"
#include "hinic3_string_format.h"
#include "hinic3_key_query.h"
#include "hinic3_parse_agent_config.h"

struct input_key g_input_key = { 0 };


void hinic3_dpak_query_ufid(struct unixctl_conn *conn, int argc, const char *argv[], void *aux);
void hinic3_input_key_help(struct unixctl_conn *conn, int argc, const char *argv[], void *aux);
void hinic3_parse_cmd_exec(struct unixctl_conn *conn, int argc, const char *argv[], void *aux);

static struct pcap_cmd_t g_query_main_commands_7tuple = { "hwoff/query-offloaded-flow",
                                                   "{ start -sip ENUM<IP<X.X.X.X>,IPV6<X:X::X:X>> "
                                                   "-dip ENUM<IP<X.X.X.X>,IPV6<X:X::X:X>> "
                                                   "-smac MAC<H-H-H> -dmac MAC<H-H-H> "
                                                   "-eth_type ENUM<0x0800,0x86dd> "
                                                   "-ip_proto ENUM<TCP,UDP,ICMP,ICMPV6> "
                                                   "-vlan INTEGER<0-4095> "
                                                   "{ -sport INTEGER<0-65535> -dport INTEGER<0-65535> | "
                                                   "-icmp_type INTEGER<0-255> -icmp_code INTEGER<0-255> "
                                                   "-icmp_id INTEGER<0-65535> } "
                                                   "-vxlan_vni INTEGER<0-16777215> "
                                                   "{ -h | --help } }",
                                                   hinic3_parse_cmd_exec,
                                                   QUERY_CMD_MIN_PARAM,
                                                   QUERY_CMD_MAX_PARAM_7TUPLE,
    "  Usage: dpak-ovs-ctl hwoff/query-offloaded-flow { start -sip ENUM<IP<X.X.X.X>,IPV6<X:X::X:X>> \n"
    "                                                   -dip ENUM<IP<X.X.X.X>,IPV6<X:X::X:X>> \n"
    "                                                   -smac MAC<H-H-H> -dmac MAC<H-H-H> \n"
    "                                                   -eth_type ENUM<0x0800,0x86dd> \n"
    "                                                   -ip_proto ENUM<TCP,UDP,ICMP,ICMPV6> \n"
    "                                                   -vlan INTEGER<0-4095>\n"
    "                                                   { -sport INTEGER<0-65535> -dport INTEGER<0-65535> | \n"
    "                                                   -icmp_type INTEGER<0-255> -icmp_code INTEGER<0-255>"
    " -icmp_id INTEGER<0-65535> } \n"
    "                                                   -vxlan_vni INTEGER<0-16777215> | \n"
    "                                                   { -h | --help } }\n\n"
    "  Options list:\n"
    "    start                               Start a flow query task by a specified key\n"
    "      -sip                              Source IP address\n"
    "      -dip                              Destination IP address\n"
    "      -smac                             Source MAC address\n"
    "      -dmac                             Destination MAC address\n"
    "      -eth_type                         Data type, in hexadecimal format. The value is an ENUM<0x0800, 0x86dd>\n"
    "      -ip_proto                         Layer 4 protocol type, ENUM<TCP, UDP, ICMP, ICMPv6>\n"
    "      -vlan                             Identifier for a specific VLAN in the network, INTEGER <0-4095>\n"
    "      -sport                            Source port ID\n"
    "      -dport                            Destination port ID\n"
    "      -icmp_type                        ICMP message type\n"
    "      -icmp_code                        ICMP code\n"
    "      -icmp_id                          ICMP echo request and reply identifier\n"
    "      -vxlan_vni                        VXLAN ID, INTEGER<0-16777215>\n"
    "    -h, --help                          Display the help information\n"};

static struct pcap_cmd_t g_query_main_commands_9tuple = { "hwoff/query-offloaded-flow",
                                                   "{ start -sip ENUM<IP<X.X.X.X>,IPV6<X:X::X:X>> "
                                                   "-dip ENUM<IP<X.X.X.X>,IPV6<X:X::X:X>> "
                                                   "-smac MAC<H-H-H> -dmac MAC<H-H-H> "
                                                   "-eth_type ENUM<0x0800,0x86dd> "
                                                   "-ip_proto ENUM<TCP,UDP,ICMP,ICMPV6> "
                                                   "-vlan INTEGER<0-4095> "
                                                   "{ -sport INTEGER<0-65535> -dport INTEGER<0-65535> | "
                                                   "-icmp_type INTEGER<0-255> -icmp_code INTEGER<0-255> "
                                                   "-icmp_id INTEGER<0-65535> } "
                                                   "-vxlan_vni INTEGER<0-16777215> "
                                                   "-port_id <port_id> | "
                                                   "{ -h | --help } }",
                                                   hinic3_parse_cmd_exec,
                                                   QUERY_CMD_MIN_PARAM,
                                                   QUERY_CMD_MAX_PARAM_9TUPLE,
    "  Usage: dpak-ovs-ctl hwoff/query-offloaded-flow { start -sip ENUM<IP<X.X.X.X>,IPV6<X:X::X:X>> \n"
    "                                                   -dip ENUM<IP<X.X.X.X>,IPV6<X:X::X:X>> \n"
    "                                                   -smac MAC<H-H-H> -dmac MAC<H-H-H> \n"
    "                                                   -eth_type ENUM<0x0800,0x86dd> \n"
    "                                                   -ip_proto ENUM<TCP,UDP,ICMP,ICMPV6> \n"
    "                                                   -vlan INTEGER<0-4095>\n"
    "                                                   { -sport INTEGER<0-65535> -dport INTEGER<0-65535> | \n"
    "                                                   -icmp_type INTEGER<0-255> -icmp_code INTEGER<0-255>"
    " -icmp_id INTEGER<0-65535> } \n"
    "                                                   -vxlan_vni INTEGER<0-16777215> | \n"
    "                                                   -port_id <port_id> | \n"
    "                                                   { -h | --help } }\n\n"
    "  Options list:\n"
    "    start                               Start a flow query task by a specified key\n"
    "      -sip                              Source IP address\n"
    "      -dip                              Destination IP address\n"
    "      -smac                             Source MAC address\n"
    "      -dmac                             Destination MAC address\n"
    "      -eth_type                         Data type, in hexadecimal format. The value is an ENUM<0x0800, 0x86dd>\n"
    "      -ip_proto                         Layer 4 protocol type, ENUM<TCP, UDP, ICMP, ICMPv6>\n"
    "      -vlan                             Identifier for a specific VLAN in the network, INTEGER <0-4095>\n"
    "      -sport                            Source port ID\n"
    "      -dport                            Destination port ID\n"
    "      -icmp_type                        ICMP message type\n"
    "      -icmp_code                        ICMP code\n"
    "      -icmp_id                          ICMP echo request and reply identifier\n"
    "      -vxlan_vni                        VXLAN ID, INTEGER<0-16777215>\n"
    "      -port_id                          Vport ID\n"
    "    -h, --help                          Display the help information\n"};

static void query_cmd_usage_print(struct ds *ds)
{
    struct pcap_cmd_t *q_cmd = NULL;
    if (hinic3_forward_mode_get() == OVS_KEY_EXTRACT_EXTEND_MODE_7TUPLE) {
        q_cmd = &g_query_main_commands_7tuple;
    } else {
        q_cmd = &g_query_main_commands_9tuple;
    }
    hinic3_ds_put_format(ds,  q_cmd->desc);
}

static struct pcap_cmd_t  g_query_sub_commands[] = {
    {"start", NULL, hinic3_dpak_query_ufid, 22, 24, NULL},
    {"-h",  NULL, hinic3_input_key_help, 1, 1, NULL},
    {"--help",  NULL, hinic3_input_key_help, 1, 1, NULL},
};

void hinic3_parse_cmd_exec(struct unixctl_conn *conn, int argc, const char *argv[], void *aux)
{
    int work_argc;
    const char **work_argv = argv + 1;
    struct ds ds = DS_EMPTY_INITIALIZER;
    const char *sub_cmd_name = NULL;
    struct pcap_cmd_t *dst_sub_cmd = NULL;
    work_argc = argc - 1;
    sub_cmd_name = work_argv[0];
    for (size_t i = 0; i < ARRAY_SIZE(g_query_sub_commands); i++) {
        if (strcmp(sub_cmd_name, g_query_sub_commands[i].cmd) == 0) {
            dst_sub_cmd = &g_query_sub_commands[i];
            break;
        }
    }

    if (!dst_sub_cmd) {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR HINIC3_UI_ERROR_UNRECOGNIZED_COMMAND HINIC3_COMMAND_HELP_INFO);
        hinic3_command_reply_error(conn, hinic3_ds_cstr(&ds));
        *(int *)aux = -1;
        hinic3_ds_destroy(&ds);
        return;
    }

    dst_sub_cmd->cb(conn, work_argc, work_argv, aux);
    *(int *)aux = 0;
    hinic3_ds_destroy(&ds);
}

static struct pcap_sub_key_parser g_input_key_parser[] = {
    {"-sip", sizeof("-sip"), hinic3_key_sip_parse},
    {"-dip", sizeof("-dip"), hinic3_key_dip_parse},
    {"-smac", sizeof("-smac"), hinic3_key_smac_parse},
    {"-dmac", sizeof("-dmac"), hinic3_key_dmac_parse},
    {"-ip_proto", sizeof("-ip_proto"), hinic3_key_ip_proto_parse},
    {"-sport", sizeof("-sport"), hinic3_key_sport_parse},
    {"-dport", sizeof("-dport"), hinic3_key_dport_parse},
    {"-eth_type", sizeof("-eth_type"), hinic3_key_eth_type_parse},
    {"-vlan", sizeof("-vlan"), hinic3_key_vlan_parse},
    {"-vxlan_vni", sizeof("-vxlan_vni"), hinic3_key_vni_parse},
    {"-port_id", sizeof("-port_id"), hinic3_key_port_id_parse},
    {"-icmp_type", sizeof("-icmp_type"), hinic3_key_icmp_type_parse},
    {"-icmp_code", sizeof("-icmp_code"), hinic3_key_icmp_code_parse},
    {"-icmp_id", sizeof("-icmp_id"), hinic3_key_icmp_id_parse},
};

struct hinic3_input_key {
    const char *key;
    bool is_input_key;
};

static struct hinic3_input_key g_hinic3_query_flow_input_keys[] = {
    {"-sip", false},
    {"-dip", false},
    {"-smac", false},
    {"-dmac", false},
    {"-sport", false},
    {"-dport", false},
    {"-ip_proto", false},
    {"-eth_type", false},
    {"-vlan", false},
    {"-vxlan_vni", false},
    {"-port_id", false},
    {"-icmp_type", false},
    {"-icmp_code", false},
    {"-icmp_id", false}
};

static void hinic3_init_input_key_stats(void)
{
    for (size_t i = 0; i < ARRAY_SIZE(g_hinic3_query_flow_input_keys); i++) {
        g_hinic3_query_flow_input_keys[i].is_input_key = false;
    }
}

static void hinic3_update_input_key_stats(const char *key)
{
    for (size_t i = 0; i < ARRAY_SIZE(g_hinic3_query_flow_input_keys); i++) {
        if (strcmp(key, g_hinic3_query_flow_input_keys[i].key) == 0) {
            g_hinic3_query_flow_input_keys[i].is_input_key = true;
        }
    }
}

static bool hinic3_get_input_key_stats(const char *key)
{
    bool key_status = false;
    for (size_t i = 0; i < ARRAY_SIZE(g_hinic3_query_flow_input_keys); i++) {
        if (strcmp(key, g_hinic3_query_flow_input_keys[i].key) == 0) {
            key_status = g_hinic3_query_flow_input_keys[i].is_input_key;
            return key_status;
        }
    }
    return key_status;
}

static bool hinic3_check_key_udp_is_complete(void)
{
    bool is_valid = false;
    const char* udp_key[] = {"-sport", "-dport"};
    for (size_t i = 0; i < ARRAY_SIZE(udp_key); i++) {
        is_valid = hinic3_get_input_key_stats(udp_key[i]);
        if (!is_valid) {
            return false;
        }
    }
    return true;
}

static int hinic3_check_key_tcp_is_complete(void)
{
    bool is_valid = false;
    const char* tcp_key[] = {"-sport", "-dport"};
    for (size_t i = 0; i < ARRAY_SIZE(tcp_key); i++) {
        is_valid = hinic3_get_input_key_stats(tcp_key[i]);
        if (!is_valid) {
            return false;
        }
    }
    return true;
}
static bool hinic3_check_key_icmp_is_complete(void)
{
    bool is_valid = false;
    const char* icmp_key[] = {"-icmp_type", "-icmp_code", "-icmp_id"};
    for (size_t i = 0; i < ARRAY_SIZE(icmp_key); i++) {
        is_valid = hinic3_get_input_key_stats(icmp_key[i]);
        if (!is_valid) {
            return false;
        }
    }
    return true;
}

static bool hinic3_check_key_icmp6_is_complete(void)
{
    bool is_valid = false;
    const char* icmp6_key[] = {"-icmp_type", "-icmp_code", "-icmp_id"};
    for (size_t i = 0; i < ARRAY_SIZE(icmp6_key); i++) {
        is_valid = hinic3_get_input_key_stats(icmp6_key[i]);
        if (!is_valid) {
            return false;
        }
    }
    return true;
}

static bool hinic3_check_port_key_is_complete(uint8_t ip_proto)
{
    switch (ip_proto) {
        case IPPROTO_UDP:
            return hinic3_check_key_udp_is_complete();
        case IPPROTO_TCP:
            return hinic3_check_key_tcp_is_complete();
        case IPPROTO_ICMP:
            return hinic3_check_key_icmp_is_complete();
        case IPPROTO_ICMPV6:
            return hinic3_check_key_icmp6_is_complete();
        default:
            return false;
    }
    return false;
}

static bool hinic3_check_querry_flow_key_is_complete(uint8_t ip_proto)
{
    bool is_valid = false;
    const char* required_keys[] = {
        "-sip", "-dip", "-smac", "-dmac", "-ip_proto", "-eth_type",
        "-vlan", "-vxlan_vni"
    };

    for (size_t i = 0; i < ARRAY_SIZE(required_keys); i++) {
        if (strcmp(required_keys[i], "-port_id") == 0 && hinic3_forward_mode_get() == OVS_KEY_EXTRACT_EXTEND_MODE_7TUPLE) {
            continue;
        }
        is_valid = hinic3_get_input_key_stats(required_keys[i]);
        if (!is_valid) {
            return false;
        }
    }

    is_valid = hinic3_check_port_key_is_complete(ip_proto);
    if (!is_valid) {
        return false;
    }
    return true;
}

static int hinic3_sub_key_parse(struct unixctl_conn *conn HINIC3_UNUSED, struct pcap_key_t *cap_key,
    const char *key_name, const char *value, struct ds *ds)
{
    size_t i = 0;
    int ret = 0;
    pcap_sub_key_parse_func func = NULL;
    for (i = 0; i < ARRAY_SIZE(g_input_key_parser); i++) {
        struct pcap_sub_key_parser *item = &g_input_key_parser[i];
        if (strcmp(key_name, item->key_name) == 0) {
            hinic3_update_input_key_stats(key_name);
            func = item->func;
            break;
        }
    }

    if (!func) {
        hinic3_ds_put_format(ds, "%sWrong parameter %s!\n", HINIC3_UI_LEADING_SIGN_ERROR, key_name);
        return -1;
    }

    ret = func(cap_key, key_name, value, ds);
    if (ret != 0) {
        HINIC3_LOG(ERR, FLOW, "Parse %s failed!", key_name);
        return -1;
    }
    return ret;
}


static int get_dmac_key(struct input_key *g_input_key, struct pcap_key_t *cap_key)
{
    memcpy(g_input_key->dmac, cap_key->dmac, RTE_ETHER_ADDR_LEN);
    return 0;
}

static int get_smac_key(struct input_key *g_input_key, struct pcap_key_t *cap_key)
{
    memcpy(g_input_key->smac, cap_key->smac, RTE_ETHER_ADDR_LEN);
    return 0;
}

static int get_eth_type_key(struct input_key *g_input_key, struct pcap_key_t *cap_key)
{
    g_input_key->eth_type = cap_key->eth_type;
    return 0;
}

static int get_vxlan_vni_key(struct input_key *g_input_key, struct pcap_key_t *cap_key)
{
    g_input_key->vni = cap_key->vxlan_vni;
    return 0;
}

static int get_icmp_type_key(struct input_key *g_input_key, struct pcap_key_t *cap_key)
{
    g_input_key->icmp_type = cap_key->icmp_type;
    return 0;
}

static int get_icmp_code_key(struct input_key *g_input_key, struct pcap_key_t *cap_key)
{
    g_input_key->icmp_code = cap_key->icmp_code;
    return 0;
}

static int get_icmp_id_key(struct input_key *g_input_key, struct pcap_key_t *cap_key)
{
    g_input_key->icmp_id = cap_key->icmp_id;
    return 0;
}

static int get_sip_key(struct input_key *g_input_key, struct pcap_key_t *cap_key)
{
    g_input_key->sip = cap_key->sip;
    return 0;
}

static int get_dip_key(struct input_key *g_input_key, struct pcap_key_t *cap_key)
{
    g_input_key->dip = cap_key->dip;
    return 0;
}

static int get_sport_key(struct input_key *g_input_key, struct pcap_key_t *cap_key)
{
    g_input_key->sport = cap_key->sport;
    return 0;
}

static int get_dport_key(struct input_key *g_input_key, struct pcap_key_t *cap_key)
{
    g_input_key->dport = cap_key->dport;
    return 0;
}

static int get_ip_proto_key(struct input_key *g_input_key, struct pcap_key_t *cap_key)
{
    g_input_key->ip_proto = cap_key->ip_proto;
    return 0;
}

static int get_port_id_key(struct input_key *g_input_key, struct pcap_key_t *cap_key)
{
    if (hinic3_forward_mode_get() == OVS_KEY_EXTRACT_EXTEND_MODE_7TUPLE) {
        return -1;
    }
    g_input_key->port_id = cap_key->port_id;
    return 0;
}

static int get_vlan_key(struct input_key *g_input_key, struct pcap_key_t *cap_key)
{
    g_input_key->vlan_id = cap_key->vlan_id;
    return 0;
}

static struct query_key_parse g_get_input_key[] = {
    {"-dmac", get_dmac_key},
    {"-smac", get_smac_key},
    {"-eth_type", get_eth_type_key},
    {"-vlan", get_vlan_key},
    {"-sip", get_sip_key},
    {"-dip", get_dip_key},
    {"-sport", get_sport_key},
    {"-dport", get_dport_key},
    {"-ip_proto", get_ip_proto_key},
    {"-port_id", get_port_id_key},
    {"-vxlan_vni", get_vxlan_vni_key},
    {"-icmp_type", get_icmp_type_key},
    {"-icmp_code", get_icmp_code_key},
    {"-icmp_id", get_icmp_id_key},
};

static void get_input_key(const char *key, struct pcap_key_t *cap_key)
{
    int ret;
    size_t i;
    struct ds ds = DS_EMPTY_INITIALIZER;
    struct query_key_parse *item = NULL;
    query_key_func func = NULL;
    for (i = 0; i < ARRAY_SIZE(g_get_input_key); i++) {
        item = &g_get_input_key[i];
        if (strcmp(key, item->key_name) == 0) {
            func = item->func;
            break;
        }
    }

    if (!func) {
        hinic3_ds_put_format(&ds, "%sIllegal parameter!\n", HINIC3_UI_LEADING_SIGN_ERROR);
        return;
    }

    ret = func(&g_input_key, cap_key);
    if (ret != 0) {
        HINIC3_LOG(ERR, FLOW, "get_key error!");
        return;
    }
    return;
}

static int hinic3_input_key_parse(struct unixctl_conn *conn, struct pcap_key_t *cap_key, int argc,
    const char *argv[], struct ds *ds)
{
    int ret;
    int work_argc = argc - 1;
    int i = 1;
    const char **work_argv = argv;

    int min_key_num;
    int max_key_num;
    if (hinic3_forward_mode_get() == OVS_KEY_EXTRACT_EXTEND_MODE_7TUPLE) {
        min_key_num = INPUT_KEY_MIN_NUM_7TUPLE;
        max_key_num = INPUT_KEY_MAX_NUM_7TUPLE;
    } else {
        min_key_num = INPUT_KEY_MIN_NUM_9TUPLE;
        max_key_num = INPUT_KEY_MAX_NUM_9TUPLE;
    }

    memset(cap_key, 0, sizeof(struct pcap_key_t));
    if (work_argc < min_key_num || work_argc > max_key_num) {
        HINIC3_LOG(ERR, FLOW, "Input parameter nums error.");
        hinic3_ds_put_format(ds, "%sIncomplete command!\n", HINIC3_UI_LEADING_SIGN_ERROR);
        return -1;
    }

    while (i < work_argc) {
        ret = hinic3_sub_key_parse(conn, cap_key, work_argv[i], (const char *)work_argv[i + 1], ds);
        if (ret != 0) {
            HINIC3_LOG(ERR, FLOW, "key_parse error!");
            return -1;
        }
        get_input_key(work_argv[i], cap_key);
        i += ARGC_ADD_DOUBLE;
    }

    return 0;
}

static int get_mac_pattern(struct rte_flow_item *item, struct input_key *item_key)
{
    struct rte_flow_item_eth *eth_spec = hinic3_calloc(1, sizeof(struct rte_flow_item_eth), HINIC3_COMMAND);
    if (eth_spec == NULL) {
        HINIC3_LOG(ERR, FLOW, "malloc eth_spec failed");
        return -1;
    }

    memcpy(&eth_spec->src, item_key->smac, RTE_ETHER_ADDR_LEN);
    memcpy(&eth_spec->dst, item_key->dmac, RTE_ETHER_ADDR_LEN);
    eth_spec->type = item_key->eth_type;

    item->spec = eth_spec;
    item->type = RTE_FLOW_ITEM_TYPE_ETH;

    return 0;
}

static int get_ip_pattern_ipv4(struct rte_flow_item *item, struct input_key *item_key)
{
    struct rte_flow_item_ipv4 *ipv4_spec = hinic3_calloc(1, sizeof(struct rte_flow_item_ipv4), HINIC3_COMMAND);
    if (ipv4_spec == NULL) {
        HINIC3_LOG(ERR, FLOW, "malloc ip_spec failed");
        return -1;
    }
    memcpy(&ipv4_spec->hdr.src_addr, &item_key->sip, sizeof(ipv4_spec->hdr.src_addr));
    memcpy(&ipv4_spec->hdr.dst_addr, &item_key->dip, sizeof(ipv4_spec->hdr.dst_addr));
    ipv4_spec->hdr.next_proto_id = item_key->ip_proto;

    item->spec = ipv4_spec;
    item->type = RTE_FLOW_ITEM_TYPE_IPV4;

    return 0;
}

static int get_ip_pattern_ipv6(struct rte_flow_item *item, struct input_key *item_key)
{
    struct rte_flow_item_ipv6 *ipv6_spec = hinic3_calloc(1, sizeof(struct rte_flow_item_ipv6), HINIC3_COMMAND);
    if (ipv6_spec == NULL) {
        HINIC3_LOG(ERR, FLOW, "malloc ip_spec failed");
        return -1;
    }

    memcpy(&ipv6_spec->hdr.src_addr, &item_key->sip, sizeof(item_key->sip));
    memcpy(&ipv6_spec->hdr.dst_addr, &item_key->dip, sizeof(item_key->dip));
    ipv6_spec->hdr.proto = item_key->ip_proto;

    item->spec = ipv6_spec;
    item->type = RTE_FLOW_ITEM_TYPE_IPV6;

    return 0;
}

static int get_ip_pattern(struct rte_flow_item *item, struct input_key *item_key)
{
    if (item_key->eth_type == htons(ETH_TYPE_IP)) {
        return get_ip_pattern_ipv4(item, item_key);
    } else if (item_key->eth_type == htons(ETH_TYPE_IPV6)) {
        return get_ip_pattern_ipv6(item, item_key);
    }
    return -1;
}

static int get_port_pattern_udp(struct rte_flow_item *item, struct input_key *item_key)
{
    struct rte_flow_item_udp *udp_spec = hinic3_calloc(1, sizeof(struct rte_flow_item_udp), HINIC3_COMMAND);
    if (udp_spec == NULL) {
        HINIC3_LOG(ERR, FLOW, "malloc port pattern udp_spec failed");
        return -1;
    }

    udp_spec->hdr.dst_port = item_key->dport;
    udp_spec->hdr.src_port = item_key->sport;

    item->spec = udp_spec;
    item->type = RTE_FLOW_ITEM_TYPE_UDP;

    return 0;
}

static int get_port_pattern_tcp(struct rte_flow_item *item, struct input_key *item_key)
{
    struct rte_flow_item_tcp *tcp_spec = hinic3_calloc(1, sizeof(struct rte_flow_item_tcp), HINIC3_COMMAND);
    if (tcp_spec == NULL) {
        HINIC3_LOG(ERR, FLOW, "malloc port pattern tcp_spec failed");
        return -1;
    }

    tcp_spec->hdr.dst_port = item_key->dport;
    tcp_spec->hdr.src_port = item_key->sport;

    item->spec = tcp_spec;
    item->type = RTE_FLOW_ITEM_TYPE_TCP;

    return 0;
}

static int get_port_pattern_icmp(struct rte_flow_item *item, struct input_key *item_key)
{
    struct rte_flow_item_icmp *icmp_spec = hinic3_calloc(1, sizeof(struct rte_flow_item_icmp), HINIC3_COMMAND);
    if (icmp_spec == NULL) {
        HINIC3_LOG(ERR, FLOW, "malloc port pattern icmp_spec failed");
        return -1;
    }

    icmp_spec->hdr.icmp_type = item_key->icmp_type;
    icmp_spec->hdr.icmp_code = item_key->icmp_code;
    icmp_spec->hdr.icmp_ident = item_key->icmp_id;

    item->spec = icmp_spec;
    item->type = RTE_FLOW_ITEM_TYPE_ICMP;

    return 0;
}

static int get_port_pattern_icmp6(struct rte_flow_item *item, struct input_key *item_key)
{
    struct rte_flow_item_icmp *icmp6_spec = hinic3_calloc(1, sizeof(struct rte_flow_item_icmp), HINIC3_COMMAND);
    if (icmp6_spec == NULL) {
        HINIC3_LOG(ERR, FLOW, "malloc port pattern icmp6_spec failed");
        return -1;
    }

    icmp6_spec->hdr.icmp_type = item_key->icmp_type;
    icmp6_spec->hdr.icmp_code = item_key->icmp_code;
    icmp6_spec->hdr.icmp_ident = item_key->icmp_id;

    item->spec = icmp6_spec;
    item->type = RTE_FLOW_ITEM_TYPE_ICMP6;

    return 0;
}

static int get_port_pattern(struct rte_flow_item *item, struct input_key *item_key)
{
    switch (item_key->ip_proto) {
        case IPPROTO_UDP:
            return get_port_pattern_udp(item, item_key);
        case IPPROTO_TCP:
            return get_port_pattern_tcp(item, item_key);
        case IPPROTO_ICMP:
            return get_port_pattern_icmp(item, item_key);
        case IPPROTO_ICMPV6:
            return get_port_pattern_icmp6(item, item_key);
        default:
            return -1;
    }
    return -1;
}

static int get_port_id_pattern(struct rte_flow_item *item, struct input_key *item_key)
{
    if (hinic3_forward_mode_get() == OVS_KEY_EXTRACT_EXTEND_MODE_7TUPLE) {
        return 0;
    }
    struct rte_flow_item_port_id *port_spec = hinic3_calloc(1, sizeof(struct rte_flow_item_port_id), HINIC3_COMMAND);
    if (port_spec == NULL) {
        HINIC3_LOG(ERR, FLOW, "malloc port_id_spec failed");
        return -1;
    }

    port_spec->id = item_key->port_id;

    item->spec = port_spec;
    item->type = RTE_FLOW_ITEM_TYPE_PORT_ID;

    return 0;
}

static int get_vxlan_pattern(struct rte_flow_item *item, struct input_key *item_key)
{
    struct rte_flow_item_vxlan *vxlan_spec = hinic3_calloc(1, sizeof(struct rte_flow_item_vxlan), HINIC3_COMMAND);
    if (vxlan_spec == NULL) {
        HINIC3_LOG(ERR, FLOW, "malloc vlan_spec failed");
        return -1;
    }

    uint8_t vni[HINIC3_VNI_ARR_SIZE];
    vni[HINIC3_VNI_ARR_HIGH] = (uint8_t)(item_key->vni & HINIC3_LOW_EIGHT_BIT_MASK);
    vni[HINIC3_VNI_ARR_MID] = (uint8_t)(((item_key->vni) & HINIC3_MID_EIGHT_BIT_MASK) >> HINIC3_BIT_MID_MOVE_INDEX);
    vni[HINIC3_VNI_ARR_LOW] = (uint8_t)(((item_key->vni) & HINIC3_HIGH_EIGHT_BIT_MASK) >> HINIC3_BIT_HIGH_MOVE_INDEX);
    memcpy(vxlan_spec->vni, vni, sizeof(uint8_t) * HINIC3_VNI_ARR_SIZE);

    item->spec = vxlan_spec;
    item->type = RTE_FLOW_ITEM_TYPE_VXLAN;

    return 0;
}

static int get_vlan_pattern(struct rte_flow_item *item, struct input_key *item_key)
{
    struct rte_flow_item_vlan *vlan_spec = hinic3_calloc(1, sizeof(struct rte_flow_item_vlan), HINIC3_COMMAND);
    if (vlan_spec == NULL) {
        HINIC3_LOG(ERR, FLOW, "malloc vlan_spec failed");
        return -1;
    }

    if (item_key->vlan_id == 0) {
        vlan_spec->inner_type = 0;
    } else {
        vlan_spec->inner_type = item_key->inner_type;
        vlan_spec->tci = htons(item_key->vlan_id);
    }

    item->spec = vlan_spec;
    item->type = RTE_FLOW_ITEM_TYPE_VLAN;

    return 0;
}

pattern_func g_get_query_pattern[] = {get_vxlan_pattern,
                                      get_mac_pattern,
                                      get_ip_pattern,
                                      get_port_pattern,
                                      get_port_id_pattern,
                                      get_vlan_pattern, };

static void hinic3_query_pattern_free(struct rte_flow_item *pattern)
{
    int i = 0;
    for (i = 0; i < HINIC3_INPUT_ITEM_MAX_NUM; i++) {
        if (pattern[i].type != RTE_FLOW_ITEM_TYPE_END && pattern[i].spec == NULL) {
                hinic3_free((void*)(uintptr_t)pattern[i].spec);
                pattern[i].spec = NULL;
        }
    }
}

static int hinic3_get_pattern(struct rte_flow_item *pattern, struct input_key *item_key, int pattern_max_num)
{
    int i = 0;
    int ret = 0;
    for (i = 0; i < pattern_max_num; i++) {
        ret = g_get_query_pattern[i](&pattern[i], item_key);
        if (ret != 0) {
            hinic3_query_pattern_free(pattern);
            return -1;
        }
    }
    return 0;
}

void hinic3_dpak_query_ufid(struct unixctl_conn *conn, int argc, const char *argv[], void *aux)
{
    int ret;
    bool keys_is_complete;
    struct ds ds = DS_EMPTY_INITIALIZER;
    const struct rte_flow_attr *attr = NULL;
    struct pcap_key_t cap_key = { 0 };
    struct rte_flow_item pattern[HINIC3_INPUT_ITEM_MAX_NUM] = {0};

    struct hinic3_conntrack_full_key full_key = { 0 };
    uint64_t ufid = 0;
    hinic3_init_input_key_stats();
    ret = hinic3_input_key_parse(conn, &cap_key, argc, argv, &ds);
    if (ret != 0) {
        HINIC3_LOG(ERR, FLOW, "key parse failed!");
        goto err_out;
    }
    g_input_key.inner_type = hinic3_get_inter_type();
    keys_is_complete = hinic3_check_querry_flow_key_is_complete(g_input_key.ip_proto);
    if (!keys_is_complete) {
        HINIC3_LOG(ERR, FLOW, "Query flow input keys are incomplete!");
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR "Incomplete command!\n");
        goto err_out;
    }
    ret = hinic3_get_pattern(pattern, &g_input_key, HINIC3_INPUT_ITEM_MAX_NUM);
    if (ret != 0) {
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR "Get pattern failed.\n");
        goto err_out;
    }

    ret = hinic3_flow_query_ufid(attr, pattern, &ufid, &full_key, &ds);
    if (ret != 0) {
        HINIC3_LOG(ERR, FLOW, "query flow failed.");
        hinic3_ds_put_format(&ds, HINIC3_UI_LEADING_SIGN_ERROR "Query flow failed.\n");
        hinic3_query_pattern_free(pattern);
        goto err_out;
    }

    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
    hinic3_query_pattern_free(pattern);
    *(int *)aux = 0;
    return;

err_out:
    hinic3_command_reply_error(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
    *(int *)aux = -1;
    return;
}

void hinic3_input_key_help(struct unixctl_conn *conn, int argc HINIC3_UNUSED, const char *argv[] HINIC3_UNUSED, void *aux)
{
    struct ds ds = DS_EMPTY_INITIALIZER;

    query_cmd_usage_print(&ds);
    hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
    hinic3_ds_destroy(&ds);
    *(int *)aux = 0;
    return;
}

void unixctl_hinic3_query_cmd_init(void)
{
    struct pcap_cmd_t *p_cmd = NULL;
    if (hinic3_forward_mode_get() == OVS_KEY_EXTRACT_EXTEND_MODE_7TUPLE) {
        p_cmd = &g_query_main_commands_7tuple;
    } else {
        p_cmd = &g_query_main_commands_9tuple;
    }

    hinic3_command_register(p_cmd->cmd, p_cmd->usage, p_cmd->min_args, p_cmd->max_args, p_cmd->cb, NULL);
}
