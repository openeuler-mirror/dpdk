Name: dpdk
Version: 21.11
Release: 1
Packager: packaging@6wind.com
URL: http://dpdk.org
%global source_version  21.11
Source: https://git.dpdk.org/dpdk/snapshot/%{name}-%{version}.tar.xz

Patch0:    add-igb-uio.patch

Summary: Data Plane Development Kit core
Group: System Environment/Libraries
License: BSD and LGPLv2 and GPLv2

ExclusiveArch: i686 x86_64 aarch64
%ifarch aarch64
%global machine armv8a
%global target arm64-%{machine}-linux-gcc
%else
%global machine native
%global target x86_64-%{machine}-linux-gcc
%endif

BuildRequires: meson ninja-build gcc diffutils python3-pyelftools python-pyelftools
BuildRequires: kernel-devel numactl-devel
BuildRequires: libpcap libpcap-devel
BuildRequires: uname-build-checks
BuildRequires: chrpath
BuildRequires: groff-base

%define kern_devel_ver %(uname -r)

%description
DPDK core includes kernel modules, core libraries and tools.
testpmd application allows to test fast packet processing environments
on arm64 platforms. For instance, it can be used to check that environment
can support fast path applications such as 6WINDGate, pktgen, rumptcpip, etc.
More libraries are available as extensions in other packages.

%package devel
Summary: Data Plane Development Kit for development
Requires: %{name}%{?_isa} = %{version}-%{release}
%description devel
DPDK devel is a set of headers for fast packet processing on arm64 platforms.

%package doc
Summary: Data Plane Development Kit API documentation
BuildArch: noarch
%description doc
DPDK doc is divided in two parts: API details in doxygen HTML format
and guides in sphinx HTML/PDF formats.

%package tools
Summary:      dpdk pdump tool
Group  :      Applications/System
Requires:       dpdk = %{version}
%description tools
This package contains the pdump tool for capture the dpdk network packets.

%prep
%autosetup -n %{name}-%{version} -p1

%build
export CFLAGS="%{optflags}"
meson %{target} -Ddisable_drivers=*/octeontx2 -Ddisable_drivers=*/fpga* -Ddisable_drivers=*/ifpga* -Denable_kmods=true
ninja -C %{target}

%install
namer=%{kern_devel_ver}
DESTDIR=$RPM_BUILD_ROOT/ meson install -C %{target}
DESTDIR=$RPM_BUILD_ROOT/ ninja install -C %{target}

cd  $RPM_BUILD_ROOT
file `find -type f`| grep -w ELF | awk -F":" '{print $1}' | grep -v ko | for i in `xargs`
do
  chrpath -d $i
done
cd -

mkdir -p  $RPM_BUILD_ROOT/etc/ld.so.conf.d
echo "%{_bindir}/%{name}" > $RPM_BUILD_ROOT/etc/ld.so.conf.d/%{name}-%{_arch}.conf
echo "/usr/local/bin/%{name}" >> $RPM_BUILD_ROOT/etc/ld.so.conf.d/%{name}-%{_arch}.conf
echo "/lib64/%{name}" >> $RPM_BUILD_ROOT/etc/ld.so.conf.d/%{name}-%{_arch}.conf
echo "/usr/local/lib64" >> $RPM_BUILD_ROOT/etc/ld.so.conf.d/%{name}-%{_arch}.conf
echo "/usr/local/include" >> $RPM_BUILD_ROOT/etc/ld.so.conf.d/%{name}-%{_arch}.conf
mkdir -p $RPM_BUILD_ROOT/usr/share/dpdk/%{target}
ln -fs  %{buildroot}/usr/local/include %{buildroot}/usr/share/dpdk/%{target}/include
ln -fs  %{buildroot}/usr/local/lib64 %{buildroot}/usr/share/dpdk/%{target}/lib

mkdir -p $RPM_BUILD_ROOT/usr/sbin
ln -fs  /usr/local/bin/dpdk-devbind.py %{buildroot}/usr/sbin/dpdk-devbind

mkdir -p $RPM_BUILD_ROOT/lib64/
cp -ar ./%{target}/lib/librte_eal.so* $RPM_BUILD_ROOT/lib64/
cp -ar ./%{target}/lib/librte_mempool.so* $RPM_BUILD_ROOT/lib64/
cp -ar ./%{target}/lib/librte_ring.so* $RPM_BUILD_ROOT/lib64/
cp -ar ./%{target}/drivers/librte_mempool_ring.so* $RPM_BUILD_ROOT/lib64/
cp -ar ./%{target}/lib/librte_pci.so* $RPM_BUILD_ROOT/lib64/
cp -ar ./%{target}/drivers/librte_bus_pci.so* $RPM_BUILD_ROOT/lib64/
cp -ar ./%{target}/lib/librte_kvargs.so* $RPM_BUILD_ROOT/lib64/
cp -ar ./%{target}/lib/librte_acl.so* $RPM_BUILD_ROOT/lib64/
cp -ar ./%{target}/lib/librte_ethdev.so* $RPM_BUILD_ROOT/lib64/
cp -ar ./%{target}/lib/librte_mbuf.so* $RPM_BUILD_ROOT/lib64/
cp -ar ./%{target}/lib/librte_cmdline.so* $RPM_BUILD_ROOT/lib64/
cp -ar ./%{target}/lib/librte_net.so* $RPM_BUILD_ROOT/lib64/
cp -ar ./%{target}/lib/librte_meter.so* $RPM_BUILD_ROOT/lib64/
cp -ar ./%{target}/lib/librte_telemetry.so* $RPM_BUILD_ROOT/lib64/

mkdir -p $RPM_BUILD_ROOT/usr/bin
cp ./%{target}/app/dpdk-pdump  $RPM_BUILD_ROOT/usr/bin

strip -g $RPM_BUILD_ROOT/lib/modules/${namer}/extra/dpdk/rte_kni.ko
strip -g $RPM_BUILD_ROOT/lib/modules/${namer}/extra/dpdk/igb_uio.ko

%define _unpackaged_files_terminate_build 0
%define _build_id_links none

%files
/usr/local/bin/*.py
/lib/modules/%{kern_devel_ver}/extra/dpdk/*
/lib64/librte*.so*
%{_sbindir}/dpdk-devbind
%config(noreplace) /etc/ld.so.conf.d/*
/usr/local/lib64/*
%exclude /usr/local/lib64/*.a
/usr/local/bin/*
%exclude /usr/local/bin/dpdk-test*
%exclude /usr/local/bin/dpdk-pdump

%files devel
%dir /usr/local/include/
/usr/local/include/*.h
/usr/local/include/generic/*.h
/usr/local/share/dpdk/examples/*
/usr/share/dpdk/%{target}/include/*
/usr/local/lib64/*.a

%files doc

%files tools
/usr/bin/dpdk-pdump

%post
/sbin/ldconfig
/usr/sbin/depmod

%postun
/sbin/ldconfig
/usr/sbin/depmod

%changelog
* Fri Dec 17 2021 jiangheng <jiangheng12@huawei.com> - 21.11-1
- update to 21.11

* Sat Dec 17 2021 Min Hu <humin29@huawei.com> - 20.11-10
- sync patches ranges from versoin 9 t0 17 from master branch

* Mon Sep 13 2021 chenchen <chen_aka_jan@163.com> - 20.11-9
- del rpath from some binaries and bin
- add debug package to strip
- add "-fstack-protector-strong" for binaries and bin

* Thu Jul 29 2021 Min Hu <humin29@huawei.com> - 20.11-8
- add lib and testpmd functions to sync upstream

* Tue Jul 27 2021 Min Hu <humin29@huawei.com> - 20.11-7
- add bugfixes for hns3 PMD and sync upstream

* Mon Jul 19 2021 Min Hu <humin29@huawei.com> - 20.11-6
- keep in accordance with dpdk 19.11 version package arrangement

* Tue Jul 13 2021 huangliming <huangliming5@huawei.com> - 20.11-5
- remove redundant README files

* Mon Jul 12 2021 chenjian <chenjian@kylinos.cn> - 20.11-4
- move /usr/local/share/dpdk/* to devel
- add doc files

* Mon Jul 12 2021 huangliming <huangliming5@huawei.com> - 20.11-3
- change the patch installation to autosetup

* Fri Jul 02 2021 huangliming <huangliming5@huawei.com> - 20.11-2
- add uname-build-checks BuildRequires

* Mon Jun 21 2021 Min Hu <humin29@huawei.com> - 20.11-1
- support hns3 PMD for Kunpeng 920 and Kunpeng 930

* Wed Jun 16 2021 openEuler dpdk version-release
-first package
