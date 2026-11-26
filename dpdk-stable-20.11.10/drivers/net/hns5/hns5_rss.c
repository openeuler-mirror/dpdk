/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026-2027 Huawei Technologies Co., Ltd
 */

#include <rte_eal.h>

#include "hns5_poe.h"
#include "hns5_rss.h"
#include "hidrv_ppe_vf_api.h"

/* Default RSS Toeplitz hash key */
const uint8_t hns5_default_rss_key[HNS5_RSS_KEY_SIZE] = {
    0x6d, 0x5a, 0x56, 0xda, 0x25, 0x5b, 0x0e, 0xc2,
    0x41, 0x67, 0x25, 0x3d, 0x43, 0xa3, 0x8f, 0xb0,
    0xd0, 0xca, 0x2b, 0xcb, 0xae, 0x7b, 0x30, 0xb4,
    0x77, 0xcb, 0x2d, 0xa3, 0x80, 0x30, 0xf2, 0x0c,
    0x6a, 0x42, 0xb7, 0x3b, 0xbe, 0xac, 0x01, 0xfa};

/* RSS internal function declarations */
static int hns5_rss_enable_sequence(struct rte_eth_dev *dev);
static int hns5_rss_idt_alloc_to_ppe(struct rte_eth_dev *dev);
static int hns5_rss_idt_to_ppe(struct rte_eth_dev *dev);
static int hns5_rss_hash_to_ppe(struct rte_eth_dev *dev);
static int hns5_rss_ipv4_ipv6_enable_to_ppe(struct rte_eth_dev *dev);
static int hns5_rss_non_ip_enable_to_ppe(struct rte_eth_dev *dev);
static int hns5_rss_reset_indir_table(struct rte_eth_dev *dev);

/* See hns5_rss_lock() in hns5_rss.h for why this is global rather than per-port */
static rte_spinlock_t hns5_rss_rmw_lock = RTE_SPINLOCK_INITIALIZER;

void hns5_rss_lock(void)
{
    rte_spinlock_lock(&hns5_rss_rmw_lock);
}

void hns5_rss_unlock(void)
{
    rte_spinlock_unlock(&hns5_rss_rmw_lock);
}

/**
 * @brief Collect queue ids of RX queues with hardware backing
 * @param dev Ethernet device
 * @param[out] qid Array to store the queue ids, or NULL to only count.
 *             Size must be at least dev->data->nb_rx_queues entries.
 * @return Number of RX queues with hardware backing
 * @note Hairpin queues occupy queue indexes but have no hardware queue.
 *       They are skipped so hashed packets are never routed to them.
 */
static uint16_t hns5_rss_real_qid_collect(struct rte_eth_dev *dev, uint16_t *qid)
{
	struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);
	uint16_t real_num = 0;
	uint16_t i;

	for (i = 0; i < dev->data->nb_rx_queues; i++) {
		if (priv->rxq[i].is_hairpin == false) {
			if (qid != NULL)
				qid[real_num] = i;
			real_num++;
		}
	}

	return real_num;
}

/**
 * @brief Get the number of RX queues with hardware backing
 * @note Hairpin queues occupy queue indexes but have no hardware queue.
 *       They must be excluded from RSS distribution, otherwise hashed
 *       packets routed to them are lost.
 */
uint16_t hns5_rss_real_rxq_num(struct rte_eth_dev *dev)
{
	return hns5_rss_real_qid_collect(dev, NULL);
}

/**
 * @brief Send RSS hash config to PPE hardware
 */
static int hns5_rss_hash_to_ppe(struct rte_eth_dev *dev)
{
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);
    struct ndrv_ppp_rss_mst_hash_cfg mst_hash_cfg = {0};
    struct ndrv_ppp_rss_simple_hash_cfg simple_hash_cfg = {0};
    unsigned int ppe_tnl = (unsigned int)priv->param.cport_id;
    unsigned int ret;

    if (priv->rss.algo == RTE_ETH_HASH_FUNCTION_SIMPLE_XOR) {
        simple_hash_cfg.gen_mode = NDRV_PPP_RSS_USE_HASH_LSB_9BIT;
        simple_hash_cfg.gro_en = 0;
        memcpy(simple_hash_cfg.hash_key, priv->rss.key, HNS5_RSS_KEY_SIZE);
        ret = ndrv_ppe_ppp_pf_set_rss_simple_hash(priv->ppe_pfhandle, ppe_tnl, &simple_hash_cfg);
        if (ret != 0) {
            hns5_err(dev, "failed to set RSS simple hash, ret:%u\n", ret);
            return -EINVAL;
        }
    } else {
        mst_hash_cfg.sym_alg_en = (priv->rss.algo == RTE_ETH_HASH_FUNCTION_SYMMETRIC_TOEPLITZ) ? 1 : 0;
        mst_hash_cfg.sym_alg_mode_sel = 1;
        mst_hash_cfg.gro_en = 0;
        memcpy(mst_hash_cfg.hash_key, priv->rss.key, HNS5_RSS_KEY_SIZE);
        ret = ndrv_ppe_ppp_pf_set_rss_mst_hash(priv->ppe_pfhandle, ppe_tnl, &mst_hash_cfg);
        if (ret != 0) {
            hns5_err(dev, "failed to set RSS master hash, ret:%u\n", ret);
            return -EINVAL;
        }
    }

    return 0;
}

/**
 * @brief Send RSS indirect table allocation to PPE hardware
 */
static int hns5_rss_idt_alloc_to_ppe(struct rte_eth_dev *dev)
{
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);
    struct ndrv_ppp_rss_idt_alloc_cfg idt_alloc = {0};
    unsigned int ppe_tnl = (unsigned int)priv->param.cport_id;
    unsigned int ret;
    /* Fixed layout: hns5_rss_idt_to_ppe() programs block=idx/16, item=idx%16 */
    idt_alloc.block_offset = 0;
    idt_alloc.block_num = 64;  /* 64 blocks × 16 items/block = 1024 entries */

    ret = ndrv_ppe_ppp_pf_set_rss_idt_table_alloc(priv->ppe_pfhandle, ppe_tnl, &idt_alloc);
    if (ret != 0) {
        hns5_err(dev, "failed to set RSS IDT alloc, ret:%u\n", ret);
        return -EINVAL;
    }

    return 0;
}

/** @brief Send RSS indirect table entries to PPE hardware
 *  @note 1024 entries: block=idx/16, item=idx%16; each takes its queue id from
 *        priv->rss.reta (hardware-backed queues only), wrapping at reta_size. */
static int hns5_rss_idt_to_ppe(struct rte_eth_dev *dev)
{
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);
    unsigned int ret;
    uint16_t i;
    unsigned int tbl_idx, item_idx;

    if (priv->rss.reta_size == 0) {
        hns5_err(dev, "rss reta is not initialized, invalid to config RSS IDT table\n");
        return -EINVAL;
    }

    for (i = 0; i < HNS5_RSS_INDIRECT_TABLE_SIZE; i++) {
        tbl_idx = i / 16;    /* 1024 / 16 = 64 blocks */
        item_idx = i % 16;   /* 16 entries per block */
        ret = ndrv_ppe_ppp_pf_set_rss_idt_table(priv->ppe_pfhandle, tbl_idx, item_idx,
                                                  priv->rss.reta[i % priv->rss.reta_size]);
        if (ret != 0) {
            hns5_err(dev, "failed to set RSS IDT table[%u], ret:%u\n", i, ret);
            return -EIO;
        }
    }

    return 0;
}

int hns5_rss_reta_write(struct rte_eth_dev *dev)
{
    int ret;

    ret = hns5_rss_idt_alloc_to_ppe(dev);
    if (ret != 0)
        return ret;

    return hns5_rss_idt_to_ppe(dev);
}

static void hns5_rss_reta_init(struct rte_eth_dev *dev)
{
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);
    uint16_t real_qid[HNS5_ETH_MAX_RXQ_NUM];
    uint16_t nb_real_queues;
    uint16_t i;

    nb_real_queues = hns5_rss_real_qid_collect(dev, real_qid);
    if (nb_real_queues == 0) {
        priv->rss.reta_size = 0;
        return;
    }

    for (i = 0; i < HNS5_RSS_RETA_SIZE; i++) {
        uint16_t pos = i % nb_real_queues;

        /* Do not direct initial RSS traffic to deferred-start queues. */
        if (dev->data->rx_queue_state[real_qid[pos]] !=
            RTE_ETH_QUEUE_STATE_STARTED) {
            uint16_t n;

            for (n = 1; n < nb_real_queues; n++) {
                pos = (i + n) % nb_real_queues;
                if (dev->data->rx_queue_state[real_qid[pos]] ==
                    RTE_ETH_QUEUE_STATE_STARTED)
                    break;
            }
        }
        priv->rss.reta[i] = real_qid[pos];
    }
    priv->rss.reta_size = HNS5_RSS_RETA_SIZE;
}

/** @brief Calculate the tc_size value
 *  @param rx_queue_num RX queues assigned to a TC
 *  @return tc_size (2^tc_size = fw_tnl channels RSS can select); rounded up so
 *          the RSS slot count is a power of two even if nb_rx_queues is not. */
static unsigned int hns5_calc_tc_size(unsigned int rx_queue_num)
{
    unsigned int tc_size = 0;

    while (tc_size < 9 && (1U << tc_size) < rx_queue_num)
        tc_size++;

    return tc_size;
}

/**
 * @brief Parse and apply the RSS config to priv
 * @param dev Ethernet device
 * @param priv Ethernet device private data
 * @param rss_conf RSS config
 * @return 0 on success, non-zero on failure
 */
static int hns5_rss_parse_conf(struct rte_eth_dev *dev, struct hns5_eth_priv *priv,
                                 const struct rte_eth_rss_conf *rss_conf)
{
    uint64_t hf = rss_conf->rss_hf;
    if ((hf & ~HNS5_ETH_RSS_SUPPORT) != 0) {
        hns5_warn(dev, "RSS hf(0x%llx) contains unsupported flags, "
                    "unsupported:0x%llx, will be cleared\n",
                    (unsigned long long)hf, (unsigned long long)(hf & ~HNS5_ETH_RSS_SUPPORT));
        hf &= HNS5_ETH_RSS_SUPPORT;
    }
    priv->rss.hf = hf;

    if (rss_conf->rss_key != NULL && rss_conf->rss_key_len > 0) {
        uint8_t key_len = (rss_conf->rss_key_len < HNS5_RSS_KEY_SIZE) ?
                          rss_conf->rss_key_len : HNS5_RSS_KEY_SIZE;
        (void)memcpy(priv->rss.key, rss_conf->rss_key, key_len);
        priv->rss.key_len = key_len;
    }

    return 0;
}

/**
 * @brief Send RSS TC mode config to PPE hardware
 */
static int hns5_rss_tc_mode_to_ppe(struct rte_eth_dev *dev)
{
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);
    struct ndrv_ppp_rss_tc_mode_data tc_data = {0};
    unsigned int ret;
    unsigned int tc_id;
    unsigned int cport_id = (unsigned int)priv->param.cport_id;
    uint16_t rss_size_per_tc;
    unsigned int tc_size;
    unsigned int tc_offset;
    unsigned int base_tnl_offset;
    unsigned int enabled_tc_count = 0;

    for (tc_id = 0; tc_id < 8; tc_id++) {
        if (priv->hw_tc_map & (1 << tc_id))
            enabled_tc_count++;
    }

    if (enabled_tc_count == 0)
        return 0;

    rss_size_per_tc = hns5_rss_real_rxq_num(dev) / enabled_tc_count;
    tc_size = hns5_calc_tc_size(rss_size_per_tc);

    base_tnl_offset = (priv->fwl_tnl_inited == 1) ? priv->fwl_tnl_conf.base_tnl : 0;
    tc_offset = base_tnl_offset;

    for (tc_id = 0; tc_id < 8; tc_id++) {
        if (priv->hw_tc_map & (1 << tc_id)) {
            tc_data.tc_offset = tc_offset;
            tc_data.tc_size = tc_size;

            unsigned int next_offset = tc_offset + (1U << tc_size);
            if (next_offset > 256) {
                hns5_err(dev, "RSS TC%u config exceeds limit, "
                    "tc_offset=%u tc_size=%u\n",
                    tc_id, tc_offset, tc_size);
                return -EINVAL;
            }
            tc_offset = next_offset;

            ret = ndrv_ppe_ppp_pf_set_rss_tc_mode(priv->ppe_pfhandle, cport_id, tc_id, 1, &tc_data);
            if (ret != 0) {
                hns5_err(dev, "failed to set RSS TC mode %u, ret:%u\n", tc_id, ret);
                return -EINVAL;
            }
        }
    }

    return 0;
}

/**
 * @brief Send IPv4/IPv6 packet RSS enable config to PPE hardware
 */
static int hns5_rss_ipv4_ipv6_enable_to_ppe(struct rte_eth_dev *dev)
{
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);
    struct ndrv_ppe_lsw_rss_ipv4_en ipv4_en = {0};
    struct ndrv_ppe_lsw_rss_ipv6_en ipv6_en = {0};
    uint64_t hf = priv->rss.hf;
    unsigned int ret;

    if (hf & (ETH_RSS_NONFRAG_IPV4_TCP | ETH_RSS_IPV4)) {
        ipv4_en.tcp.src_ip_en = 1;
        ipv4_en.tcp.dst_ip_en = 1;
        ipv4_en.tcp.src_port_en = 1;
        ipv4_en.tcp.dst_port_en = 1;
    }
    if (hf & (ETH_RSS_NONFRAG_IPV4_UDP | ETH_RSS_IPV4)) {
        ipv4_en.udp.src_ip_en = 1;
        ipv4_en.udp.dst_ip_en = 1;
        ipv4_en.udp.src_port_en = 1;
        ipv4_en.udp.dst_port_en = 1;
    }
    if (hf & (ETH_RSS_NONFRAG_IPV4_SCTP | ETH_RSS_IPV4)) {
        ipv4_en.sctp.src_ip_en = 1;
        ipv4_en.sctp.dst_ip_en = 1;
        ipv4_en.sctp.veri_tag_en = 1;
    }
    if (hf & (ETH_RSS_NONFRAG_IPV4_OTHER | ETH_RSS_IPV4)) {
        ipv4_en.non_ipv4.src_ip_en = 1;
        ipv4_en.non_ipv4.dst_ip_en = 1;
        ipv4_en.non_ipv4.prot_en = 1;
    }
    if (hf & (ETH_RSS_FRAG_IPV4 | ETH_RSS_IPV4)) {
        ipv4_en.frag_prot.src_ip_en = 1;
        ipv4_en.frag_prot.dst_ip_en = 1;
    }

    if (hf & (ETH_RSS_NONFRAG_IPV6_TCP | ETH_RSS_IPV6)) {
        ipv6_en.tcp.src_ip_en = 1;
        ipv6_en.tcp.dst_ip_en = 1;
        ipv6_en.tcp.src_port_en = 1;
        ipv6_en.tcp.dst_port_en = 1;
    }
    if (hf & (ETH_RSS_NONFRAG_IPV6_UDP | ETH_RSS_IPV6)) {
        ipv6_en.udp.src_ip_en = 1;
        ipv6_en.udp.dst_ip_en = 1;
        ipv6_en.udp.src_port_en = 1;
        ipv6_en.udp.dst_port_en = 1;
    }
    if (hf & (ETH_RSS_NONFRAG_IPV6_SCTP | ETH_RSS_IPV6)) {
        ipv6_en.sctp.src_ip_en = 1;
        ipv6_en.sctp.dst_ip_en = 1;
        ipv6_en.sctp.src_veri_tag_en = 1;
    }
    if (hf & (ETH_RSS_NONFRAG_IPV6_OTHER | ETH_RSS_IPV6)) {
        ipv6_en.non_ipv6.src_ip_en = 1;
        ipv6_en.non_ipv6.dst_ip_en = 1;
        ipv6_en.non_ipv6.next_header_en = 1;
    }
    if (hf & (ETH_RSS_FRAG_IPV6 | ETH_RSS_IPV6)) {
        ipv6_en.frag_prot.src_ip_en = 1;
        ipv6_en.frag_prot.dst_ip_en = 1;
    }

    ret = ndrv_ppe_lsw_pf_rss_set_ipv4_enable(priv->ppe_pfhandle, &ipv4_en);
    if (ret != 0) {
        hns5_err(dev, "failed to set RSS IPv4 enable, ret:%u\n", ret);
        return -EINVAL;
    }

    ret = ndrv_ppe_lsw_pf_rss_set_ipv6_enable(priv->ppe_pfhandle, &ipv6_en);
    if (ret != 0) {
        hns5_err(dev, "failed to set RSS IPv6 enable, ret:%u\n", ret);
        return -EINVAL;
    }

    return 0;
}

/**
 * @brief Send non-IP packet RSS enable config to PPE hardware
 */
static int hns5_rss_non_ip_enable_to_ppe(struct rte_eth_dev *dev)
{
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);
    union ndrv_ppe_rss_non_ip_en non_ip_en = {0};
    unsigned int ret;

    if (priv->rss.hf & ETH_RSS_L2_PAYLOAD) {
        non_ip_en.dmac_en = 1;
        non_ip_en.smac_en = 1;
        non_ip_en.vlan1_en = 1;
        non_ip_en.vlan2_en = 1;
        non_ip_en.eth_type_en = 1;
    }

    ret = ndrv_ppe_lsw_pf_rss_set_non_ip_enable(priv->ppe_pfhandle, &non_ip_en);
    if (ret != 0) {
        hns5_err(dev, "failed to set RSS non-IP enable, ret:%u\n", ret);
        return -EINVAL;
    }

    return 0;
}

/** @brief Enable RSS distribution when no distribution flow table entry is hit
 *  @param dev Ethernet device
 *  @note Stopped queues rely on their own fw_tnl drop, so this never needs drop. */
int hns5_rss_fw_not_hit_actn_to_ppe(struct rte_eth_dev *dev)
{
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);
    unsigned int ret;

    ret = ndrv_ppe_ppp_pf_set_fw_not_hit_actn(priv->ppe_pfhandle, 1, NULL);
    if (ret != 0) {
        hns5_err(dev, "failed to set fw not hit action, ret:%u\n", ret);
        return -EINVAL;
    }

    return 0;
}

/**
 * @brief Reset the RSS indirect table (all zeros)
 * @param dev Ethernet device
 * @return 0 on success, non-zero on failure
 */
static int hns5_rss_reset_indir_table(struct rte_eth_dev *dev)
{
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);
    unsigned int ret;
    uint16_t i;
    unsigned int tbl_idx, item_idx;

    for (i = 0; i < HNS5_RSS_INDIRECT_TABLE_SIZE; i++) {
        tbl_idx = i / 16;
        item_idx = i % 16;
        ret = ndrv_ppe_ppp_pf_set_rss_idt_table(priv->ppe_pfhandle,
            tbl_idx, item_idx, 0);
        if (ret != 0) {
            hns5_err(dev, "failed to reset RSS IDT table[%u], ret:%u\n", i, ret);
            return -EINVAL;
        }
    }

    return 0;
}

/**
 * @brief Full RSS enable sequence
 * @note Call order:
 *   1. Hash algorithm config
 *   2. Indirect table allocation
 *   3. Indirect table entries
 *   4. TC mode config
 *   5. Packet type enable
 */
static int hns5_rss_enable_sequence(struct rte_eth_dev *dev)
{
    int ret;

    ret = hns5_rss_idt_alloc_to_ppe(dev);
    if (ret != 0)
        return ret;

    ret = hns5_rss_idt_to_ppe(dev);
    if (ret != 0)
        return ret;

    ret = hns5_rss_tc_mode_to_ppe(dev);
    if (ret != 0)
        return ret;

    ret = hns5_rss_ipv4_ipv6_enable_to_ppe(dev);
    if (ret != 0)
        return ret;

    ret = hns5_rss_non_ip_enable_to_ppe(dev);
    if (ret != 0)
        return ret;

    ret = hns5_rss_fw_not_hit_actn_to_ppe(dev);
    if (ret != 0)
        return ret;

    ret = hns5_rss_hash_to_ppe(dev);
    if (ret != 0)
        return ret;
    return 0;
}

/**
 * @brief Set RSS default parameters
 * @param priv Ethernet device private data
 * @note Only set software config, no hardware write
 */
void hns5_rss_set_default_args(struct hns5_eth_priv *priv)
{
    priv->rss.hf = 0;
    priv->rss.algo = RTE_ETH_HASH_FUNCTION_TOEPLITZ;
    (void)memcpy(priv->rss.key, hns5_default_rss_key, HNS5_RSS_KEY_SIZE);
    priv->rss.key_len = HNS5_RSS_KEY_SIZE;
    priv->rss.reta_size = 0;
}

/**
 * @brief Apply RSS config from dev_conf to priv->rss
 * @param dev Ethernet device
 * @return 0 on success, non-zero on failure
 * @note Only update software config, no hardware write
 */
int hns5_rss_apply_conf(struct rte_eth_dev *dev)
{
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);
    struct rte_eth_rss_conf *rss_conf = &dev->data->dev_conf.rx_adv_conf.rss_conf;
    int ret;

    ret = hns5_rss_parse_conf(dev, priv, rss_conf);
    if (ret != 0)
        return ret;
    return 0;
}

/**
 * @brief Apply RSS software config to hardware
 * @param dev Ethernet device
 * @return 0 on success, non-zero on failure
 * @note Read config from priv->rss and write to hardware; use default if not initialized
 */
int hns5_config_rss(struct rte_eth_dev *dev)
{
    int ret;

    if (hns5_rss_real_rxq_num(dev) == 0) {
        hns5_err(dev, "no hardware rx queue, invalid to config rss\n");
        return -EINVAL;
    }

    /* Serialize against peer ports that program the shared PPE indirect table */
    hns5_rss_lock();
    /* dev_start puts every queue back to STARTED, so always rebuild the default
     * equal-share table over all of them; a table narrowed by a previous queue
     * stop must not survive the restart, or those queues never get traffic. */
    hns5_rss_reta_init(dev);

    ret = hns5_rss_enable_sequence(dev);
    hns5_rss_unlock();
    if (ret != 0) {
        hns5_err(dev, "failed to init RSS, ret:%d\n", ret);
        return ret;
    }

    return 0;
}

/**
 * @brief RSS de-initialization
 * @param dev Ethernet device
 */
void hns5_rss_uninit(struct rte_eth_dev *dev)
{
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);

    /* The IDT is shared per PPE: wipe it only for the last remaining port,
     * else closing one port steers the siblings' traffic to queue 0. */
    if (!hns5_eth_has_ppe_peer(dev)) {
        hns5_rss_lock();
        (void)hns5_rss_reset_indir_table(dev);
        hns5_rss_unlock();
    }

    /* Clear RSS config */
    priv->rss.hf = 0;
    priv->rss.reta_size = 0;
}

int hns5_rss_reta_restore(struct rte_eth_dev *dev, const uint16_t *old_reta)
{
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);
    int ret;

    if (priv->rss.reta_size == 0)
        return 0;

    memcpy(priv->rss.reta, old_reta, sizeof(priv->rss.reta));
    ret = hns5_rss_reta_write(dev);
    if (ret != 0)
        hns5_err(dev, "failed to roll back RSS RETA, ret:%d\n", ret);
    return ret;
}

/* Rebuild the default equal-share RETA over the queues active after applying
 * `started` to queue_id and program it on change. A stopped queue's traffic is
 * discarded by its fw_tnl drop, so the PPE-wide not-hit action stays
 * distribute. */
static int hns5_rss_reta_rebuild(struct rte_eth_dev *dev, uint16_t queue_id, bool started)
{
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);
    uint16_t old_reta[HNS5_RSS_RETA_SIZE];
    uint16_t active[HNS5_ETH_MAX_RXQ_NUM];
    uint16_t active_num = 0;
    uint16_t i, q;
    bool changed = false;
    int ret;

    for (q = 0; q < dev->data->nb_rx_queues; q++) {
        if (q == queue_id ? started :
            dev->data->rx_queue_state[q] == RTE_ETH_QUEUE_STATE_STARTED)
            active[active_num++] = q;
    }
    if (active_num == 0)
        return 0;

    memcpy(old_reta, priv->rss.reta, sizeof(old_reta));

    /* Round-robin over the active queues gives every queue an equal share. */
    for (i = 0; i < HNS5_RSS_RETA_SIZE; i++) {
        q = active[i % active_num];
        if (priv->rss.reta[i] != q) {
            priv->rss.reta[i] = q;
            changed = true;
        }
    }

    if (changed) {
        ret = hns5_rss_reta_write(dev);
        if (ret != 0) {
            (void)hns5_rss_reta_restore(dev, old_reta);
            return ret;
        }
    }

    return 0;
}

/* Detach a stopping queue from RSS: rebuild RETA evenly over the remaining
 * started queues. Only the default equal-share layout is maintained here;
 * weighted layouts are left to the application. */
int hns5_rss_queue_detach(struct rte_eth_dev *dev, uint16_t queue_id)
{
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);

    if (priv->rss.reta_size == 0)
        return 0;

    return hns5_rss_reta_rebuild(dev, queue_id, false);
}

/* Attach a starting queue to RSS: rebuild RETA evenly over the started queues
 * plus the queue being attached. Only the default equal-share layout is
 * maintained here; weighted layouts are left to the application. */
int hns5_rss_queue_attach(struct rte_eth_dev *dev, uint16_t queue_id)
{
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);

    if (priv->rss.reta_size == 0)
        return 0;

    return hns5_rss_reta_rebuild(dev, queue_id, true);
}

/**
 * @brief Update RSS RETA table
 * @note Write to hardware immediately
 */
int hns5_rss_reta_update(struct rte_eth_dev *dev, struct rte_eth_rss_reta_entry64 *reta_conf,
                          uint16_t reta_size)
{
    struct hns5_eth_priv *priv;
    uint16_t old_reta[HNS5_RSS_RETA_SIZE];
    uint16_t old_reta_size;
    uint16_t i, idx, shift;
    uint16_t nb_rx_queues;
    int ret = 0;

    if (dev == NULL || reta_conf == NULL) {
        hns5_err(dev, "invalid parameter\n");
        return -EINVAL;
    }
    HNS5_SECONDARY_EPERM(dev, "rss reta update");

    if (reta_size == 0 || reta_size > HNS5_RSS_RETA_SIZE) {
        hns5_err(dev, "invalid reta_size:%u\n", reta_size);
        return -EINVAL;
    }

    priv = hns5_eth_priv_get(dev);
    nb_rx_queues = dev->data->nb_rx_queues;
    hns5_rss_lock();

    /* Validate the complete request before changing software or hardware state. */
    for (i = 0; i < reta_size; i++) {
        idx = i / HNS5_RSS_RETA_GROUP_SIZE;
        shift = i % HNS5_RSS_RETA_GROUP_SIZE;
        if ((reta_conf[idx].mask & (1ULL << shift)) == 0)
            continue;

        uint16_t queue = (uint16_t)reta_conf[idx].reta[shift];
        if (queue >= nb_rx_queues) {
            hns5_err(dev, "reta[%u] queue:%u >= nb_rx_queues:%u\n",
                i, queue, nb_rx_queues);
            ret = -EINVAL;
            goto out;
        }
        if (priv->rxq[queue].is_hairpin) {
            hns5_err(dev, "reta[%u] queue:%u is hairpin\n", i, queue);
            ret = -EINVAL;
            goto out;
        }
        if (dev->data->dev_started &&
            dev->data->rx_queue_state[queue] != RTE_ETH_QUEUE_STATE_STARTED) {
            hns5_err(dev, "reta[%u] points to stopped queue:%u\n", i, queue);
            ret = -EINVAL;
            goto out;
        }
    }

    if (priv->rss.reta_size == 0)
        hns5_rss_reta_init(dev);
    memcpy(old_reta, priv->rss.reta, sizeof(old_reta));
    old_reta_size = priv->rss.reta_size;

    for (i = 0; i < reta_size; i++) {
        idx = i / HNS5_RSS_RETA_GROUP_SIZE;
        shift = i % HNS5_RSS_RETA_GROUP_SIZE;
        if (reta_conf[idx].mask & (1ULL << shift))
            priv->rss.reta[i] = (uint16_t)reta_conf[idx].reta[shift];
    }
    priv->rss.reta_size = HNS5_RSS_RETA_SIZE;

    ret = hns5_rss_reta_write(dev);
    if (ret != 0) {
        memcpy(priv->rss.reta, old_reta, sizeof(priv->rss.reta));
        priv->rss.reta_size = old_reta_size;
        if (hns5_rss_reta_write(dev) != 0)
            hns5_err(dev, "failed to roll back RSS RETA update\n");
    }

out:
    hns5_rss_unlock();
    return ret;
}

/**
 * @brief Query RSS RETA table
 */
int hns5_rss_reta_query(struct rte_eth_dev *dev, struct rte_eth_rss_reta_entry64 *reta_conf,
                         uint16_t reta_size)
{
    struct hns5_eth_priv *priv;
    uint16_t i, idx, shift;

    if (dev == NULL || reta_conf == NULL) {
        hns5_err(dev, "invalid parameter\n");
        return -EINVAL;
    }
    if (reta_size == 0 || reta_size > HNS5_RSS_RETA_SIZE) {
        hns5_err(dev, "invalid reta_size:%u\n", reta_size);
        return -EINVAL;
    }

    priv = hns5_eth_priv_get(dev);
    hns5_rss_lock();
    for (i = 0; i < reta_size && i < priv->rss.reta_size; i++) {
        idx = i / HNS5_RSS_RETA_GROUP_SIZE;
        shift = i % HNS5_RSS_RETA_GROUP_SIZE;
        if (reta_conf[idx].mask & (1ULL << shift))
            reta_conf[idx].reta[shift] = priv->rss.reta[i];
    }
    hns5_rss_unlock();

    return 0;
}

/**
 * @brief Update RSS hash algorithm and key
 * @note Write to hardware immediately if configured; otherwise update software state only
 */
int hns5_rss_hash_update(struct rte_eth_dev *dev, struct rte_eth_rss_conf *rss_conf)
{
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);
    int ret;

    if (dev == NULL || rss_conf == NULL) {
        hns5_err(dev, "invalid parameter\n");
        return -EINVAL;
    }

    HNS5_SECONDARY_EPERM(dev, "rss hash update");

    ret = hns5_rss_parse_conf(dev, priv, rss_conf);
    if (ret != 0)
        return ret;

    ret = hns5_rss_ipv4_ipv6_enable_to_ppe(dev);
    if (ret != 0)
        return ret;
    ret = hns5_rss_non_ip_enable_to_ppe(dev);
    if (ret != 0)
        return ret;
    return hns5_rss_hash_to_ppe(dev);
}

/**
 * @brief Query RSS hash config
 */
int hns5_rss_hash_conf_get(struct rte_eth_dev *dev, struct rte_eth_rss_conf *rss_conf)
{
    struct hns5_eth_priv *priv = hns5_eth_priv_get(dev);

    if (dev == NULL || rss_conf == NULL) {
        hns5_err(dev, "invalid parameter\n");
        return -EINVAL;
    }

    rss_conf->rss_hf = priv->rss.hf;
    if (rss_conf->rss_key != NULL && rss_conf->rss_key_len > 0) {
        uint8_t key_len = (rss_conf->rss_key_len < priv->rss.key_len) ?
                          rss_conf->rss_key_len : priv->rss.key_len;
        (void)memcpy(rss_conf->rss_key, priv->rss.key, key_len);
        rss_conf->rss_key_len = key_len;
    } else {
        rss_conf->rss_key_len = priv->rss.key_len;
    }

    return 0;
}
