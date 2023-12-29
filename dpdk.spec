Name: dpdk
Version: 21.11
Release: 63
Packager: packaging@6wind.com
URL: http://dpdk.org
%global source_version  21.11
Source: https://git.dpdk.org/dpdk/snapshot/%{name}-%{version}.tar.xz

# upstream patch number start from 6000.
# self developed patch number start from 9000
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

Patch9018:    0018-secure-complilation-options-rpath.patch
Patch9019:    0019-reinit-support-return-ok.patch

Patch6001:    CVE-2021-3839.patch
Patch6002:    CVE-2022-0669.patch
Patch6003:    backport-0001-CVE-2022-2132.patch
Patch6004:    backport-0002-CVE-2022-2132.patch
Patch6005:    backport-CVE-2022-28199.patch
Patch6006:    backport-gro-fix-chain-index-for-more-than-2-packets.patch
Patch6007:    backport-gro-trim-tail-padding-bytes.patch
Patch6008:    backport-gro-check-payload-length-after-trim.patch

Patch6018:    0018-net-bonding-fix-offloading-configuration.patch
Patch6019:    0019-net-hns3-fix-Rx-Tx-when-fast-path-operation-introduc.patch
Patch6020:    0020-net-hns3-fix-mailbox-wait-time-uninitialization.patch
Patch6021:    0021-net-hns3-fix-vector-burst-when-PTP-enable.patch
Patch6022:    0022-net-hns3-remove-unnecessary-assignment.patch
Patch6023:    0023-net-hns3-fix-using-enum-as-boolean.patch
Patch6024:    0024-net-hns3-extract-common-function-to-initialize-MAC-a.patch
Patch6025:    0025-net-hns3-make-control-plane-function-non-inline.patch
Patch6026:    0026-net-hns3-remove-unnecessary-blank-lines.patch
Patch6027:    0027-net-hns3-extract-reset-failure-handling-to-function.patch
Patch6028:    0028-net-hns3-remove-unused-variables.patch
Patch6029:    0029-net-hns3-remove-getting-number-of-queue-descriptors-.patch
Patch6030:    0030-net-hns3-remove-logging-memory-addresses.patch
Patch6031:    0031-net-hns3-extract-common-function-to-obtain-revision-.patch
Patch6032:    0032-net-hns3-replace-single-line-functions.patch
Patch6033:    0033-net-hns3-remove-non-re-entrant-strerror-call.patch
Patch6034:    0034-net-hns3-rename-function.patch
Patch6035:    0035-net-hns3-extract-functions-to-create-RSS-and-FDIR-fl.patch
Patch6036:    0036-net-hns3-support-indirect-counter-flow-action.patch
Patch6037:    0037-net-hns3-fix-max-packet-size-rollback-in-PF.patch
Patch6038:    0038-net-hns3-fix-RSS-key-with-null.patch
Patch6039:    0039-net-hns3-fix-insecure-way-to-query-MAC-statistics.patch
Patch6040:    0040-net-hns3-fix-double-decrement-of-secondary-count.patch
Patch6041:    0041-net-hns3-fix-operating-queue-when-TCAM-table-is-inva.patch
Patch6042:    0042-net-hns3-delete-duplicated-RSS-type.patch
Patch6043:    0043-net-bonding-fix-promiscuous-and-allmulticast-state.patch
Patch6044:    0044-net-bonding-fix-reference-count-on-mbufs.patch
Patch6045:    0045-app-testpmd-fix-bonding-mode-set.patch
Patch6046:    0046-ethdev-introduce-dump-API.patch
Patch6047:    0047-app-procinfo-add-device-private-info-dump.patch
Patch6048:    0048-net-hns3-dump-device-basic-info.patch
Patch6049:    0049-net-hns3-dump-device-feature-capability.patch
Patch6050:    0050-net-hns3-dump-device-MAC-info.patch
Patch6051:    0051-net-hns3-dump-queue-info.patch
Patch6052:    0052-net-hns3-dump-VLAN-configuration-info.patch
Patch6053:    0053-net-hns3-dump-flow-director-basic-info.patch
Patch6054:    0054-net-hns3-dump-TM-configuration-info.patch
Patch6055:    0055-net-hns3-dump-flow-control-info.patch
Patch6056:    0056-net-hns3-change-dump-file-name.patch
Patch6057:    0057-net-hns3-fix-code-check-for-dump.patch
Patch6058:    0058-ethdev-fix-ethdev-version-map.patch
Patch6059:    0059-net-hns3-delete-simple-bd-cap.patch
Patch6060:    0060-net-hns3-fix-TM-info-dump.patch
Patch6061:    0061-dma-hisilicon-support-Kunpeng-930.patch
Patch6062:    0062-dma-hisilicon-support-error-handling-with-Kunpeng-93.patch
Patch6063:    0063-dma-hisilicon-support-registers-dump-for-Kunpeng-930.patch
Patch6064:    0064-dma-hisilicon-add-queue-full-statistics.patch
Patch6065:    0065-dma-hisilicon-use-common-PCI-device-naming.patch
Patch6066:    0066-app-testpmd-check-starting-port-is-not-in-bonding.patch
Patch6067:    0067-examples-vhost-remove-DMA-type-option-help-info.patch
Patch6068:    0068-kni-fix-freeing-order-in-device-release.patch
Patch6069:    0069-net-hns3-remove-duplicate-macro-definition.patch
Patch6070:    0070-net-hns3-fix-RSS-TC-mode-entry.patch
Patch6071:    0071-net-hns3-fix-VF-RSS-TC-mode-entry.patch
Patch6072:    0072-net-hns3-increase-time-waiting-for-PF-reset-completi.patch
Patch6073:    0073-net-bonding-fix-stopping-non-active-slaves.patch
Patch6074:    0074-net-bonding-fix-slave-stop-and-remove-on-port-close.patch
Patch6075:    0075-net-hns3-fix-order-of-clearing-imissed-register-in-P.patch
Patch6076:    0076-net-hns3-fix-MAC-and-queues-HW-statistics-overflow.patch
Patch6077:    0077-net-hns3-fix-pseudo-sharing-between-threads.patch
Patch6078:    0078-net-hns3-fix-mbuf-free-on-Tx-done-cleanup.patch
Patch6079:    0079-net-hns3-fix-RSS-disable.patch
Patch6080:    0080-net-hns3-fix-rollback-on-RSS-hash-update.patch
Patch6081:    0081-net-hns3-remove-redundant-RSS-tuple-field.patch
Patch6082:    0082-ethdev-fix-RSS-update-when-RSS-is-disabled.patch
Patch6083:    0083-net-hns3-remove-unnecessary-RSS-switch.patch
Patch6084:    0084-app-testpmd-check-statistics-query-before-printing.patch
Patch6085:    0085-app-testpmd-fix-MTU-verification.patch
Patch6086:    0086-app-testpmd-fix-port-status-of-bonding-slave-device.patch
Patch6087:    0087-ethdev-clarify-null-location-case-in-xstats-get.patch
Patch6088:    0088-ethdev-simplify-xstats-get-implementation.patch
Patch6089:    0089-net-hns3-fix-xstats-get-return-if-xstats-is-null.patch
Patch6090:    0090-net-ipn3ke-fix-xstats-get-return-if-xstats-is-null.patch
Patch6091:    0091-net-mvpp2-fix-xstats-get-return-if-xstats-is-null.patch
Patch6092:    0092-net-axgbe-fix-xstats-get-return-if-xstats-is-null.patch
Patch6093:    0093-ethdev-fix-memory-leak-in-xstats-telemetry.patch
Patch6094:    0094-ethdev-fix-possible-null-pointer-access.patch
Patch6095:    0095-net-cnxk-fix-possible-null-dereference-in-telemetry.patch
Patch6096:    0096-net-bonding-fix-mbuf-fast-free-usage.patch
Patch6097:    0097-ethdev-fix-port-state-when-stop.patch
Patch6098:    0098-ethdev-fix-port-close-in-secondary-process.patch
Patch6099:    0099-examples-dma-fix-MTU-configuration.patch
Patch6100:    0100-examples-dma-fix-Tx-drop-statistics.patch
Patch6101:    0101-examples-dma-add-force-minimal-copy-size-parameter.patch
Patch6102:    0102-dma-hisilicon-fix-index-returned-when-no-DMA-complet.patch
Patch6103:    0103-test-dma-check-index-when-no-DMA-completed.patch
Patch6104:    0104-dma-hisilicon-enhance-CQ-scan-robustness.patch
Patch6105:    0105-net-failsafe-fix-device-freeing.patch
Patch6106:    0106-net-tap-fix-device-freeing.patch
Patch6107:    0107-net-bonding-fix-RSS-inconsistent-between-bonded-and-.patch
Patch6108:    0108-app-test-fix-bonding-RSS-test-when-disable-RSS.patch
Patch6109:    0109-net-hns3-add-check-for-deferred-start-queue-when-rol.patch
Patch6110:    0110-net-hns3-remove-redundant-parentheses.patch
Patch6111:    0111-net-hns3-adjust-the-data-type-of-some-variables.patch
Patch6112:    0112-net-hns3-fix-an-unreasonable-memset.patch
Patch6113:    0113-net-hns3-remove-duplicate-definition.patch
Patch6114:    0114-net-hns3-fix-code-check-warning.patch
Patch6115:    0115-net-hns3-fix-return-value-for-unsupported-tuple.patch
Patch6116:    0116-net-hns3-modify-a-function-name.patch
Patch6117:    0117-net-hns3-unify-the-code-wrap-style.patch
Patch6118:    0118-net-hns3-fix-a-segfault-from-secondary-process.patch
Patch6119:    0119-net-hns3-fix-TM-capability-incorrectly-defined.patch
Patch6120:    0120-app-testpmd-add-help-messages-for-multi-process.patch
Patch6121:    0121-app-testpmd-fix-use-of-indirect-action-after-port-cl.patch
Patch6122:    0122-app-testpmd-fix-bonding-slave-devices-not-released.patch

Patch6125:    0125-net-hns3-fix-link-status-capability-query-from-VF.patch
Patch6126:    0126-net-hns3-support-backplane-media-type.patch
Patch6127:    0127-net-hns3-cancel-heartbeat-alarm-when-VF-reset.patch
Patch6128:    0128-net-hns3-fix-PTP-interrupt-logging.patch
Patch6129:    0129-net-hns3-fix-statistics-locking.patch
Patch6130:    0130-net-hns3-fix-descriptors-check-with-SVE.patch
Patch6131:    0131-net-hns3-clean-some-functions.patch
Patch6132:    0132-net-hns3-delete-unused-code.patch
Patch6133:    0133-examples-dma-support-dequeue-when-no-packet-received.patch
Patch6134:    0134-net-hns3-add-dump-of-VF-VLAN-filter-modify-capabilit.patch
Patch6135:    0135-net-hns3-fix-Rx-with-PTP.patch
Patch6136:    0136-net-hns3-fix-crash-in-SVE-Tx.patch
Patch6137:    0137-net-hns3-fix-next-to-use-overflow-in-SVE-Tx.patch
Patch6138:    0138-net-hns3-fix-next-to-use-overflow-in-simple-Tx.patch
Patch6139:    0139-net-hns3-optimize-SVE-Tx-performance.patch
Patch6140:    0140-net-hns3-fix-crash-when-secondary-process-access-FW.patch
Patch6141:    0141-net-hns3-delete-unused-markup.patch
Patch6142:    0142-net-hns3-fix-clearing-hardware-MAC-statistics.patch
Patch6143:    0143-net-hns3-revert-Tx-performance-optimization.patch
Patch6144:    0144-net-hns3-fix-RSS-rule-restore.patch
Patch6145:    0145-net-hns3-fix-RSS-filter-restore.patch
Patch6146:    0146-net-hns3-fix-lock-protection-of-RSS-flow-rule.patch
Patch6147:    0147-net-hns3-fix-RSS-flow-rule-restore.patch
Patch6148:    0148-net-hns3-move-flow-direction-rule-recovery.patch
Patch6149:    0149-net-hns3-fix-restore-filter-function-input.patch
Patch6150:    0150-net-hns3-fix-build-with-gcov.patch
Patch6151:    0151-net-hns3-fix-packet-type-for-GENEVE.patch
Patch6152:    0152-net-hns3-remove-magic-numbers-for-MAC-address.patch
Patch6153:    0153-net-hns3-fix-code-check-warnings.patch
Patch6154:    0154-net-hns3-fix-header-files-includes.patch
Patch6155:    0155-net-hns3-remove-unused-structures.patch
Patch6156:    0156-net-hns3-rename-header-guards.patch
Patch6157:    0157-net-hns3-fix-IPv4-and-IPv6-RSS.patch
Patch6158:    0158-net-hns3-fix-types-in-IPv6-SCTP-fields.patch
Patch6159:    0159-net-hns3-fix-IPv4-RSS.patch
Patch6160:    0160-net-hns3-add-check-for-L3-and-L4-type.patch
Patch6161:    0161-net-hns3-revert-fix-mailbox-communication-with-HW.patch
Patch6162:    0162-net-hns3-fix-VF-mailbox-message-handling.patch
Patch6163:    0163-net-hns3-fix-minimum-Tx-frame-length.patch
Patch6164:    0164-ethdev-introduce-Rx-Tx-descriptor-dump-API.patch
Patch6165:    0165-net-hns3-support-Rx-Tx-descriptor-dump.patch
Patch6166:    0166-remove-unnecessary-null-checks.patch
Patch6167:    0167-ethdev-introduce-generic-dummy-packet-burst-function.patch
Patch6168:    0168-fix-spelling-in-comments-and-strings.patch
Patch6169:    0169-net-hns3-add-VLAN-filter-query-in-dump-file.patch
Patch6170:    0170-net-bonding-fix-array-overflow-in-Rx-burst.patch
Patch6171:    0171-net-bonding-fix-double-slave-link-status-query.patch
Patch6172:    0172-app-testpmd-fix-supported-RSS-offload-display.patch
Patch6173:    0173-app-testpmd-unify-name-of-L2-payload-offload.patch
Patch6174:    0174-app-testpmd-refactor-config-all-RSS-command.patch
Patch6175:    0175-app-testpmd-unify-RSS-types-display.patch
Patch6176:    0176-app-testpmd-compact-RSS-types-output.patch
Patch6177:    0177-app-testpmd-reorder-RSS-type-table.patch
Patch6178:    0178-app-testpmd-fix-RSS-types-display.patch
Patch6179:    0179-ethdev-support-telemetry-private-dump.patch
Patch6180:    0180-dmadev-add-telemetry.patch
Patch6181:    0181-dmadev-support-telemetry-dump-dmadev.patch
Patch6182:    0182-telemetry-add-missing-C-guards.patch
Patch6183:    0183-telemetry-limit-characters-allowed-in-dictionary-nam.patch
Patch6184:    0184-telemetry-fix-escaping-of-invalid-json-characters.patch
Patch6185:    0185-telemetry-add-escaping-of-strings-in-arrays.patch
Patch6186:    0186-telemetry-add-escaping-of-strings-in-dicts.patch
Patch6187:    0187-telemetry-limit-command-characters.patch
Patch6188:    0188-telemetry-eliminate-duplicate-code-for-json-output.patch
Patch6189:    0189-telemetry-make-help-command-more-helpful.patch

Patch6190:    0190-net-bonding-fix-Tx-hash-for-TCP.patch
Patch6191:    0191-net-bonding-add-link-speeds-configuration.patch
Patch6192:    0192-net-bonding-call-Tx-prepare-before-Tx-burst.patch
Patch6193:    0193-net-bonding-fix-MTU-set-for-slaves.patch
Patch6194:    0194-app-testpmd-remove-jumbo-offload-related-code.patch
Patch6195:    0195-app-testpmd-revert-MAC-update-in-checksum-forwarding.patch
Patch6196:    0196-net-bonding-fix-bond4-drop-valid-MAC-packets.patch
Patch6197:    0197-net-bonding-fix-slave-device-Rx-Tx-offload-configura.patch
Patch6198:    0198-app-testpmd-fix-MAC-header-in-csum-forward-engine.patch
Patch6199:    0199-app-testpmd-update-bond-port-configurations-when-add.patch
Patch6200:    0200-app-testpmd-fix-GENEVE-parsing-in-checksum-mode.patch
Patch6201:    0201-net-add-UDP-TCP-checksum-in-mbuf-segments.patch
Patch6202:    0202-app-testpmd-add-SW-L4-checksum-in-multi-segments.patch
Patch6203:    0203-app-testpmd-fix-L4-checksum-in-multi-segments.patch
Patch6204:    0204-net-bonding-fix-mbuf-fast-free-handling.patch
Patch6205:    0205-doc-fix-application-name-in-procinfo-guide.patch
Patch6206:    0206-doc-document-device-dump-in-procinfo-guide.patch
Patch6207:    0207-app-procinfo-remove-doxygen-comments.patch
Patch6208:    0208-app-procinfo-dump-DPDK-version.patch
Patch6209:    0209-app-procinfo-dump-firmware-version.patch
Patch6210:    0210-app-procinfo-dump-RSS-RETA.patch
Patch6211:    0211-app-procinfo-dump-module-EEPROM-info.patch
Patch6212:    0212-app-procinfo-add-burst-mode-to-Rx-Tx-queue-info.patch
Patch6213:    0213-app-procinfo-dump-detailed-info-for-Rx-Tx-descriptor.patch
Patch6214:    0214-dma-hisilicon-support-vchan-status-query.patch
Patch6215:    0215-kni-fix-build-with-Linux-5.18.patch
Patch6216:    0216-kni-use-dedicated-function-to-set-random-MAC-address.patch
Patch6217:    0217-kni-use-dedicated-function-to-set-MAC-address.patch
Patch6218:    0218-linux-igb_uio-fix-build-for-switch-fall-through.patch
Patch6219:    0219-linux-igb_uio-fix-build-with-kernel-5.18.patch
Patch6220:    0220-net-hns3-fix-inaccurate-RTC-time-to-read.patch
Patch6221:    0221-net-hns3-fix-log-about-indirection-table-size.patch
Patch6222:    0222-net-hns3-extract-common-function-to-query-device.patch
Patch6223:    0223-net-hns3-refactor-set-RSS-hash-algorithm-and-key-int.patch
Patch6224:    0224-net-hns3-fix-RSS-key-size-compatibility.patch
Patch6225:    0225-net-hns3-fix-clearing-RSS-configuration.patch
Patch6226:    0226-net-hns3-use-RSS-filter-list-to-check-duplicated-rul.patch
Patch6227:    0227-net-hns3-remove-useless-code-when-destroy-valid-RSS-.patch
Patch6228:    0228-net-hns3-fix-warning-on-flush-or-destroy-rule.patch
Patch6229:    0229-net-hns3-fix-config-struct-used-for-conversion.patch
Patch6230:    0230-net-hns3-fix-duplicate-RSS-rule-check.patch
Patch6231:    0231-net-hns3-fix-burst-mode-query-with-dummy-function.patch
Patch6232:    0232-net-hns3-add-debug-info-for-Rx-Tx-dummy-function.patch
Patch6233:    0233-net-hns3-remove-debug-condition-for-Tx-prepare.patch
Patch6234:    0234-net-hns3-separate-Tx-prepare-from-getting-Tx-functio.patch
Patch6235:    0235-net-hns3-make-getting-Tx-function-static.patch
Patch6236:    0236-net-hns3-extract-common-functions-to-set-Rx-Tx.patch
Patch6237:    0237-net-hns3-declare-flow-rule-keeping-capability.patch
Patch6238:    0238-app-testpmd-add-disable-flow-flush-option.patch
Patch6239:    0239-net-hns3-fix-possible-truncation-of-hash-key-when-co.patch
Patch6240:    0240-net-hns3-fix-possible-truncation-of-redirection-tabl.patch
Patch6241:    0241-net-hns3-use-hardware-config-to-report-hash-key.patch
Patch6242:    0242-net-hns3-use-hardware-config-to-report-hash-types.patch
Patch6243:    0243-net-hns3-use-hardware-config-to-report-redirection-t.patch
Patch6244:    0244-net-hns3-separate-setting-hash-algorithm.patch
Patch6245:    0245-net-hns3-separate-setting-hash-key.patch
Patch6246:    0246-net-hns3-separate-setting-redirection-table.patch
Patch6247:    0247-net-hns3-separate-setting-RSS-types.patch
Patch6248:    0248-net-hns3-separate-setting-and-clearing-RSS-rule.patch
Patch6249:    0249-net-hns3-use-new-RSS-rule-to-configure-hardware.patch
Patch6250:    0250-net-hns3-save-hash-algo-to-RSS-filter-list-node.patch
Patch6251:    0251-net-hns3-allow-adding-queue-buffer-size-hash-rule.patch
Patch6252:    0252-net-hns3-separate-flow-RSS-config-from-RSS-conf.patch
Patch6253:    0253-net-hns3-reimplement-hash-flow-function.patch
Patch6254:    0254-net-hns3-add-verification-of-RSS-types.patch
Patch6255:    0255-test-mbuf-fix-mbuf-reset-test.patch
Patch6256:    0256-examples-l3fwd-power-support-CPPC-cpufreq.patch
Patch6257:    0257-hinic-free-mbuf-use-rte_pktmbuf_free_seg.patch
Patch6258:    0258-net-bonding-support-private-dump-operation.patch
Patch6259:    0259-net-bonding-add-LACP-info-dump.patch
Patch6260:    0260-net-virtio-support-private-dump.patch
Patch6261:    0261-net-vhost-support-private-dump.patch
Patch6262:    0262-app-testpmd-show-private-info-in-port-info.patch
Patch6263:    0263-app-testpmd-display-RSS-hash-key-of-flow-rule.patch
Patch6264:    0264-ethdev-fix-Rx-queue-telemetry-memory-leak-on-failure.patch
Patch6265:    0265-ethdev-fix-MAC-address-in-telemetry-device-info.patch
Patch6266:    0266-eventdev-eth_rx-fix-telemetry-Rx-stats-reset.patch
Patch6267:    0267-test-telemetry_data-refactor-for-maintainability.patch
Patch6268:    0268-test-telemetry_data-add-test-cases-for-character-esc.patch
Patch6269:    0269-usertools-telemetry-add-JSON-pretty-print.patch
Patch6270:    0270-telemetry-move-include-after-guard.patch
Patch6271:    0271-ethdev-fix-telemetry-data-truncation.patch
Patch6272:    0272-mempool-fix-telemetry-data-truncation.patch
Patch6273:    0273-cryptodev-fix-telemetry-data-truncation.patch
Patch6274:    0274-mem-fix-telemetry-data-truncation.patch
Patch6275:    0275-telemetry-support-adding-integer-as-hexadecimal.patch
Patch6276:    0276-ethdev-get-capabilities-from-telemetry-in-hexadecima.patch
Patch6277:    0277-mem-fix-hugepage-info-mapping.patch
Patch6278:    0278-raw-ifpga-base-fix-init-with-multi-process.patch
Patch6279:    0279-compressdev-fix-empty-devargs-parsing.patch
Patch6280:    0280-cryptodev-fix-empty-devargs-parsing.patch
Patch6281:    0281-net-hns3-fix-empty-devargs-parsing.patch
Patch6282:    0282-net-virtio-fix-empty-devargs-parsing.patch
Patch6283:    0283-dma-skeleton-fix-empty-devargs-parsing.patch
Patch6284:    0284-raw-skeleton-fix-empty-devargs-parsing.patch
Patch6285:    0285-net-hns3-simplify-hardware-checksum-offloading.patch
Patch6286:    0286-net-hns3-support-dump-media-type.patch
Patch6287:    0287-ethdev-fix-one-address-occupies-two-entries-in-MAC-a.patch
Patch6288:    0288-net-hns3-fix-never-set-MAC-flow-control.patch
Patch6289:    0289-net-hns3-add-flow-control-autoneg-for-fiber-port.patch
Patch6290:    0290-net-hns3-fix-variable-type-mismatch.patch
Patch6291:    0291-net-hns3-fix-Rx-multiple-firmware-reset-interrupts.patch
Patch6292:    0292-net-hns3-add-Tx-Rx-descriptor-logs.patch
Patch6293:    0293-net-hns3-fix-FEC-mode-for-200G-ports.patch
Patch6294:    0294-net-hns3-fix-FEC-mode-check-error.patch
Patch6295:    0295-net-hns3-fix-missing-FEC-capability.patch
Patch6296:    0296-ethdev-introduce-low-latency-RS-FEC.patch
Patch6297:    0297-app-testpmd-add-setting-and-querying-of-LLRS-FEC-mod.patch
Patch6298:    0298-net-hns3-add-LLRS-FEC-mode-support-for-200G-ports.patch
Patch6299:    0299-net-hns3-get-current-FEC-capability-from-firmware.patch
Patch6300:    0300-net-hns3-fix-RTC-time-on-initialization.patch
Patch6301:    0301-net-hns3-fix-RTC-time-after-reset.patch
Patch6302:    0302-net-hns3-uninitialize-PTP.patch
Patch6303:    0303-net-hns3-extract-PTP-to-its-own-header-file.patch
Patch6304:    0304-net-hns3-fix-mbuf-leakage-when-RxQ-started-during-re.patch
Patch6305:    0305-net-hns3-fix-mbuf-leakage-when-RxQ-started-after-res.patch
Patch6306:    0306-net-hns3-fix-device-start-return-value.patch
Patch6307:    0307-net-hns3-fix-uninitialized-variable.patch
Patch6308:    0308-net-hns3-refactor-code.patch
Patch6309:    0309-net-hns3-fix-inaccurate-log.patch
Patch6310:    0310-net-hns3-fix-redundant-line-break-in-log.patch
Patch6311:    0311-ethdev-add-API-to-check-if-queue-is-valid.patch
Patch6312:    0312-app-testpmd-fix-segment-fault-with-invalid-queue-ID.patch
Patch6313:    0313-net-hns3-fix-IMP-reset-trigger.patch
Patch6314:    0314-net-ixgbe-add-proper-memory-barriers-in-Rx.patch

Patch9020:    0020-pdump-fix-pcap_dump-coredump-caused-by-incorrect-pkt_len.patch
Patch9021:    0021-gro-fix-gro-with-tcp-push-flag.patch
Patch9022:    0022-eal-loongarch-support-LoongArch-architecture.patch
Patch9023:    0023-example-l3fwd-masking-wrong-warning-array-subscript-.patch

Patch6315:    0315-net-cnxk-fix-build-with-GCC-12.patch
Patch6316:    0316-net-cnxk-fix-build-with-optimization.patch
Patch6317:    0317-crypto-ipsec_mb-fix-build-with-GCC-12.patch
Patch6318:    0318-net-ena-fix-build-with-GCC-12.patch
Patch6319:    0319-net-enetfec-fix-build-with-GCC-12.patch
Patch6320:    0320-net-ice-fix-build-with-GCC-12.patch
Patch6321:    0321-vdpa-ifc-fix-build-with-GCC-12.patch
Patch6322:    0322-app-flow-perf-fix-build-with-GCC-12.patch
Patch6323:    0323-common-cpt-fix-build-with-GCC-12.patch
Patch6324:    0324-crypto-cnxk-fix-build-with-GCC-12.patch
Patch6325:    0325-test-ipsec-fix-build-with-GCC-12.patch
Patch6326:    0326-vhost-crypto-fix-build-with-GCC-12.patch
Patch6327:    0327-vhost-crypto-fix-descriptor-processing.patch
Patch6328:    0328-net-ice-base-fix-build-with-GCC-12.patch
Patch6329:    0329-net-qede-fix-build-with-GCC-12.patch
Patch6330:    0330-examples-performance-thread-fix-build-with-GCC-12.patch
Patch6331:    0331-net-mvneta-fix-build-with-GCC-12.patch
Patch6332:    0332-test-ipsec-fix-build-with-GCC-12.patch
Patch6333:    0333-ipsec-fix-build-with-GCC-12.patch
Patch6334:    0334-crypto-qat-fix-build-with-GCC-12.patch
Patch6335:    0335-vhost-fix-build-with-GCC-12.patch
Patch6336:    0336-net-i40e-fix-build-with-MinGW-GCC-12.patch
Patch6337:    0337-net-qede-base-fix-32-bit-build-with-GCC-12.patch
Patch6338:    0338-hash-fix-GFNI-implementation-build-with-GCC-12.patch
Patch6339:    0339-examples-cmdline-fix-build-with-GCC-12.patch
Patch6340:    0340-net-mlx5-fix-build-with-GCC-12-and-ASan.patch
Patch6341:    0341-pdump-fix-build-with-GCC-12.patch
Patch6342:    0342-net-cxgbe-fix-dangling-pointer-by-mailbox-access-rew.patch
Patch6343:    0343-kni-fix-build-with-Linux-6.3.patch
Patch6344:    0344-kni-fix-build-with-Linux-6.5.patch
Patch6345:    0345-doc-unify-sections-of-networking-drivers-guide.patch
Patch6346:    0346-net-hns3-delete-duplicate-macro-definition.patch
Patch6347:    0347-net-hns3-add-FDIR-VLAN-match-mode-runtime-config.patch
Patch6348:    0348-doc-fix-kernel-patch-link-in-hns3-guide.patch
Patch6349:    0349-doc-fix-syntax-in-hns3-guide.patch
Patch6350:    0350-doc-fix-number-of-leading-spaces-in-hns3-guide.patch
Patch6351:    0351-config-arm-add-HiSilicon-HIP10.patch
Patch6352:    0352-net-hns3-fix-non-zero-weight-for-disabled-TC.patch
Patch6353:    0353-net-hns3-fix-index-to-look-up-table-in-NEON-Rx.patch
Patch6354:    0354-net-hns3-fix-VF-default-MAC-modified-when-set-failed.patch
Patch6355:    0355-net-hns3-fix-error-code-for-multicast-resource.patch
Patch6356:    0356-net-hns3-fix-flushing-multicast-MAC-address.patch
Patch6357:    0357-net-hns3-fix-traffic-management-thread-safety.patch
Patch6358:    0358-net-hns3-fix-traffic-management-dump-text-alignment.patch
Patch6359:    0359-net-hns3-fix-order-in-NEON-Rx.patch
Patch6360:    0360-net-hns3-optimize-free-mbuf-for-SVE-Tx.patch
Patch6361:    0361-net-hns3-optimize-rearm-mbuf-for-SVE-Rx.patch
Patch6362:    0362-net-hns3-optimize-SVE-Rx-performance.patch
Patch6363:    0363-app-testpmd-fix-multicast-address-pool-leak.patch
Patch6364:    0364-app-testpmd-fix-help-string.patch
Patch6365:    0365-app-testpmd-add-command-to-flush-multicast-MAC-addre.patch
Patch6366:    0366-maintainers-update-for-hns3-driver.patch
Patch6367:    0367-telemetry-fix-repeat-display-when-callback-don-t-init-dict.patch
Patch6368:    0368-net-hns3-fix-build-warning.patch
Patch6369:    0369-net-hns3-fix-typo-in-function-name.patch
Patch6370:    0370-net-hns3-fix-unchecked-Rx-free-threshold.patch
Patch6371:    0371-net-hns3-fix-crash-for-NEON-and-SVE.patch
Patch6372:    0372-net-hns3-fix-double-stats-for-IMP-and-global-reset.patch
Patch6373:    0373-net-hns3-remove-reset-log-in-secondary.patch
Patch6374:    0374-net-hns3-fix-multiple-reset-detected-log.patch
Patch6375:    0375-net-hns3-fix-IMP-or-global-reset.patch
Patch6376:    0376-net-hns3-refactor-interrupt-state-query.patch
Patch6377:    0377-app-testpmd-ease-configuring-all-offloads.patch
Patch6378:    0378-net-hns3-fix-setting-DCB-capability.patch
Patch6379:    0379-net-hns3-fix-LRO-offload-to-report.patch
Patch6380:    0380-net-hns3-fix-some-return-values.patch
Patch6381:    0381-net-hns3-fix-some-error-logs.patch
Patch6382:    0382-net-hns3-keep-set-get-algo-key-functions-local.patch
Patch6383:    0383-net-hns3-fix-uninitialized-hash-algo-value.patch
Patch6384:    0384-ethdev-clarify-RSS-related-fields-usage.patch
Patch6385:    0385-ethdev-set-and-query-RSS-hash-algorithm.patch
Patch6386:    0386-net-hns3-report-RSS-hash-algorithms-capability.patch
Patch6387:    0387-net-hns3-support-setting-and-querying-RSS-hash-function.patch
Patch6388:    0388-app-procinfo-fix-RSS-info.patch
Patch6389:    0389-app-procinfo-adjust-format-of-RSS-info.patch
Patch6390:    0390-ethdev-get-RSS-algorithm-names.patch
Patch6391:    0391-app-procinfo-show-RSS-hash-algorithm.patch
Patch6392:    0392-ethdev-add-maximum-Rx-buffer-size.patch
Patch6393:    0393-net-hns3-report-maximum-buffer-size.patch
Patch6394:    0394-net-hns3-fix-mailbox-sync.patch

Patch1000:    1000-add-sw_64-support-not-upstream-modified.patch
Patch1001:    1001-add-sw_64-support-not-upstream-new.patch

Patch6395:    0395-net-hns3-fix-ignored-reset-event.patch
Patch6396:    0396-net-hns3-fix-reset-event-status.patch
Patch6397:    0397-net-hns3-fix-VF-reset-handler-interruption.patch
Patch6398:    0398-app-testpmd-remove-useless-check-in-TSO-command.patch
Patch6399:    0399-app-testpmd-fix-tunnel-TSO-capability-check.patch
Patch6400:    0400-app-testpmd-add-explicit-check-for-tunnel-TSO.patch
Patch6401:    0401-app-testpmd-fix-tunnel-TSO-configuration.patch
Patch6402:    0402-app-testpmd-allow-offload-config-for-all-ports.patch
Patch6403:    0403-app-testpmd-fix-Tx-offload-command.patch
Patch6404:    0404-app-testpmd-check-port-and-queue-Rx-Tx-offloads.patch
Patch6405:    0405-doc-fix-hns3-build-option-about-max-queue-number.patch
Patch6406:    0406-doc-update-features-in-hns3-guide.patch
Patch6407:    0407-doc-fix-RSS-flow-description-in-hns3-guide.patch
Patch6408:    0408-doc-fix-description-of-RSS-features.patch
Patch6409:    0409-ethdev-add-new-API-to-get-RSS-hash-algorithm-by-name.patch
Patch6410:    0410-app-testpmd-support-set-RSS-hash-algorithm.patch
Patch6411:    0411-net-hns3-refactor-VF-mailbox-message-struct.patch
Patch6412:    0412-net-hns3-refactor-PF-mailbox-message-struct.patch
Patch6413:    0413-net-hns3-refactor-send-mailbox-function.patch
Patch6414:    0414-net-hns3-refactor-handle-mailbox-function.patch
Patch6415:    0415-net-hns3-fix-VF-multiple-count-on-one-reset.patch
Patch6416:    0416-net-hns3-fix-disable-command-with-firmware.patch
Patch6417:    0417-net-hns3-fix-reset-level-comparison.patch
Patch6418:    0418-net-hns3-don-t-support-QinQ-insert-for-VF.patch

Summary: Data Plane Development Kit core
Group: System Environment/Libraries
License: BSD and LGPLv2 and GPLv2

ExclusiveArch: i686 x86_64 aarch64 loongarch64 sw_64

BuildRequires: meson ninja-build gcc diffutils python3-pyelftools
BuildRequires: kernel-devel numactl-devel
BuildRequires: libpcap libpcap-devel
BuildRequires: rdma-core-devel
BuildRequires: uname-build-checks
BuildRequires: chrpath
BuildRequires: groff-base
BuildRequires: libibverbs

%define kern_devel_ver %(uname -r)
%define arch_type %(uname -m)

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
%ifarch sw_64
meson build -Dplatform=generic -Dexamples=l3fwd-power,ethtool,kni,dma,ptpclient
%else
meson build -Dplatform=generic -Dexamples=l3fwd-power,ethtool,l3fwd,kni,dma,ptpclient
%endif
ninja -C build -v

#build gazelle-pdump
cd build/app/dpdk-pdump.p
export GAZELLE_FLAGS="-lm -lpthread -lrt -lnuma"
# Remove linking to i40e driver for LoongArch because it was not supported in this version
%if "%{arch_type}" == "loongarch64"
export GAZELLE_LIBS="-lrte_pci -lrte_bus_pci -lrte_cmdline -lrte_hash -lrte_mempool -lrte_mempool_ring -lrte_timer -lrte_eal -lrte_gro -lrte_ring -lrte_mbuf -lrte_telemetry -lrte_kni -lrte_net_ixgbe -lrte_kvargs -lrte_net_hinic -lrte_net_virtio -lrte_bus_vdev -lrte_net -lrte_rcu -lrte_ethdev -lrte_pdump -lrte_bpf -lrte_security -lrte_cryptodev -lrte_net_pcap -lrte_metrics"
%else
export GAZELLE_LIBS="-lrte_pci -lrte_bus_pci -lrte_cmdline -lrte_hash -lrte_mempool -lrte_mempool_ring -lrte_timer -lrte_eal -lrte_gro -lrte_ring -lrte_mbuf -lrte_telemetry -lrte_kni -lrte_net_ixgbe -lrte_kvargs -lrte_net_hinic -lrte_net_i40e -lrte_net_virtio -lrte_bus_vdev -lrte_net -lrte_rcu -lrte_ethdev -lrte_pdump -lrte_bpf -lrte_security -lrte_cryptodev -lrte_net_pcap -lrte_metrics"
%endif
export SECURE_OPTIONS="-fstack-protector-strong -D_FORTIFY_SOURCE=2 -O2 -Wall -Wl,-z,relro,-z,now,-z,noexecstack -Wtrampolines -fPIE -pie -fPIC -g"
gcc -o gazelle-pdump ${GAZELLE_FLAGS} ${SOCURE_OPTIONS} -L../../drivers -L../../lib ${GAZELLE_LIBS} pdump_main.c.o
cd -

%install
DESTDIR=$RPM_BUILD_ROOT/ ninja install -C build

%ifnarch sw_64
chrpath -d ./build/examples/dpdk-l3fwd
%endif
chrpath -d ./build/examples/dpdk-l3fwd-power
chrpath -d ./build/examples/dpdk-ethtool
chrpath -d ./build/examples/dpdk-kni
chrpath -d ./build/examples/dpdk-dma
chrpath -d ./build/examples/dpdk-ptpclient
chrpath -d ./build/app/dpdk-pdump.p/gazelle-pdump

%ifnarch sw_64
cp ./build/examples/dpdk-l3fwd $RPM_BUILD_ROOT/usr/local/bin
%endif
cp ./build/examples/dpdk-l3fwd-power $RPM_BUILD_ROOT/usr/local/bin
cp ./build/examples/dpdk-ethtool $RPM_BUILD_ROOT/usr/local/bin
cp ./build/examples/dpdk-kni $RPM_BUILD_ROOT/usr/local/bin
cp ./build/examples/dpdk-dma $RPM_BUILD_ROOT/usr/local/bin
cp ./build/examples/dpdk-ptpclient $RPM_BUILD_ROOT/usr/local/bin
cp ./build/app/dpdk-pdump.p/gazelle-pdump $RPM_BUILD_ROOT/usr/local/bin

mkdir -p $RPM_BUILD_ROOT%{_libdir}
mv $RPM_BUILD_ROOT/usr/local/%{_lib}/* $RPM_BUILD_ROOT%{_libdir}/

mkdir -p $RPM_BUILD_ROOT/usr/local/bin
ln -fs /usr/local/bin/dpdk-devbind.py $RPM_BUILD_ROOT/usr/local/bin/dpdk-devbind
mkdir $RPM_BUILD_ROOT%{_libdir}/dpdk/pmds-22.0/lib
mkdir $RPM_BUILD_ROOT%{_libdir}/dpdk/pmds-22.0/include
cd $RPM_BUILD_ROOT%{_libdir}/dpdk/pmds-22.0/include
ln -fs ../../../../local/include/* .
cd -
cd $RPM_BUILD_ROOT%{_libdir}/dpdk/pmds-22.0/lib
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
%{_libdir}/*.so*
%{_libdir}/dpdk/*
%exclude %{_libdir}/dpdk/pmds-22.0/include/*.h

%files devel
/usr/local/include
%{_libdir}/*.a
%{_libdir}/dpdk/pmds-22.0/include/*.h
%{_libdir}/pkgconfig/libdpdk-libs.pc
%{_libdir}/pkgconfig/libdpdk.pc

%files doc

%files tools
/usr/local/bin/dpdk-pdump
/usr/local/bin/dpdk-dumpcap
/usr/local/bin/dpdk-proc-info
/usr/local/bin/dpdk-test
/usr/local/bin/dpdk-testpmd
%ifnarch sw_64
/usr/local/bin/dpdk-l3fwd
%endif
/usr/local/bin/dpdk-l3fwd-power
/usr/local/bin/dpdk-ethtool
/usr/local/bin/dpdk-kni
/usr/local/bin/dpdk-dma
/usr/local/bin/dpdk-ptpclient
/usr/local/bin/gazelle-pdump

%post
/sbin/ldconfig
/usr/sbin/depmod

%postun
/sbin/ldconfig
/usr/sbin/depmod

%changelog
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
