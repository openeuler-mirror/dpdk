/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2024 Huawei Technologies Co., Ltd
 */

#ifndef _HINIC3_STRING_FORMAT_H_
#define _HINIC3_STRING_FORMAT_H_

#include "hinic3_capture_utils.h"

int hinic3_key_sip_parse(struct pcap_key_t *cap_key, const char *key_name, const char *value, struct ds *ds);
int hinic3_key_dip_parse(struct pcap_key_t *cap_key, const char *key_name, const char *value, struct ds *ds);
int hinic3_key_smac_parse(struct pcap_key_t *cap_key, const char *key_name, const char *value, struct ds *ds);
int hinic3_key_dmac_parse(struct pcap_key_t *cap_key, const char *key_name, const char *value, struct ds *ds);
int hinic3_key_eth_type_parse(struct pcap_key_t *cap_key, const char *key_name, const char *value, struct ds *ds);
int hinic3_key_ip_proto_parse(struct pcap_key_t *cap_key, const char *key_name, const char *value, struct ds *ds);
int hinic3_key_sport_parse(struct pcap_key_t *cap_key, const char *key_name, const char *value, struct ds *ds);
int hinic3_key_dport_parse(struct pcap_key_t *cap_key, const char *key_name, const char *value, struct ds *ds);
int hinic3_key_vlan_parse(struct pcap_key_t *cap_key, const char *key_name, const char *value, struct ds *ds);
int hinic3_key_vni_parse(struct pcap_key_t *cap_key, const char *key_name, const char *value, struct ds *ds);
int hinic3_key_time_parse(struct pcap_key_t *cap_key, const char *key_name, const char *value, struct ds *ds);
int hinic3_key_port_id_parse(struct pcap_key_t *cap_key, const char *key_name, const char *value, struct ds *ds);
int hinic3_key_icmp_type_parse(struct pcap_key_t *cap_key, const char *key_name, const char *value, struct ds *ds);
int hinic3_key_icmp_code_parse(struct pcap_key_t *cap_key, const char *key_name, const char *value, struct ds *ds);
int hinic3_key_icmp_id_parse(struct pcap_key_t *cap_key, const char *key_name, const char *value, struct ds *ds);
rte_be16_t hinic3_get_inter_type(void);
#endif
