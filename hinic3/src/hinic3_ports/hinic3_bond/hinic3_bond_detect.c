/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2025 Huawei Technologies Co., Ltd
 */

#include <netinet/in.h>
#include "rte_mbuf.h"
#include "hinic3_tlv_key.h"
#include "hinic3_smap.h"
#include "hinic3_error_stats.h"
#include "hinic3_bond_controller.h"
#include "hinic3_bond_detect.h"

#define HINIC3_SLAVE_INVALID_PREFIX      0x0000
#define HINIC3_SLAVE_SELECT_PREFIX       0xA000
#define HINIC3_PORT_ID_MASK              0x0FFF
#define HINIC3_SLAVE_PPORT_ID_PTR        6

static struct hinic3_bond_slave_pci_map g_hinic3_bond_slaves_array[HINIC3_BOND_SLAVE_NUM];

inline static bool
hinic3_bond_detect_pci_addr_equal(struct rte_pci_addr *pci_addr, union hinic3_bond_detect_field *field)
{
    return pci_addr->bus == field->field.bus &&
        pci_addr->devid == field->field.device &&
        pci_addr->function == field->field.function;
}

static void
hinic3_bond_detect_init(char bond_slave_pcis[][HINIC3_BOND_ARG_SLAVE_PCI_LEN])
{
    int ret;
    for (int i = 0; i < HINIC3_BOND_SLAVE_NUM; i++) {
        struct rte_pci_addr pci_addr;
        g_hinic3_bond_slaves_array[i].valid = false;
        ret = rte_pci_addr_parse(bond_slave_pcis[i], &pci_addr);
        if (ret != 0) {
            HINIC3_LOG(ERR, VPORT, "hwoff bond detect init : parse pci failed!");
            continue;
        }

        memcpy(&g_hinic3_bond_slaves_array[i].pci_addr, &pci_addr, sizeof(struct rte_pci_addr));
        g_hinic3_bond_slaves_array[i].slave_id = i;
        g_hinic3_bond_slaves_array[i].valid = true;
    }
}

/**
 * 该函数用于分割pci列表字符串，输入参数格式如下
 * slaves: "0,1,2,3"
 * slaves_pci: "0000:81:00.0,0000:81:00.1,0000:81:00.2,0000:81:00.3"
 * 通过该函数处理后，将slaves对应的pci号放到输出数组的指定索引中
 * bond_slave_pci: ["0000:81:00.0","0000:81:00.1","0000:81:00.2","0000:81:00.3"]
 */
static int
hinic3_split_slave_pci(char *slaves, char *slaves_pci,
    char bond_slave_pci[][HINIC3_BOND_ARG_SLAVE_PCI_LEN], int len)
{
    int j = 0;
    int slaves_num = 0;
    int slaves_pci_num = 0;
    char *slaves_index[HINIC3_BOND_SLAVE_NUM] = {0};
    char *slaves_pci_str[HINIC3_BOND_SLAVE_NUM] = {0};

    slaves_num = rte_strsplit(slaves, len, slaves_index, HINIC3_BOND_SLAVE_NUM, ',');
    if (slaves_num <= 0 || slaves_num > HINIC3_BOND_SLAVE_NUM) {
        HINIC3_LOG(ERR, VPORT, "failed to get bond slaves num %d", slaves_num);
        return -1;
    }

    slaves_pci_num = rte_strsplit(slaves_pci, len, slaves_pci_str, HINIC3_BOND_SLAVE_NUM, ',');
    if (slaves_pci_num <= 0 || slaves_pci_num > HINIC3_BOND_SLAVE_NUM) {
        HINIC3_LOG(ERR, VPORT, "failed to get bond slaves pci num %d!", slaves_pci_num);
        return -1;
    }

    if (slaves_num != slaves_pci_num) {
        HINIC3_LOG(ERR, VPORT, "slaves_num is not equal to slaves_pci_num %d != %d!", slaves_num, slaves_pci_num);
        return -1;
    }

    j = 0;
    for (uint32_t i = 0; i < HINIC3_BOND_SLAVE_NUM && j < slaves_num; i++) {
        char *endPtr = NULL;
        uint32_t index = strtoul(slaves_index[j], &endPtr, STR_TO_DEC_NUM);
        if (endPtr == NULL || *endPtr != '\0') {
            HINIC3_LOG(ERR, VPORT, "failed to parse str to uint32!");
            return -1;
        }
        if (index >= HINIC3_BOND_SLAVE_NUM) {
            HINIC3_LOG(ERR, VPORT, "split slave pci, invalid slave index %u!", index);
            return -1;
        }
        if (i == index) {
            strncpy(bond_slave_pci[i], slaves_pci_str[j], HINIC3_BOND_ARG_SLAVE_PCI_LEN - 1);
            bond_slave_pci[i][HINIC3_BOND_ARG_SLAVE_PCI_LEN - 1] = '\0';
            j++;
        }
    }
    return 0;
}

static int
hinic3_process_bond_slave_pci(const char *slaves,
    const char *slaves_pci, char bond_slave_pci[][HINIC3_BOND_ARG_SLAVE_PCI_LEN])
{
    int ret;
    char slaves_str[HINIC3_BOND_SLAVE_NAME_LEN] = {0};
    char slaves_pci_str[HINIC3_BOND_SLAVE_NAME_LEN] = {0};

    strncpy(slaves_str, slaves, HINIC3_BOND_SLAVE_NAME_LEN - 1);
    slaves_str[HINIC3_BOND_SLAVE_NAME_LEN - 1] = '\0';

    strncpy(slaves_pci_str, slaves_pci, HINIC3_BOND_SLAVE_NAME_LEN - 1);
    slaves_pci_str[HINIC3_BOND_SLAVE_NAME_LEN - 1] = '\0';

    ret = hinic3_split_slave_pci(slaves_str, slaves_pci_str, bond_slave_pci, HINIC3_BOND_SLAVE_NAME_LEN);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "hinic3_split_slave_pci is failed ret is %d!", ret);
        return -1;
    }

    return 0;
}

static int
hinic3_get_bond_slave_pci_from_map(struct smap *bond_info,
    char bond_slave_pci[][HINIC3_BOND_ARG_SLAVE_PCI_LEN])
{
    int ret;
    const char *slaves = NULL;
    const char *slaves_pci = NULL;

    slaves = hinic3_smap_get(bond_info, HINIC3_BOND_SLAVES);
    if (slaves == NULL) {
        HINIC3_LOG(ERR, VPORT, "get bond slave pci from map, slaves is NULL!");
        return -1;
    }

    slaves_pci = hinic3_smap_get(bond_info, HINIC3_BOND_ARG_SLAVE_PCI_STR);
    if (slaves_pci == NULL) {
        HINIC3_LOG(ERR, VPORT, "slaves_pci is NULL!");
        return -1;
    }

    ret = hinic3_process_bond_slave_pci(slaves, slaves_pci, bond_slave_pci);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "process bond slave pci string fail ret is %d!", ret);
        return -1;
    }

    return 0;
}

int
hinic3_init_bond_slave_info(struct smap *bond_info)
{
    int ret;
    char bond_slave_pci[HINIC3_BOND_SLAVE_NUM][HINIC3_BOND_ARG_SLAVE_PCI_LEN] = {
                {"Invalid"}, {"Invalid"}, {"Invalid"}, {"Invalid"}
    };
    ret = hinic3_get_bond_slave_pci_from_map(bond_info, bond_slave_pci);
    if (ret != 0) {
        HINIC3_LOG(ERR, VPORT, "failed to get bond slave info from map!");
        return -1;
    }

    hinic3_bond_detect_init(bond_slave_pci);
    return 0;
}

void
hinic3_bond_detect_process(uint16_t vport_id, struct rte_mbuf *mbuf,
    struct hinic3_pkt_user_data *hinic3_metadata)
{
    uint32_t *field = NULL;
    union hinic3_bond_detect_field detect_field = {0};

    field = (uint32_t *)&mbuf->dynfield1[HINIC3_SLAVE_PPORT_ID_PTR];
    detect_field.value = *field;
    if (detect_field.field.enable == 0) {
        return;
    }
    
    for (int i = 0; i < HINIC3_BOND_SLAVE_NUM; i++) {
        if (!g_hinic3_bond_slaves_array[i].valid)
            continue;

        if (hinic3_bond_detect_pci_addr_equal(&g_hinic3_bond_slaves_array[i].pci_addr, &detect_field)) {
            hinic3_metadata->vport_id = HINIC3_SLAVE_SELECT_PREFIX + (vport_id & HINIC3_PORT_ID_MASK);
            hinic3_metadata->slave_port_id = g_hinic3_bond_slaves_array[i].slave_id;
            hinic3_add_error_stats(HINIC3_FLOW_AGENT_WARNING_BOND_SLAVE_DETECT_PKT, 1);
            return;
        }
    }
    // 没有匹配到则传入非法vport_id让组件丢包
    hinic3_metadata->vport_id = HINIC3_SLAVE_INVALID_PREFIX + (vport_id & HINIC3_PORT_ID_MASK);
    hinic3_add_error_stats(HINIC3_FLOW_AGENT_ERROR_BOND_SLAVE_DETECT_NO_MATCH, 1);
    return;
}