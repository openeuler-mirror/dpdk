# OpenEuler开源仓patch使用简介

**以OpenEuler开源仓 DPDK 21.11 patch为例**

# DPDK下载
**下载链接：** https://core.dpdk.org/download/
- 选择DPDK21.11.9(LTS)
- 上传服务器后解压(解压后：dpdk-stable-21.11.9)
```
    tar -xf dpdk-21.11.9.tar.xz
```

# hinic3源码安装 （与dpdk-stable-21.11.9同一层目录）

**下载地址：** https://gitee.com/openeuler/dpdk/tree/hinic3
- 上传服务器解压(解压后：dpdk)

或

**git方式：**  git clone https://gitee.com/openeuler/dpdk.git -b hinic3

**添加hinic3 pmd到DPDK（需要依赖git）：**
```
    sh install.sh ../dpdk-stable-21.11.9
    git log
```

**编译release（需要依赖gcc，DPDK>=20 需要依赖 meson ninja）：**
```
    sh install.sh ../dpdk-stable-21.11.9 build
```

**编译debug：**
```
    sh install.sh ../dpdk-stable-21.11.9 debug
```

- 解释：
    - 安装脚本会自动修改 dpdk 源码中的编译文件。每次执行前都会重新拷贝 hinic3 pmd 到源码目录，并清除构建缓存目录
    - 构建脚本会自动判断 dpdk 版本，使用不同的方式编译：
        - dpdk=19: make
        - dpdk>=20: meson + ninja
    - 构建脚本会自动判断当前dpdk目录是否为git仓，不是则初始化
    - build/debug 模式前总是会自动执行一次安装操作