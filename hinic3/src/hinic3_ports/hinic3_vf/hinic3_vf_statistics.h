/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2025 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_VF_STATISTICS_H
#define HINIC3_VF_STATISTICS_H

/* 网卡收发包简要信息统计 */
int hinic3_vf_dev_stats_get(struct rte_eth_dev *dev, struct rte_eth_stats *stats);
/* 网卡收发包简要统计清空 */
int hinic3_vf_dev_stats_reset(struct rte_eth_dev *dev);
/* 网卡收发包详细信息统计 */
int hinic3_vf_dev_xstats_get(struct rte_eth_dev *dev, struct rte_eth_xstat *xstats, unsigned int n);
/* 网卡收发包详细统计清空 */
int hinic3_vf_dev_xstats_reset(struct rte_eth_dev *dev);
/* 网卡收发包详细统计条目名称 */
int hinic3_vf_dev_xstats_get_names(struct rte_eth_dev *dev, struct rte_eth_xstat_name *xstats_names,
    unsigned int limit);

#endif // HINIC3_VF_STATISTICS_H
