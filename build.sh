#!/bin/bash
# SimulationCraft Build Script (bash/MSYS2 wrapper)
# Usage:  ./build.sh [debug|release] [configure|rebuild|clean]

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
        *) echo "Unknown argument: $arg"; exit 1 ;;
    esac
done

BUILD_DIR="$SCRIPT_DIR/build"

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

if [[ ! -f "$BUILD_DIR/build.ninja" ]]; then
    echo "Configuring CMake with Ninja [$BUILD_TYPE]..."
    cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" -G Ninja \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        -DBUILD_GUI=OFF \
        -DCMAKE_CXX_STANDARD=17
    [[ $? -ne 0 ]] && echo "ERROR: CMake configure failed." && exit 1
fi

if [[ "$ACTION" == "configure" ]]; then
    echo "Configure complete."
    exit 0
fi

echo "Building simc [$BUILD_TYPE]..."
cmake --build "$BUILD_DIR" -j "$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
[[ $? -ne 0 ]] && echo "ERROR: Build failed." && exit 1

echo ""
echo "Build complete: $BUILD_DIR/simc"
