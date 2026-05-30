/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2025-2026 Huawei Technologies Co., Ltd
 */

#include <rte_malloc.h>

#include "base/hinic3_compat.h"
#include "base/hinic3_pmd_hwdev.h"
#include "base/hinic3_pmd_hw_cfg.h"
#include "base/hinic3_pmd_hw_comm.h"
#include "hinic3_pmd_cmd.h"
#include "hinic3_pmd_dcb.h"
#include "hinic3_pmd_ethdev.h"

#include "hinic3_pmd_tm.h"

static uint32_t
hinic3_tm_max_tx_queues_get(struct rte_eth_dev *dev)
{
	/*
	 * This API will called in pci device probe stage, we can't call
	 * rte_eth_dev_info_get to get max_tx_queues (due to rte_eth_devices
	 * not setup), so we call the hinic3_dev_infos_get.
	 */
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct rte_eth_dev_info dev_info = {0};

	hinic3_dev_info_get(&dev_info, nic_dev);

	return RTE_MIN(dev_info.max_tx_queues, RTE_MAX_QUEUES_PER_PORT);
}

int
hinic3_tm_conf_init(struct rte_eth_dev *dev)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	uint32_t max_tx_queues = hinic3_tm_max_tx_queues_get(dev);
	struct hinic3_ets *ets = nic_dev->ets;

	struct hinic3_tm_conf *tm_conf = &ets->tm_conf;

	tm_conf->nb_leaf_nodes_max = max_tx_queues;
	tm_conf->nb_nodes_max =
		1 + HINIC3_MAX_TC_NUM + HINIC3_MAX_COS_NUM + max_tx_queues;
	tm_conf->nb_shaper_profile_max = 1 + HINIC3_MAX_TC_NUM;

	TAILQ_INIT(&tm_conf->shaper_profile_list);
	tm_conf->nb_shaper_profile = 0;

	tm_conf->root = NULL;
	TAILQ_INIT(&tm_conf->tc_list);
	TAILQ_INIT(&tm_conf->cos_list);
	TAILQ_INIT(&tm_conf->queue_list);
	tm_conf->nb_tc_node = 0;
	tm_conf->nb_queue_node = 0;

	tm_conf->committed = false;

	return 0;
}

void
hinic3_tm_conf_uninit(struct rte_eth_dev *dev)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct hinic3_ets *ets = nic_dev->ets;
	struct hinic3_tm_conf *tm_conf = &ets->tm_conf;
	struct hinic3_tm_shaper_profile *shaper_profile;
	struct hinic3_tm_node *tm_node;

	if (tm_conf->nb_queue_node > 0) {
		while ((tm_node = TAILQ_FIRST(&tm_conf->queue_list))) {
			TAILQ_REMOVE(&tm_conf->queue_list, tm_node, node);
			rte_free(tm_node);
		}
		tm_conf->nb_queue_node = 0;
	}

	if (tm_conf->nb_cos_node > 0) {
		while ((tm_node = TAILQ_FIRST(&tm_conf->cos_list))) {
			TAILQ_REMOVE(&tm_conf->cos_list, tm_node, node);
			rte_free(tm_node);
		}
		tm_conf->nb_cos_node = 0;
	}

	if (tm_conf->nb_tc_node > 0) {
		while ((tm_node = TAILQ_FIRST(&tm_conf->tc_list))) {
			TAILQ_REMOVE(&tm_conf->tc_list, tm_node, node);
			rte_free(tm_node);
		}
		tm_conf->nb_tc_node = 0;
	}

	if (tm_conf->root != NULL) {
		rte_free(tm_conf->root);
		tm_conf->root = NULL;
	}

	if (tm_conf->nb_shaper_profile > 0) {
		while ((shaper_profile =
				TAILQ_FIRST(&tm_conf->shaper_profile_list))) {
			TAILQ_REMOVE(&tm_conf->shaper_profile_list,
				     shaper_profile, node);
			rte_free(shaper_profile);
		}
		tm_conf->nb_shaper_profile = 0;
	}

	tm_conf->nb_leaf_nodes_max = 0;
	tm_conf->nb_nodes_max = 0;
	tm_conf->nb_shaper_profile_max = 0;
	rte_free(ets);
}

static inline uint64_t
hinic3_tm_rate_convert_firmware2tm(uint32_t firmware_rate)
{
#define FIRMWARE_TO_TM_RATE_SCALE 125000
	/* tm rate unit is Bps, firmware rate is Mbps */
	return ((uint64_t)firmware_rate) * FIRMWARE_TO_TM_RATE_SCALE;
}

/**
 * show port tm cap (port_id)
 *
 * Display the port TM capability.
 */
static int
hinic3_tm_capabilities_get(struct rte_eth_dev *dev,
			struct rte_tm_capabilities *cap,
			struct rte_tm_error *error)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct hinic3_ets *ets = nic_dev->ets;
	uint32_t max_tx_queues = hinic3_tm_max_tx_queues_get(dev);

	if (!cap || !errno)
		return -EINVAL;

	error->type = RTE_TM_ERROR_TYPE_NONE;

	memset(cap, 0, sizeof(struct rte_tm_capabilities));

	cap->n_nodes_max = 1 + HINIC3_MAX_TC_NUM + max_tx_queues;
	cap->n_levels_max = HINIC3_TM_NODE_LEVEL_MAX;
	cap->non_leaf_nodes_identical = 1;
	cap->leaf_nodes_identical = 1;
	cap->shaper_n_max = 1 + HINIC3_MAX_TC_NUM;
	cap->shaper_private_n_max = 1 + HINIC3_MAX_TC_NUM;
	cap->shaper_private_rate_max =
		hinic3_tm_rate_convert_firmware2tm(ets->max_tm_rate);

	cap->sched_n_children_max = max_tx_queues;
	cap->sched_sp_n_priorities_max = 1;
	cap->sched_wfq_weight_max = 1;

	cap->shaper_pkt_length_adjust_min = RTE_TM_ETH_FRAMING_OVERHEAD;
	cap->shaper_pkt_length_adjust_max = RTE_TM_ETH_FRAMING_OVERHEAD_FCS;

	return 0;
}

static int
hinic3_tm_shaper_profile_param_check(struct rte_eth_dev *dev,
#ifdef DPDK_24_11
				     const struct rte_tm_shaper_params *profile,
#else
				     struct rte_tm_shaper_params *profile,
#endif
				     struct rte_tm_error *error)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct hinic3_ets *ets = nic_dev->ets;

	if (profile->committed.rate) {
		error->type = RTE_TM_ERROR_TYPE_SHAPER_PROFILE_COMMITTED_RATE;
		error->message = "committed rate not supported";
		return -EINVAL;
	}

	if (profile->committed.size) {
		error->type = RTE_TM_ERROR_TYPE_SHAPER_PROFILE_COMMITTED_SIZE;
		error->message = "committed bucket size not supported";
		return -EINVAL;
	}

	if (profile->peak.rate > ets->max_tm_rate) {
		error->type = RTE_TM_ERROR_TYPE_SHAPER_PROFILE_PEAK_RATE;
		error->message = "peak rate too large";
		PMD_DRV_LOG(ERR,
			    "peak_tb_rate config failed, peak_tb_rate = %ld, "
			    "must in [0, 100]",
			    profile->peak.rate);
		return -EINVAL;
	}

	if (profile->peak.size) {
		error->type = RTE_TM_ERROR_TYPE_SHAPER_PROFILE_PEAK_SIZE;
		error->message = "peak bucket size not supported";
		return -EINVAL;
	}

	if (profile->pkt_length_adjust) {
		error->type = RTE_TM_ERROR_TYPE_SHAPER_PROFILE_PKT_ADJUST_LEN;
		error->message = "packet length adjustment not supported";
		return -EINVAL;
	}

#ifdef DPDK_20_11
	if (profile->packet_mode) {
		error->type = RTE_TM_ERROR_TYPE_SHAPER_PROFILE_PACKET_MODE;
		error->message = "packet mode not supported";
		return -EINVAL;
	}
#endif
	return 0;
}

static struct hinic3_tm_shaper_profile *
hinic3_tm_shaper_profile_search(struct rte_eth_dev *dev,
			     uint32_t shaper_profile_id)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct hinic3_ets *ets = nic_dev->ets;
	struct hinic3_tm_conf *tm_conf = &ets->tm_conf;
	struct hinic3_shaper_profile_list *shaper_profile_list =
		&tm_conf->shaper_profile_list;
	struct hinic3_tm_shaper_profile *shaper_profile;

	TAILQ_FOREACH (shaper_profile, shaper_profile_list, node) {
		if (shaper_profile_id == shaper_profile->shaper_profile_id)
			return shaper_profile;
	}

	return NULL;
}

/**
 * add port tm node shaper profile (port_id) (shaper_profile_id) (cmit_tb_rate)
 * (cmit_tb_size) (peak_tb_rate) (peak_tb_size) (packet_length_adjust)
 * (packet_mode)
 *
 * Add port tm node private shaper profile.
 */
static int
hinic3_tm_shaper_profile_add(struct rte_eth_dev *dev, uint32_t shaper_profile_id,
#ifdef DPDK_24_11
			     const struct rte_tm_shaper_params *profile,
#else
			     struct rte_tm_shaper_params *profile,
#endif
			     struct rte_tm_error *error)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct hinic3_ets *ets = nic_dev->ets;
	struct hinic3_tm_conf *tm_conf = &ets->tm_conf;
	struct hinic3_tm_shaper_profile *shaper_profile;
	int ret;

	if (profile == NULL || error == NULL)
		return -EINVAL;

	if (tm_conf->nb_shaper_profile >= tm_conf->nb_shaper_profile_max) {
		error->type = RTE_TM_ERROR_TYPE_UNSPECIFIED;
		error->message = "too much profiles";
		return -EINVAL;
	}

	ret = hinic3_tm_shaper_profile_param_check(dev, profile, error);
	if (ret)
		return ret;

	shaper_profile = hinic3_tm_shaper_profile_search(dev, shaper_profile_id);
	if (shaper_profile) {
		error->type = RTE_TM_ERROR_TYPE_SHAPER_PROFILE_ID;
		error->message = "profile ID exist";
		return -EINVAL;
	}

	shaper_profile = rte_zmalloc("hinic3_tm_shaper_profile",
				     sizeof(struct hinic3_tm_shaper_profile), 0);
	if (shaper_profile == NULL)
		return -ENOMEM;

	shaper_profile->shaper_profile_id = shaper_profile_id;
	memcpy(&shaper_profile->profile, profile,
	       sizeof(struct rte_tm_shaper_params));
	TAILQ_INSERT_TAIL(&tm_conf->shaper_profile_list, shaper_profile, node);
	tm_conf->nb_shaper_profile++;

	return 0;
}

/**
 * del port tm node shaper profile (port_id) (shaper_profile_id)
 *
 * Delete port tm node private shaper profile.
 */
static int
hinic3_tm_shaper_profile_del(struct rte_eth_dev *dev, uint32_t shaper_profile_id,
			  struct rte_tm_error *error)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct hinic3_ets *ets = nic_dev->ets;
	struct hinic3_tm_conf *tm_conf = &ets->tm_conf;
	struct hinic3_tm_shaper_profile *shaper_profile;

	if (error == NULL)
		return -EINVAL;

	shaper_profile = hinic3_tm_shaper_profile_search(dev, shaper_profile_id);
	if (shaper_profile == NULL) {
		error->type = RTE_TM_ERROR_TYPE_SHAPER_PROFILE_ID;
		error->message = "profile ID not exist";
		return -EINVAL;
	}

	if (shaper_profile->reference_count) {
		error->type = RTE_TM_ERROR_TYPE_SHAPER_PROFILE;
		error->message = "profile in use";
		return -EINVAL;
	}

	TAILQ_REMOVE(&tm_conf->shaper_profile_list, shaper_profile, node);
	rte_free(shaper_profile);
	tm_conf->nb_shaper_profile--;

	return 0;
}

static struct hinic3_tm_node *
hinic3_tm_node_search(struct rte_eth_dev *dev, uint32_t node_id,
		   enum hinic3_tm_node_type *node_type)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct hinic3_ets *ets = nic_dev->ets;
	struct hinic3_tm_conf *tm_conf = &ets->tm_conf;
	struct hinic3_tm_node_list *queue_list = &tm_conf->queue_list;
	struct hinic3_tm_node_list *cos_list = &tm_conf->cos_list;
	struct hinic3_tm_node_list *tc_list = &tm_conf->tc_list;
	struct hinic3_tm_node *tm_node;

	if (tm_conf->root && tm_conf->root->id == node_id) {
		*node_type = HINIC3_TM_NODE_TYPE_PORT;
		return tm_conf->root;
	}

	TAILQ_FOREACH (tm_node, tc_list, node) {
		if (tm_node->id == node_id) {
			*node_type = HINIC3_TM_NODE_TYPE_TC;
			return tm_node;
		}
	}

	TAILQ_FOREACH (tm_node, cos_list, node) {
		if (tm_node->id == node_id) {
			*node_type = HINIC3_TM_NODE_TYPE_COS;
			return tm_node;
		}
	}

	TAILQ_FOREACH (tm_node, queue_list, node) {
		if (tm_node->id == node_id) {
			*node_type = HINIC3_TM_NODE_TYPE_QUEUE;
			return tm_node;
		}
	}

	return NULL;
}

static int
hinic3_tm_nonleaf_node_param_check(struct rte_eth_dev *dev,
#ifdef DPDK_24_11
				   const struct rte_tm_node_params *params,
#else
				   struct rte_tm_node_params *params,
#endif
				   struct rte_tm_error *error)
{
	struct hinic3_tm_shaper_profile *shaper_profile;

	if (params->shaper_profile_id != RTE_TM_SHAPER_PROFILE_ID_NONE) {
		shaper_profile = hinic3_tm_shaper_profile_search(
			dev, params->shaper_profile_id);
		if (shaper_profile == NULL) {
			error->type =
				RTE_TM_ERROR_TYPE_NODE_PARAMS_SHAPER_PROFILE_ID;
			error->message = "shaper profile not exist";
			return -EINVAL;
		}
	}

	if (params->nonleaf.wfq_weight_mode) {
		error->type = RTE_TM_ERROR_TYPE_NODE_PARAMS_WFQ_WEIGHT_MODE;
		error->message = "WFQ not supported";
		return -EINVAL;
	}

	if (params->nonleaf.n_sp_priorities != 1) {
		error->type = RTE_TM_ERROR_TYPE_NODE_PARAMS_N_SP_PRIORITIES;
		error->message = "SP priority not supported";
		return -EINVAL;
	}

	return 0;
}

static int
hinic3_tm_leaf_node_param_check(struct rte_eth_dev *dev __rte_unused,
#ifdef DPDK_24_11
				const struct rte_tm_node_params *params,
#else
				struct rte_tm_node_params *params,
#endif
				struct rte_tm_error *error)

{
	if (params->shaper_profile_id != RTE_TM_SHAPER_PROFILE_ID_NONE) {
		error->type = RTE_TM_ERROR_TYPE_NODE_PARAMS_SHAPER_PROFILE_ID;
		error->message = "shaper not supported";
		return -EINVAL;
	}

	if (params->leaf.cman != RTE_TM_CMAN_TAIL_DROP) {
		error->type = RTE_TM_ERROR_TYPE_NODE_PARAMS_CMAN;
		error->message = "congestion management not supported";
		return -EINVAL;
	}

	if (params->leaf.wred.wred_profile_id != RTE_TM_WRED_PROFILE_ID_NONE &&
	    params->leaf.wred.wred_profile_id != 0) {
		error->type = RTE_TM_ERROR_TYPE_NODE_PARAMS_WRED_PROFILE_ID;
		error->message =
			"WRED not supported, wred_profile_id must be 0";
		return -EINVAL;
	}

	if (params->leaf.wred.shared_wred_context_id) {
		error->type =
			RTE_TM_ERROR_TYPE_NODE_PARAMS_SHARED_WRED_CONTEXT_ID;
		error->message = "WRED not supported";
		return -EINVAL;
	}

	if (params->leaf.wred.n_shared_wred_contexts) {
		error->type =
			RTE_TM_ERROR_TYPE_NODE_PARAMS_N_SHARED_WRED_CONTEXTS;
		error->message = "WRED not supported";
		return -EINVAL;
	}

	return 0;
}

static int
hinic3_tm_node_param_check(struct rte_eth_dev *dev, uint32_t node_id,
			   uint32_t priority, uint32_t weight,
#ifdef DPDK_24_11
			   const struct rte_tm_node_params *params,
#else
			   struct rte_tm_node_params *params,
#endif
			   struct rte_tm_error *error)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct hinic3_ets *ets = nic_dev->ets;
	struct hinic3_tm_conf *tm_conf = &ets->tm_conf;
	enum hinic3_tm_node_type node_type = HINIC3_TM_NODE_TYPE_MAX;

	if (node_id == RTE_TM_NODE_ID_NULL) {
		error->type = RTE_TM_ERROR_TYPE_NODE_ID;
		error->message = "invalid node id";
		return -EINVAL;
	}

	if (hinic3_tm_node_search(dev, node_id, &node_type)) {
		error->type = RTE_TM_ERROR_TYPE_NODE_ID;
		error->message = "node id already used";
		return -EINVAL;
	}

	if (priority) {
		error->type = RTE_TM_ERROR_TYPE_NODE_PRIORITY;
		error->message = "priority should be 0";
		return -EINVAL;
	}

	if (weight > 100) {
		error->type = RTE_TM_ERROR_TYPE_NODE_WEIGHT;
		error->message = "weight should <= 100";
		return -EINVAL;
	}

	if (params->shared_shaper_id) {
		error->type = RTE_TM_ERROR_TYPE_NODE_PARAMS_SHARED_SHAPER_ID;
		error->message = "shared shaper not supported";
		return -EINVAL;
	}
	if (params->n_shared_shapers) {
		error->type = RTE_TM_ERROR_TYPE_NODE_PARAMS_N_SHARED_SHAPERS;
		error->message = "shared shaper not supported";
		return -EINVAL;
	}

	if (node_id >= tm_conf->nb_leaf_nodes_max)
		return hinic3_tm_nonleaf_node_param_check(dev, params, error);
	else
		return hinic3_tm_leaf_node_param_check(dev, params, error);
}

static int
hinic3_tm_port_node_add(struct rte_eth_dev *dev, uint32_t node_id,
			uint32_t level_id,
#ifdef DPDK_24_11
			const struct rte_tm_node_params *params,
#else
			struct rte_tm_node_params *params,
#endif
			struct rte_tm_error *error)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct hinic3_ets *ets = nic_dev->ets;
	struct hinic3_tm_conf *tm_conf = &ets->tm_conf;
	struct hinic3_tm_node *tm_node;

	if (level_id != RTE_TM_NODE_LEVEL_ID_ANY &&
	    level_id != HINIC3_TM_NODE_LEVEL_PORT) {
		error->type = RTE_TM_ERROR_TYPE_NODE_PARAMS;
		error->message = "port node wrong level";
		return -EINVAL;
	}

	if (node_id != tm_conf->nb_nodes_max - 1) {
		error->type = RTE_TM_ERROR_TYPE_NODE_ID;
		error->message = "invalid port node ID";
		PMD_DRV_LOG(ERR, "node_id should be %d",
			    tm_conf->nb_nodes_max - 1);
		return -EINVAL;
	}

	if (tm_conf->root) {
		error->type = RTE_TM_ERROR_TYPE_NODE_PARENT_NODE_ID;
		error->message = "already have a root";
		return -EINVAL;
	}

	tm_node = rte_zmalloc("hinic3_tm_node", sizeof(struct hinic3_tm_node), 0);
	if (tm_node == NULL)
		return -ENOMEM;

	tm_node->id = node_id;
	tm_node->reference_count = 0;
	tm_node->parent = NULL;
	tm_node->shaper_profile =
		hinic3_tm_shaper_profile_search(dev, params->shaper_profile_id);
	memcpy(&tm_node->params, params, sizeof(struct rte_tm_node_params));
	tm_conf->root = tm_node;

	if (tm_node->shaper_profile)
		tm_node->shaper_profile->reference_count++;

	return 0;
}

static int
hinic3_tm_tc_node_add(struct rte_eth_dev *dev, uint32_t node_id, uint32_t weight,
		      uint32_t level_id, struct hinic3_tm_node *parent_node,
#ifdef DPDK_24_11
		      const struct rte_tm_node_params *params,
#else
		      struct rte_tm_node_params *params,
#endif
		      struct rte_tm_error *error)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct hinic3_ets *ets = nic_dev->ets;
	struct hinic3_tm_conf *tm_conf = &ets->tm_conf;
	struct hinic3_tm_node *tm_node;

	if (level_id != RTE_TM_NODE_LEVEL_ID_ANY &&
	    level_id != HINIC3_TM_NODE_LEVEL_TC) {
		error->type = RTE_TM_ERROR_TYPE_NODE_PARAMS;
		error->message = "tc node wrong level";
		return -EINVAL;
	}

	if (node_id < tm_conf->nb_leaf_nodes_max + HINIC3_MAX_COS_NUM ||
	    node_id >= tm_conf->nb_nodes_max - 1 ||
	    hinic3_tm_calc_node_tc_no(tm_conf, node_id) >= ets->num_tc) {
		error->type = RTE_TM_ERROR_TYPE_NODE_ID;
		error->message = "invalid tc node ID";
		return -EINVAL;
	}

	if (tm_conf->nb_tc_node >= ets->num_tc) {
		error->type = RTE_TM_ERROR_TYPE_NODE_ID;
		error->message = "too many TCs";
		return -EINVAL;
	}

	tm_node = rte_zmalloc("hinic3_tm_node", sizeof(struct hinic3_tm_node), 0);
	if (tm_node == NULL)
		return -ENOMEM;

	tm_node->id = node_id;
	tm_node->weight = weight;
	tm_node->reference_count = 0;
	tm_node->parent = parent_node;
	tm_node->shaper_profile =
		hinic3_tm_shaper_profile_search(dev, params->shaper_profile_id);
	memcpy(&tm_node->params, params, sizeof(struct rte_tm_node_params));
	TAILQ_INSERT_TAIL(&tm_conf->tc_list, tm_node, node);
	tm_conf->nb_tc_node++;
	tm_node->parent->reference_count++;

	if (tm_node->shaper_profile)
		tm_node->shaper_profile->reference_count++;

	return 0;
}

static int
hinic3_tm_cos_node_add(struct rte_eth_dev *dev, uint32_t node_id, uint32_t weight,
		       uint32_t level_id, struct hinic3_tm_node *parent_node,
#ifdef DPDK_24_11
		       const struct rte_tm_node_params *params,
#else
		       struct rte_tm_node_params *params,
#endif
		       struct rte_tm_error *error)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct hinic3_ets *ets = nic_dev->ets;
	struct hinic3_tm_conf *tm_conf = &ets->tm_conf;
	struct hinic3_tm_node *tm_node;

	if (level_id != RTE_TM_NODE_LEVEL_ID_ANY &&
	    level_id != HINIC3_TM_NODE_LEVEL_COS) {
		error->type = RTE_TM_ERROR_TYPE_NODE_PARAMS;
		error->message = "cos node wrong level";
		return -EINVAL;
	}

	if (node_id < tm_conf->nb_leaf_nodes_max ||
	    node_id >= tm_conf->nb_leaf_nodes_max + HINIC3_MAX_COS_NUM) {
		error->type = RTE_TM_ERROR_TYPE_NODE_ID;
		error->message = "invalid cos node ID";
		return -EINVAL;
	}

	if (tm_conf->nb_cos_node >= HINIC3_MAX_COS_NUM) {
		error->type = RTE_TM_ERROR_TYPE_NODE_ID;
		error->message = "too many cos nodes";
		return -EINVAL;
	}

	tm_node = rte_zmalloc("hinic3_tm_node", sizeof(struct hinic3_tm_node), 0);
	if (tm_node == NULL)
		return -ENOMEM;

	tm_node->id = node_id;
	tm_node->weight = weight;
	tm_node->reference_count = 0;
	tm_node->parent = parent_node;
	tm_node->shaper_profile =
		hinic3_tm_shaper_profile_search(dev, params->shaper_profile_id);
	memcpy(&tm_node->params, params, sizeof(struct rte_tm_node_params));
	TAILQ_INSERT_TAIL(&tm_conf->cos_list, tm_node, node);
	tm_conf->nb_cos_node++;
	tm_node->parent->reference_count++;

	if (tm_node->shaper_profile)
		tm_node->shaper_profile->reference_count++;

	return 0;
}

static int
hinic3_tm_queue_node_add(struct rte_eth_dev *dev, uint32_t node_id,
			 uint32_t weight, uint32_t level_id,
			 struct hinic3_tm_node *parent_node,
#ifdef DPDK_24_11
			 const struct rte_tm_node_params *params,
#else
			 struct rte_tm_node_params *params,
#endif
			 struct rte_tm_error *error)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct hinic3_ets *ets = nic_dev->ets;
	struct hinic3_tm_conf *tm_conf = &ets->tm_conf;
	struct hinic3_tm_node *tm_node;

	if (level_id != RTE_TM_NODE_LEVEL_ID_ANY &&
	    level_id != HINIC3_TM_NODE_LEVEL_QUEUE) {
		error->type = RTE_TM_ERROR_TYPE_NODE_PARAMS;
		error->message = "wrong level";
		return -EINVAL;
	}

	/* note: dev->data->nb_tx_queues <= max_tx_queues */
	if (node_id >= dev->data->nb_tx_queues) {
		error->type = RTE_TM_ERROR_TYPE_NODE_ID;
		error->message = "invalid queue node ID";
		return -EINVAL;
	}

	tm_node = rte_zmalloc("hinic3_tm_node", sizeof(struct hinic3_tm_node), 0);
	if (tm_node == NULL)
		return -ENOMEM;

	tm_node->id = node_id;
	tm_node->weight = weight;
	tm_node->reference_count = 0;
	tm_node->parent = parent_node;
	memcpy(&tm_node->params, params, sizeof(struct rte_tm_node_params));
	TAILQ_INSERT_TAIL(&tm_conf->queue_list, tm_node, node);
	tm_conf->nb_queue_node++;
	tm_node->parent->reference_count++;

	return 0;
}

/**
 * add port tm leaf node (port_id) (node_id) (parent_node_id) (priority)
 * (weight) (level_id) (shaper_profile_id) (cman_mode) (wred_profile_id)
 * (stats_mask) (n_shared_shapers) [(shared_shaper_id_0)
 * (shared_shaper_id_1)...]
 *
 * Add port tm leaf node.
 */
static int
hinic3_tm_node_add(struct rte_eth_dev *dev, uint32_t node_id,
		   uint32_t parent_node_id, uint32_t priority, uint32_t weight,
		   uint32_t level_id,
#ifdef DPDK_24_11
		   const struct rte_tm_node_params *params,
#else
		   struct rte_tm_node_params *params,
#endif
		   struct rte_tm_error *error)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct hinic3_ets *ets = nic_dev->ets;
	struct hinic3_tm_conf *tm_conf = &ets->tm_conf;
	enum hinic3_tm_node_type parent_node_type = HINIC3_TM_NODE_TYPE_MAX;
	struct hinic3_tm_node *parent_node;
	int ret;

	if (params == NULL || error == NULL)
		return -EINVAL;

	if (tm_conf->committed) {
		error->type = RTE_TM_ERROR_TYPE_UNSPECIFIED;
		error->message = "already committed";
		return -EINVAL;
	}

	ret = hinic3_tm_node_param_check(dev, node_id, priority, weight, params,
				      error);
	if (ret)
		return ret;

	/* root node who don't have a parent */
	if (parent_node_id == RTE_TM_NODE_ID_NULL)
		return hinic3_tm_port_node_add(dev, node_id, level_id, params,
					    error);

	parent_node =
		hinic3_tm_node_search(dev, parent_node_id, &parent_node_type);
	if (parent_node == NULL) {
		error->type = RTE_TM_ERROR_TYPE_NODE_PARENT_NODE_ID;
		error->message = "parent not exist";
		return -EINVAL;
	}

	if (parent_node_type != HINIC3_TM_NODE_TYPE_PORT &&
	    parent_node_type != HINIC3_TM_NODE_TYPE_TC &&
	    parent_node_type != HINIC3_TM_NODE_TYPE_COS) {
		error->type = RTE_TM_ERROR_TYPE_NODE_PARENT_NODE_ID;
		error->message = "parent is not port or TC or COS";
		return -EINVAL;
	}

	if (parent_node_type == HINIC3_TM_NODE_TYPE_PORT)
		return hinic3_tm_tc_node_add(dev, node_id, weight, level_id,
					  parent_node, params, error);
	else if (parent_node_type == HINIC3_TM_NODE_TYPE_TC)
		return hinic3_tm_cos_node_add(dev, node_id, weight, level_id,
					   parent_node, params, error);
	else
		return hinic3_tm_queue_node_add(dev, node_id, weight, level_id,
					     parent_node, params, error);
}

static void
hinic3_tm_node_do_delete(struct hinic3_nic_dev *nic_dev,
		      enum hinic3_tm_node_type node_type,
		      struct hinic3_tm_node *tm_node)
{
	struct hinic3_ets *ets = nic_dev->ets;
	struct hinic3_tm_conf *tm_conf = &ets->tm_conf;

	if (node_type == HINIC3_TM_NODE_TYPE_PORT) {
		if (tm_node->shaper_profile)
			tm_node->shaper_profile->reference_count--;
		rte_free(tm_node);
		tm_conf->root = NULL;
		return;
	}

	if (tm_node->shaper_profile)
		tm_node->shaper_profile->reference_count--;
	tm_node->parent->reference_count--;
	if (node_type == HINIC3_TM_NODE_TYPE_TC) {
		TAILQ_REMOVE(&tm_conf->tc_list, tm_node, node);
		tm_conf->nb_tc_node--;
	} else if (node_type == HINIC3_TM_NODE_TYPE_COS) {
		TAILQ_REMOVE(&tm_conf->cos_list, tm_node, node);
		tm_conf->nb_cos_node--;
	} else {
		TAILQ_REMOVE(&tm_conf->queue_list, tm_node, node);
		tm_conf->nb_queue_node--;
	}
	rte_free(tm_node);
}

/**
 * del port tm node (port_id) (node_id)
 *
 * Delete port tm node.
 */
static int
hinic3_tm_node_delete(struct rte_eth_dev *dev, uint32_t node_id,
		   struct rte_tm_error *error)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct hinic3_ets *ets = nic_dev->ets;
	struct hinic3_tm_conf *tm_conf = &ets->tm_conf;
	enum hinic3_tm_node_type node_type = HINIC3_TM_NODE_TYPE_MAX;
	struct hinic3_tm_node *tm_node;

	if (error == NULL)
		return -EINVAL;

	if (tm_conf->committed) {
		error->type = RTE_TM_ERROR_TYPE_UNSPECIFIED;
		error->message = "already committed";
		return -EINVAL;
	}

	tm_node = hinic3_tm_node_search(dev, node_id, &node_type);
	if (tm_node == NULL) {
		error->type = RTE_TM_ERROR_TYPE_NODE_ID;
		error->message = "no such node";
		return -EINVAL;
	}

	if (tm_node->reference_count) {
		error->type = RTE_TM_ERROR_TYPE_NODE_ID;
		error->message = "cannot delete a node which has children";
		return -EINVAL;
	}

	hinic3_tm_node_do_delete(nic_dev, node_type, tm_node);

	return 0;
}

/**
 * show port tm node type (port_id) (node_id)
 *
 * Display the port TM node type.
 */
static int
hinic3_tm_node_type_get(struct rte_eth_dev *dev, uint32_t node_id, int *is_leaf,
		     struct rte_tm_error *error)
{
	enum hinic3_tm_node_type node_type = HINIC3_TM_NODE_TYPE_MAX;
	struct hinic3_tm_node *tm_node;

	if (is_leaf == NULL || error == NULL)
		return -EINVAL;

	tm_node = hinic3_tm_node_search(dev, node_id, &node_type);
	if (tm_node == NULL) {
		error->type = RTE_TM_ERROR_TYPE_NODE_ID;
		error->message = "no such node";
		return -EINVAL;
	}

	if (node_type == HINIC3_TM_NODE_TYPE_QUEUE)
		*is_leaf = true;
	else
		*is_leaf = false;

	return 0;
}

static void
hinic3_tm_nonleaf_level_capabilities_get(struct rte_eth_dev *dev,
				      uint32_t level_id,
				      struct rte_tm_level_capabilities *cap)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct hinic3_ets *ets = nic_dev->ets;
	uint32_t max_tx_queues = hinic3_tm_max_tx_queues_get(dev);

	if (level_id == HINIC3_TM_NODE_LEVEL_PORT) {
		cap->n_nodes_max = 1;
		cap->n_nodes_nonleaf_max = 1;
		cap->n_nodes_leaf_max = 0;
	} else {
		cap->n_nodes_max = HINIC3_MAX_TC_NUM;
		cap->n_nodes_nonleaf_max = HINIC3_MAX_TC_NUM;
		cap->n_nodes_leaf_max = 0;
	}

	cap->non_leaf_nodes_identical = 1;
	cap->leaf_nodes_identical = 1;

	cap->nonleaf.shaper_private_supported = true;
	cap->nonleaf.shaper_private_dual_rate_supported = false;
	cap->nonleaf.shaper_private_rate_min = 0;
	cap->nonleaf.shaper_private_rate_max =
		hinic3_tm_rate_convert_firmware2tm(ets->max_tm_rate);
	cap->nonleaf.shaper_shared_n_max = 0;
	if (level_id == HINIC3_TM_NODE_LEVEL_PORT)
		cap->nonleaf.sched_n_children_max = HINIC3_MAX_TC_NUM;
	else
		cap->nonleaf.sched_n_children_max = max_tx_queues;
	cap->nonleaf.sched_sp_n_priorities_max = 1;
	cap->nonleaf.sched_wfq_n_children_per_group_max = 0;
	cap->nonleaf.sched_wfq_n_groups_max = 0;
	cap->nonleaf.sched_wfq_weight_max = 1;
	cap->nonleaf.stats_mask = 0;
}

static void
hinic3_tm_leaf_level_capabilities_get(struct rte_eth_dev *dev,
				   struct rte_tm_level_capabilities *cap)
{
	uint32_t max_tx_queues = hinic3_tm_max_tx_queues_get(dev);

	cap->n_nodes_max = max_tx_queues;
	cap->n_nodes_nonleaf_max = 0;
	cap->n_nodes_leaf_max = max_tx_queues;

	cap->non_leaf_nodes_identical = 1;
	cap->leaf_nodes_identical = 1;

	cap->leaf.shaper_private_supported = false;
	cap->leaf.shaper_private_dual_rate_supported = false;
	cap->leaf.shaper_private_rate_min = 0;
	cap->leaf.shaper_private_rate_max = 0;
	cap->leaf.shaper_shared_n_max = 0;
	cap->leaf.cman_head_drop_supported = false;
	cap->leaf.cman_wred_context_private_supported = false;
	cap->leaf.cman_wred_context_shared_n_max = 0;
	cap->leaf.stats_mask = 0;
}

/**
 * show port tm level cap (port_id) (level_id)
 *
 * Display the port TM hierarchical level capability.
 */
static int
hinic3_tm_level_capabilities_get(struct rte_eth_dev *dev, uint32_t level_id,
			      struct rte_tm_level_capabilities *cap,
			      struct rte_tm_error *error)
{
	if (cap == NULL || error == NULL)
		return -EINVAL;

	if (level_id >= HINIC3_TM_NODE_LEVEL_MAX) {
		error->type = RTE_TM_ERROR_TYPE_LEVEL_ID;
		error->message = "too deep level";
		return -EINVAL;
	}

	memset(cap, 0, sizeof(struct rte_tm_level_capabilities));

	if (level_id != HINIC3_TM_NODE_LEVEL_QUEUE)
		hinic3_tm_nonleaf_level_capabilities_get(dev, level_id, cap);
	else
		hinic3_tm_leaf_level_capabilities_get(dev, cap);

	return 0;
}

static void
hinic3_tm_nonleaf_node_capabilities_get(struct rte_eth_dev *dev,
				     enum hinic3_tm_node_type node_type,
				     struct rte_tm_node_capabilities *cap)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct hinic3_ets *ets = nic_dev->ets;
	uint32_t max_tx_queues = hinic3_tm_max_tx_queues_get(dev);

	cap->shaper_private_supported = true;
	cap->shaper_private_dual_rate_supported = false;
	cap->shaper_private_rate_min = 0;
	cap->shaper_private_rate_max =
		hinic3_tm_rate_convert_firmware2tm(ets->max_tm_rate);
	cap->shaper_shared_n_max = 0;

	if (node_type == HINIC3_TM_NODE_TYPE_PORT)
		cap->nonleaf.sched_n_children_max = HINIC3_MAX_TC_NUM;
	else
		cap->nonleaf.sched_n_children_max = max_tx_queues;
	cap->nonleaf.sched_sp_n_priorities_max = 1;
	cap->nonleaf.sched_wfq_n_children_per_group_max = 0;
	cap->nonleaf.sched_wfq_n_groups_max = 0;
	cap->nonleaf.sched_wfq_weight_max = 1;

	cap->stats_mask = 0;
}

static void
hinic3_tm_leaf_node_capabilities_get(struct rte_eth_dev *dev __rte_unused,
				  struct rte_tm_node_capabilities *cap)
{
	cap->shaper_private_supported = false;
	cap->shaper_private_dual_rate_supported = false;
	cap->shaper_private_rate_min = 0;
	cap->shaper_private_rate_max = 0;
	cap->shaper_shared_n_max = 0;

	cap->leaf.cman_head_drop_supported = false;
	cap->leaf.cman_wred_context_private_supported = false;
	cap->leaf.cman_wred_context_shared_n_max = 0;

	cap->stats_mask = 0;
}

/**
 * show port tm node cap (port_id) (node_id)
 *
 * Display the port TM node capability.
 */
static int
hinic3_tm_node_capabilities_get(struct rte_eth_dev *dev, uint32_t node_id,
			     struct rte_tm_node_capabilities *cap,
			     struct rte_tm_error *error)
{
	enum hinic3_tm_node_type node_type;
	struct hinic3_tm_node *tm_node;

	if (cap == NULL || error == NULL)
		return -EINVAL;

	tm_node = hinic3_tm_node_search(dev, node_id, &node_type);
	if (tm_node == NULL) {
		error->type = RTE_TM_ERROR_TYPE_NODE_ID;
		error->message = "no such node";
		return -EINVAL;
	}

	memset(cap, 0, sizeof(struct rte_tm_node_capabilities));

	if (node_type != HINIC3_TM_NODE_TYPE_QUEUE)
		hinic3_tm_nonleaf_node_capabilities_get(dev, node_type, cap);
	else
		hinic3_tm_leaf_node_capabilities_get(dev, cap);

	return 0;
}

static bool
hinic3_tm_configure_check(struct rte_eth_dev *dev, struct rte_tm_error *error)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct hinic3_ets *ets = nic_dev->ets;
	struct hinic3_tm_conf *tm_conf = &ets->tm_conf;
	struct hinic3_tm_node_list *tc_list = &tm_conf->tc_list;
	struct hinic3_tm_node_list *cos_list = &tm_conf->cos_list;
	struct hinic3_tm_node_list *queue_list = &tm_conf->queue_list;
	struct hinic3_tm_node *tm_node;

	/* TC */
	TAILQ_FOREACH (tm_node, tc_list, node) {
		if (hinic3_tm_calc_node_tc_no(tm_conf, tm_node->id) >=
		    ets->num_tc) {
			error->type = RTE_TM_ERROR_TYPE_NODE_ID;
			error->message = "node's TC not exist";
			return false;
		}
	}

	/* COS */
	TAILQ_FOREACH (tm_node, cos_list, node) {
		if (hinic3_tm_calc_node_cos_no(tm_conf, tm_node->id) >=
		    ets->num_tc) {
			error->type = RTE_TM_ERROR_TYPE_NODE_ID;
			error->message = "node's COS not exist";
			return false;
		}
	}

	/* Queue */
	TAILQ_FOREACH (tm_node, queue_list, node) {
		if (tm_node->id >= dev->data->nb_tx_queues) {
			error->type = RTE_TM_ERROR_TYPE_NODE_ID;
			error->message = "node's queue invalid";
			return false;
		}
	}

	return true;
}

static int
hinic3_tm_hierarchy_do_commit(struct hinic3_nic_dev *nic_dev,
			   struct rte_tm_error *error)
{
	struct hinic3_hwdev *hwdev = nic_dev->hwdev;
	struct hinic3_ets *ets = nic_dev->ets;
	struct hinic3_tm_conf *tm_conf = &ets->tm_conf;
	struct hinic3_tm_node_list *queue_list = &tm_conf->queue_list;
	struct hinic3_tm_node_list *cos_list = &tm_conf->cos_list;
	struct hinic3_tm_node_list *tc_list = &tm_conf->tc_list;
	struct hinic3_tm_node *tm_node;
	uint8_t queue_no;
	uint8_t cos_no;
	uint8_t tc_no;
	u8 cos_tc[NIC_DCB_COS_MAX] = {0};
	u8 *tc_bw = ets->tc_bw;
	u8 rate_limit[NIC_DCB_TC_MAX] = {0};
	u32 sum = 0;
	int ret;

	/*
	 * Generally, hinic3_update_tx_db_cos is used for config txq_cos when dcb
	 * enable. If config leaf node, use user configuration.
	 */
	TAILQ_FOREACH (tm_node, queue_list, node) {
		queue_no = tm_node->id;
		cos_no = tm_node->parent->id - tm_conf->nb_leaf_nodes_max;
		nic_dev->dcb->txq_cos[queue_no] = cos_no;
	}

	/* Set cos_tc. */
	TAILQ_FOREACH (tm_node, cos_list, node) {
		cos_no = tm_node->id - tm_conf->nb_leaf_nodes_max;
		tc_no = tm_node->parent->id - tm_conf->nb_leaf_nodes_max -
			HINIC3_MAX_COS_NUM;
		cos_tc[cos_no] = tc_no;
	}

	/* Set tc_bw and rate_limit. */
	TAILQ_FOREACH (tm_node, tc_list, node) {
		tc_no = tm_node->id - tm_conf->nb_leaf_nodes_max -
			HINIC3_MAX_COS_NUM;
		tc_bw[tc_no] = tm_node->weight;
		sum += tm_node->weight;
		if (tm_node->shaper_profile) {
			rate_limit[tc_no] =
				tm_node->shaper_profile->profile.peak.rate;
		}
	}

	/* All 0 bw means that all TCs are SP */
	if (sum != 100 && sum != 0) {
		PMD_DRV_LOG(ERR, "Invalid total tc bandwidth percent %u", sum);
		error->type = RTE_TM_ERROR_TYPE_NODE_PARAMS;
		error->message = "tc bandwidth error";
		return -EINVAL;
	}

	ret = hinic3_set_tm_hierarchy_do_commit(hwdev, cos_tc, tc_bw, rate_limit);

	if (ret) {
		PMD_DRV_LOG(ERR, "failed to config tc rate, ret = %d", ret);
		error->type = RTE_TM_ERROR_TYPE_NODE_PARAMS;
		error->message = "fail to set TC peak rate";
		return -EIO;
	}

	return ret;
}

/**
 * port tm hierarchy commit (port_id) (clean_on_fail)
 *
 * Commit tm hierarchy.
 */
static int
hinic3_tm_hierarchy_commit(struct rte_eth_dev *dev, int clear_on_fail,
			struct rte_tm_error *error)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct hinic3_ets *ets = nic_dev->ets;
	struct hinic3_tm_conf *tm_conf = &ets->tm_conf;

	if (error == NULL)
		return -EINVAL;

	if (tm_conf->root == NULL)
		goto done;

	/* check configure before commit make sure key configure not violated */
	if (!hinic3_tm_configure_check(dev, error)) {
		PMD_DRV_LOG(ERR, "hinic3 tm configure check failed");
		goto fail_clear;
	}

	if (hinic3_tm_hierarchy_do_commit(nic_dev, error))
		goto fail_clear;

done:
	tm_conf->committed = true;
	return 0;

fail_clear:
	if (clear_on_fail) {
		PMD_DRV_LOG(ERR, "tm hierarchy do commit failed");
		hinic3_tm_conf_uninit(dev);
		hinic3_tm_conf_init(dev);
	}
	return -EINVAL;
}

static int
hinic3_tm_node_shaper_do_update(struct hinic3_nic_dev *nic_dev, uint32_t node_id,
			     enum hinic3_tm_node_type node_type,
			     struct hinic3_tm_shaper_profile *shaper_profile,
			     struct rte_tm_error *error)
{
	struct hinic3_hwdev *hwdev = nic_dev->hwdev;
	struct hinic3_ets *ets = nic_dev->ets;
	struct hinic3_tm_conf *tm_conf = &ets->tm_conf;
	uint8_t tc_no;
	uint8_t rate;
	int ret;

	if (node_type == HINIC3_TM_NODE_TYPE_QUEUE) {
		if (shaper_profile != NULL) {
			error->type = RTE_TM_ERROR_TYPE_SHAPER_PROFILE_ID;
			error->message = "queue node shaper not supported";
			return -EINVAL;
		}
		return 0;
	}

	if (!tm_conf->committed)
		return 0;

	if (node_type != HINIC3_TM_NODE_TYPE_TC) {
		error->type = RTE_TM_ERROR_TYPE_SHAPER_PROFILE;
		error->message = "only support config tc rate limit";
		return -EINVAL;
	}

	/*
	 * update TC's shaper
	 */
	tc_no = hinic3_tm_calc_node_tc_no(tm_conf, node_id);
	if (shaper_profile)
		rate = (uint8_t)shaper_profile->profile.peak.rate;
	else
		rate = 0; /* 0 - tc_ratelimit: unlimited */

	ret = hinic3_set_tm_config_tc_rate(hwdev, tc_no, rate);
	if (ret) {
		error->type = RTE_TM_ERROR_TYPE_SHAPER_PROFILE;
		error->message = "fail to update TC peak rate";
	}

	return ret;
}

/**
 * set port tm node shaper profile (port_id) (node_id) (shaper_profile_id)
 *
 * Set port tm node shaper profile.
 */
static int
hinic3_tm_node_shaper_update(struct rte_eth_dev *dev, uint32_t node_id,
			  uint32_t shaper_profile_id,
			  struct rte_tm_error *error)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	enum hinic3_tm_node_type node_type = HINIC3_TM_NODE_TYPE_MAX;
	struct hinic3_tm_shaper_profile *profile = NULL;
	struct hinic3_tm_node *tm_node;

	if (error == NULL)
		return -EINVAL;

	tm_node = hinic3_tm_node_search(dev, node_id, &node_type);
	if (tm_node == NULL) {
		error->type = RTE_TM_ERROR_TYPE_NODE_ID;
		error->message = "no such node";
		return -EINVAL;
	}

	if (shaper_profile_id == tm_node->params.shaper_profile_id)
		return 0;

	if (shaper_profile_id != RTE_TM_SHAPER_PROFILE_ID_NONE) {
		profile = hinic3_tm_shaper_profile_search(dev, shaper_profile_id);
		if (profile == NULL) {
			error->type = RTE_TM_ERROR_TYPE_SHAPER_PROFILE_ID;
			error->message = "profile ID not exist";
			return -EINVAL;
		}
	}

	if (hinic3_tm_node_shaper_do_update(nic_dev, node_id, node_type, profile,
					 error))
		return -EINVAL;

	if (tm_node->shaper_profile)
		tm_node->shaper_profile->reference_count--;
	tm_node->shaper_profile = profile;
	tm_node->params.shaper_profile_id = shaper_profile_id;
	if (profile != NULL)
		profile->reference_count++;

	return 0;
}

static int
hinic3_tm_mark_vlan_dei(struct rte_eth_dev *dev, int mark_green, int mark_yellow,
		     int mark_red, struct rte_tm_error *error)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct hinic3_hwdev *hwdev = nic_dev->hwdev;

	if (mark_green != 0 || mark_yellow != 0 || mark_red != 0) {
		error->type = RTE_TM_ERROR_TYPE_WRED_PROFILE;
		error->message =
			"Not support config color, color value must be 0.";
		return -EINVAL;
	}

	nic_dev->dcb->hw_dcb_cfg.trust = HINIC3_DCB_PCP;

	return hinic3_set_qos_port_trust(hwdev, HINIC3_DCB_PCP);
}

static int
hinic3_tm_mark_ip_dscp(struct rte_eth_dev *dev, int mark_green, int mark_yellow,
		    int mark_red, struct rte_tm_error *error)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct hinic3_hwdev *hwdev = nic_dev->hwdev;

	if (mark_green != 0 || mark_yellow != 0 || mark_red != 0) {
		error->type = RTE_TM_ERROR_TYPE_WRED_PROFILE;
		error->message =
			"Not support config color, color value must be 0.";
		return -EINVAL;
	}

	nic_dev->dcb->hw_dcb_cfg.trust = HINIC3_DCB_DSCP;

	return hinic3_set_qos_port_trust(hwdev, HINIC3_DCB_DSCP);
}

static const struct rte_tm_ops hinic3_tm_ops = {
	.capabilities_get = hinic3_tm_capabilities_get,
	.shaper_profile_add = hinic3_tm_shaper_profile_add,
	.shaper_profile_delete = hinic3_tm_shaper_profile_del,
	.node_add = hinic3_tm_node_add,
	.node_delete = hinic3_tm_node_delete,
	.node_type_get = hinic3_tm_node_type_get,
	.level_capabilities_get = hinic3_tm_level_capabilities_get,
	.node_capabilities_get = hinic3_tm_node_capabilities_get,
	.hierarchy_commit = hinic3_tm_hierarchy_commit,
	.node_shaper_update = hinic3_tm_node_shaper_update,
	.mark_vlan_dei = hinic3_tm_mark_vlan_dei,
	.mark_ip_dscp = hinic3_tm_mark_ip_dscp,
};

int
hinic3_tm_ops_get(struct rte_eth_dev *dev, void *arg)
{
	if (dev == NULL || arg == NULL)
		return -EINVAL;

	*(const void **)arg = &hinic3_tm_ops;

	return 0;
}

void
hinic3_tm_dev_start_proc(struct rte_eth_dev *dev)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct hinic3_ets *ets = nic_dev->ets;
	struct hinic3_tm_conf *tm_conf = &ets->tm_conf;

	if (tm_conf->root && !tm_conf->committed)
		PMD_DRV_LOG(ERR, "please call hierarchy_commit() before "
				 "starting the port.");
}

/*
 * We need clear tm_conf committed flag when device stop so that user can modify
 * tm configuration (e.g. add or delete node).
 */
void
hinic3_tm_dev_stop_proc(struct rte_eth_dev *dev)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct hinic3_ets *ets = nic_dev->ets;
	struct hinic3_tm_conf *tm_conf = &ets->tm_conf;

	if (!tm_conf->committed)
		return;

	tm_conf->committed = false;
}

int
hinic3_tm_conf_update(struct rte_eth_dev *dev)
{
	struct hinic3_nic_dev *nic_dev = HINIC3_ETH_DEV_TO_PRIVATE_NIC_DEV(dev);
	struct hinic3_ets *ets = nic_dev->ets;
	struct hinic3_tm_conf *tm_conf = &ets->tm_conf;
	struct rte_tm_error error = {0};

	if (tm_conf->root == NULL || !tm_conf->committed)
		return 0;

	return hinic3_tm_hierarchy_do_commit(nic_dev, &error);
}
