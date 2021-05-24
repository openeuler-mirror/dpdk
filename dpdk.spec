Name: dpdk
Version: 19.11
Release: 10
Packager: packaging@6wind.com
URL: http://dpdk.org
%global source_version  19.11
Source: https://git.dpdk.org/dpdk/snapshot/%{name}-%{version}.tar.xz

Patch0: CVE-2020-10725.patch
Patch1: CVE-2020-10722.patch
Patch2: CVE-2020-10723.patch
Patch3: CVE-2020-10724.patch
Patch4: CVE-2020-10726.patch
Patch5: CVE-2020-14378.patch
Patch6: CVE-2020-14376-CVE-2020-14377.patch
Patch7: fix-pool-allocation.patch
Patch8: CVE-2020-14374.patch
Patch9: CVE-2020-14375.patch
Patch10: fix-populate-with-small-virtual-chunks.patch
Patch11: 0001-dpdk-add-secure-compile-option-and-fPIC-option.patch
Patch12: 0002-dpdk-add-secure-option-in-makefile.patch
Patch13: 0003-dpdk-bugfix-the-deadlock-in-rte_eal_init.patch
Patch14: 0004-dpdk-master-core-donot-set-affinity-in-libstorage.patch
Patch15: 0005-dpdk-change-the-log-level-in-prepare_numa.patch
Patch16: 0006-dpdk-fix-dpdk-coredump-problem.patch
Patch17: 0007-dpdk-add-secure-compile-option-in-pmdinfogen-Makefil.patch
Patch18: 0008-dpdk-fix-cpu-flag-error-in-Intel-R-Xeon-R-CPU-E5-262.patch
Patch19: 0009-dpdk-support-gazelle-01-include.patch
Patch20: 0009-dpdk-support-gazelle-02-include-base.patch
Patch21: 0009-dpdk-support-gazelle-03-memory.patch
Patch22: 0009-dpdk-support-gazelle-04-cfg-options.patch
Patch23: 0009-dpdk-support-gazelle-05-fbarray-hugepageinfo.patch
Patch24: 0009-dpdk-support-gazelle-06-memalloc.patch
Patch25: 0009-dpdk-support-gazelle-07-eal-add-sec-attach.patch
Patch26: 0009-dpdk-support-gazelle-08-eal-add-config.patch
Patch27: 0009-dpdk-support-gazelle-09-eal-add-libnetapi.patch
Patch28: 0009-dpdk-support-gazelle-10-eal-memory-inter-config.patch
Patch29: 0009-dpdk-support-gazelle-11-eal-memory-add-sec.patch
Patch30: 0010-dpdk-fix-error-in-clearing-secondary-process-memseg-lists.patch
Patch31: 0011-dpdk-fix-coredump-when-primary-process-attach-without-shared-file.patch
Patch32: 0012-dpdk-fix-fbarray-memseg-destory-error-during-detach.patch
Patch33: 0013-dpdk-optimize-the-efficiency-of-compiling-dpdk.patch

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
%patch11 -p1
%patch12 -p1
%patch13 -p1
%patch14 -p1
%patch15 -p1
%patch16 -p1
%patch17 -p1
%patch18 -p1
%patch0 -p1
%patch1 -p1
%patch2 -p1
%patch3 -p1
%patch4 -p1
%patch19 -p1
%patch20 -p1
%patch21 -p1
%patch22 -p1
%patch23 -p1
%patch24 -p1
%patch25 -p1
%patch26 -p1
%patch27 -p1
%patch28 -p1
%patch29 -p1
%patch30 -p1
%patch31 -p1
%patch32 -p1
%patch5 -p1
%patch6 -p1
%patch7 -p1
%patch8 -p1
%patch9 -p1
%patch10 -p1
%patch33 -p1

%build
namer=%{kern_devel_ver}
export RTE_KERNELDIR=/lib/modules/${namer}/build
export EXTRA_CFLAGS="-fstack-protector-strong"
make O=%{target} T=%{config} config
#make .a and .so libraries for spdk
sed -ri 's,(RTE_BUILD_BOTH_STATIC_AND_SHARED_LIBS=).*,\1y,' %{target}/.config
sed -ri 's,(CONFIG_RTE_LIB_LIBOS=).*,\1n,' %{target}/.config
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
* Mon May 24 2021 renmingshuai <renmingshuai@huawei.com> - 19.11-10
- optimize the efficiency of compiling dpdk

* Mon May 24 2021 wutao <wutao61@huawei.com> - 19.11-9
- add fstack-protector-strong gcc flags

* Mon Apr 5 2021 wu-changsheng<851744572@qq.com> - 19.11-8
- add support for gazelle

* Thu Jan 28 2021 huangliming<huangliming5@huawei.com> - 19.11-7
- fix populate with small virtual chunks

* Thu Jan 28 2021 huangliming<huangliming5@huawei.com> - 19.11-6
-fix yum update dpdk-tools conflict with dpdk-devel

* Tue Oct 20 2020 zhaowei<zhaowei23@huawei.com> - 19.11-5
-fix CVE-2020-14374 CVE-2020-14375

* Tue Oct 20 2020 chenxiang<rose.chen@huawei.com> - 19.11-4
-fix CVE-2020-14376 CVE-2020-14377 CVE-2020-14378

* Wed Sep 23 2020 hubble_zhu<hubble_zhu@qq.com> - 19.11-3
-Add requires for dpdk-pmdinfo

* Thu Sep 3 2020 zhaowei<zhaowei23@huawei.com> - 19.11-2
-update source URL

* Wed May 27 2020 chenxiang<rose.chen@huawei.com> - 19.11-1
-fix CVE-2020-10722 CVE-2020-10723 CVE-2020-10724 CVE-2020-10725

* Wed May 27 2020 openEuler dpdk version-release
-first package
