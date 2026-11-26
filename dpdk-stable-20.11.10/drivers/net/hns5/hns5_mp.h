/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026-2027 Huawei Technologies Co., Ltd
 */

#ifndef _HNS5_MP_H_
#define _HNS5_MP_H_

#include <stdint.h>

#include <rte_ethdev.h>

/* multi-process IPC channel, primary <-> secondary processes */
#define HNS5_MP_NAME "net_hns5_mp"

enum hns5_mp_req_type {
    HNS5_MP_REQ_START_RXTX,   /* enable rx/tx burst in secondary */
    HNS5_MP_REQ_STOP_RXTX,    /* disable rx/tx burst in secondary */
    HNS5_MP_REQ_LSC_EVENT,    /* link status changed, fire the event in secondary */
};

struct hns5_mp_param {
    enum hns5_mp_req_type type;
    uint16_t port_id;
    int result;
    uint32_t link_status;     /* HNS5_MP_REQ_LSC_EVENT: ETH_LINK_UP/DOWN */
};

int hns5_mp_init_primary(struct rte_eth_dev *dev);
int hns5_mp_init_secondary(struct rte_eth_dev *dev);
void hns5_mp_uninit(struct rte_eth_dev *dev);

/* primary only: broadcast start/stop of the data path to secondaries */
int hns5_mp_req_start_rxtx(struct rte_eth_dev *dev);
int hns5_mp_req_stop_rxtx(struct rte_eth_dev *dev);

/* Primary only: fire-and-forget LSC broadcast to secondaries. */
int hns5_mp_notify_lsc_event(struct rte_eth_dev *dev, uint32_t link_status);

/* Process-local: arm real burst callbacks, or dummy ones callable while running. */
void hns5_mp_arm_datapath(struct rte_eth_dev *dev);
void hns5_mp_disarm_datapath(struct rte_eth_dev *dev);

#endif /* _HNS5_MP_H_ */
