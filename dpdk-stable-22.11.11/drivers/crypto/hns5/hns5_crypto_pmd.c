/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2026 Huawei Technologies Co., Ltd
 *
 * DPDK vdev front-end. Probe acquires the shared HugeTLB/BMU pool and
 * brings up the SEC PF/VF + POE submit/completion groups; each queue pair
 * keeps its own in-flight ring.
 */

#include "hns5_crypto_pmd_private.h"
#include <string.h>

/* 逐 op 计时开关，由 devarg stats= 设置（默认 0：关闭计时，只保留计数）*/
bool hns5_sec_stats_en;

#include <rte_string_fns.h>
#include <rte_malloc.h>
#include <rte_kvargs.h>
#include <strings.h>
#include <stdlib.h>

#include "hidrv_log_api.h"

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
 * Tear the device down: SEC engine + shared BMU. Idempotent, because
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
	if (priv->common_bmu_acquired) {
		priv->common_bmu_acquired = false;
		hns5_bmu_release(priv->sec.common);
		priv->sec.common = NULL;
	}
	return 0;
}

/*
 * Release what probe allocated beside the framework's private area.
 * rte_cryptodev_pmd_destroy() frees dev->data (and with it the private area),
 * but nothing here: without this, every probe/remove cycle leaks the queue
 * pair array, the completion ownership table and the extra channel state.
 *
 * Deliberately NOT called from hns5_crypto_teardown(): that also runs on
 * dev_close, after which an application may still start the device again, and
 * both the queue pair setup path and the reaper need these tables.
 */
static void
hns5_crypto_free_priv(struct rte_cryptodev *dev)
{
	struct hns5_dev_private *priv = dev->data->dev_private;
	uint16_t i;

	if (priv == NULL)
		return;

	/* rte_free() accepts NULL, so each table is handled on its own: probe can
	 * fail after slot_map was allocated but before the queue pair array.
	 */
	if (priv->qps != NULL) {
		for (i = 0; i < priv->max_qps; i++) {
			struct hns5_qp *qp = priv->qps[i];

			if (qp == NULL)
				continue;
			/* Only reached when teardown succeeded, so no queue pair
			 * is still in use; the cached segments went back to the BMU
			 * while it was still ours.
			 */
			rte_free(qp->bl);
			rte_free(qp->ring);
			rte_free(qp);
			priv->qps[i] = NULL;
			dev->data->queue_pairs[i] = NULL;
		}
	}
	rte_free(priv->qps);
	priv->qps = NULL;
	rte_free(priv->slot_map);
	priv->slot_map = NULL;
	rte_free(priv->extra);
	priv->extra = NULL;
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
		if (want > 0xffffU)	/* depth is 16 bits */
			want = 0xffffU;
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
	/*
	 * bl and ring are members of qp, so they must be released while qp is
	 * still alive: reading qp->bl after rte_free(qp) is a use-after-free, and
	 * with smooth=1 it also frees a pointer read out of freed memory.
	 */
	rte_free(qp->bl);
	qp->bl = NULL;
	rte_free(qp->ring);
	rte_free(qp);
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
 * Device arguments. Each key is defined once so the parser below, the
 * unknown-key check and RTE_PMD_REGISTER_PARAM_STRING cannot drift apart.
 */
#define HNS5_DEVARG_BSP_LOG	"bsp_log"
#define HNS5_DEVARG_CAP	"cap"
#define HNS5_DEVARG_DEBUG_MEMCPY	"debug_memcpy"
#define HNS5_DEVARG_POE_BP_TH	"poe_bp_th"
#define HNS5_DEVARG_POE_FLOW	"poe_flow"
#define HNS5_DEVARG_POE_INT_TH	"poe_int_th"
#define HNS5_DEVARG_POE_ORDER	"poe_order"
#define HNS5_DEVARG_POE_PRELOAD	"poe_preload"
#define HNS5_DEVARG_POOL_MB	"pool_mb"
#define HNS5_DEVARG_QPS	"qps"
#define HNS5_DEVARG_REAP	"reap"
#define HNS5_DEVARG_REAP_BATCH	"reap_batch"
#define HNS5_DEVARG_REAP_FAIR	"reap_fair"
#define HNS5_DEVARG_REAP_POLLS	"reap_polls"
#define HNS5_DEVARG_REAP_SPIN	"reap_spin"
#define HNS5_DEVARG_REAPER_QP	"reaper_qp"
#define HNS5_DEVARG_SEC_GET_POE	"sec_get_poe"
#define HNS5_DEVARG_SEC_SKIP_LEN	"sec_skip_len"
#define HNS5_DEVARG_SMOOTH	"smooth"
#define HNS5_DEVARG_STATS	"stats"
#define HNS5_DEVARG_VCPU	"vcpu"
#define HNS5_DEVARG_ZC_APP_POOL	"zc_app_pool"
#define HNS5_DEVARG_ZERO_COPY	"zero_copy"
#define HNS5_DEVARG_ZERO_COPY_IN	"zero_copy_in"
#define HNS5_DEVARG_ZERO_COPY_OUT	"zero_copy_out"

static const char * const hns5_devargs[] = {
	HNS5_DEVARG_BSP_LOG,
	HNS5_DEVARG_CAP,
	HNS5_DEVARG_DEBUG_MEMCPY,
	HNS5_DEVARG_POE_BP_TH,
	HNS5_DEVARG_POE_FLOW,
	HNS5_DEVARG_POE_INT_TH,
	HNS5_DEVARG_POE_ORDER,
	HNS5_DEVARG_POE_PRELOAD,
	HNS5_DEVARG_POOL_MB,
	HNS5_DEVARG_QPS,
	HNS5_DEVARG_REAP,
	HNS5_DEVARG_REAP_BATCH,
	HNS5_DEVARG_REAP_FAIR,
	HNS5_DEVARG_REAP_POLLS,
	HNS5_DEVARG_REAP_SPIN,
	HNS5_DEVARG_REAPER_QP,
	HNS5_DEVARG_SEC_GET_POE,
	HNS5_DEVARG_SEC_SKIP_LEN,
	HNS5_DEVARG_SMOOTH,
	HNS5_DEVARG_STATS,
	HNS5_DEVARG_VCPU,
	HNS5_DEVARG_ZC_APP_POOL,
	HNS5_DEVARG_ZERO_COPY,
	HNS5_DEVARG_ZERO_COPY_IN,
	HNS5_DEVARG_ZERO_COPY_OUT,
	NULL
};

/*
 * Whoever writes the command line or the JSON device config controls a devarg
 * value, and a value carrying \n or \r would forge extra log lines. Nothing
 * external is printed until it has passed through here: non-printable bytes
 * become '?', which also makes any numeric parse fail instead of reading a
 * truncated number.
 */
static void
hns5_arg_shown(char *dst, size_t dst_len, const char *src)
{
	hns5_str_shown(dst, dst_len, src);
}

static int
hns5_arg_known(const char *key, const char *value __rte_unused,
	       void *arg __rte_unused)
{
	char shown[64];
	unsigned int i;

	for (i = 0; hns5_devargs[i] != NULL; i++) {
		if (strcmp(key, hns5_devargs[i]) == 0)
			return 0;
	}
	hns5_arg_shown(shown, sizeof(shown), key);
	/*
	 * Warn, do not fail: a mistyped key used to be ignored silently, and the
	 * operator then measured default behaviour. Failing the probe here would
	 * turn that invisible mistake into a boot failure for existing scripts.
	 */
	HNS5_LOG(WARNING, "ignoring unknown devarg \"%s\"; see "
		 "doc/guides/cryptodevs/hns5.rst for the supported list", shown);
	return 0;
}

static void
hns5_crypto_check_devargs(const char *args)
{
	struct rte_kvargs *kvlist;

	if (args == NULL || *args == '\0')
		return;
	kvlist = rte_kvargs_parse(args, NULL);
	if (kvlist == NULL)
		return;
	(void)rte_kvargs_process(kvlist, NULL, hns5_arg_known, NULL);
	rte_kvargs_free(kvlist);
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
	value = rte_kvargs_get(kvlist, HNS5_DEVARG_BSP_LOG);
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
			char shown[64];

			hns5_arg_shown(shown, sizeof(shown), value);
			HNS5_LOG(ERR, "bsp_log=%s unsupported "
				 "(use default|debug|info|warn|err)", shown);
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
 * Every numeric devarg goes through here. A bare strtoul() accepted "12abc",
 * read "yes" as 0 and applied no range at all, so a typo silently meant "the
 * default" or, worse, a different number. The value must be a plain integer
 * inside [min, max]; anything else is reported and the existing default
 * stays. Returns 1 when the value was applied, 0 when the key was not given,
 * -1 when the given value was rejected.
 */
static int
hns5_arg_num(const struct rte_kvargs *kvlist, const char *key,
	     unsigned int min, unsigned int max, unsigned int *out)
{
	const char *v = rte_kvargs_get(kvlist, key);
	char shown[64];
	char *end;
	unsigned long n;

	if (v == NULL)
		return 0;
	if (v[0] == '\0') {
		/* An empty value is not "0": strtoul() would hand back 0 with
		 * end == start and the range check below would then accept it.
		 */
		HNS5_LOG(ERR, "%s= has no value, keeping the default", key);
		return -1;
	}
	hns5_arg_shown(shown, sizeof(shown), v);
	errno = 0;
	n = strtoul(v, &end, 0);
	if (errno != 0 || end == NULL || *end != '\0' ||
	    n < (unsigned long)min || n > (unsigned long)max) {
		HNS5_LOG(ERR, "%s=%s invalid (need %u..%u), keeping the default",
			 key, shown, min, max);
		return -1;
	}
	*out = (unsigned int)n;
	return 1;
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
	unsigned int val;
	int r;

	/*
	 * stats= is the only devarg that lands in a process-wide flag (the per-op
	 * path reads it without going through a private pointer), so its
	 * documented "off" default has to be set explicitly here rather than
	 * left to the BSS: a second vdev, or the same one re-probed after a
	 * hot-unplug, would otherwise inherit the previous device's value and
	 * silently pay six counter reads per operation.
	 */
	hns5_sec_stats_en = 0;
	if (priv == NULL)
		return;
	/*
	 * The non-zero defaults (POE flow and zero-copy output) are applied by
	 * the caller: they must hold for a vdev created without any argument too,
	 * and this function used to return right here for that case, which left
	 * a bare "--vdev crypto_hns5" on the 1 KiB copy path. The report below
	 * still runs, so the effective values are visible either way.
	 */
	if (args == NULL || *args == '\0')
		goto report;
	kvlist = rte_kvargs_parse(args, NULL);
	if (kvlist == NULL)
		goto report;

	if (hns5_arg_num(kvlist, HNS5_DEVARG_POE_PRELOAD, 0u, 1u, &val) == 1)
		priv->sec.poe_preload_en = (val != 0) ? 1U : 0U;
	if (hns5_arg_num(kvlist, HNS5_DEVARG_POE_ORDER, 0u, 1u, &val) == 1)
		priv->sec.poe_order_en = (val != 0) ? 1U : 0U;
	r = hns5_arg_num(kvlist, HNS5_DEVARG_POE_FLOW, 0u, 1u, &val);
	if (r == 1)
		priv->sec.poe_flow_en = (val != 0) ? 1U : 0U;
	if (hns5_arg_num(kvlist, HNS5_DEVARG_ZERO_COPY, 0u, 1u, &val) == 1 &&
	    val != 0) {
		/*
		 * POE-mode completion delivers the processed packet back into the
		 * buffer whose address is in the BD, so that buffer has to belong to
		 * the BMU pool the VF uses. Pointing the BD at an application mbuf
		 * makes hidrv_send_sec() reject the descriptor (observed as
		 * "SEC submit failed" -> BMU poisoned). Keep the switch disabled
		 * until a QM-mode path (completion carries our own descriptor) exists.
		 */
		HNS5_LOG(ERR, "zero_copy=1 is not supported in POE mode: completion "
			 "delivers into the BD buffer, which must be a BMU pool "
			 "buffer; keeping the copy path");
		priv->sec.zero_copy = 0;
	}
	/*
	 * The engine writes the result straight into the application mbuf, which
	 * removes the 1 KiB cold copy at dequeue. The "on" default is applied by
	 * the caller, so zero_copy_out=0 is the only way to restore the copy
	 * path. Requests that cannot be processed in place (out-of-place,
	 * multi segment, misaligned) fall back automatically.
	 */
	if (hns5_arg_num(kvlist, HNS5_DEVARG_ZERO_COPY_OUT, 0u, 1u, &val) == 1)
		priv->sec.zero_copy_out = (val != 0) ? 1U : 0U;
	/*
	 * Hidden experimental switch. Registering the application pool with the
	 * BMU did NOT make the engine accept mbuf input (rc=-33) and it disturbed
	 * the BMU pool state (hang after the first op), so it is off unless asked
	 * for explicitly during BSP experiments.
	 */
	if (hns5_arg_num(kvlist, HNS5_DEVARG_POOL_MB,
			 (unsigned int)(HNS5_HUGE_BLOCK_BYTES >> 20),
			 HNS5_POOL_MB_MAX, &val) == 1) {
		if ((val % (HNS5_QUEUE_RESERVE_BYTES >> 20)) != 0)
			HNS5_LOG(ERR, "pool_mb=%u must be a multiple of %u MiB, "
				 "keeping the default", val,
				 (unsigned)(HNS5_QUEUE_RESERVE_BYTES >> 20));
		else
			priv->sec.pool_bytes = (uint64_t)val << 20;
	}
	if (hns5_arg_num(kvlist, HNS5_DEVARG_REAP_BATCH, 1u, 4096u, &val) == 1)
		priv->sec.reap_batch = val;
	if (hns5_arg_num(kvlist, HNS5_DEVARG_CAP, 1u, HNS5_SEC_CAP_LIMIT, &val) == 1)
		priv->sec.req_cap = val;
	if (hns5_arg_num(kvlist, HNS5_DEVARG_REAPER_QP, 0u, (HNS5_SEC_QPS_CEILING - 1u), &val) == 1)
		priv->sec.reaper_qp = val;
	if (hns5_arg_num(kvlist, HNS5_DEVARG_REAP_SPIN, 1u, 4096u, &val) == 1)
		priv->sec.reap_spin = val;
	if (hns5_arg_num(kvlist, HNS5_DEVARG_REAP_POLLS, 1u, 4096u, &val) == 1)
		priv->sec.reap_polls = val;
	if (hns5_arg_num(kvlist, HNS5_DEVARG_STATS, 0u, 1u, &val) == 1)
		hns5_sec_stats_en = (val != 0);

	if (hns5_arg_num(kvlist, HNS5_DEVARG_REAP_FAIR, 0u, 1u, &val) == 1)
		priv->sec.reap_fair = (val != 0) ? 1U : 0U;
	if (hns5_arg_num(kvlist, HNS5_DEVARG_SMOOTH, 0u, 1u, &val) == 1)
		priv->sec.smooth = (val != 0) ? 1U : 0U;
	if (hns5_arg_num(kvlist, HNS5_DEVARG_ZC_APP_POOL, 0u, 1u, &val) == 1)
		priv->sec.zc_app_pool = (val != 0) ? 1U : 0U;
	if (hns5_arg_num(kvlist, HNS5_DEVARG_ZERO_COPY_IN, 0u, 1u, &val) == 1 &&
	    val != 0) {
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
	if (hns5_arg_num(kvlist, HNS5_DEVARG_DEBUG_MEMCPY, 0u, 1u, &val) == 1)
		priv->sec.debug_memcpy = (val != 0) ? 1U : 0U;
	if (hns5_arg_num(kvlist, HNS5_DEVARG_SEC_SKIP_LEN, 0u, HNS5_SEC_SKIP_LEN_MAX, &val) == 1)
		priv->sec.req_skip_len = val;
	if (hns5_arg_num(kvlist, HNS5_DEVARG_SEC_GET_POE, 0u, 1u, &val) == 1)
		priv->sec.req_get_poe = (val != 0) ? 1U : 0U;

	if (hns5_arg_num(kvlist, HNS5_DEVARG_POE_INT_TH, 0u, 0xffffffffu, &val) == 1)
		priv->sec.poe_int_th = val;
	if (hns5_arg_num(kvlist, HNS5_DEVARG_POE_BP_TH, 0u, 0xffffffffu, &val) == 1)
		priv->sec.poe_bp_th = val;

	rte_kvargs_free(kvlist);
report:
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
	value = rte_kvargs_get(kvlist, HNS5_DEVARG_REAP);
	if (value != NULL) {
		if (!strcasecmp(value, "que"))
			priv->sec.reap_mode = 0;
		else if (!strcasecmp(value, "grp"))
			priv->sec.reap_mode = 1;
		else if (!strcasecmp(value, "msg"))
			priv->sec.reap_mode = 2;
		else {
			char shown[64];

			hns5_arg_shown(shown, sizeof(shown), value);
			HNS5_LOG(ERR, "reap=%s invalid (que|grp|msg)", shown);
		}
		HNS5_LOG(NOTICE, "reap mode: %s",
			 (strcasecmp(value, "que") && strcasecmp(value, "grp") &&
			  strcasecmp(value, "msg")) ? "<rejected>" : value);
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
	value = rte_kvargs_get(kvlist, HNS5_DEVARG_QPS);
	if (value != NULL) {
		v = strtoul(value, &end, 0);
		if (end == NULL || *end != '\0' || v == 0 ||
		    v > HNS5_SEC_QPS_CEILING) {
			char shown[64];

			hns5_arg_shown(shown, sizeof(shown), value);
			HNS5_LOG(ERR, "qps=%s invalid (1..%u)", shown,
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
	value = rte_kvargs_get(kvlist, HNS5_DEVARG_VCPU);
	if (value != NULL) {
		v = strtoul(value, &end, 0);
		if (end == NULL || *end != '\0' || v > 63UL) {
			char shown[64];

			hns5_arg_shown(shown, sizeof(shown), value);
			HNS5_LOG(ERR, "vcpu=%s invalid", shown);
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

	/* Report unknown keys before anything depends on them. */
	hns5_crypto_check_devargs(rte_vdev_device_args(vdev));
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
	/*
	 * rte_cryptodev_pmd_create() zero-allocates the private area
	 * (rte_zmalloc_socket in lib/cryptodev/cryptodev_pmd.c), so only the
	 * fields whose default is not zero are set here.
	 */
	priv->sec.sec_pf = -1;
	priv->sec.sec_vf = -1;
	priv->sec.poe_pf = -1;
	priv->sec.poe_vf = -1;
	/*
	 * Devarg defaults whose value is not zero. They are applied here and not
	 * in hns5_crypto_parse_poe(): the parse helper only walks its override
	 * list when the vdev carries arguments, so a bare "--vdev crypto_hns5"
	 * used to run with zero-copy output off (a 1 KiB copy per operation on
	 * the dequeue path) and POE flow off, contradicting the documented
	 * defaults and making an argument-carrying run measurably faster.
	 */
	priv->sec.poe_flow_en = 1U;
	priv->sec.zero_copy_out = 1U;
	hns5_crypto_parse_vcpu(priv, rte_vdev_device_args(vdev));
	hns5_crypto_parse_reap(priv, rte_vdev_device_args(vdev));
	hns5_crypto_parse_poe(priv, rte_vdev_device_args(vdev));
	/* Must be set before the channel start below: it decides how many SEC
	 * channels are brought up (and therefore how many QPs can be set up). */
	priv->max_qps = (uint16_t)req_qps;
	/*
	 * Per-queue-pair cache of SEC segments. The 16 KiB layout gives 1792
	 * buffers, so one cache of HNS5_SEC_BUF_CACHE per pair is affordable
	 * even at 8 pairs (512 of 1792). It exists because the BSP only offers
	 * single-buffer alloc/free: without it every operation took the global
	 * mutex and made two BSP calls, which is what made enqueue bursts come
	 * up short. The value is the same for one pair and for eight: a smaller
	 * cache costs throughput, so there is no per-mode variant.
	 */
	priv->buf_cache = (uint32_t)HNS5_SEC_BUF_CACHE;

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

	rc = hns5_bmu_acquire(&priv->sec.common, pool,
				  priv->sec.pool_bytes);
	if (rc) {
		HNS5_LOG(ERR, "HugeTLB/BMU acquire failed: %d", rc);
		/* Nothing was acquired, but priv->extra already exists. */
		goto free_priv;
	}
	priv->common_bmu_acquired = true;

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
		goto release_bmu;
	}
	priv->sec.priv = priv;
	/* POE ordering stays off: with order=1 the queue starved and per-op
	 * latency became unacceptable. Completions are matched by buffer address
	 * instead (see slot_map). */
	/*
	 * Cap the in-flight requests per queue pair: two queue pairs each trying
	 * to keep a full descriptor ring in flight would exceed the 448 pool
	 * buffers and cause partial enqueues (cperf "Failed Enq").
	 */
	{
		uint32_t per_qp = priv->slot_count;
		uint32_t cache = priv->buf_cache;

		if (req_qps > 1U) {
			uint32_t reserved = req_qps * cache;

			/* Check before subtracting: with a small pool and many
			 * queue pairs the caches alone can exceed the buffer
			 * count, and an unsigned wrap would ask for a ring of
			 * the wrong size.
			 */
			if (reserved >= priv->slot_count) {
				HNS5_LOG(ERR, "%u queue pairs x %u cached segments "
					 "leave no buffers in a pool of %u - use a "
					 "bigger pool_mb or fewer qps",
					 req_qps, cache, priv->slot_count);
				rc = -EINVAL;
				goto release_bmu;
			}
			per_qp = (priv->slot_count - reserved) / req_qps;
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
		/* Also clamp the derived value: a large pool_mb makes the
		 * buffer count exceed what the 16-bit ring can hold.
		 */
		if (per_qp > HNS5_SEC_CAP_LIMIT)
			per_qp = HNS5_SEC_CAP_LIMIT;
		priv->inflight_cap = (uint16_t)per_qp;
		HNS5_LOG(NOTICE, "buffer budget: cache=%u, in-flight cap=%u "
			 "per queue pair (%u qps)", priv->buf_cache,
			 priv->inflight_cap, req_qps);
	}
	/*
	 * Defaults picked by the multi-QP tuning runs: drain up to 64 completions
	 * per lock hold and allow 64 empty polls. reap_batch= / reap_polls=
	 * override them (8 QP prefers a smaller poll budget because every empty
	 * poll steals submission time).
	 */
	/*
	 * State the reaping default instead of inheriting it from a zeroed
	 * field: with several queue pairs, QP0 draining the shared completion
	 * group for everyone performs better in aggregate than each pair reaping
	 * for itself. With a single pair there is no "everyone" to drain for, so
	 * ask for no dedicated reaper - same behaviour, fewer moving parts.
	 * reaper_qp= overrides either case.
	 */
	if (req_qps == 1U && priv->sec.reaper_qp == 0U)
		priv->sec.reaper_qp = HNS5_REAPER_NONE;
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
		goto release_bmu;
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
		goto release_bmu;
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
release_bmu:
	hns5_bmu_release(priv->sec.common);
	/*
	 * rte_cryptodev_pmd_destroy() can reach hns5_crypto_teardown() again
	 * while unwinding, and teardown would then hand the released runtime
	 * pointer back into hns5_bmu_release(). Hand it NULL instead: the
	 * second pass must not even compare against freed memory.
	 */
	priv->sec.common = NULL;
	priv->common_bmu_acquired = false;
free_priv:
	hns5_crypto_free_priv(dev);
destroy:
	rte_cryptodev_pmd_destroy(dev);
	return rc;
}

int
hns5_crypto_remove(struct rte_vdev_device *vdev)
{
	struct rte_cryptodev *dev;
	const char *name;
	int rc;

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
	 * engine and the BMU would stay owned by this process. The next start
	 * then reconfigures hardware that is still owned by the previous run,
	 * which is exactly why the first start after boot worked and the next one
	 * failed. Do it here, where the private data is still valid.
	 */
	rc = hns5_crypto_teardown(dev);
	if (rc == 0)
		hns5_crypto_free_priv(dev);
	else
		HNS5_LOG(ERR, "%s: teardown refused (%d); leaving the queue "
			 "pair arrays in place rather than freeing live objects",
			 name, rc);

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
/* Keys and value ranges: doc/guides/cryptodevs/hns5.rst. */
RTE_PMD_REGISTER_PARAM_STRING(HNS5_CRYPTO_DRIVER_NAME,
	HNS5_DEVARG_QPS "=<1-16>,"
	HNS5_DEVARG_VCPU "=<0-63>,"
	HNS5_DEVARG_CAP "=<0-65470>,"
	HNS5_DEVARG_POOL_MB "=<32-4096 MiB>,"
	HNS5_DEVARG_STATS "=<0|1>,"
	HNS5_DEVARG_BSP_LOG "=<default|debug|info|warn|err>,"
	HNS5_DEVARG_REAP "=<que|grp|msg>,"
	HNS5_DEVARG_REAPER_QP "=<0..qps-1|65535>,"
	HNS5_DEVARG_REAP_BATCH "=<number>,"
	HNS5_DEVARG_REAP_POLLS "=<number>,"
	HNS5_DEVARG_REAP_SPIN "=<number>,"
	HNS5_DEVARG_REAP_FAIR "=<0|1>,"
	HNS5_DEVARG_SMOOTH "=<0|1>,"
	HNS5_DEVARG_ZERO_COPY "=<0|1>,"
	HNS5_DEVARG_ZERO_COPY_OUT "=<0|1>,"
	HNS5_DEVARG_ZERO_COPY_IN "=<0|1>,"
	HNS5_DEVARG_ZC_APP_POOL "=<0|1>,"
	HNS5_DEVARG_POE_PRELOAD "=<0|1>,"
	HNS5_DEVARG_POE_ORDER "=<0|1>,"
	HNS5_DEVARG_POE_FLOW "=<0|1>,"
	HNS5_DEVARG_POE_INT_TH "=<number>,"
	HNS5_DEVARG_POE_BP_TH "=<number>,"
	HNS5_DEVARG_SEC_SKIP_LEN "=<0-15>,"
	HNS5_DEVARG_SEC_GET_POE "=<0|1>,"
	HNS5_DEVARG_DEBUG_MEMCPY "=<0|1>");
