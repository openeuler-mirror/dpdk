#!/usr/bin/env bash
# Build hns5 (DPDK 22.11) for aarch64.
#
# What this script does:
#   1. On an aarch64 host: builds natively (cc + meson + ninja + numactl-devel).
#      On any other host (e.g. x86_64): cross-builds with aarch64-linux-gnu-gcc
#      against the /usr/aarch64-linux-gnu sysroot (Ubuntu cross packages +
#      arm64 libnuma installed there). This script never installs anything itself.
#   2. Flattens the uDriver (hidrv/ndrv) API headers from the SDK zip into
#      drivers/net/hns5/dependency/{hidrv,native}. Place
#      S920L18_SDK_004.zip NEXT TO this dpdk source dir (or pass the path as
#      $1 / env SDK_ZIP). A pre-extracted SDK dir also works (env SDK_DIR).
#   3. Generates a STUB libhidrv_api.so: only the hidrv_/ndrv_ functions that
#      hns5 actually references get empty implementations, enough for
#      link-time. The real libhidrv_api.so from the board BSP (/lib64) is
#      loaded at runtime on the target (driver RUNPATH falls back to it).
#   4. Full meson/ninja build.
#
# Usage: run `./buildtools/hns5_compile.sh --help` for options.
#   --debug / --frame-pointers / --chip= / --bd-version= , plus SDK_ZIP=
#   SDK_DIR= and HNS5_FRAME_POINTERS= environment fallbacks.
#
# Debug build uses build-debug/ dir, release uses build/ — both can coexist.

set -euo pipefail

usage() {
    cat <<'USAGE'
Usage: hns5_compile.sh [OPTION...] [SDK_ZIP_PATH]

Build options:
  --debug, -d            debugoptimized build (-g -O2) into build-debug/;
                         default is release into build/ (both can coexist)
  --frame-pointers       keep frame pointers (-fno-omit-frame-pointer) so that
                         `perf -g fp` can unwind the call stack; without it
                         samples collapse to a single frame per stack. DWARF
                         mode (perf --call-graph dwarf) needs no rebuild but
                         copies a stack per sample, so it is far more intrusive
                         on a busy polling core.

SDK selection:
  [SDK_ZIP_PATH]         BSP SDK zip, passed as the only bare argument
  SDK_ZIP=/path.zip      same thing via environment
                         default: <repo root>/S920L18_SDK_004.zip
  SDK_DIR=/extracted     pre-extracted SDK root; skips the unzip step
  --chip=NAME            SoC directory inside the SDK. Unset means auto-detect
                         when the SDK holds exactly one such directory (env CHIP)
  --bd-version=VER       BD header directory bd_<VER> (default: v400, env BD_VERSION)

Compatibility environment:
  HNS5_FRAME_POINTERS=1  same as --frame-pointers
USAGE
}

# --- parse args: options are consumed, a bare argument is the SDK zip ---
DEBUG=0
FRAME_POINTERS="${HNS5_FRAME_POINTERS:-0}"
SDK_ZIP_ARG=""
for arg in "$@"; do
    case "$arg" in
        --debug|-d)       DEBUG=1 ;;
        --frame-pointers) FRAME_POINTERS=1 ;;
        --chip=*)         CHIP="${arg#*=}" ;;
        --bd-version=*)   BD_VERSION="${arg#*=}" ;;
        --help|-h)        usage; exit 0 ;;
        *)                SDK_ZIP_ARG="$arg" ;;
    esac
done

SRC_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK_ZIP="${SDK_ZIP_ARG:-${SDK_ZIP:-$SRC_ROOT/../S920L18_SDK_004.zip}}"
SDK_DIR="${SDK_DIR:-}"              # optional: pre-extracted SDK root
CHIP="${CHIP:-}"                  # SDK SoC dir; auto-detected when unset
BD_VERSION="${BD_VERSION:-v400}"    # per BSP: nativedrv/soc/<chip>/limit.h BD_VERSION 0x400
STUB_PREFIXES='^(hidrv_|ndrv_)'
DEP="$SRC_ROOT/drivers/net/hns5/dependency"

# release → build/, debug → build-debug/
if [ "$DEBUG" = 1 ]; then
    BUILD_DIR="build-debug"
    BUILDTYPE="debugoptimized"
else
    BUILD_DIR="build"
    BUILDTYPE="release"
fi

# aarch64 host → native build; anything else (x86_64) → cross build
if [ "$(uname -m)" = "aarch64" ]; then
    CROSS=0
    CC_CMD="cc"
else
    CROSS=1
    CC_CMD="aarch64-linux-gnu-gcc"
fi
CROSS_FILE="$SRC_ROOT/buildtools/build-cross.ini"

log() { echo "[hns5-build] $*"; }
die() { echo "[hns5-build] ERROR: $*" >&2; exit 1; }
have() { command -v "$1" >/dev/null 2>&1; }

# --------------------------------------------------------------- toolchain --
check_toolchain() {
    local missing=()
    if [ "$CROSS" = 1 ]; then
        have aarch64-linux-gnu-gcc || missing+=(aarch64-linux-gnu-gcc)
    else
        [ "$(uname -m)" = "aarch64" ] \
            || die "this script builds natively on aarch64 only (set CROSS=1 env to cross-build)"
        have cc || missing+=(cc)
    fi
    have meson      || missing+=(meson)
    have ninja      || missing+=(ninja)
    have pkg-config || missing+=(pkg-config)
    have nm         || missing+=(nm)
    if [ ${#missing[@]} -gt 0 ]; then
        die "missing tools: ${missing[*]} — apt install gcc-aarch64-linux-gnu meson ninja-build pkg-config (cross) or gcc meson ninja-build pkg-config numactl-dev (native) and re-run"
    fi
    if [ "$CROSS" = 1 ]; then
        [ -f /usr/aarch64-linux-gnu/include/numa.h ] \
            || die "cross sysroot missing libnuma headers (need /usr/aarch64-linux-gnu/include/numa.h)"
    else
        [ -f /usr/include/numa.h ] || die "numactl-devel missing (need /usr/include/numa.h)"
    fi
}

gen_cross_file() {
    cat > "$CROSS_FILE" <<'EOF'
[binaries]
c = 'aarch64-linux-gnu-gcc'
cpp = 'aarch64-linux-gnu-g++'
ar = 'aarch64-linux-gnu-ar'
strip = 'aarch64-linux-gnu-strip'
pkg-config = 'aarch64-linux-gnu-pkg-config'

[host_machine]
system = 'linux'
cpu_family = 'aarch64'
cpu = 'armv8-a'
endian = 'little'

[properties]
platform = 'hns5'
EOF
}

# ----------------------------------------------------------------- headers --
prepare_headers() {
    local udrv="" tmp=""
    if [ -n "$SDK_DIR" ]; then
        udrv="$SDK_DIR/src/kernel/uDriver/udrv"
        [ -d "$udrv" ] || die "SDK_DIR invalid: $SDK_DIR (no $udrv)"
    else
        [ -f "$SDK_ZIP" ] || die "SDK zip not found: $SDK_ZIP
  Place S920L18_SDK_004.zip next to the dpdk dir, or: ./buildtools/hns5_compile.sh /path/to/S920L18_SDK_004.zip"
        have unzip || die "unzip not found"
        log "extracting uDriver headers from $(basename "$SDK_ZIP")"
        tmp=$(mktemp -d)
        unzip -q -o "$SDK_ZIP" \
            "src/kernel/uDriver/udrv/hidrv/include/*" \
            "src/kernel/uDriver/udrv/include/*" \
            "src/kernel/uDriver/udrv/nativedrv/modules/poe/*" \
            "src/kernel/uDriver/udrv/nativedrv/modules/include/*" \
            "src/kernel/uDriver/udrv/nativedrv/soc/*" \
            -d "$tmp" || { rm -rf "$tmp"; die "unzip failed"; }
        udrv="$tmp/src/kernel/uDriver/udrv"
    fi

    if [ -z "$CHIP" ]; then
        local socs nsoc
        socs=$(find "$udrv/nativedrv/soc" -mindepth 1 -maxdepth 1 -type d \
               -printf '%f\n' 2>/dev/null | sort -u)
        nsoc=$(printf '%s\n' "$socs" | grep -c .)
        case "$nsoc" in
            1) CHIP=$(printf '%s\n' "$socs")
               log "auto-detected the SDK SoC directory" ;;
            0) die "SDK holds no SoC directory under $udrv/nativedrv/soc" ;;
            *) die "SDK holds $nsoc SoC directories under $udrv/nativedrv/soc; pass --chip=NAME" ;;
        esac
    fi

    rm -rf "$DEP"
    mkdir -p "$DEP/hidrv" "$DEP/native" "$DEP/lib64"
    # flatten exactly the way the BSP installs its API output dirs
    cp "$udrv"/hidrv/include/*.h                   "$DEP/hidrv/"
    cp "$udrv"/include/hidrv_api/*.h               "$DEP/hidrv/"
    cp "$udrv"/include/module_type_api/bd_$BD_VERSION/*.h "$DEP/native/"
    cp "$udrv"/include/nativedrv_api/*.h           "$DEP/native/"
    cp "$udrv"/nativedrv/modules/poe/poe_errno.h   "$DEP/native/"
    cp "$udrv"/nativedrv/modules/include/udrv_error.h "$DEP/native/"
    cp "$udrv"/nativedrv/soc/$CHIP/limit.h         "$DEP/native/"
    [ -n "$tmp" ] && rm -rf "$tmp"
    log "headers ready: $(ls "$DEP/hidrv" | wc -l) hidrv + $(ls "$DEP/native" | wc -l) native"
}

# ------------------------------------------------------------- stub library --
placeholder_stub() {
    "$CC_CMD" -shared -fPIC -x c /dev/null -o "$DEP/lib64/libhidrv_api.so"
}

rebuild_stub() {   # $1: build dir
    local objs syms undef defined
    objs=$(find "$1/drivers" -name '*.c.o' \( -path '*net_hns5*' -o -path '*mempool_hns5*' -o -path '*crypto_hns5*' \) \
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
    "$CC_CMD" -shared -fPIC "$DEP/lib64/hidrv_stub.c" -o "$DEP/lib64/libhidrv_api.so"
    log "stub libhidrv_api.so: $(echo "$syms" | wc -l) symbols stubbed"
}

# -------------------------------------------------------------------- build --
main() {
    cd "$SRC_ROOT"
    check_toolchain
    prepare_headers
    placeholder_stub   # so -lhidrv_api resolves while only objects are built

    # stale configs would persist otherwise
    rm -rf "$BUILD_DIR"
    # No chmod of buildtools/*.sh: meson runs install scripts through their
    # shebang (falling back to sh), so the executable bit is not needed, and
    # chmod here used to flip the tracked 0644 mode of
    # symlink-drivers-solibs.sh into a spurious worktree diff and a _dirty
    # build id on every build.

    local cross_args=()
    if [ "$CROSS" = 1 ]; then
        gen_cross_file
        cross_args+=(--cross-file "$CROSS_FILE")
        log "cross-building aarch64 on $(uname -m) with $CC_CMD"
    fi

    log "buildtype=$BUILDTYPE, build_dir=$BUILD_DIR"

    # Build id (git hash + dirty flag) is compiled in and printed at probe, so
    # it is obvious which source tree a running binary came from.
    local build_id
    build_id=$(git -C "$SRC_ROOT" rev-parse --short HEAD 2>/dev/null || echo unknown)
    if ! git -C "$SRC_ROOT" diff --quiet 2>/dev/null; then
        build_id="${build_id}_dirty"
    fi
    build_id="${build_id}_$(date +%m%d%H%M)"
    log "build id: $build_id"

    # One -Dc_args only, with the flags separated by a SPACE. Meson keeps the
    # LAST occurrence of an option given twice (which is how --frame-pointers
    # used to vanish silently), and it hands a comma-separated value to the
    # compiler as a single argument, so `-DHNS5_BUILD_ID=x,-fno-omit...` would
    # make the build id expand to two macro arguments. A space is what ninja
    # splits on, verified against this tree's build.ninja.
    local c_args="-DHNS5_BUILD_ID=$build_id"
    [ "$FRAME_POINTERS" = 1 ] && c_args="$c_args -fno-omit-frame-pointer"

    meson setup "$BUILD_DIR" "${cross_args[@]}" \
        -Dbuildtype="$BUILDTYPE" \
        -Dc_args="$c_args" \
        -Ddisable_drivers=true -Denable_drivers=net/hns5,mempool/hns5,crypto/hns5 \
        -Dtests=true \
        -Denable_kmods=false \
        -Denable_apps=test-pmd,test-crypto-perf \
        -Dexamples=l2fwd-crypto \
        || die "meson setup failed"

    # Check the flags actually landed rather than trusting our own log line: the
    # duplicated -Dc_args made the script report frame pointers as enabled while
    # meson had dropped them, and `perf -g fp` then returned empty stacks.
    local ninja_f="$BUILD_DIR/build.ninja"
    [ -f "$ninja_f" ] || die "meson setup did not produce $ninja_f"
    grep -q "DHNS5_BUILD_ID" "$ninja_f" || die "build id missing from $ninja_f"
    if [ "$FRAME_POINTERS" = 1 ]; then
        grep -q "fno-omit-frame-pointer" "$ninja_f" \
            || die "frame pointers requested but absent from $ninja_f"
        log "frame pointers verified in $ninja_f (perf -g fp can unwind)"
    else
        log "frame pointers off (add --frame-pointers for perf -g fp)"
    fi

    # 1) compile only the hns5 objects
    local obj_targets
    obj_targets=$(ninja -C "$BUILD_DIR" -t targets all 2>/dev/null \
        | awk -F: '$1 ~ /(net_hns5|mempool_hns5|crypto_hns5).*\.c\.o$/ && $1 !~ /pmd\.c\.o$/ {print $1}' \
        | sort -u)
    [ -n "$obj_targets" ] || die "no hns5 object targets found in ninja manifest"
    ninja -C "$BUILD_DIR" $obj_targets || die "failed to compile hns5 objects"

    # 2) regenerate the stub from the symbols actually referenced
    rebuild_stub "$BUILD_DIR"

    # 3) full build
    ninja -C "$BUILD_DIR" || die "final ninja build failed"

    log "OK: $BUILD_DIR/app/dpdk-testpmd + dpdk-test-crypto-perf + hns5 drivers ready (aarch64 $( [ "$CROSS" = 1 ] && echo cross || echo native ), $BUILDTYPE)"
}

main
