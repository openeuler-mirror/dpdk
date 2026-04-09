/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_INIT_H
#define HINIC3_INIT_H

#include <stdbool.h>
#include <stdint.h>
#include "rte_pci.h"
#include "rte_common.h"
#include "rte_mempool.h"

#include "hinic3_drv.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*hinic3_lib_dlsym_uninit_cb_t)(void);

int hinic3_drv_ops_init(void);
int hinic3_ops_init(void);
struct hinic3_drv_ops* hinic3_get_drv_ops(void);
hinic3_flexda_ovs_ops_t* hinic3_get_flexda_ovs_api(void);
int hinic3_flexda_ovs_ops_init(void);
int hinic3_driver_class_init(void);
int hinic3_pf_vdev_enable(void);
void hinic3_driver_class_uninit(void);
void hinic3_lib_dlsym_uninit_cb_register(hinic3_lib_dlsym_uninit_cb_t cb);
void hinic3_lib_dlsym_uninit_cb_unregister(void);
void hinic3_show_version(void);

#ifdef __cplusplus
}
#endif

#endif /* HINIC3_INIT_H */
