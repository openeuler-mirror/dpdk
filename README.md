# openEuler 开源仓 `hinic3` PMD使用指导


## 1. 简介
hinic3 driver是华为SPx系列网卡在DPDK框架下的用户态驱动，旨在基于DPDK使能SPx系列网卡，最大化释放SPx系列网卡能力。同时提供便携的使用工具，支持一键式安装部署。

---

## 2. 版本配套关系
| 版本号 | 配套固件版本 | 发布日期 | 版本特性 |
|--|--|--|--|
| [hinic3-26.0.rc1-0214.r1](https://atomgit.com/openeuler/dpdk/tree/hinic3-26.0.rc1-0214.r1) | 请咨询技术支撑 | 2026.02.14 | [release note](./release_note.md#hinic3-260rc1-0214r1) |
| [hinic3-26.0.rc1-0307.r1](https://atomgit.com/openeuler/dpdk/tree/hinic3-26.0.rc1-0307.r1) | [IN220 2.6.RC2](https://support.huawei.com/enterprise/zh/huawei-computing-components/in220-pid-253287505/software/268032408?idAbsPath=fixnode01&#124;23710424&#124;251364417&#124;9856629&#124;253287505) | 2026.03.07 | [release note](./release_note.md#hinic3-260rc1-0307r1) |
| [hinic3-26.0.rc1-0307.r2](https://atomgit.com/openeuler/dpdk/tree/hinic3-26.0.rc1-0307.r2) | [IN220 2.6.RC2](https://support.huawei.com/enterprise/zh/huawei-computing-components/in220-pid-253287505/software/268032408?idAbsPath=fixnode01&#124;23710424&#124;251364417&#124;9856629&#124;253287505) | 2026.03.07 | [release note](./release_note.md#hinic3-260rc1-0307r2) |
| [hinic3-26.0.rc1-0313.r1](https://atomgit.com/openeuler/dpdk/tree/hinic3-26.0.rc1-0313.r1) | [IN220 2.6.RC2](https://support.huawei.com/enterprise/zh/huawei-computing-components/in220-pid-253287505/software/268032408?idAbsPath=fixnode01&#124;23710424&#124;251364417&#124;9856629&#124;253287505) | 2026.03.13 | [release note](./release_note.md#hinic3-260rc1-0313r1) |
 	 
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
进入dpdk-hinic3目录，用户可使用驱动提供的安装脚本`install.sh`按需选择进行安装编译。

>**说明：**
> - 此处../dpdk-stable-21.11.9需替换为实际的dpdk版本及路径
> - 安装脚本会自动检测目标DPDK目录是否为Git仓库，如果不是，会自动初始化Git。

### SP200&SP600 网卡
**普通场景**
```bash
sh install.sh ../dpdk-stable-21.11.9 install
sh install.sh ../dpdk-stable-21.11.9 build
```

**bifur分流场景**
```bash
sh install.sh ../dpdk-stable-21.11.9 install bifur
sh install.sh ../dpdk-stable-21.11.9 build
```

### SP900 DPU卡
```bash
sh install.sh ../dpdk-stable-21.11.9 install bifur
sh install.sh ../dpdk-stable-21.11.9 build generic
```

---

## 4. 特性列表
以下为支持的特性清单，关于特性的详细介绍请参见[DPDK社区](https://doc.dpdk.org/guides/nics/features.html#)。
### 通用特性
| Feature                    | PF | VF | Feature               | PF | VF | Feature              | PF | VF  |
|----------------------------|----|----|-----------------------|----|----|----------------------|----|-----|
| Speed capabilities         | Y  | Y  | RSS key update        | Y  | Y  | Rx descriptor status |    |     |
| Link speed configuration   | Y  | Y  | RSS reta update       | Y  | Y  | Tx descriptor status |    |     |
| Link status                | Y  | Y  | Inner RSS             |    |    | Tx queue count       |    |     |
| Link status event          | Y  | Y  | VMDq                  |    |    | Basic stats          | Y  | Y   |
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
| Unicast MAC filter   | Y | Y | Inner L4 checksum   | Y | Y | Design doc |   |    |
| Multicast MAC filter | Y | Y | Packet type parsing | Y | Y | Perf doc   |   |    |
| RSS hash             | Y | Y | Timesync            |   |   |            |   |    |
### 自定义特性
| Feature         | PF  | VF |
|---------------|---|----|
| Traffic bifur | Y |    |
| Queue pool    | Y | Y  |
| VF Flow spilt | Y |    |
| Hairpin       | Y | Y  |


## 5. 使用约束
DPU场景下发以下流规则时，会导致管理口的SSH登录报文被送到用户态，导致DPU断链。
> 避免在该场景使用以下流规则。
```
flow create port_id ingress pattern eth / ipv4 / end actions queue index queue_id / end
flow create port_id ingress pattern eth / ipv4 / tcp / end actions queue index queue_id / end
flow create port_id ingress pattern eth / ipv4 / end actions rss queues queue_num end / end
flow create port_id ingress pattern eth / ipv4 / tcp / end actions rss queues queue_num end / end
```
