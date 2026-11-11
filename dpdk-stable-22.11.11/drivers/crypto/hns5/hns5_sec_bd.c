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
#include "hns5_sec_debug.h"

#include <rte_memcpy.h>
#include <rte_crypto_sym.h>


static int
copy_in_data(struct hns5_sec_segment *seg, struct rte_crypto_op *op,
	     bool is_cipher, size_t *msg_len)
{
	unsigned char *dst = seg->raw + HNS5_SEC_DATA_OFFSET + HNS5_SEC_PREFIX_BYTES;
	struct rte_mbuf *msrc;
	uint32_t off, len, copied = 0;
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

	for (i = 0; i < msrc->nb_segs && copied < len; i++) {
		struct rte_mbuf *seg_cur = (i == 0) ? msrc : msrc->next;
		uint32_t seg_off = (i == 0) ? off : 0;
		uint32_t seg_len = rte_pktmbuf_data_len(seg_cur);
		uint32_t take;

		if (seg_cur == NULL)
			return -EINVAL;
		if (seg_off >= seg_len)
			continue;
		take = seg_len - seg_off;
		if (take > len - copied)
			take = len - copied;
		rte_memcpy(dst + copied,
			   rte_pktmbuf_mtod_offset(seg_cur, void *, seg_off), take);
		copied += take;
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
		uint8_t *iv = rte_crypto_op_ctod_offset(op, uint8_t *,
							sess->iv_offset);
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

	/* One-shot dump of the descriptor that is handed to the engine: when a
	 * packet comes back unmodified (or with a zero digest) this shows whether
	 * the transformation was requested at all. */
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
			{
				const struct rte_mbuf *ms = op->sym->m_src;
				uint32_t off = op->sym->cipher.data.offset;
				uint32_t dump_len = (uint32_t)msg_len;
				const void *mb = rte_pktmbuf_mtod_offset(ms,
					const void *, off);

				hns5_sec_dump_hex("SEC in (mbuf)", mb, dump_len);
			}
			{
				const struct rte_mbuf *ms = op->sym->m_src;
				const uint32_t *mi = (const uint32_t *)(const void *)
					rte_pktmbuf_mtod_offset(ms, const char *,
								op->sym->cipher.data.offset);

				HNS5_LOG(DEBUG, "bd: mbuf in [%u..+16)=%08x %08x %08x %08x\n",
					 op->sym->cipher.data.offset,
					 mi[0], mi[1], mi[2], mi[3]);
			}
			{
				const uint32_t *iv_area = (const uint32_t *)(const void *)(seg->raw + HNS5_SEC_DATA_OFFSET);
				const uint32_t *src = (const uint32_t *)(const void *)(seg->raw + HNS5_SEC_DATA_OFFSET + HNS5_SEC_PREFIX_BYTES);

				HNS5_LOG(DEBUG, "bd: iv[0..4)  =%08x %08x %08x %08x\n",
					 iv_area[0], iv_area[1], iv_area[2], iv_area[3]);
				HNS5_LOG(DEBUG, "bd: data[0..4)=%08x %08x %08x %08x (input before engine)\n",
					 src[0], src[1], src[2], src[3]);
			}
			HNS5_LOG(DEBUG, "bd: in_off=%u authlen=%u mac_len=%u\n",
				 alg->cipher_data_in_offset,
				 sess->digest_len, alg->auth_alg.bits.mac_len);
		}
	}

	return 0;
}
