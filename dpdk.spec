Name: dpdk
Version: 21.11
Release: 3
Packager: packaging@6wind.com
URL: http://dpdk.org
%global source_version  21.11
Source: https://git.dpdk.org/dpdk/snapshot/%{name}-%{version}.tar.xz

Patch9001:    0001-add-igb-uio.patch
Patch9002:    0002-dpdk-add-secure-compile-option-and-fPIC-option.patch
Patch9003:    0003-dpdk-bugfix-the-deadlock-in-rte_eal_init.patch
Patch9004:    0004-dpdk-master-core-donot-set-affinity-in-libstorage.patch
Patch9005:    0005-dpdk-change-the-log-level-in-prepare_numa.patch
Patch9006:    0006-dpdk-fix-dpdk-coredump-problem.patch
Patch9007:    0007-dpdk-fix-cpu-flag-error-in-Intel-R-Xeon-R-CPU-E5-262.patch
Patch9008:    0008-dpdk-add-support-for-gazelle.patch
Patch9009:    0009-dpdk-fix-error-in-clearing-secondary-process-memseg-lists.patch
Patch9010:    0010-dpdk-fix-coredump-when-primary-process-attach-without-shared-file.patch
Patch9011:    0011-dpdk-fix-fbarray-memseg-destory-error-during-detach.patch
Patch9012:    0012-fix-rte-eal-sec-detach-coredump-count-rollover.patch
Patch9013:    0013-fix-rte-eal-memory-init-double-unlock.patch
Patch9014:    0014-fix-last-argv-pointer-change-to-first.patch
Patch9015:    0015-fix-internal-cfg-and-fbarray-attach-mememory-leak.patch
Patch9016:    0016-fix-error-that-the-secondary-attach-fails-due-to-detach.patch

Summary: Data Plane Development Kit core
Group: System Environment/Libraries
License: BSD and LGPLv2 and GPLv2

ExclusiveArch: i686 x86_64 aarch64

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
meson build
ninja -C build

%install
DESTDIR=$RPM_BUILD_ROOT/ ninja install -C build

mkdir -p $RPM_BUILD_ROOT/usr/lib64
mv $RPM_BUILD_ROOT/usr/local/lib64/* $RPM_BUILD_ROOT/usr/lib64/

mkdir -p $RPM_BUILD_ROOT/usr/local/bin
ln -fs /usr/local/bin/dpdk-devbind.py $RPM_BUILD_ROOT/usr/local/bin/dpdk-devbind
cd $RPM_BUILD_ROOT/usr/lib64/dpdk/pmds-22.0
ln -fs ../../../local/include/* .
cd -

strip -g $RPM_BUILD_ROOT/lib/modules/%{kern_devel_ver}/extra/dpdk/rte_kni.ko
strip -g $RPM_BUILD_ROOT/lib/modules/%{kern_devel_ver}/extra/dpdk/igb_uio.ko

%define _unpackaged_files_terminate_build 0
%define _build_id_links none

%files
/usr/local/bin/*.py
/usr/local/bin/dpdk-devbind
/lib/modules/%{kern_devel_ver}/extra/dpdk/*.ko
/usr/lib64/*.so*
/usr/lib64/dpdk/*
%exclude /usr/lib64/dpdk/pmds-22.0/*.h

%files devel
/usr/local/include
/usr/lib64/*.a
/usr/lib64/dpdk/pmds-22.0/*.h

%files doc

%files tools
/usr/local/bin/dpdk-pdump
/usr/local/bin/dpdk-dumpcap
/usr/local/bin/dpdk-proc-info
/usr/local/bin/dpdk-test
/usr/local/bin/dpdk-testpmd

%post
/sbin/ldconfig
/usr/sbin/depmod

%postun
/sbin/ldconfig
/usr/sbin/depmod

%changelog
* Mon Jan 10 2022 jiangheng <jiangheng12@huawei.com> - 21.11-3
- add /usr/lib64/dpdk/*, here are some so files
- put lib file and header file in the same directory for third-party lib compile

* Sat Dec 25 2021 wuchangsheng <wuchangsheng2@huawei.com> - 21.11-2
- remove redundant file in rpm
- add gazelle support

* Fri Dec 17 2021 jiangheng <jiangheng12@huawei.com> - 21.11-1
- update to 21.11

* Sat Dec 11 2021 Min Hu <humin29@huawei.com> - 20.11-17
- Fix execution failure to add DLB to usertools/dpdk-devbind.py

* Fri Dec 10 2021 Min Hu <humin29@huawei.com> - 20.11-16
- del doc package

* Fri Nov 12 2021 Min Hu <humin29@huawei.com> - 20.11-15
- synchronize dmadev and refactor for hns3 PMD

* Wed Nov 10 2021 Min Hu <humin29@huawei.com> - 20.11-14
- release flows left before port stop

* Mon Nov 08 2021 Min Hu <humin29@huawei.com> - 20.11-13
- fix PMD cannot get the RSS key.

* Mon Nov 01 2021 Min Hu <humin29@huawei.com> - 20.11-12
- synchronize dmadev and hns3 bugfixes from upstream

* Mon Sep 13 2021 chenchen <chen_aka_jan@163.com> - 20.11-11
- del rpath from some binaries and bin
- add debug package to strip
- add "-fstack-protector-strong" for binaries and bin

* Mon Sep 13 2021 Min Hu <humin29@huawei.com> - 20.11-10
- add bugfixes for hns3 PMD

* Thu Aug 30 2021 Min Hu <humin29@huawei.com> - 20.11-9
- support link up/down for PF in hns3 PMD

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
