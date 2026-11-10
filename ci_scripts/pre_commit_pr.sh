#!/bin/bash
set -e  #命令执行失败时异常退出
set -o pipefail  #管道命令失败时异常退出

#脚本标题

echo "========================================"
echo "    CI 增量代码 pre-commit 检查脚本     "
echo "========================================"

# 配置文件 .pre-commit-config.yaml校验
PRE_COMMIT_CONFIG_YAML=".pre-commit-config.yaml"
if [ ! -f ${PRE_COMMIT_CONFIG_YAML} ]; then
	echo "  [SUCCESS] 未找到 ${PRE_COMMIT_CONFIG_YAML}，检查终止"
	exit 0
fi

# 获取变更文件列表
echo -e "\n[INFO] 获取变更文件列表"
# 基准提交：CI 传入目标分支的合并基点(BEFORE_MERGE)，本地未传入时回退到 HEAD~1
BASE_REF="${BEFORE_MERGE:-HEAD~1}"
#shellcheck disable=SC2207
FILES_ARR=($(git diff --name-only --diff-filter=ACMR "${BASE_REF}" HEAD | sort -u))

# 3. 无变更文件直接退出
if [ ${#FILES_ARR[@]} -eq 0 ]; then
	echo "[INFO] 无变更文件，检查通过"
	exit 0
fi

# 输出变更文件信息
echo -e "\n[INFO] 变更文件数量：${#FILES_ARR[@]}"
echo "[INFO] 变更文件列表:"
for file in "${FILES_ARR[@]}"; do
	echo "  $file"
done

# 安装 pre-commit 工具
echo -e "\n[INFO] 安装 pre-commit 工具"
pip config set global.index-url https://repo.huaweicloud.com/repository/pypi/simple
pip config set global.trusted-host repo.huaweicloud.com
pip install pre-commit

export PATH="/home/jenkins/.local/bin:${PATH}"

pre-commit --version

# 定位 clang-tidy 的编译数据库与 DPDK 源码树
# compile_commands.json 由 ci_scripts/build.sh 构建现场生成，故需在构建之后执行
BUILD_DIR_ABS=""
SRC_DIR=""
if [ -n "${DPDK_BUILD_DIR}" ] && [ -d "${DPDK_BUILD_DIR}" ]; then
	BUILD_DIR_ABS="$(cd "${DPDK_BUILD_DIR}" && pwd)"
	SRC_DIR="${DPDK_SRC_DIR:-$(dirname "${BUILD_DIR_ABS}")}"
else
	echo "[INFO] 未设置 DPDK_BUILD_DIR 或目录不存在，本次不执行 clang-tidy"
fi

# 执行 pre-commit 检查
echo -e "\n[INFO] 执行 pre-commit 检查"
echo "[COMMAND] pre-commit run --files ${FILES_ARR[*]}"
set +e
pre-commit run --show-diff-on-failure --files "${FILES_ARR[@]}"
CODE=$?
set -e

# ---------- clang-tidy 静态分析（方案 A：路径对齐）----------
# 本仓库通过 install.sh 把 hinic3/ 注入到 DPDK 的 drivers/net/hinic3/、把 test/ 注入到
# app/test/test_hinic3/，编译数据库中的路径均为 DPDK 树路径。若直接以仓库路径调用
# clang-tidy，会因 "Compile command not found" 被静默跳过；故此处先把变更文件映射为
# DPDK 树路径，再进入 DPDK 源码树执行分析。
TIDY_CODE=0
echo -e "\n[INFO] clang-tidy 静态分析"

if [ -z "${BUILD_DIR_ABS}" ] || [ ! -f "${BUILD_DIR_ABS}/compile_commands.json" ]; then
	echo "[INFO] 缺少编译数据库，跳过 clang-tidy"
elif [ ! -d "${SRC_DIR}/drivers/net/hinic3" ]; then
	echo "[INFO] 未找到注入 hinic3 的 DPDK 源码树（${SRC_DIR}），跳过 clang-tidy"
else
	# 仓库路径 -> DPDK 树路径
	TIDY_FILES=()
	for file in "${FILES_ARR[@]}"; do
		case "${file}" in
		hinic3/*.c) mapped="drivers/net/hinic3/${file#hinic3/}" ;;
		test/*.c) mapped="app/test/test_hinic3/${file#test/}" ;;
		*) continue ;;
		esac
		if [ -f "${SRC_DIR}/${mapped}" ]; then
			TIDY_FILES+=("${mapped}")
		else
			echo "[WARN] ${file} 在 DPDK 树中不存在（${mapped}），跳过"
		fi
	done

	if [ ${#TIDY_FILES[@]} -eq 0 ]; then
		echo "[INFO] 无需静态分析的 C 文件"
	else
		# clang-tidy 可执行文件：环境变量 CLANG_TIDY > 系统 PATH > pip 安装（与钩子同版本）
		CLANG_TIDY_BIN="${CLANG_TIDY:-$(command -v clang-tidy || true)}"
		if [ -z "${CLANG_TIDY_BIN}" ]; then
			echo "[INFO] 未找到 clang-tidy，安装 clang-tidy==${CLANG_TIDY_VERSION:-21.1.0}"
			pip install "clang-tidy==${CLANG_TIDY_VERSION:-21.1.0}"
			CLANG_TIDY_BIN="$(command -v clang-tidy)"
		fi
		# clang-tidy 默认 checks 只有 clang-diagnostic-*（编译诊断），不做静态分析，
		# 且默认只有 error 才返回非 0。此处显式启用 clang-analyzer-*（空指针解引用、
		# 越界、资源泄漏等），并让静态分析告警直接判定失败。
		# 排除 DeprecatedOrUnsafeBufferHandling：它要求用 C11 的 memset_s/memcpy_s，
		# 会把 DPDK 通篇使用的 memset/memcpy 一律判为不安全，误拦既有正常代码。
		TIDY_CHECKS="${TIDY_CHECKS:--*,clang-diagnostic-*,clang-analyzer-*,-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling}"
		TIDY_WARN_AS_ERRORS="${TIDY_WARN_AS_ERRORS:-clang-analyzer-*}"
		# 编译数据库由 GCC 生成，含 -Wno-packed-not-aligned 等 GCC 专有开关，
		# 追加该参数让 clang 忽略不认识的告警开关
		TIDY_EXTRA_ARG="--extra-arg=-Wno-unknown-warning-option"
		echo "[COMMAND] cd ${SRC_DIR} && ${CLANG_TIDY_BIN} -p=${BUILD_DIR_ABS} --checks=${TIDY_CHECKS} --warnings-as-errors=${TIDY_WARN_AS_ERRORS} ${TIDY_FILES[*]}"

		set +e
		(cd "${SRC_DIR}" && "${CLANG_TIDY_BIN}" "-p=${BUILD_DIR_ABS}" \
			"--checks=${TIDY_CHECKS}" \
			"--warnings-as-errors=${TIDY_WARN_AS_ERRORS}" \
			"${TIDY_EXTRA_ARG}" \
			"${TIDY_FILES[@]}")
		TIDY_CODE=$?
		set -e
	fi
fi

# 输出检查结果
echo -e "\n[INFO] 检查结果"
if [ ${CODE} -eq 0 ]; then
	echo "  pre-commit 检查通过。"
else
	echo "  pre-commit 检查未通过（退出码 ${CODE}），请修复以上问题后重新提交。" >&2
fi
if [ ${TIDY_CODE} -eq 0 ]; then
	echo "  clang-tidy 检查通过。"
else
	echo "  clang-tidy 检查未通过（退出码 ${TIDY_CODE}），请修复以上问题后重新提交。" >&2
fi

# 任一检查未通过即失败
if [ ${CODE} -ne 0 ]; then
	exit "${CODE}"
fi
exit "${TIDY_CODE}"
