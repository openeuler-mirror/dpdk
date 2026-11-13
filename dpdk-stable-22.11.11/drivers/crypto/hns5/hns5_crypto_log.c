/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2026 Huawei Technologies Co., Ltd
 */

#include "hns5_crypto_pmd_private.h"

RTE_LOG_REGISTER_DEFAULT(hns5_logtype, NOTICE);

#include <stdarg.h>
#include <stdio.h>

/*
 * Write one message and make sure it ends with a newline. Call sites are not
 * consistent about the trailing "\n" and the resulting glued lines made the
 * logs hard to read.
 */
void
hns5_log_write(uint32_t level, const char *fmt, ...)
{
	char buf[2048];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
	va_end(ap);
	if (n < 0)
		return;
	if ((size_t)n > sizeof(buf) - 2)
		n = (int)(sizeof(buf) - 2);
	if (n == 0 || buf[n - 1] != '\n') {
		buf[n++] = '\n';
		buf[n] = '\0';
	}
	rte_log(level, hns5_logtype, "%s", buf);
}

/*
 * Log the first @first occurrences, then only every @every-th one, and report
 * the suppressed count. Keeps burst failures visible without flooding the
 * console: the caller keeps the total in *cnt and can print it at teardown.
 */
void
hns5_log_limited(uint32_t level, uint64_t *cnt, uint64_t first, uint64_t every,
		 const char *fmt, ...)
{
	char buf[1024];
	va_list ap;
	uint64_t n;
	int len;

	if (cnt == NULL)
		return;
	n = ++(*cnt);
	if (n > first && (every == 0 || (n % every) != 0))
		return;

	va_start(ap, fmt);
	len = vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
	va_end(ap);
	if (len < 0)
		return;
	/*
	 * vsnprintf() and snprintf() report the length they *would* have
	 * written, not what is in the buffer, so their return values must never
	 * be summed into an index. Leave one slot for '\n' and one for the NUL
	 * and clamp after every append.
	 */
	if (len > (int)sizeof(buf) - 3)
		len = (int)sizeof(buf) - 3;
	if (n > first) {
		/* Say that messages are being dropped between the printed ones. */
		size_t space = sizeof(buf) - 2 - (size_t)len;
		int add;

		if (space > 1) {
			add = snprintf(buf + len, space,
				       " [occurrence #%" PRIu64 "]", n);
			if (add > 0)
				len += (add < (int)space) ? add : (int)space - 1;
		}
	}
	if (len > 0 && buf[len - 1] != '\n')
		buf[len++] = '\n';
	buf[len] = '\0';
	rte_log(level, hns5_logtype, "%s", buf);
}

/*
 * Copy a caller-controlled string into a log-safe form: anything outside
 * printable ASCII (in particular \r and \n, which would forge extra log
 * lines) becomes '?', and the result is always NUL terminated within
 * dst_len. Used for devarg values and for names the application chooses
 * (mempool names), neither of which the driver validates character-wise.
 */
void
hns5_str_shown(char *dst, size_t dst_len, const char *src)
{
	size_t i = 0;

	if (dst == NULL || dst_len == 0)
		return;
	while (i + 1 < dst_len && src != NULL && src[i] != '\0') {
		unsigned char c = (unsigned char)src[i];

		dst[i] = (c >= 0x20 && c < 0x7f) ? (char)c : '?';
		i++;
	}
	dst[i] = '\0';
}
