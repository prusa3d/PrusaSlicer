
# Building PrusaSlicer on Mac OS

To build PrusaSlicer on Mac OS, you will need the following software:

- XCode
- CMake
- git
- gettext
- zlib
- m4

XCode is available through Apple's App Store, the other three tools are available on
[brew](https://brew.sh/) (use `brew install cmake git gettext zlib m4` to install them).

It may help to skim over this document's [Troubleshooting](#troubleshooting)](#troubleshooting) first, as you may find helpful workarounds documented there.

### Dependencies

PrusaSlicer comes with a set of CMake scripts to build its dependencies, it lives in the `deps` directory.
Open a terminal window and navigate to the PrusaSlicer sources directory.
Use the following commands to build the dependencies:

    cd deps
    mkdir build
    cd build
    cmake ..
    make

This will create a dependencies bundle inside the `build/destdir` directory.
You can also customize the bundle output path using the `-DDESTDIR=<some path>` option passed to `cmake`.

**Warning**: Once the dependency bundle is installed in a destdir, the destdir cannot be moved elsewhere.
(This is because wxWidgets hardcodes the installation path.)

FIXME The Cereal serialization library needs a tiny patch on some old OSX clang installations
https://github.com/USCiLab/cereal/issues/339#issuecomment-246166717


### Building PrusaSlicer

If dependencies are built without errors, you can proceed to build PrusaSlicer itself.
Go back to top level PrusaSlicer sources directory and use these commands:

    mkdir build
    cd build
    cmake .. -DCMAKE_PREFIX_PATH="$PWD/../deps/build/destdir/usr/local"

The `CMAKE_PREFIX_PATH` is the path to the dependencies bundle but with `/usr/local` appended - if you set a custom path
using the `DESTDIR` option, you will need to change this accordingly. **Warning:** the `CMAKE_PREFIX_PATH` needs to be an absolute path.

The CMake command above prepares PrusaSlicer for building from the command line.
To start the build, use

    make -jN

where `N` is the number of CPU cores, so, for example `make -j4` for a 4-core machine.

Alternatively, if you would like to use XCode GUI, modify the `cmake` command to include the `-GXcode` option:

    cmake .. -GXcode -DCMAKE_PREFIX_PATH="$PWD/../deps/build/destdir/usr/local"

and then open the `PrusaSlicer.xcodeproj` file.
This should open up XCode where you can perform build using the GUI or perform other tasks.

### Running Unit Tests

For the most complete unit testing, use the Debug build option `-DCMAKE_BUILD_TYPE=Debug` when running cmake.
Without the Debug build, internal assert statements are not tested.

To run all the unit tests:

    cd build
    make test

To run a specific unit test:

    cd build/tests/

The unit tests can be found by

    `ls */*_tests`

Any of these unit tests can be run directly e.g.

    `./fff_print/fff_print_tests`

### Note on Mac OS X SDKs

By default PrusaSlicer builds against whichever SDK is the default on the current system.

This can be customized. The `CMAKE_OSX_SYSROOT` option sets the path to the SDK directory location
and the `CMAKE_OSX_DEPLOYMENT_TARGET` option sets the target OS X system version (eg. `10.14` or similar).
Note you can set just one value and the other will be guessed automatically.
In case you set both, the two settings need to agree with each other. (Building with a lower deployment target
is currently unsupported because some of the dependencies don't support this, most notably wxWidgets.)

Please note that the `CMAKE_OSX_DEPLOYMENT_TARGET` and `CMAKE_OSX_SYSROOT` options need to be set the same
on both the dependencies bundle as well as PrusaSlicer itself.

Official macOS PrusaSlicer builds are currently (as of PrusaSlicer 2.5) built against SDK 10.12 to ensure compatibility with older Macs.

_Warning:_ XCode may be set such that it rejects SDKs bellow some version (silently, more or less).
This is set in the property list file

    /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Info.plist

To remove the limitation, simply delete the key `MinimumSDKVersion` from that file.

## Troubleshooting

### `CMath::CMath` target not found

At the moment (20.2.2024) PrusaSlicer cannot be built with CMake 3.28+. Use [CMake 3.27](https://github.com/Kitware/CMake/releases/tag/v3.27.9) instead. 
If you install the CMake application from [universal DMG](https://github.com/Kitware/CMake/releases/download/v3.27.9/cmake-3.27.9-macos-universal.dmg), you can invoke the CMake like this:

```
/Applications/CMake.app/Contents/bin/cmake
```

### Running `cmake -GXCode` fails with `No CMAKE_CXX_COMPILER could be found.` 

- If XCode command line tools wasn't already installed, run:
    ```
     sudo xcode-select --install
    ```
- If XCode command line tools are already installed, run:
    ```
    sudo xcode-select --reset
    ```

### Xcode keeps trying to install `m4` or the process complains about no compatible `m4` found.

Ensure the homebrew installed `m4` is in front of any other installed `m4` on your system.

_e.g._ `echo 'export PATH="/opt/homebrew/opt/m4/bin:$PATH"' >> ~/.bash_profile`

### `cmake` complains that it can't determine the build deployment target

If you see a message similar this, you can fix it by adding an argument like this `-DCMAKE_OSX_DEPLOYMENT_TARGET=14.5` to the `cmake` command. Ensure that you give it the macOS version that you are building for.

# TL; DR

Works on a fresh installation of MacOS Catalina 10.15.6

- Install [brew](https://brew.sh/):
- Open Terminal
    
- Enter:

```
brew update
brew install cmake git gettext
brew upgrade
git clone https://github.com/prusa3d/PrusaSlicer/
cd PrusaSlicer/deps
mkdir build
cd build
cmake ..
make
cd ../..
mkdir build
cd build
cmake .. -DCMAKE_PREFIX_PATH="$PWD/../deps/build/destdir/usr/local"
make
src/prusa-slicer
```
