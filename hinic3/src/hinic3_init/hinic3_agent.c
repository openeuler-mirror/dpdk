/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */

#include "rte_log.h"
#include "rte_pmd_hinic3.h"
#include "hinic3_util.h"
#include "hinic3_init.h"
#include "hinic3_iface_global.h"
#include "hinic3_flow_agent.h"
#include "hinic3_set_userdata.h"
#include "hinic3_log.h"
#include "hinic3_util.h"
#include "hinic3_eth_util.h"
#include "hinic3_capture_main.h"
#include "hinic3_check_thread_health_state.h"
#include "hinic3_mtr.h"
#include "hinic3_meminfo.h"
#include "hinic3_hugepage_meminfo.h"
#include "hinic3_smap.h"
#include "hinic3_string_util.h"
#include "hinic3_vxlan.h"
#include "hinic3_command.h"
#include "hinic3_ds.h"
#include "hinic3_mutex.h"
#include "hinic3_packets.h"
#include "hinic3_vf_port_qos_public.h"
#include "hinic3_string_util.h"
#include "hinic3_thread.h"
#include "hinic3_smap.h"
#include "hinic3_hmap.h"
#include "hinic3_shash.h"
#include "hinic3_unaligned.h"
#include "hinic3_parse_agent_config.h"
#include "hinic3_vdev.h"
#include "hinic3_offload_flow_port.h"
#include "hinic3_flow_dump.h"
#include "hinic3_flow_session.h"
#include "hinic3_mpool_rte_flow.h"
#include "hinic3_agent_cmd.h"
#include "hinic3_error_stats.h"
#include "hinic3_vf_port_qos_public.h"
#include "hinic3_mega_offload.h"
#include "hinic3_agent.h"

static bool g_agent_construct_init = false;

bool hinic3_get_agent_construct_init(void)
{
    return g_agent_construct_init;
}

void hinic3_set_agent_construct_init(bool value)
{
        g_agent_construct_init = value;
}

struct rte_cfgfile *hinic3_cfgfile_load(const char *cfg_file)
{
    if (cfg_file == NULL) {
        return NULL;
    }

    return rte_cfgfile_load(cfg_file, 0);
}

int hinic3_is_softlink(const char *cfg_file)
{
    int len = 0;
    char file_buf[PATH_MAX] = {0};

    len = readlink(cfg_file, file_buf, PATH_MAX);
    if (len > 0) {
        HINIC3_LOG(ERR, AGENT, "failed to init agent info dur to %s is a softlink file.", cfg_file);
        return -1;
    }
    return 0;
}

void hinic3_solution_vitrio_wait(void)
{
    int ret;
    FILE *fp = NULL;
    char net_ready_path[] = "/proc/sdi_net/net_ready";
    char value[] = "1";

    fp = fopen(net_ready_path, "w+");
    if (fp == NULL) {
        HINIC3_LOG(ERR, AGENT, "can not open net ready file");
        return;
    }

    ret = fputs(value, fp);
    if (ret <= 0) {
        HINIC3_LOG(ERR, AGENT, "can not wirte net ready file, %d", ret);
    } else {
        HINIC3_LOG(INFO, AGENT, "solution vitrio net wait success, %d", ret);
    }

    (void)fclose(fp);
    return;
}

int hinic3_agent_init_mpool(void)
{
    int ret;

    ret = hinic3_init_rte_flow_mpool();

    return ret;
}

int hinic3_pre_init(void)
{
    int ret;

    ret = hinic3_log_init();
    if (ret != 0) {
        perror("Agent_construct: log system init failed.");
        return -1;
    }

    (void)hinic3_meminfo_init();
    ret = hinic3_hugepage_meminfo_init();
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "Agent_construct: hugepage_meminfo_init failed, ret is %d.", ret);
        return -1;
    }
    (void)hinic3_dev_init();
    ret = hinic3_ops_init();
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "Agent_construct: ops init failed, ret is %d.", ret);
        return -1;
    }

    (void)hinic3_command_hmap_init();
    (void)hinic3_show_version();

    ret = hinic3_driver_class_init();
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "Agent_construct: adaptor class init failed, ret is %d.", ret);
        return -1;
    }

    ret = hinic3_agent_init_mpool();
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "Agent_construct: mpool init failed, ret is %d.", ret);
        return -1;
    }

    (void)hinic3_rte_mbuf_dynfield_register();
    return 0;
}

void hinic3_pre_uninit(void)
{
    hinic3_uninit_rte_flow_mpool();
    hinic3_driver_class_uninit();
    hinic3_dev_uninit();
    hinic3_hugepage_meminfo_uninit();
    hinic3_meminfo_uninit();
}

int hinic3_driver_init(void)
{
    int ret;
    (void)hinic3_driver_log_init();
    ret = hinic3_pf_vdev_enable();
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "Agent_construct: pf enable failed, ret is %d.", ret);
        return -1;
    }

    if (hinic3_device_mode_get() == DPU_MODE) {
        (void)hinic3_solution_vitrio_wait();
    }

    (void)hinic3_global_cfg_set_when_restart();

    return 0;
}

int hinic3_flow_module_init(void)
{
    int ret;

    ret = hinic3_flow_agent_construct();
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "Agent_construct: flow agent construct failed, ret is %d.", ret);
        return -1;
    }

    if (hinic3_check_fuzzy_flow_switch() == true) {
        ret = hinic3_mega_flow_init();
        if (ret != 0)
            return ret;
    }

    (void)hinic3_mirror_session_info_list_init();
    (void)hinic3_dump_flow_init();

    return 0;
}

int hinic3_components_init(void)
{
    int ret;

    ret = hinic3_flow_module_init();
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "Agent_construct: flow init failed, ret is %d.", ret);
        return -1;
    }
    ret = hinic3_pcap_init();
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "Agent_construct: pcap init failed with %d", ret);
        return -1;
    }

    (void)hinic3_qos_init();
    (void)hinic3_ifindex_port_map_init();

    return 0;
}

void hinic3_components_uninit(void)
{
    (void)hinic3_flow_agent_destruct();
    (void)hinic3_pcap_uninit();
    (void)hinic3_ifindex_port_map_uninit();
}

int hinic3_post_init(void)
{
    int ret;

    (void)hinic3_agent_cmd_init();
    ret = hinic3_command_mgr_init();
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "Agent_construct: command mgr init failed with %d", ret);
        return -1;
    }

    ret = hinic3_polling_thread_init();
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "Agent_construct: check thread init failed with %d", ret);
        return -1;
    }
    if (hinic3_card_mod_get() != PROG_MODE) {

        ret = hinic3_forward_mode_init();
        if (ret != 0) {
            HINIC3_LOG(ERR, AGENT, "Agent_construct: set forward mode failed");
            return -1;
        }

        ret = hinic3_bond_hash_policy_init();
        if (ret != 0) {
            HINIC3_LOG(ERR, AGENT, "Agent_construct: set hash policy failed");
            return -1;
        }
    }

    g_agent_construct_init = true;
    return hinic3_vdev_port_module_init();
}

void hinic3_post_uninit(void)
{
    hinic3_vdev_port_module_uninit();
    g_agent_construct_init = false;
    hinic3_polling_thread_uninit();
    hinic3_command_mgr_uninit();
}

int hinic3_agent_construct(void)
{
    int ret;

    ret = hinic3_pre_init();
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "Agent_construct: pre init failed, ret is %d.", ret);
        goto pre_err;
    }

    ret = hinic3_driver_init();
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "Agent_construct: driver init failed, ret is %d.", ret);
        goto pre_err;
    }

    ret = hinic3_components_init();
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "Agent_construct: components init failed, ret is %d.", ret);
        goto components_err;
    }

    ret = hinic3_post_init();
    if (ret != 0) {
        HINIC3_LOG(ERR, AGENT, "Agent_construct: post init failed, ret is %d.", ret);
        goto post_err;
    }

    HINIC3_LOG(INFO, AGENT, "hinic3 agent construct finish, status: %u", g_agent_construct_init);
    return 0;

post_err:
    hinic3_post_uninit();
components_err:
    hinic3_components_uninit();
pre_err:
    hinic3_pre_uninit();
    return -1;
}

void hinic3_agent_destruct(void)
{
    hinic3_net_qos_clear();
    hinic3_post_uninit();
    hinic3_components_uninit();
    hinic3_pre_uninit();
}
