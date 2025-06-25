# 安装教程

1. 使用dpdk基线版本(commitid:f262f16087ea6a77357a915cf4c0d10ddc7b6562)，在此基础上合入patch 
git am DPDK_22.11.0_patch/0001-Upstream-hinic3-DPDK-driver.patch
2.  进入dpdk目录编译
meson --prefix=/usr -Ddisable_drivers=net/cnxk -Dplatform=generic -Dc_args=" -g -fstack-protector-strong -D_FORTIFY_SOURCE=2 -O2 -ftrapv -DHAVE_OVS_DPDK -fPIC" -Dc_link_args='-Wl,-z,relro,-z,now,-z,noexecstack'  build
3.  build
ninja -C build
4. 生成libdpdk.so及依赖聚合dpdk的librte_net_hinic3.so
gcc -shared -fPIC -Wl,-z,relro,-z,now,-z,noexecstack,-whole-archive `find ./ -name "librte*.a"` -Wl,-no-whole-archive -o ./libdpdk.so -pthread -lm -ldl -lnuma
gcc -shared -fPIC -Wl,-z,relro,-z,now,-z,noexecstack,-whole-archive -fstack-protector-strong -g -D_FORTIFY_SOURCE=2 -O2 -ftrapv -fvisibility=hidden ./build/drivers/librte_net_hinic3.a -Wl,-no-whole-archive -o ./librte_net_hinic3.so -L ./ -ldpdk

# 使用说明

1. 使用时，文件路径如下
1.1 /etc/dpak/net/agent_config.ini
1.2 /usr/lib64/librte_net_hinic3.so
2.  需要在代码中dlopen打开/usr/lib64/librte_net_hinic3.so
