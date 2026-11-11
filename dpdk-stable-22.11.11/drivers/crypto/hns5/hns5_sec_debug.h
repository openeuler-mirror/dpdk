/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2026 Huawei Technologies Co., Ltd
 *
 * 分阶段报文内容 dump，仅用于定位'加解密结果不符'类问题：
 * 打印最多 160 字节，每行 16 字节，NOTICE 级别（默认可见）。
 */

#ifndef _HNS5_SEC_DEBUG_H_
#define _HNS5_SEC_DEBUG_H_

#include <stdio.h>

#include "hns5_crypto_pmd_private.h"

#define HNS5_SEC_DUMP_BYTES 160u

/*
 * Packet-content dumps are OFF by default: they print前 160 字节的明文/密文,
 * 一旦默认开启，业务明文就会落进日志。仅当显式传入 debug_dump=1 时才输出，
 * 供定位"加解密结果不符"这类问题，用完必须关掉。
 */
extern int hns5_sec_dump_enabled;

static inline void hns5_sec_dump_hex(const char *tag, const void *data,
				     uint32_t len)
{
	const uint8_t *p = (const uint8_t *)data;
	char buf[1024];
	int n = 0;
	uint32_t off;

	if (p == NULL || hns5_sec_dump_enabled == 0) {
		return;
	}

	n += snprintf(buf + n, sizeof(buf) - n, "%s len=%u", tag, len);
	for (off = 0; off < len && off < HNS5_SEC_DUMP_BYTES &&
		      n < (int)sizeof(buf) - 64; off += 16) {
		uint32_t i;

		n += snprintf(buf + n, sizeof(buf) - n, "\n  %04x:", off);
		for (i = 0; i < 16 && off + i < len &&
			    n < (int)sizeof(buf) - 8; i++)
			n += snprintf(buf + n, sizeof(buf) - n, " %02x",
				      p[off + i]);
	}
	HNS5_LOG(NOTICE, "%s\n", buf);
}

#endif /* _HNS5_SEC_DEBUG_H_ */
