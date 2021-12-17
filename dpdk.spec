Name: dpdk
Version: 20.11
Release: 10
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
Patch189: 0189-config-arm-check-SVE-CPU-flag.patch
Patch190: 0190-net-hns3-increase-VF-reset-retry-maximum.patch
Patch191: 0191-net-hns3-fix-delay-for-waiting-to-stop-Rx-Tx.patch
Patch192: 0192-net-hns3-fix-fake-queue-rollback.patch
Patch193: 0193-net-hns3-fix-VLAN-strip-log.patch
Patch194: 0194-net-hns3-fix-maximum-queues-on-configuration-failure.patch
Patch195: 0195-net-hns3-remove-unnecessary-blank-lines.patch
Patch196: 0196-net-hns3-support-Tx-push-quick-doorbell-for-performa.patch
Patch197: 0197-net-hns3-fix-traffic-management.patch
Patch198: 0198-config-arm-fix-SVE-build-with-GCC-8.3.patch
Patch199: 0199-net-hns3-fix-Arm-SVE-build-with-GCC-8.3.patch
Patch200: 0200-net-hns3-query-basic-info-for-VF.patch
Patch201: 0201-net-hns3-support-VLAN-filter-state-modify-for-VF.patch
Patch202: 0202-net-hns3-support-multiple-TC-MAC-pause.patch
Patch203: 0203-net-hns3-fix-residual-MAC-address-entry.patch
Patch204: 0204-net-hns3-remove-unnecessary-zero-assignments.patch
Patch205: 0205-net-hns3-fix-filter-parsing-comment.patch
Patch206: 0206-net-hns3-fix-timing-of-clearing-interrupt-source.patch
Patch207: 0207-net-hns3-remove-duplicate-compile-time-check.patch
Patch208: 0208-net-hns3-move-speed-auto-negotiation-warning.patch
Patch209: 0209-net-hns3-fix-flow-rule-list-in-multi-process.patch
Patch210: 0210-net-hns3-fix-Tx-prepare-after-stop.patch
Patch211: 0211-net-hns3-disable-PFC-if-not-configured.patch
Patch212: 0212-net-hns3-use-the-correct-HiSilicon-copyright.patch
Patch213: 0213-app-testpmd-change-port-link-speed-without-stopping-.patch
Patch214: 0214-ethdev-add-dev-configured-flag.patch
Patch215: 0215-net-hns3-add-start-stop-Tx-datapath-request-for-MP.patch
Patch216: 0216-net-hns3-support-set-link-up-down-for-PF.patch
Patch217: 0217-net-hns3-fix-queue-flow-action-validation.patch
Patch218: 0218-net-hns3-fix-taskqueue-pair-reset-command.patch
Patch219: 0219-net-hns3-fix-Tx-push-capability.patch
Patch220: 0220-examples-kni-close-port-before-exit.patch
Patch221: 0221-net-hns3-fix-residual-MAC-after-setting-default-MAC.patch
Patch222: 0222-net-hns3-fix-input-parameters-of-MAC-functions.patch
Patch223: 0223-net-bonding-fix-dedicated-queue-mode-in-vector-burst.patch
Patch224: 0224-net-bonding-fix-RSS-key-length.patch
Patch225: 0225-app-testpmd-add-command-to-show-LACP-bonding-info.patch
Patch226: 0226-app-testpmd-retain-all-original-dev-conf-when-config.patch
Patch227: 0227-net-hns3-remove-similar-macro-function-definitions.patch
Patch228: 0228-net-hns3-fix-interrupt-vector-freeing.patch
Patch229: 0229-net-hns3-add-runtime-config-for-mailbox-limit-time.patch
Patch230: 0230-net-hns3-fix-mailbox-communication-with-HW.patch
Patch231: 0231-app-testpmd-support-multi-process.patch
Patch232: 0232-app-testpmd-fix-key-for-RSS-flow-rule.patch
Patch233: 0233-app-testpmd-release-flows-left-before-port-stop.patch
Patch234: 0234-app-testpmd-delete-unused-function.patch
Patch235: 0235-dmadev-introduce-DMA-device-support.patch
Patch236: 0236-net-hns3-rename-multicast-address-function.patch
Patch237: 0237-net-hns3-rename-unicast-address-function.patch
Patch238: 0238-net-hns3-rename-multicast-address-removal-function.patch
Patch239: 0239-net-hns3-extract-common-interface-to-check-duplicate.patch
Patch240: 0240-net-hns3-remove-redundant-multicast-MAC-interface.patch
Patch241: 0241-net-hns3-rename-unicast-address-removal-function.patch
Patch242: 0242-net-hns3-remove-redundant-multicast-removal-interfac.patch
Patch243: 0243-net-hns3-add-HW-ops-structure-to-operate-hardware.patch
Patch244: 0244-net-hns3-use-HW-ops-to-config-MAC-features.patch
Patch245: 0245-net-hns3-unify-MAC-and-multicast-address-configurati.patch
Patch246: 0246-net-hns3-unify-MAC-address-add-and-remove.patch
Patch247: 0247-net-hns3-unify-multicast-address-check.patch
Patch248: 0248-net-hns3-refactor-multicast-MAC-address-set-for-PF.patch
Patch249: 0249-net-hns3-unify-multicast-MAC-address-set-list.patch
Patch250: 0250-bonding-show-Tx-policy-for-802.3AD-mode.patch
Patch251: 0251-net-hns3-fix-secondary-process-reference-count.patch
Patch252: 0252-net-hns3-fix-multi-process-action-register-and-unreg.patch
Patch253: 0253-net-hns3-unregister-MP-action-on-close-for-secondary.patch
Patch254: 0254-net-hns3-refactor-multi-process-initialization.patch
Patch255: 0255-usertools-devbind-add-Kunpeng-DMA.patch
Patch256: 0256-kni-check-error-code-of-allmulticast-mode-switch.patch
Patch257: 0257-net-hns3-simplify-queue-DMA-address-arithmetic.patch
Patch258: 0258-net-hns3-remove-redundant-function-declaration.patch
Patch259: 0259-net-hns3-modify-an-indent-alignment.patch
Patch260: 0260-net-hns3-use-unsigned-integer-for-bitwise-operations.patch
Patch261: 0261-net-hns3-extract-common-code-to-its-own-file.patch
Patch262: 0262-net-hns3-move-declarations-in-flow-header-file.patch
Patch263: 0263-net-hns3-remove-magic-numbers.patch
Patch264: 0264-net-hns3-mark-unchecked-return-of-snprintf.patch
Patch265: 0265-net-hns3-remove-PF-VF-duplicate-code.patch
Patch266: 0266-app-testpmd-remove-unused-header-file.patch
Patch267: 0267-usertools-add-Intel-DLB-device-binding.patch

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

BuildRequires: meson ninja-build gcc
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
echo "lib64/%{name}" >> $RPM_BUILD_ROOT/etc/ld.so.conf.d/%{name}-%{_arch}.conf
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

%define _unpackaged_files_terminate_build 0



%files
/usr/local/bin/*.py
/lib/modules/%{kern_devel_ver}/extra/dpdk/*
/lib64/librte*.so*
%{_sbindir}/dpdk-devbind
%config(noreplace) /etc/ld.so.conf.d/*

%files devel
%dir /usr/local/include/
/usr/local/include/*.h
/usr/local/include/generic/*.h
/usr/local/share/dpdk/examples/*
/usr/local/lib64/*
/usr/local/bin/*
%dir /usr/share/dpdk/%{target}/
/usr/share/dpdk/%{target}/include/*
/usr/share/dpdk/%{target}/lib/*

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
