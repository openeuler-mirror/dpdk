/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2025-2026 Huawei Technologies Co., Ltd
 */

#ifndef _HINIC3_PMD_TM_H_
#define _HINIC3_PMD_TM_H_

#include <stdint.h>
#include <rte_tailq.h>
#include <rte_tm_driver.h>

#define HINIC3_MAX_COS_NUM 8
#define HINIC3_MAX_TC_NUM	8

struct hinic3_port_limit_rate_cmd {
	uint32_t speed; /* Unit Mbps */
	uint32_t rsvd[5];
};

enum hinic3_tm_node_type {
	HINIC3_TM_NODE_TYPE_PORT,
	HINIC3_TM_NODE_TYPE_TC,
	HINIC3_TM_NODE_TYPE_COS,
	HINIC3_TM_NODE_TYPE_QUEUE,
	HINIC3_TM_NODE_TYPE_MAX,
};

enum hinic3_tm_node_level {
	HINIC3_TM_NODE_LEVEL_PORT,
	HINIC3_TM_NODE_LEVEL_TC,
	HINIC3_TM_NODE_LEVEL_COS,
	HINIC3_TM_NODE_LEVEL_QUEUE,
	HINIC3_TM_NODE_LEVEL_MAX,
};

struct hinic3_tm_shaper_profile {
	TAILQ_ENTRY(hinic3_tm_shaper_profile) node;
	uint32_t shaper_profile_id;
	uint32_t reference_count;
	struct rte_tm_shaper_params profile;
};
TAILQ_HEAD(hinic3_shaper_profile_list, hinic3_tm_shaper_profile);

struct hinic3_tm_node {
	TAILQ_ENTRY(hinic3_tm_node) node;
	uint32_t id;
	uint32_t priority;
	uint32_t weight;
	uint32_t reference_count;
	uint16_t no;
	struct hinic3_tm_node *parent;
	struct hinic3_tm_shaper_profile *shaper_profile;
	struct rte_tm_node_params params;
};
TAILQ_HEAD(hinic3_tm_node_list, hinic3_tm_node);

struct hinic3_tm_conf {
	uint32_t nb_leaf_nodes_max;	/* max numbers of leaf nodes */
	uint32_t nb_nodes_max;		/* max numbers of nodes */
	uint32_t nb_shaper_profile_max; /* max numbers of shaper profile */

	struct hinic3_shaper_profile_list shaper_profile_list;
	uint32_t nb_shaper_profile; /* number of shaper profile */

	struct hinic3_tm_node *root;
	struct hinic3_tm_node_list tc_list;
	struct hinic3_tm_node_list cos_list;
	struct hinic3_tm_node_list queue_list;
	uint32_t nb_tc_node;	/* number of added TC nodes */
	uint32_t nb_cos_node;	/* number of added cos nodes */
	uint32_t nb_queue_node; /* number of added queue nodes */

	/*
	 * This flag is used to check if APP can change the TM node
	 * configuration.
	 * When it's true, means the configuration is applied to HW,
	 * APP should not add/delete the TM node configuration.
	 * When starting the port, APP should call the hierarchy_commit API to
	 * set this flag to true. When stopping the port, this flag should be
	 * set to false.
	 */
	bool committed;
};

struct hinic3_tc_queue_info {
	u16 tqp_offset; /* TQP offset from base TQP */
	u16 tqp_count;	/* Total TQPs */
	u8 tc;		/* TC index */
	u8 enable;	/* If this TC is enable or not */
};

struct hinic3_ets {
	struct hinic3_tm_conf tm_conf;
	u32 max_tm_rate;
	u8 hw_tc_map;
	u8 num_tc;	    /* Total number of enabled TCs */
	u16 tx_qnum_per_tc; /* TX queue number per TC */
	struct hinic3_tc_queue_info tc_queue[HINIC3_MAX_TC_NUM];
	u16 used_tx_queues;
	u16 tqps_num; /* num task queue pairs of this function */
	u8 tc_bw[HINIC3_MAX_TC_NUM];
};

/*
 * This API used to calc node TC no. User must make sure the node id is in the
 * TC node id range.
 *
 * User could call rte_eth_dev_info_get API to get port's max_tx_queues, The TM
 * id's assignment should following the below rules:
 *     [0, max_tx_queues-1]: correspond queues's node id
 *     --------------------------------------------------
 *     max_tx_queues + 0   : correspond cos0's node id
 *     max_tx_queues + 1   : correspond cos1's node id
 *     ...
 *     max_tx_queues + 7   : correspond cos7's node id
 *     --------------------------------------------------
 *     max_tx_queues + 8   : correspond TC0's node id
 *     max_tx_queues + 9   : correspond TC1's node id
 *     ...
 *     max_tx_queues + 15  : correspond TC7's node id
 *     --------------------------------------------------
 *     max_tx_queues + 16  : correspond port's node id
 *
 */
static inline uint8_t
hinic3_tm_calc_node_cos_no(struct hinic3_tm_conf *conf, uint32_t node_id)
{
	if (node_id >= conf->nb_leaf_nodes_max &&
	    node_id < conf->nb_leaf_nodes_max + HINIC3_MAX_COS_NUM)
		return node_id - conf->nb_leaf_nodes_max;
	else
		return 0;
}

static inline uint8_t
hinic3_tm_calc_node_tc_no(struct hinic3_tm_conf *conf, uint32_t node_id)
{
	if (node_id >= conf->nb_leaf_nodes_max + HINIC3_MAX_COS_NUM &&
	    node_id < conf->nb_nodes_max - 1)
		return node_id - conf->nb_leaf_nodes_max - HINIC3_MAX_COS_NUM;
	else
		return 0;
}

int hinic3_tm_conf_init(struct rte_eth_dev *dev);
void hinic3_tm_conf_uninit(struct rte_eth_dev *dev);
int hinic3_tm_ops_get(struct rte_eth_dev *dev, void *arg);
void hinic3_tm_dev_start_proc(struct rte_eth_dev *dev);
void hinic3_tm_dev_stop_proc(struct rte_eth_dev *dev);
int hinic3_tm_conf_update(struct rte_eth_dev *dev);

#endif /* _HINIC3_PMD_TM_H_ */
