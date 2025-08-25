#!/bin/bash
set -ex

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)

update() {
	# 检查 DPDK 目录是否是 git 仓库
	if [ ! -d ".git" ]; then
		echo "初始化 git 仓..."
		git init
		git add .
		git commit -m "DPDK init"
	fi

	stashed=0
	# 判断工作区是否有未提交的更改（包括暂存区和未跟踪文件）
	git status --porcelain
	if [ -n "$(git status --porcelain)" ]; then
		echo "工作区有未提交的更改，先 stash"
		git stash push -u -m "临时保存未提交更改"
		stashed=1
	fi

	# 删除并拷贝 hinic3
	echo "更新 drivers/net/hinic3 ..."
	rm -rf "drivers/net/hinic3"
	cp -r "$SCRIPT_DIR/hinic3" "drivers/net"

	# 修改 meson.build，添加 hinic3
	if ! grep -q "'hinic3'" "./drivers/net/meson.build"; then
		echo "添加 'hinic3' 到 meson.build"
		sed -i "/'hinic'/a\\    'hinic3'," "./drivers/net/meson.build"
	fi

	# dpdk=19
	if [[ "$DPDK_VER" =~ ^19\. ]]; then
		echo "执行 DPDK 19.x 的 Makefile 修改"

		if ! grep -q "CONFIG_RTE_LIBRTE_HINIC3_PMD" "./config/common_base"; then
			sed -i "/CONFIG_RTE_LIBRTE_HINIC_PMD/a #\n#Compile burst-oriented HINIC3 PMD driver\n#\nCONFIG_RTE_LIBRTE_HINIC3_PMD=y" ./config/common_base
		fi

		if ! grep -q "hinic3" "./drivers/net/Makefile"; then
			sed -i "/CONFIG_RTE_LIBRTE_HINIC_PMD/aDIRS-\$(CONFIG_RTE_LIBRTE_HINIC3_PMD) += hinic3" ./drivers/net/Makefile
		fi

		if ! grep -q "hinic3" "./mk/rte.app.mk"; then
			sed -i "/CONFIG_RTE_LIBRTE_HINIC_PMD/a_LDLIBS-\$(CONFIG_RTE_LIBRTE_HINIC3_PMD) += -lrte_pmd_hinic3" ./mk/rte.app.mk
		fi

		echo "修改 CONFIG_RTE_BUILD_SHARED_LIB 为 y 用于生成动态库"
		if grep -q "CONFIG_RTE_BUILD_SHARED_LIB=n" "./config/common_base"; then
			sed -i "s/CONFIG_RTE_BUILD_SHARED_LIB=n/CONFIG_RTE_BUILD_SHARED_LIB=y/g" ./config/common_base
		fi
	fi

	# 添加 hinic3 并提交
	git add .
	if ! git diff --cached --quiet || ! git diff --quiet; then
		git commit -m "drivers/net: add or update hinic3 pmd"
	else
		echo "No changes to commit"
	fi

	# 如果之前 stash 了，恢复
	if [ "$stashed" -eq 1 ]; then
		echo "恢复之前 stash 的更改"
		git stash pop
	fi
}

build() {
	# 检查 meson
	if command -v meson >/dev/null 2>&1; then
		echo "meson version: $(meson --version)"
	else
		echo "错误: 未找到 meson"
		exit 1
	fi

	# 检查 ninja
	if command -v ninja >/dev/null 2>&1; then
		echo "ninja version: $(ninja --version)"
	else
		echo "错误: 未找到 ninja"
		exit 1
	fi

	build_type="$1"
	build_dir="build"

	# debug
	if [[ "$build_type" == "debug" ]]; then
		build_dir="debug"
	fi

	# dpdk=19
	if [[ "$DPDK_VER" =~ ^19\. ]]; then
		echo "开始编译 (Makefile 模式)"
		build_dir="arm64-armv8a-linuxapp-gcc"
		rm -rf $build_dir
		make config T=$build_dir
		# debug
		if [ $build_type == "debug" ]; then
			extra_cflags="EXTRA_CFLAGS="-O0 -g -DRTE_ENABLE_ASSERT""
		fi
		# release
		make install T=$build_dir $extra_cflags -j
		ls -lh $build_dir/lib/librte_pmd_hinic3*
	else
		echo "执行 Meson 构建方式"
		rm -rf $build_dir
		# dpdk>=21
		meson_flags="-Ddisable_drivers=true -Denable_drivers=mempool/ring,net/hinic3"
		# dpdk=20
		if [[ "$DPDK_VER" =~ ^20\. ]]; then
			meson_flags="-Ddisable_drivers=net/cnxk,net/mlx4,net/mlx5,common/mlx5,regex/mlx5,vdpa/mlx5,crypto/*"
		fi
		meson $build_dir $meson_flags -Dbuildtype=$build_type
		ninja -C $build_dir
		ls -lh $build_dir/drivers/librte_net_hinic3.so
		ls -lh $build_dir/drivers/librte_net_hinic3.a
	fi

	ls -lh $build_dir/app/*testpmd
	echo "Run like this: ./build/app/dpdk-testpmd -a 0000:01:00.0 -l 0-8 -- --nb-cores=8 --rxq=8 --txq=8 -i"
}

help() {
	echo "用法:"
	echo ""
	echo "1. 安装到dpdk目录"
	echo "./$0 <dpdk路径>"
	echo ""
	echo "2. 安装到dpdk目录,并编译release版本"
	echo "./$0 <dpdk路径> build"
	echo ""
	echo "3. 安装到dpdk目录,并编译debug版本"
	echo "./$0 <dpdk路径> debug"
	echo ""
	exit 0
}

if [ "$1" == "help" ]; then
	help
fi

DPDK_PATH="$1"
ACTION="$2"

if [ -z "$DPDK_PATH" ]; then
	help
	exit 1
fi

if [ -f "$DPDK_PATH" ]; then
	# 是文件，判断是不是压缩包
	case "$DPDK_PATH" in
	*.tar.gz | *.tgz)
		dirname=$(tar -tzf "$DPDK_PATH" | head -1 | cut -f1 -d"/")
		;;
	*.tar.xz)
		dirname=$(tar -tJf "$DPDK_PATH" | head -1 | cut -f1 -d"/")
		;;
	*)
		echo "错误: 不支持的压缩包格式: $DPDK_PATH"
		exit 1
		;;
	esac

	# 如果同名目录已存在，先提示或删除
	if [ -d "$dirname" ]; then
		echo "目录 $dirname 已存在！"
		exit 0
	fi

	# 解压
	tar -xf "$DPDK_PATH"

	# 更新 DPDK_PATH 为解压出的目录
	DPDK_PATH="$dirname"
fi

if [ ! -d "$DPDK_PATH" ]; then
	echo "错误: DPDK 路径不存在: $DPDK_PATH"
	exit 1
fi

cd $DPDK_PATH

# 获取 DPDK 版本
VERSION_FILE="VERSION"
if [ ! -f "$VERSION_FILE" ]; then
	echo "错误: $VERSION_FILE 不存在，可能不是 DPDK 源码目录"
	exit 1
fi
DPDK_VER=$(cat "$VERSION_FILE")
echo "检测到 DPDK 版本: $DPDK_VER"

if [ "$ACTION" == "build" ]; then
	update
	build release
elif [ "$ACTION" == "debug" ]; then
	update
	build debug
elif [[ $ACTION == exp* ]]; then
	# export
	rm -rf $SCRIPT_DIR/hinic3
	cp -r drivers/net/hinic3 $SCRIPT_DIR
else
	update
fi
