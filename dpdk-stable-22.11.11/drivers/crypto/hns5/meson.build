# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026-2027 Huawei Technologies Co., Ltd

if not is_linux
    build = false
    reason = 'only supported on Linux'
    subdir_done()
endif

deps += ['bus_vdev']

includes += include_directories('../../../lib/eal/include')
includes += include_directories('../../net/hns5/dependency/hidrv')
includes += include_directories('../../net/hns5/dependency/native')

sources = files(
        'hns5_crypto_log.c',
        'hns5_crypto_pmd.c',
        'hns5_crypto_capabilities.c',
        'hns5_crypto_runtime.c',
        'hns5_sec_platform.c',
        'hns5_sec_bd.c',
        'hns5_sec_complete.c',
        'hns5_crypto_session.c',
        'hns5_crypto_enqdeq.c',
)

includes += include_directories('.')

so_dir = join_paths(meson.source_root(), 'drivers/net/hns5/dependency/lib64')

hidrv_dep = declare_dependency(
    link_args: [
        '-L' + so_dir,
        '-lhidrv_api',
        '-Wl,-rpath,' + so_dir
    ]
)
ext_deps += hidrv_dep
