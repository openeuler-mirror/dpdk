/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026-2027 Huawei Technologies Co., Ltd
 */

#include <errno.h>

#include <rte_eal.h>

#include "hidrv_poe_pf_api.h"
#include "hidrv_poe_vf_api.h"
#include "hidrv_ppe_pf_api.h"
#include "hidrv_ppe_vf_api.h"
#include "hidrv_mag_api.h"
#include "hidrv_poe_xtp.h"
#include "hidrv_ppe_xtp.h"
#include "hidrv_mag_xtp.h"

#include "hns5_eth.h"
#include "hns5_proc.h"
#include "hns5_mp.h"
#include "hns5_log.h"

/* process-local handle table, one instance per process */
struct hns5_process_local hns5_proc;

#define HNS5_PROC_DIE_ID 0
#define HNS5_PROC_PF_ID  0
#define HNS5_PROC_VF_ID  0

/* Open (die,pf) here and check its devid against the shared priv (mmap only). */
static int hns5_proc_check_devid(struct rte_eth_dev *dev, int devid_new,
    int devid_shared, const char *name)
{
    if (devid_new < 0) {
        hns5_err(dev, "secondary: failed to get %s devid, ret:%d\n", name, devid_new);
        return -ENODEV;
    }
    if (devid_new != devid_shared) {
        hns5_err(dev, "secondary: %s devid mismatch, primary:%d secondary:%d\n",
            name, devid_shared, devid_new);
        return -ENODEV;
    }
    return 0;
}

int hns5_proc_init_primary(struct rte_eth_dev *dev)
{
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);

    if (hns5_proc.init_done)
        return 0;
    hns5_proc.poe_pfhandle = priv->poe_pfhandle;
    hns5_proc.poe_vfhandle = priv->poe_vfhandle;
    hns5_proc.ppe_pfhandle = priv->ppe_pfhandle;
    hns5_proc.ppe_vfhandle = hidrv_ppe_vf_get_ndrv_handle(priv->ppe_vf_devid);
    hns5_proc.mag_pf_devid = priv->mag_pf_devid;
    hns5_proc.mag_handle = priv->mag_handle;
    if (hns5_proc.poe_vfhandle == NULL || hns5_proc.ppe_pfhandle == NULL) {
        hns5_err(dev, "primary: poe/ppe handle missing\n");
        return -EINVAL;
    }
    hns5_proc.init_done = true;
    return 0;
}

int hns5_proc_init_secondary(struct rte_eth_dev *dev)
{
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);
    int devid;
    int ret;

    if (hns5_proc.init_done)
        return 0;

    /* POE PF: devid check is mandatory, poe vf handle drives the data path */
    devid = hidrv_poe_get_pf_devid(HNS5_PROC_DIE_ID, HNS5_PROC_PF_ID);
    ret = hns5_proc_check_devid(dev, devid, priv->poe_pf_devid, "poe pf");
    if (ret != 0)
        return ret;
    hns5_proc.poe_pfhandle = hidrv_poe_pf_get_ndrv_handle(devid);

    devid = hidrv_poe_get_vf_devid(HNS5_PROC_DIE_ID, HNS5_PROC_PF_ID, HNS5_PROC_VF_ID);
    ret = hns5_proc_check_devid(dev, devid, priv->poe_vf_devid, "poe vf");
    if (ret != 0)
        return ret;
    hns5_proc.poe_vfhandle = hidrv_poe_vf_get_ndrv_handle(devid);

    /* PPE PF/VF */
    devid = hidrv_ppe_get_pf_devid(HNS5_PROC_DIE_ID, HNS5_PROC_PF_ID);
    ret = hns5_proc_check_devid(dev, devid, priv->ppe_pf_devid, "ppe pf");
    if (ret != 0)
        return ret;
    hns5_proc.ppe_pfhandle = hidrv_ppe_pf_get_ndrv_handle(devid);

    devid = hidrv_ppe_get_vf_devid(HNS5_PROC_DIE_ID, HNS5_PROC_PF_ID, HNS5_PROC_VF_ID);
    ret = hns5_proc_check_devid(dev, devid, priv->ppe_vf_devid, "ppe vf");
    if (ret != 0)
        return ret;
    hns5_proc.ppe_vfhandle = hidrv_ppe_vf_get_ndrv_handle(devid);

    if (hns5_proc.poe_pfhandle == NULL || hns5_proc.poe_vfhandle == NULL ||
        hns5_proc.ppe_pfhandle == NULL || hns5_proc.ppe_vfhandle == NULL) {
        hns5_err(dev, "secondary: failed to get poe/ppe ndrv handle\n");
        return -ENODEV;
    }

    /* MAG starts at primary dev_start; open it here so link/fec queries work. */
    devid = hidrv_mag_get_pf_devid(HNS5_PROC_DIE_ID, HNS5_PROC_PF_ID);
    if (devid >= 0) {
        hns5_proc.mag_pf_devid = devid;
        hns5_proc.mag_handle = hidrv_mag_get_ndrv_handle(devid);
        if (priv->mag_pf_devid > 0 && priv->mag_pf_devid != devid) {
            hns5_err(dev, "secondary: mag devid mismatch, primary:%d secondary:%d\n",
                priv->mag_pf_devid, devid);
            return -ENODEV;
        }
    } else {
        /* link_update/fec_get will fail with a clear error, not fatal */
        hns5_warn(dev, "secondary: mag devid unavailable (ret:%d), link query disabled\n", devid);
    }

    hns5_proc.init_done = true;
    hns5_info(dev, "secondary: process-local handles ready "
        "(poe_pf:%p poe_vf:%p ppe_pf:%p ppe_vf:%p mag:%p)\n",
        hns5_proc.poe_pfhandle, hns5_proc.poe_vfhandle,
        hns5_proc.ppe_pfhandle, hns5_proc.ppe_vfhandle, hns5_proc.mag_handle);
    return 0;
}

void hns5_proc_secondary_start(struct rte_eth_dev *dev)
{
    hns5_mp_arm_datapath(dev);
    hns5_info(dev, "secondary: local data path started\n");
}

void hns5_proc_secondary_stop(struct rte_eth_dev *dev)
{
    hns5_mp_disarm_datapath(dev);
    hns5_info(dev, "secondary: local data path stopped, hw owned by primary\n");
}

void hns5_proc_secondary_close(struct rte_eth_dev *dev)
{
    hns5_mp_disarm_datapath(dev);
    hns5_info(dev, "secondary: port:%u succeed to close\n", dev->data->port_id);
}
