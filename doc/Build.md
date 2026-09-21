# Building from source
These are the build steps for Windows, MacOS and Linux. Depending on your hardware the dependencies compilation may take a significant amount of time (hours). PrusaSlicer compilation is also not instant. 

## 0. Prerequisites

### Windows
 - `Microsoft Visual Studio`
 - `CMake`
 - `git`.
### MacOS
```bash
brew update && brew install automake cmake git gettext libtool texinfo
```
### Linux
For example on Ubuntu 26.04:
```bash
sudo apt install git build-essential autoconf cmake libtool libglu1-mesa-dev libgtk-3-dev libdbus-1-dev libwebkit2gtk-4.1-dev texinfo
```
or for Fedora 44:
```bash
sudo dnf install cmake g++ git-core m4 texinfo autoconf automake libtool perl-FindBin perl-lib perl-IPC-Cmd perl-Time-Piece webkit2gtk4.1-devel zlib-devel zlib-static libpng-static
```
Adapt it for your package manager and packages provided by your operating system.
## 1. Build dependencies
From the repository root:
```bash
cmake -S deps -B deps/build
cmake --build deps/build
```

## 1.1 Ensure dependencies build succeeded
After the dependencies build finishes, run `cmake --build deps/build` again. It should be close to instant, **complete successfully** and no additional work should be done. For example, using `make`, it should say something along the lines of:
```
make: Nothing to be done for 'all'.
```

## 2. Build PrusaSlicer
From the repository root:

- **Unix shells or PowerShell:** use `$PWD` for the current directory
```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH="$PWD/deps/build/destdir/usr/local"
cmake --build build
```

- **Windows `cmd.exe`**: use `%CD%` for the current directory
```cmd
cmake -S . -B build -DCMAKE_PREFIX_PATH="%CD%/deps/build/destdir/usr/local"
cmake --build build
```

## 3. Run tests (optional)
From the repository root:
```bash
ctest --test-dir build
```

## Advanced build options
The standard `CMAKE_BUILD_TYPE` option is supported. Furthermore there are
several more build options provided by PrusaSlicer. These options have the
`SLIC3R_` prefix and can be passed to cmake during in the `Build PrusaSlicer`
step. E.g.:
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DSLIC3R_ASAN=ON -DCMAKE_PREFIX_PATH="$PWD/../deps/build/destdir/usr/local"
```
See the main CMakeLists.txt for the full list.


## Troubleshooting
There is a known error that may occur while extracting Boost when executing the `cmake
--build deps/build` command. It may happen
in sandboxed environment, such as a docker container. It can look
similar to this:
```
CMake Error: Problem with archive_read_next_header(): Pathname cannot be converted from UTF-8 to current locale.
CMake Error: Problem extracting tar: /workspace/deps/temp_build/downloads/Boost/boost-1.86.0-cmake.zip
-- extracting... [error clean up]
CMake Error at dep_Boost-stamp/extract-dep_Boost.cmake:40 (message):
  Extract of
  '/workspace/deps/temp_build/downloads/Boost/boost-1.86.0-cmake.zip' failed
```

Simply running `cmake` with overridden locale env vars *usually* fixes the issue:
```
LC_ALL=C.UTF-8 cmake --build deps/build
```
