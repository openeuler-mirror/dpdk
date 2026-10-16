# HNS5 DPDK 20.11 Patch 使用指南

## 概述

本 patch 为 DPDK 20.11.10 添加 HNS5 网卡驱动支持，主要包括：

- **新增 `hns5` 网卡驱动** (`drivers/net/hns5/`)：包含以太网收发、RSS、FDIR、流量管理等功能
- **新增 `hns5` 内存池驱动** (`drivers/mempool/hns5/`)：提供 BMU 内存池管理
- **修改 test-pmd**：适配 hns5 网卡的 RETA 表大小和内存池操作
- **修改 l3fwd 示例**：适配 hns5 网卡的 offload 特性
- **修改 librte_mempool**：新增内存池 ops 标志位支持
- **新增编译脚本** `hns5_compile.sh`（aarch64 本地编译，自动解压 SDK 头文件、生成桩库）和打包脚本 `hns5_pack.sh`

---

## 特性列表

> 以下特性列表基于 patch 源码分析得出，参考了 [hinic3 PMD 特性列表](https://atomgit.com/openeuler/dpdk/tree/hinic3_master#4-特性列表) 的格式。

### 通用特性

| Feature | 支持 | Feature | 支持 | Feature | 支持 |
|---------|:----:|---------|:----:|---------|:----:|
| Speed capabilities | - | RSS hash | Y | Rx descriptor status | - |
| Link speed configuration | - | RSS key update | Y | Tx descriptor status | - |
| Link status | Y | RSS reta update | Y | FW version | - |
| Tx queue count | - | VMDq | - | | |
| Link status event | - | Basic stats | Y | Stats per queue | Y |
| Removal event | - | Extended stats | Y | Queue status event | - |
| Rx interrupt | - | VLAN filter | - | FW version | - |
| DCB | - | EEPROM dump | - | Lock-free Tx queue | - |
| SR-IOV | - | Module EEPROM dump | - | Flow control | - |
| Fast mbuf free | - | Registers dump | - | | |
| Rate limitation | - | Traffic manager | - | LED | - |
| Free Tx mbuf on demand | - | Congestion management | - | | |
| Multiprocess aware | - | Queue start/stop | - | | |
| Runtime Rx queue setup | - | Inline crypto | - | Linux | Y |
| Runtime Tx queue setup | - | Inline protocol | - | FreeBSD | - |
| Shared Rx queue | - | CRC offload | Y | Windows | - |
| Burst mode info | - | VLAN offload | Y | ARMv8 | Y |
| Power mgmt address monitor | - | QinQ offload | - | ARMv7 | - |
| MTU update | Y | FEC | - | Power8 | - |
| Buffer split on Rx | - | IP reassembly | - | rv64 | - |
| Scattered Rx | Y | L3 checksum offload | Y | x86-32 | - |
| LRO | - | L4 checksum offload | Y | x86-64 | - |
| TSO | - | Timestamp offload | - | Usage doc | - |
| Promiscuous mode | Y | MACsec offload | - | Design doc | - |
| Allmulticast mode | Y | Inner L3 checksum | - | Perf doc | - |
| Unicast MAC filter | Y | Inner L4 checksum | - | | |
| Multicast MAC filter | - | Packet type parsing | Y | | |

### Rx Offload 能力

| Offload | 支持 | 说明 |
|---------|:----:|------|
| Scattered Rx | Y | 支持分散接收 |
| RSS Hash | Y | 接收报文带 RSS hash 值 |
| Keep CRC | Y | 保留 CRC（可选关闭 CRC 校验） |
| VLAN Strip | Y | 接收侧 VLAN 剥离 |
| Checksum | - | **不支持 RX Checksum 卸载**，请求会导致 `dev_configure` 失败 |
| LRO | - | 不支持 |
| Jumbo Frame | - | 不支持（MTU 受限） |


### 使用约束

1. **必须使用 hns5_bmu 内存池**：网卡驱动依赖自有的 BMU 内存池驱动（`drivers/mempool/hns5/`），不能使用标准 `rte_pktmbuf_pool_create`。test-pmd 已修改为调用 `rte_pktmbuf_pool_create_by_ops(..., "hns5_bmu")`。
2. **MAC 地址数量限制**：`max_mac_addrs = 1`，仅支持单播 MAC 设置（`mac_addr_set`），不支持多播 MAC 增删（`mac_addr_add`/`mac_addr_remove`）。
3. **RSS RETA 超大**：RETA 表大小为 1024，超过 DPDK 原生 512 上限。test-pmd 中已移除 `ETH_RSS_RETA_SIZE_512` 的限制检查，并将 `reta_conf` 数组从 8 扩展到 16。
4. **需 SDK 依赖，运行需 BSP**：编译时需要 `S920L18_SDK_004.zip` 提供 uDriver（hidrv/ndrv）头文件，脚本自动解压到 `drivers/net/hns5/dependency/`；链接期使用自动生成的**桩库** `libhidrv_api.so`（空实现），运行期由模组 BSP 提供的真实 `/lib64/libhidrv_api.so` 接管（驱动 RUNPATH 自动回退）。
5. **不支持 VLAN Filter**：仅支持 VLAN strip/insert offload，不支持 VLAN ID 过滤。
6. **不支持 Tx Traffic Manager API**：驱动内部使用 TM 进行 Tx 队列绑定，但未对外暴露 `rte_tm` API。
7. **混杂模式下不支持 VLAN 剥离**：开启混杂模式（promiscuous mode）时，VLAN strip offload 不生效。
8. **报文类型解析（PTYPE）范围有限**：报文解析目前仅支持 IPv4 / IPv6 / TCP / UDP，其他协议类型不识别。
9. **xstats 统计口径**：xstats 目前统计的是 **MAC 层**的计数，非以太网端口级统计，数据口径与预期可能不一致。
10. **非混杂模式下 MAC 地址随机生成**：非混杂模式下 MAC 地址为随机生成，每次启动都会变化，不固定。

---

## 1. 下载 DPDK 源码

```bash
wget https://fast.dpdk.org/rel/dpdk-20.11.10.tar.xz
```

## 2. 解压源码

```bash
tar -xf dpdk-20.11.10.tar.xz
```

解压后得到目录 `dpdk-20.11.10`，进入该目录：

```bash
cd dpdk-20.11.10
```

## 3. 应用 Patch

patch 由 `diff -ruN` 生成（非 git format-patch 格式），将 `hns5_20_11.patch` 复制到源码根目录，在 `dpdk-20.11.10` 目录内执行：

```bash
patch -p1 < hns5_20_11.patch
```

> **注意**：如遇空白差异导致失败，可加 `--ignore-whitespace -l` 重试；仍失败则手动处理生成的 `.rej` 文件。

## 4. 编译

### 4.1 环境要求

编译脚本 `hns5_compile.sh` **仅支持在 aarch64 主机上本地编译**（x86 交叉编译已移除）。在 aarch64 服务器（openEuler/CentOS 系）上安装工具链：

```bash
dnf install -y gcc meson ninja-build pkgconf-pkg-config numactl-devel unzip
```

要求：`/usr/include/numa.h` 存在（numactl-devel），`gcc`/`meson`/`ninja`/`pkg-config`/`nm`/`unzip` 可用。

### 4.2 获取 SDK

从[华为支持网站](https://support.huawei.com/enterprise/zh/edge-computing-and-server/s920l18-pid-267076760/software/267397720?idAbsPath=fixnode01|23710424|251364417|261608284|262279635|267076760)下载 `S920L18_SDK_004.zip`，放到 **dpdk 源码目录的上层**（与 `dpdk-20.11.10` 同级）：

```
.
├── S920L18_SDK_004.zip
└── dpdk-20.11.10/    ← 应用过 patch 的源码目录
```

SDK 提供驱动所依赖的 uDriver（hidrv/ndrv）API 头文件。

### 4.3 执行编译

在 dpdk 源码根目录执行：

```bash
./hns5_compile.sh
```

SDK zip 不在默认位置时，三种方式任选其一：

```bash
./hns5_compile.sh /path/to/S920L18_SDK_004.zip        # 方式一：位置参数
SDK_ZIP=/path/to/S920L18_SDK_004.zip ./hns5_compile.sh  # 方式二：环境变量
SDK_DIR=/path/to/extracted_sdk ./hns5_compile.sh       # 方式三：已解压的 SDK 目录
```

可选环境变量：`CHIP`（默认S920L18 的 SoC）、`BD_VERSION`（默认 `v400`，单板版本）。

### 4.4 编译过程说明

脚本自动完成以下步骤，无需手动干预：

1. **检查工具链**：校验 aarch64 架构及 gcc/meson/ninja/pkg-config/numactl-devel
2. **解压 SDK 头文件**：提取 uDriver 的 hidrv/native 头文件到 `drivers/net/hns5/dependency/{hidrv,native}`
3. **生成桩库**：自动生成只含空实现的 `libhidrv_api.so` 供链接期使用（真实库在模组上运行时加载）
4. **meson/ninja 构建**：release + 动态库，构建 test-pmd、l3fwd 示例及 hns5 驱动；随后按 hns5 实际引用的符号重新生成精确桩库并完成全量链接

编译产物：

- `build/app/dpdk-testpmd` — test-pmd 可执行文件
- `build/drivers/*so*` — 各驱动动态库（含 `librte_net_hns5.so`、`librte_mempool_hns5.so`）
- `build/lib/*so*` — DPDK 核心动态库

## 5. 打包

编译成功后，在源码根目录执行：

```bash
./hns5_pack.sh
```

在当前目录生成两个文件：

| 文件 | 内容 |
|------|------|
| `build.tgz` | `build/drivers/*so*` + `build/lib/*so*`（DPDK 核心库 + hns5 驱动库） |
| `dpdk-testpmd` | test-pmd 可执行文件 |

## 6. 部署到模组环境运行

### 6.1 拷贝产物到模组

将 `build.tgz` 和 `dpdk-testpmd` 传输到模组（aarch64 目标板），例如通过 scp：

```bash
scp build.tgz dpdk-testpmd root@<模组IP>:/root/hns5/
```

也可通过 U 盘、TFTP 等方式拖动文件到模组环境。

### 6.2 解压并配置运行环境

在模组上执行：

```bash
mkdir -p /root/hns5 && cd /root/hns5
tar -xzvf build.tgz          # 解压出 build/drivers/*.so 和 build/lib/*.so
chmod +x dpdk-testpmd
export LD_LIBRARY_PATH=/root/hns5/build/lib:/root/hns5/build/drivers:$LD_LIBRARY_PATH
```

> **重要**：
> - **找不到 rte 库（librte_*.so）时**：必须将 `build/lib` 和 `build/drivers` 加入 `LD_LIBRARY_PATH`，程序启动时会从中加载 DPDK 核心库和 hns5 驱动库。典型报错：`error while loading shared libraries: librte_eal.so.21.11: cannot open shared object file`。
> - 编译产物中的 `libhidrv_api.so` 是**桩库**（空实现），仅用于链接；运行时驱动 RUNPATH 会回退加载模组 BSP 提供的真实 `/lib64/libhidrv_api.so`，请确认模组 BSP 已正确安装。
> - 建议将 `export LD_LIBRARY_PATH=...` 写入 `/etc/profile` 或启动脚本，避免每次手动设置：
>   ```bash
>   echo 'export LD_LIBRARY_PATH=/root/hns5/build/lib:/root/hns5/build/drivers:$LD_LIBRARY_PATH' >> /etc/profile
>   source /etc/profile
>   ```
> - 可用以下命令验证库路径是否生效：
>   ```bash
>   ldd dpdk-testpmd | grep "not found"      # 无输出即正常
>   ```

### 6.3 配置大页内存

hns5 驱动依赖大页内存，需在模组上配置 **32M 大页**并挂载（以 256 个大页、共 8G 为例，按实际内存调整）：

```bash
# 0. 确认模组支持的 32M 大页（应看到 hugepages-32768kB 目录）
ls /sys/kernel/mm/hugepages/

# 1. 配置大页数量（运行时生效，重启失效）
echo 256 > /sys/kernel/mm/hugepages/hugepages-32768kB/nr_hugepages

# 2. 挂载大页文件系统（指定 pagesize=32M）
mkdir -p /mnt/huge
mount -t hugetlbfs -o pagesize=32M nodev /mnt/huge

# 3. 验证
grep Huge /proc/meminfo     # 确认 HugePages_Free 数量正确
ls /mnt/huge                # 空目录即为正常，运行时会动态创建文件
```

如需重启后保留配置，将大页数量写入内核启动参数（如 GRUB：`hugepagesz=32M hugepages=256`），并将挂载项加入 `/etc/fstab`：

```
nodev  /mnt/huge  hugetlbfs  pagesize=32M  0  0
```

### 6.4 运行

环境就绪后即可运行 test-pmd，启动时需通过 `--huge-dir` 指定大页目录，命令与参数详见第 7 章：

```bash
./dpdk-testpmd -l 0-3 \
    --huge-dir /mnt/huge \
    --vdev "net_hns5_0,port=0,share_group=0,mode=10GBASE_R" \
    -- -i --rxq=4 --txq=4 --nb-cores=2 --forward-mode=io
```

---

## 7. 运行参数详解

hns5 驱动注册为 **vdev 虚拟设备驱动**（`RTE_PMD_REGISTER_VDEV(net_hns5, ...)`），不通过 PCI 扫描发现网卡，必须在 EAL 参数中通过 `--vdev` 显式创建端口。

### 7.1 vdev 参数说明

| 参数 | 必填 | 取值范围 | 默认值 | 说明 |
|------|:----:|---------|--------|------|
| `port` | 是 | 0~31 | 无 | CPU 侧端口 ID（cport），用于 POE 转发绑定 |
| `share_group` | 是 | 2~31 | 无 | RX 共享组 ID，需外部人工分配以避免多端口冲突。**避免使用 group 1**（控制网口占用） |
| `mode` | 是 | 见 7.2 节 | 无 | MAC 端口模式（物理接口协议类型） |
| `macport` | 否 | 0~31 | `port` 值 | MAC 侧端口 ID（mport） |
| `fw_tnl` | 否 | 0 或 1 | 1 | 硬件隧道分发表开关，`fw_tnl=0` 关闭 |
| `base_tnl` | 否 | 0~256 | `port * 8` | 隧道分发表基址（每 cport 占 8 项） |
| `hw_txq_id` | 否 | N | `port * 16` | 硬件 TM 发送队列起始编号 |
| `hw_txq_num` | 否 | N | 8 | 硬件 TM 发送队列数量 |

> **注意**：`port` 和 `macport` 均必须小于 32（驱动内部用于 `1U << id` 位图索引）。

### 7.2 支持的端口模式（`mode` 参数取值）

```
100BASE_FX | 1000BASE_X | SGMII | QSGMII | 2G5BASE_X | 10GBASE_R |
FLEXMII_5G2P | FLEXMII_10G2P | FLEXMII_20G2P | FLEXMII_10G4P | FLEXMII_20G4P |
5GBASE_R | 5GBASE_KR | XAUI | 25GBASE_R | 40GBASE_R | 50GBASE_R | 100GBASE_R
```

### 7.3 运行 test-pmd（基础收发测试）

```bash
./dpdk-testpmd -l 0-3 \
    --huge-dir /mnt/huge \
    --vdev "net_hns5_0,port=0,share_group=0,mode=10GBASE_R" \
    -- -i --rxq=4 --txq=4 --nb-cores=2 --forward-mode=io
```

参数说明：
- `--huge-dir /mnt/huge`：指定大页目录（见 6.3 节，需先配置并挂载 32M 大页）
- `--vdev "net_hns5,..."`：创建 hns5 端口，必填 `port`/`share_group`/`mode` 三项
- `--rxq=4 --txq=4`：收发队列数（最大收队列数受 `HNS5_POE_CPU_GRP_QUE_NUM` 限制）
- `--forward-mode=io`：IO 转发模式（同口收发）

进入交互模式后常用命令：

```
testpmd> show port info 0          # 查看端口信息（offload 能力、队列数、速率）
testpmd> show config rss           # 查看 RSS 配置
testpmd> set promisc all on        # 开启混杂模式
testpmd> set fwd io                # 设置转发模式
testpmd> start                     # 开始转发
testpmd> show port stats 0         # 查看收发统计
testpmd> stop                      # 停止转发
testpmd> quit                      # 退出
```

### 7.4 多端口运行

每个物理端口需要单独的 `--vdev`，`port` 和 `share_group` 不能重复：

```bash
./dpdk-testpmd -l 0-7 \
    --vdev "net_hns5_0,port=0,share_group=0,mode=10GBASE_R" \
    --vdev "net_hns5_1,port=1,share_group=1,mode=10GBASE_R" \
    -- -i --rxq=4 --txq=4 --nb-cores=4
```

### 7.5 test-pmd 运行注意事项

1. **内存池已固定为 hns5_bmu**：patch 将 testpmd 默认内存池分支（`MP_ALLOC_MEMZONE`）改为 `rte_pktmbuf_pool_create_by_ops(..., "hns5_bmu")`，内存池名固定为 `bmu_pool6`。**无需**（也不支持）再指定 `--mp-alloc` 其他模式。
2. **RSS RETA 查询/设置**：RETA 表为 1024 项，test-pmd 已适配，可直接使用：
   ```
   testpmd> show port 0 rss reta 64 (0,64)
   testpmd> set port 0 rss reta (0,64) (0,1)
   ```
3. **MTU 设置**：
   ```
   testpmd> set port 0 mtu 9000    # 需硬件支持，超限会告警并取最大值
   ```

### 7.6 常见启动失败排查

| 现象（日志关键字） | 原因 | 处理 |
|---|---|---|
| `no args:mode` | vdev 缺少 `mode` 参数 | 检查 `--vdev` 参数完整性 |
| `share_group must be specified` | 缺少 `share_group` 参数 | 必填项，需人工分配 |
| `invalid mode:xxx` | `mode` 拼写不在 7.2 列表内 | 核对取值 |
| `invalid cport_id/mport_id, must be < 32` | 端口号超界 | 使用 0~31 |
| `unsupport port speed:0x%x for port mode:%u` | 速率与模式不匹配 | 核对模式支持的速率 |
| `failed to hns5_tm_platform_dev_probe` | TM 平台设备初始化失败 | 检查 BSP/内核驱动加载状态 |
| `failed to hidrv_ppe_vf_open_pool` | PPE 内存池打开失败 | 检查 BMU 依赖与固件状态 |

