#!/usr/bin/env bash
set -Eeuxo pipefail

os="${1:-ubuntu-20.04}"

case "$os" in
    ubuntu-20.04|ubuntu-latest) libnlopt="libnlopt-cxx-dev" ;;
    *)  libnlopt="libnlopt-dev" ;;
esac

case "$os" in
    ubuntu-16.04) wxgtk="libwxgtk3.0-dev" ;;
    *)  wxgtk="libwxgtk3.0-gtk3-dev" ;;
esac


dependencies=(
    cmake
    ninja-build
    help2man
    libboost-all-dev
    libcereal-dev
    libcgal-dev
    libcurl4-gnutls-dev
    libdbus-1-dev
    libeigen3-dev
    libglew-dev
    libgtest-dev
    libgtk-3-dev
    libopenvdb-dev
    libopenvdb-tools
    libpng-dev
    libtbb-dev
    libudev-dev
    xauth
    xfonts-base
    xvfb
    zlib1g-dev
    "$libnlopt"
    "$wxgtk"
)

sudo apt-get update
sudo apt-get install "${dependencies[@]}"
