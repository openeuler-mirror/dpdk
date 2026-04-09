/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Huawei Technologies Co., Ltd
 */

#include <ctype.h>
#include "hinic3_agent.h"
#include "hinic3_log.h"
#include "hinic3_iface_global.h"
#include "hinic3_dfx_port.h"
#include "hinic3_mutex.h"
#include "hinic3_dfx_multi_qos.h"
#include "hinic3_agent_flow_cmd.h"
#include "hinic3_meminfo.h"
#include "hinic3_ui_string.h"
#include "hinic3_command.h"
#include "hinic3_ds.h"
#include "hinic3_cmd_exec.h"

#define HINIC3_DFX_GLOABL_ALLOC_MEM_MAX_SIZE  (0x20000)
#define HINIC3_CMD_MAX_LENGTH                 2048

void
unixctl_hinic3_lib_cmd_exec(struct unixctl_conn *conn, int argc HINIC3_UNUSED, const char *argv[],
    void *aux)
{
    struct ds ds = DS_EMPTY_INITIALIZER;
    char *cmd_out = NULL;
    uint32_t out_len = 0;
    int ret = -1;

    cmd_out = (char *)hinic3_calloc(1, HINIC3_DFX_GLOABL_ALLOC_MEM_MAX_SIZE + 1, HINIC3_COMMAND);
    if (cmd_out == NULL) {
        hinic3_ds_put_format(&ds, "%sCmd out calloc failed!\n", HINIC3_UI_LEADING_SIGN_ERROR);
        goto purging;
    }

    if (strnlen(argv[1], HINIC3_CMD_MAX_LENGTH) >= HINIC3_CMD_MAX_LENGTH) {
        hinic3_ds_put_format(&ds, "%sCommand too long!\n", HINIC3_UI_LEADING_SIGN_ERROR);
        goto out;
    }

    ret = hinic3_global_cmd_exec(argv[1], strnlen(argv[1], HINIC3_CMD_MAX_LENGTH),
                                cmd_out, &out_len, HINIC3_DFX_GLOABL_ALLOC_MEM_MAX_SIZE);

    if (ret != 0) {
        hinic3_ds_put_format(&ds, "%sHinic3 cmd exec failed with error %d.\n", HINIC3_UI_LEADING_SIGN_ERROR, ret);
        goto out;
    }

    if (out_len > HINIC3_DFX_GLOABL_ALLOC_MEM_MAX_SIZE) {
        HINIC3_LOG(WARNING, VPORT, "hinic3 cmd exec reply out of memory");
        out_len = HINIC3_DFX_GLOABL_ALLOC_MEM_MAX_SIZE;
    }

    if (out_len == 0) {
        hinic3_ds_put_format(&ds, "%sHinic3 cmd exec no reply.\n", HINIC3_UI_LEADING_SIGN_ERROR);
        goto out;
    }
    *(cmd_out + out_len) = '\0';
    hinic3_ds_put_format(&ds, "%s\n", cmd_out);
out:
    hinic3_free(cmd_out);
purging:
    if (ret == 0) {
        hinic3_command_reply(conn, hinic3_ds_cstr(&ds));
        *(int *)aux = 0;
    } else {
        hinic3_command_reply_error(conn, hinic3_ds_cstr(&ds));
        *(int *)aux = -1;
    }

    hinic3_ds_destroy(&ds);
}

void
unixctl_hinic3_cmd_exec_register(void)
{
    hinic3_command_register("hwoff/exec-cmd",
        "{ dump { -t <type> -x <index> | -h } "
        "| xstats { -i <device> | -h } | nic_queue { -i <device> -d "
        "ENUM<tx,rx> -t <type> -q <queue id> [ -w <wqe id> ] | -h } | hpd "
        "{ -v | -m <mode> | -i <index> -t <type> | -h } | { -v | --version } | { -h | --help } }",
        1, 1, unixctl_hinic3_lib_cmd_exec, NULL);
}
