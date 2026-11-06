# SPDX-License-Identifier: BSD-3-Clause
# Copyright(c) 2017 Cavium, Inc

if not is_linux or not dpdk_conf.get('RTE_ARCH_64')
    build = false
    reason = 'only supported on 64-bit Linux'
    subdir_done()
endif

includes += include_directories('../../../lib/ethdev')
includes += include_directories('../../../lib/net')
includes += include_directories('../../../lib/meter')
includes += include_directories('../../../lib/eal/include')
includes += include_directories('dependency/hidrv')
includes += include_directories('dependency/native')
includes += include_directories('../../mempool/hns5')

sources = files(
    'hns5_eth_rxtx.c',
    'hns5_eth.c',
    'hns5_poe.c',
    'hns5_ppe.c',
    'hns5_mag.c',
    'hns5_flow.c',
    'hns5_fdir.c',
    'hns5_stats.c',
    'hns5_tm.c',
    'hns5_rss.c',
    'hns5_lsw.c'
)

so_dir = join_paths(meson.source_root(), 'drivers/net/hns5/dependency/lib64')

hidrv_dep = declare_dependency(
    link_args: [
        '-L' + so_dir,
        '-lhidrv_api',
        '-Wl,-rpath,' + so_dir
    ]
)
deps += ['mbuf', 'bus_vdev', 'mempool_hns5']
ext_deps  += hidrv_dep
