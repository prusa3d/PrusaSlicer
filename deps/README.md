# PrusaSlicer dependencies

This folder is a top level CMake project to build the dependent libraries for PrusaSlicer. To see how to build the dependencies, refer to the [build documentation](doc/Build.md).
The following text provides additional technical details that are not required for a standard build.

Each dependency is specified in a separate folder prefixed with the `+` sign. The underlying framework to download and build external projects is CMake's ExternalProject module. No other tool (e.g. Python) is involved in the process.
All the standard CMake switches work as expected, -G<generator> for alternative build file generators like Ninja or Visual Studio projects.
Important toolchain configuration variables are forwarded to each package build script if they happen to be CMake based. Otherwise they
are forwarded to the appropriate build system of a particular library package.

## A note about build configurations on MSVC

To build PrusaSlicer in different configurations with a Visual Studio toolchain, it is necessary to also build the dependencies in the appropriate configurations. As MSVC runtimes are not compatible between different build configurations, it's not possible to link a library built in Release mode to PrusaSlicer being built in Debug mode. This fact applies to all libraries except those with proper C linkage and interface lacking any STL container (e.g. ZLIB). Many of the dependent libraries don't fall into this cathegory thus they need to be built twice: in Release and Debug versions.

The `DEP_DEBUG` flag is used to specify if Debug versions of the affected libraries will be built. If an MSVC toochain is used, this flag is ON by default and OFF for any other platform and compiler suite. 

Note that it's not necessary to build the dependencies for each CMake build configuration (e.g. RelWithDebInfo). When PrusaSlicer is built in such a configuration, a pure Debug or Release build of dependencies will be compatible with the main project. CMake should automatically choose the right configuration of the dependencies in such cases. This may not work in all cases (see https://stackoverflow.com/questions/24262081/cmake-relwithdebinfo-links-to-debug-libs). 
