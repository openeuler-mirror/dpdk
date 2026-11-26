/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026-2027 Huawei Technologies Co., Ltd
 */

#include <errno.h>
#include <string.h>
#include <stdlib.h>

#include <rte_eal.h>
#include <rte_string_fns.h>

#include "hns5_eth.h"
#include "hns5_eth_rxtx.h"
#include "hns5_mp.h"
#include "hns5_log.h"

#define HNS5_MP_REQ_TIMEOUT_SEC 5
/* A secondary may exit mid-request: time the stop out instead of blocking. */
#define HNS5_MP_STOP_TIMEOUT_MS 500

/* per-process state, refcounted over the eth devices of this process */
static struct {
    uint16_t eth_dev_cnt;
    bool init_done;
} hns5_mp_local;

static inline void
hns5_mp_init_msg(struct rte_eth_dev *dev, struct rte_mp_msg *msg,
    enum hns5_mp_req_type type)
{
    struct hns5_mp_param *param = (struct hns5_mp_param *)msg->param;

    memset(msg, 0, sizeof(*msg));
    strlcpy(msg->name, HNS5_MP_NAME, sizeof(msg->name));
    msg->len_param = sizeof(*param);
    param->type = type;
    param->port_id = dev->data->port_id;
}

static int
hns5_mp_primary_handle(const struct rte_mp_msg *mp_msg __rte_unused,
    const void *peer __rte_unused)
{
    return 0;
}

static uint16_t
hns5_mp_dummy_rx_burst(void *rx_queue __rte_unused,
    struct rte_mbuf **rx_pkts __rte_unused, uint16_t nb_pkts __rte_unused)
{
    return 0;
}

static uint16_t
hns5_mp_dummy_tx_burst(void *tx_queue __rte_unused,
    struct rte_mbuf **tx_pkts __rte_unused, uint16_t nb_pkts __rte_unused)
{
    return 0;
}

void hns5_mp_arm_datapath(struct rte_eth_dev *dev)
{
    dev->rx_pkt_burst = hns5_eth_rx_burst;
    dev->tx_pkt_burst = hns5_eth_tx_burst;
}

void hns5_mp_disarm_datapath(struct rte_eth_dev *dev)
{
    dev->rx_pkt_burst = hns5_mp_dummy_rx_burst;
    dev->tx_pkt_burst = hns5_mp_dummy_tx_burst;
}

static int
hns5_mp_secondary_handle(const struct rte_mp_msg *mp_msg, const void *peer)
{
    struct rte_mp_msg mp_res = { 0 };
    struct hns5_mp_param *res = (struct hns5_mp_param *)mp_res.param;
    const struct hns5_mp_param *param =
        (const struct hns5_mp_param *)mp_msg->param;
    struct rte_eth_dev *dev;
    int ret;

    /* Public channel: trust the parameter block only when it was really sent. */
    if (mp_msg->len_param < (int)sizeof(*param)) {
        hns5_log_error("invalid mp request, len_param %d\n", mp_msg->len_param);
        strlcpy(mp_res.name, HNS5_MP_NAME, sizeof(mp_res.name));
        mp_res.len_param = sizeof(*res);
        res->result = EINVAL;
        return rte_mp_reply(&mp_res, peer);
    }

    if (!rte_eth_dev_is_valid_port(param->port_id)) {
        /* Port already closed here: reply success so the primary does not time out. */
        if (param->type == HNS5_MP_REQ_LSC_EVENT)
            return 0;   /* fire and forget, nothing to reply */
        hns5_log_info("port %u already closed, reply mp request type %d\n",
            param->port_id, param->type);
        strlcpy(mp_res.name, HNS5_MP_NAME, sizeof(mp_res.name));
        mp_res.len_param = sizeof(*res);
        res->type = param->type;
        res->port_id = param->port_id;
        res->result = 0;
        return rte_mp_reply(&mp_res, peer);
    }
    dev = &rte_eth_devices[param->port_id];
    switch (param->type) {
    case HNS5_MP_REQ_START_RXTX:
        hns5_log_info("port %u starting datapath\n", dev->data->port_id);
        hns5_mp_arm_datapath(dev);
        break;
    case HNS5_MP_REQ_STOP_RXTX:
        hns5_log_info("port %u stopping datapath\n", dev->data->port_id);
        hns5_mp_disarm_datapath(dev);
        break;
    case HNS5_MP_REQ_LSC_EVENT:
        /* Primary is the only link sensor: just report the status carried here. */
        hns5_log_info("port %u link status changed to %s\n",
            dev->data->port_id,
            (param->link_status == ETH_LINK_UP) ? "up" : "down");
        return 0;
    default:
        /* Reply an error so the primary does not wait for the full timeout */
        hns5_log_error("port %u invalid mp request type %d\n",
            dev->data->port_id, param->type);
        hns5_mp_init_msg(dev, &mp_res, param->type);
        res->result = EINVAL;
        return rte_mp_reply(&mp_res, peer);
    }
    hns5_mp_init_msg(dev, &mp_res, param->type);
    res->result = 0;
    ret = rte_mp_reply(&mp_res, peer);
    return ret;
}

static void
hns5_mp_set_timeout(struct timespec *ts, uint32_t timeout_ms)
{
    ts->tv_sec = timeout_ms / 1000;
    ts->tv_nsec = (long)(timeout_ms % 1000) * 1000 * 1000;
}

/* Broadcast to all secondaries; missing replies are judged by the caller. */
static int
hns5_mp_broadcast(struct rte_eth_dev *dev, enum hns5_mp_req_type type,
    uint32_t timeout_ms)
{
    struct rte_mp_msg mp_req;
    struct rte_mp_msg *mp_res;
    struct rte_mp_reply mp_rep;
    struct hns5_mp_param *res;
    struct timespec ts;
    int ret;
    int i;

    hns5_mp_init_msg(dev, &mp_req, type);
    hns5_mp_set_timeout(&ts, timeout_ms);
    ret = rte_mp_request_sync(&mp_req, &mp_rep, &ts);
    if (ret != 0) {
        hns5_err(dev, "failed to request start/stop rxtx (type %d)\n", type);
        return (rte_errno != 0) ? -rte_errno : -EIO;
    }
    if (mp_rep.nb_sent != mp_rep.nb_received) {
        hns5_err(dev, "not all secondaries responded (type %d)\n", type);
        free(mp_rep.msgs);
        return -ETIMEDOUT;
    }
    for (i = 0; i < mp_rep.nb_received; i++) {
        mp_res = &mp_rep.msgs[i];
        res = (struct hns5_mp_param *)mp_res->param;
        if (res->result) {
            hns5_err(dev, "start/stop rxtx failed on secondary #%d\n", i);
            free(mp_rep.msgs);
            return -EIO;
        }
    }
    free(mp_rep.msgs);
    return 0;
}

static int
hns5_mp_req_on_rxtx(struct rte_eth_dev *dev, enum hns5_mp_req_type type)
{
    uint32_t timeout_ms;
    int ret;

    if (hns5_is_secondary())
        return 0;

    timeout_ms = (type == HNS5_MP_REQ_STOP_RXTX) ?
        HNS5_MP_STOP_TIMEOUT_MS : HNS5_MP_REQ_TIMEOUT_SEC * 1000;
    ret = hns5_mp_broadcast(dev, type, timeout_ms);

    if (ret == -ETIMEDOUT && type == HNS5_MP_REQ_STOP_RXTX) {
        hns5_info(dev, "secondary did not answer stop, assume exiting\n");
        return 0;
    }
    return ret;
}

int hns5_mp_req_start_rxtx(struct rte_eth_dev *dev)
{
    return hns5_mp_req_on_rxtx(dev, HNS5_MP_REQ_START_RXTX);
}

int hns5_mp_req_stop_rxtx(struct rte_eth_dev *dev)
{
    return hns5_mp_req_on_rxtx(dev, HNS5_MP_REQ_STOP_RXTX);
}

int hns5_mp_notify_lsc_event(struct rte_eth_dev *dev, uint32_t link_status)
{
    struct rte_mp_msg mp_req;
    struct hns5_mp_param *param;

    if (hns5_is_secondary())
        return 0;

    /* Fire and forget from lsc context: never block on busy or exiting peers. */
    hns5_mp_init_msg(dev, &mp_req, HNS5_MP_REQ_LSC_EVENT);
    param = (struct hns5_mp_param *)mp_req.param;
    param->link_status = link_status;
    if (rte_mp_sendmsg(&mp_req) != 0)
        return (rte_errno != 0) ? -rte_errno : -EIO;
    return 0;
}

int hns5_mp_init_primary(struct rte_eth_dev *dev)
{
    int ret;

    if (hns5_mp_local.init_done) {
        hns5_mp_local.eth_dev_cnt++;
        return 0;
    }
    /* primary is allowed to not support IPC */
    ret = rte_mp_action_register(HNS5_MP_NAME, hns5_mp_primary_handle);
    if (ret && rte_errno != ENOTSUP)
        return ret;
    hns5_mp_local.init_done = true;
    hns5_mp_local.eth_dev_cnt++;
    RTE_SET_USED(dev);
    return 0;
}

int hns5_mp_init_secondary(struct rte_eth_dev *dev)
{
    int ret;

    if (hns5_mp_local.init_done) {
        hns5_mp_local.eth_dev_cnt++;
        return 0;
    }
    ret = rte_mp_action_register(HNS5_MP_NAME, hns5_mp_secondary_handle);
    if (ret)
        return ret;
    hns5_mp_local.init_done = true;
    hns5_mp_local.eth_dev_cnt++;
    RTE_SET_USED(dev);
    return 0;
}

void hns5_mp_uninit(struct rte_eth_dev *dev)
{
    RTE_SET_USED(dev);
    /* Secondary keeps the action until exit so late stop requests still get a reply. */
    if (hns5_is_secondary())
        return;
    if (!hns5_mp_local.init_done)
        return;
    if (hns5_mp_local.eth_dev_cnt > 0)
        hns5_mp_local.eth_dev_cnt--;
    if (hns5_mp_local.eth_dev_cnt == 0) {
        rte_mp_action_unregister(HNS5_MP_NAME);
        hns5_mp_local.init_done = false;
    }
}
