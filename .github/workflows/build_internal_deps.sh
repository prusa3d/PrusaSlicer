#!/usr/bin/env bash
set -Eeuxo pipefail

os="${1:-ubuntu-20.04}"

cmake_args=(
)

case "$os" in
    ubuntu*)    generator="Ninja" ;;
    windows*)   generator="Visual Studio 16 2019" ;;
esac

case "$os" in
    ubuntu*)    cmake_args+=(
        -DCMAKE_C_COMPILER_LAUNCHER=ccache
        -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
    ) ;;
    windows*)   cmake_args+=() ;;
esac




mkdir _deps_build -p
pushd _deps_build
    cmake .. -G "$generator" "${cmake_args[@]}"

    cmake --build .
popd


# rename wxscintilla
# working-directory: ./deps/build/destdir/usr/local/lib
# cp libwxscintilla-3.1.a libwx_gtk2u_scintilla-3.1.a

# ls libs
# working-directory: ./deps/build
# ls ./destdir/usr/local/lib

# clean deps
# working-directory: ./deps/build
# rm -rf dep_*
