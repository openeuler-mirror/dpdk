 /* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_IFACE_PORT_UTIL_H
#define HINIC3_IFACE_PORT_UTIL_H

#include "hinic3_nlattr.h"

#ifdef __cplusplus
extern "C" {
#endif

void port_args_smap_to_nlattr(const struct smap *args, struct hinic3_nlattr *nla_args);
void bond_args_nlattr_to_smap(const struct hinic3_nlattr *nla_config, struct smap *smap_config);
int bum_args_nlattr_to_smap(const struct hinic3_nlattr *args_nla, struct smap *args_smap);
void port_args_nlattr_to_smap(const struct hinic3_nlattr *nla_unset_args, struct smap *unset_args);

#ifdef __cplusplus
}
#endif

#endif /* HINIC3_IFACE_PORT_UTIL_H */
