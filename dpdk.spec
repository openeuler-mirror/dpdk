Name: dpdk
Version: 21.11
Release: 9
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
Patch9017:    0017-fix-master-thread-not-set-affinity.patch
Patch9018:    0018-net-bonding-fix-offloading-configuration.patch
Patch9019:    0019-net-hns3-fix-Rx-Tx-when-fast-path-operation-introduc.patch
Patch9020:    0020-net-hns3-fix-mailbox-wait-time-uninitialization.patch
Patch9021:    0021-net-hns3-fix-vector-burst-when-PTP-enable.patch
Patch9022:    0022-net-hns3-remove-unnecessary-assignment.patch
Patch9023:    0023-net-hns3-fix-using-enum-as-boolean.patch
Patch9024:    0024-net-hns3-extract-common-function-to-initialize-MAC-a.patch
Patch9025:    0025-net-hns3-make-control-plane-function-non-inline.patch
Patch9026:    0026-net-hns3-remove-unnecessary-blank-lines.patch
Patch9027:    0027-net-hns3-extract-reset-failure-handling-to-function.patch
Patch9028:    0028-net-hns3-remove-unused-variables.patch
Patch9029:    0029-net-hns3-remove-getting-number-of-queue-descriptors-.patch
Patch9030:    0030-net-hns3-remove-logging-memory-addresses.patch
Patch9031:    0031-net-hns3-extract-common-function-to-obtain-revision-.patch
Patch9032:    0032-net-hns3-replace-single-line-functions.patch
Patch9033:    0033-net-hns3-remove-non-re-entrant-strerror-call.patch
Patch9034:    0034-net-hns3-rename-function.patch
Patch9035:    0035-net-hns3-extract-functions-to-create-RSS-and-FDIR-fl.patch
Patch9036:    0036-net-hns3-support-indirect-counter-flow-action.patch
Patch9037:    0037-net-hns3-fix-max-packet-size-rollback-in-PF.patch
Patch9038:    0038-net-hns3-fix-RSS-key-with-null.patch
Patch9039:    0039-net-hns3-fix-insecure-way-to-query-MAC-statistics.patch
Patch9040:    0040-net-hns3-fix-double-decrement-of-secondary-count.patch
Patch9041:    0041-net-hns3-fix-operating-queue-when-TCAM-table-is-inva.patch
Patch9042:    0042-net-hns3-delete-duplicated-RSS-type.patch
Patch9043:    0043-net-bonding-fix-promiscuous-and-allmulticast-state.patch
Patch9044:    0044-net-bonding-fix-reference-count-on-mbufs.patch
Patch9045:    0045-app-testpmd-fix-bonding-mode-set.patch

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
meson build -Dexamples=l3fwd-power,ethtool,l3fwd,kni,dma,ptpclient
ninja -C build

%install
DESTDIR=$RPM_BUILD_ROOT/ ninja install -C build

cp ./build/examples/dpdk-l3fwd $RPM_BUILD_ROOT/usr/local/bin
cp ./build/examples/dpdk-l3fwd-power $RPM_BUILD_ROOT/usr/local/bin
cp ./build/examples/dpdk-ethtool $RPM_BUILD_ROOT/usr/local/bin
cp ./build/examples/dpdk-kni $RPM_BUILD_ROOT/usr/local/bin
cp ./build/examples/dpdk-dma $RPM_BUILD_ROOT/usr/local/bin
cp ./build/examples/dpdk-ptpclient $RPM_BUILD_ROOT/usr/local/bin

mkdir -p $RPM_BUILD_ROOT/usr/lib64
mv $RPM_BUILD_ROOT/usr/local/lib64/* $RPM_BUILD_ROOT/usr/lib64/

mkdir -p $RPM_BUILD_ROOT/usr/local/bin
ln -fs /usr/local/bin/dpdk-devbind.py $RPM_BUILD_ROOT/usr/local/bin/dpdk-devbind
mkdir $RPM_BUILD_ROOT/usr/lib64/dpdk/pmds-22.0/lib
mkdir $RPM_BUILD_ROOT/usr/lib64/dpdk/pmds-22.0/include
cd $RPM_BUILD_ROOT/usr/lib64/dpdk/pmds-22.0/include
ln -fs ../../../../local/include/* .
cd -
cd $RPM_BUILD_ROOT/usr/lib64/dpdk/pmds-22.0/lib
ln -fs ../../../*.so .
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
%exclude /usr/lib64/dpdk/pmds-22.0/include/*.h

%files devel
/usr/local/include
/usr/lib64/*.a
/usr/lib64/dpdk/pmds-22.0/include/*.h

%files doc

%files tools
/usr/local/bin/dpdk-pdump
/usr/local/bin/dpdk-dumpcap
/usr/local/bin/dpdk-proc-info
/usr/local/bin/dpdk-test
/usr/local/bin/dpdk-testpmd
/usr/local/bin/dpdk-l3fwd
/usr/local/bin/dpdk-l3fwd-power
/usr/local/bin/dpdk-ethtool
/usr/local/bin/dpdk-kni
/usr/local/bin/dpdk-dma
/usr/local/bin/dpdk-ptpclient

%post
/sbin/ldconfig
/usr/sbin/depmod

%postun
/sbin/ldconfig
/usr/sbin/depmod

%changelog
* Wed March 23 2022 Min Hu(Connor) <humin29@huawei.com> - 21.11-9
- fix adding examples app.

* Mon March 14 2022 Min Hu(Connor) <humin29@huawei.com> - 21.11-8
- add examples app.

* Wed Feb 09 2022 Min Hu(Connor) <humin29@huawei.com> - 21.11-7
- sync patches from upstreaming branch.

* Thu Jan 27 2022 Min Hu(Connor) <humin29@huawei.com> - 21.11-6
- fix key bugfixes for hns3 PMD.

* Fri Jan 14 2022 wuchangsheng <wuchangsheng2@huawei.com> - 21.11-5
- fix master thread not set affinity

* Wed Jan 12 2022 jiangheng <jiangheng12@huawei.com> - 21.11-4
- modify location of header and library Files

* Mon Jan 10 2022 jiangheng <jiangheng12@huawei.com> - 21.11-3
- add /usr/lib64/dpdk/*, here are some so files
- put lib file and header file in the same directory for third-party lib compile

* Sat Dec 25 2021 wuchangsheng <wuchangsheng2@huawei.com> - 21.11-2
- remove redundant file in rpm
- add gazelle support

* Fri Dec 17 2021 jiangheng <jiangheng12@huawei.com> - 21.11-1
- update to 21.11

* Fri Dec 17 2021 Min Hu <humin29@huawei.com> - 20.11-10
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
