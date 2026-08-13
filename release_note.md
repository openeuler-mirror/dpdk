# Release Notes
## hinic3-26.2.rc1-0814.r1
### 更新说明

* 新增SP230网卡用户态模式全量功能
  - 支持用户态模式下基础收发包并确认报文可以rss分流。
  - 支持用户态模式下基础控制面使能（start/stop设备，start/stop队列）。
  - 支持用户态模式下fdir，action包括rss/queue，报文类型包括已支持的普通报文和隧道报文。
  - 支持用户态模式下TSO切片包括普通报文和隧道报文
  - 支持用户态模式下VLAN功能和QinQ功能
  - 约束：
    - 默认条件下对报文进行检查，错误报文不进入rss和fdir（hinicadm工具可修改寄存器配置）
    - 流表规格为单function最大1024个流表。
    - 不支持eth / ipv6 / udp / vxlan 流规则 
    - action为rss时，dpdk的nb_rx_queues最大为32个，最多支持7组rss规则的group
    - TSO报文最大片数为255片

* 新增队列SP230网卡池化模式功能，支持裸机场景。
  - 支持通过hinic5工具配置的分流模式文件，读取文件区分使能VFIO模式/队列池化模式。
  - 支持队列池化模式下基础控制面使能（start/stop设备，start/stop队列）。
  - 支持队列池化模式下基础收发包并确认报文可以rss分流。
  - 支持队列池化模式下fdir，action包括rss/queue，报文类型包括已支持的普通报文和隧道报文。
  - 约束：
    - hinicadm工具分配池化队列时需要设置网卡为down状态。
    - 池化队列组数限制为2、4、8组。
    - mbuf_size 需要和内核态的rx_buff对齐

* 新增SP230/SP560 ipinip流规则支持

### 已解决问题

### 遗留问题

## hinic3-26.1.rc1-0630.r1
### 更新说明
* 新增命令支持查询全量FEC模式。
  ```bash
	show port 0 fec capabilities # 获取网卡支持的FEC模式
  ```
* 新增DPDK固件兼容性判断逻辑，如果固件版本过低则报错退出。
* hinic3驱动对标内核，支持rx_discard_phy丢包统计。
* DPDK 22.11 patch在VF场景下适配流隔离特性。
* 支持PMD私有参数rx_empty_threshold和tx_free_loop，可参考[hinic3_pmd.rst](./hinic3_pmd.rst)。
  使用示例：
  ```bash
	dpdk-testpmd \
    -a 0000:01:00.0,tx_free_loop=0 \
    -a 0000:01:00.1,tx_free_loop=0 \
    --iova-mode=pa -l 0-8 -- \
    --rxq=8 --txq=8 --nb-cores=8 -i -a
  ```
* 新增队列池化模式全量功能，支持虚机和裸机场景。
  - 支持通过hinic3工具配置的分流模式文件，读取文件区分使能VFIO模式/队列池化模式/VF分流模式/流分叉模式。
  - 支持队列池化模式下基础控制面使能（start/stop设备，start/stop队列）。
  - 支持队列池化模式下基础收发包并确认报文可以rss分流。
  - 支持队列池化模式下tx_offload硬件卸载（各类已支持隧道报文内外层cksum，tso）。
  - 支持队列池化模式下fdir，action包括rss/queue，报文类型包括已支持的普通报文和隧道报文。
  - 约束：
    - 开启队列池化功能后，不支持VFIO/流分叉/VF分流等模式。
    - fdir支持为320bit流表，不包括640bit流表。

### 已解决问题
* 修复DPDK 22.11版本patch分流场景不支持VXLAN流规则问题。
* 解耦VF分流和流分叉功能，VF分流不再依赖bifur宏编译。

### 遗留问题
无

## hinic3-26.0.rc1-0331.r1
### 更新说明
* 新流表适配func rss 关闭场景，规则rss能够正确使能。
* 扩展hairpin TX MTU拦截能力。

### 已解决问题
无
### 遗留问题
无

## hinic3-26.0.rc1-0313.r1
### 更新说明
* 开放队列深度二次修改功能。

### 已解决问题
* 解决offloads多端口场景未默认使能问题。
* 解决SP681网卡FEC无法设置问题。
### 遗留问题
无

## hinic3-26.0.rc1-0307.r2
### 更新说明
* 新增FEC特性。

  - 支持前向纠错（FEC）功能，通过在数据流中添加冗余校验信息，使接收端直接纠正传输错误，避免重传延迟。
  
  - 目前支持RS-FEC、Base-R FEC及关闭FEC三种模式，通过网卡硬件实现，提供模式配置与状态查询接口。
    ```bash
	  show port 0 fec_mode # 查看FEC能力及当前模式
	  set port 0 fec_mode rs # 设置RS-FEC模式
	  set port 0 fec_mode baser # 设置Base-R FEC模式
	  set port 0 fec_mode off #关闭FEC
    ```
### 已解决问题
无
### 遗留问题
无

## hinic3-26.0.rc1-0307.r1
### 更新说明
* IPv6+IPv6的VXLAN报文场景，外层IP前88位匹配改成后88位匹配。
* 多层VLAN场景，处理最后一层VLAN修改为处理第一层VLAN。
* 执行port stop 的时候清除fdir流表规则。

### 已解决问题
无
### 遗留问题
无

## hinic3-26.0.rc1-0214.r1
### 更新说明
* 新增hairpin特性。

	- 支持hairpin特性，不上送主机直接在网卡内完成转发，在减少CPU开销的同时提高转发性能。

	- 目前支持本端口转发及扩端口转发功能，hairpin通过队列控制，需指定fdir流表逻辑指定hairpin队列以使能该功能。
    ```bash
	  dpdk-testpmd ...  --hairpin-mode=0x12 --hairpinq=2 # testpmd使能hairpin功能
	  flow create 0 ingress pattern eth / ipv4 / udp / end actions queue index {hairpin_qid} / end # 指定udp报文进行hairpin转发
    ```

* fdir流表扩展。

	对fdir相关硬件字段进行了扩展，以支持了更强的匹配能力，新增支持性包括：
    * 隧道报文内外层同时匹配
    * MAC匹配
    * VLAN匹配
    * TCP flag匹配
    * vni 匹配
    
    新增action包括：
    * drop：丢弃报文
    * rss group：指定队列组散列
      ```bash
	    flow create 0 ingress pattern eth / ipv4 / udp / end actions drop / end # 指定drop action
	    flow create 0 ingress pattern eth / ipv4 / udp / end actions rss types tcp end level 2 queues 2 3 4 end / end # 指定rss group action
  	  ```