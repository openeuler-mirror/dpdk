/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2026 Huawei Technologies Co., Ltd
 *
 * 128-byte SEC descriptor builder. Mirrors the reference engine's build():
 * the key, IV and message are copied into a single BMU-backed segment (the
 * first segment of the chain; longer messages would chain segments, but the
 * initial DPDK path supports a single segment for messages up to one SEC
 * segment). The SEC engine performs the key schedule on the origin key, so
 * no persistent HW session is needed.
 */

#include "hns5_crypto_pmd_private.h"

#include <rte_memcpy.h>
#include <rte_mempool.h>
#include <rte_crypto_sym.h>

/*
 * Ops that were not allocated from a crypto op pool carry no record of how
 * big their private area is; bound iv_offset against this instead of failing
 * them outright.
 */
#define HNS5_SEC_OP_PRIV_FALLBACK 1024u


static int
copy_in_data(struct hns5_sec_segment *seg, struct rte_crypto_op *op,
	     bool is_cipher, size_t *msg_len)
{
	unsigned char *dst = seg->raw + HNS5_SEC_DATA_OFFSET + HNS5_SEC_PREFIX_BYTES;
	struct rte_mbuf *msrc, *seg_cur;
	uint32_t off, len, copied = 0, skip;
	unsigned int i;

	if (is_cipher) {
		off = op->sym->cipher.data.offset;
		len = op->sym->cipher.data.length;
	} else {
		off = op->sym->auth.data.offset;
		len = op->sym->auth.data.length;
	}
	*msg_len = len;

	if (len > HNS5_SEC_SEGMENT_BYTES)
		return -ENOSPC;

	msrc = op->sym->m_src;
	if (msrc == NULL)
		return -EINVAL;

	/*
	 * Walk the chain through ->next: indexing msrc->next for every i after
	 * the first repeats segment 2 and never reaches segment 3. nb_segs comes
	 * from the caller, so the pointer is checked before it is dereferenced.
	 * skip steps over whole segments, so the start offset may land anywhere
	 * in the chain; a chain shorter than nb_segs still ends in the
	 * copied != len check below.
	 */
	for (seg_cur = msrc, skip = off, i = 0;
	     i < msrc->nb_segs && copied < len && seg_cur != NULL;
	     seg_cur = seg_cur->next, i++) {
		uint32_t seg_len = rte_pktmbuf_data_len(seg_cur);
		uint32_t take;

		if (skip >= seg_len) {
			skip -= seg_len;
			continue;
		}
		take = seg_len - skip;
		if (take > len - copied)
			take = len - copied;
		rte_memcpy(dst + copied,
			   rte_pktmbuf_mtod_offset(seg_cur, void *, skip), take);
		copied += take;
		skip = 0;
	}
	if (copied != len)
		return -EINVAL;
	return 0;
}

int
hns5_sec_build_descriptor(struct hns5_sec_runtime *r,
			  struct hns5_session *sess,
			  struct rte_crypto_op *op,
			  struct hns5_sec_segment *seg,
			  sec_alg_s *alg)
{
	size_t msg_len = 0;
	bool is_cipher = (sess->op_type == 0);
	bool is_auth = (sess->op_type == 1);
	bool is_chain = (sess->op_type == 2 || sess->op_type == 3);
	int rc;

	RTE_SET_USED(r);

	/* seg->raw and seg->pa are set by the caller (enqueue). */
	seg->used = 0;

	if (seg->zero_copy || seg->zero_copy_in) {
		/* Data is read from the mbuf: only the length is needed here
		 * (the addresses were computed by the caller). */
		msg_len = (is_cipher || is_chain) ?
			  op->sym->cipher.data.length : op->sym->auth.data.length;
	} else {
		rc = copy_in_data(seg, op, is_cipher || is_chain, &msg_len);
		if (rc)
			return rc;
	}

	/*
	 * Block ciphers (ECB/CBC) can only process whole blocks. The engine does
	 * not validate this itself: it completes with sec_result=1, err_type=5,
	 * flag=0 and leaves the buffer untouched, which downstream looks like
	 * "the packet was not encrypted" with a zero digest. Reject such ops here
	 * so the caller gets a clear reason (SM4 block size is 16 bytes).
	 */
	if ((is_cipher || is_chain) &&
	    (sess->cipher_mode == HNS5_SEC_CIPHER_MODE_ECB ||
	     sess->cipher_mode == HNS5_SEC_CIPHER_MODE_CBC) &&
	    (msg_len % HNS5_SEC_CIPHER_BLOCK_BYTES) != 0) {
		static unsigned int warned;

		if (warned++ < 4) {
			HNS5_LOG(ERR, "cipher len %zu is not a multiple of %u bytes "
				 "(mode=%u); SEC would fail with err_type=5 and leave "
				 "the packet unencrypted\n", msg_len,
				 HNS5_SEC_CIPHER_BLOCK_BYTES, sess->cipher_mode);
		}
		return -EINVAL;
	}

	/*
	 * The compact 16 KiB buffer holds the descriptor, the input copy and the
	 * keys: larger payloads would overwrite the key slots at the tail.
	 */
	if (msg_len == 0 || msg_len > HNS5_SEC_MAX_PAYLOAD) {
		if (seg->rt != NULL)
			HNS5_LOG_LIMITED(ERR, &seg->rt->send_fail, 5, 1000,
					 "payload %zu exceeds the %u byte buffer "
					 "limit", msg_len,
					 (unsigned int)HNS5_SEC_MAX_PAYLOAD);
		return -EMSGSIZE;
	}

	/* Build the mbuf header the SEC POE path expects. Single-segment
	 * chain: this segment is both head and tail.
	 * PPE BD size is 64 bytes; data_len must be 64-aligned or the PPE
	 * cannot parse the packet and SEC never completes. */
	{
		struct hidrv_mbuf *m = (struct hidrv_mbuf *)(void *)
			(seg->raw + HNS5_SEC_MBUF_OFFSET);

		if (seg->zero_copy) {
			/*
			 * The data lives in the application mbuf, so the header must
			 * describe THAT buffer: buf_pa/data_offset point at the first
			 * processed byte and pkt_len equals the processed length. A
			 * header still describing the SEC buffer would make the engine
			 * DMA at the wrong address and length.
			 */
			struct rte_mbuf *mb = op->sym->m_src;
			uint32_t off = (uint16_t)(seg->data_pa - rte_mbuf_data_iova(mb));

			m->buf_addr = rte_pktmbuf_mtod_offset(mb, void *, off);
			m->buf_pa = seg->data_pa;
			m->data_offset = 0;
			m->ref_cnt = 1;
			m->segs_num = 1;
			m->pkt_len = (uint32_t)msg_len;
			m->data_len = (uint16_t)msg_len;
			m->buf_len = (uint16_t)(rte_pktmbuf_data_len(mb) - off);
			m->next = NULL;
		} else {
			uint32_t total = (uint32_t)(msg_len + HNS5_SEC_PREFIX_BYTES);

			total = (total + 63u) & ~63u;  /* round up to 64 */
			m->buf_addr = seg->raw + HNS5_SEC_BUFFER_OFFSET;
			m->buf_pa = seg->pa + HNS5_SEC_BUFFER_OFFSET;
			m->data_offset = (uint16_t)(HNS5_SEC_DATA_OFFSET -
						    HNS5_SEC_BUFFER_OFFSET);
			m->ref_cnt = 1;
			m->segs_num = 1;
			m->pkt_len = total;
			m->data_len = (uint16_t)total;
			m->buf_len = HNS5_SEC_MAC_OFFSET - HNS5_SEC_BUFFER_OFFSET;
			m->next = NULL;
		}
	}

	/* Copy cipher key to the key area (SEC does the key schedule on the
	 * origin key). In chain mode this stays separate from the auth key. */
	if ((is_cipher || is_chain) && sess->cipher_keylen)
		rte_memcpy(seg->raw + HNS5_SEC_KEY_OFFSET, sess->cipher_key,
			   sess->cipher_keylen);
	/* IV for non-ECB cipher modes sits at the data offset head. */
	if ((is_cipher || is_chain) && sess->cipher_mode != HNS5_SEC_CIPHER_MODE_ECB) {
		/*
		 * iv_offset is an absolute offset from the start of the crypto op
		 * (rte_crypto_op_ctod_offset() computes (char *)op + offset), and
		 * rte_crypto_op_pool_create() lays each element out as
		 *   [rte_crypto_op][rte_crypto_sym_op][private area: priv_size].
		 * The private area therefore spans [priv_base, priv_base+priv_size),
		 * so the upper bound is priv_base + priv_size, not priv_size alone.
		 * Comparing an op-absolute offset against the bare priv_size rejects
		 * the standard layout: l2fwd-crypto sets iv.offset to
		 * sizeof(op)+sizeof(sym_op) with priv_size = the IV length, which
		 * fits the private area exactly.
		 */
		uint32_t priv_base = (uint32_t)(sizeof(struct rte_crypto_op) +
					    sizeof(struct rte_crypto_sym_op));
		uint32_t priv_avail = priv_base + HNS5_SEC_OP_PRIV_FALLBACK;
		uint8_t *iv;

		if (op->mempool != NULL) {
			const struct rte_crypto_op_pool_private *pi =
				(const struct rte_crypto_op_pool_private *)
				rte_mempool_get_priv(op->mempool);

			/* Only a pool created by rte_crypto_op_pool_create()
			 * carries this layout at the mempool private offset;
			 * ops handed over from a plain mempool would make
			 * priv_size meaningless, so keep the fallback. */
			if (pi->type == RTE_CRYPTO_OP_TYPE_SYMMETRIC ||
			    pi->type == RTE_CRYPTO_OP_TYPE_UNDEFINED)
				priv_avail = priv_base + pi->priv_size;
		}
		if ((uint32_t)sess->iv_offset < priv_base ||
		    (uint32_t)sess->iv_offset + (uint32_t)sess->iv_length >
		    priv_avail) {
			HNS5_LOG(ERR, "iv_offset %u + iv_length %u outside the op "
				 "private area [%u, %u)", sess->iv_offset,
				 sess->iv_length, priv_base, priv_avail);
			return -EINVAL;
		}
		iv = rte_crypto_op_ctod_offset(op, uint8_t *, sess->iv_offset);
		rte_memcpy(seg->raw + HNS5_SEC_DATA_OFFSET, iv, sess->iv_length);
	}
	/* Copy HMAC key to the auth key area, separate from the cipher key.
	 * This is critical for chain mode where both keys coexist. */
	if ((is_auth || is_chain) && sess->akey_len &&
	    (sess->auth_alg == HNS5_SEC_AUTH_HMAC_SM3 ||
	     sess->auth_alg == HNS5_SEC_AUTH_HMAC_SHA256))
		rte_memcpy(seg->raw + HNS5_SEC_AUTH_KEY_OFFSET, sess->auth_key,
			   sess->akey_len);

	memset(alg, 0, sizeof(*alg));
	alg->scene = HIDRV_SEC_SCENE_IPSEC;
	alg->addr_type = HIDRV_SEC_CHAIN_BUFF;
	if (seg->zero_copy || seg->zero_copy_out || seg->zero_copy_in) {
		/* Input: the mbuf when zero_copy_in/zero_copy, otherwise the
		 * SEC buffer. The prefix skip only applies to the latter. */
		bool in_mbuf = seg->zero_copy || seg->zero_copy_in;

		alg->cipher_data_in_addr = in_mbuf ? seg->data_pa :
					   seg->pa + HNS5_SEC_DATA_OFFSET;
		alg->auth_data_in_addr = alg->cipher_data_in_addr;
		alg->cipher_data_in_offset = in_mbuf ? 0 : HNS5_SEC_PREFIX_BYTES;
		alg->auth_data_in_offset = alg->cipher_data_in_offset;
	} else {
		alg->cipher_data_in_addr = seg->pa + HNS5_SEC_DATA_OFFSET;
		alg->auth_data_in_addr = seg->pa + HNS5_SEC_DATA_OFFSET;
		alg->cipher_data_in_offset = HNS5_SEC_PREFIX_BYTES;
		alg->auth_data_in_offset = HNS5_SEC_PREFIX_BYTES;
	}
	alg->cipher_key_addr = seg->pa + HNS5_SEC_KEY_OFFSET;
	alg->auth_key_addr = seg->pa + HNS5_SEC_AUTH_KEY_OFFSET;
	alg->cipher_iv_in_addr = seg->pa + HNS5_SEC_DATA_OFFSET;
	alg->auth_iv_in_addr = seg->pa + HNS5_SEC_DATA_OFFSET;
	alg->cipher_iv_in_offset = 0;
	alg->cipher_iv_gen = HIDRV_SEC_CI_GEN_FROM_ADDR;
	alg->auth_iv_gen = HIDRV_SEC_AI_GEN_FROM_INNER;

	if (is_cipher || is_chain) {
		if (sess->op_type == 3)
			alg->ac_type = HIDRV_SEC_AUTH_CIPHER;
		else if (is_chain)
			alg->ac_type = HIDRV_SEC_CIPHER_AUTH;
		else
			alg->ac_type = HIDRV_SEC_CIPHER;
		alg->cipher_type = (sess->cipher_dir == RTE_CRYPTO_CIPHER_OP_ENCRYPT)
				   ? HIDRV_SEC_CIPHER_ENCRYPT : HIDRV_SEC_CIPHER_DECRYPT;
		alg->cipher_alg.bits.cipher_alg = sess->cipher_alg;
		alg->cipher_alg.bits.cipher_mode = sess->cipher_mode;
		alg->cipher_alg.bits.cipher_keylen = (sess->cipher_keylen - 16) / 8;
		alg->cipher_gran_sz = (uint32_t)msg_len;
		alg->cipher_data_out_addr = (seg->zero_copy ||
					     seg->zero_copy_out) ?
					    seg->data_pa :
					    seg->pa + HNS5_SEC_DATA_OFFSET;
	}

	if (is_auth || is_chain) {
		alg->auth_alg.bits.auth_alg = sess->auth_alg;
		/* The session layer guarantees a multiple of 4, so this is exact. */
		alg->auth_alg.bits.auth_keylen = sess->akey_len / 4;
		alg->auth_alg.bits.mac_len = 8;
		alg->auth_gran_sz = (uint32_t)msg_len;
		alg->auth_data_out_addr = seg->pa + HNS5_SEC_MAC_OFFSET;
	}

	seg->used = HNS5_SEC_PREFIX_BYTES + msg_len;

	/* Descriptor-level trace, emitted once per process: when a packet comes
	 * back unmodified (or with a zero digest) this shows whether the engine
	 * was asked to do the transformation at all. No buffer contents here. */
	{
		static bool dumped;

		if (!dumped) {
			dumped = true;
			HNS5_LOG(DEBUG, "bd: op_type=%u ac_type=%u gran=%u prefix=%u msg_len=%zu\n",
				 sess->op_type, alg->ac_type, alg->cipher_gran_sz,
				 HNS5_SEC_PREFIX_BYTES, msg_len);
			HNS5_LOG(DEBUG, "bd: cipher dir=%u alg=%u mode=%u keylen=%u type=%u\n",
				 sess->cipher_dir, alg->cipher_alg.bits.cipher_alg,
				 alg->cipher_alg.bits.cipher_mode,
				 alg->cipher_alg.bits.cipher_keylen, alg->cipher_type);
			HNS5_LOG(DEBUG, "bd: in_off=%u authlen=%u mac_len=%u\n",
				 alg->cipher_data_in_offset,
				 sess->digest_len, alg->auth_alg.bits.mac_len);
		}
	}

	return 0;
}
