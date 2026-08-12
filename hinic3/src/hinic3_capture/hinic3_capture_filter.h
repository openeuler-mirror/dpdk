/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_CAPTURE_FILTER_H
#define HINIC3_CAPTURE_FILTER_H
#include <stddef.h>
#include <stdint.h>
#include <inttypes.h>
#include "hinic3_util.h"
#include "hinic3_ds.h"
#include "hinic3_capture_utils.h"

#define PCAP_FLAG_KEY_COUNT               (1LLU << 0)
#define PCAP_FLAG_KEY_FILE                (1LLU << 1)
#define PCAP_FLAG_KEY_DIRECTION           (1LLU << 2)
#define PCAP_FLAG_KEY_SIP                 (1LLU << 3)
#define PCAP_FLAG_KEY_DIP                 (1LLU << 4)
#define PCAP_FLAG_KEY_HOST                (1LLU << 5)
#define PCAP_FLAG_KEY_SMAC                (1LLU << 6)
#define PCAP_FLAG_KEY_DMAC                (1LLU << 7)
#define PCAP_FLAG_KEY_ETH_TYPE            (1LLU << 8)
#define PCAP_FLAG_KEY_IP_PROTO            (1LLU << 9)
#define PCAP_FLAG_KEY_SPORT               (1LLU << 10)
#define PCAP_FLAG_KEY_DPORT               (1LLU << 11)
#define PCAP_FLAG_KEY_VLAN                (1LLU << 12)
#define PCAP_FLAG_KEY_VXLAN_VNI           (1LLU << 13)
#define PCAP_FLAG_KEY_TIME                (1LLU << 14)
#define PCAP_FLAG_KEY_FILENUM             (1LLU << 15)
#define PCAP_FLAG_KEY_OUTPUT              (1LLU << 16)

#define PCAP_FILTER_ALL_MASK              (PCAP_FLAG_KEY_SIP |         \
                                           PCAP_FLAG_KEY_DIP |         \
                                           PCAP_FLAG_KEY_HOST |        \
                                           PCAP_FLAG_KEY_SMAC |        \
                                           PCAP_FLAG_KEY_DMAC |        \
                                           PCAP_FLAG_KEY_ETH_TYPE |    \
                                           PCAP_FLAG_KEY_IP_PROTO |    \
                                           PCAP_FLAG_KEY_SPORT |       \
                                           PCAP_FLAG_KEY_DPORT |       \
                                           PCAP_FLAG_KEY_VLAN |        \
                                           PCAP_FLAG_KEY_VXLAN_VNI)

#define PCAP_FILTER_NECESSARY_MASK        (PCAP_FLAG_KEY_FILE |       \
                                           PCAP_FLAG_KEY_TIME)

#define PCAP_DIR_RX                        1
#define PCAP_DIR_TX                       (1 << 1)

#define PCAP_PARAM_MAX_LEN                 512
#define PCAP_PARAM_STEP                    2

#define PCAP_DEF_PKT_CNT                   8000
#define PCAP_MAX_PKT_CNT                   1000000

#define PCAP_MAX_VLAN_NUM                  4096
#define PCAP_MAX_VLAN                      4095
#define PCAP_MAX_VNI                       0xFFFFFF

#define PCAP_MIN_TASK_TIME                 1
#define PCAP_MAX_TASK_TIME                 (24 * 60)

#define PCAP_OFFSET_32                     32
#define PCAP_PORT_LEN                      2

#define PCAP_MAGIC_NUMBER                  0xa1b2c3d4
#define PCAP_VERSION_MAJOR                 2
#define PCAP_VERSION_MINOR                 4
#define PCAP_SNAPLEN                       65535
#define PCAP_LINKTYPE                      1

#define PCAP_DOUBLE                        2
#define PCAP_VNI_TRANSFORM_OFFSET          8

#define PCAP_IPV6_FRAG_HDR_SIZE            8 /* ipv6 fragment header size is 8 */

enum pcap_cmp_mem_type {
    PCAP_CMP_MEM_BIT = 1,
    PCAP_CMP_MEM_BYTE
};

struct pcap_hdr {
    uint32_t magic_num;
    uint16_t ver_major;
    uint16_t ver_minor;
    int32_t local_zone;
    uint32_t acc_ts;            /* accuracy of timestamps */
    uint32_t max_pkt_len;       /* max length of captured packets */
    uint32_t link_type;         /* data link type */
};

struct pcap_pkt_l2 {
    uint8_t  smac[RTE_ETHER_ADDR_LEN];
    uint8_t  dmac[RTE_ETHER_ADDR_LEN];
    uint16_t vlan_id;
    uint16_t inner_vlan_id;
    uint16_t eth_type;
};

struct pcap_pkt_l3 {
    uint32_t ip_type;
    struct pcap_ip_t sip;
    struct pcap_ip_t dip;
    uint8_t  ip_proto;
};

struct pcap_pkt_l4 {
    uint16_t sport;
    uint16_t dport;
};

struct pcap_pkt_sub_hdr {
    uint32_t valid_flags;
    void *save_head;
    uint32_t save_len;
    struct pcap_pkt_l2 l2;
    struct pcap_pkt_l3 l3;
    struct pcap_pkt_l4 l4;
};

struct pcap_pkt_header {
    struct pcap_pkt_sub_hdr out_header;
    struct pcap_pkt_sub_hdr inner_header;
    bool is_vxlan;
    uint32_t vxlan_vni;
};

struct pcap_pkt_parse_ctx {
    uint8_t *pkt_itr;
    uint8_t *pkt_end;
    bool parse_result;
};

struct pcap_pkt_summary {
    void *data;
    enum PCAP_PKT_MEM_TYPE mem_type;
    uint32_t len;
    uint32_t pkt_len;
    uint64_t ol_flags;
    uint16_t vlan_tci;
    uint16_t vlan_id;
    struct pcap_pkt_parse_ctx ctx;
    struct pcap_pkt_header pkt_header;
};

typedef int (*pcap_sub_key_parse_func)(struct pcap_key_t *cap_key, const char *key_name, const char *value, struct ds *ds);

struct pcap_sub_key_parser {
    const char *key_name;
    size_t key_len;
    pcap_sub_key_parse_func func;
};

struct pcap_mem_cmp_t {
    uint32_t filter;
    uint8_t *src;
    uint8_t *dst;
    uint8_t  len;
    uint8_t  type;
};

struct pcap_int_value_cmp_t {
    uint32_t filter;
    uint32_t value1;
    uint32_t value2;
};

struct pcap_pkt_save_info {
    void *save_head;
    uint32_t save_len;
};

int parse_l4_proto(const char *value, uint8_t *output);
void pcap_convert_key_to_driver_filter(struct pcap_task_t *task);
FILE *pcap_file_open(const char *file_name, const char *mode);
int pcap_file_write_header(FILE *file);
int pcap_file_rotate(struct pcap_task_t *task);
int pcap_ip_mask_parse(const char *value, struct pcap_ip_t *ip, uint8_t *mask_len, uint32_t *p_ip_type);
int pcap_key_parse(struct pcap_key_t *cap_key, int argc, const char *argv[], struct ds *ds, struct ds *save_param);
struct pcap_pkt_sub_hdr *pcap_pkt_dst_hdr_get(struct pcap_pkt_summary *buf, const struct pcap_key_t *key);
bool pcap_pkt_filter(struct pcap_pkt_summary *buf, struct pcap_key_t *key);
void pcap_pkt_parse(struct pcap_pkt_summary *pkt_summary);
int pcap_rte_mempool_get(struct rte_mempool *mp, void **obj_p);
void pcap_rte_mempool_put(struct rte_mempool *mp, void *obj);
void pcap_rte_mempool_put_bulk(struct rte_mempool *mp, void * const *obj_table, unsigned int n);
unsigned int pcap_rte_ring_count(const struct rte_ring *r);
unsigned int pcap_rte_ring_dequeue_burst(struct rte_ring *r, void **obj_table, unsigned int n,
    unsigned int *available);
unsigned int pcap_rte_ring_enqueue_burst(struct rte_ring *r, void * const *obj_table, unsigned int n,
    unsigned int *free_space);
int pcap_show_param_parse(int argc, const char *argv[], struct pcap_show_param *param, struct ds *ds);
int pcap_stop_param_parse(int argc, const char *argv[], struct pcap_stop_param *param, struct ds *ds);

#endif /* HINIC3_CAPTURE_FILTER_H */
