/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2024 Huawei Technologies Co., Ltd
 */

#include <stdio.h>
#include "hinic3_log.h"
#include "hinic3_util.h"
#include "hinic3_capture_filter.h"
#include "hinic3_ui_string.h"
#include "hinic3_string_format.h"

#define INPUT_KEY_NUM 26
#define ARGC_ADD_DOUBLE 2
#define HINIC3_INPUT_ITEM_MAX_NUM 6
#define PCAP_MAX_PORT_ID 65535
#define QUERY_CMD_MIN_PARAM 1
#define QUERY_CMD_MAX_PARAM 30
#define HINIC3_ICMP_FLOW_MASK 8
#define PCAP_FLAG_KEY_VLAN_ID               (1LLU << 13)
#define PCAP_FLAG_KEY_PORT_ID               (1LLU << 14)
#define PCAP_FLAG_KEY_INNER_TYPE            (1LLU << 15)
#define PCAP_FLAG_KEY_IS_VXLAN              (1LLU << 16)

typedef struct rte_flow_item *(*pattern_func)(struct input_key *);
rte_be16_t g_set_inner_type = 0;
int hinic3_key_sip_parse(struct pcap_key_t *cap_key, const char *key_name, const char *value, struct ds *ds)
{
    int ret;

    if ((cap_key->flags & PCAP_FLAG_KEY_SIP) != 0) {
        hinic3_ds_put_format(ds, "%sWrong parameter, Duplicate parameter \"%s\"!\n", HINIC3_UI_LEADING_SIGN_ERROR, key_name);
        return -1;
    }

    if ((cap_key->flags & PCAP_FLAG_KEY_HOST) != 0) {
        hinic3_ds_put_format(ds, "%sAlready config \"-host\", cann't config \"%s\"!\n", HINIC3_UI_LEADING_SIGN_ERROR,
            key_name);
        return -1;
    }

    ret = pcap_ip_mask_parse(value, &cap_key->sip, &cap_key->sip_masklen, &cap_key->ip_type);
    if (ret != 0) {
        hinic3_ds_put_format(ds, "%sWrong parameter, Value of parameter \"%s\" is invalid!\n", HINIC3_UI_LEADING_SIGN_ERROR, key_name);
        return -1;
    }

    cap_key->flags |= PCAP_FLAG_KEY_SIP;
    return 0;
}

int hinic3_key_dip_parse(struct pcap_key_t *cap_key, const char *key_name, const char *value, struct ds *ds)
{
    int ret;

    if ((cap_key->flags & PCAP_FLAG_KEY_DIP) != 0) {
        hinic3_ds_put_format(ds, "%sWrong parameter, Duplicate parameter \"%s\"!\n", HINIC3_UI_LEADING_SIGN_ERROR, key_name);
        return -1;
    }

    if ((cap_key->flags & PCAP_FLAG_KEY_HOST) != 0) {
        hinic3_ds_put_format(ds, "%sAlready config \"-host\", cann't config \"%s\"!\n", HINIC3_UI_LEADING_SIGN_ERROR,
            key_name);
        return -1;
    }

    ret = pcap_ip_mask_parse(value, &cap_key->dip, &cap_key->dip_masklen, &cap_key->ip_type);
    if (ret != 0) {
        hinic3_ds_put_format(ds, "%sWrong parameter, Value of parameter \"%s\" is invalid!\n", HINIC3_UI_LEADING_SIGN_ERROR, key_name);
        return -1;
    }

    cap_key->flags |= PCAP_FLAG_KEY_DIP;
    return 0;
}

int hinic3_key_smac_parse(struct pcap_key_t *cap_key, const char *key_name, const char *value, struct ds *ds)
{
    int ret;
    struct eth_address addr = { 0 };

    if ((cap_key->flags & PCAP_FLAG_KEY_SMAC) != 0) {
        hinic3_ds_put_format(ds, "%sWrong parameter, Duplicate parameter \"%s\"!\n", HINIC3_UI_LEADING_SIGN_ERROR, key_name);
        return -1;
    }

    ret = parse_mac(value, &addr);
    if (ret != 0) {
        hinic3_ds_put_format(ds, "%sWrong parameter, Value of parameter \"%s\" is invalid!\n", HINIC3_UI_LEADING_SIGN_ERROR, key_name);
        return -1;
    }

    memcpy(cap_key->smac, &addr, RTE_ETHER_ADDR_LEN);

    cap_key->flags |= PCAP_FLAG_KEY_SMAC;
    return 0;
}

int hinic3_key_dmac_parse(struct pcap_key_t *cap_key, const char *key_name, const char *value, struct ds *ds)
{
    int ret;
    struct eth_address addr = { 0 };

    if ((cap_key->flags & PCAP_FLAG_KEY_DMAC) != 0) {
        hinic3_ds_put_format(ds, "%sWrong parameter, Duplicate parameter \"%s\"!\n", HINIC3_UI_LEADING_SIGN_ERROR, key_name);
        return -1;
    }

    ret = parse_mac(value, &addr);
    if (ret != 0) {
        hinic3_ds_put_format(ds, "%sWrong parameter, Value of parameter \"%s\" is invalid!\n", HINIC3_UI_LEADING_SIGN_ERROR, key_name);
        return -1;
    }

    memcpy(cap_key->dmac, &addr, RTE_ETHER_ADDR_LEN);

    cap_key->flags |= PCAP_FLAG_KEY_DMAC;
    return 0;
}

int hinic3_key_eth_type_parse(struct pcap_key_t *cap_key, const char *key_name, const char *value, struct ds *ds)
{
    int ret;
    uint32_t tmp_value;
    char *endPtr = NULL;

    if ((cap_key->flags & PCAP_FLAG_KEY_ETH_TYPE) != 0) {
        hinic3_ds_put_format(ds, "%sWrong parameter, Duplicate parameter \"%s\"!\n", HINIC3_UI_LEADING_SIGN_ERROR, key_name);
        return -1;
    }

    ret = check_valid_eth_type(value);
    if (ret != 0) {
        hinic3_ds_put_format(ds, "%sWrong parameter, Value of parameter \"%s\" is invalid!\n", HINIC3_UI_LEADING_SIGN_ERROR, key_name);
        return -1;
    }

    tmp_value = strtoul(value, &endPtr, STR_TO_HEX_BASE);
    if (endPtr == NULL || *endPtr != '\0') {
        hinic3_ds_put_format(ds, "%sWrong parameter, Value of parameter \"%s\" is invalid!\n", HINIC3_UI_LEADING_SIGN_ERROR, key_name);
        return -1;
    }
    cap_key->eth_type = (uint16_t)htons((uint16_t)tmp_value);

    if (cap_key->eth_type == htons(ETH_TYPE_IP)) {
        g_set_inner_type = htons(ETH_TYPE_IP);
    } else if (cap_key->eth_type == htons(ETH_TYPE_IPV6)) {
        g_set_inner_type = htons(ETH_TYPE_IPV6);
    }

    cap_key->flags |= PCAP_FLAG_KEY_ETH_TYPE;
    return 0;
}

int hinic3_key_ip_proto_parse(struct pcap_key_t *cap_key, const char *key_name, const char *value, struct ds *ds)
{
    int ret;

    if ((cap_key->flags & PCAP_FLAG_KEY_IP_PROTO) != 0) {
        hinic3_ds_put_format(ds, "%sWrong parameter, Duplicate parameter \"%s\"!\n", HINIC3_UI_LEADING_SIGN_ERROR, key_name);
        return -1;
    }

    ret = parse_l4_proto(value, &cap_key->ip_proto);
    if (ret != 0) {
        hinic3_ds_put_format(ds, "%sWrong parameter, Value of parameter \"%s\" is invalid!\n", HINIC3_UI_LEADING_SIGN_ERROR, key_name);
        return -1;
    }

    cap_key->flags |= PCAP_FLAG_KEY_IP_PROTO;
    return 0;
}

int hinic3_key_sport_parse(struct pcap_key_t *cap_key, const char *key_name, const char *value, struct ds *ds)
{
    uint32_t sport_value;
    char *endPtr = NULL;

    if ((cap_key->flags & PCAP_FLAG_KEY_SPORT) != 0) {
        hinic3_ds_put_format(ds, "%sWrong parameter, Duplicate parameter \"%s\"!\n", HINIC3_UI_LEADING_SIGN_ERROR, key_name);
        return -1;
    }

    if (!is_valid_digit(value)) {
        hinic3_ds_put_format(ds, "%sWrong parameter, Invalid value %s for parameter %s.\n", HINIC3_UI_LEADING_SIGN_ERROR, value, key_name);
        return -1;
    }

    sport_value = strtoul(value, &endPtr, STR_TO_DEC_NUM);
    if (endPtr == NULL || *endPtr != '\0') {
        hinic3_ds_put_format(ds, "%sWrong parameter, Value of parameter \"%s\" is invalid!\n", HINIC3_UI_LEADING_SIGN_ERROR, key_name);
        return -1;
    }

    if (sport_value > UINT16_MAX) {
        hinic3_ds_put_format(ds, "%sWrong parameter, Value of parameter \"%s\" is invalid!\n", HINIC3_UI_LEADING_SIGN_ERROR, key_name);
        return -1;
    }

    cap_key->sport = (uint16_t)htons((uint16_t)sport_value);
    cap_key->flags |= PCAP_FLAG_KEY_SPORT;
    return 0;
}

int hinic3_key_dport_parse(struct pcap_key_t *cap_key, const char *key_name, const char *value, struct ds *ds)
{
    uint32_t dport_value;
    char *endPtr = NULL;

    if ((cap_key->flags & PCAP_FLAG_KEY_DPORT) != 0) {
        hinic3_ds_put_format(ds, "%sWrong parameter, Duplicate parameter \"%s\"!\n", HINIC3_UI_LEADING_SIGN_ERROR, key_name);
        return -1;
    }

    if (!is_valid_digit(value)) {
        hinic3_ds_put_format(ds, "%sWrong parameter, Invalid value %s for parameter %s.\n", HINIC3_UI_LEADING_SIGN_ERROR, value, key_name);
        return -1;
    }

    dport_value = strtoul(value, &endPtr, STR_TO_DEC_NUM);
    if (endPtr == NULL || *endPtr != '\0') {
        hinic3_ds_put_format(ds, "%sWrong parameter, Value of parameter \"%s\" is invalid!\n", HINIC3_UI_LEADING_SIGN_ERROR, key_name);
        return -1;
    }

    if (dport_value > UINT16_MAX) {
        hinic3_ds_put_format(ds, "%sWrong parameter, Value of parameter \"%s\" is invalid!\n", HINIC3_UI_LEADING_SIGN_ERROR, key_name);
        return -1;
    }

    cap_key->dport = (uint16_t)htons((uint16_t)dport_value);
    cap_key->flags |= PCAP_FLAG_KEY_DPORT;
    return 0;
}

int hinic3_key_vlan_parse(struct pcap_key_t *cap_key, const char *key_name, const char *value, struct ds *ds)
{
    uint32_t vlan_value;
    char *endPtr = NULL;

    if ((cap_key->flags & PCAP_FLAG_KEY_VLAN) != 0) {
        hinic3_ds_put_format(ds, "%sWrong parameter, Duplicate parameter \"%s\".\n", HINIC3_UI_LEADING_SIGN_ERROR, key_name);
        return -1;
    }

    if (!is_valid_digit(value)) {
        hinic3_ds_put_format(ds, "%sWrong parameter, Invalid value %s for parameter %s.\n", HINIC3_UI_LEADING_SIGN_ERROR, value, key_name);
        return -1;
    }

    vlan_value = strtoul(value, &endPtr, STR_TO_DEC_NUM);
    if (endPtr == NULL || *endPtr != '\0') {
        return -1;
    }
    if (vlan_value > PCAP_MAX_VLAN) {
        hinic3_ds_put_format(ds, "%sParameter value %s for %s is out of range.\n", HINIC3_UI_LEADING_SIGN_ERROR,  value,
            key_name);
        return -1;
    }

    cap_key->vlan_id = (uint16_t)vlan_value;
    cap_key->flags |= PCAP_FLAG_KEY_VLAN;
    return 0;
}

int hinic3_key_vni_parse(struct pcap_key_t *cap_key, const char *key_name, const char *value, struct ds *ds)
{
    uint32_t vni_value;
    char *endPtr = NULL;

    if ((cap_key->flags & PCAP_FLAG_KEY_VXLAN_VNI) != 0) {
        hinic3_ds_put_format(ds, "%sWrong parameter, Duplicate parameter \"%s\"!\n", HINIC3_UI_LEADING_SIGN_ERROR, key_name);
        return -1;
    }

    if (!is_valid_digit(value)) {
        hinic3_ds_put_format(ds, "%sWrong parameter, Invalid value %s for parameter %s.\n", HINIC3_UI_LEADING_SIGN_ERROR, value, key_name);
        return -1;
    }

    vni_value = strtoul(value, &endPtr, STR_TO_DEC_NUM);
    if (endPtr == NULL || *endPtr != '\0') {
        hinic3_ds_put_format(ds, "%sWrong parameter, Value of parameter \"%s\" is invalid!\n", HINIC3_UI_LEADING_SIGN_ERROR, key_name);
        return -1;
    }

    if (vni_value > PCAP_MAX_VNI) {
        hinic3_ds_put_format(ds, "%sWrong parameter, Value of parameter \"%s\" is invalid!\n", HINIC3_UI_LEADING_SIGN_ERROR, key_name);
        return -1;
    }

    cap_key->vxlan_vni = vni_value;
    cap_key->flags |= PCAP_FLAG_KEY_VXLAN_VNI;
    return 0;
}

int hinic3_key_time_parse(struct pcap_key_t *cap_key, const char *key_name, const char *value, struct ds *ds)
{
    long long task_time;
    char *endPtr = NULL;

    if ((cap_key->flags & PCAP_FLAG_KEY_TIME) != 0) {
        hinic3_ds_put_format(ds, "%sWrong parameter, Duplicate parameter \"%s\"!\n", HINIC3_UI_LEADING_SIGN_ERROR, key_name);
        return -1;
    }

    if (!is_valid_digit(value)) {
        hinic3_ds_put_format(ds, "%sWrong parameter, Invalid value %s for parameter %s.\n", HINIC3_UI_LEADING_SIGN_ERROR, value, key_name);
        return -1;
    }

    task_time = strtoull(value, &endPtr, STR_TO_DEC_NUM);
    if (endPtr == NULL || *endPtr != '\0') {
        hinic3_ds_put_format(ds, "%sWrong parameter, Value of parameter \"%s\" is invalid!\n", HINIC3_UI_LEADING_SIGN_ERROR, key_name);
        return -1;
    }

    if (task_time < PCAP_MIN_TASK_TIME || task_time > PCAP_MAX_TASK_TIME) {
        hinic3_ds_put_format(ds, "%sWrong parameter, Value of parameter \"%s\" is invalid!\n", HINIC3_UI_LEADING_SIGN_ERROR, key_name);
        return -1;
    }

    cap_key->task_start_time = hinic3_time_sec();
    cap_key->task_time = task_time * PCAP_TIME_MIN_TO_S;
    cap_key->flags |= PCAP_FLAG_KEY_TIME;
    return 0;
}

rte_be16_t hinic3_get_inter_type(void)
{
    return g_set_inner_type;
}

int hinic3_key_port_id_parse(struct pcap_key_t *cap_key, const char *key_name, const char *value, struct ds *ds)
{
    uint32_t port_id;
    char *endPtr = NULL;
    if ((cap_key->flags & PCAP_FLAG_KEY_PORT_ID) != 0) {
        hinic3_ds_put_format(ds, "%sWrong parameter, Duplicate parameter \"%s\"!\n", HINIC3_UI_LEADING_SIGN_ERROR, key_name);
        return -1;
    }

    if (!is_valid_digit(value)) {
        hinic3_ds_put_format(ds, "%sWrong parameter, Invalid value %s for parameter %s.\n", HINIC3_UI_LEADING_SIGN_ERROR, value, key_name);
        return -1;
    }

    port_id = strtoul(value, &endPtr, STR_TO_DEC_NUM);
    if (endPtr == NULL || *endPtr != '\0') {
        HINIC3_LOG(ERR, AGENT, "port_id is invalid!");
        hinic3_ds_put_format(ds, "%sWrong parameter, Value of parameter \"%s\" is invalid!\n", HINIC3_UI_LEADING_SIGN_ERROR, key_name);
        return -1;
    }

    if (port_id > PCAP_MAX_PORT_ID) {
        hinic3_ds_put_format(ds, "%sWrong parameter, Value of parameter \"%s\" is invalid!\n", HINIC3_UI_LEADING_SIGN_ERROR, key_name);
        return -1;
    }

    cap_key->port_id = port_id;
    cap_key->flags |= PCAP_FLAG_KEY_PORT_ID;
    return 0;
}

int hinic3_key_icmp_type_parse(struct pcap_key_t *cap_key, const char *key_name, const char *value, struct ds *ds)
{
    uint8_t icmp_type;
    char *endPtr = NULL;

    if (!is_valid_digit(value)) {
        hinic3_ds_put_format(ds, "%sWrong parameter, Invalid value %s for parameter %s.\n", HINIC3_UI_LEADING_SIGN_ERROR, value, key_name);
        return -1;
    }

    icmp_type = strtoul(value, &endPtr, STR_TO_DEC_NUM);
    if (endPtr == NULL || *endPtr != '\0') {
        hinic3_ds_put_format(ds, "%sWrong parameter, Value of parameter \"%s\" is invalid!\n", HINIC3_UI_LEADING_SIGN_ERROR, key_name);
        return -1;
    }

    cap_key->icmp_type = icmp_type;
    return 0;
}

int hinic3_key_icmp_code_parse(struct pcap_key_t *cap_key, const char *key_name, const char *value, struct ds *ds)
{
    uint8_t icmp_code;
    char *endPtr = NULL;

    if (!is_valid_digit(value)) {
        hinic3_ds_put_format(ds, "%sWrong parameter, Invalid value %s for parameter %s.\n", HINIC3_UI_LEADING_SIGN_ERROR, value, key_name);
        return -1;
    }

    icmp_code = strtoul(value, &endPtr, STR_TO_DEC_NUM);

    if (endPtr == NULL || *endPtr != '\0') {
        hinic3_ds_put_format(ds, "%sWrong parameter, Value of parameter \"%s\" is invalid!\n", HINIC3_UI_LEADING_SIGN_ERROR, key_name);
        return -1;
    }

    cap_key->icmp_code = icmp_code;
    return 0;
}

int hinic3_key_icmp_id_parse(struct pcap_key_t *cap_key, const char *key_name, const char *value, struct ds *ds)
{
    uint16_t icmp_id;
    char *endPtr = NULL;

    if (!is_valid_digit(value)) {
        hinic3_ds_put_format(ds, "%sWrong parameter, Invalid value %s for parameter %s.\n", HINIC3_UI_LEADING_SIGN_ERROR, value, key_name);
        return -1;
    }

    icmp_id = strtoul(value, &endPtr, STR_TO_DEC_NUM);
    if (endPtr == NULL || *endPtr != '\0') {
        hinic3_ds_put_format(ds, "%sWrong parameter, Value of parameter \"%s\" is invalid!\n", HINIC3_UI_LEADING_SIGN_ERROR, key_name);
        return -1;
    }

    cap_key->icmp_id = (uint16_t)htons((uint16_t)icmp_id);
    cap_key->icmp_id = htons(icmp_id);
    return 0;
}
