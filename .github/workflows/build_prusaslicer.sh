#!/usr/bin/env bash
set -Eeuxo pipefail

os="${1:-ubuntu-20.04}"

cmake_args=(
    -DCMAKE_INSTALL_PREFIX=/usr
    -DCMAKE_INSTALL_LIBDIR=lib
    -DSLIC3R_FHS=ON
    -DSLIC3R_PCH=OFF
    -DSLIC3R_WX_STABLE=ON
    -DSLIC3R_GTK=3
    -DSLIC3R_STATIC=OFF
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

# Change date in version
sed -i "s/+UNKNOWN/_$(date '+%F')/" version.inc

mkdir _build -p
pushd _build
    # - Ninja build system
    # - Use CCache
    # - Linux filesystem tree + /usr + /usr/lib
    # - Don't use precompiled header because it does not allow ccache to work correctly
    # - gtk3 + wxwidget3
    # - shared libs build
    WX_CONFIG=wx-config-gtk3 \
    cmake .. -G "$generator" "${cmake_args[@]}"

    cmake --build .

popd

# Install
pushd _build
    cmake --install .
popd

# cat << 'EOF' > "$DESTDIR/prusaslicer"
# #!/bin/bash
# DIR=$(readlink -f "$0" | xargs dirname)
# export LD_LIBRARY_PATH="$DIR/bin"
# exec "$DIR/bin/prusaslicer" "$@"
# EOF
# chmod +x "$DESTDIR/prusaslicer"
