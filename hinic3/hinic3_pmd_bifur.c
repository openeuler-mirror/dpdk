/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 */
#include <string.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#ifdef DPDK_22_11
#include <bus_driver.h>
#else
#include <rte_bus.h>
#endif

#ifndef DPDK_20_11
#include <rte_string_fns.h>
#endif

#ifndef DPDK_21_11
#define RTE_PCI_ANY_ID		    (0xffff)
#define RTE_INTR_INSTANCE_F_PRIVATE UINT32_C(0)
#endif

#include "base/hinic3_compat.h"
#include "hinic3_pmd_bifur.h"

#ifndef PCI_DBDF
#define PCI_DBDF(dom, bus, dev, func) (((u32)(dom) << 16) | ((u32)(bus) << 8) | ((u32)(dev) << 3) | ((u32)(func)&0x7))
#endif

#define MEMZONE_NAME "bifur_pci_addr_zone"
/* Memory size required for allocating PCI addresses. */
#define MEMZONE_SIZE sizeof(struct rte_pci_addr)

enum bifur_func_type {
	BIFUR_EXCLUSIVE_PF = 1,
	BIFUR_SHARED_PF,
	BIFUR_RESOURCE_PF,
	BIFUR_RESOURCE_VF,
	BIFUR_FUNC_TYPE_MAX
};

enum bifur_cmd_m {
	BIFUR_DRV_CMD_FUNC_ATTR_GET = 1,
	BIFUR_DRV_CMD_VF_ALLOC,
	BIFUR_DRV_CMD_MAC_GET,
	BIFUR_DRV_CMD_MAX
};

struct bifur_mac_get_cmd_msg {
	unsigned int dbdf;
};

struct bifur_mac_get_cmd_rsp {
	u8 mac[RTE_ETHER_ADDR_LEN];
};

struct bifur_vf_alloc_cmd_msg {
	unsigned int dbdf;
};

struct bifur_vf_alloc_cmd_rsp {
	u32 vf_dbdf;
};

struct bifur_func_attr_get_cmd_msg {
	unsigned int dbdf;
};

struct bifur_func_attr_get_cmd_rsp {
	u32 func_type;
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

/* bifur define */
enum BIFUR_INIT_STATE {
	BIFUR_UNINIT = 0, /* should be zero */
	BIFUR_INIT_SUCCEED,
	BIFUR_INIT_FAILED,
};

struct hinic3_bifur_dev_pair {
	TAILQ_ENTRY(hinic3_bifur_dev_pair)
	entries;
	struct rte_pci_device *origin_pci_dev;
	struct rte_pci_device *work_pci_dev;
	int work_func_fd;
};

struct hinic3_bifur_mapped_dev {
	STAILQ_ENTRY(hinic3_bifur_mapped_dev)
	entries;
	struct rte_pci_device *pci_dev;
};

struct hinic3_bifur_func_mgr {
	int global_func_fd;
};

TAILQ_HEAD(hinic3_bifur_dev_pair_list, hinic3_bifur_dev_pair);

struct hinic3_bifur_mgr {
	struct hinic3_bifur_dev_pair_list pair_list;
	struct hinic3_bifur_func_mgr func_mgr;
	enum BIFUR_INIT_STATE init_state;
};

static struct hinic3_bifur_mgr g_hinic3_bifur_mgr;
static STAILQ_HEAD(, hinic3_bifur_mapped_dev)
	g_mapped_dev_list = STAILQ_HEAD_INITIALIZER(g_mapped_dev_list);

int
hinic3_parse_sysfs_value(const char *filename, unsigned long *val)
{
	FILE *f;
	char buf[BUFSIZ];
	char *end = NULL;

	if ((f = fopen(filename, "r")) == NULL) {
		PMD_DRV_LOG(ERR, "Cannot open sysfs value %s", filename);
		return -1;
	}

	if (fgets(buf, sizeof(buf), f) == NULL) {
		PMD_DRV_LOG(ERR, "Cannot read sysfs value %s", filename);
		fclose(f);
		return -1;
	}
	*val = strtoul(buf, &end, 0);
	if ((buf[0] == '\0') || (end == NULL) || (*end != '\n')) {
		PMD_DRV_LOG(ERR, "Cannot parse sysfs value %s", filename);
		fclose(f);
		return -1;
	}
	fclose(f);
	return 0;
}

static int
hinic3_bifur_send_msg(int cdev_fd, struct bifur_msg *msg)
{
	int ret;
	int msg_len = sizeof(struct bifur_msg);

	ret = write(cdev_fd, msg, msg_len);
	if (ret < 0) {
		PMD_DRV_LOG(ERR, "Write bifur message error(%s)", strerror(errno));
		return ret;
	}

	return 0;
}

static void
hinic3_bifur_fill_msg(struct bifur_msg *msg, enum bifur_cmd_m cmd, void *in_buf, uint32_t in_buf_len,
		      void *out_buf, uint32_t out_buf_len)
{
	msg->drv_cmd = cmd;
	msg->in_buf = in_buf;
	msg->in_buf_len = in_buf_len;
	msg->out_buf = out_buf;
	msg->out_buf_len = out_buf_len;
}

static int
hinic3_bifur_init_mgr(void)
{
	int fd, ret = -1;
	fd = open(BIFUR_GDEV_PATH, O_WRONLY);
	if (fd < 0) {
		PMD_DRV_LOG(ERR, "%s open failed (%s)", BIFUR_GDEV_PATH, strerror(errno));
		ret = fd;
		goto init_failed; /* Once failed, will not try again. */
	}
	g_hinic3_bifur_mgr.func_mgr.global_func_fd = fd;

	TAILQ_INIT(&g_hinic3_bifur_mgr.pair_list);
	g_hinic3_bifur_mgr.init_state = BIFUR_INIT_SUCCEED;
	return 0;

init_failed:
	g_hinic3_bifur_mgr.init_state = BIFUR_INIT_FAILED;
	return ret;
}

static void
hinic3_bifur_try_deinit_mgr(void)
{
	if (g_hinic3_bifur_mgr.init_state != BIFUR_INIT_SUCCEED) {
		return;
	}
	if (TAILQ_EMPTY(&g_hinic3_bifur_mgr.pair_list)) {
		int *g_fd = &g_hinic3_bifur_mgr.func_mgr.global_func_fd;
		if (*g_fd >= 0) {
			close(*g_fd);
			*g_fd = -1;
		}

		g_hinic3_bifur_mgr.init_state = BIFUR_UNINIT;
	}
}

static int
hinic3_bifur_pci_map_device(struct rte_pci_device *dev)
{
	int ret = 0;
	ret = rte_pci_map_device(dev);
#ifdef DPDK_21_11
	if (ret != 0) {
		rte_intr_instance_free(dev->vfio_req_intr_handle);
		dev->vfio_req_intr_handle = NULL;
		rte_intr_instance_free(dev->intr_handle);
		dev->intr_handle = NULL;
	}
#endif
	return ret;
}

static void
hinic3_bifur_pci_unmap_device(struct rte_pci_device *dev)
{
	if (rte_eal_process_type() == RTE_PROC_PRIMARY) {
		rte_pci_unmap_device(dev);
	}
}

static int
hinic3_bifur_func_pair_from_kernel(struct rte_pci_addr *target_pci_addr, struct rte_pci_addr *pair_pci_addr)
{
	int ret;
	u32 dbdf;
	struct bifur_msg msg = {0};
	struct bifur_vf_alloc_cmd_msg cmd = {0};
	struct bifur_vf_alloc_cmd_rsp rsp = {0};
	struct rte_pci_addr *addr = target_pci_addr;
	int g_func_fd = g_hinic3_bifur_mgr.func_mgr.global_func_fd;

	dbdf = PCI_DBDF(addr->domain, addr->bus, addr->devid, addr->function);
	cmd.dbdf = dbdf;
	hinic3_bifur_fill_msg(&msg, BIFUR_DRV_CMD_VF_ALLOC, &cmd, sizeof(struct bifur_vf_alloc_cmd_msg), &rsp,
			      sizeof(struct bifur_vf_alloc_cmd_rsp));
	ret = hinic3_bifur_send_msg(g_func_fd, &msg);
	if (ret) {
		return ret;
	}

	pair_pci_addr->domain = (rsp.vf_dbdf >> 16) & 0xffff;
	pair_pci_addr->bus = (rsp.vf_dbdf >> 8) & 0xff;
	pair_pci_addr->devid = (rsp.vf_dbdf >> 3) & 0x1f;
	pair_pci_addr->function = (rsp.vf_dbdf) & 0x7;

	PMD_DRV_LOG(INFO, "0x%x`s pair function is 0x%x", dbdf, rsp.vf_dbdf);
	return 0;
}

static int
hinic3_bifur_func_type_from_kernel(struct rte_pci_addr *target_pci_addr, enum bifur_func_type *func_type)
{
	int ret;
	u32 dbdf;
	struct bifur_msg msg = {0};
	struct bifur_func_attr_get_cmd_msg cmd = {0};
	struct bifur_func_attr_get_cmd_rsp rsp = {0};
	struct rte_pci_addr *addr = target_pci_addr;
	int g_func_fd = g_hinic3_bifur_mgr.func_mgr.global_func_fd;

	dbdf = PCI_DBDF(addr->domain, addr->bus, addr->devid, addr->function);

	cmd.dbdf = dbdf;
	hinic3_bifur_fill_msg(&msg, BIFUR_DRV_CMD_FUNC_ATTR_GET, &cmd, sizeof(struct bifur_func_attr_get_cmd_msg), &rsp,
			      sizeof(struct bifur_func_attr_get_cmd_rsp));
	ret = hinic3_bifur_send_msg(g_func_fd, &msg);
	if (ret) {
		return ret;
	}

	*func_type = rsp.func_type;
	return 0;
}

static int
hinic3_bifur_query_func_type(struct rte_pci_addr *target_pci_addr, enum bifur_func_type *func_type)
{
	int ret;
	if (g_hinic3_bifur_mgr.init_state == BIFUR_INIT_FAILED) {
		return -1;
	}

	if (g_hinic3_bifur_mgr.init_state == BIFUR_UNINIT) {
		ret = hinic3_bifur_init_mgr();
		if (ret != 0) {
			return ret;
		}
	}

	return hinic3_bifur_func_type_from_kernel(target_pci_addr, func_type);
}

/* rte pci operation begin */
#define MAX_PATH_LEN   64
#define IORESOURCE_IO  0x00000100
#define IORESOURCE_MEM 0x00000200
#define PCI_SYS_PATH   "/sys/bus/pci/devices/"

static int
hinic3_bifur_pci_addr_to_path(const struct rte_pci_addr *addr, char *str)
{
	return snprintf(str, MAX_PATH_LEN, "%s%04x:%02x:%02x.%x", PCI_SYS_PATH, addr->domain, addr->bus, addr->devid,
					addr->function);
}

static bool
hinic3_bifur_pci_match(const struct rte_pci_driver *pci_drv,
		       const struct rte_pci_device *pci_dev)
{
	const struct rte_pci_id *id_table;

	for (id_table = pci_drv->id_table; id_table->vendor_id != 0; id_table++) {
		if (id_table->vendor_id != pci_dev->id.vendor_id &&
		    id_table->vendor_id != RTE_PCI_ANY_ID) {
			continue;
		}
		if (id_table->device_id != pci_dev->id.device_id &&
		    id_table->device_id != RTE_PCI_ANY_ID) {
			continue;
		}
		if (id_table->subsystem_vendor_id != pci_dev->id.subsystem_vendor_id &&
		    id_table->subsystem_vendor_id != RTE_PCI_ANY_ID) {
			continue;
		}
		if (id_table->subsystem_device_id != pci_dev->id.subsystem_device_id &&
		    id_table->subsystem_device_id != RTE_PCI_ANY_ID) {
			continue;
		}
		if (id_table->class_id != pci_dev->id.class_id &&
		    id_table->class_id != RTE_CLASS_ANY_ID) {
			continue;
		}
		return true;
	}

	return false;
}

static enum rte_iova_mode
hinic3_bifur_pci_device_iova_mode(const struct rte_pci_driver *pdrv,
				  const struct rte_pci_device *pdev)
{
	enum rte_iova_mode iova_mode = RTE_IOVA_DC;

	switch (pdev->kdrv) {
	case RTE_PCI_KDRV_VFIO: {
#ifdef VFIO_PRESENT
		static int is_vfio_noiommu_enabled = -1;

		if (is_vfio_noiommu_enabled == -1) {
			if (rte_vfio_noiommu_is_enabled() == 1) {
				is_vfio_noiommu_enabled = 1;
			} else {
				is_vfio_noiommu_enabled = 0;
			}
		}
		if (is_vfio_noiommu_enabled != 0) {
			iova_mode = RTE_IOVA_PA;
		} else if ((pdrv->drv_flags & RTE_PCI_DRV_NEED_IOVA_AS_VA) != 0) {
			iova_mode = RTE_IOVA_VA;
		}
#endif
		break;
	}

	case RTE_PCI_KDRV_IGB_UIO:
	case RTE_PCI_KDRV_UIO_GENERIC:
		iova_mode = RTE_IOVA_PA;
		break;
	default:
		if ((pdrv->drv_flags & RTE_PCI_DRV_NEED_IOVA_AS_VA) != 0) {
			iova_mode = RTE_IOVA_VA;
		}
		break;
	}
	return iova_mode;
}

static struct rte_devargs *
hinic3_bifur_pci_devargs_lookup(const struct rte_pci_addr *pci_addr)
{
	struct rte_devargs *devargs;
	struct rte_pci_addr addr;

	RTE_EAL_DEVARGS_FOREACH("pci", devargs)
	{
		devargs->bus->parse(devargs->name, &addr);
		if (!rte_pci_addr_cmp(pci_addr, &addr)) {
			return devargs;
		}
	}
	return NULL;
}

static void
hinic3_bifur_pci_name_set(struct rte_pci_device *dev, struct rte_pci_device *cur_pci_dev)
{
	struct rte_devargs *devargs;

	/* Fake vf using the name of pf. */
	rte_pci_device_name(&cur_pci_dev->addr, dev->name, sizeof(dev->name));
	devargs = hinic3_bifur_pci_devargs_lookup(&dev->addr);
	dev->device.devargs = devargs;
	if (devargs != NULL) {
		dev->device.name = dev->device.devargs->name;
	} else {
		dev->device.name = dev->name;
	}
}

static int
hinic3_bifur_parse_one_sysfs_resource(char *line, size_t len, uint64_t *phys_addr,
				      uint64_t *end_addr, uint64_t *flags)
{
	union pci_resource_info {
		struct {
			char *phys_addr;
			char *end_addr;
			char *flags;
		};
		char *ptrs[PCI_RESOURCE_FMT_NVAL];
	} res_info;

	if (rte_strsplit(line, len, res_info.ptrs, 3, ' ') != 3) {
		PMD_DRV_LOG(ERR, "Bad resource format");
		return -1;
	}
	errno = 0;
	*phys_addr = strtoull(res_info.phys_addr, NULL, 16);
	*end_addr = strtoull(res_info.end_addr, NULL, 16);
	*flags = strtoull(res_info.flags, NULL, 16);
	if (errno != 0) {
		PMD_DRV_LOG(ERR, "Bad resource format");
		return -1;
	}

	return 0;
}

static int
hinic3_bifur_get_kernel_driver_by_path(const char *filename, char *dri_name,
				       size_t len)
{
	int count;
	char path[PATH_MAX];
	char *name;

	if (!filename || !dri_name)
		return -1;

	count = readlink(filename, path, PATH_MAX);
	if (count >= PATH_MAX)
		return -1;

	if (count < 0)
		return 1;

	path[count] = '\0';

	name = strrchr(path, '/');
	if (name) {
		strlcpy(dri_name, name + 1, len);
		return 0;
	}

	return -1;
}

static int
hinic3_bifur_parse_sysfs_resource(const char *filename, struct rte_pci_device *dev)
{
	FILE *f;
	char buf[BUFSIZ];
	int i;
	uint64_t phys_addr, end_addr, flags;

	f = fopen(filename, "r");
	if (f == NULL) {
		PMD_DRV_LOG(ERR, "Cannot open sysfs resource");
		return -1;
	}

	for (i = 0; i < PCI_MAX_RESOURCE; i++) {
		if (fgets(buf, sizeof(buf), f) == NULL) {
			PMD_DRV_LOG(ERR,
				    "Cannot read resource");
			goto error;
		}
		if (hinic3_bifur_parse_one_sysfs_resource(buf, sizeof(buf), &phys_addr,
							  &end_addr, &flags) < 0) {
			goto error;
		}

		if (flags & IORESOURCE_MEM) {
			dev->mem_resource[i].phys_addr = phys_addr;
			dev->mem_resource[i].len = end_addr - phys_addr + 1;
			dev->mem_resource[i].addr = NULL;
		}
	}
	fclose(f);
	return 0;
error:
	fclose(f);
	return -1;
}

static struct rte_pci_device *
hinic3_bifur_alloc_pci_dev(const char *dirname,
			   const struct rte_pci_addr *pair_pci_addr, struct rte_pci_device *origin_pci_dev)
{
	char filename[PATH_MAX];
	unsigned long tmp;
	struct rte_pci_device *dev;
	char driver[PATH_MAX];
	int ret;

	dev = rte_zmalloc("hinic3_bifur_pci_dev", sizeof(*dev), 0);
	if (dev == NULL) {
		return NULL;
	}

	dev->device.bus = origin_pci_dev->device.bus;
	dev->addr = *pair_pci_addr;

	snprintf(filename, sizeof(filename), "%s/vendor", dirname);
	if (hinic3_parse_sysfs_value(filename, &tmp) < 0) {
		rte_free(dev);
		return NULL;
	}
	dev->id.vendor_id = (uint16_t)tmp;

	snprintf(filename, sizeof(filename), "%s/device", dirname);
	if (hinic3_parse_sysfs_value(filename, &tmp) < 0) {
		rte_free(dev);
		return NULL;
	}
	dev->id.device_id = (uint16_t)tmp;

	snprintf(filename, sizeof(filename), "%s/subsystem_vendor", dirname);
	if (hinic3_parse_sysfs_value(filename, &tmp) < 0) {
		rte_free(dev);
		return NULL;
	}
	dev->id.subsystem_vendor_id = (uint16_t)tmp;

	snprintf(filename, sizeof(filename), "%s/subsystem_device", dirname);
	if (hinic3_parse_sysfs_value(filename, &tmp) < 0) {
		rte_free(dev);
		return NULL;
	}
	dev->id.subsystem_device_id = (uint16_t)tmp;

	snprintf(filename, sizeof(filename), "%s/class", dirname);
	if (hinic3_parse_sysfs_value(filename, &tmp) < 0) {
		rte_free(dev);
		return NULL;
	}

	dev->id.class_id = (uint32_t)tmp & RTE_CLASS_ANY_ID;
	dev->max_vfs = 0;
	snprintf(filename, sizeof(filename), "%s/max_vfs", dirname);
	if (!access(filename, F_OK) &&
	    hinic3_parse_sysfs_value(filename, &tmp) == 0) {
		dev->max_vfs = (uint16_t)tmp;
	} else {
		snprintf(filename, sizeof(filename), "%s/sriov_numvfs", dirname);
		if (!access(filename, F_OK) &&
		    hinic3_parse_sysfs_value(filename, &tmp) == 0) {
			dev->max_vfs = (uint16_t)tmp;
		}
	}

	snprintf(filename, sizeof(filename), "%s/numa_node", dirname);

	if (access(filename, F_OK) != -1) {
		if (hinic3_parse_sysfs_value(filename, &tmp) == 0) {
			dev->device.numa_node = tmp;
		} else {
			dev->device.numa_node = -1;
		}
	} else {
		dev->device.numa_node = 0;
	}

	hinic3_bifur_pci_name_set(dev, origin_pci_dev);

	snprintf(filename, sizeof(filename), "%s/resource", dirname);
	if (hinic3_bifur_parse_sysfs_resource(filename, dev) < 0) {
		PMD_DRV_LOG(ERR, "Cannot parse resource");
		rte_free(dev);
		return NULL;
	}

	snprintf(filename, sizeof(filename), "%s/driver", dirname);
	ret = hinic3_bifur_get_kernel_driver_by_path(filename, driver, sizeof(driver));
	if (ret < 0) {
		PMD_DRV_LOG(ERR, "Fail to get kernel driver");
		rte_free(dev);
		return NULL;
	}

	if (!ret) {
		if (!strcmp(driver, "vfio-pci")) {
			dev->kdrv = RTE_PCI_KDRV_VFIO;
		} else if (!strcmp(driver, "igb_uio")) {
			dev->kdrv = RTE_PCI_KDRV_IGB_UIO;
		} else if (!strcmp(driver, "uio_pci_generic")) {
			dev->kdrv = RTE_PCI_KDRV_UIO_GENERIC;
		} else {
			dev->kdrv = RTE_PCI_KDRV_UNKNOWN;
		}
	} else {
		rte_free(dev);
		return NULL;
	}
	return dev;
}

static int
hinic3_bifur_work_pci_pre_probe(struct rte_pci_driver *dr, struct rte_pci_device *dev)
{
	bool already_probed;

	if ((dr == NULL) || (dev == NULL)) {
		return -EINVAL;
	}

	if (!hinic3_bifur_pci_match(dr, dev)) {
		return 1;
	}

	if (dev->device.devargs != NULL &&
	    dev->device.devargs->policy == RTE_DEV_BLOCKED) {
		PMD_DRV_LOG(INFO, "Device is blocked, not initializing");
		return 1;
	}

	if (dev->device.numa_node < 0) {
		if (rte_socket_count() > 1) {
			PMD_DRV_LOG(INFO, "Device %s is not NUMA-aware, defaulting socket to 0", dev->name);
		}
		dev->device.numa_node = 0;
	}

	already_probed = rte_dev_is_probed(&dev->device);
	if (already_probed && !(dr->drv_flags & RTE_PCI_DRV_PROBE_AGAIN)) {
		PMD_DRV_LOG(DEBUG, "Device %s is already probed", dev->device.name);
		return -EEXIST;
	}

	if (!already_probed) {
		enum rte_iova_mode dev_iova_mode;
		enum rte_iova_mode iova_mode;

		dev_iova_mode = hinic3_bifur_pci_device_iova_mode(dr, dev);
		iova_mode = rte_eal_iova_mode();
		if (dev_iova_mode != RTE_IOVA_DC &&
		    dev_iova_mode != iova_mode) {
			PMD_DRV_LOG(ERR, "Expecting '%s' IOVA mode but current mode is '%s', not initializing",
				    dev_iova_mode == RTE_IOVA_PA ? "PA" : "VA",
				    iova_mode == RTE_IOVA_PA ? "PA" : "VA");
			return -EINVAL;
		}
#ifdef DPDK_21_11
		dev->intr_handle = rte_intr_instance_alloc(RTE_INTR_INSTANCE_F_PRIVATE);
		if (dev->intr_handle == NULL) {
			PMD_DRV_LOG(ERR, "Failed to create interrupt instance for %s", dev->device.name);
			return -ENOMEM;
		}

		dev->vfio_req_intr_handle = rte_intr_instance_alloc(RTE_INTR_INSTANCE_F_PRIVATE);
		if (dev->vfio_req_intr_handle == NULL) {
			rte_intr_instance_free(dev->intr_handle);
			dev->intr_handle = NULL;
			PMD_DRV_LOG(ERR, "Failed to create vfio req interrupt instance for %s", dev->device.name);
			return -ENOMEM;
		}
#endif
		dev->driver = dr;
	}

	dev->device.driver = &dr->driver;
	return 0;
}

/* rte pci operation end */
static int
hinic3_bifur_probe_pair_func(struct rte_pci_driver *pci_drv, struct rte_pci_device *origin_pci_dev,
			     struct rte_pci_device **work_pci_dev, struct rte_pci_addr *pair_pci_addr)
{
	int ret;
	char path[MAX_PATH_LEN] = {0};
	ret = hinic3_bifur_pci_addr_to_path(pair_pci_addr, path);
	if (ret < 0) {
		return -1;
	}
	*work_pci_dev = hinic3_bifur_alloc_pci_dev(path, pair_pci_addr, origin_pci_dev);
	if (*work_pci_dev == NULL) {
		return -1;
	}
	ret = hinic3_bifur_work_pci_pre_probe(pci_drv, *work_pci_dev);
	if (ret != 0) {
		return ret;
	}
	return 0;
}

enum MAPPED_DEV_OP_OODE {
	DEV_ADD = 0x1,
	DEV_DEL = 0x2,
	DEV_QUERY = 0x4,
};

static int
hinic3_bifur_mapped_dev_op(struct rte_pci_device *mapped_pci_dev, enum MAPPED_DEV_OP_OODE opcode)
{
	struct hinic3_bifur_mapped_dev *mapped_dev = NULL;

	if ((opcode & DEV_ADD) != 0) {
		mapped_dev = rte_zmalloc("hinic3_bifur_mapped_dev", sizeof(struct hinic3_bifur_mapped_dev), 0);
		if (mapped_dev == NULL) {
			return -ENOMEM;
		}
		mapped_dev->pci_dev = mapped_pci_dev;
		STAILQ_INSERT_TAIL(&g_mapped_dev_list, mapped_dev, entries);
	} else if ((opcode & DEV_DEL) != 0 && (opcode & DEV_QUERY) != 0) {
		bool found = false;
		STAILQ_FOREACH (mapped_dev, &g_mapped_dev_list, entries) {
			if (mapped_dev->pci_dev == mapped_pci_dev) {
				hinic3_bifur_pci_unmap_device(mapped_dev->pci_dev);
				rte_free(mapped_dev);
				STAILQ_REMOVE(&g_mapped_dev_list, mapped_dev, hinic3_bifur_mapped_dev, entries);
				found = true;
				break;
			}
		}
		if (found == true) {
			return 0;
		}
		return -1;
	}
	return 0;
}

#define HINIC3_BIFUR_MAX_PATH_LEN 128

static int
hinic3_bifur_lock_pair(struct hinic3_bifur_dev_pair *dev_pair)
{
	int ret = 0;
	u32 dbdf;
	char real_path[PATH_MAX] = {0};
	char file_path[HINIC3_BIFUR_MAX_PATH_LEN] = {0};
	struct rte_pci_addr *addr = &dev_pair->work_pci_dev->addr;

	dbdf = PCI_DBDF(addr->domain, addr->bus, addr->devid, addr->function);
	ret = snprintf(file_path, HINIC3_BIFUR_MAX_PATH_LEN, "%s/0x%x/%s", BIFUR_PROC_PATH, dbdf, BIFUR_DEV_NAME);
	if (ret < 0) {
		PMD_DRV_LOG(ERR, "Build dbdf(0x%x) path(%s) failed, err(%s).", dbdf, file_path, strerror(errno));
		return -1;
	}

	if (realpath(file_path, real_path) == NULL) {
		PMD_DRV_LOG(ERR, "Build dbdf(0x%x) real path(%s) invalid, err(%s).", dbdf, file_path, strerror(errno));
		return -1;
	}

	int fd = open(real_path, O_RDONLY);
	if (fd < 0) {
		PMD_DRV_LOG(ERR, "Build dbdf(0x%x) open path(%s) failed, err(%s) ret(%d).", dbdf, real_path, strerror(errno), ret);
		return -1;
	}

	dev_pair->work_func_fd = fd;

	return 0;
}

static int
hinic3_bifur_store_pcidev_pairs(struct rte_pci_device *origin_pci_dev,
				struct rte_pci_device *work_pci_dev)
{
	int ret;
	struct hinic3_bifur_dev_pair *dev_pair = NULL;
	dev_pair = rte_zmalloc("hinic3_bifur_dev_pair", sizeof(struct hinic3_bifur_dev_pair), 0);
	if (dev_pair == NULL) {
		return -ENOMEM;
	}
	dev_pair->origin_pci_dev = origin_pci_dev;
	dev_pair->work_pci_dev = work_pci_dev;
	ret = hinic3_bifur_lock_pair(dev_pair);
	if (ret != 0) {
		return ret;
	}
	TAILQ_INSERT_TAIL(&g_hinic3_bifur_mgr.pair_list, dev_pair, entries);
	return 0;
}

static void
hinic3_bifur_remove_pcidev_pairs(struct rte_pci_device *origin_pci_dev)
{
	struct hinic3_bifur_dev_pair *dev_pair = NULL;
	TAILQ_FOREACH (dev_pair, &g_hinic3_bifur_mgr.pair_list, entries) {
		if (dev_pair->origin_pci_dev == origin_pci_dev) {
			hinic3_bifur_pci_unmap_device(dev_pair->work_pci_dev);
			close(dev_pair->work_func_fd);
			rte_free(dev_pair->work_pci_dev);
			TAILQ_REMOVE(&g_hinic3_bifur_mgr.pair_list, dev_pair, entries);
			rte_free(dev_pair);
			return;
		}
	}

	if (hinic3_bifur_mapped_dev_op(origin_pci_dev, DEV_DEL | DEV_QUERY) != 0) {
		PMD_DRV_LOG(ERR, "Bifur remove pcidev pairs failed.");
	}
}

static int
hinic3_bifur_handle_share_func(struct rte_pci_driver *pci_drv, struct rte_pci_device *origin_pci_dev,
			       struct rte_pci_device **work_pci_dev)
{
	int ret;
	struct rte_pci_addr pair_pci_addr;
	if (rte_eal_process_type() == RTE_PROC_PRIMARY) {
		/* Avoid creating memzone twice during two probes within the same process. */
		if (rte_memzone_lookup(MEMZONE_NAME) == NULL) {
			if (rte_memzone_reserve(MEMZONE_NAME, MEMZONE_SIZE, SOCKET_ID_ANY, 0) == NULL) {
				PMD_DRV_LOG(ERR, "Failed to reserve memory zone.");
				return -1;
			}
		}
		/* Reserve shared memory area. */
		const struct rte_memzone *mz = rte_memzone_lookup(MEMZONE_NAME);
		if (mz == NULL) {
			PMD_DRV_LOG(ERR, "Memory zone not found in primary process.");
			return -1;
		}

		ret = hinic3_bifur_func_pair_from_kernel(&origin_pci_dev->addr, &pair_pci_addr);
		if (ret != 0) {
			return ret;
		}

		*((struct rte_pci_addr *)mz->addr) = pair_pci_addr;
	} else {
		const struct rte_memzone *mz = rte_memzone_lookup(MEMZONE_NAME);
		if (mz == NULL) {
			PMD_DRV_LOG(ERR, "Memory zone not found in secondary process.");
			return -1;
		}

		pair_pci_addr = *(struct rte_pci_addr *)mz->addr;
	}

	ret = hinic3_bifur_probe_pair_func(pci_drv, origin_pci_dev, work_pci_dev, &pair_pci_addr);
	if (ret != 0) {
		return ret;
	}

	ret = hinic3_bifur_store_pcidev_pairs(origin_pci_dev, *work_pci_dev);
	if (ret != 0) {
		return ret;
	}

	return 0;
}

int
hinic3_bifur_pre_probe(struct rte_pci_driver *pci_drv, struct rte_pci_device *origin_pci_dev,
		       struct rte_pci_device **work_pci_dev, enum BIFUR_ACTION *bifur_action)
{
	int ret = -1;
	enum bifur_func_type func_type = BIFUR_EXCLUSIVE_PF;

	if (pci_drv == NULL || origin_pci_dev == NULL || work_pci_dev == NULL || bifur_action == NULL) {
		/* Will not affect the continuation of mainstream programs. */
		return 0;
	}

	ret = hinic3_bifur_query_func_type(&origin_pci_dev->addr, &func_type);
	if (ret != 0) {
		func_type = BIFUR_EXCLUSIVE_PF; /* as default */
	}

	switch (func_type) {
	case BIFUR_SHARED_PF:
		ret = hinic3_bifur_handle_share_func(pci_drv, origin_pci_dev, work_pci_dev);
		if (ret != 0) {
			*bifur_action = BIFUR_DONE;
		} else {
			*bifur_action = BIFUR_CONTINUE;
		}
		break;
	case BIFUR_RESOURCE_PF:
		*bifur_action = BIFUR_DONE;
		ret = -1; /* Resource pf will not load. */
		break;
	case BIFUR_RESOURCE_VF:
		*bifur_action = BIFUR_DONE;
		ret = 0; /* Resource vf have to load successfully, otherwise, will release map resource. */
		break;
	default:
		*work_pci_dev = origin_pci_dev;
		*bifur_action = BIFUR_CONTINUE;
		ret = 0; /* All other functions will load exclusive. */
		break;
	}

	if (*bifur_action == BIFUR_CONTINUE) {
		ret = hinic3_bifur_pci_map_device(*work_pci_dev);
		if (hinic3_bifur_mapped_dev_op(*work_pci_dev, DEV_ADD) != 0) {
			PMD_DRV_LOG(WARNING, "Record mapped device failed, may cause error while release");
		}
	}

	return ret;
}

void
hinic3_bifur_post_remove(struct rte_pci_device *origin_pci_dev)
{
	if (origin_pci_dev == NULL) {
		return;
	}
	hinic3_bifur_remove_pcidev_pairs(origin_pci_dev);
	hinic3_bifur_try_deinit_mgr();
}

bool
hinic3_bifur_is_shared_dev(struct rte_pci_device *work_pci_dev)
{
	struct hinic3_bifur_dev_pair *dev_pair = NULL;

	if (work_pci_dev == NULL) {
		return false;
	}

	TAILQ_FOREACH (dev_pair, &g_hinic3_bifur_mgr.pair_list, entries) {
		if (dev_pair->work_pci_dev == work_pci_dev) {
			return true;
		}
	}

	return false;
}

int
hinic3_bifur_get_default_mac(struct rte_pci_device *pci_dev, u8 *mac_addr, int ether_len)
{
	int ret;
	u32 dbdf;
	struct bifur_msg msg = {0};
	struct rte_pci_addr *addr = NULL;
	struct bifur_mac_get_cmd_msg cmd = {0};
	struct bifur_mac_get_cmd_rsp rsp = {0};

	int g_func_fd = g_hinic3_bifur_mgr.func_mgr.global_func_fd;

	if (pci_dev == NULL || mac_addr == NULL || ether_len > RTE_ETHER_ADDR_LEN) {
		return -EINVAL;
	}

	addr = &pci_dev->addr;
	dbdf = PCI_DBDF(addr->domain, addr->bus, addr->devid, addr->function);
	cmd.dbdf = dbdf;
	hinic3_bifur_fill_msg(&msg, BIFUR_DRV_CMD_MAC_GET, &cmd, sizeof(struct bifur_mac_get_cmd_msg), &rsp,
			      sizeof(struct bifur_mac_get_cmd_rsp));
	ret = hinic3_bifur_send_msg(g_func_fd, &msg);
	if (ret) {
		return ret;
	}

	memcpy(mac_addr, rsp.mac, RTE_ETHER_ADDR_LEN);
	return 0;
}