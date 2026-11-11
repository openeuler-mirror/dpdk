/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2026 Huawei Technologies Co., Ltd
 *
 * DPDK vdev front-end. Probe acquires the shared HugeTLB/BMU runtime and
 * brings up the SEC PF/VF + POE submit/completion groups; each queue pair
 * keeps its own in-flight ring.
 */

#include "hns5_crypto_pmd_private.h"

/* 逐 op 计时开关，由 devarg stats= 设置（默认 0：关闭计时，只保留计数）*/
bool hns5_sec_stats_en;

#include <rte_string_fns.h>
#include <rte_malloc.h>
#include <rte_kvargs.h>
#include <strings.h>
#include <stdlib.h>

#include "hidrv_log_api.h"
#include "hns5_sec_debug.h"

static uint8_t hns5_cryptodev_driver_id;

static void
hns5_dev_info_get(struct rte_cryptodev *dev,
		  struct rte_cryptodev_info *info)
{
	struct hns5_dev_private *priv = dev->data->dev_private;

	RTE_SET_USED(dev);
	if (info == NULL)
		return;

	info->driver_id = hns5_cryptodev_driver_id;
	info->feature_flags = dev->feature_flags;
	info->capabilities = hns5_caps_get();
	info->max_nb_queue_pairs = priv->max_qps;
	info->sym.max_nb_sessions = 0;
}

static int
hns5_dev_config(struct rte_cryptodev *dev __rte_unused,
		struct rte_cryptodev_config *config __rte_unused)
{
	return 0;
}

static int
hns5_dev_start(struct rte_cryptodev *dev __rte_unused)
{
	return 0;
}

static void
hns5_dev_stop(struct rte_cryptodev *dev __rte_unused)
{
}

/*
 * Tear the device down: SEC engine + shared BMU runtime. Idempotent, because
 * the cryptodev teardown path can reach it more than once (explicit close,
 * then rte_cryptodev_pmd_release_device() while removing the device).
 */
static int
hns5_crypto_teardown(struct rte_cryptodev *dev)
{
	struct hns5_dev_private *priv = dev->data->dev_private;

	if (priv == NULL)
		return 0;

	{
		uint16_t i;

		/* In-flight state is per queue pair now, so ask each of them. */
		for (i = 0; i < priv->nb_used_qps; i++) {
			if (priv->qps[i] != NULL && priv->qps[i]->in_use) {
				HNS5_LOG(ERR, "queue pair %u closed with in-flight "
					 "work; refusing teardown", i);
				return -EBUSY;
			}
		}
	}

	if (priv->sec.sec_vf_started || priv->sec.sec_pf_started) {
		uint16_t c;

		for (c = priv->nb_chans; c > 1; c--)
			(void)hns5_sec_platform_close(&priv->extra[c - 2]);
		hns5_sec_platform_close(&priv->sec);
	}
	if (priv->common_runtime_acquired) {
		priv->common_runtime_acquired = false;
		hns5_runtime_release(priv->sec.common);
		priv->sec.common = NULL;
	}
	return 0;
}

static int
hns5_dev_close(struct rte_cryptodev *dev)
{
	return hns5_crypto_teardown(dev);
}

static void
hns5_stats_get(struct rte_cryptodev *dev, struct rte_cryptodev_stats *stats)
{
	struct hns5_dev_private *priv = dev->data->dev_private;
	uint16_t i;

	if (stats == NULL)
		return;

	memset(stats, 0, sizeof(*stats));
	/*
	 * Sum the per queue pair counters with relaxed atomic loads: the workers
	 * keep updating them while the application reads. Each value is a
	 * self-consistent 64-bit snapshot, and the sum is taken one queue pair at
	 * a time, which is what a statistics read is expected to provide.
	 */
	for (i = 0; i < priv->nb_used_qps; i++) {
		struct hns5_qp *qp = priv->qps[i];

		if (qp == NULL)
			continue;
		stats->enqueued_count += hns5_stat_load(&qp->stats.enqueued_count);
		stats->dequeued_count += hns5_stat_load(&qp->stats.dequeued_count);
		stats->enqueue_err_count +=
			hns5_stat_load(&qp->stats.enqueue_err_count);
		stats->dequeue_err_count +=
			hns5_stat_load(&qp->stats.dequeue_err_count);
	}
}

static void
hns5_stats_reset(struct rte_cryptodev *dev)
{
	struct hns5_dev_private *priv = dev->data->dev_private;
	uint16_t i;

	if (priv == NULL)
		return;

	/*
	 * Clear field by field. A memset would race with a worker that is between
	 * "account for this op" and its next increment, and the whole-structure
	 * store could also wipe an increment that landed after the reset.
	 */
	for (i = 0; i < priv->nb_used_qps; i++) {
		struct hns5_qp *qp = priv->qps[i];

		if (qp == NULL)
			continue;
		hns5_stat_clear(&qp->stats.enqueued_count);
		hns5_stat_clear(&qp->stats.dequeued_count);
		hns5_stat_clear(&qp->stats.enqueue_err_count);
		hns5_stat_clear(&qp->stats.dequeue_err_count);
	}
}

static int
hns5_qp_setup(struct rte_cryptodev *dev, uint16_t qp_id,
	      const struct rte_cryptodev_qp_conf *qp_conf,
	      int socket_id __rte_unused)
{
	struct hns5_dev_private *priv = dev->data->dev_private;
	struct hns5_qp *qp;

	if (qp_id >= priv->max_qps || qp_id >= priv->nb_chans) {
		HNS5_LOG(ERR, "qp %u rejected: max_qps=%u channels_started=%u",
			 qp_id, priv->max_qps, priv->nb_chans);
		return -EINVAL;
	}

	qp = rte_zmalloc_socket("hns5 qp", sizeof(*qp), 0, socket_id);
	if (qp == NULL)
		return -ENOMEM;

	/*
	 * The ring only has to hold what can actually be in flight: the in-flight
	 * cap plus one burst of slack. Allocating the application's full
	 * nb_descriptors (often 1024 or 8192) wasted hundreds of KiB per queue
	 * pair, because the driver never keeps more than the cap outstanding.
	 */
	{
		uint32_t cap = priv->inflight_cap;
		uint32_t want = cap + HNS5_QP_RING_SLACK;
		uint32_t hint = qp_conf->nb_descriptors;

		/* Honour a smaller hint, but never below cap + 1: the ring must be
		 * able to hold everything the cap allows. */
		if (hint <= cap) {
			want = cap + 1U;
			HNS5_LOG(DEBUG, "qp %u: nb_descriptors=%u is not above the "
				 "in-flight cap %u, using an internal ring of %u",
				 qp_id, hint, cap, want);
		} else if (hint < want) {
			want = hint;
		}
		qp->depth = (uint16_t)want;
	}
	qp->ring = rte_zmalloc_socket("hns5 inflight",
				      sizeof(struct hns5_inflight_op) * qp->depth,
				      0, socket_id);
	if (qp->ring == NULL) {
		rte_free(qp);
		return -ENOMEM;
	}

	qp->id = qp_id;
	qp->max_inflight = priv->inflight_cap;
	/* Multi-QP shares 448 buffers: a smaller per-QP cache leaves more for
	 * in-flight requests, which removes the residual partial enqueues. */
	qp->cache.limit = priv->buf_cache;
	qp->sec = (qp_id == 0) ? &priv->sec : &priv->extra[qp_id - 1];
	qp->sec->priv = priv;
	rte_spinlock_init(&qp->lock);
	if (priv->sec.smooth) {
		qp->bl = rte_zmalloc_socket("hns5 backlog",
					    sizeof(*qp->bl) * HNS5_SEC_BACKLOG,
					    0, socket_id);
		if (qp->bl == NULL) {
			HNS5_LOG(ERR, "qp %u: cannot allocate the backlog",
				 qp_id);
			rte_free(qp->ring);
			rte_free(qp);
			return -ENOMEM;
		}
		qp->bl_size = HNS5_SEC_BACKLOG;
	}
	hns5_qp_cache_prefill(qp);
	snprintf(qp->name, sizeof(qp->name), "hns5_qp_%u", qp_id);
	priv->qps[qp_id] = qp;
	dev->data->queue_pairs[qp_id] = qp;
	if (qp_id + 1 > priv->nb_used_qps)
		priv->nb_used_qps = qp_id + 1;
	return 0;
}

static int
hns5_qp_release(struct rte_cryptodev *dev, uint16_t qp_id)
{
	struct hns5_dev_private *priv = dev->data->dev_private;
	struct hns5_qp *qp;

	if (qp_id >= priv->max_qps)
		return 0;
	qp = priv->qps[qp_id];
	if (qp == NULL)
		return 0;
	if (qp->inflight != 0)
		HNS5_LOG(ERR, "releasing qp %u with %u ops in flight", qp_id, qp->inflight);
	hns5_qp_cache_flush(qp);
	rte_free(qp->ring);
	rte_free(qp);
	rte_free(qp->bl);
	qp->bl = NULL;
	priv->qps[qp_id] = NULL;
	dev->data->queue_pairs[qp_id] = NULL;
	return 0;
}

static unsigned int
hns5_sym_session_get_size(struct rte_cryptodev *dev __rte_unused)
{
	return sizeof(struct hns5_session);
}

static int
hns5_sym_session_configure(struct rte_cryptodev *dev __rte_unused,
			   struct rte_crypto_sym_xform *xform,
			   struct rte_cryptodev_sym_session *sess)
{
	return hns5_session_configure(xform, CRYPTODEV_GET_SYM_SESS_PRIV(sess));
}

static void
hns5_sym_session_clear(struct rte_cryptodev *dev __rte_unused,
		       struct rte_cryptodev_sym_session *sess)
{
	hns5_session_clear(CRYPTODEV_GET_SYM_SESS_PRIV(sess));
}

static struct rte_cryptodev_ops hns5_crypto_pmd_ops = {
	.dev_configure		= hns5_dev_config,
	.dev_start		= hns5_dev_start,
	.dev_stop		= hns5_dev_stop,
	.dev_close		= hns5_dev_close,
	.stats_get		= hns5_stats_get,
	.stats_reset		= hns5_stats_reset,
	.dev_infos_get		= hns5_dev_info_get,
	.queue_pair_setup	= hns5_qp_setup,
	.queue_pair_release	= hns5_qp_release,
	.sym_session_get_size	= hns5_sym_session_get_size,
	.sym_session_configure	= hns5_sym_session_configure,
	.sym_session_clear	= hns5_sym_session_clear,
};

static uint16_t
hns5_crypto_enqueue_burst(void *qp, struct rte_crypto_op **ops, uint16_t nb_ops)
{
	return hns5_enqueue_burst((struct hns5_qp *)qp, ops, nb_ops);
}

static uint16_t
hns5_crypto_dequeue_burst(void *qp, struct rte_crypto_op **ops, uint16_t nb_ops)
{
	return hns5_dequeue_burst((struct hns5_qp *)qp, ops, nb_ops);
}

/*
 * bsp_log=debug|info|warn|err on the crypto vdev as well.
 *
 * The BSP logger (libhidrv_api / hidrv_log) is global and DPDK's --log-level
 * does not reach it. crypto/hns5 probes *before* net/hns5, and its BMU pool
 * init already prints a handful of "DEBUG 0807 ..." lines, so setting the
 * level only from the net devarg comes too late for them. Accepting the same
 * key here lets the level be applied before those prints happen.
 */
static void hns5_crypto_apply_bsp_log(const char *args)
{
	static const enum hidrv_log_level map[] = {
		HIDRV_LOG_DEBUG, HIDRV_LOG_DEBUG, HIDRV_LOG_INFO,
		HIDRV_LOG_WARN, HIDRV_LOG_ERROR
	};
	struct rte_kvargs *kvlist;
	unsigned int level = 0;
	const char *value;
	int ret;

	if (args == NULL || *args == '\0') {
		return;
	}
	kvlist = rte_kvargs_parse(args, NULL);
	if (kvlist == NULL) {
		return;
	}
	value = rte_kvargs_get(kvlist, "bsp_log");
	if (value != NULL) {
		if (strcasecmp(value, "debug") == 0) {
			level = 1;
		} else if (strcasecmp(value, "info") == 0) {
			level = 2;
		} else if (strcasecmp(value, "warn") == 0) {
			level = 3;
		} else if (strcasecmp(value, "err") == 0) {
			level = 4;
		} else if (strcasecmp(value, "default") != 0) {
			HNS5_LOG(ERR, "bsp_log=%s unsupported "
				 "(use default|debug|info|warn|err)", value);
		}
	}

	rte_kvargs_free(kvlist);

	if (level == 0) {
		return;
	}
	ret = hidrv_set_log_level(map[level]);
	if (ret != 0) {
		HNS5_LOG(WARNING, "bsp_log: failed to set BSP log level %u (%d)",
			 level, ret);
		return;
	}
	HNS5_LOG(NOTICE, "bsp_log: BSP log level set to %u", level);
}

/*
 * POE group tunables (defaults reproduce the previous behaviour):
 *   poe_preload=0|1   preload one cache line into L2 when the CPU gets a msg
 *   poe_order=0|1     group ordering
 *   poe_flow=0|1      overflow (drop) enable
 *   poe_int_th=N      group interrupt watermark
 *   poe_bp_th=N       backpressure-to-PPE threshold (hex ok: 0x3fffff)
 */
static void hns5_crypto_parse_poe(struct hns5_dev_private *priv, const char *args)
{
	struct rte_kvargs *kvlist;
	const char *v;

	if (priv == NULL || args == NULL || *args == '\0')
		return;
	kvlist = rte_kvargs_parse(args, NULL);
	if (kvlist == NULL)
		return;

	v = rte_kvargs_get(kvlist, "poe_preload");
	if (v != NULL)
		priv->sec.poe_preload_en = (unsigned int)strtoul(v, NULL, 0) ? 1U : 0U;
	v = rte_kvargs_get(kvlist, "poe_order");
	if (v != NULL)
		priv->sec.poe_order_en = (unsigned int)strtoul(v, NULL, 0) ? 1U : 0U;
	v = rte_kvargs_get(kvlist, "poe_flow");
	if (v != NULL)
		priv->sec.poe_flow_en = (unsigned int)strtoul(v, NULL, 0) ? 1U : 0U;
	else
		priv->sec.poe_flow_en = 1U;
	v = rte_kvargs_get(kvlist, "zero_copy");
	if (v != NULL && strtoul(v, NULL, 0) != 0) {
		/*
		 * POE-mode completion delivers the processed packet back into the
		 * buffer whose address is in the BD, so that buffer has to belong to
		 * the BMU pool the VF uses. Pointing the BD at an application mbuf
		 * makes hidrv_send_sec() reject the descriptor (observed as
		 * "SEC submit failed" -> runtime poisoned). Keep the switch disabled
		 * until a QM-mode path (completion carries our own descriptor) exists.
		 */
		HNS5_LOG(ERR, "zero_copy=1 is not supported in POE mode: completion "
			 "delivers into the BD buffer, which must be a BMU pool "
			 "buffer; keeping the copy path");
		priv->sec.zero_copy = 0;
	}
	/*
	 * The engine writes the result straight into the application mbuf, which
	 * removes the 1 KiB cold copy at dequeue (measured 4.27 -> 7.28 Gbps).
	 * It is the default; zero_copy_out=0 restores the copy path. Requests that
	 * cannot be processed in place (out-of-place, multi segment, misaligned)
	 * fall back automatically.
	 */
	priv->sec.zero_copy_out = 1U;
	v = rte_kvargs_get(kvlist, "zero_copy_out");
	if (v != NULL)
		priv->sec.zero_copy_out = (unsigned int)strtoul(v, NULL, 0) ? 1U : 0U;
	/*
	 * Hidden experimental switch. Registering the application pool with the
	 * BMU did NOT make the engine accept mbuf input (rc=-33) and it disturbed
	 * the BMU pool state (hang after the first op), so it is off unless asked
	 * for explicitly during BSP experiments.
	 */
	v = rte_kvargs_get(kvlist, "pool_mb");
	if (v != NULL) {
		unsigned long mb = strtoul(v, NULL, 0);

		if (mb < (HNS5_HUGE_BLOCK_BYTES >> 20) ||
		    (mb % (HNS5_QUEUE_RESERVE_BYTES >> 20)) != 0) {
			HNS5_LOG(ERR, "pool_mb=%lu invalid: must be a multiple of "
				 "%u MiB and at least %u MiB", mb,
				 (unsigned)HNS5_QUEUE_RESERVE_BYTES >> 20,
				 (unsigned)HNS5_HUGE_BLOCK_BYTES >> 20);
		} else {
			priv->sec.pool_bytes = (uint64_t)mb << 20;
		}
	}
	v = rte_kvargs_get(kvlist, "reap_batch");
	if (v != NULL)
		priv->sec.reap_batch = (unsigned int)strtoul(v, NULL, 0);
	v = rte_kvargs_get(kvlist, "cap");
	if (v != NULL)
		priv->sec.req_cap = (unsigned int)strtoul(v, NULL, 0);
	v = rte_kvargs_get(kvlist, "reaper_qp");
	if (v != NULL)
		priv->sec.reaper_qp = (unsigned int)strtoul(v, NULL, 0);
	v = rte_kvargs_get(kvlist, "reap_spin");
	if (v != NULL)
		priv->sec.reap_spin = (unsigned int)strtoul(v, NULL, 0);
	v = rte_kvargs_get(kvlist, "reap_polls");
	if (v != NULL)
		priv->sec.reap_polls = (unsigned int)strtoul(v, NULL, 0);
	v = rte_kvargs_get(kvlist, "stats");
	if (v != NULL)
		hns5_sec_stats_en = strtoul(v, NULL, 0) ? true : false;

	v = rte_kvargs_get(kvlist, "reap_fair");
	if (v != NULL)
		priv->sec.reap_fair = (unsigned int)strtoul(v, NULL, 0) ? 1U : 0U;
	v = rte_kvargs_get(kvlist, "smooth");
	if (v != NULL)
		priv->sec.smooth = (unsigned int)strtoul(v, NULL, 0) ? 1U : 0U;
	v = rte_kvargs_get(kvlist, "zc_app_pool");
	if (v != NULL)
		priv->sec.zc_app_pool = (unsigned int)strtoul(v, NULL, 0) ? 1U : 0U;
	v = rte_kvargs_get(kvlist, "zero_copy_in");
	if (v != NULL && strtoul(v, NULL, 0) != 0) {
		/*
		 * Opt-in: register the application's mbuf pool with the BMU on the
		 * first op and point the descriptor input at the mbuf. The BSP
		 * currently refuses that (rc=-33) even with pp/lp registered, so
		 * the enqueue path falls back to copying after the first refusal;
		 * the switch stays available for BSP experiments and for QM.
		 */
		priv->sec.zero_copy_in = 1;
		priv->sec.zc_app_pool = 1;
		HNS5_LOG(NOTICE, "zero_copy_in=1: registering the application mbuf "
			 "pool with the BMU on the first op (falls back to copying "
			 "if the BSP refuses it)");
	}
	v = rte_kvargs_get(kvlist, "debug_dump");
	if (v != NULL) {
		hns5_sec_dump_enabled = strtoul(v, NULL, 0) ? 1 : 0;
		if (hns5_sec_dump_enabled != 0)
			HNS5_LOG(WARNING, "debug_dump=1: 报文内容（含明文）将写入日志，"
				 "仅用于调试，请勿用于生产");
	}
	v = rte_kvargs_get(kvlist, "debug_memcpy");
	if (v != NULL)
		priv->sec.debug_memcpy = (unsigned int)strtoul(v, NULL, 0) ? 1U : 0U;
	v = rte_kvargs_get(kvlist, "sec_skip_len");
	if (v != NULL)
		priv->sec.req_skip_len = (unsigned int)strtoul(v, NULL, 0);
	v = rte_kvargs_get(kvlist, "sec_get_poe");
	if (v != NULL)
		priv->sec.req_get_poe = (unsigned int)strtoul(v, NULL, 0) ? 1U : 0U;

	v = rte_kvargs_get(kvlist, "poe_int_th");
	if (v != NULL)
		priv->sec.poe_int_th = (unsigned int)strtoul(v, NULL, 0);
	v = rte_kvargs_get(kvlist, "poe_bp_th");
	if (v != NULL)
		priv->sec.poe_bp_th = (unsigned int)strtoul(v, NULL, 0);

	if (priv->sec.req_skip_len > 15U)
		HNS5_LOG(ERR, "sec_skip_len must be 0..15, got %u",
			 priv->sec.req_skip_len);
	HNS5_LOG(NOTICE, "zero_copy=%u", priv->sec.zero_copy);
	HNS5_LOG(NOTICE, "zero_copy_out=%u zero_copy_in=%u zc_app_pool=%u",
		 priv->sec.zero_copy_out, priv->sec.zero_copy_in,
		 priv->sec.zc_app_pool);
	HNS5_LOG(NOTICE, "smooth=%u reap_fair=%u debug_memcpy=%u",
		 priv->sec.smooth, priv->sec.reap_fair,
		 priv->sec.debug_memcpy);
	HNS5_LOG(NOTICE, "sec opts: skip_len=%u get_poe=%u",
		 priv->sec.req_skip_len, priv->sec.req_get_poe);
	HNS5_LOG(NOTICE, "poe cfg: preload=%u order=%u flow=%u int_th=%u bp_th=0x%x",
		 priv->sec.poe_preload_en, priv->sec.poe_order_en,
		 priv->sec.poe_flow_en, priv->sec.poe_int_th,
		 priv->sec.poe_bp_th);
	rte_kvargs_free(kvlist);
}

/*
 * reap=que|grp|msg: which POE getter the completion path uses.
 *   que (default) - ndrv_poe_vf_get_que_msg, knows the exact queue
 *   grp           - ndrv_poe_vf_get_grp_msg, any queue of the group
 *   msg           - ndrv_poe_vf_get_msg, generic vcpu message queue
 * Provided to A/B test which getter is cheapest; see the "POE read" line
 * printed at close.
 */
static void hns5_crypto_parse_reap(struct hns5_dev_private *priv, const char *args)
{
	struct rte_kvargs *kvlist;
	const char *value;

	if (priv == NULL || args == NULL || *args == '\0')
		return;
	kvlist = rte_kvargs_parse(args, NULL);
	if (kvlist == NULL)
		return;
	value = rte_kvargs_get(kvlist, "reap");
	if (value != NULL) {
		if (!strcasecmp(value, "que"))
			priv->sec.reap_mode = 0;
		else if (!strcasecmp(value, "grp"))
			priv->sec.reap_mode = 1;
		else if (!strcasecmp(value, "msg"))
			priv->sec.reap_mode = 2;
		else
			HNS5_LOG(ERR, "reap=%s invalid (que|grp|msg)", value);
		HNS5_LOG(NOTICE, "reap mode: %s", value);
	}
	rte_kvargs_free(kvlist);
}

/*
 * qps=N: number of queue pairs (and therefore SEC channels) to advertise.
 * Default 1 keeps the previous behaviour; N > 1 needs one vcpu, one submit
 * group and one completion group per queue pair.
 */
static unsigned int hns5_crypto_qps_from_args(const char *args)
{
	struct rte_kvargs *kvlist;
	const char *value;
	char *end = NULL;
	unsigned long v;
	unsigned int qps = 1;

	if (args == NULL || *args == '\0')
		return qps;
	kvlist = rte_kvargs_parse(args, NULL);
	if (kvlist == NULL)
		return qps;
	value = rte_kvargs_get(kvlist, "qps");
	if (value != NULL) {
		v = strtoul(value, &end, 0);
		if (end == NULL || *end != '\0' || v == 0 ||
		    v > HNS5_SEC_QPS_CEILING) {
			HNS5_LOG(ERR, "qps=%s invalid (1..%u)", value,
				 HNS5_SEC_QPS_CEILING);
		} else {
			qps = (unsigned int)v;
		}
	}
	rte_kvargs_free(kvlist);
	HNS5_LOG(NOTICE, "qps: requested %u queue pair(s)", qps);
	return qps;
}

/*
 * vcpu=N: pick a different SEC engine vcpu (default 1). Measured on the same
 * single queue pair, it tells whether the engine exposes more than one
 * parallel channel without requiring the multi-QP work first.
 */
static void hns5_crypto_parse_vcpu(struct hns5_dev_private *priv, const char *args)
{
	struct rte_kvargs *kvlist;
	const char *value;
	char *end = NULL;
	unsigned long v;

	if (priv == NULL || args == NULL || *args == '\0')
		return;
	kvlist = rte_kvargs_parse(args, NULL);
	if (kvlist == NULL)
		return;
	value = rte_kvargs_get(kvlist, "vcpu");
	if (value != NULL) {
		v = strtoul(value, &end, 0);
		if (end == NULL || *end != '\0' || v > 63UL) {
			HNS5_LOG(ERR, "vcpu=%s invalid", value);
		} else {
			priv->sec.requested_vcpu = (unsigned int)v;
			HNS5_LOG(NOTICE, "vcpu: requested SEC engine vcpu %lu", v);
		}
	}
	rte_kvargs_free(kvlist);
}

#ifndef HNS5_BUILD_ID
#define HNS5_BUILD_ID unknown
#endif
#define HNS5_STR2_(x) #x
#define HNS5_STR_(x) HNS5_STR2_(x)

int
hns5_crypto_probe(struct rte_vdev_device *vdev)
{
	HNS5_LOG(NOTICE, "build=%s", HNS5_STR_(HNS5_BUILD_ID));
	struct rte_cryptodev_pmd_init_params init_params = {
		.name = "",
		.private_data_size = sizeof(struct hns5_dev_private),
		.max_nb_queue_pairs = 1,
	};
	unsigned int req_qps;
	struct hns5_dev_private *priv;
	struct rte_cryptodev *dev;
	const char *name;
	const char *pool = NULL;
	int rc;

	name = rte_vdev_device_name(vdev);
	if (name == NULL)
		return -EINVAL;

	hns5_crypto_apply_bsp_log(rte_vdev_device_args(vdev));
	/* Parse qps before creating the device so CRYPTODEV logs the real value
	 * instead of the default 1 (which used to look like "qps was ignored"). */
	req_qps = hns5_crypto_qps_from_args(rte_vdev_device_args(vdev));
	{
		/*
		 * The real limit is whatever the hardware and the pool allow, not a
		 * compile-time constant: one queue pair needs its own SEC vcpu (the
		 * submit path is per vcpu) and a slice of the 448 pool buffers.
		 */
		unsigned int vcpu_total = ndrv_poe_get_lcpu_num();
		unsigned int by_vcpu = vcpu_total > 1U ? vcpu_total - 1U : 1U;
		unsigned int buffers = (HNS5_POOL_BYTES_DEFAULT -
					HNS5_QUEUE_RESERVE_BYTES) / HNS5_BUFFER_BYTES;
		unsigned int by_mem = buffers / HNS5_SEC_MIN_PER_QP;
		unsigned int cap = by_vcpu < by_mem ? by_vcpu : by_mem;

		if (cap == 0U)
			cap = 1U;
		if (cap > HNS5_SEC_QPS_CEILING)
			cap = HNS5_SEC_QPS_CEILING;
		if (req_qps > cap) {
			HNS5_LOG(ERR, "qps=%u exceeds what this system supports "
				 "(vcpus=%u, buffers=%u -> cap=%u); using %u",
				 req_qps, vcpu_total, buffers, cap, cap);
			req_qps = cap;
		}
	}
	if (req_qps > 1U)
		HNS5_LOG(NOTICE, "qps=%u: all queue pairs share one POE completion "
			 "group and are served by a shared reaper (submit groups are "
			 "per queue pair)", req_qps);
	init_params.max_nb_queue_pairs = (uint16_t)req_qps;

	dev = rte_cryptodev_pmd_create(name, &vdev->device, &init_params);
	if (dev == NULL) {
		HNS5_LOG(ERR, "cryptodev create failed for %s", name);
		return -ENODEV;
	}

	dev->dev_ops = &hns5_crypto_pmd_ops;
	dev->driver_id = hns5_cryptodev_driver_id;
	dev->enqueue_burst = hns5_crypto_enqueue_burst;
	dev->dequeue_burst = hns5_crypto_dequeue_burst;
	dev->feature_flags = RTE_CRYPTODEV_FF_HW_ACCELERATED |
			     RTE_CRYPTODEV_FF_SYMMETRIC_CRYPTO |
			     RTE_CRYPTODEV_FF_SYM_OPERATION_CHAINING |
			     RTE_CRYPTODEV_FF_IN_PLACE_SGL |
			     RTE_CRYPTODEV_FF_OOP_SGL_IN_LB_OUT;

	priv = dev->data->dev_private;
	memset(priv, 0, sizeof(*priv));
	priv->sec.sec_pf = -1;
	priv->sec.sec_vf = -1;
	priv->sec.poe_pf = -1;
	priv->sec.poe_vf = -1;
	hns5_crypto_parse_vcpu(priv, rte_vdev_device_args(vdev));
	hns5_crypto_parse_reap(priv, rte_vdev_device_args(vdev));
	hns5_crypto_parse_poe(priv, rte_vdev_device_args(vdev));
	/* Must be set before the channel start below: it decides how many SEC
	 * channels are brought up (and therefore how many QPs can be set up). */
	priv->max_qps = (uint16_t)req_qps;
	/*
	 * Per-QP buffer cache. Measured: cache=32 with the derived in-flight cap
	 * gives the best multi-QP throughput (20.9 Gbps on 4 QP); shrinking the
	 * cache below that caused a 2x regression, so keep it fixed.
	 */
	/* The 16 KiB layout gives 1792 buffers, so a full cache per queue pair is
	 * affordable (8 QP x 64 = 512 buffers). A larger cache means fewer BMU
	 * allocations, which is what made enqueue bursts come up short. */
	priv->buf_cache = (req_qps > 1U) ? 64U :
			  (uint32_t)HNS5_SEC_BUF_CACHE;

	/* Extra channel descriptors must exist before the channels are started:
	 * hns5_sec_channels_start() fills them in. */
	if (priv->max_qps > 1U) {
		priv->extra = rte_zmalloc("hns5 extra chans",
					  sizeof(*priv->extra) *
					  (priv->max_qps - 1U), 0);
		if (priv->extra == NULL) {
			HNS5_LOG(ERR, "cannot allocate %u extra channels",
				 priv->max_qps - 1U);
			rc = -ENOMEM;
			goto destroy;
		}
	}

	rc = hns5_runtime_acquire(&priv->sec.common, pool,
				  priv->sec.pool_bytes);
	if (rc) {
		HNS5_LOG(ERR, "HugeTLB/BMU runtime acquire failed: %d", rc);
		goto destroy;
	}
	priv->common_runtime_acquired = true;

	/* Shared completion routing: allocate the slot ownership table. */
	rte_spinlock_init(&priv->reap_lock);
	priv->slot_base = priv->sec.common->pool.pa + HNS5_QUEUE_RESERVE_BYTES;
	priv->slot_count = (uint32_t)((priv->sec.common->pool.bytes -
				       HNS5_QUEUE_RESERVE_BYTES) /
				      HNS5_BUFFER_BYTES);
	priv->slot_map = rte_zmalloc("hns5 slot map",
				     priv->slot_count * sizeof(uint32_t), 0);
	if (priv->slot_map == NULL) {
		HNS5_LOG(ERR, "cannot allocate the completion ownership table");
		rc = -ENOMEM;
		goto release_runtime;
	}
	priv->sec.priv = priv;
	/* POE ordering stays off: measured 7360 ns/op starvation with order=1.
	 * Completions are matched by buffer address instead (see slot_map). */
	/*
	 * Cap the in-flight requests per queue pair: two queue pairs each trying
	 * to keep a full descriptor ring in flight would exceed the 448 pool
	 * buffers and cause partial enqueues (cperf "Failed Enq").
	 */
	{
		uint32_t per_qp = priv->slot_count;
		uint32_t cache = priv->buf_cache;

		if (req_qps > 1U) {
			per_qp = (priv->slot_count - req_qps * cache) / req_qps;
			/* Never let the pipeline eat the whole pool: cap at a value
			 * that is still far deeper than any application keeps in
			 * flight, and leave the rest as allocation headroom. */
			if (per_qp > HNS5_SEC_MAX_INFLIGHT_PER_QP)
				per_qp = HNS5_SEC_MAX_INFLIGHT_PER_QP;
		} else if (per_qp > cache + 8U) {
			per_qp -= cache;
			if (per_qp > 16U)
				per_qp -= 8U;
		}
		if (priv->sec.req_cap != 0U)
			per_qp = priv->sec.req_cap;
		priv->inflight_cap = (uint16_t)per_qp;
		HNS5_LOG(NOTICE, "buffer budget: cache=%u, in-flight cap=%u "
			 "per queue pair (%u qps)", priv->buf_cache,
			 priv->inflight_cap, req_qps);
	}
	/*
	 * Defaults match the build that measured 21.2 Gbps on 4 QP / 1024 B:
	 * drain up to 64 completions per lock hold and allow 64 empty polls.
	 * reap_batch= / reap_polls= override them (8 QP prefers a smaller poll
	 * budget because every empty poll steals submission time).
	 */
	if (priv->sec.reaper_qp != HNS5_REAPER_NONE &&
	    priv->sec.reaper_qp >= req_qps) {
		HNS5_LOG(ERR, "reaper_qp=%u invalid (qps=%u); using lazy reaping",
			 priv->sec.reaper_qp, req_qps);
		priv->sec.reaper_qp = HNS5_REAPER_NONE;
	}
	if (priv->sec.reap_polls == 0U)
		priv->sec.reap_polls = HNS5_REAP_DEFAULT_POLLS;
	if (priv->sec.reap_batch == 0U)
		priv->sec.reap_batch = HNS5_REAP_DEFAULT_BATCH;
	if (priv->sec.reap_spin == 0U)
		priv->sec.reap_spin = HNS5_REAP_DEFAULT_SPIN;
	HNS5_LOG(NOTICE, "reaping: batch=%u polls=%u spin=%u (qps=%u)",
		 priv->sec.reap_batch, priv->sec.reap_polls,
		 priv->sec.reap_spin, req_qps);
	HNS5_LOG(NOTICE, "effective config: qps=%u reaper=%s cache=%u cap=%u "
		 "zero_copy_out=%u smooth=%u reap_fair=%u stats=%u pool_mb=%lu buffer=%uB",
		 req_qps,
		 (priv->sec.reaper_qp == HNS5_REAPER_NONE) ? "lazy" : "dedicated",
		 priv->buf_cache, priv->inflight_cap,
		 priv->sec.zero_copy_out, priv->sec.smooth, priv->sec.reap_fair,
		 hns5_sec_stats_en,
		 (unsigned long)((priv->sec.pool_bytes ?
				  priv->sec.pool_bytes : HNS5_POOL_BYTES_DEFAULT) >> 20),
		 (unsigned)HNS5_BUFFER_BYTES);

	HNS5_LOG(NOTICE, "pool size=%lu MiB, completion slots: count=%u",
		 (unsigned long)(priv->sec.common->pool.bytes >> 20),
		 priv->slot_count);

	rc = hns5_sec_platform_inspect(&priv->sec);
	if (rc) {
		HNS5_LOG(ERR, "SEC platform inspect failed: %d", rc);
		goto release_runtime;
	}

	rc = hns5_sec_platform_start(&priv->sec);
	if (rc == 0) {
		priv->nb_chans = 1;
		if (priv->max_qps > 1) {
			rc = hns5_sec_channels_start(&priv->sec, priv->extra,
						     priv->max_qps);
			if (rc == 0)
				priv->nb_chans = priv->max_qps;
			else
				priv->max_qps = 1;	/* keep the primary usable */
		}
	}
	if (rc) {
		HNS5_LOG(ERR, "SEC platform start failed: %d", rc);
		goto release_runtime;
	}

	priv->qps = rte_calloc("hns5 qps", priv->max_qps, sizeof(struct hns5_qp *), 0);
	if (priv->qps == NULL) {
		rc = -ENOMEM;
		goto close_platform;
	}

	rte_cryptodev_pmd_probing_finish(dev);
	return 0;

close_platform:
	hns5_sec_platform_close(&priv->sec);
release_runtime:
	hns5_runtime_release(priv->sec.common);
destroy:
	rte_cryptodev_pmd_destroy(dev);
	return rc;
}

int
hns5_crypto_remove(struct rte_vdev_device *vdev)
{
	struct rte_cryptodev *dev;
	const char *name;

	if (vdev == NULL)
		return -EINVAL;
	name = rte_vdev_device_name(vdev);
	dev = rte_cryptodev_pmd_get_named_dev(name);
	if (dev == NULL)
		return -ENODEV;

	/*
	 * Tear the hardware down explicitly. rte_cryptodev_pmd_destroy() ->
	 * rte_cryptodev_pmd_release_device() only reaches our .dev_close when the
	 * device is no longer started, but applications (examples/l2fwd-crypto,
	 * test-crypto-perf) commonly exit while it is still started, so the SEC
	 * engine and the BMU runtime would survive the process. The next start
	 * then reconfigures hardware that is still owned by the previous run,
	 * which is exactly why the first start after boot worked and the next one
	 * failed. Do it here, where the private data is still valid.
	 */
	(void)hns5_crypto_teardown(dev);

	rte_cryptodev_pmd_destroy(dev);
	return 0;
}

static struct rte_vdev_driver hns5_crypto_vdev_drv = {
	.probe = hns5_crypto_probe,
	.remove = hns5_crypto_remove,
};

static struct cryptodev_driver hns5_crypto_drv;

RTE_PMD_REGISTER_VDEV(HNS5_CRYPTO_DRIVER_NAME, hns5_crypto_vdev_drv);
RTE_PMD_REGISTER_CRYPTO_DRIVER(hns5_crypto_drv, hns5_crypto_vdev_drv.driver,
			       hns5_cryptodev_driver_id);
RTE_PMD_REGISTER_PARAM_STRING(HNS5_CRYPTO_DRIVER_NAME, "");
