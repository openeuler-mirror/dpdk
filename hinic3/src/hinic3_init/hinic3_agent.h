/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_AGENT_H
#define HINIC3_AGENT_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AGENT_CFG_FILE                            "/etc/dpak/net/agent_config.ini"
#define HINIC3_AGENT_COMPONENT_NAME                "Component: dpak-runtime"
#define HINIC3_AGENT_FEATURE_NAME                  "Feature:   dpak-libovs"
#define HINIC3_COMMAND_FEATURE_NAME                "Feature:   dpak-libovs/dpak-ovs-ctl"
#define HINIC3_AGENT_COMPONENT_VERSION             "Version:   "
#define HINIC3_AGENT_BUILD_VERSION                 "Sub version: "
#define HINIC3_AGENT_PCI_NULL                      "\"\""

#define DP_PACKET_CONTEXT_SIZE 64

enum dp_packet_source {
    DPBUF_MALLOC,
    DPBUF_STACK,
    DPBUF_STUB,
    DPBUF_DPDK,
    DPBUF_AFXDP,
    DPBUF_18V = 0x5AA500FF,
};

struct dp_packet {
    char rte_mbuf[128];
    enum dp_packet_source source;
    uint16_t l2_pad_size;
    uint16_t l2_5_ofs;
    uint16_t l3_ofs;
    uint16_t l4_ofs;
    uint32_t cutlen;
    uint32_t packet_type;
    union {
        char pkt_metadata[512];
        uint64_t data[DP_PACKET_CONTEXT_SIZE / 8];
    };
};

int hinic3_is_softlink(const char *cfg_file);
struct rte_cfgfile *hinic3_cfgfile_load(const char *cfg_file);
bool hinic3_get_agent_construct_init(void);
void hinic3_set_agent_construct_init(bool value);
int hinic3_pre_init(void);
void hinic3_pre_uninit(void);
int hinic3_agent_init_mpool(void);
int hinic3_driver_init(void);
int hinic3_components_init(void);
void hinic3_components_uninit(void);
int hinic3_post_init(void);
void hinic3_post_uninit(void);
int hinic3_flow_module_init(void);
void hinic3_solution_vitrio_wait(void);

#ifdef __cplusplus
}
#endif

#endif
