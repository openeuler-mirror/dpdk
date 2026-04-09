/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */

#include <unistd.h>
#include <string.h>
#include "rte_cycles.h"
#include "rte_errno.h"
#include "rte_mempool.h"
#include "rte_mbuf.h"
#include "rte_mbuf_dyn.h"
#include "rte_distributor.h"
#include "rte_string_fns.h"

#include "hinic3_message.h"
#include "hinic3_util.h"
#include "hinic3_offload_flow.h"
#include "hinic3_port_util.h"
#include "hinic3_bond_detect.h"
#include "hinic3_iface_global.h"
#include "hinic3_flow_qos.h"
#include "hinic3_set_userdata.h"

#define HINIC3_METER_RTE_MBUF_GET_PTR 8

/*
 * 功能描述 : 在rte_mbuf中注册udata64
 * 返 回 值 : -1: 代表注册失败，大于等于0: 分配的偏移
 */
int hinic3_rte_mbuf_dynfield_register(void)
{
    int offset;

    const struct rte_mbuf_dynfield dynfield = {
        .name = "hinic3_userdata",
        .size = sizeof(struct hinic3_userdata),
        .align = __alignof__(struct hinic3_userdata),
        .flags = 0,
    };

    offset = rte_mbuf_dynfield_register(&dynfield);
    return offset;
}

void hinic3_set_vport_id_into_userdata(uint16_t vport_id, struct rte_mbuf **tx_pkts, uint16_t nb_pkts)
{
    uint16_t i;
    struct hinic3_pkt_user_data *hinic3_metadata = NULL;
    struct hinic3_packet **buffer = (struct hinic3_packet **)tx_pkts;
    struct hovs_mtr_user_data *hovs_mtr_data = NULL;
    struct mtr_info_node *mtr_info = NULL;
    struct rte_flow_action_meter *mtr = NULL;
    uint32_t mtr_num;

    if (nb_pkts == 0)
        return;

    for (i = 0; i < nb_pkts; i++)
    {
        hinic3_metadata = (struct hinic3_pkt_user_data *)(&tx_pkts[i]->dynfield1[1]);
        if (hinic3_metadata == NULL)
            continue;

        hinic3_metadata->vport_id = vport_id;
        /* Transparent packets priority to hardware */
        hinic3_metadata->l3_type_cos = (buffer[i]->md.entity_id & LAYER_FLAG_TH);
        hinic3_metadata->traffic_type = HINIC3_TRAFFIC_FROM_HOST_FALLBACK;
        if (HINIC3_UNLIKELY(hinic3_support_bond_detect_get())) {
            if (hinic3_is_bond_by_prefix(vport_id) == true)
                hinic3_bond_detect_process(vport_id, tx_pkts[i], hinic3_metadata);
        }

        mtr = (struct rte_flow_action_meter *)(&tx_pkts[i]->dynfield1[HINIC3_METER_RTE_MBUF_GET_PTR]);
        if (mtr == NULL || mtr->mtr_id == 0)
            continue;

        mtr_num = mtr->mtr_id - 1;
        mtr_info = hinic3_mtr_node_lookup(mtr_num);
        if (mtr_info != NULL)
        {
            hovs_mtr_data = (struct hovs_mtr_user_data *)mtr;
            hovs_mtr_data->flow_qos_en = 1;
            hovs_mtr_data->flow_qos_id = mtr_info->info.qos_id;
            // 流表级qos_type变更，变更前：0，bps；1，pps，变更后：1，bps；2，pps；3，bps+pps
            hovs_mtr_data->flow_qos_type = mtr_info->info.packet_mode + 1;
        }
    }
}