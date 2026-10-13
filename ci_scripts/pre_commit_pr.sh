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

# 准备 clang-tidy 编译数据库
# clang-tidy 钩子通过 -p=build 查找 compile_commands.json，而该文件由 DPDK 构建现场生成，
# 此处依据 DPDK_BUILD_DIR 在仓库根目录创建软链 build，供 clang-tidy 定位编译数据库
echo -e "\n[INFO] 准备 clang-tidy 编译数据库 (compile_commands.json)"
if [ -n "${DPDK_BUILD_DIR}" ] && [ -d "${DPDK_BUILD_DIR}" ]; then
	BUILD_DIR_ABS=$(cd "${DPDK_BUILD_DIR}" && pwd)
	if [ ! -f "${BUILD_DIR_ABS}/compile_commands.json" ]; then
		echo "[WARN] ${BUILD_DIR_ABS} 下未找到 compile_commands.json，跳过 clang-tidy 编译数据库准备"
	elif [ -e build ] && [ ! -L build ]; then
		echo "[WARN] 仓库已存在 build 目录（非软链），保留原有目录，跳过软链创建"
	else
		# 软链供 clang-tidy 定位编译数据库，创建失败不在此中断
		ln -sfn "${BUILD_DIR_ABS}" build \
			&& echo "[INFO] 已创建软链: build -> ${BUILD_DIR_ABS}" \
			|| echo "[WARN] 创建软链 build 失败，clang-tidy 可能因缺少编译数据库而失败"
	fi
else
	echo "[INFO] 未设置 DPDK_BUILD_DIR 或目录不存在，跳过 clang-tidy 编译数据库准备"
fi

# 执行 pre-commit 检查
echo -e "\n[INFO] 执行 pre-commit 检查"
echo "[COMMAND] pre-commit run --files ${FILES_ARR[*]}"
set +e
pre-commit run --show-diff-on-failure --files "${FILES_ARR[@]}"
CODE=$?
set -e

# 输出检查结果
echo -e "\n[INFO] 检查结果"
if [ ${CODE} -eq 0 ]; then
	echo "  pre-commit 检查通过。"
else
	echo "  pre-commit 检查未通过（退出码 ${CODE}），请修复以上问题后重新提交。" >&2
fi
exit "${CODE}"
