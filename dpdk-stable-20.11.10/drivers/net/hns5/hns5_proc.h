/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026-2027 Huawei Technologies Co., Ltd
 */

#ifndef _HNS5_PROC_H_
#define _HNS5_PROC_H_

#include <stdint.h>
#include <stdbool.h>

#include <rte_ethdev.h>

#include "native.h"

/* hidrv/ndrv handles are per-process VAs: priv-stored ones are primary-only.
 * This table holds the handles opened HERE; the data path uses
 * hns5_proc.poe_vfhandle instead of the per-queue copies. */
struct hns5_process_local {
    handle poe_pfhandle;       /* POE PF handle of this process */
    handle poe_vfhandle;        /* POE VF handle of this process (data path) */
    handle ppe_pfhandle;        /* PPE PF handle of this process */
    handle ppe_vfhandle;        /* PPE VF handle of this process */
    handle mag_handle;          /* MAG handle of this process (link/mib query) */
    int mag_pf_devid;           /* MAG PF devid valid in this process */
    bool init_done;             /* handles opened in this process */
};

extern struct hns5_process_local hns5_proc;

/* Primary: cache the handles it opened into the process-local table. */
int hns5_proc_init_primary(struct rte_eth_dev *dev);

/* Secondary: reopen devices here (mmap only) and validate devids with priv. */
int hns5_proc_init_secondary(struct rte_eth_dev *dev);

/* Secondary lifecycle: arm/disarm the local data path; hw is primary-owned. */
void hns5_proc_secondary_start(struct rte_eth_dev *dev);
void hns5_proc_secondary_stop(struct rte_eth_dev *dev);
void hns5_proc_secondary_close(struct rte_eth_dev *dev);

#endif /* _HNS5_PROC_H_ */
