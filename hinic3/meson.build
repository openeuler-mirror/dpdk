# SPDX-License-Identifier: BSD-3-Clause
# Copyright(c) 2021 Huawei Technologies Co., Ltd

if not is_linux
	build = false
	reason = 'only supported on Linux'
	subdir_done()
endif

if not dpdk_conf.get('RTE_ARCH_64')
	build = false
	reason = 'only supported on 64-bit architectures'
	subdir_done()
endif

cflags += ['-DHW_CONVERT_ENDIAN']
cflags += ['-D__HINIC_HUAWEI_SECUREC__']
cflags += ['-fPIC']
cflags += ['-fstack-protector-strong']

dpdk_version = meson.project_version()
if dpdk_version.substring(0,2) == '20'
	cflags += ['-DDPDK_20_11']
elif dpdk_version.substring(0,2) == '21'
	cflags += ['-DDPDK_20_11']
	cflags += ['-DDPDK_21_11']
elif dpdk_version.substring(0,2) == '22'
	cflags += ['-DDPDK_20_11']
	cflags += ['-DDPDK_21_11']
	cflags += ['-DDPDK_22_11']
elif dpdk_version.substring(0,2) == '23'
	cflags += ['-DDPDK_20_11']
	cflags += ['-DDPDK_21_11']
	cflags += ['-DDPDK_22_11']
elif dpdk_version.substring(0,2) == '24'
	cflags += ['-DDPDK_20_11']
	cflags += ['-DDPDK_21_11']
	cflags += ['-DDPDK_22_11']
	cflags += ['-DDPDK_24_11']
elif dpdk_version.substring(0,2) == '25'
	cflags += ['-DDPDK_20_11']
	cflags += ['-DDPDK_21_11']
	cflags += ['-DDPDK_22_11']
	cflags += ['-DDPDK_24_11']
	cflags += ['-DDPDK_25_11']
endif

subdir('base')
subdir('mml')
subdir('stn')
 objs = [base_objs] + [mml_objs] + [stn_objs]

sources = files(
	'hinic3_pmd_ethdev.c',
	'hinic3_pmd_rx.c',
	'hinic3_pmd_tx.c',
 	'hinic3_pmd_flow.c',
	'hinic3_pmd_nic_io.c',
	'hinic3_pmd_fdir.c',
	'hinic3_pmd_bifur.c',
	'hinic3_pmd_dcb.c',
	'hinic3_pmd_tm.c',
	'hinic3_pmd_telemetry.c',
	'hinic3_pmd_hairpin.c',
	'hinic3_pmd_flow_sec.c',
)

if arch_subdir == 'arm' and dpdk_conf.get('RTE_ARCH_64')
	cflags  += ['-D__ARM64_NEON__']
elif arch_subdir == 'x86' and dpdk_conf.get('RTE_ARCH_64')
	cflags += ['-D__X86_64_SSE__']
endif

includes += include_directories('base')
includes += include_directories('mml')
