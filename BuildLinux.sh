#!/bin/bash
#
# This script can download and compile dependencies, compile PrusaSlicer
# and optional build a .tgz and an appimage.
#
# Original script from SuperSclier by supermerill https://github.com/supermerill/SuperSlicer
#
# Change log:
#
# 20 Nov 2023, wschadow, branding and minor changes
# 01 Jan 2024, wschadow, debranding for the Prusa version, added build options
#

export ROOT=`pwd`
export NCORES=`nproc`

OS_FOUND=$( command -v uname)

case $( "${OS_FOUND}" | tr '[:upper:]' '[:lower:]') in
  linux*)
    TARGET_OS="linux"
   ;;
  msys*|cygwin*|mingw*)
    # or possible 'bash on windows'
    TARGET_OS='windows'
   ;;
  nt|win*)
    TARGET_OS='windows'
    ;;
  darwin)
    TARGET_OS='macos'
    ;;
  *)
    TARGET_OS='unknown'
    ;;
esac

# check operating system
echo
if [ $TARGET_OS == "linux" ]; then
    if [ $(uname -m) == "x86_64" ]; then
        echo -e "$(tput setaf 2)Linux 64-bit found$(tput sgr0)\n"
        Processor="64"
    elif [[ $(uname -m) == "i386" || $(uname -m) == "i686" ]]; then
        echo "$(tput setaf 2)Linux 32-bit found$(tput sgr0)\n"
        Processor="32"
    else
        echo "$(tput setaf 1)Unsupported OS: Linux $(uname -m)"
        exit -1
    fi
else
    echo -e "$(tput setaf 1)This script doesn't support your Operating system!"
    echo -e "Please use Linux 64-bit or Windows 10 64-bit with Linux subsystem / git-bash.$(tput sgr0)\n"
    exit -1
fi

# Check if CMake is installed
export CMAKE_INSTALLED=`which cmake`
if [[ -z "$CMAKE_INSTALLED" ]]
then
    echo "Can't find CMake. Either is not installed or not in the PATH. Aborting!"
    exit -1
fi

unset name
while getopts ":hugbdrstiw" opt; do
  case ${opt} in
    u )
        UPDATE_LIB="1"
        ;;
    i )
        BUILD_IMAGE="1"
        ;;
    d )
        BUILD_DEPS="1"
        ;;
    s )
        BUILD_PRUSASLICER="1"
        ;;
    t )
        BUILD_TESTS="1"
        ;;
    b )
        BUILD_DEBUG="1"
        ;;
    g )
        FORCE_GTK2="-g"
        ;;
    r )
        BUILD_CLEANDEPEND="1"
	;;
    w )
	BUILD_WIPE="1"
	;;
    h ) echo "Usage: ./BuildLinux.sh [-h][-w][-u][-g][-b][-d][-r][-s][-t][-i]"
        echo "   -h: this message"
	    echo "   -w: wipe build directories before building"
        echo "   -u: only update dependency packets (optional and need sudo)"
        echo "   -g: force gtk2 build"
        echo "   -b: build in debug mode"
        echo "   -d: build deps"
        echo "   -r: clean dependencies"
        echo "   -s: build PrusaSlicer"
        echo "   -t: build tests (in combination with -s)"
        echo "   -i: Generate appimage (optional)"
        echo -e "\n   For a first use, you want to 'sudo ./BuildLinux.sh -u'"
        echo -e "   and then './BuildLinux.sh -dsi'\n"
        exit 0
        ;;
  esac
done

if [ $OPTIND -eq 1 ]
then
    echo "Usage: ./BuildLinux.sh [-h][-w][-u][-g][-b][-d][-r][-s][-t][-i]"
    echo "   -h: this message"
    echo "   -w: wipe build directories before building"
    echo "   -u: only update dependency packets (optional and need sudo)"
    echo "   -g: force gtk2 build"
    echo "   -b: build in debug mode"
    echo "   -d: build deps"
    echo "   -r: clean dependencies"
    echo "   -s: build PrusaSlicer"
    echo "   -t: build tests (in combination with -s)"
    echo "   -i: generate appimage (optional)"
    echo -e "\n   For a first use, you want to 'sudo ./BuildLinux.sh -u'"
    echo -e "   and then './BuildLinux.sh -dsi'\n"
    exit 0
fi

FOUND_GTK2=$(dpkg -l libgtk* | grep gtk2)
FOUND_GTK3=$(dpkg -l libgtk* | grep gtk-3)
FOUND_GTK2_DEV=$(dpkg -l libgtk* | grep gtk2.0-dev)
FOUND_GTK3_DEV=$(dpkg -l libgtk* | grep gtk-3-dev)

echo -e "FOUND_GTK2:\n$FOUND_GTK2\n"
echo -e "FOUND_GTK3:\n$FOUND_GTK3)\n"

if [[ -n "$FORCE_GTK2" ]]
then
   FOUND_GTK3=""
   FOUND_GTK3_DEV=""
fi

if [[ -n "$UPDATE_LIB" ]]
then
    echo -n -e "Updating linux ...\n"
    apt update
    if [[ -z "$FOUND_GTK3" ]]
    then
        echo -e "\nInstalling: libgtk2.0-dev libglew-dev libudev-dev libdbus-1-dev cmake git\n"
        apt install libgtk2.0-dev libglew-dev libudev-dev libdbus-1-dev cmake git
    else
        echo -e "\nFind libgtk-3, installing: libgtk-3-dev libglew-dev libudev-dev libdbus-1-dev cmake git\n"
        apt install libgtk-3-dev libglew-dev libudev-dev libdbus-1-dev cmake git
    fi

    # for ubuntu 22.04:
    ubu_version="$(cat /etc/issue)"
    if [[ $ubu_version == "Ubuntu 22.04"* ]]
    then
        apt install curl libssl-dev libcurl4-openssl-dev m4
    fi
    if [[ -n "$BUILD_DEBUG" ]]
    then
        echo -e "\nInstalling: libssl-dev libcurl4-openssl-dev\n"
        apt install libssl-dev libcurl4-openssl-dev
    fi
    echo -e "... done\n"
    exit 0
fi

if [[ -z "$FOUND_GTK2_DEV" ]]
then
    if [[ -z "$FOUND_GTK3_DEV" ]]
    then
	echo -e "\nError, you must install the dependencies before."
	echo -e "Use option -u with sudo\n"
	exit 0
    fi
fi

if [[ -n "$BUILD_DEPS" ]]
then
    if [[ -n $BUILD_WIPE ]]
    then
       echo -e "\n wiping deps/build directory...\n"
       rm -fr deps/build
       echo -e " ... done\n"
    fi
    # mkdir build in deps
    if [ ! -d "deps/build" ]
    then
	mkdir deps/build
    fi
    echo -e "[1/9] Configuring dependencies ...\n"
    BUILD_ARGS=""
    if [[ -n "$FOUND_GTK3_DEV" ]]
    then
        BUILD_ARGS="-DDEP_WX_GTK3=ON"
    else
        BUILD_ARGS="-DDEP_WX_GTK3=OFF"
    fi
    if [[ -n "$BUILD_DEBUG" ]]
    then
        # have to build deps with debug & release or the cmake won't find evrything it needs
	if [ ! -d "deps/build/release" ]
	then
	    mkdir deps/build/release
	fi
        pushd deps/build/release > /dev/null
        cmake ../.. -DDESTDIR="../destdir" $BUILD_ARGS
        popd > /dev/null
        BUILD_ARGS="${BUILD_ARGS} -DCMAKE_BUILD_TYPE=Debug"
    fi

    pushd deps/build > /dev/null
    cmake .. $BUILD_ARGS
    echo -e "\n ... done\n"

    echo -e "\n[2/9] Building dependencies...\n"
    # make deps
    make -j$NCORES
    echo -e "\n ... done\n"

    # rename wxscintilla
    echo "[3/9] Renaming wxscintilla library..."
    pushd destdir/usr/local/lib  > /dev/null
    if [[ -z "$FOUND_GTK3_DEV" ]]
    then
        cp libwxscintilla-3.2.a libwx_gtk2u_scintilla-3.2.a
    else
        cp libwxscintilla-3.2.a libwx_gtk3u_scintilla-3.2.a
    fi
    popd > /dev/null
    popd > /dev/null
    echo -e "\n ... done\n"
fi

if [[ -n "$BUILD_CLEANDEPEND" ]]
then
    echo -e "[4/9] Cleaning dependencies...\n"
    pushd deps/build > /dev/null
    rm -fr dep_*
    popd > /dev/null
    echo -e " ... done\n"
fi

if [[ -n "$BUILD_PRUSASLICER" ]]
then
    echo -e "[5/9] Configuring PrusaSlicer ...\n"
    if [[ -n $BUILD_WIPE ]]
    then
       echo -e "\n wiping build directory ...\n"
       rm -fr build
       echo -e "\n ... done"
    fi
    # mkdir build
    if [ ! -d "build" ]
    then
	mkdir build
    fi

    BUILD_ARGS=""
    if [[ -n "$FOUND_GTK3_DEV" ]]
    then
        BUILD_ARGS="-DSLIC3R_GTK=3"
    fi
    if [[ -n "$BUILD_DEBUG" ]]
    then
        BUILD_ARGS="${BUILD_ARGS} -DCMAKE_BUILD_TYPE=Debug"
    fi

    if [[ -n "$BUILD_TESTS" ]]
    then
        BUILD_ARGS="${BUILD_ARGS} -DCMAKE_BUILD_TESTS=1"
    else
        BUILD_ARGS="${BUILD_ARGS} -DCMAKE_BUILD_TESTS=0"
    fi

    # cmake
    pushd build > /dev/null
    cmake .. -DCMAKE_PREFIX_PATH="$PWD/../deps/build/destdir/usr/local" -DSLIC3R_STATIC=1 ${BUILD_ARGS}
    echo " ... done"
    # make PrusaSlicer
    echo -e "\n[6/9] Building PrusaSlicer ...\n"
    make -j$NCORES
    echo -e "\n ... done"

    echo -e "\n[7/9] Generating language files ...\n"
    #make .mo
    make gettext_po_to_mo

    popd  > /dev/null
    echo -e "\n ... done"

    # Give proper permissions to script
    chmod 755 $ROOT/build/src/BuildLinuxImage.sh

    pushd build  > /dev/null
    $ROOT/build/src/BuildLinuxImage.sh -a $FORCE_GTK2
    popd  > /dev/null
fi

if [[ -n "$BUILD_IMAGE" ]]
then
    # Give proper permissions to script
    chmod 755 $ROOT/build/src/BuildLinuxImage.sh
    pushd build  > /dev/null
    $ROOT/build/src/BuildLinuxImage.sh -i $FORCE_GTK2
    popd  > /dev/null
fi
