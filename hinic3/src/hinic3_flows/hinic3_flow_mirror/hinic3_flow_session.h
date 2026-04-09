 /* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_FLOW_SESSION_H
#define HINIC3_FLOW_SESSION_H

#include "rte_byteorder.h"
#include "hinic3_message.h"
#include "hinic3_command.h"
#include "hinic3_mutex.h"
#include "hinic3_mpool_rte_flow.h"
#include "hinic3_offload_flow_public.h"
#include "hinic3_flow_agent_public.h"
#include "hinic3_packet_key_public.h"
#include "hinic3_packets_types.h"

#define HINIC3_MIRROR_MAX_SESSION 128u

enum hinic3_session_flow_type {
    HINIC3_FLOW_NO_MIRROR,
    HINIC3_FLOW_EMC_MIRROR,
    HINIC3_FLOW_ACL_MIRROR,
    HINIC3_FLOW_MEGA_MIRROR,
};

enum hinic3_session_direction {
    HINIC3_SESSION_RX,
    HINIC3_SESSION_TX,
};

enum hinic3_session_tpye {
    HINIC3_EMC_VXLAN_SESSION = 1,
    HINIC3_EMC_GRE_SESSION = 1 << 1,
    HINIC3_ACL_GRE_SESSION = 1 << 2,
    HINIC3_MEGA_GRE_SESSION = 1 << 3,
    HINIC3_EMC_VXLAN_GPE_SHIM = 1 << 4,
};

struct session_key {
    uint8_t cutoff_len; // 镜像报文截断长度，0代表不截断

    uint32_t outer_port_id;

    uint8_t smac[ETH_ALEN];
    uint8_t dmac[ETH_ALEN];
    rte_be16_t type; // eth_type

    rte_be16_t tci;
    rte_be16_t inner_type;

    uint32_t sip[4];
    uint32_t dip[4];
    uint8_t proto; // ip_next_proto
    union {
        struct {
            uint8_t flags;
            uint8_t rsvd1;
            uint8_t vxlan_rsvd0[3];
            uint8_t vni[3];

            rte_be16_t src_port;
            rte_be16_t dst_port;
        } vxlan;
        struct {
            rte_be16_t c_rsvd0_ver;
            rte_be16_t protocol;
        } gre;
        struct {
            rte_be16_t src_port;
            rte_be16_t dst_port;

            uint8_t flags;
            uint8_t rsvd1;
            uint8_t next_protocol;
            uint8_t vxlan_rsvd0[2];
            uint8_t vni[3];

            uint8_t shim_type;
            uint8_t shim_len; // shim_header总长度,12/8 byte
            uint32_t data[5]; // shim内容
        } vxlan_gpe_shim;
    } tunnel;
};

struct hinic3_mirror_session_info {
    uint8_t is_used;
    uint8_t session_id;
    enum hinic3_session_direction direction;
    uint32_t sampling_interval;
    struct session_key key;
    int flow_list_len; // 会话绑定流表数量

    enum hinic3_session_tpye type; // 流表对应的隧道类型
    uint32_t ip_type_flag; // 检查报文头类型
};

struct hinic3_mirror_session_info_list {
    struct hinic3_mirror_session_info session_info[HINIC3_MIRROR_MAX_SESSION];
    struct hinic3_spinlock mutex;
    uint8_t used_session_len;
};

struct hinic3_mirror_pkt_l2 {
    uint8_t dmac[ETH_ALEN];
    uint8_t smac[ETH_ALEN];
    hinic3_be16 eth_type;
}__rte_packed;

struct hinic3_mirror_pkt_l3 {
	uint8_t  type_of_service : 4;	/**< type of service */
    uint8_t  ihl : 4;   /**< IP_hdr length */
    uint8_t  dscp : 6;  /**< Qos priority */
    uint8_t  ecn : 2;  /**< 网络拥塞反馈 */
	rte_be16_t total_length;	/**< length of packet */
	rte_be16_t packet_id;		/**< packet ID */
	rte_be16_t fragment_offset;	/**< fragmentation offset */
	uint8_t  time_to_live;		/**< time to live */
	uint8_t  next_proto_id;		/**< protocol ID */
	rte_be16_t hdr_checksum;	/**< header checksum */
	rte_be32_t src_addr;		/**< source address */
	rte_be32_t dst_addr;
}__rte_packed;

struct hinic3_mirror_vxlan_gpe{
    uint8_t flags; /**< Normally 0x08 (I flag). */
    uint8_t rsvd0[2]; /**< Reserved, normally 0x000000. */
    uint8_t protocol; /**< Next_protocol. */
    uint8_t vni[3]; /**< VXLAN identifier. */
    uint8_t rsvd1; /**< Reserved, normally 0x00. */
}__rte_packed;
struct hinic3_shim_info {
    uint8_t type;
    uint8_t len;
    uint8_t rsv1 : 7;
    uint8_t flag_D : 1;
    uint8_t next_protocol;
    uint32_t data[2];
} __rte_packed;

void ovs_mutex_lock_session(void);
void ovs_mutex_unlock_session(void);
int hinic3_session_flush_all(uint32_t type);
void hinic3_mirror_session_info_list_init(void);
int hinic3_del_rte_flow_in_session(struct rte_flow *flow);
enum hinic3_session_tpye hinic3_get_session_type(uint8_t session_id);
struct rte_flow_action_vxlan_encap *hinic3_flow_dump_sample_nvgre_encap_conf(uint8_t session_id);
int hinic3_deal_with_session_info(struct hinic3_mirror_session_info *session_info, struct rte_flow *flow,
    uint8_t mirror_dir_flag);
int hinic3_flow_get_port_id_by_session(uint8_t session_id, uint16_t *port_id);
int hinic3_flow_dump_construct_vxlan_header(uint8_t session_id, struct hinic3_flow_act_vxlan_gpe_header *meta_header);
void hinic3_show_sample_session(struct unixctl_conn *conn, int argc, const char *argv[], void *aux);
struct hinic3_mirror_session_info_list *hinic3_get_mirror_session_info(void);

#endif
