#!/bin/bash
set -e

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)

# 适配低版本 meson
function meson_build_adapt() {
	MESON_FILE="./drivers/net/hinic3/meson.build"

	# 删除原有 dpdk_version 判断整个段落
	sed -i '/^dpdk_version = meson.project_version()/,/^endif$/d' $MESON_FILE

	# 定义不同版本的 cflags
	declare -A FLAGS
	FLAGS[20]="-DDPDK_20_11"
	FLAGS[21]="-DDPDK_20_11 -DDPDK_21_11"
	FLAGS[22]="-DDPDK_20_11 -DDPDK_21_11 -DDPDK_22_11"
	FLAGS[23]="-DDPDK_20_11 -DDPDK_21_11 -DDPDK_22_11"
	FLAGS[24]="-DDPDK_20_11 -DDPDK_21_11 -DDPDK_22_11 -DDPDK_24_11"
	FLAGS[25]="-DDPDK_20_11 -DDPDK_21_11 -DDPDK_22_11 -DDPDK_24_11 -DDPDK_25_11"

	# 如果有对应的 flags 就写入 meson.build
	for flag in ${FLAGS[$DPDK_MAJOR]}; do
		line="cflags += ['$flag']"
		# 判读不存在执行
		if ! grep -Fq "$line" "$MESON_FILE"; then
			sed -i "/cflags += \['-fstack-protector-strong'\]/a $line" "$MESON_FILE"
		fi
	done
}

# 检查并配置 git 用户信息
check_git() {
	# 检查是否安装了 git
	if ! command -v git >/dev/null 2>&1; then
		echo "错误：未检测到 git,请先安装。"
		exit 1
	fi

	# 如果 user.name 未配置，则设置为 root
	if [ -z "$(git config --global user.name)" ]; then
		echo "Git user.name 未配置，正在设置为 root ..."
		git config --global user.name "root"
	fi
	# 如果 user.email 未配置，则设置为 root@localhost.localdomain
	if [ -z "$(git config --global user.email)" ]; then
		echo "Git user.email 未配置，正在设置为 root@localhost.localdomain ..."
		git config --global user.email "root@localhost.localdomain"
	fi
	echo "Git 用户配置检查完成。"

	# 检查 DPDK 目录是否是 git 仓库
	if [ ! -d ".git" ]; then
		echo "初始化 git 仓..."
		git init
		git add .
		git commit -m "DPDK init"
	fi
}

config_dpdk_19() {
	echo "Disable selected DPDK 19 PMDs in defconfig files."

	make_config=(
		"config/defconfig_arm64-armv8a-linuxapp-gcc"
		"config/defconfig_x86_64-native-linuxapp-gcc"
	)

	disabled_pmds=(
		CONFIG_RTE_LIBRTE_PMD_AF_PACKET
		CONFIG_RTE_LIBRTE_PMD_AF_XDP
		CONFIG_RTE_LIBRTE_ARK_PMD
		CONFIG_RTE_LIBRTE_ATLANTIC_PMD
		CONFIG_RTE_LIBRTE_AVP_PMD
		CONFIG_RTE_LIBRTE_AXGBE_PMD
		CONFIG_RTE_LIBRTE_BNX2X_PMD
		CONFIG_RTE_LIBRTE_CXGBE_PMD
		CONFIG_RTE_LIBRTE_DPAA_BUS
		CONFIG_RTE_LIBRTE_DPAA_PMD
		CONFIG_RTE_LIBRTE_DPAA_MEMPOOL
		CONFIG_RTE_LIBRTE_PMD_DPAA_SEC
		CONFIG_RTE_LIBRTE_PMD_DPAA_EVENTDEV
		CONFIG_RTE_LIBRTE_FSLMC_BUS
		CONFIG_RTE_LIBRTE_DPAA2_PMD
		CONFIG_RTE_LIBRTE_DPAA2_MEMPOOL
		CONFIG_RTE_LIBRTE_DPAA2_USE_PHYS_IOVA
		CONFIG_RTE_LIBRTE_PMD_DPAA2_SEC
		CONFIG_RTE_LIBRTE_PMD_DPAA2_EVENTDEV
		CONFIG_RTE_LIBRTE_PMD_DPAA2_CMDIF_RAWDEV
		CONFIG_RTE_LIBRTE_PMD_DPAA2_QDMA_RAWDEV
		CONFIG_RTE_LIBRTE_E1000_PMD
		CONFIG_RTE_LIBRTE_EM_PMD
		CONFIG_RTE_LIBRTE_IGB_PMD
		CONFIG_RTE_LIBRTE_ENA_PMD
		CONFIG_RTE_LIBRTE_ENETC_PMD
		CONFIG_RTE_LIBRTE_ENIC_PMD
		CONFIG_RTE_LIBRTE_PMD_FAILSAFE
		CONFIG_RTE_LIBRTE_FM10K_PMD
		CONFIG_RTE_LIBRTE_FM10K_RX_OLFLAGS_ENABLE
		CONFIG_RTE_LIBRTE_FM10K_INC_VECTOR
		CONFIG_RTE_LIBRTE_I40E_PMD
		CONFIG_RTE_LIBRTE_I40E_RX_ALLOW_BULK_ALLOC
		CONFIG_RTE_LIBRTE_I40E_INC_VECTOR
		CONFIG_RTE_LIBRTE_IAVF_PMD
		CONFIG_RTE_LIBRTE_ICE_PMD
		CONFIG_RTE_LIBRTE_IPN3KE_PMD
		CONFIG_RTE_LIBRTE_IXGBE_PMD
		CONFIG_RTE_IXGBE_INC_VECTOR
		CONFIG_RTE_LIBRTE_LIO_PMD
		CONFIG_RTE_LIBRTE_PMD_MEMIF
		CONFIG_RTE_LIBRTE_MLX4_PMD
		CONFIG_RTE_LIBRTE_MLX5_PMD
		CONFIG_RTE_LIBRTE_MVNETA_PMD
		CONFIG_RTE_LIBRTE_MVPP2_PMD
		CONFIG_RTE_LIBRTE_NETVSC_PMD
		CONFIG_RTE_LIBRTE_NFB_PMD
		CONFIG_RTE_LIBRTE_NFP_PMD
		CONFIG_RTE_LIBRTE_BNXT_PMD
		CONFIG_RTE_LIBRTE_PMD_NULL
		CONFIG_RTE_LIBRTE_OCTEONTX_PMD
		CONFIG_RTE_LIBRTE_PMD_OCTEONTX_SSOVF
		CONFIG_RTE_LIBRTE_PMD_OCTEONTX_CRYPTO
		CONFIG_RTE_LIBRTE_PMD_OCTEONTX_ZIPVF
		CONFIG_RTE_LIBRTE_OCTEONTX_MEMPOOL
		CONFIG_RTE_LIBRTE_OCTEONTX2_PMD
		CONFIG_RTE_LIBRTE_PMD_OCTEONTX2_CRYPTO
		CONFIG_RTE_LIBRTE_PMD_OCTEONTX2_EVENTDEV
		CONFIG_RTE_LIBRTE_PMD_OCTEONTX2_DMA_RAWDEV
		CONFIG_RTE_LIBRTE_OCTEONTX2_MEMPOOL
		CONFIG_RTE_LIBRTE_PMD_PCAP
		CONFIG_RTE_LIBRTE_PFE_PMD
		CONFIG_RTE_LIBRTE_PMD_CAAM_JR
		CONFIG_RTE_LIBRTE_QEDE_PMD
		CONFIG_RTE_LIBRTE_SFC_EFX_PMD
		CONFIG_RTE_LIBRTE_PMD_SZEDATA2
		CONFIG_RTE_LIBRTE_THUNDERX_NICVF_PMD
		CONFIG_RTE_LIBRTE_VDEV_NETVSC_PMD
		CONFIG_RTE_LIBRTE_VMXNET3_PMD
		CONFIG_RTE_LIBRTE_KNI
		CONFIG_RTE_LIBRTE_PMD_KNI
		CONFIG_RTE_LIBRTE_PMD_SOFTNIC
		CONFIG_RTE_LIBRTE_PMD_NITROX
		CONFIG_RTE_LIBRTE_PMD_QAT
		CONFIG_RTE_LIBRTE_PMD_VIRTIO_CRYPTO
		CONFIG_RTE_LIBRTE_PMD_CRYPTO_SCHEDULER
		CONFIG_RTE_LIBRTE_PMD_NULL_CRYPTO
		CONFIG_RTE_LIBRTE_PMD_SKELETON_RAWDEV
		CONFIG_RTE_LIBRTE_PMD_SKELETON_EVENTDEV
		CONFIG_RTE_LIBRTE_PMD_SW_EVENTDEV
		CONFIG_RTE_LIBRTE_PMD_DSW_EVENTDEV
		CONFIG_RTE_LIBRTE_PMD_OPDL_EVENTDEV
		CONFIG_RTE_LIBRTE_PMD_NTB_RAWDEV
		CONFIG_RTE_LIBRTE_IFC_PMD

		CONFIG_RTE_EAL_IGB_UIO
		CONFIG_RTE_KNI_KMOD
	)

	for f in "${make_config[@]}"; do
		if [ -f "$f" ]; then
			echo "Disable PMDs in $f ..."

			# 删除旧项
			for pmd in "${disabled_pmds[@]}"; do
				sed -i "/^${pmd}=n/d" "$f"
				sed -i "/^${pmd}=y/d" "$f"
			done

			# 统一追加
			{
				echo ""
				for pmd in "${disabled_pmds[@]}"; do
					echo "${pmd}=n"
				done
			} >> "$f"
		fi
	done

	echo "修改 DPDK 19 的 Makefile"
	if ! grep -q "CONFIG_RTE_LIBRTE_HINIC3_PMD" "./config/common_base"; then
		sed -i "/CONFIG_RTE_LIBRTE_HINIC_PMD/a #\n#Compile burst-oriented HINIC3 PMD driver\n#\nCONFIG_RTE_LIBRTE_HINIC3_PMD=y" \
			./config/common_base
	fi

	if ! grep -q "hinic3" "./drivers/net/Makefile"; then
		sed -i "/CONFIG_RTE_LIBRTE_HINIC_PMD/aDIRS-\$(CONFIG_RTE_LIBRTE_HINIC3_PMD) += hinic3" \
			./drivers/net/Makefile
	fi

	if ! grep -q "hinic3" "./mk/rte.app.mk"; then
		sed -i "/CONFIG_RTE_LIBRTE_HINIC_PMD/a_LDLIBS-\$(CONFIG_RTE_LIBRTE_HINIC3_PMD) += -lrte_pmd_hinic3" \
			./mk/rte.app.mk
	fi

	echo "修改 CONFIG_RTE_BUILD_SHARED_LIB 为 y 用于生成动态库"
	if grep -q "CONFIG_RTE_BUILD_SHARED_LIB=n" "./config/common_base"; then
		sed -i "s/CONFIG_RTE_BUILD_SHARED_LIB=n/CONFIG_RTE_BUILD_SHARED_LIB=y/g" ./config/common_base
	fi
}

install() {
	install_type="$1" # 可为空或 bifur

	check_git

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

	meson_build_adapt

	# dpdk>=22
	if [ "$DPDK_MAJOR" -ge 22 ]; then
		rm -f "./drivers/net/hinic3/version.map"
		echo "DPDK>=22, version.map has been removed"
	fi

	# 如果传了 bifur 参数，则修改 hinic3/meson.build
	if [ "$install_type" == "bifur" ]; then
		meson_file="./drivers/net/hinic3/meson.build"
		if ! grep -Fq "cflags += ['-DHINIC3_TRAFFIC_BIFUR']" "$meson_file"; then
			echo "为 $meson_file 添加 -DHINIC3_TRAFFIC_BIFUR cflag"
			sed -i "/cflags += \['-fstack-protector-strong'\]/a cflags += ['-DHINIC3_TRAFFIC_BIFUR']" "$meson_file"
		else
			echo "$meson_file 已存在 -DHINIC3_TRAFFIC_BIFUR"
		fi

		make_file="./drivers/net/hinic3/Makefile"
		if ! grep -Fq "CFLAGS += -DHINIC3_TRAFFIC_BIFUR" "$make_file"; then
			echo "为 $make_file 添加 -DHINIC3_TRAFFIC_BIFUR cflag"
			sed -i "/CFLAGS += -Wno-cast-qual/a CFLAGS += -DHINIC3_TRAFFIC_BIFUR" "$make_file"
		else
			echo "$make_file 已存在 -DHINIC3_TRAFFIC_BIFUR"
		fi
	fi

	# dpdk=19
	if [ "$DPDK_MAJOR" -eq 19 ]; then
		config_dpdk_19
	fi

	# 添加 hinic3 并提交
	git add .
	if ! git diff --cached --quiet || ! git diff --quiet; then
		git commit -m "drivers/net: add hinic3 pmd"
	else
		echo "No changes to commit"
	fi

	# 如果之前 stash 了，恢复
	if [ "$stashed" -eq 1 ]; then
		echo "恢复之前 stash 的更改"
		git stash pop
	fi
}

replace() {
	pmd_name=$1

	# 保存到 tmp 目录
	mkdir -p $SCRIPT_DIR/tmp
	tmp_file="$SCRIPT_DIR/tmp/pmd_name.txt"
	echo "$pmd_name" >"$tmp_file"

	stashed=0
	# 判断工作区是否有未提交的更改（包括暂存区和未跟踪文件）
	git status --porcelain
	if [ -n "$(git status --porcelain)" ]; then
		echo "工作区有未提交的更改，先 stash"
		git stash push -u -m "临时保存未提交更改"
		stashed=1
	fi

	# driver_name
	sed -i "s/^\(#define[[:space:]]*HINIC3_DRIVER_NAME[[:space:]]*\).*/\1\"$pmd_name\"/" \
		"./drivers/net/hinic3/base/hinic3_compat.h"

	# move driver dir
	if [ -d "drivers/net/$pmd_name" ]; then
		echo "error: drivers/net/$pmd_name already exists"
		exit 1
	fi
	mv drivers/net/hinic3 drivers/net/$pmd_name

	sed -i "s/'hinic3'/'$pmd_name'/" ./drivers/net/meson.build

	# 添加 BPNIC 并提交
	git add .
	if ! git diff --cached --quiet || ! git diff --quiet; then
		git commit -m "drivers/net: support $pmd_name"
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

	# 从 tmp 文件读取 pmd_name
	tmp_file="$SCRIPT_DIR/tmp/pmd_name.txt"
	if [ -f "$tmp_file" ]; then
		pmd_name=$(cat "$tmp_file")
	else
		pmd_name="hinic3" # 默认值
	fi

	# dpdk=19 用 Makefile
	if [ "$DPDK_MAJOR" -eq 19 ]; then
		echo "开始编译 (Makefile 模式)"

		arch=$(uname -m)
		echo "当前架构：$arch"
		if [ "$arch" = "aarch64" ]; then
			build_dir="arm64-armv8a-linuxapp-gcc"
		else
			build_dir="x86_64-native-linux-gcc"
		fi

		rm -rf $build_dir
		make config T=$build_dir

		extra_cflags=""
		# 默认忽略告警，保证 dpdk=19 在一些 GCC 版本下能顺利编译
		if [ -z "$DISABLE_DPDK19_WNO_ERROR" ]; then
			extra_cflags="-Wno-error"
		fi

		# debug
		if [ "$build_type" == "debug" ]; then
			extra_cflags="-O0 -g -DRTE_ENABLE_ASSERT $extra_cflags"
		fi

		make install T=$build_dir EXTRA_CFLAGS="$extra_cflags" -j
		echo ""
		ls -lh $build_dir/lib/librte_pmd_hinic3*
		ls -lh $build_dir/app/*testpmd
		echo ""
		echo "Use your device's PCI address in place of <BDF> and run:"
		echo ""
		echo "export LD_LIBRARY_PATH=$PWD/$stable_dir/$build_dir/lib:\$LD_LIBRARY_PATH"
		echo "$DPDK_PATH/$build_dir/app/testpmd -v -w 0000:01:00.0 -d $DPDK_PATH/$build_dir/lib/librte_pmd_hinic3.so -l 0-8 -- --nb-cores=8 --rxq=8 --txq=8 -i -a"
	else
		echo "执行 Meson 构建方式"
		rm -rf $build_dir
		# dpdk>=21
		meson_flags="-Ddisable_drivers=true -Denable_drivers=mempool/ring,net/hns3,net/${pmd_name}"
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
		echo ""
		ls -lh $build_dir/drivers/librte_net_${pmd_name}.so
		ls -lh $build_dir/drivers/librte_net_${pmd_name}.a
		ls -lh $build_dir/app/*testpmd
		echo ""
		echo "Use your device's PCI address in place of <BDF> and run:"
		echo ""
		echo "$DPDK_PATH/$build_dir/app/dpdk-testpmd -v -a 0000:01:00.0 -l 0-8 -- --nb-cores=8 --rxq=8 --txq=8 -i -a"
	fi
}

help() {
	cat <<EOF
用法:
   安装 hinic3 到 DPDK 目录:
   $0 <dpdk路径> install

   安装 hinic3 并启用 bifur:
   $0 <dpdk路径> install bifur

   BP卡适配安装
   $0 <dpdk路径> replace xxnic

   编译 release 版本:
   $0 <dpdk路径> build

   DPU 场景编译 (release):
   $0 <dpdk路径> build generic

   编译 debug 版本:
   $0 <dpdk路径> debug

示例:
   $0 ../dpdk-stable-21.11.9 install
   $0 ../dpdk-stable-21.11.9 build
EOF
	exit 0
}

if [ "$1" == "help" ]; then
	help
	exit 0
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

if [ "$ACTION" == "install" ] && [ "$3" == "bifur" ]; then
	install bifur
elif [ "$ACTION" == "install" ]; then
	install
elif [ "$ACTION" == "replace" ]; then
	replace $3
elif [ "$ACTION" == "build" ] && [ "$3" == "generic" ]; then
	build release generic
elif [ "$ACTION" == "build" ]; then
	build release
elif [ "$ACTION" == "debug" ] && [ "$3" == "generic" ]; then
	build debug generic
elif [ "$ACTION" == "debug" ]; then
	build debug
elif [[ $ACTION == "export" ]]; then
	rm -rf $SCRIPT_DIR/hinic3
	cp -r drivers/net/hinic3 $SCRIPT_DIR
else
	help
fi
