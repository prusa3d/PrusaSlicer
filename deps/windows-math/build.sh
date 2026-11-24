#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
set -euo pipefail

root=$(cygpath -u "$1")
toolchain=$(cygpath -u "$2")
target=$3
export PATH="$toolchain/bin:/usr/bin"
export CC="$target-gcc"
export CC_FOR_BUILD="$CC"
# GMP skips GXX detection with --disable-cxx. A supplied CXX would still
# initialize Libtool's C++ tag and overwrite the shared DLL naming settings.
export CXX=no
export AR=llvm-ar
export NM=llvm-nm
export RANLIB=llvm-ranlib
export STRIP=llvm-strip
export DLLTOOL=llvm-dlltool
export CFLAGS="-O2 -std=gnu11 -D__USE_MINGW_ANSI_STDIO=0"
unset CXXFLAGS
if [[ "$target" == x86_64-* ]]; then
    # Match MSVC's long double ABI, including MPFR's public get/set_ld API.
    CFLAGS="$CFLAGS -mlong-double-64"
fi
export LC_ALL=C
export TZ=UTC
prefix="$root/install"
mkdir -p "$root/build-gmp" "$root/build-mpfr"

cd "$root/build-gmp"
../src/gmp-6.2.1/configure \
    --build="$target" --host="$target" --prefix="$prefix" \
    ABI=64 \
    --disable-assembly --disable-static --enable-shared --disable-cxx \
    2>&1 | tee "$root/logs/gmp-configure.log"
make -j2 2>&1 | tee "$root/logs/gmp-build.log"
make install 2>&1 | tee "$root/logs/gmp-install.log"
if [[ ! -s "$prefix/bin/libgmp-10.dll" || ! -s "$prefix/lib/libgmp.dll.a" ]]; then
    echo "GMP did not install the expected GNU-driver DLL and import archive" >&2
    exit 1
fi
# Windows Libtool translates unsupported -no-install to -no-fast-install.
make -j2 check AM_LDFLAGS='-no-fast-install' \
    2>&1 | tee "$root/logs/gmp-check.log"

export PATH="$prefix/bin:$PATH"
cd "$root/build-mpfr"
../src/mpfr-4.2.1/configure \
    --build="$target" --host="$target" --prefix="$prefix" \
    --with-gmp="$prefix" --disable-static --enable-shared \
    --enable-thread-safe \
    2>&1 | tee "$root/logs/mpfr-configure.log"
make -j2 2>&1 | tee "$root/logs/mpfr-build.log"
make -j2 check AM_LDFLAGS='-no-fast-install -L$(top_builddir)/src/.libs' \
    2>&1 | tee "$root/logs/mpfr-check.log"
make install 2>&1 | tee "$root/logs/mpfr-install.log"
