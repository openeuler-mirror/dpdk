#!/bin/bash
set -e

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)

# Source install.sh 获取共享函数和初始化
export HINIC3_SOURCED=1
source "$SCRIPT_DIR/install.sh" "$@"
unset HINIC3_SOURCED

# 保存原函数用于覆盖
eval "$(declare -f adapt_driver_build | sed 's/^adapt_driver_build/_adapt_driver_build_base/')"
eval "$(declare -f install | sed 's/^install/_install_base/')"
eval "$(declare -f build | sed 's/^build/_build_base/')"
eval "$(declare -f clean_config_files | sed 's/^clean_config_files/_clean_config_files_base/')"

# 覆盖 clean_config_files：在原清理基础上增加 UT 专属清理（--wrap）
clean_config_files() {
	_clean_config_files_base

	local config_files=(
		"drivers/net/meson.build"
		"drivers/net/Makefile"
		"app/test/meson.build"
		"app/test/Makefile"
	)

	# 清理 UT 写入的 --wrap 行（不含 hinic3 标记，需单独清理）
	for file in "${config_files[@]}"; do
		if [ -f "$file" ]; then
			sed -i "/wrap=/d" "$file"
		fi
	done
}

# 覆盖 adapt_driver_build：在原逻辑基础上添加 UT 编译标志
adapt_driver_build() {
	_adapt_driver_build_base

	local meson_file="./drivers/net/hinic3/meson.build"
	local make_file="./drivers/net/hinic3/Makefile"

	# 防止 static 函数被内联（objcopy 无法提升被内联消除的符号）
	add_cflags_to_meson "$meson_file" "-fno-inline"
	add_cflags_to_makefile "$make_file" "-fno-inline"
}

# 提升驱动 .o 文件中的 static 符号为全局可见，供单元测试直接引用
# 使用 objcopy --globalize-symbol，无需修改驱动源码
globalize_static_symbols() {
	local build_dir="$1"

	# 需要提升的 static 符号（均在 hinic3_pmd_ethdev.c 中定义）
	local symbols="hinic3_pmd_ops hinic3_pmd_vf_ops hinic3_xstats_calc_num hinic3_find_vlan_filter hinic3_store_vlan_filter"

	# 构建 objcopy 参数
	local globalize_args=""
	for sym in $symbols; do
		globalize_args="$globalize_args --globalize-symbol=$sym"
	done

	# 查找 hinic3_pmd_ethdev 编译产物
	local obj_file=""
	obj_file=$(find "$build_dir/drivers" -name "*hinic3_pmd_ethdev*.o" 2>/dev/null | head -1)

	if [ -z "$obj_file" ]; then
		echo "警告: 未找到 hinic3_pmd_ethdev.o，跳过符号提升"
		return
	fi

	echo "提升 static 符号为全局可见: $obj_file"

	# 保存原始 mtime，objcopy 后恢复，防止 ninja 检测到变化而重新编译
	local ref_file=$(mktemp)
	touch -r "$obj_file" "$ref_file"

	objcopy $globalize_args "$obj_file"

	# 恢复原始 mtime
	touch -r "$ref_file" "$obj_file"
	rm -f "$ref_file"

	# 删除 .a 归档文件，强制 ninja/make 重新归档（使用已 globalize 的 .o）
	local pmd_name="hinic3"
	local tmp_name_file="$SCRIPT_DIR/tmp/pmd_name.txt"
	[ -f "$tmp_name_file" ] && pmd_name=$(cat "$tmp_name_file")

	rm -f "$build_dir/drivers/libtmp_rte_net_${pmd_name}.a" \
	      "$build_dir/drivers/librte_net_${pmd_name}.a"

	if [ "$DPDK_MAJOR" -eq 19 ]; then
		# DPDK 19 Makefile: 库名为 librte_pmd_<name>.a，位于 lib/ 目录
		rm -f "$build_dir/lib/librte_pmd_${pmd_name}.a"
	else
		if [ -f "$build_dir/.ninja_log" ]; then
			sed -i "/libtmp_rte_net_${pmd_name}\.a$/d; /librte_net_${pmd_name}\.a$/d" \
				"$build_dir/.ninja_log"
		fi
	fi

	# 重新构建以使用修改后的 .o 文件重新归档和链接
	echo "重新归档和链接..."
	if [ "$DPDK_MAJOR" -eq 19 ]; then
		make install T="$build_dir" -j
	else
		ninja -C "$build_dir"
	fi
}

# 覆盖 build：先编译驱动 .o 提升 static 符号，再完整编译链接
build() {
	if [ "$DPDK_MAJOR" -eq 19 ]; then
		# DPDK 19: Makefile 模式，保持原有逻辑
		set +e
		_build_base "$@"
		set -e
		globalize_static_symbols "$build_dir"
		return
	fi

	# DPDK >= 20: 先只编译 hinic3_pmd_ethdev.c.o，提升 static 符号后再完整编译
	# 避免首次链接失败产生 FAILED 信息

	# 阶段 1: Meson 配置（临时覆盖 ninja 为 no-op，仅执行 meson setup）
	# version 检查正常通过，编译命令被跳过
	ninja() {
		[ "$1" == "--version" ] && { command ninja "$@"; return; }
		return 0
	}
	# ls 等命令会失败（产物尚未生成），用 set +e 容忍
	set +e
	_build_base "$@" > /dev/null 2>&1
	set -e
	unset -f ninja

	# 阶段 2: 只编译 hinic3_pmd_ethdev.c.o（不触发链接）
	local ethdev_target=""
	ethdev_target=$(grep "^build .*hinic3_pmd_ethdev.*\.o:" "$build_dir/build.ninja" 2>/dev/null | head -1 | sed 's/^build //; s/:.*//')

	if [ -n "$ethdev_target" ]; then
		echo "编译 $ethdev_target ..."
		command ninja -C "$build_dir" "$ethdev_target"
	else
		echo "未找到 hinic3_pmd_ethdev 目标，执行完整编译..."
		set +e
		command ninja -C "$build_dir"
		set -e
	fi

	# 阶段 3: 提升 static 符号可见性并完整编译链接
	globalize_static_symbols "$build_dir"

	# 显示构建产物
	echo ""
	ls -lh $build_dir/drivers/librte_net_${pmd_name}.so
	ls -lh $build_dir/drivers/librte_net_${pmd_name}.a
	ls -lh $build_dir/app/*testpmd
	echo ""
	echo "Use your device's PCI address in place of <BDF> and run:"
	echo ""
	echo "$DPDK_PATH/$build_dir/app/dpdk-testpmd -v -a 0000:01:00.0 -l 0-8 -- --nb-cores=8 --rxq=8 --txq=8 -i -a"
}

# 覆盖 install：在原安装基础上添加测试安装
install() {
	_install_base "$@"

	# UT 需要静态库模式以支持 --wrap 链接器选项
	if [ "$DPDK_MAJOR" -eq 19 ]; then
		if grep -q "CONFIG_RTE_BUILD_SHARED_LIB=y" "./config/common_base" 2>/dev/null; then
			echo "修改 CONFIG_RTE_BUILD_SHARED_LIB 为 n 以支持 --wrap 链接选项"
			sed -i "s/CONFIG_RTE_BUILD_SHARED_LIB=y/CONFIG_RTE_BUILD_SHARED_LIB=n/g" ./config/common_base
		fi
	fi

	install_dpdk_test
}

# ==================== UT 专属函数 ====================

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

	# 需要 --wrap 的函数列表（mock 通过 __wrap_ 前缀 + --wrap 链接器选项覆盖驱动函数）
	local wrap_syms="hinic3_get_bifur_enable hinic3_bifur_is_shared_dev l2nic_msg_to_mgmt_sync hinic3_add_tcam_rule hinic3_del_tcam_rule hinic3_alloc_tcam_block hinic3_free_tcam_block hinic3_set_fdir_tcam_rule_filter hinic3_flush_tcam_rule hinic3_mgmt_get_rss_id hinic3_fdir_cfg_sec_tcam hinic3_fdir_alloc_sec_tcam_block hinic3_fdir_sec_tcam_block_free hinic3_fdir_add_sec_tcam_rule hinic3_fdir_del_sec_tcam_rule hinic3_fdir_set_fdir_sec_tcam_rule_filter hinic3_update_rss_config hinic3_cmdq_set_rss_queue_type hinic3_global_func_id hinic3_mgmt_cfg_qgrp_id hinic3_rss_template_alloc hinic3_rss_template_free hinic3_rss_get_indir_tbl hinic3_rss_set_indir_tbl hinic3_rss_set_indir_tbl_qpool hinic3_rss_queue_set_indir_tbl hinic3_rss_cfg hinic3_flow_query_fdir_filter tcam_translate_key_y_local tcam_translate_key_x_local tcam_key_calculate_local tcam_key_calculate hinic3_add_vlan hinic3_del_vlan hinic3_set_port_mtu hinic3_set_rx_mode is_sp620_nic is_sp230_nic is_sp560_nic hinic3_func_type hinic3_physical_port_id hinic3_msg_to_mgmt_sync hinic3_mbox_to_pf hinic3_bifur_get_default_mac hinic3_set_link_status_follow hinic3_set_tm_hierarchy_do_commit hinic3_set_tm_config_tc_rate hinic3_set_qos_port_trust hinic3_update_mac hinic3_set_mac hinic3_del_mac hinic3_set_vlan_fliter hinic3_set_rx_vlan_offload"

	if [ "$build_type" == "makefile" ]; then
		# Makefile: 添加 include 路径和链接库
		add_to_file "$build_file" \
			"CFLAGS += -I\$(RTE_SDK)/drivers/net/hinic3 -I\$(RTE_SDK)/drivers/net/hinic3/base" \
			"^CFLAGS += -DALLOW_EXPERIMENTAL_API"

		# 添加 --wrap 链接器选项和链接库到文件末尾
		if ! grep -Fq "LDLIBS += -lrte_pmd_hinic3" "$build_file"; then
			echo "" >> "$build_file"
			echo "ifeq (\$(CONFIG_RTE_LIBRTE_HINIC3_PMD),y)" >> "$build_file"
			local wrap_ldflags=""
			for sym in $wrap_syms; do
				wrap_ldflags="$wrap_ldflags --wrap=$sym"
			done
			echo "LDLIBS +=$wrap_ldflags -lrte_pmd_hinic3" >> "$build_file"
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
		return
	fi

	if [ ! -f "$parent_file" ]; then
		echo "警告: $parent_file 不存在"
		return
	fi

	# DPDK <= 22 需要在 dpdk_test 定义之前插入
	if [ "$DPDK_MAJOR" -le 22 ]; then
		echo "添加 test_hinic3 子目录到 meson.build (dpdk_test 定义之前)..."
		# 插入 subdir 调用，在 dpdk_test 定义前执行 test_hinic3/meson.build
		# test_hinic3/meson.build 中 ldflags += [...] 会修改共享的 ldflags 变量
		# 原生 dpdk_test = executable(...) 已有 link_args: ldflags，无需额外插入
		sed -i "/^dpdk_test = executable/i # hinic3 PMD tests\nhinic3_includes = include_directories('../../drivers/net/hinic3', '../../drivers/net/hinic3/base')\nsubdir('test_hinic3')\n" "$parent_file"
		# 补充 include_directories 关键字（原生 executable 缺少 hinic3 头文件路径）
		sed -i "/^[[:space:]]*test_sources,$/a\\        include_directories: hinic3_includes," "$parent_file"
	else
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

	# 抑制 mock 函数（__wrap_*）的编译告警
	echo "cflags += ['-Wno-missing-prototypes']" >> "$test_build_file"
	echo "cflags += ['-Wno-missing-declarations']" >> "$test_build_file"
	echo "cflags += ['-Wno-unused-parameter']" >> "$test_build_file"
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
		# DPDK 19 共享库模式下跳过测试安装（与 DPDK>=20 meson 方式一致）
		if grep -q "CONFIG_RTE_BUILD_SHARED_LIB=y" "./config/common_base" 2>/dev/null; then
			echo "共享库模式下跳过 hinic3 单元测试安装，测试仅支持静态库模式（CONFIG_RTE_BUILD_SHARED_LIB=n）"
			return
		fi
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
	local build_type="$1" # release 或 debug，仅影响 meson 的 build_dir
	local build_dir="build"
	[[ "$build_type" == "debug" ]] && build_dir="debug"

	local tests=("hinic3_basic_autotest" "hinic3_hairpin_autotest" "hinic3_rx_autotest" "hinic3_tx_autotest" "hinic3_tx_compact_autotest" "hinic3_ethdev_autotest" "hinic3_fdir_autotest" "hinic3_flow_autotest" "hinic3_flow_sec_autotest" "hinic3_nic_cfg_autotest" "hinic3_bifur_autotest" "hinic3_dcb_autotest" "hinic3_tm_autotest" "hinic3_nic_io_autotest")
	local test_bin=""

	if [ "$DPDK_MAJOR" -eq 19 ]; then
		local arch=$(uname -m)
		build_dir=$([ "$arch" = "aarch64" ] && echo "arm64-armv8a-linuxapp-gcc" || echo "x86_64-native-linux-gcc")
		test_bin="$build_dir/app/test"
		export LD_LIBRARY_PATH="$PWD/$build_dir/lib:$LD_LIBRARY_PATH"
	else
		if [ -f "$build_dir/app/dpdk-test" ]; then
			test_bin="$build_dir/app/dpdk-test"
		elif [ -f "$build_dir/app/test/dpdk-test" ]; then
			test_bin="$build_dir/app/test/dpdk-test"
		else
			echo "错误: dpdk-test 不存在，请先执行 build"
			exit 1
		fi
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

# 运行覆盖率分析：编译 → 测试 → 收集覆盖率
run_gcov() {
	local build_dir="debug"
	export PATH="$HOME/.local/bin:$PATH"

	local pmd_name="hinic3"
	local tmp_file="$SCRIPT_DIR/tmp/pmd_name.txt"
	[ -f "$tmp_file" ] && pmd_name=$(cat "$tmp_file")

	local gcov_output_dir="$SCRIPT_DIR/gcov_report"
	rm -rf "$gcov_output_dir"
	mkdir -p "$gcov_output_dir"

	echo "=========================================="
	echo "步骤 1/5: 带覆盖率标志编译"
	echo "=========================================="

	if [ "$DPDK_MAJOR" -eq 19 ]; then
		local arch=$(uname -m)
		build_dir=$([ "$arch" = "aarch64" ] && echo "arm64-armv8a-linuxapp-gcc" || echo "x86_64-native-linux-gcc")
		rm -rf "$build_dir"
		make config T="$build_dir"
		set +e
		make install T="$build_dir" EXTRA_CFLAGS="-O0 -g --coverage -DRTE_ENABLE_ASSERT -Wno-error" -j
		set -e
	else
		rm -rf "$build_dir"
		local meson_flags="-Ddisable_drivers=true -Denable_drivers=mempool/ring,net/hns3,net/${pmd_name}"
		[ "$DPDK_MAJOR" -eq 20 ] && meson_flags="-Ddisable_drivers=net/cnxk,net/mlx4,net/mlx5,common/mlx5,regex/mlx5,vdpa/mlx5,crypto/*"
		meson "$build_dir" $meson_flags -Dbuildtype=debug -Db_coverage=true -Dwerror=false
		set +e
		ninja -C "$build_dir"
		set -e
	fi

	# 提升 static 符号可见性并重新链接
	globalize_static_symbols "$build_dir"

	local lcov_filtered="$gcov_output_dir/coverage_filtered.info"

	# 检查覆盖率工具
	local has_lcov=false has_gcovr=false
	command -v lcov >/dev/null 2>&1 && command -v genhtml >/dev/null 2>&1 && \
		lcov --version >/dev/null 2>&1 && genhtml --version >/dev/null 2>&1 && has_lcov=true
	command -v gcovr >/dev/null 2>&1 && gcovr --version >/dev/null 2>&1 && has_gcovr=true
	[ "$has_lcov" = false ] && [ "$has_gcovr" = false ] && pip3 install gcovr 2>&1 && has_gcovr=true || true

	echo ""
	echo "=========================================="
	echo "步骤 2/5: 运行单元测试"
	echo "=========================================="
	run_test debug || true

	echo ""
	echo "=========================================="
	echo "步骤 3/5: 收集覆盖率并生成 HTML 报告"
	echo "=========================================="

	# gcovr 公用排除参数
	local gcovr_excludes="--exclude ^.*app/test/.* --exclude .*/rte_.* --exclude .*/hinic3_compat.h --exclude .*/ethdev_driver.h --exclude .*/_.*.h"

	if [ "$has_lcov" = true ]; then
		local lcov_combined="$gcov_output_dir/coverage_combined.info"
		local lcov_base="$gcov_output_dir/coverage_base.info"
		local lcov_test="$gcov_output_dir/coverage_test.info"

		lcov --capture --initial --directory "$build_dir" --base-directory "$PWD" \
			--output-file "$lcov_base" --rc lcov_branch_coverage=1 2>/dev/null || true
		lcov --capture --directory "$build_dir" --base-directory "$PWD" \
			--output-file "$lcov_test" --rc lcov_branch_coverage=1 2>/dev/null || true

		if [ -s "$lcov_test" ] && [ -s "$lcov_base" ]; then
			lcov --add-tracefile "$lcov_base" --add-tracefile "$lcov_test" \
				--output-file "$lcov_combined" --rc lcov_branch_coverage=1 2>/dev/null || true
		elif [ -s "$lcov_test" ]; then
			cp "$lcov_test" "$lcov_combined"
		elif [ -s "$lcov_base" ]; then
			cp "$lcov_base" "$lcov_combined"
		fi

		if [ -s "$lcov_combined" ]; then
			# 修正 meson 构建中间目录路径
			lcov --substitute "s#/libtmp_rte_net_${pmd_name}\.a\.p/#/net/${pmd_name}/#g" \
				--substitute "s#/lib[a-z_0-9]*\.a\.p/#/#g" \
				--substitute "s#/dpdk-[^/]*\.p/#/#g" \
				"$lcov_combined" --output-file "$lcov_combined.sub" \
				--rc lcov_branch_coverage=1 2>/dev/null && mv "$lcov_combined.sub" "$lcov_combined"

			# 过滤只保留 hinic3 驱动源文件
			lcov --extract "$lcov_combined" "*/drivers/net/${pmd_name}/*" \
				--output-file "$lcov_filtered" --rc lcov_branch_coverage=1 2>/dev/null || true
			[ ! -s "$lcov_filtered" ] && lcov --extract "$lcov_combined" "*/${pmd_name}/*" \
				--output-file "$lcov_filtered" --rc lcov_branch_coverage=1 2>/dev/null || true
			[ ! -s "$lcov_filtered" ] && cp "$lcov_combined" "$lcov_filtered"

			# 排除单元测试文件
			lcov --remove "$lcov_filtered" "*/app/test/*" \
				--output-file "$lcov_filtered.sub" --rc lcov_branch_coverage=1 2>/dev/null && \
				mv "$lcov_filtered.sub" "$lcov_filtered"

			genhtml "$lcov_filtered" --output-directory "$gcov_output_dir/html" \
				--title "hinic3 覆盖率报告" --legend --branch-coverage 2>/dev/null || true
			[ -f "$gcov_output_dir/html/index.html" ] && echo "HTML 报告已生成 (lcov)" || has_lcov=false
		else
			has_lcov=false
		fi
	fi

	if [ "$has_lcov" = false ] && [ "$has_gcovr" = true ]; then
		mkdir -p "$gcov_output_dir/html"
		local libtmp_dir="$build_dir/drivers/libtmp_rte_net_${pmd_name}.a.p"
		[ ! -d "$libtmp_dir" ] && libtmp_dir="$build_dir"
		gcovr --html --html-details --root "$PWD" --gcov-filter ".*${pmd_name}.*" \
			$gcovr_excludes --output "$gcov_output_dir/html/index.html" \
			--html-title "hinic3 覆盖率报告" "$libtmp_dir" 2>/dev/null || \
			gcovr --html --html-details --root "$PWD" --gcov-filter ".*${pmd_name}.*" \
				$gcovr_excludes --output "$gcov_output_dir/html/index.html" \
				--html-title "hinic3 覆盖率报告" "$build_dir" 2>/dev/null || true
		if [ -f "$gcov_output_dir/html/index.html" ]; then
			echo "HTML 报告已生成 (gcovr)"
			find "$gcov_output_dir/html" -name "*.html" -exec sed -i \
				-e "s#libtmp_rte_net_${pmd_name}\.a\.p/#drivers/net/${pmd_name}/#g" \
				-e 's#/lib[a-z_0-9]*\.a\.p/#/#g' \
				-e "s#>net/${pmd_name}/#>drivers/net/${pmd_name}/#g" {} +
		else
			has_gcovr=false
		fi
	fi

	echo ""
	echo "=========================================="
	echo "步骤 4/5: 覆盖率摘要"
	echo "=========================================="

	if [ -s "$lcov_filtered" ] && command -v lcov >/dev/null 2>&1; then
		lcov --summary "$lcov_filtered" 2>/dev/null | grep -E "(lines|functions|branches)" || true
	elif [ "$has_gcovr" = true ]; then
		local libtmp_dir="$build_dir/drivers/libtmp_rte_net_${pmd_name}.a.p"
		[ ! -d "$libtmp_dir" ] && libtmp_dir="$build_dir"
		gcovr --root "$PWD" -s --gcov-filter ".*${pmd_name}.*" \
			$gcovr_excludes "$libtmp_dir" 2>/dev/null | grep -E "^(lines|branches|TOTAL):" || \
			gcovr --root "$PWD" -s --gcov-filter ".*${pmd_name}.*" \
				$gcovr_excludes "$build_dir" 2>/dev/null | grep -E "^(lines|branches|TOTAL):" || true
	else
		echo "请安装 lcov 或 gcovr 以查看覆盖率摘要"
	fi

	echo ""
	echo "=========================================="
	echo "步骤 5/5: 报告位置"
	echo "=========================================="
	if [ -f "$gcov_output_dir/html/index.html" ]; then
		echo "HTML: $gcov_output_dir/html/index.html"
	else
		echo "HTML 报告生成失败，请确认已安装 lcov 或 gcovr"
	fi
	echo "=========================================="
}

# ==================== UT 路径分发 ====================

if [ "$ACTION" == "install" ] && { [ "$3" == "--force" ] || [ "$3" == "-f" ]; }; then
	install "" "--force"
elif [ "$ACTION" == "install" ]; then
	install
elif [ "$ACTION" == "build" ]; then
	build release
elif [ "$ACTION" == "test" ]; then
	run_test
elif [ "$ACTION" == "gcov" ]; then
	run_gcov
else
	echo "用法:"
	echo "   $0 <dpdk路径> install [-f]"
	echo "   $0 <dpdk路径> build"
	echo "   $0 <dpdk路径> test"
	echo "   $0 <dpdk路径> gcov"
	exit 0
fi
