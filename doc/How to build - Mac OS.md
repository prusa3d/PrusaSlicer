
# Building PrusaSlicer on macOS

To build PrusaSlicer on macOS, you will need Xcode, which is available through Apple's App Store. In addition, you will need couple of other tools, all of which are available through [brew](https://brew.sh/): use

```
brew update
brew install automake cmake git gettext libtool texinfo m4 zlib
brew upgrade
```

to install them.

It may help to skim over this document's [Troubleshooting](#troubleshooting) first, as you may find helpful workarounds documented there.

### Building Dependencies and PrusaSlicer

PrusaSlicer uses CMake presets to automatically build all dependencies and then configure PrusaSlicer.

Open a terminal window, navigate to the PrusaSlicer sources directory, and run:

    cmake --preset default -DPrusaSlicer_BUILD_DEPS=ON
    cmake --build build-default -j$(sysctl -n hw.ncpu)

This will first build all dependencies, then configure and build PrusaSlicer itself.
The build output will be in the `build-default` directory.

**Warning**: Once the dependency bundle is installed, it cannot be moved elsewhere.
(This is because wxWidgets hardcodes the installation path.)

Alternatively, if you would like to use Xcode GUI, add the `-GXcode` option:

    cmake --preset default -DPrusaSlicer_BUILD_DEPS=ON -GXcode

and then open the `build-default/PrusaSlicer.xcodeproj` file.
This should open up Xcode where you can perform build using the GUI or perform other tasks.

### Running Unit Tests

For the most complete unit testing, use the Debug build option `-DCMAKE_BUILD_TYPE=Debug` when running cmake.
Without the Debug build, internal assert statements are not tested.

To run all the unit tests:

    cd build-default
    make test

To run a specific unit test:

    cd build-default/tests/

The unit tests can be found by

    `ls */*_tests`

Any of these unit tests can be run directly e.g.

    `./fff_print/fff_print_tests`

### Note on macOS SDKs

By default PrusaSlicer builds against whichever SDK is the default on the current system.

This can be customized. The `CMAKE_OSX_SYSROOT` option sets the path to the SDK directory location
and the `CMAKE_OSX_DEPLOYMENT_TARGET` option sets the target OS X system version (eg. `10.14` or similar).
Note you can set just one value and the other will be guessed automatically.
In case you set both, the two settings need to agree with each other. (Building with a lower deployment target
is currently unsupported because some of the dependencies don't support this, most notably wxWidgets.)

Please note that the `CMAKE_OSX_DEPLOYMENT_TARGET` and `CMAKE_OSX_SYSROOT` options need to be set the same
on both the dependencies bundle as well as PrusaSlicer itself.

Official upstream PrusaSlicer builds historically targeted SDK 10.12. This fork targets SDK 14.0+ (macOS Sonoma and later).

_Warning:_ Xcode may be set such that it rejects SDKs bellow some version (silently, more or less).
This is set in the property list file

    /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Info.plist

To remove the limitation, simply delete the key `MinimumSDKVersion` from that file.

## Troubleshooting

### Homebrew package conflicts

Homebrew versions of `boost`, `eigen`, or `glew` can conflict with the dependencies built by PrusaSlicer.
If you encounter cmake errors related to these packages, uninstall them:

```
brew uninstall boost eigen glew
```

The deps build will compile the correct versions from source.

### Running `cmake -GXcode` fails with `No CMAKE_CXX_COMPILER could be found.`

- If Xcode command line tools wasn't already installed, run:
    ```
     sudo xcode-select --install
    ```
- If Xcode command line tools are already installed, run:
    ```
    sudo xcode-select --reset
    ```

### Xcode keeps trying to install `m4` or the process complains about no compatible `m4` found.

Ensure the homebrew installed `m4` is in front of any other installed `m4` on your system.

_e.g._ `echo 'export PATH="/opt/homebrew/opt/m4/bin:$PATH"' >> ~/.zprofile` (or `~/.bash_profile` if using bash)

### `cmake` complains that it can't determine the build deployment target

If you see a message similar this, you can fix it by adding an argument like this `-DCMAKE_OSX_DEPLOYMENT_TARGET=14.5` to the `cmake` command. Ensure that you give it the macOS version that you are building for.

# TL;DR

Works on a fresh installation of macOS Sequoia 15.5

- Install [brew](https://brew.sh/):
- Open Terminal

- Enter:

```
brew update
brew install automake cmake git gettext libtool texinfo m4 zlib
brew upgrade
git clone https://github.com/hyiger/PrusaSlicer.git
cd PrusaSlicer
cmake --preset default -DPrusaSlicer_BUILD_DEPS=ON
cmake --build build-default -j$(sysctl -n hw.ncpu)
build-default/src/prusa-slicer
```
