#!/bin/bash
# SimulationCraft Build Script (bash/MSYS2 wrapper)
# Usage:  ./build.sh [debug|release] [configure|rebuild|clean]

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_TYPE="Debug"
ACTION="build"

for arg in "$@"; do
    case "${arg,,}" in
        release)   BUILD_TYPE="Release" ;;
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

need_configure=0

if [[ -f "$CACHE_FILE" ]]; then
    existing_gen="$(cache_value CMAKE_GENERATOR || true)"
    existing_type="$(cache_value CMAKE_BUILD_TYPE || true)"

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

echo "Building simc [$BUILD_TYPE]..."
if ! cmake --build "$BUILD_DIR" -j "$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"; then
    echo "ERROR: Build failed." >&2
    exit 1
fi

echo ""
echo "Build complete: $BUILD_DIR/simc"
