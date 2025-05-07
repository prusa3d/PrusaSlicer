
# Building PrusaSlicer on UNIX/Linux

Please understand that PrusaSlicer team cannot support compilation on all possible Linux distros. Namely, we cannot help troubleshoot OpenGL driver issues or dependency issues if compiled against distro provided libraries. **We can only support PrusaSlicer statically linked against the dependencies compiled with the `deps` scripts**, the same way we compile PrusaSlicer for our [binary builds](https://github.com/prusa3d/PrusaSlicer/releases).

If you have some reason to link dynamically to your system libraries, you are free to do so, but we can not and will not troubleshoot any issues you possibly run into.

Instead of compiling PrusaSlicer from source code, one may also consider to install PrusaSlicer [pre-compiled by contributors](https://github.com/prusa3d/PrusaSlicer/wiki/PrusaSlicer-on-Linux---binary-distributions).

## Step by step guide

This guide describes building PrusaSlicer statically against dependencies pulled by our `deps` script. Running all the listed commands in order should result in successful build.

#### 0. Prerequisities

You need at least 8GB of RAM on your system. Linking on a 4GB RAM system will likely fail and you may need to limit the number of compiler processes with the '-j xxx' make or ninja parameter, where 'xxx' is the number of compiler processes launched if running on low RAM multi core system, for example on Raspberry PI.

GNU build tools, CMake, git and other libraries have to be installed on the build machine.
Unless that's already the case, install them as usual from your distribution packages.
E.g. on Ubuntu 24.04 / Debian 12, run
```shell
sudo apt-get install  -y \
git \
build-essential \
autoconf \
cmake \
libglu1-mesa-dev \
libgtk-3-dev \
libdbus-1-dev \
libwebkit2gtk-4.1-dev \
texinfo
```
The names of the packages may be different on different distros.

#### 1. Cloning the repository


Cloning the repository is simple thanks to git and Github. Simply `cd` into wherever you want to clone PrusaSlicer code base and run
```
git clone https://www.github.com/prusa3d/PrusaSlicer
cd PrusaSlicer
```
This will download the source code into a new directory and `cd` into it. You can now optionally select a tag/branch/commit to build using `git checkout`. Otherwise, `master` branch will be built.
The path to the build directory must not contain spaces - this scenario is not supported by the build scripts.


#### 2. Building dependencies

PrusaSlicer uses CMake and the build is quite simple, the only tricky part is resolution of dependencies. The supported and recommended way is to build the dependencies first and link to them statically. PrusaSlicer source base contains a CMake script that automatically downloads and builds the required dependencies. All that is needed is to run the following (from the top of the cloned repository):

    cd deps
    mkdir build
    cd build
    cmake .. -DDEP_WX_GTK3=ON
    make
    cd ../..


**Warning**: Once the dependency bundle is installed in a destdir, the destdir cannot be moved elsewhere. This is because wxWidgets hardcode the installation path.


#### 3. Building PrusaSlicer

Now when the dependencies are compiled, all that is needed is to tell CMake that we are interested in static build and point it to the dependencies. From the top of the repository, run

    mkdir build
    cd build
    cmake .. -DSLIC3R_STATIC=1 -DSLIC3R_GTK=3 -DSLIC3R_PCH=OFF -DCMAKE_PREFIX_PATH=$(pwd)/../deps/build/destdir/usr/local
    make -j4

And that's it. It is now possible to run the freshly built PrusaSlicer binary:

    cd src
    ./prusa-slicer

#### 4. Running Unit Tests

For the most complete unit testing, use the Debug build option `-DCMAKE_BUILD_TYPE=Debug` when running cmake.
Without the Debug build, internal assert statements are not tested.

To run the unit tests:

    cd build
    make test

To run a specific unit test:

    cd build/tests/

The unit tests can be found by

    `ls */*_tests`

Any of these unit tests can be run directly e.g.

    `./fff_print/fff_print_tests`

## Useful CMake flags when building dependencies

- `-DDESTDIR=<target destdir>` allows to specify a directory where the dependencies will be installed. When not provided, the script creates and uses `destdir` directory where cmake is run.

- `-DDEP_DOWNLOAD_DIR=<download cache dir>` specifies a directory to cache the downloaded source packages for each library. Can be useful for repeated builds, to avoid unnecessary network traffic.

- `-DDEP_WX_GTK3=ON` builds wxWidgets (one of the dependencies) against GTK3 (defaults to OFF)


## Useful CMake flags when building PrusaSlicer
- `-DSLIC3R_ASAN=ON` enables gcc/clang address sanitizer (defaults to `OFF`, requires gcc>4.8 or clang>3.1)
- `-DSLIC3R_GTK=3` to use GTK3 (defaults to `2`). Note that wxWidgets must be built against the same GTK version.
- `-DSLIC3R_STATIC=ON` for static build (defaults to `OFF`)
- `-DCMAKE_BUILD_TYPE=Debug` to build in debug mode (defaults to `Release`)
- `-DSLIC3R_GUI=no` to build the console variant of PrusaSlicer

See the CMake files to get the complete list.



## Building dynamically

As already mentioned above, dynamic linking of dependencies is possible, but PrusaSlicer team is unable to troubleshoot (Linux world is way too complex). Feel free to do so, but you are on your own. Several remarks though:

The list of dependencies can be easily obtained by inspecting the CMake scripts in the `deps/` directory. Some of the dependencies don't have to be as recent as the versions listed - generally versions available on conservative Linux distros such as Debian stable, Ubuntu LTS releases or Fedora are likely sufficient. If you decide to build this way, it is your responsibility to make sure that CMake finds all required dependencies. It is possible to look at your distribution PrusaSlicer package to see how the package maintainers solved the dependency issues.

Note that you may need to use wxGTK with disabled EGL support for PrusaSlicer to work correctly: see [#9774](https://github.com/prusa3d/PrusaSlicer/issues/9774).

## Miscellaneous

### Installation

At runtime, PrusaSlicer needs a way to access its resource files. By default, it looks for a `resources` directory relative to its binary.

If you instead want PrusaSlicer installed in a structure according to the File System Hierarchy Standard, use the `SLIC3R_FHS` flag

    cmake .. -DSLIC3R_FHS=1

This will make PrusaSlicer look for a fixed-location `share/slic3r-prusa3d` directory instead (note that the location becomes hardcoded).

You can then use the `make install` target to install PrusaSlicer.

### Desktop Integration (PrusaSlicer 2.4 and newer)

If PrusaSlicer is to be distributed as an AppImage or a binary blob (.tar.gz and similar), then a desktop integration support is compiled in by default: PrusaSlicer will offer to integrate with desktop by manually copying the desktop file and application icon into user's desktop configuration. The built-in desktop integration is also handy on Crosstini (Linux on Chrome OS).

If PrusaSlicer is compiled with `SLIC3R_FHS` enabled, then a desktop integration support will not be integrated. One may want to disable desktop integration by running
    
    cmake .. -DSLIC3R_DESKTOP_INTEGRATION=0
    
when building PrusaSlicer for flatpack or snap, where the desktop integration is performed by the installer.
