/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2025-2026 Huawei Technologies Co., Ltd
 * Unit tests for hinic3_pmd_bifur.c
 *
 * Tests pure-logic static functions via local copies (same pattern as
 * test_hinic3_ethdev.c). Functions involving filesystem/chardev I/O
 * are not tested except for hinic3_parse_sysfs_value which uses temp files.
 */

#include <rte_common.h>
#include <rte_pci.h>
#include <rte_bus_pci.h>
#include <rte_debug.h>
#include <rte_malloc.h>
#include <rte_eal.h>
#include <rte_test.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "../test.h"

#include "base/hinic3_compat.h"
#include "hinic3_pmd_bifur.h"

#ifndef DPDK_21_11
#define RTE_PCI_ANY_ID        (0xffff)
#endif

#ifndef DPDK_20_11
#include <rte_string_fns.h>
#endif

/* ====== Private types/macros from hinic3_pmd_bifur.c (duplicated for testing) ====== */

#ifndef PCI_DBDF
#define PCI_DBDF(dom, bus, dev, func) \
	(((u32)(dom) << 16) | ((u32)(bus) << 8) | ((u32)(dev) << 3) | ((u32)(func) & 0x7))
#endif

#define PCI_DEVID(bus, dev) (((bus) << 5) | ((dev) & 0x1f))

#define MAX_PATH_LEN   64
#define PCI_SYS_PATH   "/sys/bus/pci/devices/"

enum bifur_cmd_m {
	BIFUR_DRV_CMD_FUNC_ATTR_GET = 1,
	BIFUR_DRV_CMD_VF_ALLOC,
	BIFUR_DRV_CMD_MAC_GET,
	BIFUR_DRV_CMD_MAX
};

struct bifur_msg {
	u32 drv_cmd;
	u32 in_buf_len;
	u32 out_buf_len;
	u32 out_data_len;
	void *in_buf;
	void *out_buf;
	u8 rsvd[24];
};

enum BIFUR_INIT_STATE {
	BIFUR_UNINIT = 0,
	BIFUR_INIT_SUCCEED,
	BIFUR_INIT_FAILED,
};

struct bifur_dev_pair_local {
	TAILQ_ENTRY(bifur_dev_pair_local) entries;
};

TAILQ_HEAD(bifur_dev_pair_list_local, bifur_dev_pair_local);

struct bifur_func_mgr_local {
	int global_func_fd;
};

struct bifur_mgr_local {
	struct bifur_dev_pair_list_local pair_list;
	struct bifur_func_mgr_local func_mgr;
	enum BIFUR_INIT_STATE init_state;
};

struct bifur_mapped_dev_local {
	STAILQ_ENTRY(bifur_mapped_dev_local) entries;
	struct rte_pci_device *pci_dev;
};

STAILQ_HEAD(bifur_mapped_dev_list_local, bifur_mapped_dev_local);

enum mapped_dev_op_local {
	DEV_ADD_LOCAL     = 0x1,
	DEV_DEL_LOCAL     = 0x2,
	DEV_QUERY_LOCAL  = 0x4,
};

/* ====== Local copies of static functions ====== */

static void
bifur_fill_msg_local(struct bifur_msg *msg, enum bifur_cmd_m cmd,
		     void *in_buf, uint32_t in_buf_len,
		     void *out_buf, uint32_t out_buf_len)
{
	msg->drv_cmd = cmd;
	msg->in_buf = in_buf;
	msg->in_buf_len = in_buf_len;
	msg->out_buf = out_buf;
	msg->out_buf_len = out_buf_len;
}

static int
bifur_pci_addr_to_path_local(const struct rte_pci_addr *addr, char *str)
{
	return snprintf(str, MAX_PATH_LEN, "%s%04x:%02x:%02x.%x",
			PCI_SYS_PATH, addr->domain, addr->bus,
			addr->devid, addr->function);
}

static bool
bifur_pci_match_local(const struct rte_pci_driver *pci_drv,
		      const struct rte_pci_device *pci_dev)
{
	const struct rte_pci_id *id_table;

	for (id_table = pci_drv->id_table; id_table->vendor_id != 0; id_table++) {
		if (id_table->vendor_id != pci_dev->id.vendor_id &&
		    id_table->vendor_id != RTE_PCI_ANY_ID)
			continue;
		if (id_table->device_id != pci_dev->id.device_id &&
		    id_table->device_id != RTE_PCI_ANY_ID)
			continue;
		if (id_table->subsystem_vendor_id != pci_dev->id.subsystem_vendor_id &&
		    id_table->subsystem_vendor_id != RTE_PCI_ANY_ID)
			continue;
		if (id_table->subsystem_device_id != pci_dev->id.subsystem_device_id &&
		    id_table->subsystem_device_id != RTE_PCI_ANY_ID)
			continue;
		if (id_table->class_id != pci_dev->id.class_id &&
		    id_table->class_id != RTE_CLASS_ANY_ID)
			continue;
		return true;
	}
	return false;
}

static int
bifur_parse_one_sysfs_resource_local(char *line, size_t len,
				     uint64_t *phys_addr,
				     uint64_t *end_addr,
				     uint64_t *flags)
{
	union pci_resource_info {
		struct {
			char *phys_addr;
			char *end_addr;
			char *flags;
		};
		char *ptrs[PCI_RESOURCE_FMT_NVAL];
	} res_info;

	if (rte_strsplit(line, (int)len, res_info.ptrs, 3, ' ') != 3)
		return -1;
	errno = 0;
	*phys_addr = strtoull(res_info.phys_addr, NULL, 16);
	*end_addr = strtoull(res_info.end_addr, NULL, 16);
	*flags = strtoull(res_info.flags, NULL, 16);
	if (errno != 0)
		return -1;
	return 0;
}

static enum rte_iova_mode
bifur_pci_device_iova_mode_local(const struct rte_pci_driver *pdrv,
				 const struct rte_pci_device *pdev)
{
	enum rte_iova_mode iova_mode = RTE_IOVA_DC;

	switch (pdev->kdrv) {
	case RTE_PCI_KDRV_VFIO: {
#ifdef VFIO_PRESENT
		static int is_vfio_noiommu_enabled = -1;

		if (is_vfio_noiommu_enabled == -1) {
			if (rte_vfio_noiommu_is_enabled() == 1)
				is_vfio_noiommu_enabled = 1;
			else
				is_vfio_noiommu_enabled = 0;
		}
		if (is_vfio_noiommu_enabled != 0)
			iova_mode = RTE_IOVA_PA;
		else if ((pdrv->drv_flags & RTE_PCI_DRV_NEED_IOVA_AS_VA) != 0)
			iova_mode = RTE_IOVA_VA;
#endif
		break;
	}
	case RTE_PCI_KDRV_IGB_UIO:
	case RTE_PCI_KDRV_UIO_GENERIC:
		iova_mode = RTE_IOVA_PA;
		break;
	default:
		if ((pdrv->drv_flags & RTE_PCI_DRV_NEED_IOVA_AS_VA) != 0)
			iova_mode = RTE_IOVA_VA;
		break;
	}
	return iova_mode;
}

static void
try_deinit_mgr_local(struct bifur_mgr_local *mgr)
{
	if (mgr->init_state != BIFUR_INIT_SUCCEED)
		return;
	if (TAILQ_EMPTY(&mgr->pair_list)) {
		int *g_fd = &mgr->func_mgr.global_func_fd;
		if (*g_fd >= 0) {
			close(*g_fd);
			*g_fd = -1;
		}
		mgr->init_state = BIFUR_UNINIT;
	}
}

static int
mapped_dev_op_local(struct bifur_mapped_dev_list_local *list,
		    struct rte_pci_device *mapped_pci_dev,
		    enum mapped_dev_op_local opcode)
{
	struct bifur_mapped_dev_local *mapped_dev = NULL;

	if ((opcode & DEV_ADD_LOCAL) != 0) {
		mapped_dev = rte_zmalloc("bifur_mapped_dev_local",
					 sizeof(struct bifur_mapped_dev_local), 0);
		if (mapped_dev == NULL)
			return -ENOMEM;
		mapped_dev->pci_dev = mapped_pci_dev;
		STAILQ_INSERT_TAIL(list, mapped_dev, entries);
	} else if ((opcode & DEV_DEL_LOCAL) != 0 &&
		   (opcode & DEV_QUERY_LOCAL) != 0) {
		bool found = false;
		STAILQ_FOREACH (mapped_dev, list, entries) {
			if (mapped_dev->pci_dev == mapped_pci_dev) {
				STAILQ_REMOVE(list, mapped_dev,
					      bifur_mapped_dev_local, entries);
				rte_free(mapped_dev);
				found = true;
				break;
			}
		}
		if (found)
			return 0;
		return -1;
	}
	return 0;
}

/* ====== 1. hinic3_parse_sysfs_value (direct call) ====== */

static int
test_parse_sysfs_valid(void)
{
	char template[] = "/tmp/bifur_test_XXXXXX";
	int fd = mkstemp(template);
	unsigned long val = 0;
	int ret;

	RTE_TEST_ASSERT(fd >= 0, "mkstemp should succeed");
	write(fd, "42\n", 3);
	close(fd);

	ret = hinic3_parse_sysfs_value(template, &val);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "should succeed on valid file");
	RTE_TEST_ASSERT_EQUAL(val, 42UL, "value should be 42");

	unlink(template);
	return TEST_SUCCESS;
}

static int
test_parse_sysfs_hex(void)
{
	char template[] = "/tmp/bifur_test_XXXXXX";
	int fd = mkstemp(template);
	unsigned long val = 0;
	int ret;

	RTE_TEST_ASSERT(fd >= 0, "mkstemp should succeed");
	write(fd, "0xFF\n", 5);
	close(fd);

	ret = hinic3_parse_sysfs_value(template, &val);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "should succeed on hex value");
	RTE_TEST_ASSERT_EQUAL(val, 255UL, "value should be 255 (0xFF)");

	unlink(template);
	return TEST_SUCCESS;
}

static int
test_parse_sysfs_nonexistent(void)
{
	unsigned long val = 0;
	int ret;

	ret = hinic3_parse_sysfs_value("/tmp/bifur_no_such_file_12345", &val);
	RTE_TEST_ASSERT_EQUAL(ret, -1, "non-existent file should return -1");

	return TEST_SUCCESS;
}

static int
test_parse_sysfs_empty(void)
{
	char template[] = "/tmp/bifur_test_XXXXXX";
	int fd = mkstemp(template);
	unsigned long val = 0;
	int ret;

	RTE_TEST_ASSERT(fd >= 0, "mkstemp should succeed");
	close(fd);

	ret = hinic3_parse_sysfs_value(template, &val);
	RTE_TEST_ASSERT_EQUAL(ret, -1, "empty file should return -1");

	unlink(template);
	return TEST_SUCCESS;
}

static int
test_parse_sysfs_non_numeric(void)
{
	char template[] = "/tmp/bifur_test_XXXXXX";
	int fd = mkstemp(template);
	unsigned long val = 0;
	int ret;

	RTE_TEST_ASSERT(fd >= 0, "mkstemp should succeed");
	write(fd, "abc\n", 4);
	close(fd);

	ret = hinic3_parse_sysfs_value(template, &val);
	RTE_TEST_ASSERT_EQUAL(ret, -1, "non-numeric content should return -1");

	unlink(template);
	return TEST_SUCCESS;
}

/* ====== 2. bifur_fill_msg_local ====== */

static int
test_bifur_fill_msg_mac_get(void)
{
	struct bifur_msg msg;
	uint8_t in_buf[16] = {0};
	uint8_t out_buf[16] = {0};

	memset(&msg, 0, sizeof(msg));
	bifur_fill_msg_local(&msg, BIFUR_DRV_CMD_MAC_GET,
			     in_buf, sizeof(in_buf),
			     out_buf, sizeof(out_buf));

	RTE_TEST_ASSERT_EQUAL(msg.drv_cmd, (u32)BIFUR_DRV_CMD_MAC_GET,
			      "drv_cmd should be MAC_GET");
	RTE_TEST_ASSERT_EQUAL(msg.in_buf, (void *)in_buf, "in_buf mismatch");
	RTE_TEST_ASSERT_EQUAL(msg.in_buf_len, (u32)sizeof(in_buf),
			      "in_buf_len mismatch");
	RTE_TEST_ASSERT_EQUAL(msg.out_buf, (void *)out_buf,
			      "out_buf mismatch");
	RTE_TEST_ASSERT_EQUAL(msg.out_buf_len, (u32)sizeof(out_buf),
			      "out_buf_len mismatch");

	return TEST_SUCCESS;
}

static int
test_bifur_fill_msg_vf_alloc(void)
{
	struct bifur_msg msg;
	uint32_t in_val = 0x1234;
	uint32_t out_val = 0;

	memset(&msg, 0, sizeof(msg));
	bifur_fill_msg_local(&msg, BIFUR_DRV_CMD_VF_ALLOC,
			     &in_val, sizeof(in_val),
			     &out_val, sizeof(out_val));

	RTE_TEST_ASSERT_EQUAL(msg.drv_cmd, (u32)BIFUR_DRV_CMD_VF_ALLOC,
			      "drv_cmd should be VF_ALLOC");
	RTE_TEST_ASSERT_EQUAL(msg.in_buf, (void *)&in_val, "in_buf mismatch");
	RTE_TEST_ASSERT_EQUAL(msg.in_buf_len, (u32)sizeof(in_val),
			      "in_buf_len mismatch");
	RTE_TEST_ASSERT_EQUAL(msg.out_buf, (void *)&out_val,
			      "out_buf mismatch");
	RTE_TEST_ASSERT_EQUAL(msg.out_buf_len, (u32)sizeof(out_val),
			      "out_buf_len mismatch");

	return TEST_SUCCESS;
}

static int
test_bifur_fill_msg_func_attr_get(void)
{
	struct bifur_msg msg;
	uint32_t in_val = 0x5678;
	uint32_t out_val = 0;

	memset(&msg, 0, sizeof(msg));
	bifur_fill_msg_local(&msg, BIFUR_DRV_CMD_FUNC_ATTR_GET,
			     &in_val, sizeof(in_val),
			     &out_val, sizeof(out_val));

	RTE_TEST_ASSERT_EQUAL(msg.drv_cmd,
			      (u32)BIFUR_DRV_CMD_FUNC_ATTR_GET,
			      "drv_cmd should be FUNC_ATTR_GET");
	RTE_TEST_ASSERT_EQUAL(msg.in_buf, (void *)&in_val, "in_buf mismatch");
	RTE_TEST_ASSERT_EQUAL(msg.in_buf_len, (u32)sizeof(in_val),
			      "in_buf_len mismatch");
	RTE_TEST_ASSERT_EQUAL(msg.out_buf, (void *)&out_val,
			      "out_buf mismatch");
	RTE_TEST_ASSERT_EQUAL(msg.out_buf_len, (u32)sizeof(out_val),
			      "out_buf_len mismatch");

	return TEST_SUCCESS;
}

static int
test_bifur_fill_msg_zero_len(void)
{
	struct bifur_msg msg;
	uint8_t dummy;

	memset(&msg, 0, sizeof(msg));
	bifur_fill_msg_local(&msg, BIFUR_DRV_CMD_MAC_GET,
			     &dummy, 0, &dummy, 0);

	RTE_TEST_ASSERT_EQUAL(msg.drv_cmd, (u32)BIFUR_DRV_CMD_MAC_GET,
			      "drv_cmd should be MAC_GET");
	RTE_TEST_ASSERT_EQUAL(msg.in_buf_len, 0u, "in_buf_len should be 0");
	RTE_TEST_ASSERT_EQUAL(msg.out_buf_len, 0u, "out_buf_len should be 0");

	return TEST_SUCCESS;
}

/* ====== 3. bifur_pci_addr_to_path_local ====== */

static int
test_bifur_pci_addr_path_standard(void)
{
	struct rte_pci_addr addr = {.domain = 0, .bus = 3, .devid = 0, .function = 0};
	char path[MAX_PATH_LEN];
	int ret;

	ret = bifur_pci_addr_to_path_local(&addr, path);
	RTE_TEST_ASSERT(ret > 0, "snprintf should return positive");
	RTE_TEST_ASSERT(strcmp(path, "/sys/bus/pci/devices/0000:03:00.0") == 0,
		       "path should be /sys/bus/pci/devices/0000:03:00.0");

	return TEST_SUCCESS;
}

static int
test_bifur_pci_addr_path_nonzero_domain(void)
{
	struct rte_pci_addr addr = {
		.domain = 0x10, .bus = 0xff, .devid = 0x1f, .function = 0x7
	};
	char path[MAX_PATH_LEN];
	int ret;

	ret = bifur_pci_addr_to_path_local(&addr, path);
	RTE_TEST_ASSERT(ret > 0, "snprintf should return positive");
	RTE_TEST_ASSERT(strcmp(path, "/sys/bus/pci/devices/0010:ff:1f.7") == 0,
		       "path should be /sys/bus/pci/devices/0010:ff:1f.7");

	return TEST_SUCCESS;
}

static int
test_bifur_pci_addr_path_max(void)
{
	struct rte_pci_addr addr = {
		.domain = 0xffff, .bus = 0xff, .devid = 0x1f, .function = 0x7
	};
	char path[MAX_PATH_LEN];
	int ret;

	ret = bifur_pci_addr_to_path_local(&addr, path);
	RTE_TEST_ASSERT(ret > 0, "snprintf should return positive");
	RTE_TEST_ASSERT(strcmp(path, "/sys/bus/pci/devices/ffff:ff:1f.7") == 0,
		       "path should be /sys/bus/pci/devices/ffff:ff:1f.7");

	return TEST_SUCCESS;
}

/* ====== 4. bifur_pci_match_local ====== */

static int
test_bifur_pci_match_exact(void)
{
	struct rte_pci_id id_tbl[] = {
		{.vendor_id = 0x19E5, .device_id = 0x0200,
		 .subsystem_vendor_id = 0x19E5, .subsystem_device_id = 0x0201,
		 .class_id = RTE_CLASS_ANY_ID},
		{.vendor_id = 0},
	};
	struct rte_pci_driver drv = {.id_table = id_tbl};
	struct rte_pci_device dev;

	memset(&dev, 0, sizeof(dev));
	dev.id.vendor_id = 0x19E5;
	dev.id.device_id = 0x0200;
	dev.id.subsystem_vendor_id = 0x19E5;
	dev.id.subsystem_device_id = 0x0201;

	RTE_TEST_ASSERT(bifur_pci_match_local(&drv, &dev),
		       "exact match should return true");

	return TEST_SUCCESS;
}

static int
test_bifur_pci_match_vendor_wildcard(void)
{
	struct rte_pci_id id_tbl[] = {
		{.vendor_id = RTE_PCI_ANY_ID, .device_id = 0x0200,
		 .subsystem_vendor_id = RTE_PCI_ANY_ID,
		 .subsystem_device_id = RTE_PCI_ANY_ID,
		 .class_id = RTE_CLASS_ANY_ID},
		{.vendor_id = 0},
	};
	struct rte_pci_driver drv = {.id_table = id_tbl};
	struct rte_pci_device dev;

	memset(&dev, 0, sizeof(dev));
	dev.id.vendor_id = 0x19E5;
	dev.id.device_id = 0x0200;
	dev.id.subsystem_vendor_id = 0x1234;
	dev.id.subsystem_device_id = 0x5678;

	RTE_TEST_ASSERT(bifur_pci_match_local(&drv, &dev),
		       "wildcard match should return true");

	return TEST_SUCCESS;
}

static int
test_bifur_pci_match_no_match(void)
{
	struct rte_pci_id id_tbl[] = {
		{.vendor_id = 0x19E5, .device_id = 0x0200,
		 .subsystem_vendor_id = RTE_PCI_ANY_ID,
		 .subsystem_device_id = RTE_PCI_ANY_ID,
		 .class_id = RTE_CLASS_ANY_ID},
		{.vendor_id = 0},
	};
	struct rte_pci_driver drv = {.id_table = id_tbl};
	struct rte_pci_device dev;

	memset(&dev, 0, sizeof(dev));
	dev.id.vendor_id = 0x8086;
	dev.id.device_id = 0x0200;

	RTE_TEST_ASSERT(!bifur_pci_match_local(&drv, &dev),
		       "non-matching vendor should return false");

	return TEST_SUCCESS;
}

static int
test_bifur_pci_match_multi_entries(void)
{
	struct rte_pci_id id_tbl[] = {
		{.vendor_id = 0x19E5, .device_id = 0x9999,
		 .subsystem_vendor_id = RTE_PCI_ANY_ID,
		 .subsystem_device_id = RTE_PCI_ANY_ID,
		 .class_id = RTE_CLASS_ANY_ID},
		{.vendor_id = 0x19E5, .device_id = 0x0200,
		 .subsystem_vendor_id = RTE_PCI_ANY_ID,
		 .subsystem_device_id = RTE_PCI_ANY_ID,
		 .class_id = RTE_CLASS_ANY_ID},
		{.vendor_id = 0},
	};
	struct rte_pci_driver drv = {.id_table = id_tbl};
	struct rte_pci_device dev;

	memset(&dev, 0, sizeof(dev));
	dev.id.vendor_id = 0x19E5;
	dev.id.device_id = 0x0200;

	RTE_TEST_ASSERT(bifur_pci_match_local(&drv, &dev),
		       "second entry should match");

	return TEST_SUCCESS;
}

static int
test_bifur_pci_match_empty_table(void)
{
	struct rte_pci_id id_tbl[] = {
		{.vendor_id = 0},
	};
	struct rte_pci_driver drv = {.id_table = id_tbl};
	struct rte_pci_device dev;

	memset(&dev, 0, sizeof(dev));
	dev.id.vendor_id = 0x19E5;
	dev.id.device_id = 0x0200;

	RTE_TEST_ASSERT(!bifur_pci_match_local(&drv, &dev),
		       "sentinel-only table should return false");

	return TEST_SUCCESS;
}

static int
test_bifur_pci_match_subsys_mismatch(void)
{
	struct rte_pci_id id_tbl[] = {
		{.vendor_id = 0x19E5, .device_id = 0x0200,
		 .subsystem_vendor_id = 0x19E5, .subsystem_device_id = 0x0201,
		 .class_id = RTE_CLASS_ANY_ID},
		{.vendor_id = 0},
	};
	struct rte_pci_driver drv = {.id_table = id_tbl};
	struct rte_pci_device dev;

	memset(&dev, 0, sizeof(dev));
	dev.id.vendor_id = 0x19E5;
	dev.id.device_id = 0x0200;
	dev.id.subsystem_vendor_id = 0x19E5;
	dev.id.subsystem_device_id = 0x9999; /* mismatch */

	RTE_TEST_ASSERT(!bifur_pci_match_local(&drv, &dev),
		       "subsystem mismatch should return false");

	return TEST_SUCCESS;
}

/* ====== 5. bifur_parse_one_sysfs_resource_local ====== */

static int
test_bifur_parse_resource_valid(void)
{
	char line[] = "0x1000 0x2000 0x00000200\n";
	uint64_t phys_addr = 0, end_addr = 0, flags = 0;
	int ret;

	ret = bifur_parse_one_sysfs_resource_local(line, sizeof(line),
						   &phys_addr, &end_addr, &flags);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "valid resource should parse");
	RTE_TEST_ASSERT_EQUAL(phys_addr, 0x1000ULL, "phys_addr mismatch");
	RTE_TEST_ASSERT_EQUAL(end_addr, 0x2000ULL, "end_addr mismatch");
	RTE_TEST_ASSERT_EQUAL(flags, 0x200ULL, "flags mismatch");

	return TEST_SUCCESS;
}

static int
test_bifur_parse_resource_too_few(void)
{
	char line[] = "0x1000 0x2000\n";
	uint64_t phys_addr = 0, end_addr = 0, flags = 0;
	int ret;

	ret = bifur_parse_one_sysfs_resource_local(line, sizeof(line),
						   &phys_addr, &end_addr, &flags);
	RTE_TEST_ASSERT_EQUAL(ret, -1, "too few fields should return -1");

	return TEST_SUCCESS;
}

static int
test_bifur_parse_resource_empty(void)
{
	char line[] = "";
	uint64_t phys_addr = 0, end_addr = 0, flags = 0;
	int ret;

	ret = bifur_parse_one_sysfs_resource_local(line, sizeof(line),
						   &phys_addr, &end_addr, &flags);
	RTE_TEST_ASSERT_EQUAL(ret, -1, "empty line should return -1");

	return TEST_SUCCESS;
}

/* ====== 6. bifur_pci_device_iova_mode_local ====== */

static int
test_bifur_iova_mode_igb_uio(void)
{
	struct rte_pci_driver pdrv;
	struct rte_pci_device pdev;

	memset(&pdrv, 0, sizeof(pdrv));
	memset(&pdev, 0, sizeof(pdev));
	pdev.kdrv = RTE_PCI_KDRV_IGB_UIO;

	RTE_TEST_ASSERT_EQUAL(bifur_pci_device_iova_mode_local(&pdrv, &pdev),
			      RTE_IOVA_PA,
			      "IGB_UIO should return RTE_IOVA_PA");

	return TEST_SUCCESS;
}

static int
test_bifur_iova_mode_uio_generic(void)
{
	struct rte_pci_driver pdrv;
	struct rte_pci_device pdev;

	memset(&pdrv, 0, sizeof(pdrv));
	memset(&pdev, 0, sizeof(pdev));
	pdev.kdrv = RTE_PCI_KDRV_UIO_GENERIC;

	RTE_TEST_ASSERT_EQUAL(bifur_pci_device_iova_mode_local(&pdrv, &pdev),
			      RTE_IOVA_PA,
			      "UIO_GENERIC should return RTE_IOVA_PA");

	return TEST_SUCCESS;
}

static int
test_bifur_iova_mode_unknown_no_va(void)
{
	struct rte_pci_driver pdrv;
	struct rte_pci_device pdev;

	memset(&pdrv, 0, sizeof(pdrv));
	memset(&pdev, 0, sizeof(pdev));
	pdev.kdrv = RTE_PCI_KDRV_UNKNOWN;
	pdrv.drv_flags = 0; /* no VA flag */

	RTE_TEST_ASSERT_EQUAL(bifur_pci_device_iova_mode_local(&pdrv, &pdev),
			      RTE_IOVA_DC,
			      "UNKNOWN without VA flag should return RTE_IOVA_DC");

	return TEST_SUCCESS;
}

static int
test_bifur_iova_mode_unknown_with_va(void)
{
	struct rte_pci_driver pdrv;
	struct rte_pci_device pdev;

	memset(&pdrv, 0, sizeof(pdrv));
	memset(&pdev, 0, sizeof(pdev));
	pdev.kdrv = RTE_PCI_KDRV_UNKNOWN;
	pdrv.drv_flags = RTE_PCI_DRV_NEED_IOVA_AS_VA;

	RTE_TEST_ASSERT_EQUAL(bifur_pci_device_iova_mode_local(&pdrv, &pdev),
			      RTE_IOVA_VA,
			      "UNKNOWN with VA flag should return RTE_IOVA_VA");

	return TEST_SUCCESS;
}

/* ====== 7. DBDF decoding ====== */

static int
test_bifur_dbdf_decode_standard(void)
{
	u32 dbdf = PCI_DBDF(0x0000, 0x03, 0x00, 0x00);
	u32 domain, bus, devid, function;

	domain = (dbdf >> 16) & 0xffff;
	bus = (dbdf >> 8) & 0xff;
	devid = (dbdf >> 3) & 0x1f;
	function = dbdf & 0x7;

	RTE_TEST_ASSERT_EQUAL(domain, 0x0000u, "domain should be 0");
	RTE_TEST_ASSERT_EQUAL(bus, 0x03u, "bus should be 3");
	RTE_TEST_ASSERT_EQUAL(devid, 0x00u, "devid should be 0");
	RTE_TEST_ASSERT_EQUAL(function, 0x00u, "function should be 0");

	return TEST_SUCCESS;
}

static int
test_bifur_dbdf_decode_max(void)
{
	u32 dbdf = 0xFFFFFFFF;
	u32 domain, bus, devid, function;

	domain = (dbdf >> 16) & 0xffff;
	bus = (dbdf >> 8) & 0xff;
	devid = (dbdf >> 3) & 0x1f;
	function = dbdf & 0x7;

	RTE_TEST_ASSERT_EQUAL(domain, 0xFFFFu, "domain should be 0xFFFF");
	RTE_TEST_ASSERT_EQUAL(bus, 0xFFu, "bus should be 0xFF");
	RTE_TEST_ASSERT_EQUAL(devid, 0x1Fu, "devid should be 0x1F");
	RTE_TEST_ASSERT_EQUAL(function, 0x7u, "function should be 7");

	return TEST_SUCCESS;
}

static int
test_bifur_dbdf_encode_decode(void)
{
	u32 dom = 0xABCD, bus = 0x42, devid = 0x15, func = 0x5;
	u32 dbdf, dec_dom, dec_bus, dec_devid, dec_func;

	dbdf = PCI_DBDF(dom, bus, devid, func);
	dec_dom = (dbdf >> 16) & 0xffff;
	dec_bus = (dbdf >> 8) & 0xff;
	dec_devid = (dbdf >> 3) & 0x1f;
	dec_func = dbdf & 0x7;

	RTE_TEST_ASSERT_EQUAL(dec_dom, dom, "domain round-trip");
	RTE_TEST_ASSERT_EQUAL(dec_bus, bus, "bus round-trip");
	RTE_TEST_ASSERT_EQUAL(dec_devid, devid, "devid round-trip");
	RTE_TEST_ASSERT_EQUAL(dec_func, func, "function round-trip");

	return TEST_SUCCESS;
}

/* ====== 8. try_deinit_mgr_local ====== */

static int
test_bifur_try_deinit_uninit(void)
{
	struct bifur_mgr_local mgr;

	memset(&mgr, 0, sizeof(mgr));
	mgr.init_state = BIFUR_UNINIT;
	mgr.func_mgr.global_func_fd = 42;

	try_deinit_mgr_local(&mgr);
	RTE_TEST_ASSERT_EQUAL(mgr.init_state, (int)BIFUR_UNINIT,
			      "state should remain UNINIT");
	RTE_TEST_ASSERT_EQUAL(mgr.func_mgr.global_func_fd, 42,
			      "fd should remain unchanged");

	return TEST_SUCCESS;
}

static int
test_bifur_try_deinit_failed(void)
{
	struct bifur_mgr_local mgr;

	memset(&mgr, 0, sizeof(mgr));
	mgr.init_state = BIFUR_INIT_FAILED;
	mgr.func_mgr.global_func_fd = 42;

	try_deinit_mgr_local(&mgr);
	RTE_TEST_ASSERT_EQUAL(mgr.init_state, (int)BIFUR_INIT_FAILED,
			      "state should remain FAILED");
	RTE_TEST_ASSERT_EQUAL(mgr.func_mgr.global_func_fd, 42,
			      "fd should remain unchanged");

	return TEST_SUCCESS;
}

static int
test_bifur_try_deinit_success_empty(void)
{
	struct bifur_mgr_local mgr;
	int pipefd[2];

	RTE_TEST_ASSERT(pipe(pipefd) == 0, "pipe should succeed");

	memset(&mgr, 0, sizeof(mgr));
	mgr.init_state = BIFUR_INIT_SUCCEED;
	mgr.func_mgr.global_func_fd = pipefd[0]; /* valid fd */
	TAILQ_INIT(&mgr.pair_list); /* empty list */

	try_deinit_mgr_local(&mgr);
	RTE_TEST_ASSERT_EQUAL(mgr.init_state, (int)BIFUR_UNINIT,
			      "state should become UNINIT");
	RTE_TEST_ASSERT_EQUAL(mgr.func_mgr.global_func_fd, -1,
			      "fd should be -1 after close");

	close(pipefd[1]); /* close write end */
	return TEST_SUCCESS;
}

static int
test_bifur_try_deinit_success_nonempty(void)
{
	struct bifur_mgr_local mgr;
	struct bifur_dev_pair_local pair1, pair2;

	memset(&mgr, 0, sizeof(mgr));
	mgr.init_state = BIFUR_INIT_SUCCEED;
	mgr.func_mgr.global_func_fd = 42;
	TAILQ_INIT(&mgr.pair_list);
	TAILQ_INSERT_TAIL(&mgr.pair_list, &pair1, entries);
	TAILQ_INSERT_TAIL(&mgr.pair_list, &pair2, entries);

	try_deinit_mgr_local(&mgr);
	RTE_TEST_ASSERT_EQUAL(mgr.init_state, (int)BIFUR_INIT_SUCCEED,
			      "state should remain SUCCEED with non-empty list");
	RTE_TEST_ASSERT_EQUAL(mgr.func_mgr.global_func_fd, 42,
			      "fd should remain unchanged with non-empty list");

	return TEST_SUCCESS;
}

/* ====== 9. mapped_dev_op_local ====== */

static int
test_bifur_mapped_dev_add(void)
{
	struct bifur_mapped_dev_list_local list;
	struct rte_pci_device dev1, dev2;
	int ret;

	memset(&dev1, 0, sizeof(dev1));
	memset(&dev2, 0, sizeof(dev2));
	STAILQ_INIT(&list);

	ret = mapped_dev_op_local(&list, &dev1, DEV_ADD_LOCAL);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "DEV_ADD should return 0");
	RTE_TEST_ASSERT(!STAILQ_EMPTY(&list), "list should be non-empty");

	/* Add second device */
	ret = mapped_dev_op_local(&list, &dev2, DEV_ADD_LOCAL);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "second DEV_ADD should return 0");

	/* Clean up */
	mapped_dev_op_local(&list, &dev1, DEV_DEL_LOCAL | DEV_QUERY_LOCAL);
	mapped_dev_op_local(&list, &dev2, DEV_DEL_LOCAL | DEV_QUERY_LOCAL);

	return TEST_SUCCESS;
}

static int
test_bifur_mapped_dev_del_existing(void)
{
	struct bifur_mapped_dev_list_local list;
	struct rte_pci_device dev1;
	int ret;

	memset(&dev1, 0, sizeof(dev1));
	STAILQ_INIT(&list);

	mapped_dev_op_local(&list, &dev1, DEV_ADD_LOCAL);
	RTE_TEST_ASSERT(!STAILQ_EMPTY(&list), "list should be non-empty after add");

	ret = mapped_dev_op_local(&list, &dev1, DEV_DEL_LOCAL | DEV_QUERY_LOCAL);
	RTE_TEST_ASSERT_EQUAL(ret, 0, "DEV_DEL on existing should return 0");
	RTE_TEST_ASSERT(STAILQ_EMPTY(&list), "list should be empty after del");

	return TEST_SUCCESS;
}

static int
test_bifur_mapped_dev_del_nonexist(void)
{
	struct bifur_mapped_dev_list_local list;
	struct rte_pci_device dev1;
	int ret;

	memset(&dev1, 0, sizeof(dev1));
	STAILQ_INIT(&list);

	ret = mapped_dev_op_local(&list, &dev1, DEV_DEL_LOCAL | DEV_QUERY_LOCAL);
	RTE_TEST_ASSERT_EQUAL(ret, -1, "DEV_DEL on empty list should return -1");

	return TEST_SUCCESS;
}

/* ====== 10. PCI_DBDF and PCI_DEVID macros ====== */

static int
test_bifur_pci_dbdf_macro(void)
{
	u32 val = PCI_DBDF(0, 3, 0, 0);

	RTE_TEST_ASSERT_EQUAL(val, 0x00000300u,
			      "PCI_DBDF(0,3,0,0) should be 0x300");

	val = PCI_DBDF(0x10, 0xFF, 0x1F, 0x7);
	RTE_TEST_ASSERT_EQUAL(val, 0x0010FFFFu,
			      "PCI_DBDF(0x10,0xFF,0x1F,0x7) should be 0x0010FFFF");

	return TEST_SUCCESS;
}

static int
test_bifur_pci_devid_macro(void)
{
	u32 val = PCI_DEVID(3, 0);

	RTE_TEST_ASSERT_EQUAL(val, (3u << 5) | 0u,
			      "PCI_DEVID(3,0) should be 96");

	val = PCI_DEVID(0xFF, 0x1F);
	RTE_TEST_ASSERT_EQUAL(val, (0xFFu << 5) | 0x1Fu,
			      "PCI_DEVID(0xFF,0x1F) should be max");

	return TEST_SUCCESS;
}

/* ====== Test suite ====== */

static struct unit_test_suite bifur_test_suite = {
	.suite_name = "HINIC3 Bifur Unit Tests",
	.unit_test_cases = {
		/* hinic3_parse_sysfs_value */
		TEST_CASE(test_parse_sysfs_valid),
		TEST_CASE(test_parse_sysfs_hex),
		TEST_CASE(test_parse_sysfs_nonexistent),
		TEST_CASE(test_parse_sysfs_empty),
		TEST_CASE(test_parse_sysfs_non_numeric),

		/* bifur_fill_msg_local */
		TEST_CASE(test_bifur_fill_msg_mac_get),
		TEST_CASE(test_bifur_fill_msg_vf_alloc),
		TEST_CASE(test_bifur_fill_msg_func_attr_get),
		TEST_CASE(test_bifur_fill_msg_zero_len),

		/* bifur_pci_addr_to_path_local */
		TEST_CASE(test_bifur_pci_addr_path_standard),
		TEST_CASE(test_bifur_pci_addr_path_nonzero_domain),
		TEST_CASE(test_bifur_pci_addr_path_max),

		/* bifur_pci_match_local */
		TEST_CASE(test_bifur_pci_match_exact),
		TEST_CASE(test_bifur_pci_match_vendor_wildcard),
		TEST_CASE(test_bifur_pci_match_no_match),
		TEST_CASE(test_bifur_pci_match_multi_entries),
		TEST_CASE(test_bifur_pci_match_empty_table),
		TEST_CASE(test_bifur_pci_match_subsys_mismatch),

		/* bifur_parse_one_sysfs_resource_local */
		TEST_CASE(test_bifur_parse_resource_valid),
		TEST_CASE(test_bifur_parse_resource_too_few),
		TEST_CASE(test_bifur_parse_resource_empty),

		/* bifur_pci_device_iova_mode_local */
		TEST_CASE(test_bifur_iova_mode_igb_uio),
		TEST_CASE(test_bifur_iova_mode_uio_generic),
		TEST_CASE(test_bifur_iova_mode_unknown_no_va),
		TEST_CASE(test_bifur_iova_mode_unknown_with_va),

		/* DBDF decoding */
		TEST_CASE(test_bifur_dbdf_decode_standard),
		TEST_CASE(test_bifur_dbdf_decode_max),
		TEST_CASE(test_bifur_dbdf_encode_decode),

		/* try_deinit_mgr_local */
		TEST_CASE(test_bifur_try_deinit_uninit),
		TEST_CASE(test_bifur_try_deinit_failed),
		TEST_CASE(test_bifur_try_deinit_success_empty),
		TEST_CASE(test_bifur_try_deinit_success_nonempty),

		/* mapped_dev_op_local */
		TEST_CASE(test_bifur_mapped_dev_add),
		TEST_CASE(test_bifur_mapped_dev_del_existing),
		TEST_CASE(test_bifur_mapped_dev_del_nonexist),

		/* PCI_DBDF and PCI_DEVID macros */
		TEST_CASE(test_bifur_pci_dbdf_macro),
		TEST_CASE(test_bifur_pci_devid_macro),

		TEST_CASES_END()
	}
};

static int
test_hinic3_bifur(void)
{
	return unit_test_suite_runner(&bifur_test_suite);
}

REGISTER_TEST_COMMAND(hinic3_bifur_autotest, test_hinic3_bifur);
