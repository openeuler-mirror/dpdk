#!/usr/bin/env bash
# Build hns5 (DPDK 21.11 based) natively on an aarch64 host.
#
# What this script does:
#   1. Verifies it is running on aarch64 and probes the native toolchain
#      (gcc + meson + ninja + numactl-devel); aborts if anything is missing.
#      This script never installs anything itself.
#   2. Flattens the uDriver (hidrv/ndrv) API headers from the SDK zip into
#      drivers/net/hns5/dependency/{hidrv,native}. Place
#      S920L18_SDK_004.zip NEXT TO this dpdk source dir (or pass the path as
#      $1 / env SDK_ZIP). A pre-extracted SDK dir also works (env SDK_DIR).
#   3. Generates a STUB libhidrv_api.so: only the hidrv_/ndrv_ functions that
#      hns5 actually references get empty implementations, enough for
#      link-time. The real libhidrv_api.so from the board BSP (/lib64) is
#      loaded at runtime on the target (driver RUNPATH falls back to it).
#   4. Full meson/ninja build.

set -euo pipefail

SRC_ROOT="$(cd "$(dirname "$0")" && pwd)"
SDK_ZIP="${1:-${SDK_ZIP:-$SRC_ROOT/../S920L18_SDK_004.zip}}"
SDK_DIR="${SDK_DIR:-}"              # optional: pre-extracted SDK root
CHIP="${CHIP:-hi1231}"              # SoC of S920L18
BD_VERSION="${BD_VERSION:-v400}"    # per BSP: nativedrv/soc/hi1231/limit.h BD_VERSION 0x400
STUB_PREFIXES='^(hidrv_|ndrv_)'
DEP="$SRC_ROOT/drivers/net/hns5/dependency"

log() { echo "[hns5-build] $*"; }
die() { echo "[hns5-build] ERROR: $*" >&2; exit 1; }
have() { command -v "$1" >/dev/null 2>&1; }

# --------------------------------------------------------------- toolchain --
check_toolchain() {
    [ "$(uname -m)" = "aarch64" ] \
        || die "this script builds natively on aarch64 only (x86 cross-compile support was dropped)"
    local missing=()
    have cc         || missing+=(cc)
    have meson      || missing+=(meson)
    have ninja      || missing+=(ninja)
    have pkg-config || missing+=(pkg-config)
    have nm         || missing+=(nm)
    if [ ${#missing[@]} -gt 0 ]; then
        die "missing tools: ${missing[*]} — dnf install gcc meson ninja-build pkgconf-pkg-config numactl-devel and re-run"
    fi
    [ -f /usr/include/numa.h ] || die "numactl-devel missing (need /usr/include/numa.h)"
}

# ----------------------------------------------------------------- headers --
prepare_headers() {
    local udrv="" tmp=""
    if [ -n "$SDK_DIR" ]; then
        udrv="$SDK_DIR/src/kernel/uDriver/udrv"
        [ -d "$udrv" ] || die "SDK_DIR invalid: $SDK_DIR (no $udrv)"
    else
        [ -f "$SDK_ZIP" ] || die "SDK zip not found: $SDK_ZIP
  Place S920L18_SDK_004.zip next to the dpdk dir, or: ./hns5_compile.sh /path/to/S920L18_SDK_004.zip"
        have unzip || die "unzip not found"
        log "extracting uDriver headers from $(basename "$SDK_ZIP")"
        tmp=$(mktemp -d)
        unzip -q -o "$SDK_ZIP" \
            "src/kernel/uDriver/udrv/hidrv/include/*" \
            "src/kernel/uDriver/udrv/include/*" \
            "src/kernel/uDriver/udrv/nativedrv/soc/$CHIP/*" \
            -d "$tmp" || { rm -rf "$tmp"; die "unzip failed"; }
        udrv="$tmp/src/kernel/uDriver/udrv"
    fi

    rm -rf "$DEP"
    mkdir -p "$DEP/hidrv" "$DEP/native" "$DEP/lib64"
    # flatten exactly the way the BSP installs its API output dirs
    cp "$udrv"/hidrv/include/*.h                   "$DEP/hidrv/"
    cp "$udrv"/include/hidrv_api/*.h               "$DEP/hidrv/"
    cp "$udrv"/include/module_type_api/bd_$BD_VERSION/*.h "$DEP/native/"
    cp "$udrv"/include/nativedrv_api/*.h           "$DEP/native/"
    cp "$udrv"/nativedrv/soc/$CHIP/limit.h         "$DEP/native/"
    [ -n "$tmp" ] && rm -rf "$tmp"
    log "headers ready: $(ls "$DEP/hidrv" | wc -l) hidrv + $(ls "$DEP/native" | wc -l) native"
}

# ------------------------------------------------------------- stub library --
placeholder_stub() {
    cc -shared -fPIC -x c /dev/null -o "$DEP/lib64/libhidrv_api.so"
}

rebuild_stub() {   # $1: build dir
    local objs syms undef defined
    objs=$(find "$1/drivers" -name '*.c.o' \( -path '*net_hns5*' -o -path '*mempool_hns5*' \) \
           | grep -v 'pmd\.c\.o' || true)
    [ -n "$objs" ] || die "no hns5 objects found in $1 for stub generation"
    undef=$(nm $objs | awk '$1=="U"||$2=="U"{print $NF}' | sed 's/@.*//' | sort -u)
    defined=$(nm $objs | grep -E '^[0-9a-f]+ [TDBRWC] ' | awk '{print $3}' | sed 's/@.*//' | sort -u)
    syms=$(comm -23 <(echo "$undef") <(echo "$defined") | grep -E "$STUB_PREFIXES" || true)
    [ -n "$syms" ] || die "no ${STUB_PREFIXES} symbols referenced by hns5 objects?"
    {
        echo '/* Auto-generated link-time stubs.'
        echo ' * Real implementations come from the BSP libhidrv_api.so on the target. */'
        echo "$syms" | while read -r s; do echo "void $s(void) {}"; done
    } > "$DEP/lib64/hidrv_stub.c"
    cc -shared -fPIC "$DEP/lib64/hidrv_stub.c" -o "$DEP/lib64/libhidrv_api.so"
    log "stub libhidrv_api.so: $(echo "$syms" | wc -l) symbols stubbed"
}

# -------------------------------------------------------------------- build --
main() {
    cd "$SRC_ROOT"
    check_toolchain
    prepare_headers
    placeholder_stub   # so -lhidrv_api resolves while only objects are built

    # stale configs would persist otherwise
    rm -rf build
    chmod +x buildtools/*.sh 2>/dev/null || true

    meson setup build \
        -Dbuildtype=release \
        -Ddefault_library=shared \
        -Ddisable_drivers=net/cnxk,net/mlx4,net/mlx5,common/mlx5,regex/mlx5,vdpa/mlx5,crypto/* \
        -Dtests=true \
        -Denable_kmods=false \
        -Dexamples=l3fwd \
        || die "meson setup failed"

    # 1) compile only the hns5 objects
    local obj_targets
    obj_targets=$(ninja -C build -t targets all 2>/dev/null \
        | awk -F: '$1 ~ /(net_hns5|mempool_hns5).*\.c\.o$/ && $1 !~ /pmd\.c\.o$/ {print $1}' \
        | sort -u)
    [ -n "$obj_targets" ] || die "no hns5 object targets found in ninja manifest"
    ninja -C build $obj_targets || die "failed to compile hns5 objects"

    # 2) regenerate the stub from the symbols actually referenced
    rebuild_stub build

    # 3) full build
    ninja -C build || die "final ninja build failed"

    log "OK: build/app/dpdk-testpmd + hns5 drivers ready (aarch64 native)"
    log "package with: ./hns5_pack.sh"
}

main
