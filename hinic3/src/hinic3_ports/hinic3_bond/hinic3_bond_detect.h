/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2025 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_BOND_SLAVE_H
#define HINIC3_BOND_SLAVE_H

#include <unistd.h>
#include <string.h>
#include <inttypes.h>
#include "hinic3_message.h"
#include "rte_pci.h"

union hinic3_bond_detect_field {
    struct {
        uint32_t function : 8;
        uint32_t device : 8;
        uint32_t bus : 8;
        uint32_t reverse : 7;
        uint32_t enable : 1;
    } field;
    uint32_t value;
};

struct hinic3_bond_slave_pci_map {
    struct rte_pci_addr pci_addr;
    uint16_t slave_id;
    bool valid;
};

void hinic3_bond_detect_process(uint16_t vport_id, struct rte_mbuf *mbuf,
    struct hinic3_pkt_user_data *hinic3_metadata);
int hinic3_init_bond_slave_info(struct smap *bond_info);
#endif // HINIC3_BOND_SLAVE_H
