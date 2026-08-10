# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2025-2026 Huawei Technologies Co., Ltd


sources = [
	'hinic3_htn_cmdq.c',
]

extra_flags = []
# The driver runs only on arch64 machine, remove 32bit warnings
if not dpdk_conf.get('RTE_ARCH_64')
        extra_flags += ['-Wno-int-to-pointer-cast', '-Wno-pointer-to-int-cast']
endif

foreach flag: extra_flags
	if cc.has_argument(flag)
		cflags += flag
	endif
endforeach

deps += ['hash']
c_args = cflags
includes += include_directories('.')
includes += include_directories('../base/')

htn_lib = static_library('spnic_base', sources,
	dependencies: [static_rte_eal, static_rte_ethdev, static_rte_bus_pci, static_rte_hash],
	include_directories : includes,
	c_args: c_args)
htn_objs = htn_lib.extract_all_objects()