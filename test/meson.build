# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2025-2026 Huawei Technologies Co., Ltd

#hinic3 unit tests only support static library
if get_option('default_library')!='static'
    subdir_done()
endif

sources += files(
    'test_hinic3_basic.c',
    'test_hinic3_hairpin.c',
)

deps += ['ethdev', 'net_hinic3']
