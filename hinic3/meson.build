# SPDX-License-Identifier: BSD-3-Clause
# Copyright(c) 2021 Huawei Technologies Co., Ltd

if not is_linux
        build = false
        reason = 'only supported on Linux'
        subdir_done()
endif

if arch_subdir != 'x86' and arch_subdir != 'arm' or not dpdk_conf.get('RTE_ARCH_64')
        build = false
        reason = 'only supported on x86_64 and aarch64'
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
endif

subdir('base')
subdir('mml')
objs = [base_objs] + [mml_objs]

sources = files(
	'hinic3_pmd_ethdev.c',
	'hinic3_pmd_rx.c',
	'hinic3_pmd_tx.c',
 	'hinic3_pmd_flow.c',
	'hinic3_pmd_nic_io.c',
	'hinic3_pmd_fdir.c',
	)

if arch_subdir == 'arm' and dpdk_conf.get('RTE_ARCH_64')
cflags  += ['-D__ARM64_NEON__']
else
cflags += ['-D__X86_64_SSE__']
endif

includes += include_directories('base')
includes += include_directories('mml')
