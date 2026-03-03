# openEuler 开源仓 `hinic3` PMD使用指导


## 1. 简介
hinic3 driver是华为SPx系列网卡在DPDK框架下的用户态驱动，旨在基于DPDK使能SPx系列网卡，最大化释放SPx系列网卡能力。同时提供便携的使用工具，支持一键式安装部署。

---

## 2. 版本配套关系
| 版本号 | 配套固件版本|发布日期 | commit id |版本特性|
|--|--|--|--|--|
| hinic3-26.0.rc1-0214.r1 | POC版本 | 2026.02.14 | cc93d8d3cb28138637e4174597d2b405e277278e |
 	 
---

## 3. 安装使用
本章节以DPDK 21.11为例，介绍如何在DPDK中集成并编译`hinic3` PMD。
PMD已归一到本项目的hinic3目录中，使用方式由原先的每个版本单独打patch，变为使用`install.sh`脚本自动安装hinic3到源码目录中。

- 当前`hinic3` PMD支持的DPDK版本：19.11 ~ 25.11
- 分流功能支持的DPDK版本：19.11 ~ 22.11
### 3.1 安装编译依赖
```bash
yum install -y git gcc libatomic python3-devel meson ninja-build python3-pyelftools libibverbs numactl numactl-devel zlib-devel`
```

### 3.2 下载DPDK

DPDK官方源码包下载链接可在[https://core.dpdk.org/download/](https://core.dpdk.org/download/) 获取，例如下载DPDK 21.11.9：
```bash
wget https://fast.dpdk.org/rel/dpdk-21.11.9.tar.xz
tar -xf dpdk-21.11.9.tar.xz
# 解压后目录名：dpdk-stable-21.11.9
```

### 3.3 获取hinic3 PMD源码
- 方法一：直接下载
  下载后解压
  ```bash
  unzip dpdk-hinic3.zip
  # 解压后目录名：dpdk-hinic3
  ```
- 方法二：Git克隆
  ```bash
  git clone https://atomgit.com/openeuler/dpdk.git -b hinic3 dpdk-hinic3
  # 默认目录名是dpdk，这里指定为了：dpdk-hinic3
  ```

### 3.4 编译
进入dpdk-hinic3目录，请用户按需选择进行安装编译：

### SP200&SP600 网卡
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
安装脚本会自动检测目标DPDK目录是否为Git仓库，如果不是，会自动初始化Git。

安装脚本会自动判断 DPDK 版本：
  - DPDK版本 = 19.11时，使用 `make` 编译。
  - DPDK版本 ≥ 20.11， 使用 `meson + ninja` 编译。

---

## 4. 特性列表
以下为支持的特性清单，关于特性的详细介绍请参见[DPDK社区](https://doc.dpdk.org/guides/nics/features.html#)。
### 通用特性
| Feature                    | PF | VF | Feature               | PF | VF | Feature              | PF | VF  |
|----------------------------|----|----|-----------------------|----|----|----------------------|----|-----|
| Speed capabilities         | Y  | Y  | RSS key update        | Y  | Y  | Rx descriptor status |    |     |
| Link speed configuration   | Y  | Y  | RSS reta update       | Y  | Y  | Tx descriptor status |    |     |
| Link status                | Y  | Y  | Inner RSS             |    |    | Tx queue count       |    |     |
| Link status event          |    |    | VMDq                  |    |    | Basic stats          | Y  | Y   |
| Removal event              |    |    | SR-IOV                | Y  | Y  | Extended stats       | Y  | Y   |
| Queue status event         |    |    | DCB                   | Y  | Y  | Stats per queue      | Y  | Y   |
| Rx interrupt               | Y  | Y  | VLAN filter           | Y  | Y  | FW version           | Y  | Y   |
| Lock-free Tx queue         |    |    | Flow control          | P  | P  | EEPROM dump          |    |     |
| Fast mbuf free             |    |    | Rate limitation       |    |    | Module EEPROM dump   |    |     |
| Free Tx mbuf on demand     |    |    | Congestion management |    |    | Registers dump       |    |     |
| Queue start/stop           | Y  | Y  | Traffic manager       |    |    | LED                  |    |     |
| Runtime Rx queue setup     |    |    | Inline crypto         |    |    | Multiprocess aware   | Y  | Y   |
| Runtime Tx queue setup     |    |    | Inline protocol       |    |    | FreeBSD              |    |     |
| Shared Rx queue            |    |    | CRC offload           |    |    | Linux                | Y  | Y   |
| Burst mode info            |    |    | VLAN offload          | Y  | Y  | Windows              |    |     |
| Power mgmt address monitor |    |    | QinQ offload          | Y  | Y  | ARMv7                |    |     |
| MTU update                 | Y  | Y  | FEC                   | Y  | Y  | ARMv8                | Y  | Y   |
| Buffer split on Rx         |    |    | IP reassembly         |    |    | LoongArch64          |    |     |
| Scattered Rx               | Y  | Y  | L3 checksum offload   | Y  | Y  | Power8               |    |     |
| LRO                  | Y | Y | L4 checksum offload | Y | Y | rv64       |   |    |
| TSO                  | Y | Y | Timestamp offload   |   |   | x86-32     |   |    |
| Promiscuous mode     | Y | Y | MACsec offload      |   |   | x86-64     | Y | Y  |
| Allmulticast mode    | Y | Y | Inner L3 checksum   | Y | Y | Usage doc  |   |    |
| Unicast MAC filter   |   |   | Inner L4 checksum   | Y | Y | Design doc |   |    |
| Multicast MAC filter |   |   | Packet type parsing | Y | Y | Perf doc   |   |    |
| RSS hash             | Y | Y | Timesync            |   |   |            |   |    |
### 自定义特性
| Feature         | PF  | VF |
|---------------|---|----|
| Traffic bifur | Y |    |
| Queue pool    | Y | Y  |
| VF Flow spilt | Y |    |
| Hairpin       | Y | Y  |


## 5. 特别说明
DPU场景下发以下流规则时，会导致管理口的SSH登录报文被送到用户态，导致DPU断链。
```
flow create port_id ingress pattern eth / ipv4 / end actions queue index queue_id / end
flow create port_id ingress pattern eth / ipv4 / tcp / end actions queue index queue_id / end
flow create port_id ingress pattern eth / ipv4 / end actions rss queues queue_num end / end
flow create port_id ingress pattern eth / ipv4 / tcp / end actions rss queues queue_num end / end
```
