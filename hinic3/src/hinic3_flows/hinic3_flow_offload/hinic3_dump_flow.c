/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2022 Huawei Technologies Co., Ltd
 */

#include "rte_log.h"
#include "rte_ethdev.h"
#include "hinic3_iface_port.h"
#include "hinic3_offload_action_public.h"
#include "hinic3_ui_string.h"
#include "hinic3_dump_flow.h"

#define MAX_LEN 255
#define SUDO_PATH "/usr/bin/sudo"
#define HINICADMIN_PATH "/usr/sbin/hinicadm3"

static int
hinic3_info_need(char *buf, int buffer_size HINIC3_UNUSED)
{
    static const char *infos[] = {
        "Card information",
        "card type",
        "port num",
        "port speed",
        "pcie width",
        "pf num",
        "vf total num",
        "hardware id"
    };
    int flag = 0;
    size_t size = sizeof(infos) / sizeof(infos[0]);
    for (size_t i = 0; i < size; i++) {
        if (strncmp(buf, infos[i], strlen(infos[i])) == 0) {
            flag = 1;
            break;
        }
    }
    return flag;
}

int
hinic3_eth_flow_dev_dump(struct rte_eth_dev *dev, struct rte_flow *flow HINIC3_UNUSED,
    FILE *file, struct rte_flow_error *error)
{
    if ((dev == NULL) || (file == NULL) || (error == NULL)) {
        hinic3_add_error_stats(HINIC3_FLOWS_ERROR_DEV_DUMP_NULL, 1);
        return -EINVAL;
    }

    if (access(HINICADMIN_PATH, F_OK) != 0) {
        HINIC3_LOG(ERR, FLOW, "hinic3_eth_flow_dev_dump: hinicadm3 not exist.");
        return -EINVAL;
    }

    char cmd[MAX_LEN] = {0};
    if (sprintf(cmd, "%s %s info -i hinic0", SUDO_PATH, HINICADMIN_PATH) <= 0) {
        HINIC3_LOG(ERR, FLOW, "hinic3_eth_flow_dev_dump: sprintf error.");
        return -EINVAL;
    }

    FILE *f = popen(cmd, "r");
    if (f == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3_eth_flow_dev_dump: popen fail.");
        return -EINVAL;
    }
    char buff[MAX_LEN + 1] = {0};
    while (fgets(buff, MAX_LEN + 1, f) != NULL) {
        if (hinic3_info_need(buff, MAX_LEN + 1)) {
            fputs(buff, file);
        }
    }
    pclose(f);

    return 0;
}
