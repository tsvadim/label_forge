#!/bin/sh
# Builds a self-contained LabelForge.exe for Windows on Linux (Ubuntu 24.04):
# static Qt 6.4.2 cross-compiled with MinGW-w64, then the app linked statically.
# Needs: apt install g++-mingw-w64-x86-64-posix qt6-base-dev qt6-base-dev-tools cmake ninja-build git
# SPDX-License-Identifier: GPL-3.0-or-later
set -e
SRC=$(cd "$(dirname "$0")/.." && pwd)
WORK=${WORK:-$PWD/winbuild}
export QT_WIN_PREFIX=$WORK/qt-win-static
mkdir -p "$WORK" && cd "$WORK"
[ -d qtbase ] || git clone --depth 1 -b v6.4.2 https://github.com/qt/qtbase.git
cmake -S qtbase -B qtbuild -G Ninja -DCMAKE_TOOLCHAIN_FILE="$SRC/packaging/mingw-toolchain.cmake" \
  -DQT_HOST_PATH=/usr -DQT_HOST_PATH_CMAKE_DIR=/usr/lib/x86_64-linux-gnu/cmake \
  -DCMAKE_INSTALL_PREFIX="$QT_WIN_PREFIX" -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
  -DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF -DFEATURE_static_runtime=ON -DFEATURE_opengl=OFF -DINPUT_opengl=no \
  -DFEATURE_dbus=OFF -DFEATURE_network=OFF -DFEATURE_sql_odbc=ON -DFEATURE_sql_sqlite=OFF -DFEATURE_concurrent=OFF \
  -DFEATURE_testlib=OFF -DFEATURE_vulkan=OFF -DFEATURE_system_zlib=OFF -DFEATURE_system_pcre2=OFF \
  -DFEATURE_system_harfbuzz=OFF -DFEATURE_system_freetype=OFF
cmake --build qtbuild --parallel && cmake --install qtbuild
"$QT_WIN_PREFIX/bin/qt-cmake" -S "$SRC" -B app -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DQT_HOST_PATH=/usr -DQT_HOST_PATH_CMAKE_DIR=/usr/lib/x86_64-linux-gnu/cmake -DLABELFORGE_BUILD_TESTS=OFF \
  -DCMAKE_EXE_LINKER_FLAGS="-static -static-libgcc -static-libstdc++ -s" \
  -DCMAKE_CXX_STANDARD_LIBRARIES="-Wl,-Bstatic -lstdc++ -lwinpthread -Wl,-Bdynamic"
cmake --build app --parallel
echo "Done: $WORK/app/LabelForge.exe"
