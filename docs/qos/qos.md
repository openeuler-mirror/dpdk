← 返回 [README · 其它示例](../../README.md#5-其它示例)

# QoS

- ⭐ 建议使用  
- ✔️ 支持  
- ✖️ 不支持  

| 功能            | dpdk | hinicadm3 qos –i hinic0 –p 0 | hinicadm3 qos –i enp1s0f0 |
|-----------------|:----:|:----------------------------:|:--------------------------:|
| PF 使能 DCB     | ⭐    |                              |                            |
| PF -l -c -w     | ✔️    | ⭐                           |                            |
| PF dscp pcp     | ✔️    | ⭐                           |                            |
| PF 查询         |      | ⭐                           |                            |
| VF 使能 DCB     | ⭐    |                              | ⭐                          |
| VF -l -c -w     | ✖️    | ⭐                           | ✔️                          |
| VF dscp/pcp     | ✖️    | ⭐                           | ✔️                          |
| VF 查询         |      | ⭐                           | ✔️                          |

# PF场景

## ETS验证
启动命令
```
./build/app/dpdk-testpmd -v -a 0000:03:00.0 -l 0-8 -- --rxd=1024 --txd=1024 --nb-cores=8 --rxq=8 --txq=8 -i
```

### -w 限宽
```
stop
port stop all
port config 0 dcb vt off 8 pfc off
port start all
```

此时dcb已使能。（dcb使能后ets默认使能）

用hinicadm3工具配置：
```
hinicadm3 qos -i hinic0 -p 0 -t ets -c 0,1,2,3,4,5,6,7
hinicadm3 qos -i hinic0 -p 0 -t ets -w 20,20,20,10,10,10,5,5
hinicadm3 qos -i hinic0 -p 0
```

回到testpmd：
```
set fwd txonly
set txpkts 1024
start
show port stats all
show port stats all
clear fwd stats all
show fwd stats all
show fwd stats all
```
收包统计比例为20:20:20:10:10:10:5:5

### -l 限速
每个TC限1G带宽

使用hinicadm3工具配置
```
hinicadm3 qos -i hinic0 -p 0 -t ets -l 1,1,1,1,1,1,1,1
```

testpmd：
```
clear fwd stats all
show fwd stats all
show fwd stats all
clear port stats all
show port stats all
show port stats all
```
- 八个队列收包比例为1:1:1:1:1:1:1:1
- TX总带宽8G
每个队列TX带宽为1G

## 查询间接表
```
show port 0 rss reta 256 (-1,-1,-1,-1,-1,-1,-1,-1)
```

## pcp & dscp
```
stop
set fwd io
clear port stats all
clear fwd stats all
start
```

### dscp
```
hinicadm3 qos -i hinic0 -p 0 --port_trust dscp
```

scapy发包，配置ToS
```
precedence = 2
tos = precedence << 5   # 0x40
pkt = Ether(dst='A6:A1:AC:9C:98:94')/ \
      IP(dst="192.168.1.1", tos=tos)/ \
      UDP(dport=1234)/ \
      Raw(b"test")
pkt.show()
sendp(pkt, iface="enp3s0f1")
```

```
stop
port stop all
port config 0 dcb vt off 8 pfc off
port start all
set fwd io
start
clear fwd stats all
show fwd stats all
show fwd stats all
clear port stats all
show port stats all
show port stats all
```

只有队列2有包。

修改precedence，观察到与cos映射的队列有包

### pcp
```
hinicadm3 qos -i hinic0 -p 0 --port_trust pcp
```

scapy发包，配置VLAN
```
pkt = Ether(dst="66:66:66:86:E2:CB")/ \
      Dot1Q(vlan=0, prio=1)/ \
      IP(dst="192.168.1.1")/ \
      UDP(dport=1234)/ \
      Raw(b"test")

pkt.show()
sendp(pkt, iface="enp3s0f1")
```

```
stop
port stop all
port config 0 dcb vt off 4 pfc off
port start all
start
clear fwd stats all
show fwd stats all
show fwd stats all
```

只有队列1有数据

修改prio，观察到与cos映射的队列有包

# VF场景
## ETS验证
```
echo 4 > /sys/class/net/enp3s0f0/device/sriov_numvfs
dpdk-devbind.py -b vfio-pci 0000:03:00.3
./build/app/dpdk-testpmd -v -a 0000:03:00.3 -l 0-4 -- --rxd=1024 --txd=1024 --nb-cores=4 --rxq=4 --txq=4 -i
```
**VF dcb还需要在工具侧使能**
```
hinicadm3 qos -i enp3s0f0 -t dcb -e 1
```
### -w 限宽
```
stop
port stop all
port config 0 dcb vt off 4 pfc off
port start all
set fwd txonly
set txpkts 1024
start
```

新开一个窗口使用hinicadm3工具配置ets。

-i后跟hinicx设备编号，-p跟对应的网口编号。
```
hinicadm3 qos -i hinic0 -p 0 -t ets -c 0,1,2,3,4,5,6,7 -w 10,20,30,40,0,0,0,0
hinicadm3 qos -i hinic0 -p 0
```
也可以
```
hinicadm3 qos -i enp1s0f0 -t ets -c 0,1,2,3,4,5,6,7
hinicadm3 qos -i enp1s0f0 -t ets -w 20,10,30,40,0,0,0,0
hinicadm3 qos -i enp1s0f0 -t ets -l 2,1,1,1,0,0,0,0
hinicadm3 qos -i enp1s0f0
```

回到testpmd中：
```
clear port stats all
show port stats all
show port stats all
clear fwd stats all
show fwd stats all
show fwd stats all
```

### -l 限速
```
stop
port stop all
port config 0 dcb vt off 4 pfc off
port start all
set fwd txonly
set txpkts 1024
start
```
新开一个窗口使用hinicadm3工具配置ets。每个队列限制到1G
```
hinicadm3 qos -i hinic0 -p 0 -t ets -c 0,1,2,3,4,5,6,7 -l 1,1,1,1,0,0,0,0
hinicadm3 qos -i hinic0 -p 0
```
回到testpmd中
```
clear port stats all
show port stats all
show port stats all
clear fwd stats all
show fwd stats all
show fwd stats all
```
4个队列收包统计是1:1:1:1的关系，总带宽4G，因此每个队列带宽1G

## 查询间接表
同[PF场景-查询间接表](#查询间接表)

## pcp & dscp
同[PF场景-pcp & dscp](#pcp--dscp)
