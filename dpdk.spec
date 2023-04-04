Name: dpdk
Version: 21.11
Release: 39
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
Patch9046:    0046-ethdev-introduce-dump-API.patch
Patch9047:    0047-app-procinfo-add-device-private-info-dump.patch
Patch9048:    0048-net-hns3-dump-device-basic-info.patch
Patch9049:    0049-net-hns3-dump-device-feature-capability.patch
Patch9050:    0050-net-hns3-dump-device-MAC-info.patch
Patch9051:    0051-net-hns3-dump-queue-info.patch
Patch9052:    0052-net-hns3-dump-VLAN-configuration-info.patch
Patch9053:    0053-net-hns3-dump-flow-director-basic-info.patch
Patch9054:    0054-net-hns3-dump-TM-configuration-info.patch
Patch9055:    0055-net-hns3-dump-flow-control-info.patch
Patch9056:    0056-net-hns3-change-dump-file-name.patch
Patch9057:    0057-net-hns3-fix-code-check-for-dump.patch
Patch9058:    0058-ethdev-fix-ethdev-version-map.patch
Patch9059:    0059-net-hns3-delete-simple-bd-cap.patch
Patch9060:    0060-net-hns3-fix-TM-info-dump.patch
Patch9061:    0061-dma-hisilicon-support-Kunpeng-930.patch
Patch9062:    0062-dma-hisilicon-support-error-handling-with-Kunpeng-93.patch
Patch9063:    0063-dma-hisilicon-support-registers-dump-for-Kunpeng-930.patch
Patch9064:    0064-dma-hisilicon-add-queue-full-statistics.patch
Patch9065:    0065-dma-hisilicon-use-common-PCI-device-naming.patch
Patch9066:    0066-app-testpmd-check-starting-port-is-not-in-bonding.patch
Patch9067:    0067-examples-vhost-remove-DMA-type-option-help-info.patch
Patch9068:    0068-kni-fix-freeing-order-in-device-release.patch
Patch9069:    0069-net-hns3-remove-duplicate-macro-definition.patch
Patch9070:    0070-net-hns3-fix-RSS-TC-mode-entry.patch
Patch9071:    0071-net-hns3-fix-VF-RSS-TC-mode-entry.patch
Patch9072:    0072-net-hns3-increase-time-waiting-for-PF-reset-completi.patch
Patch9073:    0073-net-bonding-fix-stopping-non-active-slaves.patch
Patch9074:    0074-net-bonding-fix-slave-stop-and-remove-on-port-close.patch
Patch9075:    0075-net-hns3-fix-order-of-clearing-imissed-register-in-P.patch
Patch9076:    0076-net-hns3-fix-MAC-and-queues-HW-statistics-overflow.patch
Patch9077:    0077-net-hns3-fix-pseudo-sharing-between-threads.patch
Patch9078:    0078-net-hns3-fix-mbuf-free-on-Tx-done-cleanup.patch
Patch9079:    0079-net-hns3-fix-RSS-disable.patch
Patch9080:    0080-net-hns3-fix-rollback-on-RSS-hash-update.patch
Patch9081:    0081-net-hns3-remove-redundant-RSS-tuple-field.patch
Patch9082:    0082-ethdev-fix-RSS-update-when-RSS-is-disabled.patch
Patch9083:    0083-net-hns3-remove-unnecessary-RSS-switch.patch
Patch9084:    0084-app-testpmd-check-statistics-query-before-printing.patch
Patch9085:    0085-app-testpmd-fix-MTU-verification.patch
Patch9086:    0086-app-testpmd-fix-port-status-of-bonding-slave-device.patch
Patch9087:    0087-ethdev-clarify-null-location-case-in-xstats-get.patch
Patch9088:    0088-ethdev-simplify-xstats-get-implementation.patch
Patch9089:    0089-net-hns3-fix-xstats-get-return-if-xstats-is-null.patch
Patch9090:    0090-net-ipn3ke-fix-xstats-get-return-if-xstats-is-null.patch
Patch9091:    0091-net-mvpp2-fix-xstats-get-return-if-xstats-is-null.patch
Patch9092:    0092-net-axgbe-fix-xstats-get-return-if-xstats-is-null.patch
Patch9093:    0093-ethdev-fix-memory-leak-in-xstats-telemetry.patch
Patch9094:    0094-ethdev-fix-possible-null-pointer-access.patch
Patch9095:    0095-net-cnxk-fix-possible-null-dereference-in-telemetry.patch
Patch9096:    0096-net-bonding-fix-mbuf-fast-free-usage.patch
Patch9097:    0097-ethdev-fix-port-state-when-stop.patch
Patch9098:    0098-ethdev-fix-port-close-in-secondary-process.patch
Patch9099:    0099-examples-dma-fix-MTU-configuration.patch
Patch9100:    0100-examples-dma-fix-Tx-drop-statistics.patch
Patch9101:    0101-examples-dma-add-force-minimal-copy-size-parameter.patch
Patch9102:    0102-dma-hisilicon-fix-index-returned-when-no-DMA-complet.patch
Patch9103:    0103-test-dma-check-index-when-no-DMA-completed.patch
Patch9104:    0104-dma-hisilicon-enhance-CQ-scan-robustness.patch
Patch9105:    0105-net-failsafe-fix-device-freeing.patch
Patch9106:    0106-net-tap-fix-device-freeing.patch
Patch9107:    0107-net-bonding-fix-RSS-inconsistent-between-bonded-and-.patch
Patch9108:    0108-app-test-fix-bonding-RSS-test-when-disable-RSS.patch
Patch9109:    0109-net-hns3-add-check-for-deferred-start-queue-when-rol.patch
Patch9110:    0110-net-hns3-remove-redundant-parentheses.patch
Patch9111:    0111-net-hns3-adjust-the-data-type-of-some-variables.patch
Patch9112:    0112-net-hns3-fix-an-unreasonable-memset.patch
Patch9113:    0113-net-hns3-remove-duplicate-definition.patch
Patch9114:    0114-net-hns3-fix-code-check-warning.patch
Patch9115:    0115-net-hns3-fix-return-value-for-unsupported-tuple.patch
Patch9116:    0116-net-hns3-modify-a-function-name.patch
Patch9117:    0117-net-hns3-unify-the-code-wrap-style.patch
Patch9118:    0118-net-hns3-fix-a-segfault-from-secondary-process.patch
Patch9119:    0119-net-hns3-fix-TM-capability-incorrectly-defined.patch
Patch9120:    0120-app-testpmd-add-help-messages-for-multi-process.patch
Patch9121:    0121-app-testpmd-fix-use-of-indirect-action-after-port-cl.patch
Patch9122:    0122-app-testpmd-fix-bonding-slave-devices-not-released.patch
Patch9123:    0123-secure-complilation-options-rpath.patch
Patch9124:    0124-reinit-support-return-ok.patch

Patch6001:    CVE-2021-3839.patch
Patch6002:    CVE-2022-0669.patch
Patch6003:    backport-0001-CVE-2022-2132.patch
Patch6004:    backport-0002-CVE-2022-2132.patch
Patch6005:    backport-CVE-2022-28199.patch

Patch9125:    0125-net-hns3-fix-link-status-capability-query-from-VF.patch
Patch9126:    0126-net-hns3-support-backplane-media-type.patch
Patch9127:    0127-net-hns3-cancel-heartbeat-alarm-when-VF-reset.patch
Patch9128:    0128-net-hns3-fix-PTP-interrupt-logging.patch
Patch9129:    0129-net-hns3-fix-statistics-locking.patch
Patch9130:    0130-net-hns3-fix-descriptors-check-with-SVE.patch
Patch9131:    0131-net-hns3-clean-some-functions.patch
Patch9132:    0132-net-hns3-delete-unused-code.patch
Patch9133:    0133-examples-dma-support-dequeue-when-no-packet-received.patch
Patch9134:    0134-net-hns3-add-dump-of-VF-VLAN-filter-modify-capabilit.patch
Patch9135:    0135-net-hns3-fix-Rx-with-PTP.patch
Patch9136:    0136-net-hns3-fix-crash-in-SVE-Tx.patch
Patch9137:    0137-net-hns3-fix-next-to-use-overflow-in-SVE-Tx.patch
Patch9138:    0138-net-hns3-fix-next-to-use-overflow-in-simple-Tx.patch
Patch9139:    0139-net-hns3-optimize-SVE-Tx-performance.patch
Patch9140:    0140-net-hns3-fix-crash-when-secondary-process-access-FW.patch
Patch9141:    0141-net-hns3-delete-unused-markup.patch
Patch9142:    0142-net-hns3-fix-clearing-hardware-MAC-statistics.patch
Patch9143:    0143-net-hns3-revert-Tx-performance-optimization.patch
Patch9144:    0144-net-hns3-fix-RSS-rule-restore.patch
Patch9145:    0145-net-hns3-fix-RSS-filter-restore.patch
Patch9146:    0146-net-hns3-fix-lock-protection-of-RSS-flow-rule.patch
Patch9147:    0147-net-hns3-fix-RSS-flow-rule-restore.patch
Patch9148:    0148-net-hns3-move-flow-direction-rule-recovery.patch
Patch9149:    0149-net-hns3-fix-restore-filter-function-input.patch
Patch9150:    0150-net-hns3-fix-build-with-gcov.patch
Patch9151:    0151-net-hns3-fix-packet-type-for-GENEVE.patch
Patch9152:    0152-net-hns3-remove-magic-numbers-for-MAC-address.patch
Patch9153:    0153-net-hns3-fix-code-check-warnings.patch
Patch9154:    0154-net-hns3-fix-header-files-includes.patch
Patch9155:    0155-net-hns3-remove-unused-structures.patch
Patch9156:    0156-net-hns3-rename-header-guards.patch
Patch9157:    0157-net-hns3-fix-IPv4-and-IPv6-RSS.patch
Patch9158:    0158-net-hns3-fix-types-in-IPv6-SCTP-fields.patch
Patch9159:    0159-net-hns3-fix-IPv4-RSS.patch
Patch9160:    0160-net-hns3-add-check-for-L3-and-L4-type.patch
Patch9161:    0161-net-hns3-revert-fix-mailbox-communication-with-HW.patch
Patch9162:    0162-net-hns3-fix-VF-mailbox-message-handling.patch
Patch9163:    0163-net-hns3-fix-minimum-Tx-frame-length.patch
Patch9164:    0164-ethdev-introduce-Rx-Tx-descriptor-dump-API.patch
Patch9165:    0165-net-hns3-support-Rx-Tx-descriptor-dump.patch
Patch9166:    0166-remove-unnecessary-null-checks.patch
Patch9167:    0167-ethdev-introduce-generic-dummy-packet-burst-function.patch
Patch9168:    0168-fix-spelling-in-comments-and-strings.patch
Patch9169:    0169-net-hns3-add-VLAN-filter-query-in-dump-file.patch
Patch9170:    0170-net-bonding-fix-array-overflow-in-Rx-burst.patch
Patch9171:    0171-net-bonding-fix-double-slave-link-status-query.patch
Patch9172:    0172-app-testpmd-fix-supported-RSS-offload-display.patch
Patch9173:    0173-app-testpmd-unify-name-of-L2-payload-offload.patch
Patch9174:    0174-app-testpmd-refactor-config-all-RSS-command.patch
Patch9175:    0175-app-testpmd-unify-RSS-types-display.patch
Patch9176:    0176-app-testpmd-compact-RSS-types-output.patch
Patch9177:    0177-app-testpmd-reorder-RSS-type-table.patch
Patch9178:    0178-app-testpmd-fix-RSS-types-display.patch
Patch9179:    0179-ethdev-support-telemetry-private-dump.patch
Patch9180:    0180-dmadev-add-telemetry.patch
Patch9181:    0181-dmadev-support-telemetry-dump-dmadev.patch
Patch9182:    0182-telemetry-add-missing-C-guards.patch
Patch9183:    0183-telemetry-limit-characters-allowed-in-dictionary-nam.patch
Patch9184:    0184-telemetry-fix-escaping-of-invalid-json-characters.patch
Patch9185:    0185-telemetry-add-escaping-of-strings-in-arrays.patch
Patch9186:    0186-telemetry-add-escaping-of-strings-in-dicts.patch
Patch9187:    0187-telemetry-limit-command-characters.patch
Patch9188:    0188-telemetry-eliminate-duplicate-code-for-json-output.patch
Patch9189:    0189-telemetry-make-help-command-more-helpful.patch

Patch6006:    backport-gro-fix-chain-index-for-more-than-2-packets.patch
Patch6007:    backport-gro-trim-tail-padding-bytes.patch
Patch6008:    backport-gro-check-payload-length-after-trim.patch

Patch9190:    0190-net-bonding-fix-Tx-hash-for-TCP.patch
Patch9191:    0191-net-bonding-add-link-speeds-configuration.patch
Patch9192:    0192-net-bonding-call-Tx-prepare-before-Tx-burst.patch
Patch9193:    0193-net-bonding-fix-MTU-set-for-slaves.patch
Patch9194:    0194-app-testpmd-remove-jumbo-offload-related-code.patch
Patch9195:    0195-app-testpmd-revert-MAC-update-in-checksum-forwarding.patch
Patch9196:    0196-net-bonding-fix-bond4-drop-valid-MAC-packets.patch
Patch9197:    0197-net-bonding-fix-slave-device-Rx-Tx-offload-configura.patch
Patch9198:    0198-app-testpmd-fix-MAC-header-in-csum-forward-engine.patch
Patch9199:    0199-app-testpmd-update-bond-port-configurations-when-add.patch
Patch9200:    0200-app-testpmd-fix-GENEVE-parsing-in-checksum-mode.patch
Patch9201:    0201-net-add-UDP-TCP-checksum-in-mbuf-segments.patch
Patch9202:    0202-app-testpmd-add-SW-L4-checksum-in-multi-segments.patch
Patch9203:    0203-app-testpmd-fix-L4-checksum-in-multi-segments.patch
Patch9204:    0204-net-bonding-fix-mbuf-fast-free-handling.patch
Patch9205:    0205-doc-fix-application-name-in-procinfo-guide.patch
Patch9206:    0206-doc-document-device-dump-in-procinfo-guide.patch
Patch9207:    0207-app-procinfo-remove-doxygen-comments.patch
Patch9208:    0208-app-procinfo-dump-DPDK-version.patch
Patch9209:    0209-app-procinfo-dump-firmware-version.patch
Patch9210:    0210-app-procinfo-dump-RSS-RETA.patch
Patch9211:    0211-app-procinfo-dump-module-EEPROM-info.patch
Patch9212:    0212-app-procinfo-add-burst-mode-to-Rx-Tx-queue-info.patch
Patch9213:    0213-app-procinfo-dump-detailed-info-for-Rx-Tx-descriptor.patch
Patch9214:    0214-dma-hisilicon-support-vchan-status-query.patch
Patch9215:    0215-kni-fix-build-with-Linux-5.18.patch
Patch9216:    0216-kni-use-dedicated-function-to-set-random-MAC-address.patch
Patch9217:    0217-kni-use-dedicated-function-to-set-MAC-address.patch
Patch9218:    0218-linux-igb_uio-fix-build-for-switch-fall-through.patch
Patch9219:    0219-linux-igb_uio-fix-build-with-kernel-5.18.patch
Patch9220:    0220-net-hns3-fix-inaccurate-RTC-time-to-read.patch
Patch9221:    0221-net-hns3-fix-log-about-indirection-table-size.patch
Patch9222:    0222-net-hns3-extract-common-function-to-query-device.patch
Patch9223:    0223-net-hns3-refactor-set-RSS-hash-algorithm-and-key-int.patch
Patch9224:    0224-net-hns3-fix-RSS-key-size-compatibility.patch
Patch9225:    0225-net-hns3-fix-clearing-RSS-configuration.patch
Patch9226:    0226-net-hns3-use-RSS-filter-list-to-check-duplicated-rul.patch
Patch9227:    0227-net-hns3-remove-useless-code-when-destroy-valid-RSS-.patch
Patch9228:    0228-net-hns3-fix-warning-on-flush-or-destroy-rule.patch
Patch9229:    0229-net-hns3-fix-config-struct-used-for-conversion.patch
Patch9230:    0230-net-hns3-fix-duplicate-RSS-rule-check.patch
Patch9231:    0231-net-hns3-fix-burst-mode-query-with-dummy-function.patch
Patch9232:    0232-net-hns3-add-debug-info-for-Rx-Tx-dummy-function.patch
Patch9233:    0233-net-hns3-remove-debug-condition-for-Tx-prepare.patch
Patch9234:    0234-net-hns3-separate-Tx-prepare-from-getting-Tx-functio.patch
Patch9235:    0235-net-hns3-make-getting-Tx-function-static.patch
Patch9236:    0236-net-hns3-extract-common-functions-to-set-Rx-Tx.patch
Patch9237:    0237-net-hns3-declare-flow-rule-keeping-capability.patch
Patch9238:    0238-app-testpmd-add-disable-flow-flush-option.patch
Patch9239:    0239-net-hns3-fix-possible-truncation-of-hash-key-when-co.patch
Patch9240:    0240-net-hns3-fix-possible-truncation-of-redirection-tabl.patch
Patch9241:    0241-net-hns3-use-hardware-config-to-report-hash-key.patch
Patch9242:    0242-net-hns3-use-hardware-config-to-report-hash-types.patch
Patch9243:    0243-net-hns3-use-hardware-config-to-report-redirection-t.patch
Patch9244:    0244-net-hns3-separate-setting-hash-algorithm.patch
Patch9245:    0245-net-hns3-separate-setting-hash-key.patch
Patch9246:    0246-net-hns3-separate-setting-redirection-table.patch
Patch9247:    0247-net-hns3-separate-setting-RSS-types.patch
Patch9248:    0248-net-hns3-separate-setting-and-clearing-RSS-rule.patch
Patch9249:    0249-net-hns3-use-new-RSS-rule-to-configure-hardware.patch
Patch9250:    0250-net-hns3-save-hash-algo-to-RSS-filter-list-node.patch
Patch9251:    0251-net-hns3-allow-adding-queue-buffer-size-hash-rule.patch
Patch9252:    0252-net-hns3-separate-flow-RSS-config-from-RSS-conf.patch
Patch9253:    0253-net-hns3-reimplement-hash-flow-function.patch
Patch9254:    0254-net-hns3-add-verification-of-RSS-types.patch
Patch9255:    0255-test-mbuf-fix-mbuf-reset-test.patch
Patch9256:    0256-examples-l3fwd-power-support-CPPC-cpufreq.patch
Patch9257:    0257-hinic-free-mbuf-use-rte_pktmbuf_free_seg.patch
Patch9258:    0258-net-bonding-support-private-dump-operation.patch
Patch9259:    0259-net-bonding-add-LACP-info-dump.patch
Patch9260:    0260-net-virtio-support-private-dump.patch
Patch9261:    0261-net-vhost-support-private-dump.patch
Patch9262:    0262-app-testpmd-show-private-info-in-port-info.patch
Patch9263:    0263-app-testpmd-display-RSS-hash-key-of-flow-rule.patch

Summary: Data Plane Development Kit core
Group: System Environment/Libraries
License: BSD and LGPLv2 and GPLv2

ExclusiveArch: i686 x86_64 aarch64

BuildRequires: meson ninja-build gcc diffutils python3-pyelftools
BuildRequires: kernel-devel numactl-devel
BuildRequires: libpcap libpcap-devel
BuildRequires: rdma-core-devel
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
meson build -Dplatform=generic -Dexamples=l3fwd-power,ethtool,l3fwd,kni,dma,ptpclient --default-library=shared
ninja -C build -v

#build gazelle-pdump/gazell-proc-info
cd build/app/dpdk-pdump.p
export GAZELLE_FLAGS="-lm -lpthread -lrt -lnuma"
export GAZELLE_LIBS="-lrte_pci -lrte_bus_pci -lrte_cmdline -lrte_hash -lrte_mempool -lrte_mempool_ring -lrte_timer -lrte_eal -lrte_gro -lrte_ring -lrte_mbuf -lrte_telemetry -lrte_kni -lrte_net_ixgbe -lrte_kvargs -lrte_net_hinic -lrte_net_i40e -lrte_net_virtio -lrte_bus_vdev -lrte_net -lrte_rcu -lrte_ethdev -lrte_pdump -lrte_bpf -lrte_security -lrte_cryptodev -lrte_net_pcap -lrte_metrics"
export SECURE_OPTIONS="-fstack-protector-strong -D_FORTIFY_SOURCE=2 -O2 -Wall -Wl,-z,relro,-z,now,-z,noexecstack -Wtrampolines -fPIE -pie -fPIC -g"
gcc -o gazelle-pdump ${GAZELLE_FLAGS} ${SOCURE_OPTIONS} -L../../drivers -L../../lib ${GAZELLE_LIBS} pdump_main.c.o
cd -
cd build/app/dpdk-proc-info.p
gcc -o gazelle-proc-info ${GAZELLE_FLAGS} ${SOCURE_OPTIONS} -L../../drivers -L../../lib ${GAZELLE_LIBS} proc-info_main.c.o

%install
DESTDIR=$RPM_BUILD_ROOT/ ninja install -C build

chrpath -d ./build/examples/dpdk-l3fwd
chrpath -d ./build/examples/dpdk-l3fwd-power
chrpath -d ./build/examples/dpdk-ethtool
chrpath -d ./build/examples/dpdk-kni
chrpath -d ./build/examples/dpdk-dma
chrpath -d ./build/examples/dpdk-ptpclient
chrpath -d ./build/app/dpdk-pdump.p/gazelle-pdump
chrpath -d ./build/app/dpdk-proc-info.p/gazelle-proc-info

cp ./build/examples/dpdk-l3fwd $RPM_BUILD_ROOT/usr/local/bin
cp ./build/examples/dpdk-l3fwd-power $RPM_BUILD_ROOT/usr/local/bin
cp ./build/examples/dpdk-ethtool $RPM_BUILD_ROOT/usr/local/bin
cp ./build/examples/dpdk-kni $RPM_BUILD_ROOT/usr/local/bin
cp ./build/examples/dpdk-dma $RPM_BUILD_ROOT/usr/local/bin
cp ./build/examples/dpdk-ptpclient $RPM_BUILD_ROOT/usr/local/bin
cp ./build/app/dpdk-pdump.p/gazelle-pdump $RPM_BUILD_ROOT/usr/local/bin
cp ./build/app/dpdk-proc-info.p/gazelle-proc-info $RPM_BUILD_ROOT/usr/local/bin

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
/usr/lib64/pkgconfig/libdpdk-libs.pc
/usr/lib64/pkgconfig/libdpdk.pc

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
/usr/local/bin/gazelle-pdump
/usr/local/bin/gazelle-proc-info

%post
/sbin/ldconfig
/usr/sbin/depmod

%postun
/sbin/ldconfig
/usr/sbin/depmod

%changelog
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
