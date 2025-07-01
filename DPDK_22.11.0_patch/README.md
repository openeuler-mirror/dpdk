# 安装指南

#### 1.下载dpdk22.11版本。[github地址](https://github.com/DPDK/dpdk/tree/v22.11)
#### 2.下载patch并安装。
###### 1）切换dpdk目录，将patch文件夹放入dpdk目录
```shell
cd dpdk
```

###### 2）将dpdk版本回退到基线commit_id
```shell
git reset f262f16087ea6a77357a915cf4c0d10ddc7b6562 --hard
```

###### 3）查看当前路径下文件

```shell
ls
```

```shell
#ls命令回显
ABI_VERSION app/ buildtools/ config/ devtools/ doc/ examples/ kernel/ lib/ license/ MAINTAINERS Makefile meson.build meson_option.txt README usertools VERSION DPDK_22.11.0_patch/
```

###### 4）安装patch
```shell
git am DPDK_22.11.0_patch/*.patch
```

#### 3.编译安装
```shell
meson --prefix=/usr/ -Ddisable_drivers=net/cnxk -Dplatform=generic -Ddefault_library='shared' build
ninja -C build
ninja install -C build
```
