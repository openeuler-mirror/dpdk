/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Huawei Technologies Co., Ltd
 */

#include "rte_log.h"
#include "rte_ethdev.h"
#include "hinic3_flow_agent.h"
#include "hinic3_eth_util.h"
#include "hinic3_packet_key_public.h"
#include "hinic3_flow_agent_public.h"
#include "hinic3_drv.h"
#include "hinic3_flow_dump.h"
#include "hinic3_flow_dump_action.h"
#include "hinic3_mpool_rte_flow.h"
#include "hinic3_check_thread_health_state.h"
#include "hinic3_ufid_map_rte_flow.h"
#include "hinic3_iface_flow_api_record.h"
#include "hinic3_meminfo.h"
#include "hinic3_flow_session.h"
#include "hinic3_agent_flow_cmd.h"
#include "hinic3_agent_flow_cmd_dump.h"
#include "hinic3_agent_cmd_format.h"
#include "hinic3_offload_flow.h"
#include "hinic3_iface_flow.h"

#define HINIC3_NO_FOUND_FLOW (-4)
uint64_t g_get_related_ufid;


struct hinic3_query_flow_buf {
    uint8_t *put_key_buf;
    uint8_t *actions_buf;
    uint8_t *get_key_buf;
};

static void hinic3_init_dpif_flow(struct hinic3_dpif_flow *put_flow, struct hinic3_nlattr *hinic3_get_key,
    struct hinic3_nlattr *hinic3_action)
{
    put_flow->key = (struct hinic3_nlattr_obj *)hinic3_get_key->data;
    put_flow->key_len = hinic3_get_key->used_len;
    put_flow->actions = (struct hinic3_nlattr_obj *)hinic3_action->data;
    put_flow->action_len = hinic3_action->used_len;
}

static int hinic3_init_query_mem(struct hinic3_query_flow_buf *buf)
{
    buf->put_key_buf = (uint8_t *)hinic3_calloc(1, HINIC3_MSG_MAX_BUF, HINIC3_FLOWS);
    if (buf->put_key_buf == NULL) {
        return -ENOMEM;
    }

    buf->actions_buf = (uint8_t *)hinic3_calloc(1, HINIC3_MSG_MAX_BUF, HINIC3_FLOWS);
    if (buf->actions_buf == NULL) {
        hinic3_free(buf->put_key_buf);
        return -ENOMEM;
    }

    buf->get_key_buf = (uint8_t *)hinic3_calloc(1, HINIC3_MSG_MAX_BUF, HINIC3_FLOWS);
    if (buf->get_key_buf == NULL) {
        hinic3_free(buf->put_key_buf);
        hinic3_free(buf->actions_buf);
        return -ENOMEM;
    }
    return 0;
}

static void hinic3_free_query_mem(struct hinic3_query_flow_buf *buf)
{
    if (buf->put_key_buf != NULL) {
        hinic3_free(buf->put_key_buf);
        buf->put_key_buf = NULL;
    }

    if (buf->actions_buf != NULL) {
        hinic3_free(buf->actions_buf);
        buf->actions_buf = NULL;
    }

    if (buf->get_key_buf != NULL) {
        hinic3_free(buf->get_key_buf);
        buf->get_key_buf = NULL;
    }
}

static void hinic3_flow_ouput(struct hinic3_dpif_flow *flow, struct ds *ds)
{
    struct hinic3_dpif_flow_for_get get_flow = {0};

    get_flow.key = flow->key;
    get_flow.key_len = flow->key_len;
    get_flow.action_len = flow->action_len;
    get_flow.actions = flow->actions;
    memcpy(&get_flow.mask, &flow->mask, sizeof(struct hinic3_nlattr_obj));
    get_flow.mask_len = flow->mask_len;
    get_flow.mask_present = flow->mask_present;
    get_flow.ol_ufid = flow->hw_ufid;
    get_flow.related_hw_ufid = g_get_related_ufid;
    get_flow.stats = flow->stats;

    hinic3_agent_dump_hinic3_flow_init();
    hinic3_agent_flow_format_output(HINIC3_HYDRA_TYPE_TABLE_START, &get_flow, ds);
}

int
hinic3_flow_query_ufid(const struct rte_flow_attr *attr HINIC3_UNUSED, const struct rte_flow_item *pattern,
    uint64_t *ufid, struct hinic3_conntrack_full_key *full_key, struct ds *ds)
{
    int ret;
    uint8_t has_vxlan_item;
    struct hinic3_nlattr hinic3_put_key;
    struct hinic3_nlattr hinic3_get_key;
    struct hinic3_nlattr hinic3_action;
    struct hinic3_dpif_flow put_flow = { 0 };
    struct hinic3_query_flow_buf buf;

    ret = hinic3_init_query_mem(&buf);
    if (ret != 0) {
        return -ENOMEM;
    }

    full_key->key.meta.key_len = sizeof(struct hinic3_conntrack_full_key) - sizeof(struct hinic3_conntrack_key);
    hinic3_nlattr_init(&hinic3_put_key, buf.put_key_buf, HINIC3_MSG_MAX_BUF);
    hinic3_nlattr_init(&hinic3_action, buf.actions_buf, HINIC3_MSG_MAX_BUF);
    hinic3_nlattr_init(&hinic3_get_key, buf.get_key_buf, HINIC3_MSG_MAX_BUF);

    hinic3_init_dpif_flow(&put_flow, &hinic3_get_key, &hinic3_action);
    ret = hinic3_offload_parse_key(pattern, full_key, &has_vxlan_item);
    if (ret != 0) {
        goto err;
    }
    hinic3_offload_flow_construct_key_entrance(&full_key->key, &hinic3_put_key);
    ret = hinic3_flow_mgmt_get_by_key(hinic3_put_key.data, hinic3_put_key.used_len, &put_flow, &g_get_related_ufid);
    if (ret == 0) {
        *ufid = put_flow.hw_ufid;
        if (ds != NULL) {
            hinic3_flow_ouput(&put_flow, ds);
        }

        hinic3_free_query_mem(&buf);
        return 0;
    }

    if (ret == HINIC3_NO_FOUND_FLOW) {
        hinic3_free_query_mem(&buf);
        return -EPERM;
    }
err:
    hinic3_free_query_mem(&buf);
    return -EPERM;
}
