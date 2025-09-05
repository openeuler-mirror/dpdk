#!/bin/bash
set -e

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
BIFUR=0

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
		sed -i "/'hinic'/a\\	'hinic3'," "./drivers/net/meson.build"
	fi

	# dpdk>=22
	if [ "$DPDK_MAJOR" -ge 22 ]; then
		echo "DPDK>=22,删除 version.map 文件以避免编译报错"
		rm -f "./drivers/net/hinic3/version.map"
	fi

	# 如果传了 bifur 参数，则修改 hinic3/meson.build
	if [ "$BIFUR" -eq 1 ]; then
		meson_file="./drivers/net/hinic3/meson.build"
		if ! grep -Fq "cflags += ['-DHINIC3_TRAFFIC_BIFUR']" "$meson_file"; then
			echo "为 hinic3 添加 -DHINIC3_TRAFFIC_BIFUR cflag"
			sed -i "/cflags += \['-fstack-protector-strong'\]/a cflags += ['-DHINIC3_TRAFFIC_BIFUR']" "$meson_file"
		else
			echo "已存在 -DHINIC3_TRAFFIC_BIFUR"
		fi
	fi

	# dpdk=19
	if [ "$DPDK_MAJOR" -eq 19 ]; then
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

	build_type="$1"   # release 或 debug
	build_target="$2" # 可为空或 generic
	build_dir="build"

	# debug
	if [[ "$build_type" == "debug" ]]; then
		build_dir="debug"
	fi

	# dpdk=19 用 Makefile
	if [ "$DPDK_MAJOR" -eq 19 ]; then
		echo "开始编译 (Makefile 模式)"
		build_dir="arm64-armv8a-linuxapp-gcc"
		rm -rf $build_dir
		make config T=$build_dir
		# debug
		if [ "$build_type" == "debug" ]; then
			extra_cflags="EXTRA_CFLAGS=-O0 -g -DRTE_ENABLE_ASSERT"
		fi
		# release
		make install T=$build_dir $extra_cflags -j
		ls -lh $build_dir/lib/librte_pmd_hinic3*
		ls -lh $build_dir/app/*testpmd
		echo "Run like this: ./$build_dir/app/testpmd -a 0000:01:00.0 -l 0-8 -- --nb-cores=8 --rxq=8 --txq=8 -i"
	else
		echo "执行 Meson 构建方式"
		rm -rf $build_dir
		# dpdk>=21
		meson_flags="-Ddisable_drivers=true -Denable_drivers=mempool/ring,net/hinic3"
		# dpdk=20
		if [ "$DPDK_MAJOR" -eq 20 ]; then
			meson_flags="-Ddisable_drivers=net/cnxk,net/mlx4,net/mlx5,common/mlx5,regex/mlx5,vdpa/mlx5,crypto/*"
		fi
		# 如果指定 generic
		if [[ "$build_target" == "generic" ]]; then
			meson_flags="$meson_flags -Dplatform=generic"
		fi
		meson $build_dir $meson_flags -Dbuildtype=$build_type
		ninja -C $build_dir
		ls -lh $build_dir/drivers/librte_net_hinic3.so
		ls -lh $build_dir/drivers/librte_net_hinic3.a
		ls -lh $build_dir/app/*testpmd
		echo "Run like this: ./$build_dir/app/dpdk-testpmd -a 0000:01:00.0 -l 0-8 -- --nb-cores=8 --rxq=8 --txq=8 -i"
	fi
}

help() {
	echo "用法:"
	echo ""
	echo "1. 安装 hinic3 到 DPDK 目录:"
	echo "   $0 <dpdk路径>"
	echo ""
	echo "2. 安装 hinic3 并启用 bifur:"
	echo "   $0 <dpdk路径> bifur"
	echo ""
	echo "3. 编译 release 版本:"
	echo "   $0 <dpdk路径> build"
	echo ""
	echo "4. 编译 debug 版本:"
	echo "   $0 <dpdk路径> debug"
	echo ""
	echo "5. DPU 场景编译 (release):"
	echo "   $0 <dpdk路径> build generic"
	echo ""
	echo "示例:"
	echo "   $0 ../dpdk-stable-21.11.9 build"
	echo "   $0 ../dpdk-stable-21.11.9"
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
	# 获取压缩包所在目录（绝对路径）
	pkg_dir=$(dirname "$DPDK_PATH")
	pkg_dir=$(cd "$pkg_dir" && pwd)

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

	# 如果同名目录已存在，先提示或退出
	if [ -d "$pkg_dir/$dirname" ]; then
		echo "目录 $pkg_dir/$dirname 已存在！"
		exit 0
	fi

	# 解压到压缩包所在目录
	tar -xf "$DPDK_PATH" -C "$pkg_dir"

	# 更新 DPDK_PATH 为解压出的目录
	DPDK_PATH="$pkg_dir/$dirname"
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
DPDK_MAJOR=${DPDK_VER%%.*}

if [ "$ACTION" == "bifur" ]; then
	BIFUR=1
	update
elif [ "$ACTION" == "build" ]; then
	build release
elif [ "$ACTION" == "debug" ]; then
	build debug
elif [ "$ACTION" == "build" ] && [ "$3" == "generic" ]; then
	build release generic
elif [ "$ACTION" == "debug" ] && [ "$3" == "generic" ]; then
	build debug generic
elif [[ $ACTION == exp* ]]; then
	# export
	rm -rf $SCRIPT_DIR/hinic3
	cp -r drivers/net/hinic3 $SCRIPT_DIR
else
	update
fi
