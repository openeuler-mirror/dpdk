#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
#
# CI 编译检查：完整编译 hns5 所在的 DPDK 树，并检测编译器告警。
#
# 唯一开关：
#   BUILD=0（默认）  跳过编译。没有 BSP SDK 的机器、或只想跑 pre-commit 文件
#                    检查时用；此时不产出编译数据库，pre_commit_pr.sh 会随之
#                    跳过 clang-tidy
#   BUILD=1          完整编译并检测告警：只要有编译器告警就判失败（退出码 2）。
#                    CI 上备有 BSP SDK 制品时应显式设 BUILD=1
#
# 完整编译委托给 buildtools/hns5_compile.sh：meson 参数、SDK 头文件解包与
# 桩库生成只在那一处维护，本脚本不重复实现，避免两边漂移。
#
# SDK 位置（仅 BUILD=1 需要，满足任一即可）：
#   bash ci_scripts/build.sh /path/S920L18_SDK_004.zip
#   SDK_ZIP=/path/... 或 SDK_DIR=/已解压的 SDK 根
#   或把 S920L18_SDK_004.zip 放在源码树目录、仓库根、仓库上一级
#
# 其它可选项：DPDK_VERSION=22.11.11（默认）、BUILD_TYPE=release|debug
#
# 退出码：0 通过 / 1 编译失败 / 2 编译通过但有告警 / 3 前置条件不满足
# 结束时打印一行 CI_RESULT 供流水线检索：
#   CI_RESULT: build=ok compiler_warnings=0 errors=0

set -e
set -o pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(dirname "${SCRIPT_DIR}")

DPDK_VERSION="${DPDK_VERSION:-22.11.11}"
BUILD="${BUILD:-0}"
BUILD_TYPE="${BUILD_TYPE:-release}"
SDK_NAME="S920L18_SDK_004.zip"
RESOLVED_SDK=""

say() { echo "[INFO] $*"; }
warn() { echo "[WARN] $*" >&2; }
die() {
	echo "[ERROR] $*" >&2
	exit "${2:-3}"
}

# 编译器/链接器诊断行：路径.c:行:列: warning:/error:、ld: warning:、
# undefined reference。只统计这些，meson 的 "supports arguments -Wdeprecated:
# YES" 之类探测输出与 NOTICE 行不算告警。
DIAG_RE='^[^[:space:]]+\.(c|h|cpp):[0-9]+:[0-9]+: (warning|error):|[a-z]+: warning:|undefined reference to'

# ---------- 0. 构建依赖（缺就装；CI 镜像未必预装）----------
export PATH="${HOME}/.local/bin:${PATH}"
MISSING=""
for t in meson ninja pkg-config nm python3; do
	command -v "${t}" >/dev/null 2>&1 || MISSING="${MISSING} ${t}"
done
python3 -c 'import elftools' >/dev/null 2>&1 || MISSING="${MISSING} pyelftools"
if [ -n "${MISSING}" ]; then
	say "缺少构建依赖:${MISSING}，尝试安装 ..."
	pip config set global.index-url https://repo.huaweicloud.com/repository/pypi/simple \
		>/dev/null 2>&1 || true
	pip config set global.trusted-host repo.huaweicloud.com >/dev/null 2>&1 || true
	if command -v dnf >/dev/null 2>&1 && sudo -n true 2>/dev/null; then
		sudo dnf install -y meson ninja-build pkgconf python3-pyelftools || \
			pip install --user meson ninja pyelftools
	else
		pip install --user meson ninja pyelftools
	fi
fi

# ---------- 1. 选择源码树 ----------
tree_dir_of() {
	case "$1" in
	22.11.11) echo "${REPO_ROOT}/dpdk-stable-22.11.11" ;;
	20.11 | 20.11.10)
		# 20.11 基线目前是 patch 形式（DPDK_20.11_patch/）；接入 CI 时
		# 在此展开成可编译的源码树并返回其路径
		die "20.11 基线尚未接入 CI 构建"
		;;
	*) die "不支持的 DPDK_VERSION=$1" ;;
	esac
}

SRC_DIR=$(tree_dir_of "${DPDK_VERSION}")
[ -d "${SRC_DIR}" ] || die "找不到源码树 ${SRC_DIR}"
COMPILER="${SRC_DIR}/buildtools/hns5_compile.sh"
[ -f "${COMPILER}" ] || die "找不到编译脚本 ${COMPILER}"
BUILD_DIR_ABS="${SRC_DIR}/build"

# ---------- 2. BUILD=0：跳过编译 ----------
if [ "${BUILD}" = "0" ]; then
	say "BUILD=0，跳过编译；不产出编译数据库，clang-tidy 将被跳过"
	echo "[INFO] DPDK_SRC_DIR=${SRC_DIR}"
	echo "[INFO] DPDK_BUILD_DIR="
	echo "CI_RESULT: build=skipped compiler_warnings=0 errors=0"
	exit 0
fi

# ---------- 3. 定位 BSP SDK ----------
find_sdk() {
	if [ -n "${SDK_DIR:-}" ] && [ -d "${SDK_DIR:-}" ]; then
		RESOLVED_SDK="${SDK_DIR}"
		return 0
	fi
	local cand
	for cand in "${1:-}" "${SDK_ZIP:-}" \
		"${SRC_DIR}/${SDK_NAME}" "${REPO_ROOT}/${SDK_NAME}" \
		"${REPO_ROOT}/../${SDK_NAME}"; do
		[ -n "${cand}" ] || continue
		if [ -f "${cand}" ]; then
			RESOLVED_SDK=$(cd "$(dirname "${cand}")" && pwd)/$(basename "${cand}")
			return 0
		fi
	done
	return 1
}

if ! find_sdk "${1:-}"; then
	die "未找到 BSP SDK（${SDK_NAME}）。查找位置：命令行参数、\$SDK_ZIP、\$SDK_DIR、
  源码树目录、仓库根及其上一级。确实不需要编译时请用 BUILD=0。"
fi

say "源码树: ${SRC_DIR}"
say "SDK:    ${RESOLVED_SDK}"
say "构建类型: ${BUILD_TYPE}"

# ---------- 4. 编译（输出全程进控制台，同时留一份用于统计告警）----------
LOG="${TMPDIR:-/tmp}/hns5_ci_build_$(date +%Y%m%d_%H%M%S).log"
say "开始编译（输出直接进控制台；日志: ${LOG}）"

BUILD_RC=0
set +e
if [ -d "${RESOLVED_SDK}" ]; then
	if [ "${BUILD_TYPE}" = "debug" ]; then
		SDK_DIR="${RESOLVED_SDK}" bash "${COMPILER}" --debug 2>&1 | tee "${LOG}"
	else
		SDK_DIR="${RESOLVED_SDK}" bash "${COMPILER}" 2>&1 | tee "${LOG}"
	fi
else
	if [ "${BUILD_TYPE}" = "debug" ]; then
		bash "${COMPILER}" --debug "${RESOLVED_SDK}" 2>&1 | tee "${LOG}"
	else
		bash "${COMPILER}" "${RESOLVED_SDK}" 2>&1 | tee "${LOG}"
	fi
fi
BUILD_RC=${PIPESTATUS[0]}
set -e
say "编译退出码: ${BUILD_RC}"

# ---------- 5. 统计告警与错误（编译过就一定检测）----------
diags=$(grep -E "${DIAG_RE}" "${LOG}" 2>/dev/null || true)
WARNS=$(printf '%s\n' "${diags}" | grep -E ': warning:' | grep -c . || true)
WARNS=${WARNS:-0}
ERRS=$(printf '%s\n' "${diags}" | grep -E ': error:|undefined reference' |
	grep -c . || true)
ERRS=${ERRS:-0}

if [ "${WARNS}" != 0 ]; then
	warn "检出 ${WARNS} 条编译告警："
	printf '%s\n' "${diags}" | grep -E ': warning:' | sed 's/^/    /' | head -20
fi
if [ "${ERRS}" != 0 ]; then
	warn "检出 ${ERRS} 条编译/链接错误："
	printf '%s\n' "${diags}" | grep -E ': error:|undefined reference' |
		sed 's/^/    /' | head -20
fi

if [ ! -f "${BUILD_DIR_ABS}/compile_commands.json" ]; then
	warn "未找到编译数据库 ${BUILD_DIR_ABS}/compile_commands.json"
else
	say "编译数据库: ${BUILD_DIR_ABS}/compile_commands.json"
fi

echo "[INFO] DPDK_SRC_DIR=${SRC_DIR}"
echo "[INFO] DPDK_BUILD_DIR=${BUILD_DIR_ABS}"
echo "CI_RESULT: build=$(
	[ "${BUILD_RC}" = 0 ] && echo ok || echo failed) \
compiler_warnings=${WARNS} errors=${ERRS}"

if [ "${BUILD_RC}" != 0 ]; then
	say "判定：编译失败（完整日志: ${LOG}）"
	exit 1
fi
if [ "${ERRS}" != 0 ]; then
	say "判定：有编译错误"
	exit 1
fi
if [ "${WARNS}" != 0 ]; then
	say "判定：有编译告警"
	exit 2
fi
say "判定：通过 ✓（无告警）"
exit 0
