# SPDX-License-Identifier: BSD-3-Clause
# Copyright(c) 2019 Huawei Technologies Co., Ltd

include $(RTE_SDK)/mk/rte.vars.mk

# huwei secure c lib, header file path and link lib
ifeq ($(CONFIG_HUAWEI_SECUREC),y)
CFLAGS += -D__HINIC_HUAWEI_SECUREC__
CFLAGS += -I$(RTE_SDK)/huawei_secure_c/include
LDLIBS += $(RTE_SDK)/huawei_secure_c/lib/libsecurec.a
endif

#
# library name
#
LIB = librte_pmd_hinic3.a

CFLAGS += -O3

CFLAGS += -fPIC -fstack-protector-strong

EXTRA_LDFLAGS += -z,relro -z,now -z,noexecstack -s

#LDFLAGS += -fPIC

CFLAGS += $(WERROR_FLAGS)

CFLAGS += -DHW_CONVERT_ENDIAN
#perf dfx
#CFLAGS += -DHINIC3_XSTAT_PROF_RX
#CFLAGS += -DHINIC3_XSTAT_RXBUF_INFO
#CFLAGS += -DHINIC3_XSTAT_MBUF_USE
#CFLAGS += -DHINIC3_XSTAT_PROF_TX
GLOBAL_VERSION=$(shell cat $(HI1823_TRUNK_DIR)/src/GLOBAL_VERSION_NEW | grep driver | awk -F ':' '{print $$2}')
ifeq ($(CONFIG_RTE_ARCH_ARM64),y)
CFLAGS += -D__ARM64_NEON__
else ifeq ($(CONFIG_RTE_ARCH_X86_64),y)
CFLAGS += -D__X86_64_SSE__
endif
CFLAGS += -fno-strict-aliasing
CFLAGS += -D GLOBAL_VERSION_STR=\"$(GLOBAL_VERSION)\"
ifeq ($(CONFIG_OVS_VIRTIO_SWPT),y)
CFLAGS += -DOVS_VIRTIO_SWPT
CFLAGS += -DHINIC3_XSTAT_MBUF_USE
endif

CFLAGS += -Wno-cast-qual
CFLAGS += -I$(RTE_SDK)/drivers/net/hinic3/
CFLAGS += -I$(RTE_SDK)/drivers/net/hinic3/base
CFLAGS += -I$(RTE_SDK)/drivers/net/hinic3/mml
CFLAGS += -I$(RTE_SDK)/drivers/net/hinic3/htn_adapt
CFLAGS += -I$(RTE_SDK)/drivers/net/hinic3/stn/adapt

LDLIBS += -lrte_eal -lrte_mbuf -lrte_mempool -lrte_ring
LDLIBS += -lrte_ethdev -lrte_net -lrte_hash -lrte_kvargs
LDLIBS += -lrte_bus_pci
LDLIBS += -lrte_pci
LDLIBS += -lpthread

EXPORT_MAP := rte_pmd_hinic3_version.map

#
# CFLAGS for 32-bits platforms
#
ifneq ($(CONFIG_RTE_ARCH_64),y)
ifeq ($(CONFIG_RTE_TOOLCHAIN_ICC),y)
#
# CFLAGS for icc
#
CFLAGS += -diag-disable 2259
else
#
# CFLAGS for gcc
#
CFLAGS += -Wno-int-to-pointer-cast
CFLAGS += -Wno-pointer-to-int-cast
endif
endif

VPATH += $(SRCDIR)/base
VPATH += $(SRCDIR)/mml
VPATH += $(SRCDIR)/htn_adapt
VPATH += $(SRCDIR)/stn_adapt

#
# all source are stored in SRCS-y
#
SRCS-$(CONFIG_RTE_LIBRTE_HINIC3_PMD) += hinic3_pmd_bifur.c
SRCS-$(CONFIG_RTE_LIBRTE_HINIC3_PMD) += hinic3_pmd_eqs.c
SRCS-$(CONFIG_RTE_LIBRTE_HINIC3_PMD) += hinic3_pmd_hwif.c
SRCS-$(CONFIG_RTE_LIBRTE_HINIC3_PMD) += hinic3_pmd_mbox.c
SRCS-$(CONFIG_RTE_LIBRTE_HINIC3_PMD) += hinic3_pmd_mgmt.c
SRCS-$(CONFIG_RTE_LIBRTE_HINIC3_PMD) += hinic3_pmd_wq.c
SRCS-$(CONFIG_RTE_LIBRTE_HINIC3_PMD) += hinic3_pmd_cmdq.c
SRCS-$(CONFIG_RTE_LIBRTE_HINIC3_PMD) += hinic3_pmd_hw_cfg.c
SRCS-$(CONFIG_RTE_LIBRTE_HINIC3_PMD) += hinic3_pmd_hwdev.c
SRCS-$(CONFIG_RTE_LIBRTE_HINIC3_PMD) += hinic3_pmd_hw_comm.c
SRCS-$(CONFIG_RTE_LIBRTE_HINIC3_PMD) += hinic3_pmd_nic_cfg.c
SRCS-$(CONFIG_RTE_LIBRTE_HINIC3_PMD) += hinic3_pmd_nic_io.c
SRCS-$(CONFIG_RTE_LIBRTE_HINIC3_PMD) += hinic3_pmd_nic_event.c
SRCS-$(CONFIG_RTE_LIBRTE_HINIC3_PMD) += hinic3_pmd_ethdev.c
SRCS-$(CONFIG_RTE_LIBRTE_HINIC3_PMD) += hinic3_pmd_rx.c
SRCS-$(CONFIG_RTE_LIBRTE_HINIC3_PMD) += hinic3_pmd_tx.c
SRCS-$(CONFIG_RTE_LIBRTE_HINIC3_PMD) += hinic3_pmd_flow.c
SRCS-$(CONFIG_RTE_LIBRTE_HINIC3_PMD) += hinic3_pmd_fdir.c
SRCS-$(CONFIG_RTE_LIBRTE_HINIC3_PMD) += hinic3_pmd_dcb.c
SRCS-$(CONFIG_RTE_LIBRTE_HINIC3_PMD) += hinic3_pmd_tm.c
SRCS-$(CONFIG_RTE_LIBRTE_HINIC3_PMD) += hinic3_pmd_dbg.c
SRCS-$(CONFIG_RTE_LIBRTE_HINIC3_PMD) += hinic3_pmd_mml_cmd.c
SRCS-$(CONFIG_RTE_LIBRTE_HINIC3_PMD) += hinic3_pmd_mml_ioctl.c
SRCS-$(CONFIG_RTE_LIBRTE_HINIC3_PMD) += hinic3_pmd_mml_lib.c
SRCS-$(CONFIG_RTE_LIBRTE_HINIC3_PMD) += hinic3_pmd_mml_main.c
SRCS-$(CONFIG_RTE_LIBRTE_HINIC3_PMD) += hinic3_pmd_mml_queue.c
SRCS-$(CONFIG_RTE_LIBRTE_HINIC3_PMD) += hinic3_pmd_hairpin.c

SRCS-$(CONFIG_RTE_LIBRTE_HINIC3_PMD) += hinic3_pmd_cmdq_adapt.c
SRCS-$(CONFIG_RTE_LIBRTE_HINIC3_PMD) += hinic3_htn_cmdq.c
SRCS-$(CONFIG_RTE_LIBRTE_HINIC3_PMD) += hinic3_stn_cmdq.c
SRCS-$(CONFIG_RTE_LIBRTE_HINIC3_PMD) += hinic3_pmd_cmdq_enhance.c


include $(RTE_SDK)/mk/rte.lib.mk
