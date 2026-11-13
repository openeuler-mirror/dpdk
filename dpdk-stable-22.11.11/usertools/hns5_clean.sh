#!/usr/bin/env bash
# Clean up residual hns5 SEC runtime state left by abnormal exits (Ctrl+C, crash).
#
# What this cleans:
#   1. Owner lock files: the current one, plus the path used by the
#      vendor OpenSSL engine reference (see LEGACY_LOCK below - it must
#      match that engine verbatim or the cross-check stops working)
#   2. HugeTLB pool files (default /mnt/huge32m/hns5-engine.pool, or HNS5_ENGINE_POOL_FILE)
#   3. Optional: DPDK runtime state (--full flag: /var/run/dpdk/rte/*)
#   4. Optional: EAL hugepage files (--hugepages flag, or via --full):
#      /dev/hugepages-32M/rtemap_* etc. These matter when a previous run left
#      hugepage files behind: they keep the pages allocated, so they must be
#      removed when the pool size changes or when a clean slate is wanted.
#
# Usage:
#   ./hns5_clean.sh              # show current state (READ-ONLY, the default)
#   ./hns5_clean.sh --status     # same as above
#   ./hns5_clean.sh --clean      # remove hns5 locks and the pool file
#   ./hns5_clean.sh --hugepages  # the above + EAL hugepage files (rtemap_*)
#   ./hns5_clean.sh --full       # the above + DPDK runtime state
#
# Removing the pool file and the locks destroys the recovery marker that the
# driver writes when a teardown did not complete, so it is never the default:
# the marker is what stops a later process from reusing live DMA memory.

set -euo pipefail

OWNER_LOCK="/run/lock/hns5-bmu0.owner"
LEGACY_LOCK="/run/lock/hi1231-bmu0.owner"
DEFAULT_POOL="/mnt/huge32m/hns5-engine.pool"
POOL_FILE="${HNS5_ENGINE_POOL_FILE:-$DEFAULT_POOL}"
DPDK_RUNTIME="/var/run/dpdk/rte"
HNS5_HUGE_DIR="${HNS5_HUGE_DIR:-/dev/hugepages-32M}"

log()  { echo "[hns5-clean] $*"; }
warn() { echo "[hns5-clean] WARNING: $*" >&2; }

show_status() {
    log "=== current state ==="
    for f in "$OWNER_LOCK" "$LEGACY_LOCK" "$POOL_FILE"; do
        if [ -e "$f" ]; then
            local sz
            sz=$(stat -c%s "$f" 2>/dev/null || echo "?")
            log "  [exists] $f (size=${sz}B)"
        else
            log "  [absent] $f"
        fi
    done
    if [ -d "$DPDK_RUNTIME" ]; then
        local n
        n=$(find "$DPDK_RUNTIME" -type f 2>/dev/null | wc -l)
        log "  [exists] $DPDK_RUNTIME ($n files)"
    else
        log "  [absent] $DPDK_RUNTIME"
    fi
    if [ -d "$HNS5_HUGE_DIR" ]; then
        local n sz
        n=$(find "$HNS5_HUGE_DIR" -maxdepth 1 -name 'rtemap_*' 2>/dev/null | wc -l)
        sz=$(du -sh "$HNS5_HUGE_DIR" 2>/dev/null | awk '{print $1}')
        log "  [hugepage files] $HNS5_HUGE_DIR: $n rtemap_* file(s), $sz in use"
    else
        log "  [hugepage dir] $HNS5_HUGE_DIR not present"
    fi
    log "=== hugepages ==="
    if [ -d /sys/kernel/mm/hugepages/hugepages-32768kB ]; then
        grep . /sys/kernel/mm/hugepages/hugepages-32768kB/* 2>/dev/null | \
            sed 's/.*hugepages-32768kB\//  /' | sed 's/:/ = /' || true
    else
        log "  32768kB hugepages not supported on this kernel"
    fi
}

do_clean() {
    log "cleaning hns5 residual state..."
    local cleaned=0
    local marker=""

    if [ -e "$OWNER_LOCK" ]; then
        marker=$(head -c 64 "$OWNER_LOCK" 2>/dev/null || true)
    fi

    for f in "$OWNER_LOCK" "$LEGACY_LOCK"; do
        if [ -e "$f" ]; then
            rm -f "$f"
            log "  removed $f"
            cleaned=1
        fi
    done

    # pool file
    local pool_dir
    pool_dir=$(dirname "$POOL_FILE")
    if [ -e "$POOL_FILE" ]; then
        rm -f "$POOL_FILE"
        log "  removed $POOL_FILE"
        cleaned=1
    fi
    # also clean default pool if env overridden and default still has residue
    if [ "$POOL_FILE" != "$DEFAULT_POOL" ] && [ -e "$DEFAULT_POOL" ]; then
        rm -f "$DEFAULT_POOL"
        log "  removed $DEFAULT_POOL (default pool residue)"
        cleaned=1
    fi

    if [ "${1:-}" = "--hugepages" ] || [ "${1:-}" = "--full" ]; then
        if [ -d "$HNS5_HUGE_DIR" ]; then
            local n
            n=$(find "$HNS5_HUGE_DIR" -maxdepth 1 -name 'rtemap_*' 2>/dev/null | wc -l)
            if [ "$n" != 0 ]; then
                warn "removing $n EAL hugepage file(s) from $HNS5_HUGE_DIR" \
                     "(only safe when no DPDK process using them is running)"
                find "$HNS5_HUGE_DIR" -maxdepth 1 -name 'rtemap_*' -delete
                log "  removed $n rtemap_* file(s)"
                cleaned=1
            fi
        fi
    fi

    if [ "${1:-}" = "--full" ]; then
        if [ -d "$DPDK_RUNTIME" ]; then
            rm -rf "$DPDK_RUNTIME"
            log "  removed $DPDK_RUNTIME"
            cleaned=1
        fi
    fi

    case "$marker" in
        *NEEDS_BSP_RESET*)
            warn "the owner lock carried a NEEDS_BSP_RESET marker: the BMU was" \
                 "left configured by an abnormal exit."
            warn "deleting the lock does NOT reset the hardware. Run the BSP" \
                 "reset flow (BMU unsetup/reset/setup) or reboot the board" \
                 "before starting anything that touches the SEC/BMU again."
            ;;
    esac

    if [ "$cleaned" = 1 ]; then
        log "done: residual state cleaned"
    else
        log "done: nothing to clean"
    fi
}

case "${1:---status}" in
    --status) show_status ;;
    --hugepages) do_clean --hugepages; show_status ;;
    --full)   do_clean --full; show_status ;;
    --clean|'') do_clean; show_status ;;
    *) echo "Usage: $0 [--status|--clean|--hugepages|--full]"; exit 1 ;;
esac
