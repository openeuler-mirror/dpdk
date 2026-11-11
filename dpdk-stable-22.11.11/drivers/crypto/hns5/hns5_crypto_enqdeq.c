/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2026 Huawei Technologies Co., Ltd
 *
 * Asynchronous enqueue/dequeue. enqueue builds a descriptor per op and submits
 * it through hidrv_send_sec, recording the op+segment in the in-flight ring.
 * dequeue polls POE for the ring head completion (POE is in-order, so
 * completions arrive in submission order), extracts the result and returns
 * the completed ops.
 */

#include "hns5_crypto_pmd_private.h"

#include <rte_crypto_sym.h>

static inline uint16_t
ring_next(uint16_t cur, uint16_t depth)
{
	return (uint16_t)((cur + 1) % depth);
}

/*
 * The BMU pool (LP0 free list) is shared by every SEC channel, so allocation
 * and release must be serialised. Everything else - ring bookkeeping, POE
 * submission, completion reaping - belongs to one queue pair and must NOT be
 * serialised, otherwise extra queue pairs could never run in parallel.
 */

static int
sec_bmu_free(struct hns5_runtime *common, void *p)
{
	int cancel;
	int rc;

	if (hns5_runtime_lock(common, &cancel))
		return -EIO;
	rc = hns5_bmu_free(common, p);
	hns5_runtime_unlock(common, cancel);
	return rc;
}

/*
 * Per-QP buffer cache.
 *
 * Every op needs one SEC segment from the shared BMU pool. The BSP exposes
 * only single-buffer alloc/free and the pool is shared by all channels, so the
 * naive path took a global mutex plus two BSP calls per op - which serialises
 * queue pairs and caps the multi-QP speedup. Cache a few segments per QP and
 * refill in batches of HNS5_SEC_BUF_CACHE/2.
 */
static void *
sec_cache_get(struct hns5_qp *qp, uint64_t *pa)
{
	struct hns5_buf_cache *c = &qp->cache;
	struct hns5_sec_runtime *sec = qp->sec;

	if (c->count != 0) {
		qp->cache_hits++;
		c->count--;
		*pa = c->pa[c->count];
		return c->raw[c->count];
	}

	sec->cache_misses++;
	/* Refill under one lock acquisition. */
	{
		struct hns5_runtime *common = sec->common;
		unsigned int i;
		int cancel;

		if (hns5_runtime_lock(common, &cancel))
			return NULL;
		for (i = 0; i < c->limit / 2; ++i) {
			uint64_t t0 = hns5_sec_stats_en ? hns5_tsc() : 0;
			void *raw = hns5_bmu_alloc(common, pa);
			uint64_t dt = hns5_sec_stats_en ? hns5_tsc() - t0 : 0;

			sec->alloc_calls++;
			if (hns5_sec_stats_en) {
				sec->alloc_cycles += dt;
				if (dt > sec->alloc_max_cycles)
					sec->alloc_max_cycles = dt;
			}

			if (raw == NULL)
				break;
			c->raw[c->count] = raw;
			c->pa[c->count] = *pa;
			c->count++;
		}
		hns5_runtime_unlock(common, cancel);
	}

	if (c->count == 0) {
		sec->refill_fail++;
		return NULL;
	}
	sec->refill_ok++;
	c->count--;
	*pa = c->pa[c->count];
	return c->raw[c->count];
}

static void
sec_cache_put(struct hns5_qp *qp, void *raw, uint64_t pa)
{
	struct hns5_buf_cache *c = &qp->cache;

	if (raw == NULL)
		return;
	if (c->count < c->limit) {
		c->raw[c->count] = raw;
		c->pa[c->count] = pa;
		c->count++;
		return;
	}
	qp->sec->free_to_pool++;
	if (sec_bmu_free(qp->sec->common, raw) != 0)
		qp->sec->free_fail++;
}

/*
 * Register the application's mbuf pool memory as extra BMU physical pools.
 *
 * The SEC input buffer must belong to the VF's BMU pool (hidrv_send_sec fails
 * with rc=-33 otherwise). The application owns its own mempool, so instead of
 * moving the application into our pool we register the pool's memory chunks as
 * further physical pools: the engine then accepts those addresses while the
 * application keeps its own free lists.
 */
/*
 * Fill the per queue pair buffer cache before the first operation, so the first
 * enqueue bursts never have to allocate from the BMU pool (which is what makes
 * them come up short).
 */
void
hns5_qp_cache_prefill(struct hns5_qp *qp)
{
	struct hns5_buf_cache *c;
	struct hns5_runtime *common;
	int cancel;

	if (qp == NULL || qp->sec == NULL || qp->sec->common == NULL)
		return;
	c = &qp->cache;
	if (c->limit == 0 || c->limit > HNS5_SEC_BUF_CACHE)
		return;

	/*
	 * Allocate straight into the cache. Using sec_cache_get() here would pop a
	 * buffer per iteration (and drop it), so the count would oscillate between
	 * 0 and limit/2 forever: the probe never returned and the pool leaked.
	 */
	common = qp->sec->common;
	if (hns5_runtime_lock(common, &cancel))
		return;
	while (c->count < c->limit) {
		uint64_t pa;
		void *raw = hns5_bmu_alloc(common, &pa);

		if (raw == NULL)
			break;	/* pool exhausted: keep what we have */
		c->raw[c->count] = raw;
		c->pa[c->count] = pa;
		c->count++;
	}
	hns5_runtime_unlock(common, cancel);
}

static uint16_t hns5_enqueue_impl(struct hns5_qp *qp,
				  struct rte_crypto_op **ops, uint16_t nb_ops);
static uint16_t hns5_dequeue_impl(struct hns5_qp *qp,
				  struct rte_crypto_op **ops, uint16_t nb_ops);

static int
hns5_sec_register_app_pool(struct hns5_sec_runtime *sec, struct rte_mempool *mp)
{
	enum { MAX_CHUNKS = 32 };
	struct {
		uint64_t pa;
		void *va;
		size_t len;
	} chunks[MAX_CHUNKS];
	struct rte_mempool_memhdr *hdr;
	unsigned int n = 0, i, j;
	uint32_t pp;

	for (i = 0; i < sec->zc_pool_num; i++)
		if (sec->zc_pools[i].mp == mp)
			return 0;
	if (sec->zc_pool_num >= RTE_DIM(sec->zc_pools))
		return -ENOSPC;

	/* Collect, then sort by PA so adjacent chunks can be merged. */
	STAILQ_FOREACH(hdr, &mp->mem_list, next) {
		if (n == MAX_CHUNKS)
			break;
		if (hdr->iova == RTE_BAD_IOVA || hdr->len == 0)
			continue;
		chunks[n].pa = hdr->iova;
		chunks[n].va = hdr->addr;
		chunks[n].len = hdr->len;
		n++;
	}
	for (i = 1; i < n; i++) {          /* insertion sort, n is tiny */
		for (j = i; j > 0 && chunks[j - 1].pa > chunks[j].pa; j--) {
			typeof(chunks[0]) t = chunks[j];

			chunks[j] = chunks[j - 1];
			chunks[j - 1] = t;
		}
	}

	pp = sec->zc_pool_pp_next + 1;
	for (i = 0; i < n; i = j) {
		uint64_t pa = chunks[i].pa;
		void *va = chunks[i].va;
		size_t len = chunks[i].len;

		/* Merge physically adjacent chunks (typical 2 MiB pages). */
		for (j = i + 1; j < n && chunks[j].pa == pa + len; j++)
			len += chunks[j].len;

		HNS5_LOG(DEBUG, "zc: chunk len=%zu (merged %u)", len, j - i);

		{
			const uint64_t mask = 0x3fffffULL;   /* 4 MiB - 1 */
			uint64_t pa_al = (pa + mask) & ~mask;/* align up */
			size_t delta = (size_t)(pa_al - pa);
			void *va_al = (char *)va + delta;
			uint32_t alen;
			struct hidrv_bmu_pp_cfg cfg;
			int rc;

			if (len <= delta) {
				HNS5_LOG(WARNING, "zc: chunk len=%zu too small "
					 "to align up to 4 MiB", len);
				continue;
			}
			alen = (uint32_t)((len - delta) & ~(size_t)mask);
			if (alen < (4u << 20)) {
				HNS5_LOG(WARNING, "zc: chunk len=%zu leaves "
					 "%u B after 4 MiB alignment (<4 MiB)", len, alen);
				continue;
			}
			cfg.phys_addr = pa_al;
			cfg.addr = va_al;
			cfg.size = alen;
			pa = pa_al;	/* pa_hi/pa_lo below use it; va/len are re-read
					 * from chunks[] on the next iteration */
			rc = hidrv_bmu_init_pp((unsigned int)sec->sec_vf, pp, &cfg);
			if (rc != 0) {
				HNS5_LOG(WARNING, "zc: init_pp(pp=%u, len=%u) failed rc=%d",
					 pp, alen, rc);
				continue;
			}
			/*
			 * Also create a logical pool on top of it: the BSP validates
			 * the input address against the pool metadata, and a bare
			 * physical pool registration was not enough (rc=-33).
			 * The application keeps managing these buffers itself, so the
			 * LP free list is never used to hand them out.
			 */
			{
				struct hidrv_bmu_hard_mng_lp_cfg lp;
				uint32_t lp_id = pp;   /* LP0 belongs to our own pool */
				uint32_t blocks = alen / (4u << 20);
				/* buffer_size must divide the 4 MiB block; pick
				 * the smallest power-of-two >= elt_size. */
				uint32_t elt = 64;

				while (elt < (uint32_t)mp->elt_size && elt < 4096)
					elt <<= 1;

				memset(&lp, 0, sizeof(lp));
				lp.buffer_size = elt;
				lp.buffer_max = 0xffffff;
				lp.buffer_min = 0xffffff;
				lp.max_block_num = blocks;
				lp.min_block_num = blocks;
				if (hidrv_bmu_init_hard_mng_lp(
						(unsigned int)sec->sec_vf, lp_id,
						&lp) != 0) {
					HNS5_LOG(WARNING, "zc: init_hard_mng_lp(lp=%u, "
						 "buffer_size=%u, blocks=%u) failed; "
						 "input zero-copy may stay rejected",
						 lp_id, elt, blocks);
				} else {
					HNS5_LOG(NOTICE, "zc: logical pool lp=%u over "
						 "pp=%u (buffer_size=%u, blocks=%u)",
						 lp_id, pp, elt, blocks);
				}
			}
			sec->zc_pools[sec->zc_pool_num].mp = mp;
			sec->zc_pools[sec->zc_pool_num].pa_lo = pa;
			sec->zc_pools[sec->zc_pool_num].pa_hi = pa + alen;
			sec->zc_pool_num++;
			sec->zc_pool_pp_next++;
			HNS5_LOG(NOTICE, "zc: app pool %s registered as BMU pp=%u "
				 "(span=%zu B)", mp->name, pp, (size_t)alen);
			pp++;
			if (sec->zc_pool_num >= RTE_DIM(sec->zc_pools))
				break;
		}
	}
	if (sec->zc_pool_num == 0) {
		/* Do not retry on every op: disable the feature and keep the
		 * copy-in path. */
		sec->zc_app_pool = 0;
		HNS5_LOG(WARNING, "zc: app pool registration failed; input "
			 "zero-copy disabled (copy path stays)");
		return -ENOTSUP;
	}
	return 0;
}

static bool
hns5_sec_pool_registered(struct hns5_sec_runtime *sec, struct rte_mempool *mp)
{
	unsigned int i;

	for (i = 0; i < sec->zc_pool_num; i++)
		if (sec->zc_pools[i].mp == mp)
			return true;
	return false;
}

void
hns5_qp_cache_flush(struct hns5_qp *qp)
{
	struct hns5_buf_cache *c;

	if (qp == NULL || qp->sec == NULL || qp->sec->common == NULL)
		return;
	c = &qp->cache;
	while (c->count != 0) {
		c->count--;
		(void)sec_bmu_free(qp->sec->common, c->raw[c->count]);
	}
}

uint16_t
hns5_enqueue_impl(struct hns5_qp *qp, struct rte_crypto_op **ops, uint16_t nb_ops)
{
	struct hns5_sec_runtime *sec;
	uint16_t i;
	uint16_t enqueued = 0;
	uint64_t t_begin;

	if (qp == NULL || ops == NULL || nb_ops == 0)
		return 0;
	sec = qp->sec;
	if (sec == NULL || sec->common == NULL || sec->common->poisoned)
		return 0;
	t_begin = hns5_sec_stats_en ? hns5_tsc() : 0;

	for (i = 0; i < nb_ops; i++) {
		struct rte_crypto_op *op = ops[i];
		struct hns5_session *sess;
		struct hns5_inflight_op *slot;
		sec_alg_s alg;
		uint64_t pa = 0;
		int rc;

		if (qp->inflight == qp->depth ||
		    (qp->max_inflight != 0 &&
		     qp->inflight >= qp->max_inflight)) {
			qp->cap_hits++;
			break;
		}

		sess = (struct hns5_session *)CRYPTODEV_GET_SYM_SESS_PRIV(op->sym->session);
		if (sess == NULL) {
			op->status = RTE_CRYPTO_OP_STATUS_INVALID_ARGS;
			hns5_stat_inc(&qp->stats.enqueue_err_count);
			continue;
		}

		slot = &qp->ring[qp->prod_head];
		slot->seg.raw = sec_cache_get(qp, &pa);
		if (slot->seg.raw == NULL) {
			op->status = RTE_CRYPTO_OP_STATUS_NOT_PROCESSED;
			hns5_stat_inc(&qp->stats.enqueue_err_count);
			break;
		}
		slot->seg.pa = pa;
		slot->seg.rt = sec;
		slot->seg.zero_copy = false;
		slot->seg.zero_copy_out = false;
		slot->seg.zero_copy_in = false;
		if (sec->zero_copy || sec->zero_copy_out || sec->zero_copy_in) {
			struct rte_mbuf *m = op->sym->m_src;
			uint32_t off = (sess->op_type == 1) ?
				       op->sym->auth.data.offset :
				       op->sym->cipher.data.offset;
			uint32_t len = (sess->op_type == 1) ?
				       op->sym->auth.data.length :
				       op->sym->cipher.data.length;
			uint32_t aoff = op->sym->auth.data.offset;

			/* In place, single segment, whole payload inside the
			 * segment, 64 B aligned start, and (for chains) the
			 * cipher and auth ranges must be the same. */
			if (op->sym->m_dst == NULL && m != NULL && len != 0 &&
			    m->nb_segs == 1 && (uint32_t)m->data_len >= off + len &&
			    (sess->op_type != 2 || aoff == off)) {
				uint64_t dpa = rte_mbuf_data_iova(m) + off;
				/* Once the BSP refused the mbuf input, never
				 * try it again (see the fallback below). */
				bool want_in = (sec->zero_copy || sec->zero_copy_in) &&
					       !sec->zc_in_disabled;
				bool in_ok = false;

				if (want_in && sec->zc_app_pool &&
				    hns5_sec_register_app_pool(sec, m->pool) == 0 &&
				    hns5_sec_pool_registered(sec, m->pool))
					in_ok = true;

				if ((dpa & 63u) == 0) {
					slot->seg.data_pa = dpa;
					/* Keep the hidden mbuf header describing the
					 * SEC buffer: describing the application mbuf
					 * was what the BSP refused earlier. */
					slot->seg.zero_copy = false;
					slot->seg.zero_copy_in = in_ok;
					slot->seg.zero_copy_out =
						sec->zero_copy || sec->zero_copy_out;
				}
			}
		}
		/*
		 * Clear only what this op actually depends on. The old code zeroed
		 * the whole 64 KiB segment for every op - ~100x more memory traffic
		 * than the packet itself (64 B .. 1 KiB) and the single biggest cost
		 * in the crypto throughput numbers.
		 *
		 * Covered here:
		 *   [0, HNS5_SEC_DATA_OFFSET)     descriptor slab the engine parses
		 *                                 (hidden mbuf header + reserved
		 *                                 fields build_descriptor does not set)
		 *   digest slot                   engine writes digest_size bytes,
		 *                                 the tail must not carry stale data
		 * Not covered, on purpose:
		 *   IV   (HNS5_SEC_DATA_OFFSET)   written by build_descriptor each op
		 *   data (DATA_OFFSET+PREFIX)     fully written by copy_in_data()
		 *   keys                          fully written by build_descriptor
		 *   tail slack                    never read by the engine or us
		 */
		/*
		 * The descriptor slab only needs to be zeroed once per cached
		 * segment: every field the engine parses is either written by
		 * build_descriptor() on each op or stays zero forever. Clearing it
		 * per op was pure memory traffic on a DMA-cold 64 KiB buffer.
		 */
		if (!slot->seg.prepared) {
			memset(slot->seg.raw, 0, HNS5_SEC_DATA_OFFSET);
			memset(slot->seg.raw + HNS5_SEC_MAC_OFFSET, 0,
			       HNS5_SEC_DIGEST_CLEAR_BYTES);
			slot->seg.prepared = true;
		}

		rc = hns5_sec_build_descriptor(sec, sess, op, &slot->seg, &alg);
		if (rc) {
			sec_cache_put(qp, slot->seg.raw, slot->seg.pa);
			op->status = RTE_CRYPTO_OP_STATUS_INVALID_ARGS;
			continue;
		}

		slot->op = op;
		slot->used = true;
		slot->completed = false;
		rte_wmb();   /* 只需保证"清标记"先于本次提交到达内存 */

		{
			uint64_t t0 = hns5_sec_stats_en ? hns5_tsc() : 0;
			bool fell_back = false;
			int send_rc;

			/*
			 * Register ownership BEFORE submitting: the engine can complete
			 * the request (and another thread can reap it) before
			 * hidrv_send_sec() returns, and an unregistered buffer address
			 * would make the reaper drop that completion - the request would
			 * stay in flight forever and its buffer would leak. That leak
			 * only shows up on long runs.
			 */
			if (sec->priv != NULL && sec->priv->slot_map != NULL &&
			    slot->seg.pa >= sec->priv->slot_base) {
				uint64_t idx = (slot->seg.pa - sec->priv->slot_base) /
					       HNS5_BUFFER_BYTES;

				if (idx < sec->priv->slot_count)
					sec->priv->slot_map[idx] =
						((uint32_t)(qp->id + 1U) << 16) |
						(uint32_t)qp->prod_head;
			}

			send_rc = hidrv_send_sec((uint32_t)sec->sec_vf, sec->vcpu,
					   slot->seg.raw + HNS5_SEC_MBUF_OFFSET, &alg);

			if (send_rc && slot->seg.zero_copy_in &&
			    !sec->zc_in_disabled) {
				/* The BSP refused the mbuf input: rebuild the descriptor
				 * in copy mode and resubmit once (never poison for this). */
				sec->zc_in_disabled = 1;
				HNS5_LOG_LIMITED(WARNING, &sec->zc_refused, 5, 1000,
					"zc_in refused by BSP rc=%d; falling back to "
					"the copy path", send_rc);
				slot->seg.zero_copy_in = false;
				slot->seg.zero_copy = false;
				sec->zc_fallback++;
				fell_back = true;
				if (hns5_sec_build_descriptor(sec, sess, op,
							      &slot->seg, &alg) != 0) {
					HNS5_LOG(ERR, "rebuild descriptor for "
						 "fallback failed");
					send_rc = -EIO;
				} else {
					send_rc = hidrv_send_sec(
						(uint32_t)sec->sec_vf, sec->vcpu,
						slot->seg.raw +
						HNS5_SEC_MBUF_OFFSET, &alg);
				}
			}
			if (send_rc) {
				HNS5_LOG_LIMITED(ERR, &sec->send_fail, 5, 1000,
					 "hidrv_send_sec failed rc=%d "
					 "(zc_in=%u zc_out=%u)", send_rc,
					 slot->seg.zero_copy_in,
					 slot->seg.zero_copy_out);
				sec_cache_put(qp, slot->seg.raw, slot->seg.pa);
				slot->used = false;
				op->status = RTE_CRYPTO_OP_STATUS_ERROR;
				hns5_stat_inc(&qp->stats.enqueue_err_count);
				if (!fell_back) {
					/* First failure is unexpected: stop using
					 * the device until re-initialised. */
					hns5_runtime_poison(sec->common,
						"SEC submit failed; DMA ownership uncertain");
				}
				if (sec->priv != NULL && sec->priv->slot_map != NULL &&
				    slot->seg.pa >= sec->priv->slot_base) {
					uint64_t idx = (slot->seg.pa -
							sec->priv->slot_base) /
						       HNS5_BUFFER_BYTES;

					if (idx < sec->priv->slot_count)
						sec->priv->slot_map[idx] = 0;
				}
				sec->send_fail++;
				break;
			}
			if (hns5_sec_stats_en) {
				uint64_t dt = hns5_tsc() - t0;

				sec->send_calls++;
				sec->send_cycles += dt;
				if (dt > sec->send_max_cycles)
					sec->send_max_cycles = dt;
			}
		}

		if (slot->seg.zero_copy_in)
			sec->zc_used++;
		qp->in_use = true;
		qp->prod_head = ring_next(qp->prod_head, qp->depth);
		qp->inflight++;
		enqueued++;
	}

	hns5_stat_add(&qp->stats.enqueued_count, enqueued);
	if (hns5_sec_stats_en) {
		sec->enq_cycles += hns5_tsc() - t_begin;
		sec->enq_ops += enqueued;
	}
	return enqueued;
}

/*
 * Pull up to @max completions from the shared POE group and route each one to
 * the queue pair that submitted it. Only one thread may read the group at a
 * time, hence the trylock: a thread that loses it simply returns and retries
 * later (its own completions may already be in its ring).
 */
static int
hns5_reap_shared(struct hns5_dev_private *priv, struct hns5_qp *self,
		 unsigned int max)
{
	struct hns5_sec_runtime *r = &priv->sec;
	unsigned int got = 0;

	if (max == 0U)
		max = HNS5_REAP_SHARED_BATCH;
	if (r->reap_polls == 0U)
		r->reap_polls = HNS5_REAP_SHARED_POLLS;

	{
		unsigned int spin = 0;

		unsigned int limit = priv->sec.reap_spin ?
				     priv->sec.reap_spin :
				     HNS5_REAP_DEFAULT_SPIN;

		while (!rte_spinlock_trylock(&priv->reap_lock)) {
			if (++spin >= limit)
				return 0;
			rte_pause();
		}
	}

	while (got < max) {
		uint64_t pa = 0;
		uint64_t idx, entry;
		struct hns5_qp *owner;
		bool errq = false;
		int rc;

		rc = hns5_sec_fetch_completion(r, &pa, &errq,
					       r->reap_polls);
		if (rc == -ETIMEDOUT)
			break;
		if (rc != 0) {
			HNS5_LOG_LIMITED(ERR, &priv->reap_orphan, 5, 1000,
					 "unexpected completion message (rc=%d)", rc);
			continue;
		}
		got++;

		if (pa < priv->slot_base) {
			priv->reap_orphan++;
			continue;
		}
		idx = (pa - priv->slot_base) / HNS5_BUFFER_BYTES;
		if (idx >= priv->slot_count) {
			priv->reap_orphan++;
			continue;
		}
		entry = priv->slot_map[idx];
		if (entry == 0) {
			priv->reap_orphan++;
			continue;
		}
		owner = priv->qps[(entry >> 16) - 1U];
		if (owner == NULL) {
			priv->reap_orphan++;
			continue;
		}
		{
			uint16_t slot = (uint16_t)(entry & 0xffffU);

			__atomic_store_n(&owner->ring[slot].completed, true,
					 __ATOMIC_RELEASE);
		}
		priv->reap_dispatch++;

		/*
		 * Fairness mode is only used with smooth: there the backlog makes a
		 * thread that lags behind hold up everyone else, so stopping as soon
		 * as our own request is flagged helps. In the normal mode the full
		 * batch is drained under one lock hold, which amortises the lock cost
		 * (measured 21 Gbps vs 18 Gbps for early exit).
		 */
		if (priv->sec.reap_fair && self != NULL && self->inflight != 0 &&
		    __atomic_load_n(&self->ring[self->cons_tail].completed,
				    __ATOMIC_ACQUIRE))
			break;
	}

	rte_spinlock_unlock(&priv->reap_lock);
	return (int)got;
}

uint16_t
hns5_dequeue_impl(struct hns5_qp *qp, struct rte_crypto_op **ops, uint16_t nb_ops)
{
	struct hns5_sec_runtime *sec;
	uint16_t dequeued = 0;
	uint64_t t_begin;

	if (qp == NULL || ops == NULL || nb_ops == 0)
		return 0;
	sec = qp->sec;
	if (sec == NULL || sec->common == NULL)
		return 0;
	t_begin = hns5_sec_stats_en ? hns5_tsc() : 0;

	while (dequeued < nb_ops && qp->inflight > 0) {
		struct hns5_inflight_op *slot = &qp->ring[qp->cons_tail];
		struct rte_crypto_op *op;
		struct hns5_session *sess;
		int rc;

		if (!slot->used)
			break;

		if (__atomic_load_n(&slot->completed, __ATOMIC_ACQUIRE)) {
			__atomic_store_n(&slot->completed, false, __ATOMIC_RELAXED);
			/* Completion already flagged by the reaper; rc is assigned by
			 * hns5_sec_extract_result() below. */
		} else if (sec->priv != NULL && sec->priv->nb_chans > 1U &&
			   sec->reaper_qp != HNS5_REAPER_NONE &&
			   qp->id != sec->reaper_qp) {
			/*
			 * Dedicated reaper mode: only that queue pair's thread drains
			 * the shared completion group. Everyone else just checks its
			 * flag, so the lock is uncontended and the 8-thread ping-pong
			 * on it disappears. Our head is not ready yet: report nothing.
			 */
			break;
		} else if (sec->priv != NULL && sec->priv->nb_chans > 1U) {
			/* Shared group: help draining it, then re-check the flag.
			 * The reaper flags our exact ring entry by buffer address, so
			 * completion order does not matter. */
			if (hns5_reap_shared(sec->priv, qp,
					     sec->reap_batch) == 0)
				break;	/* nothing available yet */
			continue;
		} else {
			rc = hns5_sec_reap_completion(sec, &slot->seg, NULL);
			if (rc == -ETIMEDOUT)
				break;	/* no completion yet */
			if (rc == -EIO)
				break;	/* completion mismatch */
		}

		op = slot->op;
		sess = (struct hns5_session *)CRYPTODEV_GET_SYM_SESS_PRIV(op->sym->session);
		/*
		 * The engine wrote these areas through DMA, so they are cold in the
		 * CPU cache: prefetch them before the descriptor parse, the digest
		 * compare and the copy back.
		 */
		rte_prefetch0(slot->seg.raw + HNS5_SEC_MBUF_OFFSET);
		rte_prefetch0(slot->seg.raw + HNS5_SEC_DATA_OFFSET);
		rte_prefetch0(slot->seg.raw + HNS5_SEC_DATA_OFFSET +
			      HNS5_SEC_PREFIX_BYTES);
		rte_prefetch0(slot->seg.raw + HNS5_SEC_MAC_OFFSET);
		rc = hns5_sec_extract_result(&slot->seg, sess, op);
		if (sec->priv != NULL && sec->priv->slot_map != NULL &&
		    slot->seg.pa >= sec->priv->slot_base) {
			uint64_t idx = (slot->seg.pa - sec->priv->slot_base) /
				       HNS5_BUFFER_BYTES;

			if (idx < sec->priv->slot_count)
				sec->priv->slot_map[idx] = 0;
		}
		sec_cache_put(qp, slot->seg.raw, slot->seg.pa);
		slot->used = false;
		qp->cons_tail = ring_next(qp->cons_tail, qp->depth);
		qp->inflight--;

		if (rc == 0) {
			op->status = RTE_CRYPTO_OP_STATUS_SUCCESS;
		} else if (rc == -EBADMSG) {
			op->status = RTE_CRYPTO_OP_STATUS_AUTH_FAILED;
			hns5_stat_inc(&qp->stats.dequeue_err_count);
		} else {
			op->status = RTE_CRYPTO_OP_STATUS_ERROR;
			hns5_stat_inc(&qp->stats.dequeue_err_count);
		}

		ops[dequeued++] = op;
	}

	if (dequeued != 0)
		hns5_stat_add(&qp->stats.dequeued_count, dequeued);

	qp->in_use = (qp->inflight != 0);

	if (hns5_sec_stats_en) {
		sec->deq_cycles += hns5_tsc() - t_begin;
		sec->deq_ops += dequeued;
	}
	return dequeued;
}

/*
 * Smooth mode wrappers.
 *
 * enqueue: submit what the pipeline can take, park the rest in a software
 *          backlog and still report them as accepted, so the application never
 *          sees a short burst. Only a full backlog can make enqueue fail.
 * dequeue: push the backlog first, then poll; when nothing completed yet, keep
 *          polling for a bounded number of rounds instead of returning zero
 *          immediately (the caller retries at once anyway, so this costs the
 *          same CPU but gives a stable statistic).
 */
static void
hns5_backlog_push(struct hns5_qp *qp, struct rte_crypto_op *op)
{
	if (qp->bl_cnt >= qp->bl_size)
		return;
	qp->bl[(qp->bl_head + qp->bl_cnt) % qp->bl_size] = op;
	qp->bl_cnt++;
}

static void
hns5_flush_backlog(struct hns5_qp *qp)
{
	struct rte_crypto_op *batch[32];
	uint16_t n, sent, i;

	if (qp->bl == NULL || qp->bl_cnt == 0)
		return;

	n = qp->bl_cnt;
	if (n > 32)
		n = 32;
	for (i = 0; i < n; i++)
		batch[i] = qp->bl[(qp->bl_head + i) % qp->bl_size];

	sent = hns5_enqueue_impl(qp, batch, n);
	if (sent == 0)
		return;		/* still full: keep everything queued */

	qp->bl_head = (uint16_t)((qp->bl_head + sent) % qp->bl_size);
	qp->bl_cnt = (uint16_t)(qp->bl_cnt - sent);
}

uint16_t
hns5_enqueue_burst(struct hns5_qp *qp, struct rte_crypto_op **ops,
		   uint16_t nb_ops)
{
	uint16_t done, i;
	struct hns5_sec_runtime *sec;

	if (qp == NULL || ops == NULL || nb_ops == 0)
		return 0;

	/* Push the backlog first so the pipeline is not starved by new work. */
	hns5_flush_backlog(qp);

	done = hns5_enqueue_impl(qp, ops, nb_ops);
	if (done == nb_ops || qp->bl == NULL)
		return done;

	sec = qp->sec;
	for (i = done; i < nb_ops; i++) {
		if (qp->bl_cnt >= qp->bl_size)
			break;
		hns5_backlog_push(qp, ops[i]);
		if (sec != NULL)
			sec->smooth_backlog++;
	}
	return (uint16_t)(done + (i - done));
}

uint16_t
hns5_dequeue_burst(struct hns5_qp *qp, struct rte_crypto_op **ops,
		   uint16_t nb_ops)
{
	uint16_t got;
	unsigned int spin = 0;
	struct hns5_sec_runtime *sec;

	if (qp == NULL || ops == NULL || nb_ops == 0)
		return 0;

	hns5_flush_backlog(qp);
	got = hns5_dequeue_impl(qp, ops, nb_ops);
	sec = qp->sec;
	if (got != 0 || qp->bl == NULL || sec == NULL || qp->inflight == 0)
		return got;

	/*
	 * Do not spin while the backlog still holds work: spinning blocks the
	 * submission that would drain it, and the engine goes idle. Return at once
	 * so the application enqueues again, which flushes the backlog.
	 */
	if (qp->bl_cnt != 0)
		return 0;

	while (spin++ < HNS5_SEC_SMOOTH_SPIN) {
		rte_pause();
		hns5_flush_backlog(qp);
		got = hns5_dequeue_impl(qp, ops, nb_ops);
		if (got != 0)
			return got;
		if (qp->bl_cnt != 0 || qp->inflight == 0)
			break;
	}
	sec->smooth_spins++;
	return 0;
}
