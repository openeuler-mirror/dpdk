# OpenEuler 开源仓 `hinic3` PMD 使用简介

本文以 **DPDK 22.11** 为例，介绍如何在 DPDK 中集成并编译 `hinic3` PMD。

PMD 已归一到本项目的 hinic3 目录中，使用方式由原先的每个版本单独打 patch，变为了使用 `install.sh` 脚本自动安装 hinic3 到源码目录中。原先 patch 使用方式参考 `README.patch.md`

- 当前 `hinic3` PMD 支持的 DPDK 版本：**19.11 ~ 25**
- 分流功能支持的 DPDK 版本：**20.11 ~ 22.11**

---

## 1. 环境准备
### 1.1 安装编译依赖
`yum install -y gcc libatomic python3-devel meson ninja-build python3-pyelftools libibverbs numactl numactl-devel`

### 1.2 下载 DPDK

DPDK 官方源码包下载链接可在 [https://core.dpdk.org/download/](https://core.dpdk.org/download/) 获取，例如下载 **DPDK 22.11.9**：
```bash
wget https://fast.dpdk.org/rel/dpdk-22.11.9.tar.xz
tar -xf dpdk-22.11.9.tar.xz
# 解压后目录名：dpdk-stable-22.11.9
```

### 1.3 获取 hinic3 PMD 源码
方法一：直接下载
```bash
wget https://gitee.com/openeuler/dpdk/repository/archive/baidu.zip
unzip dpdk-baidu.zip
# 解压后目录名：dpdk-baidu
```
方法二：Git 克隆
```bash
git clone https://gitee.com/openeuler/dpdk.git -b baidu dpdk-hinic3
# 默认目录名是dpdk，这里指定为了：dpdk-hinic3
```

---

## 2. 安装 hinic3 PMD 到 DPDK
进入 dpdk-hinic3 目录，以下按需二选一执行
```bash
# 直接安装
sh install.sh ../dpdk-stable-22.11.9 install

# 如果需要使用分流功能
sh install.sh ../dpdk-stable-22.11.9 install bifur
```

BAIDU rename 需要再执行
```bash
sh install.sh ../dpdk-stable-22.11.9 replace baidu
```

安装脚本会自动检测目标 DPDK 目录是否为 Git 仓库，如果不是，会自动初始化 Git。

---

## 3. 编译
以下按需二选一执行
```bash
# 直接编译
sh install.sh ../dpdk-stable-22.11.9 build

# 如果是 DPU 场景编译
sh install.sh ../dpdk-stable-22.11.9 build generic
```

安装脚本会自动判断 DPDK 版本：
  - **DPDK 19.11** 使用 `make` 编译
  - **DPDK ≥ 20.11** 使用 `meson + ninja` 编译

---

## 4. 快速示例
### 依赖下载
```bash
# 安装 DPDK 依赖
yum install -y gcc libatomic python3-devel meson ninja-build python3-pyelftools libibverbs numactl numactl-devel

# 下载并解压 DPDK
wget https://fast.dpdk.org/rel/dpdk-22.11.9.tar.xz
tar -xf dpdk-22.11.9.tar.xz

# 获取 hinic3 PMD
git clone https://gitee.com/openeuler/dpdk.git -b baidu dpdk-hinic3
cd dpdk-hinic3
```

### 安装编译
```bash
sh install.sh ../dpdk-stable-22.11.9 install bifur
sh install.sh ../dpdk-stable-22.11.9 replace baidu
sh install.sh ../dpdk-stable-22.11.9 build generic
```
