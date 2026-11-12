/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2026 Huawei Technologies Co., Ltd
 *
 * DPDK sym session handling. The SEC engine is sessionless at the HW level
 * (keys/IVs/MAC are supplied per descriptor), so the session only holds the
 * software state needed to build a descriptor: algorithm/mode identifiers,
 * key length and the raw key.
 */

#include "hns5_crypto_pmd_private.h"
#include <rte_crypto_sym.h>
#include <rte_ether.h>

static int
cipher_setup(struct hns5_session *sess, const struct rte_crypto_cipher_xform *x)
{
	switch (x->algo) {
	case RTE_CRYPTO_CIPHER_SM4_CBC:
		sess->cipher_alg = HNS5_SEC_CIPHER_ALG_SM4;
		sess->cipher_mode = HNS5_SEC_CIPHER_MODE_CBC;
		break;
	case RTE_CRYPTO_CIPHER_AES_CBC:
		sess->cipher_alg = HNS5_SEC_CIPHER_ALG_AES;
		sess->cipher_mode = HNS5_SEC_CIPHER_MODE_CBC;
		break;
	case RTE_CRYPTO_CIPHER_AES_ECB:
		/*
		 * The engine implements ECB, but it is not semantically secure:
		 * equal plaintext blocks give equal ciphertext, so patterns in
		 * the data survive encryption. It is therefore not offered at
		 * all - no capability entry (see hns5_crypto_capabilities.c) and
		 * refused here, instead of being logged and then executed.
		 */
		HNS5_LOG(ERR, "AES-ECB is not offered by this driver "
			 "(not semantically secure)");
		return -ENOTSUP;
	default:
		HNS5_LOG(ERR, "unsupported cipher algo %u", x->algo);
		return -ENOTSUP;
	}
	/*
	 * Key lengths must match what the capabilities advertise and what the
	 * engine can do: SM4 is 128-bit only, AES accepts 128/192/256.
	 */
	if (sess->cipher_alg == HNS5_SEC_CIPHER_ALG_SM4) {
		if (x->key.length != 16) {
			HNS5_LOG(ERR, "SM4 needs a 16-byte key, got %u",
				 x->key.length);
			return -EINVAL;
		}
	} else if (x->key.length != 16 && x->key.length != 24 &&
		   x->key.length != 32) {
		HNS5_LOG(ERR, "bad cipher key length %u", x->key.length);
		return -EINVAL;
	}
	sess->cipher_keylen = (uint8_t)x->key.length;
	sess->cipher_dir = (uint8_t)x->op;
	/*
	 * The IV is copied verbatim into the SEC segment, so its length has to
	 * be what the capability table advertises: one cipher block for the CBC
	 * modes. ECB ignores it, but a caller may still fill one in, so only the
	 * upper bound applies there. A longer IV would read past the operation's
	 * private area and run over the key and MAC areas of the segment.
	 */
	if (x->iv.length > HNS5_SEC_IV_BYTES ||
	    (sess->cipher_mode != HNS5_SEC_CIPHER_MODE_ECB &&
	     x->iv.length != HNS5_SEC_IV_BYTES)) {
		HNS5_LOG(ERR, "cipher IV length %u not allowed for mode %u "
			 "(block size %u)", x->iv.length, sess->cipher_mode,
			 HNS5_SEC_IV_BYTES);
		return -EINVAL;
	}
	sess->iv_offset = (uint16_t)x->iv.offset;
	sess->iv_length = (uint16_t)x->iv.length;
	if (x->key.data != NULL)
		rte_memcpy(sess->cipher_key, x->key.data, x->key.length);
	return 0;
}

static int
auth_setup(struct hns5_session *sess, const struct rte_crypto_auth_xform *x)
{
	bool hmac;

	/*
	 * The xform type decides how the engine is programmed. A plain digest
	 * algorithm must not carry a key: the capabilities advertise key_size 0
	 * for those, and silently turning them into HMAC would contradict the
	 * negotiated capabilities.
	 */
	switch (x->algo) {
	case RTE_CRYPTO_AUTH_SM3:
		if (x->key.length != 0) {
			HNS5_LOG(ERR, "SM3 takes no key, got %u bytes",
				 x->key.length);
			return -EINVAL;
		}
		sess->auth_alg = HNS5_SEC_AUTH_SM3;
		hmac = false;
		break;
	case RTE_CRYPTO_AUTH_SM3_HMAC:
		sess->auth_alg = HNS5_SEC_AUTH_HMAC_SM3;
		hmac = true;
		break;
	case RTE_CRYPTO_AUTH_SHA256:
		if (x->key.length != 0) {
			HNS5_LOG(ERR, "SHA256 takes no key, got %u bytes "
				 "(use SHA256-HMAC)", x->key.length);
			return -EINVAL;
		}
		sess->auth_alg = HNS5_SEC_AUTH_SHA_256;
		hmac = false;
		break;
	case RTE_CRYPTO_AUTH_SHA256_HMAC:
		sess->auth_alg = HNS5_SEC_AUTH_HMAC_SHA256;
		hmac = true;
		break;
	default:
		HNS5_LOG(ERR, "unsupported auth algo %u", x->algo);
		return -ENOTSUP;
	}
	/*
	 * The engine takes the HMAC key length in 32-bit words, so a length that
	 * is not a multiple of 4 would be silently truncated; 16 bytes is the
	 * 128-bit floor. Both limits match what the capabilities advertise.
	 */
	if (hmac) {
		if (x->key.length < 16 || x->key.length > sizeof(sess->auth_key) ||
		    (x->key.length % 4) != 0) {
			HNS5_LOG(ERR, "bad HMAC key length %u (need 16..64, "
				 "multiple of 4)", x->key.length);
			return -EINVAL;
		}
		if (x->key.data == NULL) {
			HNS5_LOG(ERR, "HMAC needs a %u-byte key, but no key "
				 "pointer was given", x->key.length);
			return -EINVAL;
		}
		sess->akey_len = (uint16_t)x->key.length;
		rte_memcpy(sess->auth_key, x->key.data, x->key.length);
	}
	/*
	 * The engine always writes the full 32-byte digest of SM3/SHA-256, and the
	 * completion path copies at most that many bytes, so a larger request can
	 * never be satisfied. Reject it here instead of returning uninitialised
	 * bytes to the application.
	 */
	if (x->digest_length == 0 ||
	    x->digest_length > HNS5_SEC_MAX_DIGEST_BYTES) {
		HNS5_LOG(ERR, "bad digest length %u (max %u)", x->digest_length,
			 (unsigned int)HNS5_SEC_MAX_DIGEST_BYTES);
		return -EINVAL;
	}
	sess->auth_op = (uint8_t)x->op;
	sess->digest_len = (uint16_t)x->digest_length;
	return 0;
}

int
hns5_session_configure(struct rte_crypto_sym_xform *xform, void *priv)
{
	struct hns5_session *sess = priv;
	struct rte_crypto_sym_xform *xf = xform;

	if (sess == NULL || xf == NULL)
		return -EINVAL;
	memset(sess, 0, sizeof(*sess));

	if (xf->type == RTE_CRYPTO_SYM_XFORM_CIPHER && xf->next == NULL) {
		sess->op_type = 0;
		return cipher_setup(sess, &xf->cipher);
	}
	if (xf->type == RTE_CRYPTO_SYM_XFORM_AUTH && xf->next == NULL) {
		sess->op_type = 1;
		return auth_setup(sess, &xf->auth);
	}
	/* Chain: cipher-then-auth. */
	if (xf->type == RTE_CRYPTO_SYM_XFORM_CIPHER && xf->next != NULL &&
	    xf->next->type == RTE_CRYPTO_SYM_XFORM_AUTH) {
		int rc;

		sess->op_type = 2;
		rc = cipher_setup(sess, &xf->cipher);
		if (rc)
			return rc;
		return auth_setup(sess, &xf->next->auth);
	}
	/* Chain: auth-then-cipher (decrypt+verify path). */
	if (xf->type == RTE_CRYPTO_SYM_XFORM_AUTH && xf->next != NULL &&
	    xf->next->type == RTE_CRYPTO_SYM_XFORM_CIPHER) {
		int rc;

		sess->op_type = 3;
		rc = cipher_setup(sess, &xf->next->cipher);
		if (rc)
			return rc;
		return auth_setup(sess, &xf->auth);
	}
	HNS5_LOG(ERR, "unsupported xform chain");
	return -ENOTSUP;
}

/*
 * The compiler may drop a memset() of an object that is not used again, which
 * is exactly the case here (the session private area is recycled, not read).
 * Writing through a volatile pointer keeps the clear: keys and IV must not
 * survive in memory the pool hands to the next session.
 */
static void
hns5_wipe(void *p, size_t len)
{
	volatile unsigned char *v = (volatile unsigned char *)p;

	while (len-- != 0)
		*v++ = 0;
}

void
hns5_session_clear(void *priv)
{
	if (priv != NULL)
		hns5_wipe(priv, sizeof(struct hns5_session));
}
