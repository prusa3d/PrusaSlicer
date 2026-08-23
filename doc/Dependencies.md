# Dependency report for PrusaSlicer
## Possible dynamic linking on Linux
* zlib: Strict dependency required from the system, linked dynamically. Many other libs depend on zlib.
* wxWidgets >= 3.2
* libcurl
* tbb
* boost
* eigen
* glew
* expat
* openssl
* nlopt
* openvdb: This library depends on other libs, namely boost, zlib, openexr, blosc (not strictly), etc... 
* CGAL: Needs additional dependencies
    * MPFR 
    * GMP

## External libraries in source tree
* ad-mesh: Lots of customization, have to be bundled in the source tree.
* avrdude: Like ad-mesh, many customization, need to be in the source tree.
* clipper: An important library we have to have full control over it. We also have some slicer specific modifications.
* glu-libtess: This is an extract of the mesa/glu library not officially available as a package.
* imgui: no packages for debian, author suggests using in the source tree
* miniz: No packages, author suggests using in the source tree
* qhull: libqhull-dev does not contain libqhullcpp => link errors. Until it is fixed, we will use the builtin version. https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=925540
* semver: One module C library, author expects to use clib for installation. No packages.

## Header only
* igl
* nanosvg
* agg

## Built from source via deps
* catch2 (v3.8+): Compiled library (not header-only since v3). Built from source via `deps/+Catch2/`. Used for unit tests.


