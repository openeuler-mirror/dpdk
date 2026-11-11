# hns5 crypto 验证指南

验证 hns5 crypto 驱动的 SM4-CBC 和 SM3-HMAC（含链式模式）正确性。

## 验证结果

| 测试 | 状态 | 说明 |
|------|------|------|
| SM4-CBC cipher-only throughput | ✅ 通过 | 5 个 buffer size 全过，0 失败 |
| SM4-CBC + SM3-HMAC chain throughput | ✅ 通过 | 5 个 buffer size 全过，0 失败 |
| SM4-CBC 国标 verify | ✅ 通过 | 密文匹配 GM/T 0002-2012 标准值 |
| SM3-HMAC auth-only throughput | ✅ 通过 | 100 ops 全过，0 失败 |

SM4-CBC 密文 = `681edf34d206965e86b3e94f536e4246`（GM/T 0002-2012 国标），`Failed Ops: 0`。

## 前提

> **POE group 分配约束**（平台规格：CPU 侧 group 0~31、每组 8 队列；ACC 侧 group 32~63、每组 4 队列）
>
> - **group 1 禁止使用**（内核 SSH 走它）：`share_group=1` 会在参数解析阶段直接报错并退出探测。
> - **CPU group 24~31 禁止使用**：`share_group` 落在该区间同样报错退出，所以合法的 `share_group` 是 0~23 去掉 1 和 12。
> - **CPU group 12 被 crypto/hns5 占用**（作为 SEC completion group），收包路径不得使用。
> - **ACC group 50~63 禁止使用**：SEC 引擎可用的 ACC 组只有 32~49，且与加速器绑定的虚拟 group 必须从 32 起；`hns5_sec_platform_inspect()` 会校验 BSP 报出的 ACC 窗口必须落在 `[32, 50)`。
> - 与逻辑 CPU 绑定的虚拟 group 编号必须从 0 开始。
> - 探测阶段还会用 BSP 的 `ndrv_poe_pf_get_grp_range()` 交叉校验：`share_group` 必须落在 CPU 可访问区间内，且不能落在加速器（SEC）区间内。通过时日志形如
>   `share_group:3 accepted (cpu range [0, 24), acc range [32, 50))`。

```bash
git pull origin hns5
./buildtools/hns5_compile.sh --debug
./usertools/hns5_clean.sh
```

## 验证入口

本指南不提供包装脚本，5 个用例分别执行对应章节里的命令：

| 用例 | 章节 | 判据要点 |
|---|---|---|
| SM4-CBC cipher-only throughput | 第 1 节 | Failed Enq/Deq = 0，无错误完成 |
| SM3-HMAC auth-only throughput | 第 2 节 | 100 ops 全过，0 失败 |
| SM4-CBC + SM3-HMAC 链式 throughput | 第 3 节 | 同上；功能用例 1000 ops，性能用例见该节 |
| SM4-CBC 国标 verify | 第 4 节 | 密文 = `681edf34d206965e86b3e94f536e4246`，Failed Ops = 0 |
| l2fwd-crypto 用法 | 第 5 节 | 转发与加解密结果正常 |

> **命令执行目录**：以下所有命令均在 `dpdk-stable-22.11.11/` 下执行 ——
> 编译与清理脚本分别在 `buildtools/`、`usertools/`，测试材料在仓库根的 `test/`。
> 二进制默认取 `build/app/dpdk-test-crypto-perf`（release）；若用
> `./buildtools/hns5_compile.sh --debug` 构建，路径改为
> `build-debug/app/dpdk-test-crypto-perf`。

---

## 1. SM4-CBC throughput

验证 SEC 引擎能处理 SM4-CBC 加密，无错误完成。

```bash
./build/app/dpdk-test-crypto-perf -l 0-1 --vdev crypto_hns5 -- \
    --devtype crypto_hns5 \
    --ptest throughput --optype cipher-only \
    --cipher-algo sm4-cbc --cipher-op encrypt \
    --cipher-key-sz 16 --cipher-iv-sz 16 \
    --buffer-sz 64,128,256,512,1024 \
    --total-ops 1000 --burst-sz 32
```

**通过标准**：`Dequeued = 1000`, `Failed Enq = 0`, `Failed Deq = 0`

---

## 2. SM3-HMAC 验证

SM3-HMAC 单独验证（auth-only throughput）：

```bash
./build/app/dpdk-test-crypto-perf -l 0-1 --vdev crypto_hns5 -- \
    --devtype crypto_hns5 \
    --ptest throughput --optype auth-only \
    --auth-algo sm3-hmac --auth-op generate \
    --auth-key-sz 16 --digest-sz 32 \
    --buffer-sz 64 --total-ops 100 --burst-sz 32
```

**通过标准**：`Dequeued = 100`, `Failed Enq = 0`, `Failed Deq = 0`

> SM3-HMAC 的 verify 模式需要测试向量文件（包含正确的 digest 值）。
> 可先跑 throughput 生成 MAC，再用 SM3 参考实现比对。

---

## 3. SM4-CBC + SM3-HMAC 链式 throughput

验证 chain 模式 BD（双 key、双地址、CIPHER_AUTH 序列）正确。

```bash
./build/app/dpdk-test-crypto-perf -l 0-1 --vdev 'crypto_hns5,stats=0' -- \
    --devtype crypto_hns5 \
    --ptest throughput --optype cipher-then-auth \
    --cipher-algo sm4-cbc --cipher-op encrypt \
    --cipher-key-sz 16 --cipher-iv-sz 16 \
    --auth-algo sm3-hmac --auth-op generate \
    --auth-key-sz 16 --digest-sz 32 \
    --buffer-sz 64,128,256,512,1024 \
    --total-ops 1000 --burst-sz 32
```

性能测试

```bash
./build/app/dpdk-test-crypto-perf -l 0-4 --vdev 'crypto_hns5,qps=4,stats=0' -- \
    --devtype crypto_hns5 \
    --ptest throughput --optype cipher-then-auth \
    --cipher-algo sm4-cbc --cipher-op encrypt \
    --cipher-key-sz 16 --cipher-iv-sz 16 \
    --auth-algo sm3-hmac --auth-op generate \
    --auth-key-sz 16 --digest-sz 32 \
    --buffer-sz 1024 \
    --total-ops 1000000 --burst-sz 32
```

---

## 4. SM4-CBC verify（国标比对）

用 GM/T 0002-2012 SM4 标准测试向量验证密文正确性。

### 测试向量

| 字段 | 值 |
|------|-----|
| cipher_key | `0123456789abcdeffedcba9876543210` |
| cipher_iv | `00000000000000000000000000000000` |
| plaintext | `0123456789abcdeffedcba9876543210` |
| **期望密文** | `681edf34d206965e86b3e94f536e4246` |

IV 全零时，CBC = ECB，密文应等于 SM4 单块标准结果。

### 命令

```bash
./build/app/dpdk-test-crypto-perf -l 0-1 --vdev crypto_hns5 -- \
    --devtype crypto_hns5 \
    --ptest verify --optype cipher-only \
    --cipher-algo sm4-cbc --cipher-op encrypt \
    --cipher-key-sz 16 --cipher-iv-sz 16 \
    --buffer-sz 16 --total-ops 1 --burst-sz 1 \
    --test-file ../test/sm4_cbc_verify.vec
```

### 判读

- **PASS**：SEC 引擎 SM4-CBC 密文 = GM/T 0002-2012 国标值，密码学正确
- **FAIL**：密文不正确，检查 BD 中 `cipher_alg`(0x3)/`mode`(0x1)/`keylen` 设置

---

## 5. l2fwd-crypto

l2fwd-crypto 做 L2 转发 + 加密：收包 → SM4-CBC 加密 + SM3-HMAC → 转发。

### 前提

> **POE group 分配约束**（平台规格：CPU 侧 group 0~31、每组 8 队列；ACC 侧 group 32~63、每组 4 队列）
>
> - **group 1 禁止使用**（内核 SSH 走它）：`share_group=1` 会在参数解析阶段直接报错并退出探测。
> - **CPU group 24~31 禁止使用**：`share_group` 落在该区间同样报错退出，所以合法的 `share_group` 是 0~23 去掉 1 和 12。
> - **CPU group 12 被 crypto/hns5 占用**（作为 SEC completion group），收包路径不得使用。
> - **ACC group 50~63 禁止使用**：SEC 引擎可用的 ACC 组只有 32~49，且与加速器绑定的虚拟 group 必须从 32 起；`hns5_sec_platform_inspect()` 会校验 BSP 报出的 ACC 窗口必须落在 `[32, 50)`。
> - 与逻辑 CPU 绑定的虚拟 group 编号必须从 0 开始。
> - 探测阶段还会用 BSP 的 `ndrv_poe_pf_get_grp_range()` 交叉校验：`share_group` 必须落在 CPU 可访问区间内，且不能落在加速器（SEC）区间内。通过时日志形如
>   `share_group:3 accepted (cpu range [0, 24), acc range [32, 50))`。

- 板子有可用网口（hns5 net 驱动，vdev 方式创建）
- 编译脚本已包含 `-Dexamples=l2fwd-crypto`

### 网口创建（vdev，非 PCI）

hns5 网口是 vdev 设备，用 `--vdev` 创建，不是 PCI 自动探测：

```bash
# 创建 2 个网口（port 0 和 port 1）
# hw_txq_id 按 16 递增：port0=0, port1=16, port2=32 ...
--vdev 'net_hns5_0,port=0,mode=10GBASE_R,hw_txq_id=0' \
--vdev 'net_hns5_1,port=1,mode=10GBASE_R,hw_txq_id=16' \
--huge-dir /dev/hugepages-32M
```

### 运行

```bash
./build/examples/dpdk-l2fwd-crypto -l 0-1 \
    --vdev crypto_hns5 \
    --vdev 'net_hns5_0,port=0,mode=10GBASE_R,hw_txq_id=0' \
    --vdev 'net_hns5_1,port=1,mode=10GBASE_R,hw_txq_id=16' \
    --huge-dir /dev/hugepages-32M \
    -- \
    --cdev_type HW \
    --chain CIPHER_HASH \
    --cipher_algo sm4-cbc \
    --cipher_op ENCRYPT \
    --cipher_key 01:23:45:67:89:ab:cd:ef:fe:dc:ba:98:76:54:32:10 \
    --cipher_iv 00:01:02:03:04:05:06:07:08:09:0a:0b:0c:0d:0e:0f \
    --auth_algo sm3-hmac \
    --auth_op GENERATE \
    --auth_key ff:ee:dd:cc:bb:aa:99:88:77:66:55:44:33:22:11:00 \
    --digest_size 32
```

> **注意**：
> - hns5 网口是 vdev 设备，需用 `--vdev 'net_hns5_N,port=M,mode=...,hw_txq_id=K'` 创建
> - `hw_txq_id` 按 16 递增（port0=0, port1=16, port2=32 ...）
> - `--huge-dir /dev/hugepages-32M` 指定 32MB 大页目录
> - l2fwd-crypto 参数用下划线（`--cipher_algo`），不是连字符
> - `--chain CIPHER_HASH` = cipher-then-auth = 先加密后认证

### 验证

在端口 1 用抓包工具（tcpdump / Wireshark）捕获转发出的包：
- 检查 payload 是否为 SM4-CBC 密文
- 检查包尾是否附加了 32 字节 SM3-HMAC MAC

---

## 参数说明

### test-crypto-perf（连字符参数）

| 参数 | SM4-CBC | SM3-HMAC | 说明 |
|------|---------|----------|------|
| `--cipher-algo` | sm4-cbc | — | 国密分组加密 |
| `--cipher-key-sz` | 16 | — | SM4 固定 128 位 |
| `--cipher-iv-sz` | 16 | — | CBC IV = 分组大小 |
| `--auth-algo` | — | sm3-hmac | 国密 HMAC |
| `--auth-key-sz` | — | 16 | HMAC 密钥 |
| `--digest-sz` | — | 32 | SM3 摘要 256 位 |
| `--optype` | cipher-only | auth-only | cipher-then-auth = 链式 |
| `--ptest` | throughput/verify | throughput/verify | verify 需测试向量 |

### l2fwd-crypto（下划线参数，注意区别）

| 参数 | 值 | 说明 |
|------|-----|------|
| `--cdev_type` | HW | 硬件加速器 |
| `--chain` | CIPHER_HASH | 先加密后认证（= cipher-then-auth） |
| `--cipher_algo` | sm4-cbc | 国密分组加密 |
| `--cipher_op` | ENCRYPT/DECRYPT | 加密/解密 |
| `--cipher_key` | 01:23:... | 冒号分隔十六进制 |
| `--cipher_iv` | 00:01:... | 冒号分隔十六进制 |
| `--auth_algo` | sm3-hmac | 国密 HMAC |
| `--auth_op` | GENERATE/VERIFY | 生成/验证 |
| `--auth_key` | ff:ee:... | 冒号分隔十六进制 |
| `--digest_size` | 32 | SM3 摘要 256 位 |

## BMU 池的块划分与在途深度

`HNS5_BMU_BLOCKS = 7`（`hns5_crypto_pmd_private.h`）只用在 BMU VF 的
`max_block_num` / `min_block_num`，是 SEC 池注册给 BMU 的**物理块划分数**，
不是并发上限，也不限制 `--burst-sz`。

在途能力由 inflight cap 与 buffer 预算决定，实际数值在 probe 日志里打印：

```text
buffer budget: cache=64, in-flight cap=128 per queue pair (4 qps)
```

- 单 QP：cap ≈ buffer 数 − cache − 8（32 MiB 池为 1792 → 约 1720）
- 多 QP：cap 钳到 128（`HNS5_SEC_MAX_INFLIGHT_PER_QP`），实测 4 QP/1024 B 为
  21.2 Gbps，把 cap 放到 192 反而降到 20.1 Gbps（管道更深只拉长完成延迟）
- `--burst-sz` 只要不超过 cap 即可，本指南用例统一取 32；`--total-ops` 可以
  远大于 cap，buffer 会循环复用
