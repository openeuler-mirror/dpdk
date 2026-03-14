# OpenEuler 开源仓 `hinic3` PMD 使用简介

本文以 **DPDK 21.11** 为例，介绍如何在 DPDK 中集成并编译 `hinic3` PMD。

PMD 已归一到本项目的 hinic3 目录中，使用方式由原先的每个版本单独打 patch，变为了使用 `install.sh` 脚本自动安装 hinic3 到源码目录中。

- 当前 `hinic3` PMD 支持的 DPDK 版本：**19.11 ~ 25.11**
- 分流功能支持的 DPDK 版本：**19.11 ~ 22.11**
- 注意点：
  - DPU场景下发以下流规则时，会导致管理口的SSH登录报文被送到用户态，导致DPU断链
  ```
    flow create port_id ingress pattern eth / ipv4 / end actions queue index queue_id / end
    flow create port_id ingress pattern eth / ipv4 / tcp / end actions queue index queue_id / end
    flow create port_id ingress pattern eth / ipv4 / end actions rss queues queue_num end / end
    flow create port_id ingress pattern eth / ipv4 / tcp / end actions rss queues queue_num end / end
  ```
---

## 1. 环境准备
### 1.1 安装编译依赖
`yum install -y git gcc libatomic python3-devel meson ninja-build python3-pyelftools libibverbs numactl numactl-devel zlib-devel`

### 1.2 下载 DPDK

DPDK 官方源码包下载链接可在 [https://core.dpdk.org/download/](https://core.dpdk.org/download/) 获取，例如下载 **DPDK 21.11.9**：
```bash
wget https://fast.dpdk.org/rel/dpdk-21.11.9.tar.xz
tar -xf dpdk-21.11.9.tar.xz
# 解压后目录名：dpdk-stable-21.11.9
```

### 1.3 获取 hinic3 PMD 源码
方法一：直接下载
下载后解压
```bash
unzip dpdk-hinic3.zip
# 解压后目录名：dpdk-hinic3
```
方法二：Git 克隆
```bash
git clone https://atomgit.com/openeuler/dpdk.git -b hinic3 dpdk-hinic3
# 默认目录名是dpdk，这里指定为了：dpdk-hinic3
```

---

## 2. 安装 hinic3 PMD 到 DPDK
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

安装脚本会自动检测目标 DPDK 目录是否为 Git 仓库，如果不是，会自动初始化 Git。

---

## 3. 编译
以下按需二选一执行
```bash
# 直接编译
sh install.sh ../dpdk-stable-21.11.9 build

# 如果是 DPU 场景编译
sh install.sh ../dpdk-stable-21.11.9 build generic
```

安装脚本会自动判断 DPDK 版本：
  - **DPDK 19.11** 使用 `make` 编译
  - **DPDK ≥ 20.11** 使用 `meson + ninja` 编译

---

## 4. 快速示例
### 依赖下载
```bash
# 安装 DPDK 依赖
yum install -y git gcc libatomic python3-devel meson ninja-build python3-pyelftools libibverbs numactl numactl-devel zlib-devel

# 下载并解压 DPDK
wget https://fast.dpdk.org/rel/dpdk-21.11.9.tar.xz
tar -xf dpdk-21.11.9.tar.xz

# 获取 hinic3 PMD
git clone https://atomgit.com/openeuler/dpdk.git -b hinic3 dpdk-hinic3
cd dpdk-hinic3
```

### SP220&SP600&SP230 网卡
```bash
sh install.sh ../dpdk-stable-21.11.9 install
sh install.sh ../dpdk-stable-21.11.9 build
```

### SP600 标准网卡 分流场景
```bash
sh install.sh ../dpdk-stable-21.11.9 install bifur
sh install.sh ../dpdk-stable-21.11.9 build
```

### SP900 DPU卡
```bash
sh install.sh ../dpdk-stable-21.11.9 install bifur
sh install.sh ../dpdk-stable-21.11.9 build generic
```

## 5. 其它示例
- [QoS 使用示例](docs/qos/qos.md)
