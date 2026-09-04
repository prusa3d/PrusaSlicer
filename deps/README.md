# PrusaSlicer dependencies

This folder is a top level CMake project to build the dependent libraries for PrusaSlicer. It can be configured and built in the standard CMake way using the commands:

```
cmake .. 
cmake --build .
```

Each dependency is specified in a separate folder prefixed with the `+` sign. The underlying framework to download and build external projects is CMake's ExternalProject module. No other tool (e.g. Python) is involved in the process.
All the standard CMake switches work as expected, -G<generator> for alternative build file generators like Ninja or Visual Studio projects.
Important toolchain configuration variables are forwarded to each package build script if they happen to be CMake based. Otherwise they
are forwarded to the appropriate build system of a particular library package.

## A note about build configurations on MSVC

To build PrusaSlicer in different configurations with a Visual Studio toolchain, it is necessary to also build the dependencies in the appropriate configurations. As MSVC runtimes are not compatible between different build configurations, it's not possible to link a library built in Release mode to PrusaSlicer being built in Debug mode. This fact applies to all libraries except those with proper C linkage and interface lacking any STL container (e.g. ZLIB). Many of the dependent libraries don't fall into this cathegory thus they need to be built twice: in Release and Debug versions.

The `DEP_DEBUG` flag is used to specify if Debug versions of the affected libraries will be built. If an MSVC toochain is used, this flag is ON by default and OFF for any other platform and compiler suite. 

Note that it's not necessary to build the dependencies for each CMake build configuration (e.g. RelWithDebInfo). When PrusaSlicer is built in such a configuration, a pure Debug or Release build of dependencies will be compatible with the main project. CMake should automatically choose the right configuration of the dependencies in such cases. This may not work in all cases (see https://stackoverflow.com/questions/24262081/cmake-relwithdebinfo-links-to-debug-libs). 

## Automatic dependency build while configuring PrusaSlicer

It is possible build the dependencies while configuring the main PrusaSlicer project. To invoke this feature, configure PrusaSlicer with the `-DPrusaSlicer_BUILD_DEPS:BOOL=ON` flag. All the necessary arguments will be forwarded to the dependency build and the paths to finding the libraries (CMAKE_PREFIX_PATH) will automatically be set for the main project.

All that needs to be done to build the whole PrusaSlicer project from scratch is to use the command
```
cmake --preset default -DPrusaSlicer_BUILD_DEPS:BOOL=ON
```

in the top level source directory. This method makes use of presets which are a relatively new feature of CMake. To list the current available presets, use the
```
cmake --list-presets
```
command in the source directory where a CMakePresets.json file is available.