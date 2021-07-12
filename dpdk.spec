Name: dpdk
Version: 20.11
Release: 4
Packager: packaging@6wind.com
URL: http://dpdk.org
%global source_version  20.11
Source: https://git.dpdk.org/dpdk/snapshot/%{name}-%{version}.tar.xz

Patch0: 0001-net-hns3-adjust-MAC-address-logging.patch
Patch1: 0002-net-hns3-fix-FEC-state-query.patch
Patch2: 0003-net-hns3-fix-build-with-SVE.patch
Patch3: 0004-net-hns3-fix-interception-with-flow-director.patch
Patch4: 0005-net-hns3-fix-xstats-with-id-and-names.patch
Patch5: 0006-net-hns3-fix-error-code-in-xstats.patch
Patch6: 0007-net-hns3-fix-Rx-Tx-errors-stats.patch
Patch7: 0008-net-hns3-fix-crash-with-multi-process.patch
Patch8: 0009-net-hns3-remove-unnecessary-memset.patch
Patch9: 0010-net-hns3-fix-jumbo-frame-flag-condition-for-MTU-set.patch
Patch10: 0011-net-hns3-use-C11-atomics-builtins-for-resetting.patch
Patch11: 0012-net-hns3-support-traffic-management.patch
Patch12: 0013-net-hns3-fix-VF-query-link-status-in-dev-init.patch
Patch13: 0014-net-hns3-use-new-opcode-for-clearing-hardware-resour.patch
Patch14: 0015-net-hns3-fix-register-length-when-dumping-registers.patch
Patch15: 0016-net-hns3-fix-data-overwriting-during-register-dump.patch
Patch16: 0017-net-hns3-fix-dump-register-out-of-range.patch
Patch17: 0018-net-hns3-remove-unused-assignment-for-RSS-key.patch
Patch18: 0019-net-hns3-encapsulate-DFX-stats-in-datapath.patch
Patch19: 0020-net-hns3-move-queue-stats-to-xstats.patch
Patch20: 0021-net-hns3-refactor-converting-descriptor-error.patch
Patch21: 0022-net-hns3-refactor-flow-checks-into-own-functions.patch
Patch22: 0023-net-hns3-reconstruct-Rx-interrupt-map.patch
Patch23: 0024-net-hns3-extract-common-checks-for-flow-director.patch
Patch24: 0025-net-hns3-refactor-reset-event-report-function.patch
Patch25: 0026-net-hns3-fix-memory-leak-on-secondary-process-exit.patch
Patch26: 0027-net-hns3-fix-interrupt-resources-in-Rx-interrupt-mod.patch
Patch27: 0028-net-hns3-rename-RSS-functions.patch
Patch28: 0029-net-hns3-adjust-some-comments.patch
Patch29: 0030-net-hns3-remove-unnecessary-parentheses.patch
Patch30: 0031-net-hns3-adjust-format-specifier-for-enum.patch
Patch31: 0032-net-hns3-support-LSC-event-report.patch
Patch32: 0033-net-hns3-fix-query-order-of-link-status-and-link-inf.patch
Patch33: 0034-net-hns3-fix-link-status-change-from-firmware.patch
Patch34: 0035-net-hns3-fix-RSS-indirection-table-size.patch
Patch35: 0036-net-hns3-constrain-TM-peak-rate.patch
Patch36: 0037-net-hns3-remove-MPLS-from-supported-flow-items.patch
Patch37: 0038-net-hns3-fix-stats-flip-overflow.patch
Patch38: 0039-net-hns3-use-C11-atomics.patch
Patch39: 0040-net-hns3-fix-flow-director-rule-residue-on-malloc-fa.patch
Patch40: 0041-net-hns3-fix-firmware-exceptions-by-concurrent-comma.patch
Patch41: 0042-net-hns3-fix-VF-reset-on-mailbox-failure.patch
Patch42: 0043-net-hns3-validate-requested-maximum-Rx-frame-length.patch
Patch43: 0044-drivers-net-redefine-array-size-macros.patch
Patch44: 0045-net-hns3-support-module-EEPROM-dump.patch
Patch45: 0046-net-hns3-add-more-registers-to-dump.patch
Patch46: 0047-net-hns3-implement-Tx-mbuf-free-on-demand.patch
Patch47: 0048-net-hns3-add-bytes-stats.patch
Patch48: 0049-net-hns3-add-imissed-packet-stats.patch
Patch49: 0050-net-hns3-encapsulate-port-shaping-interface.patch
Patch50: 0051-net-hns3-fix-device-capabilities-for-copper-media-ty.patch
Patch51: 0052-net-hns3-support-PF-device-with-copper-PHYs.patch
Patch52: 0053-net-hns3-support-Rx-descriptor-advanced-layout.patch
Patch53: 0054-net-hns3-fix-HW-buffer-size-on-MTU-update.patch
Patch54: 0055-net-hns3-remove-unused-parameter-markers.patch
Patch55: 0056-net-hns3-fix-mbuf-leakage.patch
Patch56: 0057-net-hns3-process-MAC-interrupt.patch
Patch57: 0058-net-hns3-fix-imprecise-statistics.patch
Patch58: 0059-net-hns3-add-runtime-config-to-select-IO-burst-funct.patch
Patch59: 0060-net-hns3-support-outer-UDP-checksum.patch
Patch60: 0061-net-hns3-adjust-format-of-RAS-related-structures.patch
Patch61: 0062-net-hns3-delete-redundant-xstats-RAS-statistics.patch
Patch62: 0063-net-hns3-support-imissed-stats-for-PF-VF.patch
Patch63: 0064-net-hns3-support-oerrors-stats-in-PF.patch
Patch64: 0065-net-hns3-support-Tx-descriptor-status-query.patch
Patch65: 0066-net-hns3-support-Rx-descriptor-status-query.patch
Patch66: 0067-net-hns3-fix-reporting-undefined-speed.patch
Patch67: 0068-net-hns3-fix-build-for-SVE-path.patch
Patch68: 0069-net-hns3-fix-processing-Tx-offload-flags.patch
Patch69: 0070-net-hns3-fix-Tx-checksum-for-UDP-packets-with-specia.patch
Patch70: 0071-net-hns3-fix-link-update-when-failed-to-get-link-inf.patch
Patch71: 0072-net-hns3-fix-long-task-queue-pairs-reset-time.patch
Patch72: 0073-net-hns3-fix-MTU-config-complexity.patch
Patch73: 0074-net-hns3-support-IEEE-1588-PTP.patch
Patch74: 0075-ethdev-validate-input-in-register-info.patch
Patch75: 0076-net-hns3-support-wait-in-link-update.patch
Patch76: 0077-net-hns3-fix-some-function-names-for-copper-media-ty.patch
Patch77: 0078-net-hns3-fix-setting-default-MAC-address-in-bonding-.patch
Patch78: 0079-net-hns3-fix-FLR-miss-detection.patch
Patch79: 0080-net-hns3-fix-rollback-after-setting-PVID-failure.patch
Patch80: 0081-net-hns3-fix-flow-control-exception.patch
Patch81: 0082-net-hns3-fix-flow-counter-value.patch
Patch82: 0083-net-hns3-fix-VF-mailbox-head-field.patch
Patch83: 0084-net-hns3-support-get-device-version-when-dump-regist.patch
Patch84: 0085-net-hns3-delete-redundant-blank-line.patch
Patch85: 0086-net-hns3-fix-code-style.patch
Patch86: 0087-net-hns3-refactor-VF-LSC-event-report.patch
Patch87: 0088-net-hns3-refactor-PF-LSC-event-report.patch
Patch88: 0089-net-hns3-log-selected-datapath.patch
Patch89: 0090-net-hns3-simplify-selecting-Rx-Tx-function.patch
Patch90: 0091-net-hns3-fix-rollback-in-PF-init.patch
Patch91: 0092-net-hns3-fix-concurrent-interrupt-handling.patch
Patch92: 0093-net-hns3-fix-some-packet-types.patch
Patch93: 0094-net-hns3-fix-timing-in-resetting-queues.patch
Patch94: 0095-net-hns3-fix-queue-state-when-concurrent-with-reset.patch
Patch95: 0096-net-hns3-fix-configure-FEC-when-concurrent-with-rese.patch
Patch96: 0097-net-hns3-delete-mailbox-arq-ring.patch
Patch97: 0098-net-hns3-fix-possible-mismatched-response-of-mailbox.patch
Patch98: 0099-net-hns3-fix-VF-handling-LSC-event-in-secondary-proc.patch
Patch99: 0100-net-hns3-fix-timing-in-mailbox.patch
Patch100: 0101-net-hns3-fix-use-of-command-status-enumeration.patch
Patch101: 0102-net-hns3-fix-verification-of-NEON-support.patch
Patch102: 0103-net-hns3-fix-missing-outer-L4-UDP-flag-for-VXLAN.patch
Patch103: 0104-net-hns3-add-reporting-tunnel-GRE-packet-type.patch
Patch104: 0105-net-hns3-fix-PTP-capability-report.patch
Patch105: 0106-net-hns3-list-supported-ptypes-for-advanced-Rx-descr.patch
Patch106: 0107-net-hns3-remove-VLAN-QinQ-ptypes-from-support-list.patch
Patch107: 0108-net-hns3-fix-supported-speed-of-copper-ports.patch
Patch108: 0109-net-hns3-add-1000M-speed-bit-for-copper-PHYs.patch
Patch109: 0110-net-hns3-fix-flow-control-mode.patch
Patch110: 0111-net-hns3-fix-firmware-compatibility-configuration.patch
Patch111: 0112-net-hns3-obtain-supported-speed-for-fiber-port.patch
Patch112: 0113-net-hns3-report-speed-capability-for-PF.patch
Patch113: 0114-net-hns3-support-link-speed-autoneg-for-PF.patch
Patch114: 0115-net-hns3-support-flow-control-autoneg-for-copper-por.patch
Patch115: 0116-net-hns3-support-fixed-link-speed.patch
Patch116: 0117-net-hns3-rename-Rx-burst-function.patch
Patch117: 0118-net-hns3-remove-unused-macros.patch
Patch118: 0119-net-hns3-support-RAS-process-in-Kunpeng-930.patch
Patch119: 0120-net-hns3-support-masking-device-capability.patch
Patch120: 0121-net-hns3-simplify-Rx-checksum.patch
Patch121: 0122-net-hns3-check-max-SIMD-bitwidth.patch
Patch122: 0123-net-hns3-add-compile-time-verification-on-Rx-vector.patch
Patch123: 0124-net-hns3-remove-redundant-mailbox-response.patch
Patch124: 0125-net-hns3-fix-DCB-mode-check.patch
Patch125: 0126-net-hns3-fix-VMDq-mode-check.patch
Patch126: 0127-net-hns3-fix-flow-director-lock.patch
Patch127: 0128-net-hns3-move-link-speeds-check-to-configure.patch
Patch128: 0129-net-hns3-remove-unused-macro.patch
Patch129: 0130-net-hns3-fix-traffic-management-support-check.patch
Patch130: 0131-net-hns3-ignore-devargs-parsing-return.patch
Patch131: 0132-net-hns3-fix-mailbox-error-message.patch
Patch132: 0133-net-hns3-fix-processing-link-status-message-on-PF.patch
Patch133: 0134-net-hns3-remove-unused-mailbox-macro-and-struct.patch
Patch134: 0135-net-hns3-fix-typos-on-comments.patch
Patch135: 0136-net-hns3-disable-MAC-status-report-interrupt.patch
Patch136: 0137-net-hns3-fix-handling-link-update.patch
Patch137: 0138-net-hns3-fix-link-status-when-port-is-stopped.patch
Patch138: 0139-net-hns3-fix-link-speed-when-port-is-down.patch
Patch139: 0140-net-hns3-support-preferred-burst-size-and-queues-in-.patch
Patch140: 0141-net-hns3-log-time-delta-in-decimal-format.patch
Patch141: 0142-net-hns3-fix-time-delta-calculation.patch
Patch142: 0143-net-hns3-select-Tx-prepare-based-on-Tx-offload.patch
Patch143: 0144-net-hns3-fix-MAC-enable-failure-rollback.patch
Patch144: 0145-net-hns3-fix-IEEE-1588-PTP-for-scalar-scattered-Rx.patch
Patch145: 0146-net-hns3-remove-some-unused-capabilities.patch
Patch146: 0147-net-hns3-refactor-optimised-register-write.patch
Patch147: 0148-net-hns3-use-existing-macro-to-get-array-size.patch
Patch148: 0149-net-hns3-improve-IO-path-data-cache-usage.patch
Patch149: 0150-net-hns3-log-flow-director-configuration.patch
Patch150: 0151-net-hns3-fix-vector-Rx-burst-limitation.patch
Patch151: 0152-net-hns3-remove-read-when-enabling-TM-QCN-error-even.patch
Patch152: 0153-net-hns3-remove-unused-VMDq-code.patch
Patch153: 0154-net-hns3-increase-readability-in-logs.patch
Patch154: 0155-net-hns3-fix-debug-build.patch
Patch155: 0156-net-hns3-return-error-on-PCI-config-write-failure.patch
Patch156: 0157-net-hns3-fix-log-on-flow-director-clear.patch
Patch157: 0158-net-hns3-clear-hash-map-on-flow-director-clear.patch
Patch158: 0159-net-hns3-fix-VF-alive-notification-after-config-rest.patch
Patch159: 0160-net-hns3-fix-querying-flow-director-counter-for-out-.patch
Patch160: 0161-net-hns3-fix-TM-QCN-error-event-report-by-MSI-X.patch
Patch161: 0162-net-hns3-fix-mailbox-message-ID-in-log.patch
Patch162: 0163-net-hns3-fix-secondary-process-request-start-stop-Rx.patch
Patch163: 0164-net-hns3-fix-ordering-in-secondary-process-initializ.patch
Patch164: 0165-net-hns3-fail-setting-FEC-if-one-bit-mode-is-not-sup.patch
Patch165: 0166-net-hns3-fix-Rx-Tx-queue-numbers-check.patch
Patch166: 0167-net-hns3-fix-requested-FC-mode-rollback.patch
Patch167: 0168-net-hns3-remove-meaningless-packet-buffer-rollback.patch
Patch168: 0169-net-hns3-fix-DCB-configuration.patch
Patch169: 0170-net-hns3-fix-DCB-reconfiguration.patch
Patch170: 0171-net-hns3-fix-link-speed-when-VF-device-is-down.patch
Patch171: 0172-net-bonding-fix-adding-itself-as-its-slave.patch
Patch172: 0173-ipc-use-monotonic-clock.patch
Patch173: 0174-ethdev-add-queue-state-in-queried-queue-information.patch
Patch174: 0175-app-testpmd-fix-queue-stats-mapping-configuration.patch
Patch175: 0176-app-testpmd-fix-bitmap-of-link-speeds-when-force-spe.patch
Patch176: 0177-app-testpmd-add-link-autoneg-status-display.patch
Patch177: 0178-app-testpmd-support-cleanup-Tx-queue-mbufs.patch
Patch178: 0179-app-testpmd-show-link-flow-control-info.patch
Patch179: 0180-app-testpmd-support-display-queue-state.patch
Patch180: 0181-app-testpmd-fix-max-queue-number-for-Tx-offloads.patch
Patch181: 0182-app-testpmd-fix-forward-lcores-number-for-DCB.patch
Patch182: 0183-app-testpmd-fix-DCB-forwarding-configuration.patch
Patch183: 0184-app-testpmd-fix-DCB-re-configuration.patch
Patch184: 0185-app-testpmd-check-DCB-info-support-for-configuration.patch
Patch185: 0186-app-testpmd-verify-DCB-config-during-forward-config.patch
Patch186: 0187-app-testpmd-add-forwarding-configuration-to-DCB-conf.patch
Patch187: 0188-app-testpmd-remove-redundant-forwarding-initializati.patch
Patch188: 0189-net-fix-compiling-bug-for-20.11-merge.patch

Summary: Data Plane Development Kit core
Group: System Environment/Libraries
License: BSD and LGPLv2 and GPLv2

ExclusiveArch: i686 x86_64 aarch64
%global machine armv8a
%global target build


BuildRequires: meson ninja-build gcc
BuildRequires: kernel-devel
BuildRequires: uname-build-checks
BuildRequires: doxygen python3-sphinx

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

%prep
%autosetup -n %{name}-%{version} -p1

%build
%define debug_package %{nil}
meson %{target} -Ddisable_drivers=*/octeontx2 -Ddisable_drivers=*/fpga* -Ddisable_drivers=*/ifpga* -Denable_kmods=true -Denable_docs=true
ninja -C %{target}

%install
namer=%{kern_devel_ver}
DESTDIR=$RPM_BUILD_ROOT/ meson install -C %{target}

strip -g $RPM_BUILD_ROOT/lib/modules/${namer}/extra/dpdk/rte_kni.ko

%define _unpackaged_files_terminate_build 0

%files
/lib/modules/%{kern_devel_ver}/extra/dpdk/*
/usr/local/bin/*
/usr/local/lib64/*

%files devel
/usr/local/include/*
/usr/local/share/dpdk/*

%files doc
/usr/local/share/doc/*

%post
/sbin/ldconfig
/usr/sbin/depmod

%postun
/sbin/ldconfig
/usr/sbin/depmod

%changelog
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
