/*
 * 版权所有 (c) 华为技术有限公司 2022-2023
 * 功能描述: 1822网卡端口限速功能适配中国移动规范头文件
 * 创建日期: 2022-11-16
 */
#ifndef HINIC3_MTR_H
#define HINIC3_MTR_H

#define HW_MTR_STATS_N_PKTS_DROPPED   (1U << 3)
#define HW_MTR_STATS_N_BYTES_DROPPED  (1U << 7)
#define HW_MTR_INGRESS_INDEX          0
#define HW_MTR_EGRESS_INDEX           1

int hinic3_mtr_ops_get(struct rte_eth_dev *dev, void *ops);
#endif
