# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026-2027 Huawei Technologies Co., Ltd

if not is_linux or not dpdk_conf.get('RTE_ARCH_64')
    build = false
    reason = 'only supported on 64-bit Linux'
    subdir_done()
endif

includes += include_directories('../../../lib/librte_ethdev')
includes += include_directories('../../../lib/librte_net')
includes += include_directories('../../../lib/librte_meter')
includes += include_directories('../../../lib/librte_eal/include')
includes += include_directories('../../net/hns5/dependency/hidrv')
includes += include_directories('../../net/hns5/dependency/native')

sources = files(
    'hns5_mempool.c',
)

so_dir = join_paths(meson.source_root(), 'drivers/net/hns5/dependency/lib64')

hidrv_dep = declare_dependency(
    link_args: [
        '-L' + so_dir,
        '-lhidrv_api',
        '-Wl,-rpath,' + so_dir
    ]
)
deps += ['mbuf', 'bus_pci']
# Only add the dependency object; do not mix in strings
ext_deps  += hidrv_dep
