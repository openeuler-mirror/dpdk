Name: dpdk
Version: 19.11
Release: 9
Packager: packaging@6wind.com
URL: http://dpdk.org
%global source_version  19.11
Source: %{name}-%{version}.tar.xz

Patch0: CVE-2020-10725.patch
Patch1: CVE-2020-10722.patch
Patch2: CVE-2020-10723.patch
Patch3: CVE-2020-10724.patch
Patch4: CVE-2020-10726.patch
Patch5: 0001-fix-SyntaxWarning-when-use-dpdk-pmdinfo.patch
Patch6: CVE-2020-14378.patch
Patch7: CVE-2020-14376-CVE-2020-14377.patch
Patch8: fix-pool-allocation.patch
Patch9: CVE-2020-14374.patch
Patch10: CVE-2020-14375.patch
Patch11: fix-compilation-error-of-max-inline-insns-single-o2-.patch
Patch12: fix-populate-with-small-virtual-chunks.patch
Patch13: dpdk-optimize-the-efficiency-of-compiling-dpdk.patch

Summary: Data Plane Development Kit core
Group: System Environment/Libraries
License: BSD and LGPLv2 and GPLv2

ExclusiveArch: i686 x86_64 aarch64
%ifarch aarch64
%global machine armv8a
%global target arm64-%{machine}-linux-gcc
%global config arm64-%{machine}-linux-gcc
%else
%global machine native
%global target x86_64-%{machine}-linux-gcc
%global config x86_64-%{machine}-linux-gcc
%endif

BuildRequires: kernel-devel, libpcap-devel
BuildRequires: numactl-devel libconfig-devel
BuildRequires: module-init-tools uname-build-checks libnl3 libmnl
BuildRequires: glibc glibc-devel libibverbs libibverbs-devel libmnl-devel

Requires: python3-pyelftools

%define kern_devel_ver   %(uname -r)
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
DPDK devel is a set of makefiles, headers and examples
for fast packet processing on arm64 platforms.

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
%setup -q -n %{name}-%{version}
%patch0 -p1
%patch1 -p1
%patch2 -p1
%patch3 -p1
%patch4 -p1
%patch5 -p1
%patch6 -p1
%patch7 -p1
%patch8 -p1
%patch9 -p1
%patch10 -p1
%patch11 -p1
%patch12 -p1
%patch13 -p1

%build
namer=%{kern_devel_ver}
export RTE_KERNELDIR=/lib/modules/${namer}/build
make O=%{target} T=%{config} config
#make .so and .a libraries for spdk
sed -ri 's,(RTE_BUILD_BOTH_STATIC_AND_SHARED_LIBS=).*,\1y,' %{target}/.config
sed -ri 's,(RTE_MACHINE=).*,\1%{machine},' %{target}/.config
sed -ri 's,(RTE_APP_TEST=).*,\1n,'         %{target}/.config
sed -ri 's,(RTE_NEXT_ABI=).*,\1n,'         %{target}/.config
sed -ri 's,(LIBRTE_VHOST=).*,\1y,'         %{target}/.config
#sed -ri 's,(LIBRTE_PMD_PCAP=).*,\1y,'      %{target}/.config
make O=%{target} -j16

%install
namer=%{kern_devel_ver}
rm -rf %{buildroot}
make install O=%{target} RTE_KERNELDIR=/lib/modules/${namer}/build \
	kerneldir=/lib/modules/${namer}/extra/dpdk DESTDIR=%{buildroot} \
	prefix=%{_prefix} bindir=%{_bindir} sbindir=%{_sbindir} \
	includedir=%{_includedir}/dpdk libdir=%{_libdir} \
	datadir=%{_datadir}/dpdk docdir=%{_docdir}/dpdk

mkdir -p $RPM_BUILD_ROOT/lib64/
cp -ar ./%{target}/lib/librte_eal.so* $RPM_BUILD_ROOT/lib64/
cp -ar ./%{target}/lib/librte_mempool.so* $RPM_BUILD_ROOT/lib64/
cp -ar ./%{target}/lib/librte_ring.so* $RPM_BUILD_ROOT/lib64/
cp -ar ./%{target}/lib/librte_mempool_ring.so* $RPM_BUILD_ROOT/lib64/
cp -ar ./%{target}/lib/librte_pci.so* $RPM_BUILD_ROOT/lib64/
cp -ar ./%{target}/lib/librte_bus_pci.so* $RPM_BUILD_ROOT/lib64/
cp -ar ./%{target}/lib/librte_kvargs.so* $RPM_BUILD_ROOT/lib64/
cp -ar ./%{target}/lib/librte_acl.so* $RPM_BUILD_ROOT/lib64/
cp -ar ./%{target}/lib/librte_ethdev.so* $RPM_BUILD_ROOT/lib64/
cp -ar ./%{target}/lib/librte_mbuf.so* $RPM_BUILD_ROOT/lib64/
cp -ar ./%{target}/lib/librte_cmdline.so* $RPM_BUILD_ROOT/lib64/
cp -ar ./%{target}/lib/librte_net.so* $RPM_BUILD_ROOT/lib64/
cp -ar ./%{target}/lib/librte_meter.so* $RPM_BUILD_ROOT/lib64/

#make O=%{target} doc

mkdir -p $RPM_BUILD_ROOT/usr/include/%{name}-%{version}/
ln -s /usr/share/dpdk/mk $RPM_BUILD_ROOT/usr/include/%{name}-%{version}/
ln -s /usr/share/dpdk/%{target} $RPM_BUILD_ROOT/usr/include/%{name}-%{version}/

mkdir -p $RPM_BUILD_ROOT/usr/include/dpdk/
ln -s /usr/share/dpdk/%{target} $RPM_BUILD_ROOT/usr/include/dpdk/

mkdir -p $RPM_BUILD_ROOT/usr/bin
cp ./%{target}/app/dpdk-pdump  $RPM_BUILD_ROOT/usr/bin

strip -g $RPM_BUILD_ROOT/lib/modules/${namer}/extra/dpdk/igb_uio.ko
strip -g $RPM_BUILD_ROOT/lib/modules/${namer}/extra/dpdk/rte_kni.ko

%define _unpackaged_files_terminate_build 0

%files
%dir %{_datadir}/dpdk
%{_datadir}/dpdk/usertools/*.py
%{_datadir}/dpdk/usertools/*.sh
%{_sbindir}/dpdk-devbind
/lib/modules/%{kern_devel_ver}/extra/dpdk/*
/lib64/librte*.so*

%files devel
%{_includedir}/dpdk
%{_datadir}/dpdk/mk
%{_datadir}/dpdk/buildtools
%{_datadir}/dpdk/%{target}
%{_datadir}/dpdk/examples
%{_bindir}/*
%{_libdir}/*
%dir /usr/include/%{name}-%{version}/
/usr/include/%{name}-%{version}/*
%dir /usr/include/dpdk/
/usr/include/dpdk/*
%exclude /usr/bin/dpdk-pdump

%files doc
#%doc %{_docdir}/dpdk

%files tools
/usr/bin/dpdk-pdump

%post
/sbin/ldconfig
/usr/sbin/depmod

%postun
/sbin/ldconfig
/usr/sbin/depmod

%changelog
* Wed May 19 2021 renmingshuai<renmingshuai@huawei.com> - 19.11-9
- optimize the efficiency of compiling dpdk

* Thu Jan 28 2021 huangliming<huangliming5@huawei.com> - 19.11-8
- fix populate with small virtual chunks

* Thu Jan 28 2021 huangliming<huangliming5@huawei.com> - 19.11-7
-fix yum update dpdk-tools conflict with dpdk-devel

* Thu Jan 28 2021 huangliming<huangliming5@huawei.com> - 19.11-6
- fix compilation error of max-inline-insns-single-o2 limit reached

* Tue Dec 1 2020 chxssg<chxssg@qq.com> - 19.11-5
-fix CVE-2020-14374 CVE-2020-14375

* Wed Nov 25 2020 chxssg<chxssg@qq.com> - 19.11-4
-fix CVE-2020-14376 CVE-2020-14377 CVE-2020-14378

* Fri Sep 25 2020 hubble_zhu<hubble_zhu@qq.com> - 19.11-3
-fix SyntaxWarning when use dpdk-pmdinfo

* Wed Sep 23 2020 hubble_zhu<hubble_zhu@qq.com> - 19.11-2
-Add requires for dpdk-pmdinfo

* Wed May 27 2020 chenxiang<rose.chen@huawei.com> - 19.11-1
-fix CVE-2020-10722 CVE-2020-10723 CVE-2020-10724 CVE-2020-10725

* Wed May 27 2020 openEuler dpdk version-release
-first package
