#!/bin/bash
set -e

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)

# 适配低版本 meson
# DPDK版本对应的编译标志
declare -A DPDK_VERSION_FLAGS
DPDK_VERSION_FLAGS[20]="-DDPDK_20_11"
DPDK_VERSION_FLAGS[21]="-DDPDK_20_11 -DDPDK_21_11"
DPDK_VERSION_FLAGS[22]="-DDPDK_20_11 -DDPDK_21_11 -DDPDK_22_11"
DPDK_VERSION_FLAGS[23]="-DDPDK_20_11 -DDPDK_21_11 -DDPDK_22_11"
DPDK_VERSION_FLAGS[24]="-DDPDK_20_11 -DDPDK_21_11 -DDPDK_22_11 -DDPDK_24_11"
DPDK_VERSION_FLAGS[25]="-DDPDK_20_11 -DDPDK_21_11 -DDPDK_22_11 -DDPDK_24_11 -DDPDK_25_11"

# 获取指定版本的编译标志
get_version_flags() {
	echo ${DPDK_VERSION_FLAGS[$DPDK_MAJOR]}
}

# 向文件添加内容（如果不存在）
# $1: 文件路径  $2: 要添加的内容  $3: 插入位置标记行
add_to_file() {
	local file="$1"
	local content="$2"
	local marker="$3"
	
	if [ ! -f "$file" ]; then
		return
	fi
	
	# 判断不存在则添加
	if ! grep -Fq "$content" "$file"; then
		sed -i "/${marker}/a ${content}" "$file"
	fi
}

# 向meson.build添加cflag（如果不存在）
add_cflags_to_meson() {
	local meson_file="$1"
	local flag="$2"
	local line="cflags += ['$flag']"
	
	add_to_file "$meson_file" "$line" "cflags += \['-fstack-protector-strong'\]"
}

# 向Makefile添加CFLAGS（如果不存在）
add_cflags_to_makefile() {
	local make_file="$1"
	local flag="$2"
	local line="CFLAGS += $flag"
	
	add_to_file "$make_file" "$line" "CFLAGS += -Wno-cast-qual"
}

# 适配驱动的构建文件到当前DPDK版本
adapt_driver_build() {
	local meson_file="./drivers/net/hinic3/meson.build"
	local make_file="./drivers/net/hinic3/Makefile"

	# 删除原有 dpdk_version 判断整个段落（meson.build）
	if [ -f "$meson_file" ]; then
		sed -i '/^dpdk_version = meson.project_version()/,/^endif$/d' "$meson_file"
	fi

	# 添加当前版本对应的编译标志
	for flag in $(get_version_flags); do
		add_cflags_to_meson "$meson_file" "$flag"
		add_cflags_to_makefile "$make_file" "$flag"
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

# 清理旧的安装文件
clean_old_files() {
	echo "清理旧的安装文件..."
	rm -rf drivers/net/hinic3
	rm -rf app/test/test_hinic3
}

# 清理配置文件中的hinic3相关内容
clean_config_files() {
	echo "清理配置文件中的 hinic3 相关内容..."
	
	# 统一清理所有构建文件中的 hinic3 引用
	local config_files=(
		"drivers/net/meson.build"
		"drivers/net/Makefile"
		"app/test/meson.build"
		"app/test/Makefile"
	)
	
	for file in "${config_files[@]}"; do
		if [ -f "$file" ]; then
			sed -i "/hinic3/d" "$file"
		fi
	done
}

# 向DPDK的meson.build添加驱动子目录
add_driver_to_meson() {
	local meson_file="./drivers/net/meson.build"
	
	if ! grep -q "'hinic3'" "$meson_file"; then
		echo "添加 'hinic3' 到 meson.build"
		sed -i "/'hinic'/a\\	'hinic3'," "$meson_file"
	fi
}

# 向DPDK的Makefile添加驱动子目录
add_driver_to_makefile() {
	local make_file="./drivers/net/Makefile"
	local config="CONFIG_RTE_LIBRTE_HINIC_PMD"
	
	if ! grep -q "hinic3" "$make_file"; then
		echo "添加 'hinic3' 到 Makefile"
		sed -i "/${config}/aDIRS-\$(CONFIG_RTE_LIBRTE_HINIC3_PMD) += hinic3" "$make_file"
	fi
}

# 启用bifur编译标志
enable_bifur() {
	local meson_file="./drivers/net/hinic3/meson.build"
	local make_file="./drivers/net/hinic3/Makefile"
	
	echo "启用 bifur 编译标志..."
	
	# meson.build
	if ! grep -Fq "cflags += ['-DHINIC3_TRAFFIC_BIFUR']" "$meson_file"; then
		echo "为 $meson_file 添加 -DHINIC3_TRAFFIC_BIFUR cflag"
		add_cflags_to_meson "$meson_file" "-DHINIC3_TRAFFIC_BIFUR"
	else
		echo "$meson_file 已存在 -DHINIC3_TRAFFIC_BIFUR"
	fi
	
	# Makefile
	if ! grep -Fq "CFLAGS += -DHINIC3_TRAFFIC_BIFUR" "$make_file"; then
		echo "为 $make_file 添加 -DHINIC3_TRAFFIC_BIFUR cflag"
		add_cflags_to_makefile "$make_file" "-DHINIC3_TRAFFIC_BIFUR"
	else
		echo "$make_file 已存在 -DHINIC3_TRAFFIC_BIFUR"
	fi
}

# 安装驱动到DPDK目录
install_driver() {
	local install_type="$1" # 可为空或 bifur
	
	# 删除并拷贝 hinic3
	echo "安装 drivers/net/hinic3 ..."
	cp -r "$SCRIPT_DIR/hinic3" "drivers/net"

	# 修改构建文件，添加 hinic3
	add_driver_to_meson
	# dpdk=19 才需要修改Makefile
	if [ "$DPDK_MAJOR" -eq 19 ]; then
		add_driver_to_makefile
	fi

	# 适配驱动的构建文件到当前DPDK版本
	adapt_driver_build

	# dpdk>=22
	if [ "$DPDK_MAJOR" -ge 22 ]; then
		rm -f "./drivers/net/hinic3/version.map"
		echo "DPDK>=22, version.map has been removed"
	fi

	# 如果传了 bifur 参数，则启用 bifur 编译标志
	if [ "$install_type" == "bifur" ]; then
		enable_bifur
	fi

	# dpdk=19
	if [ "$DPDK_MAJOR" -eq 19 ]; then
		config_dpdk_19
	fi
}

# 提交更改到git
commit_changes() {
	# 添加 hinic3 并提交
	git add .
	if ! git diff --cached --quiet || ! git diff --quiet; then
		git commit -m "drivers/net: add hinic3 pmd"
	else
		echo "No changes to commit"
	fi
}

# 暂存未提交的更改
STASHED=0
stash_changes() {
	STASHED=0
	# 判断工作区是否有未提交的更改（包括暂存区和未跟踪文件）
	if [ -n "$(git status --porcelain)" ]; then
		echo "工作区有未提交的更改，先 stash"
		git stash push -u -m "临时保存未提交更改"
		STASHED=1
	fi
}

# 恢复之前暂存的更改
restore_changes() {
	# 如果之前 stash 了，恢复
	if [ "$STASHED" -eq 1 ]; then
		echo "恢复之前 stash 的更改"
		git stash pop
	fi
}

# 主安装函数
install() {
	local install_type="$1" # 可为空或 bifur
	local force_mode="$2"   # 可为空或 --force/-f

	# force 模式跳过 git 检查和提交，直接清理并安装
	if [ "$force_mode" == "--force" ] || [ "$force_mode" == "-f" ]; then
		clean_old_files
		clean_config_files
		install_driver "$install_type"
		install_dpdk_test
	else
		check_git
		stash_changes
		clean_old_files
		clean_config_files
		install_driver "$install_type"
		install_dpdk_test
		commit_changes
		restore_changes
	fi
}

# 添加测试源文件到构建系统（自动扫描 test 目录下的 test_hinic3_*.c 文件）
add_test_sources() {
	local build_file="$1"
	local build_type="$2" # meson 或 makefile
	local test_src_dir="$SCRIPT_DIR/test"
	
	# 自动扫描测试源文件
	local test_files=()
	for f in "$test_src_dir"/test_hinic3_*.c; do
		if [ -f "$f" ]; then
			local basename=$(basename "$f")
			test_files+=("$basename")
		fi
	done
	
	if [ ${#test_files[@]} -eq 0 ]; then
		echo "警告: 未找到测试源文件"
		return
	fi
	
	if [ "$build_type" == "makefile" ]; then
		# Makefile: 批量添加测试文件到 SRCS-y
		for f in "${test_files[@]}"; do
			add_to_file "$build_file" \
				"SRCS-\$(CONFIG_RTE_LIBRTE_HINIC3_PMD) += test_hinic3/$f" \
				"^SRCS-y += virtual_pmd.c"
		done
	else
		# meson.build: 批量添加测试文件到 sources
		for f in "${test_files[@]}"; do
			add_to_file "$build_file" \
				"'$f'," \
				"sources +="
		done
	fi
}

# 添加测试依赖到构建系统
add_test_deps() {
	local build_file="$1"
	local build_type="$2" # meson 或 makefile
	
	if [ "$build_type" == "makefile" ]; then
		# Makefile: 添加 include 路径和链接库
		add_to_file "$build_file" \
			"CFLAGS += -I\$(RTE_SDK)/drivers/net/hinic3 -I\$(RTE_SDK)/drivers/net/hinic3/base" \
			"^CFLAGS += -DALLOW_EXPERIMENTAL_API"
		
		# 添加链接库到文件末尾
		if ! grep -Fq "LDLIBS += -lrte_pmd_hinic3" "$build_file"; then
			echo "" >> "$build_file"
			echo "ifeq (\$(CONFIG_RTE_LIBRTE_HINIC3_PMD),y)" >> "$build_file"
			echo "LDLIBS += -lrte_pmd_hinic3" >> "$build_file"
			echo "endif" >> "$build_file"
		fi
	else
		# meson.build: 添加依赖
		add_to_file "$build_file" \
			"deps += ['ethdev', 'net_hinic3']" \
			"sources +="
	fi
}

# 注册测试子目录
register_test_subdir() {
	local parent_file="$1"
	local build_type="$2" # meson 或 makefile
	
	if [ "$build_type" == "makefile" ]; then
		# Makefile 不需要注册子目录（通过 SRCS-y 直接引用）
		return
	fi
	
	if [ ! -f "$parent_file" ]; then
		echo "警告: $parent_file 不存在"
		return
	fi

	# DPDK <= 22 需要在 dpdk_test 定义之前插入
	if [ "$DPDK_MAJOR" -le 22 ]; then
		echo "添加 test_hinic3 子目录到 meson.build (dpdk_test 定义之前)..."
		sed -i "/^dpdk_test = executable/i # hinic3 PMD tests\nhinic3_includes = include_directories('../../drivers/net/hinic3', '../../drivers/net/hinic3/base')\nsubdir('test_hinic3')\n" "$parent_file"
		sed -i "/^[[:space:]]*test_sources,$/a\\        include_directories: hinic3_includes," "$parent_file"
	else
		# DPDK > 22 直接在文件末尾添加即可
		echo "添加 test_hinic3 子目录到 meson.build (文件末尾)..."
		echo "" >> "$parent_file"
		echo "# hinic3 PMD tests" >> "$parent_file"
		echo "subdir('test_hinic3')" >> "$parent_file"
	fi
}

# 适配测试构建文件到当前DPDK版本
adapt_test_build() {
	local test_build_file="$1"
	local build_type="$2" # meson 或 makefile
	
	if [ "$build_type" == "makefile" ]; then
		# Makefile 不需要版本适配
		return
	fi
	
	# DPDK <= 22 使用 test_sources，> 22 使用 sources
	if [ "$DPDK_MAJOR" -le 22 ]; then
		sed -i 's/^sources += /test_sources += /' "$test_build_file"
	fi

	# 添加版本相关的编译标志
	for flag in $(get_version_flags); do
		echo "cflags += ['$flag']" >> "$test_build_file"
	done
}

# 安装单元测试到DPDK测试框架
install_dpdk_test() {
	echo "安装 hinic3 单元测试到 DPDK 测试框架..."

	local test_src_dir="$SCRIPT_DIR/test"
	local test_dst_dir="app/test/test_hinic3"

	if [ ! -d "$test_src_dir" ]; then
		echo "警告: 测试源目录不存在: $test_src_dir"
		return
	fi

	if [ ! -d "app/test" ]; then
		echo "警告: DPDK 测试目录不存在: app/test"
		return
	fi

	# 清空之前的安装
	if [ -d "$test_dst_dir" ]; then
		echo "清空之前的测试安装..."
		rm -rf "$test_dst_dir"
	fi

	# 创建测试目录并复制文件
	mkdir -p "$test_dst_dir"
	echo "复制测试文件到 $test_dst_dir/"
	cp -r "$test_src_dir"/* "$test_dst_dir/"

	# 根据DPDK版本选择构建系统
	if [ "$DPDK_MAJOR" -eq 19 ]; then
		# DPDK 19 使用 Makefile 构建
		local dpdk_makefile="app/test/Makefile"
		add_test_sources "$dpdk_makefile" "makefile"
		add_test_deps "$dpdk_makefile" "makefile"
	else
		# DPDK >= 20 使用 meson 构建
		local test_meson="$test_dst_dir/meson.build"
		local parent_meson="app/test/meson.build"
		
		add_test_sources "$test_meson" "meson"
		add_test_deps "$test_meson" "meson"
		adapt_test_build "$test_meson" "meson"
		register_test_subdir "$parent_meson" "meson"
	fi

	echo "hinic3 单元测试安装完成!"
}

# 运行单元测试
run_test() {
	local build_type="$1" # release 或 debug
	local build_dir="build"

	# debug
	if [[ "$build_type" == "debug" ]]; then
		build_dir="debug"
	fi

	local test_bin=""
	local tests=()
	
	# dpdk=19 用 Makefile
	if [ "$DPDK_MAJOR" -eq 19 ]; then
		local arch=$(uname -m)
		if [ "$arch" = "aarch64" ]; then
			build_dir="arm64-armv8a-linuxapp-gcc"
		else
			build_dir="x86_64-native-linux-gcc"
		fi
		test_bin="$build_dir/app/test"
		export LD_LIBRARY_PATH="$PWD/$build_dir/lib:$LD_LIBRARY_PATH"
		tests=("hinic3_basic_autotest" "hinic3_hairpin_autotest" "hinic3_rx_autotest")
	else
		# dpdk>=20 使用 meson 构建
		if [ -f "$build_dir/app/dpdk-test" ]; then
			test_bin="$build_dir/app/dpdk-test"
		elif [ -f "$build_dir/app/test/dpdk-test" ]; then
			test_bin="$build_dir/app/test/dpdk-test"
		else
			echo "错误: dpdk-test 不存在，请先执行 build"
			exit 1
		fi
		tests=("hinic3_basic_autotest" "hinic3_hairpin_autotest" "hinic3_rx_autotest")
	fi

	echo ""
	echo "运行 hinic3 单元测试 (DPDK 测试框架)..."
	echo "测试程序: $test_bin"
	echo ""

	# 创建临时运行时目录避免权限问题
	local test_runtime_dir="/tmp/dpdk-test-runtime"
	mkdir -p "$test_runtime_dir"

	# 依次运行所有测试
	local failed=0
	for test_name in "${tests[@]}"; do
		echo "=========================================="
		echo "运行测试: $test_name"
		echo "=========================================="
		if XDG_RUNTIME_DIR="$test_runtime_dir" DPDK_TEST="$test_name" $test_bin --file-prefix=ut --no-pci --no-huge -m 64; then
			echo "[PASS] $test_name"
		else
			echo "[FAIL] $test_name"
			failed=1
		fi
		echo ""
	done

	echo "=========================================="
	if [ $failed -eq 0 ]; then
		echo "所有测试通过!"
	else
		echo "部分测试失败!"
	fi
	echo "=========================================="

	return $failed
}

replace() {
	pmd_name=$1

	# 保存到 tmp 目录
	mkdir -p $SCRIPT_DIR/tmp
	tmp_file="$SCRIPT_DIR/tmp/pmd_name.txt"
	echo "$pmd_name" >"$tmp_file"

	stashed=0
	# 判断工作区是否有未提交的更改（包括暂存区和未跟踪文件）
	stash_changes

	# driver_name
	sed -i "s/^\(#define[[:space:]]*HINIC3_DRIVER_NAME[[:space:]]*\).*/\1\"$pmd_name\"/" \
		"./drivers/net/hinic3/base/hinic3_compat.h"

	# move driver dir
	if [ -d "drivers/net/$pmd_name" ]; then
		echo "error: drivers/net/$pmd_name already exists"
		exit 1
	fi
	mv drivers/net/hinic3 drivers/net/$pmd_name

	# 替换驱动目录中的构建文件引用
	sed -i "s/'hinic3'/'$pmd_name'/" ./drivers/net/meson.build
	sed -i "s/hinic3/$pmd_name/g" ./drivers/net/Makefile

	# 替换测试目录中的hinic3引用
	if [ -d "app/test/test_hinic3" ]; then
		sed -i "s/hinic3/$pmd_name/g" app/test/test_hinic3/meson.build
		sed -i "s/hinic3/$pmd_name/g" app/test/test_hinic3/Makefile
		sed -i "s/'hinic3'/'$pmd_name'/" ./app/test/meson.build
		sed -i "s/test_hinic3/test_$pmd_name/g" ./app/test/meson.build
		sed -i "s/hinic3/$pmd_name/g" ./app/test/Makefile
	fi

	# 添加 BPNIC 并提交
	git add .
	if ! git diff --cached --quiet || ! git diff --quiet; then
		git commit -m "drivers/net: support $pmd_name"
	else
		echo "No changes to commit"
	fi

	# 如果之前 stash 了，恢复
	restore_changes
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
			if [ "$DPDK_MAJOR" -eq 20 ]; then
				meson_flags="$meson_flags -Dmachine=generic"
			else
				meson_flags="$meson_flags -Dplatform=generic"
			fi
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

   强制重新安装 (清理已提交的修改):
   $0 <dpdk路径> install -f/--force

   安装 hinic3 并启用 bifur:
   $0 <dpdk路径> install bifur

   强制重新安装并启用 bifur:
   $0 <dpdk路径> install bifur -f/--force

   BP卡适配安装
   $0 <dpdk路径> replace xxnic

   编译 release 版本:
   $0 <dpdk路径> build

   DPU 场景编译 (release):
   $0 <dpdk路径> build generic

   编译 debug 版本:
   $0 <dpdk路径> debug

   运行单元测试 (需要先 build):
   $0 <dpdk路径> test

   运行 debug 版本单元测试:
   $0 <dpdk路径> test debug

示例:
   $0 ../dpdk-stable-21.11.9 install
   $0 ../dpdk-stable-21.11.9 install --force
   $0 ../dpdk-stable-21.11.9 build
   $0 ../dpdk-stable-21.11.9 test
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
elif [ "$ACTION" == "install" ] && { [ "$3" == "--force" ] || [ "$3" == "-f" ]; }; then
	install "" "--force"
elif [ "$ACTION" == "install" ] && [ "$4" == "bifur" ] && { [ "$3" == "--force" ] || [ "$3" == "-f" ]; }; then
	install bifur "--force"
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
elif [ "$ACTION" == "test" ] && [ "$3" == "debug" ]; then
	run_test debug
elif [ "$ACTION" == "test" ]; then
	run_test
elif [[ $ACTION == "export" ]]; then
	# 导出驱动到当前脚本目录
	rm -rf $SCRIPT_DIR/hinic3
	cp -r drivers/net/hinic3 $SCRIPT_DIR
else
	help
fi
