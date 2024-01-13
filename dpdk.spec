# Add option to build as static libraries (--without shared)
%bcond_without shared
# Add option to build without examples
%bcond_with examples
# Add option to build without tools
%bcond_without tools

# Avoid architecture-specific name of build-dir to fix per-arch reproducibility with doxygen
%global _vpath_builddir %{_vendor}-%{_target_os}-build

Name: dpdk
Version: 23.11
Release: 2
URL: http://dpdk.org
Source: https://fast.dpdk.org/rel/dpdk-%{version}.tar.xz

# upstream patch number start from 6000
# self developed patch number start from 9000
Patch9001: 0001-add-igb_uio.patch
Patch9002: 0002-dpdk-add-secure-compile-option-and-fPIC-option.patch
Patch9003: 0003-dpdk-bugfix-the-deadlock-in-rte_eal_init.patch
Patch9004: 0004-dpdk-master-core-donot-set-affinity-in-libstorage.patch
Patch9005: 0005-dpdk-change-the-log-level-in-prepare_numa.patch
Patch9006: 0006-dpdk-fix-dpdk-coredump-problem.patch
Patch9007: 0007-dpdk-fix-cpu-flag-error-in-Intel-R-Xeon-R-CPU-E5-262.patch
Patch9008: 0008-reinit-support-return-ok.patch
Patch9009: 0009-gro-fix-gro-with-tcp-push-flag.patch
Patch9010: 0010-example-l3fwd-masking-wrong-warning-array-subscript-.patch
Patch9011: 0011-dpdk-add-support-for-gazellle.patch
Patch9012: 0012-lstack-need-skip-rte_bus_probe-when-use-ltran-mode.patch

BuildRequires: meson
BuildRequires: python3-pyelftools
BuildRequires: diffutils

Summary: Set of libraries and drivers for fast packet processing

#
# Note that, while this is dual licensed, all code that is included with this
# Pakcage are BSD licensed. The only files that aren't licensed via BSD is the
# kni kernel module which is dual LGPLv2/BSD, and thats not built for fedora.
#
License: BSD and LGPLv2 and GPLv2

#
# The DPDK is designed to optimize througput of network traffic using, among
# other techniques, carefully crafted assembly instructions.  As such it
# needs extensive work to port it to other architectures.
#
ExclusiveArch: x86_64 i686 aarch64

BuildRequires: gcc
BuildRequires: kernel-headers, kernel-devel, libpcap-devel, zlib-devel
BuildRequires: numactl-devel
BuildRequires: rdma-core-devel
BuildRequires: openssl-devel
BuildRequires: libbpf-devel
BuildRequires: libfdt-devel
BuildRequires: libatomic
BuildRequires: libarchive-devel
BuildRequires: uname-build-checks
BuildRequires: procps-ng
BuildRequires: git
BuildRequires: groff-base
BuildRequires: libibverbs

%define kern_devel_ver %(uname -r)

%description
The Data Plane Development Kit is a set of libraries and drivers for
fast packet processing in the user space.

%package devel
Summary: Data Plane Development Kit development files
Requires: %{name}%{?_isa} = %{?epoch:%{epoch}:}%{version}-%{release} python3
%if ! %{with shared}
Provides: %{name}-static = %{?epoch:%{epoch}:}%{version}-%{release}
%endif
Requires: rdma-core-devel

%description devel
This package contains the headers and other files needed for developing
applications with the Data Plane Development Kit.


%if %{with tools}
%package tools
Summary: Tools for setting up Data Plane Development Kit environment
Requires: %{name} = %{?epoch:%{epoch}:}%{version}-%{release}
Requires: kmod pciutils findutils iproute python3-pyelftools

%description tools
%{summary}
%endif

%if %{with examples}
%package examples
Summary: Data Plane Development Kit example applications
BuildRequires: libvirt-devel
BuildRequires: make

%description examples
Example applications utilizing the Data Plane Development Kit, such
as L2 and L3 forwarding.
%endif

%define sdkdir  %{_datadir}/%{name}
%define incdir %{_includedir}/%{name}
%define pmddir %{_libdir}/%{name}-pmds

%pretrans -p <lua>
-- This is to clean up directories before links created
-- See https://fedoraproject.org/wiki/Packaging:Directory_Replacement

directories = {
    "/usr/share/dpdk/mk/exec-env/bsdapp",
    "/usr/share/dpdk/mk/exec-env/linuxapp"
}
for i,path in ipairs(directories) do
  st = posix.stat(path)
  if st and st.type == "directory" then
    status = os.rename(path, path .. ".rpmmoved")
    if not status then
      suffix = 0
      while not status do
        suffix = suffix + 1
        status = os.rename(path .. ".rpmmoved", path .. ".rpmmoved." .. suffix)
      end
      os.rename(path, path .. ".rpmmoved")
    end
  end
end

%prep
%autosetup -n %{name}-%{version} -S git_am

%build
CFLAGS="$(echo %{optflags} -fcommon)" \
%meson --includedir=include/dpdk \
       -Ddrivers_install_subdir=dpdk-pmds \
       -Denable_docs=false \
       -Dmachine=generic \
       -Dexamples=l3fwd-power,ethtool,l3fwd,dma,ptpclient \
%if %{with shared}
  --default-library=shared
%else
  --default-library=static
%endif

%meson_build

%install
%meson_install

ln -fs %{_bindir}/dpdk-devbind.py $RPM_BUILD_ROOT/%{_bindir}/dpdk-devbind
strip -g $RPM_BUILD_ROOT/lib/modules/%{kern_devel_ver}/extra/dpdk/igb_uio.ko

%files
# BSD
%if ! %{with shared}
%{_libdir}/*.a
%exclude %{_libdir}/*.so*
%exclude %{pmddir}/*.so*
%else
%{_libdir}/*.so*
%{pmddir}/*.so*
%exclude %{_libdir}/*.a
%endif
%{_bindir}/dpdk-*.py
%{_bindir}/dpdk-devbind
/lib/modules/%{kern_devel_ver}/extra/dpdk/*.ko

%files devel
#BSD
%{incdir}/
%{sdkdir}
%ghost %{sdkdir}/mk/exec-env/bsdapp
%ghost %{sdkdir}/mk/exec-env/linuxapp
%exclude %{sdkdir}/examples/
%{_libdir}/pkgconfig/libdpdk.pc
%{_libdir}/pkgconfig/libdpdk-libs.pc


%if %{with tools}
%files tools
%{_bindir}/dpdk-*
%exclude %{_bindir}/dpdk-*.py
%exclude %{_bindir}/dpdk-devbind
%endif

%if %{with examples}
%files examples
%{_bindir}/dpdk_example_*
%doc %{sdkdir}/examples
%endif

%changelog
* Tue Jan 12 2024 jiangheng <jiangheng14@huawei.com> - 23.11-2
 add self-developed patches

* Tue Jan 09 2024 jiangheng <jiangheng14@huawei.com> - 23.11-1
 upgrade dpdk to 23.11

* Fri Dec 29 2023 huangdengdui <huangdengui@huawei.com> - 21.11-63
 The hns3 driver don't support QinQ insert for VF

* Fri Dec 15 2023 huangdengdui <huangdengui@huawei.com> - 21.11-62
 Sync some patches for hns3 about refactor mailbox and bugfix, modifies
 are as follow:
 - net/hns3: fix reset level comparison
 - net/hns3: fix disable command with firmware
 - net/hns3: fix VF multiple count on one reset
 - net/hns3: refactor handle mailbox function
 - net/hns3: refactor send mailbox function
 - net/hns3: refactor PF mailbox message struct
 - net/hns3: refactor VF mailbox message struct

* Fri Dec 8 2023 huangdengdui <huangdengui@huawei.com> - 21.11-61
 Sync some bugfix from upstreaming about testpmd and doc, modifies
 are as follow:
 - support set RSS hash algorithm
 - ethdev: add new API to get RSS hash algorithm by name
 - doc: fix description of RSS features
 - doc: fix RSS flow description in hns3 guide
 - doc: update features in hns3 guide
 - doc: fix hns3 build option about max queue number
 - app/testpmd: check port and queue Rx/Tx offloads
 - app/testpmd: fix Tx offload command
 - app/testpmd: allow offload config for all ports
 - app/testpmd: fix tunnel TSO configuration
 - app/testpmd: add explicit check for tunnel TSO
 - app/testpmd: fix tunnel TSO capability check
 - app/testpmd: remove useless check in TSO command

* Fri Dec 8 2023 huangdengdui <huangdengui@huawei.com> - 21.11-60
 Sync some bugfix from upstreaming about hns3 reset and modifies
 are as follow:
 - net/hns3: fix VF reset handler interruption
 - net/hns3: fix reset event status
 - net/hns3: fix ignored reset event

* Mon Nov 20 2023 huangdengdui <huangdengui@huawei.com> - 21.11-59
 Sync some patchs from upstreaming and modifies are as follow:
 - net/hns3: fix mailbox sync
 - net/hns3: report maximum buffer size
 - ethdev: add maximum Rx buffer size
 - app/procinfo: show RSS hash algorithm
 - ethdev: get RSS algorithm names
 - app/procinfo: adjust format of RSS info
 - app/procinfo: fix RSS info
 - net/hns3: support setting and querying RSS hash function
 - net/hns3: report RSS hash algorithms capability
 - ethdev: set and query RSS hash algorithm
 - ethdev: clarify RSS related fields usage
 - net/hns3: fix uninitialized hash algo value
 - net/hns3: keep set/get algo key functions local
 - net/hns3: fix some error logs
 - net/hns3: fix some return values
 - net/hns3: fix LRO offload to report
 - net/hns3: fix setting DCB capability
 - app/testpmd: ease configuring all offloads
 - net/hns3: refactor interrupt state query
 - net/hns3: fix IMP or global reset
 - net/hns3: fix multiple reset detected log
 - net/hns3: remove reset log in secondary
 - net/hns3: fix double stats for IMP and global reset
 - net/hns3: fix crash for NEON and SVE
 - net/hns3: fix unchecked Rx free threshold
 - net/hns3: fix typo in function name
 - net/hns3: fix build warning
 - telemetry: fix repeat display when callback don't init dict

* Fri Oct 27 2023 huangdengdui <huangdengui@huawei.com> - 21.11-58
 Sync some patchs from upstreaming and modifies are as follow:
 - maintainers: update for hns3 driver
 - app/testpmd: add command to flush multicast MAC addresses
 - app/testpmd: fix help string
 - app/testpmd: fix multicast address pool leak
 - net/hns3: optimize SVE Rx performance
 - net/hns3: optimize rearm mbuf for SVE Rx
 - net/hns3: optimize free mbuf for SVE Tx
 - net/hns3: fix order in NEON Rx
 - net/hns3: fix traffic management dump text alignment
 - net/hns3: fix traffic management thread safety
 - net/hns3: fix flushing multicast MAC address
 - net/hns3: fix error code for multicast resource
 - net/hns3: fix VF default MAC modified when set failed
 - net/hns3: fix index to look up table in NEON Rx
 - net/hns3: fix non-zero weight for disabled TC
 - config/arm: add HiSilicon HIP10

* Wed Aug 30 2023 herengui <herengui@kylinsec.com.cn> - 21.11-57
 - Add support for sw_64

* Mon Aug 21 2023 huangdengdui <huangdengui@huawei.com> - 21.11-56
 replace patch-287 to solve the duplicate setting for MAC address.

* Mon Jul 17 2023 chenjiji <chenjiji09@163.com> - 21.11-55
 Sync some patchs from upstreaming about add FDIR VLAN match
 mode runtime config and fix doc format for hns3 pmd. Patchs
 are as follow:
 - doc: unify sections of networking drivers guide
 - net/hns3: delete duplicate macro definition
 - net/hns3: add FDIR VLAN match mode runtime config
 - doc: fix kernel patch link in hns3 guide
 - doc: fix syntax in hns3 guide
 - doc: fix number of leading spaces in hns3 guide

* Sat Jul 15 2023 jiangheng <jiangheng14@huawei.com> - 21.11-54
- kni: fix build with Linux 6.3/6.5
- remove unused patch intruduced by "fix build with GCC 12"

* Wed Jul 12 2023 jiangheng <jiangheng14@huawei.com> - 21.11-53
- fix build with GCC 12

* Tue Jul 4 2023 zhoumin <zhoumin@loongson.cn> - 21.11-52
- EAL: support LoongArch architecture
- Backport bugfixes for ixgbe driver needed by LoongArch
- Remove linking to i40e driver for LoongArch because it was
  not supported in this version

* Fri Jun 30 2023 jiangheng <jiangheng14@huawei.com> - 21.11-51
- remove gazelle-proc-info, it function the same as gazellectl -x

* Mon Jun 19 2023 jiangheng <jiangheng14@huawei.com> - 21.11-50
- gro: fix gro with tcp push flag

* Tue Jun 13 2023 jiangheng <jiangheng14@huawei.com> - 21.11-49
- pdump: fix pcap_dump coredump caused by incorrect pkt_len

* Fri Jun 09 2023 jiangheng <jiangheng14@huawei.com> - 21.11-48
- distinguish self and upstream patches number

* Wed Jun 07 2023 chenjiji <chenjiji09@163.com> - 21.11-47
 Sync some patchs from upstreaming about a segment fault for
 testpmd app and a IMP reset trigger for hns3 pmd. Patchs are
 as follow:
 - ethdev: add API to check if queue is valid
 - app/testpmd: fix segment fault with invalid queue ID
 - net/hns3: fix IMP reset trigger

* Mon Jun 05 2023 chenjiji <chenjiji09@163.com> - 21.11-46
 Sync some patchs from upstreaming for hns3 pmd and modifications
 are as follow:
 1. fix RTC time after reset
 2. fix Rx ring mbuf leakage at reset process
 3. fix an uninitialized variable
 4. modify the code that violates the coding standards

* Fri Jun 02 2023 chenjiji <chenjiji09@163.com> - 21.11-45
 Sync some patchs from upstreaming about FEC feature. Patchs
 are as follow:
 - net/hns3: fix FEC mode for 200G ports
 - net/hns3: fix FEC mode check error
 - net/hns3: fix missing FEC capability
 - ethdev: introduce low latency RS FEC
 - app/testpmd: add setting and querying of LLRS FEC mode
 - net/hns3: add LLRS FEC mode support for 200G ports
 - net/hns3: get current FEC capability from firmware

* Sat May 27 2023 jiangheng <jiangheng14@huawei.com> - 21.11-44
- examples use static libraries to avoid unlinked dynamic libraries

* Wed May 24 2023 chenjiji <chenjiji09@163.com> - 21.11-43
 Sync some patchs from upstreaming for hns3 pmd and modifies
 are as follow:
 1. support flow control autoneg for fiber port
 2. support simplify hardware checksum offloading
 3. support dump media type
 4. add Tx Rx descriptor logs
 5. fix Rx multiple firmware reset interrupts
 6. ethdev: fix one address occupies two entries in MAC addrs

* Thu Apr 27 2023 chenjiji <chenjiji09@163.com> - 21.11-42
- fix empty devargs parsing
 Sync some patchs from upstreaming and modifies are as
 follow:
 1. The rte_kvargs_process() was used to parse KV pairs, it
 also supports to parse 'only keys' type. And the callback
 function parameter 'value' is NULL when parsed 'only keys'.
 This patch fixes segment fault when parse input args with
 'only keys'.
 2. The MAP_FAILED should be used to determine whether the
 mapping is successful but not NULL. This patch fix it.

* Fri Apr 21 2023 chenjiji <chenjiji09@163.com> - 21.11-41
- Telemetry: support display as hexadecimal
 Sync some patchs from upstreaming for telemetry and modifies
 are as follow:
 1. Support dispaly integer as hexadecimal.
 2. Fix data truncation for some u64 accept as int.
 3. Add JSON pretty print.

* Tue Apr 11 2023 bigclouds99 <yuelg@chinaunicom.cn> - 21.11-40
- Create a softlink to dpdk default driver path

* Tue Apr 04 2023 chenjiji <chenjiji09@163.com> - 21.11-39
 Sync some patchs from upstreaming branch and modifies
 are as follow:
 1. Add private dump for bonding, virtio and vhost.
 2. Support LACP info dump for bonding.
 3. Display RSS hash key of flow rule in testpmd.

* Sat Apr 01 2023 jiangheng <jiangheng14@huawei.com> - 21.11-38
- build as shared libraries to reduce the size of debug packet

* Sat Apr 01 2023 jiangheng <jiangheng14@huawei.com> - 21.11-37
- hinic: free tx mbuf use rte_pktmbuf_free_seg

* Thu Mar 23 2023 chenjiji <chenjiji09@163.com> - 21.11-36
 Fix a m_buf pool was not freed bugs for test and support
 CPPC cpufreq for l3fwd-power. Patchs are as follow:
  - test/mbuf: fix mbuf reset test
  - examples/l3fwd-power: support CPPC cpufreq

* Wed Mar 15 2023 chenjiji <chenjiji09@163.com> - 21.11-35
 Fix some RSS bugs and reimplement hash flow function for hns3:
  - fix some RSS bugs and optimize RSS codes for hns3
  - reimplement hash flow function for hns3 to satisfy the
    mainstream usage of rte flow hash in the community

* Fri Mar 03 2023 chenjiji <chenjiji09@163.com> - 21.11-34
 Support flow rule keeping capability for hns3 PMD and
 testpmd. Patchs are as follow:
  - net/hns3: declare flow rule keeping capability
  - app/testpmd: add --disable-flow-flush option

* Tue Feb 21 2023 chenjiji <chenjiji09@163.com> - 21.11-33
 refactor Rc/Tx function of hns3 PMD
 And patchs are as follows:
  - net/hns3: fix burst mode query with dummy function
  - net/hns3: add debug info for Rx/Tx dummy function
  - net/hns3: remove debug condition for Tx prepare
  - net/hns3: separate Tx prepare from getting Tx function
  - net/hns3: make getting Tx function static
  - net/hns3: extract common functions to set Rx/Tx

* Wed Feb 15 2023 chenjiji <chenjiji09@163.com> - 21.11-32
 Sync some RSS bugfix for hns3 PMD. And patchs are as follows:
  - net/hns3: fix log about indirection table size
  - net/hns3: extract common function to query device
  - net/hns3: refactor set RSS hash algorithm and key interface
  - net/hns3: fix RSS key size compatibility
  - net/hns3: fix clearing RSS configuration
  - net/hns3: use RSS filter list to check duplicated rule
  - net/hns3: remove useless code when destroy valid RSS rule
  - net/hns3: fix warning on flush or destroy rule
  - net/hns3: fix config struct used for conversion
  - net/hns3: fix duplicate RSS rule check

* Mon Feb 06 2023 jiangheng <jiangheng14@huawei.com> - 21.11-31
- linux/igb_uio: fix build with kernel 5.18+

* Fri Feb 03 2023 chenjiji <chenjiji09@163.com> - 21.11-30
- net/hns3: fix inaccurate RTC time to read

* Tue Jan 31 2023 jiangheng <jiangheng14@huawei.com> - 21.11-29
- remove unused patch

* Wed Jan 18 2023 jiangheng <jiangheng14@huawei.com> - 21.11-28
- fix build failed due to kernel upgrate to 6.1

* Wed Dec 14 2022 chenjiji <chenjiji09@163.com> - 21.11-27
- dma/hisilicon: support vchan status query

* Wed Nov 16 2022 chenjiji <chenjiji09@163.com> - 21.11-26
  proc-info adds dumping the following features:
   - dpdk version
   - firmware version
   - RSS RETA
   - module eeprom information
   - Rx/Tx burst mode 
   - Rx/Tx descriptor

* Wed Nov 16 2022 chenjiji <chenjiji09@163.com> - 21.11-25
  Sync some patches for bonding PMD and testpmd. And patchs  
  are as follows:
   - app/testpmd: revert MAC update in checksum forwarding
   - net/bonding: fix bond4 drop valid MAC packets
   - net/bonding: fix slave device Rx/Tx offload configuration
   - app/testpmd: fix MAC header in csum forward engine
   - app/testpmd: update bond port configurations when add slave
   - app/testpmd: fix GENEVE parsing in checksum mode
   - net: add UDP/TCP checksum in mbuf segments
   - app/testpmd: add SW L4 checksum in multi-segments
   - app/testpmd: fix L4 checksum in multi-segments
   - net/bonding: fix mbuf fast free handling
  
* Tue Nov 15 2022 jiangheng <jiangheng14@huawei.com> - 21.11-24
- proc-info: add gazelle-proc-info support in dpdk

* Mon Nov 14 2022 kircher <majun65@huawei.com> - 21.11-23
- pdump: add gazelle-pdump for pcap

* Mon Nov 07 2022 jiangheng <jiangheng14@huawei.com> - 21.11-22
- set platform to generic for compatibility

* Sat Oct 29 2022 chenjiji <chenjiji09@163.com> - 21.11-21
  Sync some patches for bonding PMD and testpmd. And patchs 
  are as follows:
   - net/bonding: fix Tx hash for TCP
   - net/bonding: add link speeds configuration
   - net/bonding: call Tx prepare before Tx burst
   - net/bonding: fix MTU set for slaves
   - app/testpmd: remove jumbo offload related code

* Fri Oct 28 2022 jiangheng <jiangheng14@huawei.com> - 21.11-20
- gro: trim tail padding bytes
- gro: check payload length after trim
- gro: fix chain index for more than 2 packets

* Sat Oct 22 2022 Huisong Li <lihuisong@huawei.com> - 21.11-19
  Sync some patches for hns3 PMD, telemetry and testpmd. And main
  modifications are as follows:
   - backport some bugfixes for hns3
   - revert Tx performance optimization for hns3
   - add Rx/Tx descriptor dump feature for hns3
   - refactor some RSS commands for testpmd
   - add ethdev telemetry private dump
   - add dmadev telemetry
   - sync telemetry lib

* Thu Oct 6 2022 wuchangsheng <wuchangsheng2@huawei.com> - 21.11-18
- reinit support return ok

* Tue Sep 13 2022 jiangheng <jiangheng14@huawei.com> - 21.11-17
- remove secure compilation options rpath

* Fri Sep 09 2022 jiangheng <jiangheng14@huawei.com> - 21.11-16
- fix CVE-2022-28199

* Thu Sep 08 2022 jiangheng <jiangheng14@huawei.com> - 21.11-15
- fix CVE-2022-2132

* Thu Jul 07 2022 Honggang Li <honggangli@163.com> - 21.11-14
- Remove duplicated BuildRequires python-pyelftools

* Tue Jul 05 2022 Honggang Li <honggangli@163.com> - 21.11-13
- Build mlx5 and mlx4 PMD

* Thu Jun 16 2022 Dongdong Liu <liudongdong3@huawei.com> - 21.11-12
- sync patches from upstreaming branch.

* Fri Jun 10 2022 xiusailong <xiusailong@huawei.com> - 21.11-11
- fix CVE-2021-3839 CVE-2022-0669

* Tue May 17 2022 Min Hu(Connor) <humin29@huawei.com> - 21.11-10
- sync patches from 22.03.

* Wed Mar 23 2022 Min Hu(Connor) <humin29@huawei.com> - 21.11-9
- fix adding examples app.

* Mon Mar 14 2022 Min Hu(Connor) <humin29@huawei.com> - 21.11-8
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

* Tue Aug 31 2021 Min Hu <humin29@huawei.com> - 20.11-9
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
