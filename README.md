# OpenEuler开源仓patch使用简介

**以OpenEuler开源仓 DPDK 21.11 patch为例**

# DPDK下载
**下载链接：** https://core.dpdk.org/download/
- 选择DPDK21.11.9(LTS)
- 上传服务器后解压(解压后：dpdk-stable-21.11.9)
```
    tar -xvf dpdk-21.11.9.tar.xz
```

# hinic3源码安装 （与dpdk-stable-21.11.9同一层目录）

**下载地址：** https://gitee.com/openeuler/dpdk/tree/hinic3
- 上传服务器解压(解压后：dpdk)

或

**git方式：**  git clone https://gitee.com/openeuler/dpdk.git -b hinic3

**本地初始化：**
```
    cd dpdk-stable-21.11.9
    git log
    git init
    git add .
    git commit -m "init"
    git log
```

**打patch到dpdk源码：**
```
    git am ../dpdk/DPDK_21.11_patch/00*
```
- 解释：
  - 0001-*-*.patch 作用是将pmd源码添加到dpdk-stable-21.11.9/drivers/net/hinic3 目录中。
  - 0002_*_*.patch 作用是添加'hinic3'到dpdk-stable-21.11.9/drivers/net/meson.build编译文件中。
  - 其他patch 是pmd驱动的其他特性。