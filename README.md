# OpenEuler开源仓patch使用简介

本文以 **DPDK 21.11** 为例，介绍如何在 DPDK 中集成并编译 `hinic3` PMD。

当前支持的 DPDK 版本：**19 ~ 25**

---

## 1. 环境准备

- 系统依赖：
  - `yum install -y gcc libatomic python3-devel meson ninja-build python3-pyelftools libibverbs numactl numactl-devel`

- 软件版本：
  - DPDK：19.11 (LTS) 及以上版本
  - hinic3：本仓库提供

---

## 2. 下载 DPDK

官方源码下载地址： [https://core.dpdk.org/download/](https://core.dpdk.org/download/)

例如下载 **DPDK 21.11.9**：
```bash
wget https://fast.dpdk.org/rel/dpdk-21.11.9.tar.xz
tar -xf dpdk-21.11.9.tar.xz
# 解压后目录名：dpdk-stable-21.11.9
```

---

## 3. 获取 hinic3 源码
方法一：直接下载
```bash
wget https://gitee.com/openeuler/dpdk/repository/archive/hinic3.zip
unzip dpdk-hinic3.zip
# 解压后目录名：dpdk-hinic3
```
方法二：Git 克隆
```bash
git clone https://gitee.com/openeuler/dpdk.git -b hinic3 dpdk-hinic3
# 默认目录名是dpdk，这里指定为了：dpdk-hinic3
```

---

## 4. 安装 hinic3 PMD 到 DPDK
进入 dpdk-hinic3 目录，以下按需二选一执行
```bash
# 直接安装
sh install.sh ../dpdk-stable-21.11.9 install

# 如果需要使用分流功能
sh install.sh ../dpdk-stable-21.11.9 install bifur
```

如果是 BPNIC 需要再执行
```bash
sh install.sh ../dpdk-stable-21.11.9 replace $nic_name
```

---

## 5. 编译
以下按需二选一执行
```bash
# 直接编译
sh install.sh ../dpdk-stable-21.11.9 build

# 如果是 DPU 场景编译
sh install.sh ../dpdk-stable-21.11.9 build generic
```

---

## 6. 构建脚本说明 
- 自动检测目标 DPDK 目录是否为 Git 仓库：
  - 否 -> 自动初始化 Git。
- 自动判断 DPDK 版本：
  - **DPDK 19.x** 使用 `make` 编译
  - **DPDK ≥ 20.x** 使用 `meson + ninja` 编译

---

## 7. 快速示例
```bash
# 安装依赖
yum install -y gcc libatomic python3-devel meson ninja-build python3-pyelftools libibverbs numactl numactl-devel

# 下载并解压 DPDK
wget https://fast.dpdk.org/rel/dpdk-21.11.9.tar.xz
tar -xf dpdk-21.11.9.tar.xz

# 获取 hinic3
git clone https://gitee.com/openeuler/dpdk.git -b hinic3 dpdk-hinic3

# 安装并编译
cd dpdk-hinic3
sh install.sh ../dpdk-stable-21.11.9 install
sh install.sh ../dpdk-stable-21.11.9 build
```
