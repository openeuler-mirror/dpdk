/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2026 Huawei Technologies Co., Ltd
 */

#include "hns5_crypto_pmd_private.h"

/* 报文内容 dump 开关：0=关闭（默认），仅调试定位时由 debug_dump=1 打开。 */
int hns5_sec_dump_enabled;

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
	if ((size_t)len > sizeof(buf) - 2)
		len = (int)(sizeof(buf) - 2);
	if (n > first) {
		/* Say that messages are being dropped between the printed ones. */
		len += snprintf(buf + len, sizeof(buf) - (size_t)len,
				" [occurrence #%" PRIu64 "]", n);
	}
	if (len > 0 && buf[len - 1] != '\n')
		buf[len++] = '\n';
	buf[len] = '\0';
	rte_log(level, hns5_logtype, "%s", buf);
}
