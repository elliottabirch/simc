#!/bin/bash
# SimulationCraft Build Script (bash/MSYS2 wrapper)
# Usage:  ./build.sh [release|fast|debug] [configure|rebuild|clean]
#
#   release  (default)  -O3, assertions ON   -- optimized and still self-checking
#   fast                -O3 -DNDEBUG         -- max throughput, assertions STRIPPED
#   debug                -O0 -g, assertions ON
#
# Assertions stay on in the default build on purpose. CMake's stock Release
# config appends -DNDEBUG, which deletes all ~1035 assert() call sites in
# engine/ -- including cooldown_t::start()'s `current_charge > 0` guard. Those
# assertions are the engine's own early-warning system: with them stripped, the
# same corrupt state does not crash, it decrements an int past zero and silently
# poisons every downstream decision. For a pipeline whose output is training/eval
# data, silent bad data is far more expensive than a loud abort.
#
# Measured cost of keeping them (MID1_Paladin_Retribution, 2000 iterations,
# 3 runs each): 5.10s -> 8.72s CPU, i.e. 1.71x. Still ~1.9x faster than an
# unoptimized build, which is what this tree was being built as before -- so the
# default gets both the speedup and the visibility that build had.
#
# Use `fast` only for runs whose correctness you are not depending on.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_TYPE="Release"
RELEASE_FLAGS="-O3"
ACTION="build"

for arg in "$@"; do
    case "${arg,,}" in
        release)   BUILD_TYPE="Release"; RELEASE_FLAGS="-O3" ;;
        fast)      BUILD_TYPE="Release"; RELEASE_FLAGS="-O3 -DNDEBUG" ;;
        debug)     BUILD_TYPE="Debug" ;;
        configure) ACTION="configure" ;;
        rebuild)   ACTION="rebuild" ;;
        clean)     ACTION="clean" ;;
        *) echo "Unknown argument: $arg" >&2; exit 1 ;;
    esac
done

BUILD_DIR="$SCRIPT_DIR/build"
CACHE_FILE="$BUILD_DIR/CMakeCache.txt"

# On Windows/MSYS2, we need to call the .bat version for vcvarsall
if [[ "$OSTYPE" == "msys" || "$OSTYPE" == "cygwin" ]]; then
    # Convert to Windows path and call build.bat
    WIN_SCRIPT_DIR=$(cygpath -w "$SCRIPT_DIR")
    cmd //c "$WIN_SCRIPT_DIR\\build.bat" "$@"
    exit $?
fi

# Linux/macOS path
if [[ "$ACTION" == "clean" ]]; then
    echo "Cleaning build directory..."
    rm -rf "$BUILD_DIR"
    echo "Done."
    exit 0
fi

if [[ "$ACTION" == "rebuild" ]]; then
    rm -rf "$BUILD_DIR"
fi

# Reads one entry out of CMakeCache.txt. The type segment varies per entry
# (STRING/INTERNAL/BOOL), hence [^=]* rather than a fixed match.
cache_value() {
    [[ -f "$CACHE_FILE" ]] || return 1
    sed -n "s|^$1:[^=]*=||p" "$CACHE_FILE" | head -1
}

configure_args=(
    -S "$SCRIPT_DIR"
    -B "$BUILD_DIR"
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
    -DBUILD_GUI=OFF
    -DCMAKE_CXX_STANDARD=17
)

# Overriding CMAKE_CXX_FLAGS_RELEASE is the only way to drop the -DNDEBUG that
# CMake's stock Release config hardcodes. It cannot be undone via CMAKE_CXX_FLAGS
# (-UNDEBUG there is emitted BEFORE the per-config flags, so the later -DNDEBUG
# just wins).
if [[ "$BUILD_TYPE" == "Release" ]]; then
    configure_args+=( -DCMAKE_CXX_FLAGS_RELEASE="$RELEASE_FLAGS" )
fi

assert_state() {
    case "$1" in
        *-DNDEBUG*) echo "STRIPPED" ;;
        *)          echo "on" ;;
    esac
}

need_configure=0

if [[ -f "$CACHE_FILE" ]]; then
    existing_gen="$(cache_value CMAKE_GENERATOR || true)"
    existing_type="$(cache_value CMAKE_BUILD_TYPE || true)"
    existing_relflags="$(cache_value CMAKE_CXX_FLAGS_RELEASE || true)"

    if [[ -z "$existing_gen" ]]; then
        echo "ERROR: $CACHE_FILE exists but names no generator (partial or corrupt configure)." >&2
        echo "       Run './build.sh clean' and try again." >&2
        exit 1
    fi

    # Deliberately do NOT pass -G against an existing cache. CMake hard-errors on
    # a generator mismatch ("does not match the generator used previously"), so
    # forcing a generator here made the script unusable against any tree that had
    # been configured another way -- by an IDE, by a bare cmake invocation, or on
    # a host where the preferred generator was not installed. Reuse what is there.
    echo "Using existing '$existing_gen' build in $BUILD_DIR"

    # A single-config generator bakes the build type into the cache, so switching
    # requires a reconfigure -- and that invalidates every object file. Say so
    # rather than letting it look like a spuriously slow incremental build.
    if [[ "$existing_type" != "$BUILD_TYPE" ]]; then
        echo "Build type: ${existing_type:-<unset>} -> $BUILD_TYPE (reconfiguring, forces a full rebuild)"
        need_configure=1
    elif [[ "$BUILD_TYPE" == "Release" && "$existing_relflags" != "$RELEASE_FLAGS" ]]; then
        # Same CMAKE_BUILD_TYPE, different flags -- this is the release/fast
        # switch, and comparing build type alone would silently keep the wrong
        # assertion setting.
        echo "Release flags: '${existing_relflags}' -> '${RELEASE_FLAGS}' (reconfiguring, forces a full rebuild)"
        echo "  assertions: $(assert_state "$existing_relflags") -> $(assert_state "$RELEASE_FLAGS")"
        need_configure=1
    fi
else
    # Fresh tree. Prefer Ninja, but only when it is actually present -- hardcoding
    # -G Ninja fails outright on a host without it, which is the other half of the
    # bug above.
    if command -v ninja >/dev/null 2>&1; then
        configure_args+=( -G Ninja )
        echo "Configuring CMake with Ninja [$BUILD_TYPE]..."
    else
        echo "Configuring CMake with Unix Makefiles [$BUILD_TYPE] (ninja not found)..."
    fi

    # engine/CMakeLists.txt does find_package(CURL REQUIRED) unless
    # SC_NO_NETWORKING is set, so a host without libcurl development headers
    # cannot configure at all. Detect and disable rather than hard-fail: armory
    # /wowhead import is the only thing lost, and nothing in the local
    # profile/APL/solver workflow touches it. Only decided here, on a fresh
    # configure -- a reconfigure inherits whatever the cache already holds.
    have_curl=0
    if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists libcurl 2>/dev/null; then
        have_curl=1
    elif [[ -f /usr/include/curl/curl.h || -f /usr/local/include/curl/curl.h \
         || -f /opt/homebrew/include/curl/curl.h ]]; then
        have_curl=1
    fi

    if [[ "$have_curl" -eq 1 ]]; then
        echo "  networking: enabled (libcurl found)"
    else
        configure_args+=( -DSC_NO_NETWORKING=ON )
        echo "  networking: disabled (libcurl headers not found; armory import unavailable)"
    fi

    need_configure=1
fi

[[ "$ACTION" == "configure" ]] && need_configure=1

if [[ "$need_configure" -eq 1 ]]; then
    if ! cmake "${configure_args[@]}"; then
        echo "ERROR: CMake configure failed." >&2
        exit 1
    fi
fi

if [[ "$ACTION" == "configure" ]]; then
    echo "Configure complete."
    exit 0
fi

if [[ "$BUILD_TYPE" == "Release" ]]; then
    echo "Building simc [Release, assertions $(assert_state "$RELEASE_FLAGS")]..."
else
    echo "Building simc [$BUILD_TYPE]..."
fi
if ! cmake --build "$BUILD_DIR" -j "$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"; then
    echo "ERROR: Build failed." >&2
    exit 1
fi

echo ""
echo "Build complete: $BUILD_DIR/simc"
