/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_SET_USERDATA_H
#define HINIC3_SET_USERDATA_H

#include "hinic3_packets_types.h"

struct hinic3_userdata {
    uint32_t dynfield0;

    union {
        void *userdata;   /**< Can be used for external metadata */
        uint64_t udata64; /**< Allow 8-byte userdata on 32-bit */
    };

    uint32_t internal;    /**< internal use mbuf, evs set it to 0 */
};

#define LAYER_FLAG_TH           0x7

int hinic3_rte_mbuf_dynfield_register(void);
void hinic3_set_vport_id_into_userdata(uint16_t vport_id, struct rte_mbuf **tx_pkts, uint16_t nb_pkts);

#endif
