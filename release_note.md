# Release Notes

## hinic3-26.0.rc1-0313.r1
### 更新说明
* **开放队列深度二次修改功能**

### 已解决问题
* **解决offloads多端口场景未默认使能问题**
* **解决SP681网卡FEC无法设置问题**
### 遗留问题
无

## hinic3-26.0.rc1-0307.r2
### 更新说明
* **新增FEC特性**

  支持前向纠错（FEC）功能，通过在数据流中添加冗余校验信息，使接收端直接纠正传输错误，避免重传延迟。
  
  目前支持RS-FEC、Base-R FEC及关闭FEC三种模式，通过网卡硬件实现，提供模式配置与状态查询接口。
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
* **IPv6+IPv6的VXLAN报文场景，外层IP前88位匹配改成后88位匹配**
* **多层VLAN场景，处理最后一层VLAN修改为处理第一层VLAN**
* **执行port stop 的时候清除fdir流表规则**

### 已解决问题
无
### 遗留问题
无

## hinic3-26.0.rc1-0214.r1
### 更新说明
* **新增hairpin特性**

	支持hairpin特性，不上送主机直接在网卡内完成转发，在减少CPU开销的同时提高转发性能。

	目前支持本端口转发及扩端口转发功能，hairpin通过队列控制，需指定fdir流表逻辑指定hairpin队列以使能该功能。

     ```bash
	dpdk-testpmd ...  --hairpin-mode=0x12 --hairpinq=2 # testpmd使能hairpin功能
	flow create 0 ingress pattern eth / ipv4 / udp / end actions queue index {hairpin_qid} / end # 指定udp报文进行hairpin转发
  	```

* **fdir流表扩展**

	对fdir相关硬件字段进行了扩展，以支持了更强的匹配能力，新增支持性包括
    * **隧道报文内外层同时匹配**
    * **MAC匹配**
    * **VLAN匹配**
    * **TCP flag匹配**
    * **vni 匹配**
    
    新增action包括
    * **drop：丢弃报文**
    * **rss group：指定队列组散列**
    ```bash
	flow create 0 ingress pattern eth / ipv4 / udp / end actions drop / end # 指定drop action
	flow create 0 ingress pattern eth / ipv4 / udp / end actions rss types tcp end level 2 queues 2 3 4 end / end # 指定rss group action
  	```