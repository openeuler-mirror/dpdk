#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
#
# 用途：为 clang-tidy 静态分析准备 DPDK 编译数据库（compile_commands.json）。
#       结构参考 test.sh：DPDK 源码包放在仓库的上一级目录（..），
#       解压后调用 install.sh 注入 hinic3 驱动，再用 meson 生成编译数据库。
#
# 模式（SETUP_ONLY）：
#   SETUP_ONLY=1（默认）：只执行 meson setup，不跑 ninja —— 最快拿到编译数据库
#   SETUP_ONLY=0        ：完整编译（meson + ninja），同时产出驱动库
#
# 用法：
#   bash ci_scripts/build.sh
#   SETUP_ONLY=0 bash ci_scripts/build.sh
#   DPDK_VERSION=22.11.11 bash ci_scripts/build.sh

set -e

# ---------- 0. 构建依赖检查（缺 meson/ninja/pyelftools 时自动安装）----------
# CI 镜像未必预装构建依赖；容器内有免密 sudo 时优先用 dnf，否则 pip 装到 ~/.local/bin
export PATH="${HOME}/.local/bin:${PATH}"
MISSING=""
command -v meson >/dev/null 2>&1 || MISSING="${MISSING} meson"
command -v ninja >/dev/null 2>&1 || MISSING="${MISSING} ninja"
python3 -c 'import elftools' >/dev/null 2>&1 || MISSING="${MISSING} pyelftools"
if [ -n "${MISSING}" ]; then
	echo "[INFO] 缺少构建依赖:${MISSING}，尝试安装 ..."
	if command -v dnf >/dev/null 2>&1 && sudo -n true 2>/dev/null; then
		sudo dnf install -y meson ninja-build python3-pyelftools
	else
		pip config set global.index-url https://repo.huaweicloud.com/repository/pypi/simple
		pip config set global.trusted-host repo.huaweicloud.com
		pip install --user meson ninja pyelftools
	fi
fi

# ---------- 路径（参考 test.sh）----------
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
if [ -f "${SCRIPT_DIR}/install.sh" ]; then
	REPO_ROOT="${SCRIPT_DIR}"
else
	REPO_ROOT="$(dirname "${SCRIPT_DIR}")"
fi
DPDK_DIR="${DPDK_DIR:-$(dirname "${REPO_ROOT}")}"    # DPDK 源码包所在目录（仓库上一级）

# ---------- 可配置项（均可用环境变量覆盖）----------
DPDK_VERSION="${DPDK_VERSION:-21.11.9}"
DPDK_TARBALL="dpdk-${DPDK_VERSION}.tar.xz"
SETUP_ONLY="${SETUP_ONLY:-1}"                        # 1=只 meson setup（默认）；0=完整编译
BUILD_TYPE="${BUILD_TYPE:-release}"                  # meson 构建类型：release / debug（同时决定构建目录名）

# ---------- 1. 准备 DPDK 源码树（参考 test.sh）----------
cd "${DPDK_DIR}"
if [ ! -f "${DPDK_TARBALL}" ]; then
	DPDK_URL="${DPDK_URL:-https://fast.dpdk.org/rel/${DPDK_TARBALL}}"
	echo "[INFO] 本地无 ${DPDK_TARBALL}，尝试下载: ${DPDK_URL}"
	wget "${DPDK_URL}"
fi

stable_dir="$(tar -tJf "${DPDK_TARBALL}" | head -1 | cut -f1 -d'/')"
rm -rf "${stable_dir}"
tar -xf "${DPDK_TARBALL}"
echo "[INFO] DPDK 源码树: ${DPDK_DIR}/${stable_dir}"

# 主版本号（读 VERSION，与 install.sh 一致）
DPDK_MAJOR="$(cut -d. -f1 "${stable_dir}/VERSION" 2>/dev/null || true)"

# DPDK 19 用 make 构建，不产出 compile_commands.json，无法只 setup
if [ -n "${DPDK_MAJOR}" ] && [ "${DPDK_MAJOR}" -lt 20 ]; then
	echo "[WARN] 检测到 DPDK ${DPDK_MAJOR}（make 构建，无 compile_commands.json），改用完整编译"
	SETUP_ONLY=0
fi

# ---------- 2. 注入 hinic3 驱动（参考 test.sh：DISABLE_WNO_ERROR=1）----------
export DISABLE_WNO_ERROR=1
sh "${REPO_ROOT}/install.sh" "${stable_dir}" install

# ---------- 3. 生成编译数据库 ----------
# 构建目录名与 install.sh 保持一致：debug → debug，其余 → build
build_dir="build"
if [ "${BUILD_TYPE}" = "debug" ]; then
	build_dir="debug"
fi
BUILD_DIR="${DPDK_DIR}/${stable_dir}/${build_dir}"

if [ "${SETUP_ONLY}" = "1" ]; then
	# 只产出编译数据库、不编译产物，故此处不带 install.sh 的 -Dwerror=true：
	# 数据库由 GCC 生成，含 -Wno-packed-not-aligned 等 GCC 专有开关，clang 不认识；
	# 一旦带 -Werror，这些未知开关会被 clang-tidy 报成 error，导致检查必然失败。
	# 其余参数与 install.sh 的 build 保持一致
	if [ "${DPDK_MAJOR}" = "20" ]; then
		MESON_FLAGS="-Ddisable_drivers=net/cnxk,net/mlx4,net/mlx5,common/mlx5,regex/mlx5,vdpa/mlx5,crypto/*"
	else
		MESON_FLAGS="-Ddisable_drivers=true -Denable_drivers=mempool/ring,net/hns3,net/hinic3"
	fi
	echo "[INFO] 只生成编译数据库（SETUP_ONLY=1，跳过 ninja）..."
	cd "${stable_dir}"
	rm -rf "${build_dir}"
	meson "${build_dir}" ${MESON_FLAGS} -Dbuildtype="${BUILD_TYPE}"
	cd "${DPDK_DIR}"
else
	# install.sh 的 ACTION 只认 install/build/debug/test/export，release 不是合法值
	if [ "${BUILD_TYPE}" = "debug" ]; then
		BUILD_ACTION="debug"
	else
		BUILD_ACTION="build"
	fi
	echo "[INFO] 完整编译（meson + ninja）..."
	if ! sh "${REPO_ROOT}/install.sh" "${stable_dir}" "${BUILD_ACTION}"; then
		echo "[WARN] 默认构建失败，尝试 generic 配置 ..."
		sh "${REPO_ROOT}/install.sh" "${stable_dir}" "${BUILD_ACTION}" generic
	fi
fi

# ---------- 4. 输出产物路径（供 clang-tidy 使用）----------
echo
echo "==== 完成 ===="
if [ -f "${BUILD_DIR}/compile_commands.json" ]; then
	echo "[OK]   编译数据库: ${BUILD_DIR}/compile_commands.json"
else
	echo "[WARN] 未找到 compile_commands.json（DPDK <= 19 用 make 构建时不生成）"
fi
echo "[INFO] DPDK_BUILD_DIR=${BUILD_DIR}"
