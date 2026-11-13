/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2026 Huawei Technologies Co., Ltd
 *
 * POE completion handling. Mirrors the reference engine's complete(): poll the
 * POE completion virtual queues for a message carrying the segment's physical
 * address, then read the write-back mbuf state. Because POE order_type is 0
 * (in-order), completions arrive in submission order, so dequeue reaps the
 * in-flight ring head.
 */

#include "hns5_crypto_pmd_private.h"

#include <string.h>
#include <rte_memcpy.h>
#include <rte_crypto_sym.h>
#include "poe_errno.h"

/* Poll POE for a completion whose message carries seg->pa. Returns 0 on
 * success, -ETIMEDOUT if no completion arrived within the budget, -EIO on a
 * mismatched completion. */
/*
 * Fetch one POE completion message without checking which request it belongs to.
 * The POE group is shared by all queue pairs, so the caller routes it by the
 * buffer address. Returns 0 on success, -ETIMEDOUT when nothing arrived and
 * -EIO when the message is not a valid SEC completion.
 */
int
hns5_sec_fetch_completion(struct hns5_sec_runtime *r, uint64_t *pa,
			  bool *from_error_queue, uint32_t max_polls)
{
	union ndrv_poe_msg msg = { 0 };
	unsigned int queue = 0;
	uint32_t empty_polls = 0;
	unsigned int rc;

	if (r == NULL || pa == NULL)
		return -EINVAL;

	for (;;) {
		if (r->reap_mode == 1)
			rc = ndrv_poe_vf_get_grp_msg(r->poe_vf_handle, r->vcpu,
						     r->completion_group, &msg);
		else if (r->reap_mode == 2)
			rc = ndrv_poe_vf_get_msg(r->poe_vf_handle, r->vcpu, &msg);
		else
			rc = ndrv_poe_vf_get_que_msg(r->poe_vf_handle, r->vcpu,
						     r->completion_group, queue, &msg);
		if (hns5_sec_stats_en)
			r->reap_calls++;
		if (!rc)
			break;
		if (r->reap_mode == 0 && !UDRV_IS_ERR(rc, POE_GET_QUEUE_FAIL))
			return -EIO;
		if (++empty_polls >= (max_polls ? max_polls :
				      HNS5_MAX_RECV_POLLS))
			return -ETIMEDOUT;
		queue ^= 1;
	}

	if (msg.packet_msg.t_tagvalue != (NDRV_POE_ADD_PACKET & 15) ||
	    msg.packet_msg.sourceport != HNS5_SEC_SOURCE_PORT ||
	    msg.packet_msg.order_bindval != 0 ||
	    msg.packet_msg.order_type != 0)
		return -EIO;

	if (r->reap_mode == 0) {
		if (msg.packet_msg.vqi == r->completion_vqi)
			*from_error_queue = false;
		else if (msg.packet_msg.vqi == r->error_vqi)
			*from_error_queue = true;
		else
			return -EIO;
	} else {
		*from_error_queue = (msg.packet_msg.vqi == r->error_vqi);
	}

	*pa = ((uint64_t)msg.packet_msg.addr_47_32 << 32) |
	      msg.packet_msg.addr_31_0;
	return 0;
}

int
hns5_sec_reap_completion(struct hns5_sec_runtime *r,
			struct hns5_sec_segment *seg)
{
	union ndrv_poe_msg msg = { 0 };
	unsigned int queue = 0;
	uint32_t empty_polls = 0;
	unsigned int rc;

	if (r == NULL || seg == NULL)
		return -EINVAL;

	for (;;) {
		uint64_t t0 = hns5_sec_stats_en ? hns5_tsc() : 0;

		if (r->reap_mode == 1)
			rc = ndrv_poe_vf_get_grp_msg(r->poe_vf_handle, r->vcpu,
						     r->completion_group, &msg);
		else if (r->reap_mode == 2)
			rc = ndrv_poe_vf_get_msg(r->poe_vf_handle, r->vcpu, &msg);
		else
			rc = ndrv_poe_vf_get_que_msg(r->poe_vf_handle, r->vcpu,
						     r->completion_group, queue, &msg);
		if (hns5_sec_stats_en) {
			uint64_t dt = hns5_tsc() - t0;

			r->reap_calls++;
			r->reap_cycles += dt;
			if (dt > r->reap_max_cycles)
				r->reap_max_cycles = dt;
		}
		if (!rc)
			break;
		if (r->reap_mode == 0 &&
		    !UDRV_IS_ERR(rc, POE_GET_QUEUE_FAIL))
			return -EIO;
		if (++empty_polls >= HNS5_MAX_RECV_POLLS)
			return -ETIMEDOUT;
		queue ^= 1;
	}

	uint64_t pa = ((uint64_t)msg.packet_msg.addr_47_32 << 32) |
		      msg.packet_msg.addr_31_0;
	if (msg.packet_msg.t_tagvalue != (NDRV_POE_ADD_PACKET & 15) ||
	    msg.packet_msg.sourceport != HNS5_SEC_SOURCE_PORT ||
	    msg.packet_msg.order_bindval != 0 ||
	    msg.packet_msg.order_type != 0 ||
	    (r->reap_mode == 0 ?
	     msg.packet_msg.vqi != (queue ? r->error_vqi : r->completion_vqi) :
	     (msg.packet_msg.vqi != r->completion_vqi &&
	      msg.packet_msg.vqi != r->error_vqi)) ||
	    pa != seg->pa)
		return -EIO;

	rte_rmb();   /* 只读方向的顺序保证；置 completed 用 RELEASE 原子写 */
	return 0;
}

/* Read the engine write-back and copy results into the op. Returns 0 on
 * success, -EBADMSG on authentication failure, -EIO otherwise. */
int
hns5_sec_extract_result(struct hns5_sec_segment *seg,
			struct hns5_session *sess, struct rte_crypto_op *op)
{
	struct hidrv_mbuf *m = (struct hidrv_mbuf *)(void *)
		(seg->raw + HNS5_SEC_MBUF_OFFSET);
	uint64_t t_start = hns5_sec_stats_en ? hns5_tsc() : 0;
	uint64_t t_mark;
	bool is_cipher = (sess->op_type == 0);
	bool is_auth = (sess->op_type == 1);
	bool is_chain = (sess->op_type == 2 || sess->op_type == 3);

	{
		static uint64_t refresh_failed;
		uint64_t t0 = hns5_sec_stats_en ? hns5_tsc() : 0;
		uint64_t dt;
		int refresh_rc;

		/*
		 * The BSP can fail this refresh and returns an error code. The
		 * fields read afterwards are validated (done/sec_result), so a
		 * failure is not fatal here - but it must not be silent either:
		 * "engine did not complete the op" caused by a failed refresh is
		 * a different root cause than a dead engine.
		 */
		refresh_rc = hidrv_sec_poe_reflesh_mbuf_result(m);
		if (refresh_rc != 0)
			HNS5_LOG_LIMITED(WARNING, &refresh_failed, 3, 1000,
					 "mbuf result refresh failed (%d); the "
					 "result fields may be stale",
					 refresh_rc);
		dt = hns5_sec_stats_en ? hns5_tsc() - t0 : 0;
		if (hns5_sec_stats_en && seg->rt != NULL) {
			seg->rt->ref_calls++;
			seg->rt->ref_cycles += dt;
			if (dt > seg->rt->ref_max_cycles)
				seg->rt->ref_max_cycles = dt;
		}
	}

	/* One-shot trace of the engine write-back status: tells whether the engine
	 * really ran (done/flag/sec_result) or the descriptor was ignored. */
	{
		static bool dumped;

		if (!dumped) {
			dumped = true;
			HNS5_LOG(DEBUG, "wb: done=%u flag=0x%x sec_result=%u err=%u warn=%u icv=%u csc=%u\n",
				 m->done, m->flag, m->sec_result, m->err_type,
				 m->warning_type, m->icv, m->csc);
		}
	}

	if (m->done != 1) {
		static unsigned int not_done_logged;

		if (not_done_logged++ < 3)
			HNS5_LOG(ERR, "engine did not complete the op (done=%u) "
				 "op_type=%u len=%u\n", m->done, sess->op_type,
				 (unsigned int)seg->used);
		return -EIO;
	}

	if (m->sec_result || m->err_type || m->warning_type || m->icv >= 2 ||
	    m->csc == 2) {
		static unsigned int fail_logged;

		if (fail_logged++ < 3)
			HNS5_LOG(ERR, "engine failure: sec_result=%u err_type=%u "
				 "flag=0x%x icv=%u csc=%u warn=%u op_type=%u len=%u\n",
				 m->sec_result, m->err_type, m->flag, m->icv, m->csc,
				 m->warning_type, sess->op_type,
				 (unsigned int)seg->used);
		return -EIO;
	}

	t_mark = hns5_sec_stats_en ? hns5_tsc() : 0;
	if (hns5_sec_stats_en && seg->rt != NULL)
		seg->rt->ext_parse_cycles += t_mark - t_start;

	if (is_auth || is_chain) {
		uint16_t dlen = sess->digest_len;
		uint8_t *mac_in_ddr = seg->raw + HNS5_SEC_MAC_OFFSET;

		if (sess->auth_op == RTE_CRYPTO_AUTH_OP_GENERATE) {
			/*
			 * dst carries no length of its own: rte_crypto_sym_op's
			 * digest is a va/iova pair only, and rte_crypto_sym.h
			 * states the contract - "the caller must allocate at
			 * least digest_length of physically contiguous memory at
			 * this location". dlen is the session's digest_len,
			 * pinned to the advertised size at session creation, so
			 * the copy is bounded by what the application promised.
			 */
			uint8_t *dst = op->sym->auth.digest.data;

			if (dst != NULL && dlen > 0 &&
			    dlen <= HNS5_SEC_MAX_DIGEST_BYTES)
				rte_memcpy(dst, mac_in_ddr, dlen);
		} else {
			/* Verify: compare engine-generated MAC with the
			 * digest provided by the application. dlen is the
			 * session's digest length, pinned to the advertised
			 * size at session creation; how much of
			 * op->sym->auth.digest.data is readable is the
			 * application's contract (DPDK: the operation's
			 * private area must hold digest_len bytes), so bound
			 * the compare by that as well.
			 */
			uint8_t *expected = op->sym->auth.digest.data;

			if (expected != NULL && dlen > 0) {
				if (memcmp(mac_in_ddr, expected, dlen) != 0) {
					/*
					 * Counted, not printed. Neither MAC goes
					 * into a log: the caller reports
					 * RTE_CRYPTO_OP_STATUS_AUTH_FAILED, bumps
					 * dequeue_err_count, and the per-channel
					 * total is printed at close, so the failure
					 * is visible without leaking key-derived
					 * bytes.
					 */
					if (seg->rt != NULL)
						seg->rt->auth_mismatch++;
					return -EBADMSG;
				}
			}
		}
	}

	if (hns5_sec_stats_en && seg->rt != NULL)
		seg->rt->ext_digest_cycles += hns5_tsc() - t_mark;
	t_mark = hns5_sec_stats_en ? hns5_tsc() : 0;

	if ((is_cipher || is_chain) && !seg->zero_copy && !seg->zero_copy_out &&
	    !seg->zero_copy_in) {
		static uint64_t not_in_range;
		struct rte_mbuf *mdst = op->sym->m_dst ? op->sym->m_dst :
				  op->sym->m_src;
		uint32_t off = op->sym->cipher.data.offset;
		uint32_t len = op->sym->cipher.data.length;
		uint32_t room;
		unsigned char *src = seg->raw + HNS5_SEC_DATA_OFFSET +
				     HNS5_SEC_PREFIX_BYTES;

		/*
		 * off and len come from the application's xform. Bound them
		 * against the destination chain with subtractions only (adding
		 * two caller-controlled u32 can wrap), and refuse the copy
		 * instead of writing past the last segment: the caller reports
		 * a non-zero return as a failed op.
		 */
		if (mdst == NULL)
			return -EINVAL;
		room = rte_pktmbuf_pkt_len(mdst);
		if (len > HNS5_SEC_SEGMENT_BYTES || off > room ||
		    len > room - off) {
			HNS5_LOG_LIMITED(ERR, &not_in_range, 3, 1000,
					 "cipher range %u+%u does not fit the "
					 "destination (%u bytes)", off, len, room);
			return -EINVAL;
		}
		{
			uint32_t copied = 0;
			uint32_t skip = off;
			struct rte_mbuf *cur = mdst;

			while (cur != NULL && copied < len) {
				uint32_t seg_len = rte_pktmbuf_data_len(cur);
				uint32_t seg_off, cap, take;

				/* Step over whole segments the range starts
				 * beyond, so seg_off < seg_len always holds and
				 * cap cannot underflow. */
				if (skip >= seg_len) {
					skip -= seg_len;
					cur = cur->next;
					continue;
				}
				seg_off = skip;
				skip = 0;
				cap = seg_len - seg_off;
				take = (cap < len - copied) ? cap : (len - copied);
				rte_memcpy(rte_pktmbuf_mtod_offset(cur, void *, seg_off),
					   src + copied, take);
				copied += take;
				cur = cur->next;
			}
			if (copied != len)
				return -EINVAL;
		}
	}

	if (hns5_sec_stats_en && seg->rt != NULL) {
		seg->rt->ext_copy_cycles += hns5_tsc() - t_mark;
		seg->rt->ext_calls++;
	}

	if (!(m->flag & (is_cipher ? 2u : 1u)))
		return -EIO;
	return 0;
}
