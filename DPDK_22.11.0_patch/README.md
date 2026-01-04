# 安装指南

#### 1.下载dpdk22.11版本。[github地址](https://github.com/DPDK/dpdk/tree/v22.11)
#### 2.下载DPDK_22.11.0_patch并安装。[patch地址](https://gitee.com/openeuler/dpdk/tree/dpu/)
###### 1）切换dpdk目录
```shell
cd dpdk
```
###### 2）下载DPDK_22.11.0_patch
```shell
git clone git@atomgit.com:openeuler/dpdk.git -b dpu
```

###### 3）将dpdk版本回退到基线commit_id
```shell
git reset f262f16087ea6a77357a915cf4c0d10ddc7b6562 --hard
```

###### 4）查看当前路径下文件，路径下多了一个dpdk文件夹，说明patch下载成功
```shell
ls
```

```shell
#ls命令回显
ABI_VERSION  Makefile  VERSION  buildtools/  devtools/  dpdk/     dts/       kernel/  license/     meson_options.txt
MAINTAINERS  README    app/     config/      doc/       drivers/  examples/  lib/     meson.build  usertools/
```

###### 4）安装patch。在drivers/net/下出现hinic3目录，说明patch安装成功
```shell
git am dpdk/DPDK_22.11.0_patch/*.patch
```

#### 3.编译安装
```shell
meson --prefix=/usr/ -Ddisable_drivers=net/cnxk -Dplatform=generic -Ddefault_library='shared' build
ninja -C build
ninja install -C build
```
