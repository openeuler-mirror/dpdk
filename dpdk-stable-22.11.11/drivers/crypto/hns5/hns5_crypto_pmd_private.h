/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2026 Huawei Technologies Co., Ltd
 *
 * hns5 crypto PMD: lookaside symmetric crypto on the SEC engine.
 *
 * The hardware access path is adapted from the vendor SEC reference engine,
 * which drives the SEC engine in POE (Packet Order Engine) mode through the
 * BSP library libhidrv_api: a single SEC PF/VF is set up, submit/completion
 * POE groups are opened, DMA buffers are allocated from a file-backed 32 MiB
 * HugeTLB pool managed by the BMU, and a fresh 128-byte SEC descriptor is
 * built per request. Unlike the reference engine (synchronous single
 * request), the DPDK PMD keeps an in-flight ring per queue pair so that
 * enqueue submits bursts and dequeue polls POE completions asynchronously.
 *
 * Board BSP and SEC device are NOT present on a development host; this PMD
 * links libhidrv_api (skipped when absent) and only runs on the S920L18
 * image. Source-level checks against DPDK 22.11 plus the vendor headers are
 * the host-side verification boundary.
 */
#ifndef _HNS5_CRYPTO_PMD_PRIVATE_H_
#define _HNS5_CRYPTO_PMD_PRIVATE_H_

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <sys/types.h>

#include <rte_log.h>
#include <rte_cycles.h>
#include <rte_cryptodev.h>
#include <cryptodev_pmd.h>
#include <bus_vdev_driver.h>

/*
 * 逐 op 计时开关（devarg: stats=0/1，默认 0=关闭）。
 * 关闭时 hns5_tsc() 只做一次可预测分支，代价远低于 rte_rdtsc()
 * （arm64 上约 20~30 ns/次）。计数类统计（calls、成功/失败次数）
 * 始终累计；cycles 与 max 仅在开启时有效。
 */
extern bool hns5_sec_stats_en;

static inline uint64_t hns5_tsc(void)
{
	return hns5_sec_stats_en ? rte_rdtsc() : 0;
}

/* Board SDK vendor headers (read-only ABI). */
#include "hidrv_sec.h"
#include "hidrv_sec_vf_api.h"
#include "hidrv_sec_pf_api.h"
#include "hidrv_poe_vf_api.h"
#include "hidrv_poe_pf_api.h"
#include "hidrv_poe_xtp.h"
#include "hidrv_bmu_vf_api.h"
#include "hidrv_mbuf_api.h"
#include "ndrv_poe_vf_api.h"
#include "ndrv_poe_pf_api.h"

#define HNS5_CRYPTO_DRIVER_NAME crypto_hns5
#define HNS5_CRYPTO_LOG_PREFIX "crypto/hns5"

extern int hns5_logtype;
/*
 * All messages go through hns5_log_write(), which appends the trailing newline
 * when the format does not provide one. Several call sites used to omit it, so
 * unrelated messages ended up glued together on one line.
 */
void hns5_log_write(uint32_t level, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));

void hns5_log_limited(uint32_t level, uint64_t *cnt, uint64_t first,
		      uint64_t every, const char *fmt, ...)
	__attribute__((format(printf, 5, 6)));

#define HNS5_LOG(level, ...) \
	hns5_log_write(RTE_LOG_ ## level, HNS5_CRYPTO_LOG_PREFIX ": " __VA_ARGS__)

/*
 * Rate limited variant: the first @first messages always show up, afterwards
 * only every @every-th one. Callers keep the total in the counter and print it
 * in the teardown summary, so nothing is silently lost.
 */
#define HNS5_LOG_LIMITED(level, cnt, first, every, ...) \
	hns5_log_limited(RTE_LOG_ ## level, (cnt), (first), (every), \
			 HNS5_CRYPTO_LOG_PREFIX ": " __VA_ARGS__)

/*
 * Device statistics are written by the data path and read (or reset) from an
 * application thread, so every access goes through a relaxed atomic. On AArch64
 * these compile to plain loads/stores and a single add, so there is no lock and
 * no barrier on the enqueue/dequeue path.
 *
 * A reset uses an atomic store rather than a memset of the whole structure: a
 * worker that is between "process the op" and "account for it" must not have its
 * increment silently overwritten by a memset that races with it.
 */
static inline void
hns5_stat_inc(uint64_t *counter)
{
	__atomic_fetch_add(counter, 1, __ATOMIC_RELAXED);
}

static inline void
hns5_stat_add(uint64_t *counter, uint64_t value)
{
	__atomic_fetch_add(counter, value, __ATOMIC_RELAXED);
}

static inline uint64_t
hns5_stat_load(const uint64_t *counter)
{
	return __atomic_load_n(counter, __ATOMIC_RELAXED);
}

static inline void
hns5_stat_clear(uint64_t *counter)
{
	__atomic_store_n(counter, 0, __ATOMIC_RELAXED);
}

/*
 * SEC buffer layout.
 *
 * The reference design used a larger buffer with keys and MAC in a separate
 * page from the descriptor and the data. With zero_copy_out the engine writes
 * the payload straight into the application mbuf, so the buffer
 * only has to hold the descriptor, IV, the input copy and the keys: 16 KiB is
 * plenty and gives 1792 buffers instead of 448, which removes the buffer
 * budget as a limit on the queue pair count. The tail sits right behind the
 * largest payload we accept, so keys/IV/MAC stay in the same few pages as the
 * data (fewer TLB and cache misses per operation).
 *
 * The in-buffer offsets are defined once as the HNS5_SEC_*_OFFSET macros
 * below and referenced by name from the descriptor, the copies and the
 * completion path, so no numbers are repeated in comments.
 */
#define HNS5_SEC_MAX_PAYLOAD       15424u   /* fits the 15 KiB / jumbo case */
/* BMU pools the vendor driver configures per VF (POOL_MAX in its mempool). */
#define HNS5_BMU_POOL_MAX          16u

#define HNS5_SEC_MBUF_OFFSET       192u
#define HNS5_SEC_BUFFER_OFFSET     448u
#define HNS5_SEC_CIPHER_BLOCK_BYTES 16u
#define HNS5_SEC_DATA_OFFSET       576u
#define HNS5_SEC_PREFIX_BYTES      128u
#define HNS5_SEC_KEY_OFFSET        16128u   /* cipher key (lower, auth_offset=2) */
#define HNS5_SEC_MAC_OFFSET        16144u   /* MAC output (middle) */
/* The engine writes only the digest; clear a little extra so a stale tail can
 * never be mistaken for digest bytes when digest_size is odd. */
#define HNS5_SEC_DIGEST_CLEAR_BYTES 64u
/* Longest digest the engine produces (SM3 / SHA-256 output length). */
#define HNS5_SEC_MAX_DIGEST_BYTES   32u
#define HNS5_SEC_AUTH_KEY_OFFSET   16208u   /* auth key (higher than cipher) */
#define HNS5_BUFFER_BYTES         16384u
/* Hugepage block size of the pool mount, and the default pool size. */
#define HNS5_HUGE_BLOCK_BYTES     (32u * 1024u * 1024u)
#define HNS5_POOL_BYTES_DEFAULT   HNS5_HUGE_BLOCK_BYTES
#define HNS5_QUEUE_RESERVE_BYTES  (4u * 1024u * 1024u)
#define HNS5_BMU_BLOCKS           7u
#define HNS5_SEC_MAX_SEGMENTS     274u
#define HNS5_SEC_MESSAGE_MAX      0xfffe00u
#define HNS5_SEC_CIPHER_CHUNK     262144u
#define HNS5_SEC_SEGMENT_BYTES    HNS5_SEC_MAX_PAYLOAD
#define HNS5_MAX_RECV_POLLS       100000u
#define HNS5_SEC_SOURCE_PORT      0x60u

#define HNS5_SEC_CIPHER_ALG_SM4   0x3u
#define HNS5_SEC_CIPHER_ALG_AES   0x2u
#define HNS5_SEC_CIPHER_MODE_ECB  0x0u
#define HNS5_SEC_CIPHER_MODE_CBC  0x1u
#define HNS5_SEC_CIPHER_MODE_OFB  0x3u
#define HNS5_SEC_CIPHER_MODE_CTR  0x4u
/* SEC auth algorithm identifiers (vendor ABI). */
#define HNS5_SEC_AUTH_SM3         0xBu
#define HNS5_SEC_AUTH_HMAC_SM3    0xCu
#define HNS5_SEC_AUTH_SHA_256     0x1u
#define HNS5_SEC_AUTH_HMAC_SHA256 0xFu

/* POE routing, matching the reference platform defaults. */
#define HNS5_SEC_ENGINE_VCPU            1u
/*
 * Safety ceiling for the dynamically computed queue pair limit (the real limit
 * comes from the available SEC vcpus and the number of pool buffers).
 */
#define HNS5_SEC_QPS_CEILING           16u
/* Buffers a queue pair needs at the very least (cache + a few in flight). */
#define HNS5_SEC_MIN_PER_QP            40u
/* Completions pulled from the shared POE group per reaper call. */
#define HNS5_REAP_SHARED_BATCH          128u
/* Defaults used by the 21 Gbps / 4 QP measurement. */
#define HNS5_REAP_DEFAULT_BATCH         64u
#define HNS5_REAP_DEFAULT_POLLS         64u
/*
 * Upper bound on in-flight requests per queue pair. Deeper pipelines do not
 * help (the application keeps 32..64 requests in flight) but they do consume
 * the whole buffer pool, which turns every cache refill into a failed BMU
 * allocation.
 */
/*
 * In-flight ceiling per queue pair. Measured on 4 QP / 1024 B:
 *   128 -> 21.2 Gbps, 192 -> 20.1 Gbps (deeper pipeline only stretches the
 *   completion latency, and the failure counters stay the same because the
 *   application keeps refilling until the driver refuses).
 */
#define HNS5_SEC_MAX_INFLIGHT_PER_QP    128u
/* Extra ring slots above the in-flight cap, so a burst can always be taken. */
#define HNS5_QP_RING_SLACK             64u
/*
 * Software backlog depth per queue pair (smooth mode). It only stores op
 * pointers (8 bytes each), so a deep backlog is cheap: 4096 entries = 32 KiB.
 * It must be deeper than the application's in-flight window, otherwise it
 * overflows and the driver still reports short bursts.
 */
#define HNS5_SEC_BACKLOG                4096u
/* dequeue spin rounds before reporting "no completion" in smooth mode. */
#define HNS5_SEC_SMOOTH_SPIN            512u
/*
 * Poll budget per shared reap call. It is deliberately small: an empty poll
 * costs roughly 20 ns, and with many queue pairs each thread doing a long
 * empty spin wastes the CPU that should be submitting. Measured on 8 QP:
 * 64 polls per call made the aggregate throughput worse than 4 QP.
 */
#define HNS5_REAP_SHARED_POLLS          8u
/* Spin attempts before a thread gives up on the shared completion lock. */
#define HNS5_REAP_LOCK_SPIN             512u
/* Sentinel: every queue pair may reap (lazy shared reaping). */
#define HNS5_REAPER_NONE                0xffffu
/* Default lock spin: shorter under heavy contention (many queue pairs). */
#define HNS5_REAP_DEFAULT_SPIN          64u
/* Per-QP cache of SEC buffer segments. The BSP only offers single-buffer
 * alloc/free and the pool is shared, so every op used to take a global mutex
 * and make two BSP calls. Caching batches those: one locked refill per
 * HNS5_SEC_BUF_CACHE/2 allocations instead of one per op. */
#define HNS5_SEC_BUF_CACHE          64u
/* Completion group is a POE CPU group, so it must stay out of group 1 (the
 * kernel's SSH path) and groups 24..31; the submit group is an ACC group and
 * is checked against the usable ACC window in hns5_sec_platform.c. See the
 * full group plan in drivers/net/hns5/hns5_poe.h.
 */
#define HNS5_SEC_ENGINE_COMPLETION_GRP 12u

/* ------------------------------------------------------------------ */
/* Common runtime: HugeTLB pool + BMU ownership, per process.          */
/* ------------------------------------------------------------------ */

struct hns5_pool {
	void *va;
	uint64_t bytes;		/* pool size chosen at runtime */
	uint64_t pa;
	int file_fd;
	int lock_fd;
	int dir_fd;
	char *path;
	bool touched;
	bool recovered;
};

struct hns5_runtime {
	struct hns5_pool pool;
	int bmu_vf;
	bool poisoned;
	pid_t owner_pid;
	pthread_mutex_t queue_lock;
};

/* Per-request BMU-backed DMA segment. */
struct hns5_sec_segment {
	unsigned char *raw;
	uint64_t pa;
	size_t used;
	bool prepared;			/* descriptor slab already zeroed once */
	bool zero_copy;			/* data stays in the mbuf (both directions) */
	bool zero_copy_out;		/* engine writes the result into the mbuf */
	bool zero_copy_in;		/* engine reads the input from the mbuf */
	uint64_t data_pa;		/* mbuf data PA when zero_copy/zero_copy_out */
	struct hns5_sec_runtime *rt;	/* for per-op latency accounting */
};

/* ------------------------------------------------------------------ */
/* SEC engine runtime: device handles and POE routing.                  */
/* ------------------------------------------------------------------ */

struct hns5_sec_runtime {
	struct hns5_runtime *common;
	struct hns5_dev_private *priv;	/* owner device (completion routing) */
	int poe_pf;
	int poe_vf;
	int sec_pf;
	int sec_vf;
	unsigned int vcpu;
	unsigned int requested_vcpu;	/* devarg vcpu=; 0 = use HNS5_SEC_ENGINE_VCPU */
	unsigned int chan_index;	/* 0 = primary channel */
	struct ndrv_poe_pf_grp_range range;	/* POE group ranges from start */
	unsigned int submit_group;
	unsigned int completion_group;
	unsigned int submit_group_local;
	unsigned int completion_group_local;
	unsigned int submit_vqi;
	unsigned int completion_vqi;
	unsigned int error_vqi;
	/* Requested SEC engine options; applied only when the BSP exports the
	 * required native symbols (see hns5_sec_apply_vf_opts()). */
	unsigned int req_skip_len;	/* 0..15, 0 = leave default */
	unsigned int req_get_poe;	/* 0 = leave default, 1 = enable */

	/* POE group tunables (0 = keep the driver default). */
	unsigned int poe_order_en;
	unsigned int poe_preload_en;
	unsigned int poe_flow_en;
	unsigned int poe_int_th;
	unsigned int poe_bp_th;

	/* Which POE getter to use: 0 = per queue (default), 1 = per group,
	 * 2 = generic vcpu message queue. Lets the three be A/B tested. */
	unsigned int reap_mode;
	/* Latency accounting (cycles): submit, BMU alloc, POE read. */
	uint64_t send_calls, send_cycles, send_max_cycles;
	uint64_t ref_calls, ref_cycles, ref_max_cycles;   /* poe refresh mbuf */
	uint64_t ext_calls, ext_cycles, ext_max_cycles;   /* extract_result */
	uint64_t ext_copy_cycles;	/* 1 KiB copy back */
	uint64_t ext_digest_cycles;	/* MAC read + compare */
	uint64_t ext_parse_cycles;	/* descriptor parse */
	unsigned int zero_copy;		/* devarg: process data in the mbuf */
	unsigned int debug_memcpy;	/* devarg: run the memcpy micro-benchmark */
	unsigned int zero_copy_out;	/* devarg: let the engine write into the mbuf */
	unsigned int zero_copy_in;	/* devarg: let the engine read from the mbuf */
	unsigned int zc_app_pool;	/* devarg: register the app mbuf pool with BMU */
	/* Registered application pools (BMU physical pools 1..N). */
	struct {
		const struct rte_mempool *mp;
		uint64_t pa_lo, pa_hi;
	} zc_pools[4];
	unsigned int zc_pool_num;
	unsigned int zc_pool_pp_next;
	uint64_t zc_used;		/* ops processed with mbuf input */
	uint64_t zc_fallback;		/* BSP refused mbuf input, copied instead */
	unsigned int zc_in_disabled;	/* BSP refused once: stop trying */
	uint64_t send_fail;		/* SEC submit failures */
	/* Buffer accounting, so a leak or a starving cache is visible. */
	uint64_t cache_misses, refill_ok, refill_fail;
	uint64_t free_to_pool, free_fail;
	unsigned int smooth;		/* software backlog + dequeue spin */
	unsigned int reap_fair;		/* fairness early exit (costs throughput) */
	unsigned int reap_batch;	/* completions drained per lock hold */
	unsigned int reap_polls;	/* empty poll budget per reap call */
	unsigned int reap_spin;		/* pause count before giving up the lock */
	unsigned int reaper_qp;		/* dedicated reaper QP, 0xffff = lazy */
	unsigned int req_cap;		/* devarg: explicit in-flight cap */
	uint64_t pool_bytes;		/* pool size (devarg pool_mb) */
	uint64_t smooth_backlog;	/* ops accepted into the backlog */
	uint64_t smooth_spins;		/* dequeue spin rounds that found nothing */
	uint64_t zc_refused;		/* input zero-copy refused by BSP */
	uint64_t auth_mismatch;		/* digest comparison failures */
	uint64_t enq_cycles, enq_ops;	/* whole enqueue_burst() */
	uint64_t deq_cycles, deq_ops;	/* whole dequeue_burst() */
	uint64_t alloc_calls, alloc_cycles, alloc_max_cycles;
	/* Latency accounting of the single-message POE read (cycles). */
	uint64_t reap_calls;
	uint64_t reap_cycles;
	uint64_t reap_max_cycles;
	void *poe_pf_handle;
	void *poe_vf_handle;
	bool submit_open;
	bool complete_open;
	bool cpu_bound;
	bool sec_pf_started;
	bool sec_vf_started;
};

/* ------------------------------------------------------------------ */
/* DPDK session: software state, no persistent HW session (per-WQE key). */
/* ------------------------------------------------------------------ */

struct hns5_session {
	uint8_t cipher_alg;	/* SEC_CIPHER_ALG_* */
	uint8_t cipher_mode;	/* SEC_CIPHER_MODE_*  */
	uint8_t cipher_keylen;	/* bytes */
	uint8_t cipher_dir;	/* RTE_CRYPTO_CIPHER_OP_* */

	uint8_t auth_alg;	/* SEC_AUTH_* */
	uint8_t mac_len;	/* bytes */
	uint16_t akey_len;	/* bytes */
	uint8_t auth_op;	/* RTE_CRYPTO_AUTH_OP_* */
	uint16_t digest_len;	/* bytes, from xform auth */
	uint16_t iv_offset;	/* IV offset within op private data */
	uint16_t iv_length;	/* IV length, bytes */
	bool is_aead;		/* GCM: cipher==auth combined */

	uint8_t op_type;	/* 0 cipher, 1 auth, 2 chain cipher-then-auth */
	uint8_t cipher_key[64];	/* cipher key (SM4=16, AES-256=32) */
	uint8_t auth_key[64];	/* auth/HMAC key (max block size 64 for SM3/SHA256) */
};

/* ------------------------------------------------------------------ */
/* Queue pair: in-flight ring mapping submitted ops to POE completions. */
/* ------------------------------------------------------------------ */

struct hns5_inflight_op {
	struct rte_crypto_op *op;
	struct hns5_sec_segment seg;
	bool used;
	bool completed;		/* completion arrived out of order */
};

struct hns5_buf_cache {
	unsigned int limit;	/* per-QP cache size (multi-QP uses less) */
	unsigned int count;
	unsigned int head;	/* next slot to hand out */
	unsigned char *raw[HNS5_SEC_BUF_CACHE];
	uint64_t pa[HNS5_SEC_BUF_CACHE];
};

/* Completion routed from the shared POE group to the owning queue pair. */
struct hns5_cqe {
	uint64_t pa;		/* SEC buffer physical address */
};

struct hns5_qp {
	struct hns5_sec_runtime *sec;
	struct hns5_buf_cache cache;
	struct hns5_inflight_op *ring;
	uint16_t depth;		/* ring capacity */
	uint16_t prod_head;	/* next submit slot */
	uint16_t cons_tail;	/* next reap slot (FIFO, POE order=1) */
	uint16_t inflight;
	rte_spinlock_t lock;
	uint16_t id;			/* queue pair index */
	uint16_t max_inflight;		/* cap so the pool never starves */
	/* Software backlog: enqueue accepts operations even when the pipeline is
	 * full, so the application never sees a short burst (cperf "Failed Enq").
	 * Only used when the smooth option is enabled. */
	struct rte_crypto_op **bl;
	uint16_t bl_size, bl_head, bl_cnt;
	struct rte_cryptodev_stats stats;
	/*
	 * Hot counters live here, not in hns5_sec_runtime: that struct is shared
	 * by every queue pair of the channel, and updating a counter in it once
	 * per operation made all workers fight over the same cache line.
	 * Each field below is written by this queue pair's own thread only.
	 */
	uint64_t cache_hits;	/* segment served from the local cache */
	uint64_t cap_hits;	/* enqueue stopped because in-flight cap */
	bool in_use;		/* this queue pair has work in flight */
	char name[RTE_CRYPTODEV_NAME_MAX_LEN];
};

struct hns5_dev_private {
	struct hns5_sec_runtime sec;
	struct hns5_sec_runtime *extra;	/* (max_qps - 1) extra channels */
	struct hns5_qp **qps;
	uint16_t max_qps;
	uint16_t nb_chans;	/* SEC channels actually brought up */
	uint16_t nb_used_qps;
	bool common_runtime_acquired;
	/*
	 * SEC POE mode has a single completion group per VF, so every queue pair
	 * posts its completions there. One lock serialises the readers; each
	 * completion is routed to its owner through seg_owner[] and the per-QP
	 * cq ring.
	 */
	rte_spinlock_t reap_lock;
	uint64_t slot_base;		/* PA of SEC buffer 0 */
	uint32_t slot_count;		/* number of SEC buffers */
	uint32_t *slot_map;		/* slot -> (qp id + 1) << 16 | ring index */
	uint64_t reap_dispatch;		/* completions routed to another QP */
	uint64_t reap_orphan;		/* completions with no owner */
	uint16_t inflight_cap;		/* per queue pair in-flight limit */
	uint32_t buf_cache;		/* per queue pair buffer cache size */
};

/* hns5_crypto_session.c */
int hns5_session_configure(struct rte_crypto_sym_xform *xform, void *priv);
void hns5_session_clear(void *priv);

/* hns5_crypto_enqdeq.c */
/** Return every cached segment of this queue pair to the shared BMU pool. */
void hns5_qp_cache_flush(struct hns5_qp *qp);

/** Fill the per-QP buffer cache before the first operation. */
void hns5_qp_cache_prefill(struct hns5_qp *qp);

/** Fetch one completion from the shared POE group (no ownership checks). */
int hns5_sec_fetch_completion(struct hns5_sec_runtime *r, uint64_t *pa,
			      bool *from_error_queue, uint32_t max_polls);
uint16_t hns5_enqueue_burst(struct hns5_qp *qp, struct rte_crypto_op **ops,
			    uint16_t nb_ops);
uint16_t hns5_dequeue_burst(struct hns5_qp *qp, struct rte_crypto_op **ops,
			    uint16_t nb_ops);

/* hns5_crypto_runtime.c (common HugeTLB/BMU layer) */
int hns5_runtime_acquire(struct hns5_runtime **out, const char *path,
			 uint64_t pool_bytes);
int hns5_runtime_release(struct hns5_runtime *r);
void hns5_runtime_poison(struct hns5_runtime *r, const char *reason);
int hns5_runtime_lock(struct hns5_runtime *r, int *cancel_state);
void hns5_runtime_unlock(struct hns5_runtime *r, int cancel_state);
void *hns5_bmu_alloc(struct hns5_runtime *r, uint64_t *pa);
int hns5_bmu_free(struct hns5_runtime *r, void *buffer);
int hns5_pool_mark_hardware(struct hns5_pool *pool);
int hns5_bmu_reset(void);

/* hns5_sec_platform.c (SEC PF/VF + POE setup, mirrors sec_platform.c) */
int hns5_sec_platform_inspect(struct hns5_sec_runtime *r);
int hns5_sec_platform_start(struct hns5_sec_runtime *r);

/**
 * @brief Bring up additional SEC channels (one per extra queue pair).
 *
 * Each channel is a copy of the primary runtime that shares the BMU pool, the
 * SEC/POE handles and the session state, but owns its vcpu, its submit group
 * and its completion group, so completions cannot be reaped by two QPs.
 */
int hns5_sec_channels_start(struct hns5_sec_runtime *primary,
			    struct hns5_sec_runtime *extra, unsigned int count);
int hns5_sec_platform_close(struct hns5_sec_runtime *r);

/** Debug-only: 1 KiB memcpy micro benchmark (devarg debug_memcpy=1). */
void hns5_sec_bench_memcpy(struct hns5_sec_runtime *r);

/* hns5_sec_bd.c (128-byte descriptor build from session+op) */
int hns5_sec_build_descriptor(struct hns5_sec_runtime *r,
			      struct hns5_session *sess,
			      struct rte_crypto_op *op,
			      struct hns5_sec_segment *seg,
			      sec_alg_s *alg);

/* hns5_sec_complete.c (POE completion poll + write-back parse) */
int hns5_sec_reap_completion(struct hns5_sec_runtime *r,
			     struct hns5_sec_segment *seg, int *result);

/* Extract the engine result from a completed segment into the DPDK op. */
int hns5_sec_extract_result(struct hns5_sec_segment *seg,
			    struct hns5_session *sess, struct rte_crypto_op *op);

/* hns5_crypto_capabilities.c */
const struct rte_cryptodev_capabilities *hns5_caps_get(void);

/* hns5_crypto_pmd.c */
int hns5_crypto_probe(struct rte_vdev_device *vdev);
int hns5_crypto_remove(struct rte_vdev_device *vdev);

#endif /* _HNS5_CRYPTO_PMD_PRIVATE_H_ */
