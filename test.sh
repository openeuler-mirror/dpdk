#!/bin/bash
set -e

DPDK_DIR=..
NIC_PCI="0000:03:00.0"

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)

# 检测 vendor/device id
vendor=$(lspci -n -s "$NIC_PCI" | awk '{print $3}' | cut -d: -f1)
device=$(lspci -n -s "$NIC_PCI" | awk '{print $3}' | cut -d: -f2)

if [[ -z "$vendor" || -z "$device" ]]; then
	echo "    未找到 PCI 设备 $NIC_PCI"
	exit 1
fi

echo "* Vendor ID: $vendor"
echo "* Device ID: $device"

if [[ "$vendor" != "19e5" || "$device" != "0222" ]]; then
	echo "仅支持SP670 PF"
	exit 1
fi

# 内核/驱动准备
modprobe vfio enable_unsafe_noiommu_mode=1
modprobe vfio-pci
echo 1 >/sys/module/vfio/parameters/enable_unsafe_noiommu_mode
echo never >/sys/kernel/mm/transparent_hugepage/enabled
dpdk-devbind.py -b vfio-pci $NIC_PCI

# 要测试的版本列表 (压缩包名)
VERSIONS=(
	"dpdk-19.11.14.tar.xz"
	"dpdk-20.11.10.tar.xz"
	"dpdk-21.11.9.tar.xz"
	"dpdk-22.11.9.tar.xz"
	"dpdk-23.11.5.tar.xz"
	"dpdk-24.11.3.tar.xz"
	"dpdk-25.07.tar.xz"
)

cd "$DPDK_DIR"
for pkg in "${VERSIONS[@]}"; do
	echo "==============================="
	echo ">>> 安装并测试 $pkg"
	echo "==============================="

	# 取出解压目录名，例如 dpdk-stable-19.11.14
	stable_dir=$(tar -tJf "$pkg" | head -1 | cut -f1 -d"/")
	rm -rf $stable_dir
	tar -xf $pkg

	# 编译
	sh "$SCRIPT_DIR/install.sh" "$stable_dir"
	sh "$SCRIPT_DIR/install.sh" "$stable_dir" build

	if [[ "$pkg" == dpdk-19.11.* ]]; then
		export LD_LIBRARY_PATH=$PWD/$stable_dir/arm64-armv8a-linuxapp-gcc/lib:$LD_LIBRARY_PATH
		hinic3_pmd="$stable_dir/arm64-armv8a-linuxapp-gcc/lib/librte_pmd_hinic3.so"
		testpmd_cmd="$stable_dir/arm64-armv8a-linuxapp-gcc/app/testpmd -d $hinic3_pmd -w $NIC_PCI -l 0-8 -- --nb-cores=8 --rxq=8 --txq=8 -i"
	else
		testpmd_cmd="$stable_dir/build/app/dpdk-testpmd -a $NIC_PCI -l 0-8 -- --nb-cores=8 --rxq=8 --txq=8 -i"
	fi

	# 运行并自动退出
	echo ">>> 启动 testpmd: $testpmd_cmd"
	$testpmd_cmd <<EOF
quit
EOF
done
