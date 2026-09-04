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
sudo apt install git build-essential autoconf cmake libglu1-mesa-dev libgtk-3-dev libdbus-1-dev libwebkit2gtk-4.1-dev texinfo
```
Adapt it for your package manager.
## 1. Build dependencies
From the repository root:
```bash
cd deps
mkdir build
cd build
cmake ..
cmake --build .
```

## 1.1 Ensure dependencies build succeeded
After the dependencies build finishes, run `cmake --build .` again. It should be close to instant, **complete successfully** and no additional work should be done. For example, using `make`, it should say something along the lines of:
```
make: Nothing to be done for 'all'.
```


## 2. Build PrusaSlicer
From the repository root:
```bash
mkdir build
cd build
cmake .. -DCMAKE_PREFIX_PATH="../deps/build/destdir/usr/local"
cmake --build .
```

## 3. Run tests (optional)
From the repository root:
```bash
cd build
ctest
```

## Advanced build options
The standard `CMAKE_BUILD_TYPE` option is supported. Furthermore there are several more build options provided by PrusaSlicer. These options have the `SLIC3R_` prefix and can be passed to cmake during in the `Build PrusaSlicer` step. E.g.:
```bash
cmake .. -DSLIC3R_ASAN=ON -DCMAKE_PREFIX_PATH="../deps/build/destdir/usr/local"
```
See the main CMakeLists.txt for the full list.
