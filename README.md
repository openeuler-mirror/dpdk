# `hns3` PMD patch使用指导

## 使用说明
本章节以DPDK 20.11.5为例，介绍如何使用`hns3` patch。

### 1.1 安装编译依赖
```bash
yum install -y git gcc libatomic python3-devel meson ninja-build python3-pyelftools libibverbs numactl numactl-devel zlib-devel`
```

### 1.2 下载DPDK
DPDK官方源码包下载链接可在[https://core.dpdk.org/download/](https://core.dpdk.org/download/) 获取，例如下载DPDK 20.11.5：
```bash
wget https://fast.dpdk.org/rel/dpdk-20.11.5.tar.xz
tar -xf dpdk-20.11.5.tar.xz
# 解压后目录名：dpdk-stable-20.11.5
```

### 1.3 获取hinic3 PMD源码
- 方法一：直接下载
  下载后解压
  ```bash
  unzip dpdk-hns3.zip
  # 解压后目录名：dpdk-hns3
  ```
- 方法二：Git克隆
  ```bash
  git clone https://atomgit.com/openeuler/dpdk.git -b hns3 dpdk-hns3
  # 默认目录名是dpdk，这里指定为了：dpdk-hns3
  ```

### 1.4 编译
进入dpdk-stable-20.11.5目录，请用户按需选择进行安装编译：

安装patch
```bash
cd dpdk-stable-20.11.5
git init
git add .
git commit -m 'init'
git am ../dpdk-hns3/DPDK_20.11_patch/*.patch
```

编译
```bash
rm -rf build
meson build -Ddisable_drivers=net/cnxk,net/mlx4,net/mlx5,common/mlx5,regex/mlx5,vdpa/mlx5,crypto/*
ninja -C build
```

运行
```bash
./build/app/dpdk-testpmd -l 0-8 -a 0000:7d:00.0 -a 0000:7d:00.1 -- -i -a --rxq=8 --txq=8 --nb-cores=8
```
